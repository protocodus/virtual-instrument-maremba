#pragma once

#include "MarimbaModel.h"
#include "MarembaVoice.h"
#include "MicMixer.h"
#include "AnalogPreamp.h"
#include "Compressor.h"
#include "Oversampler.h"
#include "RandomArtifacts.h"
#include "SympatheticMesh.h"
#include "FrameBody.h"

#include <array>
#include <cstdint>

namespace maremba {

// Physical (already de-normalised) engine parameters. The Rack wrapper maps
// the 0..1 motherboard values into these units; the engine clamps every field
// again, so any finite value is safe.
struct EngineParameters {
    int model = 0;                  // 0: Rosewood, 1: Padauk, 2: Balafon, 3: Kalimba
    int malletType = 1;             // 0: Soft Yarn/Thumb Flesh, 1: Medium Cord/Natural Thumb, 2: Hard Rubber/Thumb Nail, 3: Wood Baton/Thumb Pick
    float malletHardness = 0.40f;   // 0..1
    float strikePosition = 0.50f;   // 0..1
    float resonatorTune = 0.0f;     // cents, -50..+50
    float resonatorCoupling = 0.70f;// 0..1
    float decay = 1.00f;            // decay multiplier, 0.1..8.0 (x the model/register T60)
    float buzzAmount = 0.15f;       // 0..1
    float artifacts = 0.35f;        // 0..1

    // Premium physical modeling phenomena
    float sympathetic = 0.40f;      // 0..1 inter-bar sympathetic resonance halo
    float pitchGlide = 0.30f;       // 0..1 tension-modulation pitch attack
    float bodyBloom = 0.50f;        // 0..1 frame & soundboard body

    float closeLevel = 0.85f;       // 0..1
    float farLevel = 0.45f;         // 0..1
    float piezoLevel = 0.30f;       // 0..1
    float stereoWidth = 1.0f;       // 0..2 (1 = natural), applied to the main bus

    float preampDrive = 0.15f;      // 0..1
    float warmth = 0.10f;           // -1..1 tilt
    float compAmount = 0.25f;       // 0..1
    float compAttack = 10.0f;       // ms, 1..50
    float compRelease = 120.0f;     // ms, 20..500

    float volume = 0.80f;           // 0..1 (knob + volume CV, before the squared law)
    int oversampling = 0;           // 0: 2x, 1: 4x, 2: 8x (literal factors at every host rate)
    int velocityCurve = 1;          // 0: Soft (sqrt, light touch plays loud), 1: Linear, 2: Hard (v^1.55, needs force), 3: Expressive
    int polyphony = 1;              // 0: 8, 1: 16, 2: 24 voices
    float tuneCents = 0.0f;         // static tuning offset in cents: device Master Tune knob (+/-100) + Reason global master tune (+/-100)
    float detune = 0.0f;            // 0..100: per-key drift (0 = exact, 100 = up to +/-100 cents per key)
    float strikeJitter = 15.0f;     // 0..100: stochastic strike variance
};

class MarembaEngine {
public:
    static constexpr int kMaxPolyphony = 24;   // highest Polyphony setting
    static constexpr int kVoicePoolSize = 32;  // slots; spares let stolen/excess voices fade out instead of being cut
    static constexpr int kMaxFrames = 64;      // largest frameCount RenderBatch accepts

    // Allocates everything the engine will ever need (delay lines are sized for
    // 8x the construction sample rate; the host re-creates the native object
    // when the system rate changes). Must be called outside the realtime thread.
    explicit MarembaEngine(double sampleRate = 44100.0);

    // request_reset_audio: silence every voice and tail, and reseed every PRNG so
    // that a render after Reset() is identical to a render of a fresh engine with
    // the same parameters.
    void Reset();

    // Realtime-safe, allocation-free and idempotent. Cheap when nothing changed:
    // rate-, model- and tuning-dependent components are reconfigured only when
    // their inputs change. A change of the oversampling factor fades all sound
    // out over a few milliseconds and then switches the internal rate cleanly.
    void SetParameters(const EngineParameters& params);
    const EngineParameters& GetParameters() const { return mParams; }

    // Pitch bend in cents (the wrapper maps the wheel to +/-200). Applies to
    // ringing voices and to the sympathetic mesh as well as to new notes.
    void SetPitchBendCents(float cents);

    void NoteOn(int noteNumber, float velocity); // velocity 0..1; <= 0 is a note-off
    void NoteOff(int noteNumber);                // bars ring freely by design: no damping
    void DampAll();                              // transport stop: gentle per-model hand damping of every sounding voice

    int GetActiveVoiceCount() const;             // voices that are still sounding (including fading ones)

    // True when no voice is sounding and every tail (mesh, body, room, filters,
    // decimators) has decayed below audibility and been flushed to zero. The
    // caller may then skip RenderBatch and leave the outputs unwritten.
    bool IsSilent() const;

    // Render frameCount (1..kMaxFrames) frames. Any of the direct-out pointers
    // may be null, in which case that output is neither decimated nor written.
    // Outputs are always finite.
    void RenderBatch(float* outMainL, float* outMainR,
                     float* outCloseL, float* outCloseR,
                     float* outFarL, float* outFarR,
                     float* outPiezo,
                     int frameCount);

private:
    static constexpr int kMaxOversample = 8;
    static constexpr int kControlInterval = 16;      // output frames between control-rate updates
    static constexpr int kMaxPendingNotes = 32;
    static constexpr int kNumDirectOuts = 5;         // close L/R, far L/R, piezo

    struct PendingNote {
        int note;
        float velocity;
    };

    static EngineParameters Sanitize(const EngineParameters& params);
    static int FactorFor(int oversampling) { return oversampling == 1 ? 4 : oversampling == 2 ? 8 : 2; }
    static int VoiceLimitFor(int polyphony) { return polyphony == 0 ? 8 : polyphony == 1 ? 16 : 24; }
    static uint32_t VoiceSeed(int slot);

    double InternalRate() const { return mBaseSampleRate * mActiveFactor; }
    float GlobalCents() const { return mParams.tuneCents + mBendCents; }
    float MapVelocity(float vel) const;
    void ConfigureForRate();           // rate-dependent coefficients (no allocation)
    void ConfigureModel();             // model-dependent coefficients
    void ApplyGlobalTuning();          // retune ringing voices + mesh when tune/bend changed
    void FlushTails();                 // clear every shared tail (mesh, body, room, dynamics, decimators)
    void ApplyFactorSwitch();          // end of the oversampling fade: reset and switch the rate
    int TriggerNote(int noteNumber, float velocity);   // returns the slot used
    int CountLiveVoices() const;
    int FindStealVictim() const;
    void EnforcePolyphony();
    void ControlTick();

    double mBaseSampleRate = 44100.0;
    EngineParameters mParams;
    float mBendCents = 0.0f;
    float mAppliedGlobalCents = 0.0f;
    int mActiveFactor = 2;
    int mConfiguredModel = -1;
    bool mMeshRunning = false;
    bool mBodyRunning = false;
    bool mRoomRunning = false;

    // Oversampling change: fade out at the old rate, then reset and switch
    bool mSwitchPending = false;
    float mSwitchGain = 1.0f;
    float mSwitchStep = 0.0f;
    std::array<PendingNote, kMaxPendingNotes> mPendingNotes{};
    int mPendingCount = 0;
    int mDampedPendingCount = 0;       // queued notes that DampAll must reach when they start

    // Master volume smoothing (per output frame)
    float mVolumeGain = 0.0f;
    float mVolumeAlpha = 0.0f;

    // Control-rate clock and silence tracking
    int mControlCounter = 0;
    int mQuietFrames = 0;
    int mQuietFramesNeeded = 4410;
    bool mTailsSilent = true;

    std::array<MarembaVoice, kVoicePoolSize> mVoices;
    MicMixer mMicMixer;
    AnalogPreamp mPreamp;
    Compressor mCompressor;
    StereoOversampler mOversampler;
    std::array<MonoOversampler, kNumDirectOuts> mDirectDecimators;
    std::array<bool, kNumDirectOuts> mDirectRunning{};
    SympatheticMesh mSympathetic;
    FrameBody mFrameBody;
    FastPRNG mPrng;

    // Sub-sample buffers for oversampling decimation
    float mSubBufMainL[kMaxOversample];
    float mSubBufMainR[kMaxOversample];
    float mSubBufDirect[kNumDirectOuts][kMaxOversample];
};

} // namespace maremba
