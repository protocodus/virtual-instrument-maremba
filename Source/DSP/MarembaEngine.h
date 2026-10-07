#pragma once

#include "MarimbaModel.h"
#include "MarembaVoice.h"
#include "CaptureScene.h"
#include "AnalogPreamp.h"
#include "AnalogLimiter.h"
#include "OutputHeadroom.h"
#include "Oversampler.h"
#include "RandomArtifacts.h"
#include "SympatheticMesh.h"
#include "FrameBody.h"
#include "KalimbaBody.h"
#include "Tunings.h"

#include <array>
#include <cstdint>

namespace maremba {

// Physical (already de-normalised) engine parameters. The Rack wrapper maps
// the 0..1 motherboard values into these units; the engine clamps every field
// again, so any finite value is safe.
struct EngineParameters {
    int model = 0;                  // 0: Rosewood, 1: Padauk, 2: Balafon, 3: Kalimba
    int malletType = 1;             // 0: Soft Yarn/Thumb Flesh, 1: Medium Cord/Natural Thumb, 2: Hard Rubber/Thumb Nail, 3: Wood Baton/Thumb Pick,
                                    // 4: Mallet Shaft/Nail Tap
    float malletHardness = 0.40f;   // 0..1
    float strikePosition = 0.50f;   // 0..1
    float resonatorTune = 0.0f;     // cents, -50..+50
    float resonatorCoupling = 0.70f;// 0..1
    float decay = 1.00f;            // nominal 0.1..8.0 control; Kalimba compresses 1..8 to 1x..2x sustain
    float buzzAmount = 0.15f;       // stored knob 0..1; voice maps to physical 0..0.7
    float artifacts = 0.35f;        // 0..1

    // Shared acoustic response
    float sympathetic = 0.40f;      // 0..1 inter-bar sympathetic resonance halo
    float pitchGlide = 0.30f;       // legacy input, ignored and sanitized to 0
    float bodyBloom = 0.50f;        // 0..1 frame & soundboard body

    float closeLevel = 0.85f;       // 0..1 independent capture coefficient
    float farLevel = 0.45f;         // 0..1 Room knob; squared coefficient on the complete capture
    int roomType = 0;              // 0: Wood (legacy), 1: Chamber FDN, 2: Hall tank
    float piezoLevel = 0.30f;       // 0..1 independent capture coefficient
    float stereoWidth = 1.0f;       // 0..2 (1 = natural), applied to the main bus

    float preampDrive = 0.05f;      // 0..1 Piezo amplifier only; legacy stored name
    float warmth = 0.10f;           // -1..1 tilt
    float compAmount = 0.05f;       // 0..1 analog limiter; legacy stored name
    float compAttack = 10.0f;       // main limiter control attack, ms, 1..50
    float compRelease = 120.0f;     // initial limiter recovery, ms, 20..500

    float volume = 0.68f;           // 0..1 (knob + volume CV, before the squared law)
    int oversampling = 0;           // 0: 2x, 1: 4x, 2: 8x (literal factors at every host rate)
    int velocityCurve = 3;          // 0: Soft (sqrt), 1: Linear, 2: Hard (v^1.55), 3: Sigmoid (smoothstep)
    int polyphony = 1;              // 0: 8, 1: 16, 2: 24 voices
    float tuneCents = 0.0f;         // static tuning offset in cents: device Master Tune knob (+/-100) + Reason global master tune (+/-100)
    float detune = 0.0f;            // 0..100: per-key drift (0 = exact, 100 = up to +/-100 cents per key)
    float strikeJitter = 15.0f;     // 0..100: stochastic strike variance
    float velocityToHardness = 0.20f; // 0..1: added fraction of the control's full range
    float velocityToPosition = 0.0f;  // 0..1 depth, applied per strike after the velocity curve
    float velocityToDecay = 0.0f;     // MIDI 1 adds zero; MIDI 127 adds depth in normalized Decay travel
    // The same per-strike law for seven more controls; 0 leaves them fixed.
    float velocityToCarving = 0.0f;
    float velocityToCoupling = 0.0f;
    float velocityToBloom = 0.0f;       // the strike's drive into the frame (or kalimba box) resonances
    float velocityToSympathetic = 0.0f; // the strike's drive into the ringing-along keys
    float velocityToBuzz = 0.0f;
    float velocityToTension = 0.0f;
    float velocityToArtifacts = 0.0f;

    int tuningScale = 0;            // ETuningScale: Equal, Bala, Timbila, Pentatonic, Sambla, Gyil, Valimba
    int noteOffMode = 0;            // 0: Ring (bars ring freely), 1: Damp (hand/finger stops the key),
                                    // 2: Dead (the mallet or thumb stays on the key while it is held)
    float modWheel = 0.0f;          // 0..1: roll speed on mallet models, hole cover on Kalimba
    float carving = 1.0f;           // 0..1: uncarved bars / stiff tine profile .. keys as built
    float membraneTension = 0.5f;   // 0..1: supports and buzz attachments slack .. tight (.5 as voiced)
};

class MarembaEngine {
public:
    static constexpr int kMaxPolyphony = 24;   // highest Polyphony setting
    // Slots: spares let stolen/excess voices fade out instead of being cut. Twice
    // the highest polyphony covers the worst burst within one steal fade (24 live
    // voices each fading for 24 new notes) and a 24 -> 8 polyphony drop.
    static constexpr int kVoicePoolSize = 2 * kMaxPolyphony;
    static constexpr int kMaxFrames = 64;      // largest frameCount RenderBatch accepts

    // Calibration of the direct outputs: a capture's fixed gain is its trim times
    // CaptureScene::kMasterMixScale. Volume, the Level knobs, Width, Warmth and
    // Limit shape Main only and never reach a direct output. The calibration and
    // its evidence are in MarembaEngine.cpp and docs/OUTPUT_LEVEL.md.
    static constexpr float kCloseStemTrim = 0.28f;
    static constexpr float kRoomStemTrim = 0.385f;
    static constexpr float kPiezoStemTrim = 0.49f;
    // Both families use 70% of their original fixed calibration. The direct
    // outputs retain independent guards and never follow Main controls.
    static constexpr float kKalimbaCloseStemTrim = 0.56f;
    static constexpr float kKalimbaRoomStemTrim = 0.546f;
    static constexpr float kKalimbaPiezoStemTrim = 0.581f;

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
    void NoteOff(int noteNumber);                // Ring: bars ring freely; Damp: the key is stopped; Dead: the mallet lifts
    void DampAll();                              // transport stop: gentle per-model hand damping of every sounding voice

    int GetActiveVoiceCount() const;             // voices that are still sounding (including fading ones)

    // Mod-wheel roll strokes struck since the last call (the wrapper lights
    // its Note lamp for them as for any strike).
    int TakeRollStrokes() { const int strokes = mRollStrokes; mRollStrokes = 0; return strokes; }

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
    friend struct MarembaEngineTestAccess;   // unit tests only (Tests/test_dsp.cpp)

    static constexpr int kMaxOversample = 8;
    static constexpr int kControlInterval = 16;      // output frames between control-rate updates
    static constexpr int kMaxPendingNotes = 32;
    static constexpr int kNumDirectOuts = 5;         // close L/R, far L/R, piezo

    struct PendingNote {
        int note;
        float velocity;
        bool released;   // key released while queued: the voice starts released
    };

    static EngineParameters Sanitize(const EngineParameters& params);
    static int FactorFor(int oversampling) { return oversampling == 1 ? 4 : oversampling == 2 ? 8 : 2; }
    static int VoiceLimitFor(int polyphony) { return polyphony == 0 ? 8 : polyphony == 1 ? 16 : 24; }
    static uint32_t VoiceSeed(int slot);

    double InternalRate() const { return mBaseSampleRate * mActiveFactor; }
    float GlobalCents() const { return mParams.tuneCents + mBendCents; }
    float MapVelocity(float vel) const;
    float MapControlVelocity(float vel) const;
    void ConfigureForRate();           // rate-dependent coefficients (no allocation)
    void ConfigureModel();             // model-dependent coefficients
    void ApplyGlobalTuning();          // retune ringing voices + mesh when tune/bend changed
    void ConfigureSympathetic(float globalCents);
    void FlushTails(bool resetRoomMotion = true); // clear shared audio; optionally retain slow room motion
    void ApplyFactorSwitch();          // end of the oversampling fade: reset and switch the rate
    void CancelFactorSwitch();         // oversampling set back to the active factor during the fade
    void StartPendingNotes();          // notes queued during the fade start now
    int TriggerNote(int noteNumber, float velocity, float positionOffset = 0.0f);   // returns the slot used
    void AdvanceRolls();               // mod-wheel rolls on held keys (mallet models)
    bool RollsPending() const;
    void ApplyKalimbaAir();
    int CountLiveVoices() const;
    int FindStealVictim() const;
    void EnforcePolyphony();
    void ControlTick();
    void SnapLiveControls();
    void SmoothLiveControls();
    // Close, Room and Piezo trims of an instrument (see kCloseStemTrim above).
    static std::array<float, 3> StemTrimsFor(int model);

    double mBaseSampleRate = 44100.0;
    EngineParameters mParams;
    float mBendCents = 0.0f;
    float mAppliedGlobalCents = 0.0f;
    int mActiveFactor = 2;
    int mConfiguredModel = -1;
    int mConfiguredScale = -1;
    bool mMeshRunning = false;
    bool mBodyRunning = false;
    bool mBodyVelocityActive = false, mMeshVelocityActive = false;
    bool mBodyVelocityRunning = false, mMeshVelocityRunning = false;

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
    // Five-millisecond output-rate smoothing for continuously audible controls.
    // Strike/contact parameters deliberately stay latched to the next note.
    float mBodyGain = 0.0f, mSympatheticGain = 0.0f;
    float mCloseGain = 0.0f, mFarGain = 0.0f, mPiezoGain = 0.0f;
    float mStereoWidth = 1.0f, mDrive = 0.0f;
    // The direct outputs' trims for the current Model: exact when idle or after
    // a reset, and smoothed like the controls above when Model changes mid-ring.
    std::array<float, 3> mStemTrim{};

    // Control-rate clock and silence tracking
    int mControlCounter = 0;
    int mQuietFrames = 0;
    int mQuietFramesNeeded = 4410;
    bool mTailsSilent = true;

    std::array<MarembaVoice, kVoicePoolSize> mVoices;
    // Each slot's latest strike: additive normalized offsets for shared/live
    // controls, and absolute air coupling into the kalimba box. Offsets stay
    // latched until a restrike; moving a base knob keeps that strike's offset.
    struct StrikeRoute {
        float body = 0.0f, sympathetic = 0.0f, coupling = 0.0f;
        float boxAir = 1.0f, buzz = 0.0f, tension = 0.0f;
    };
    std::array<StrikeRoute, kVoicePoolSize> mRoutes{};
    std::array<RadiationCacheEntry, 128> mRadiationCache{};
    std::array<float, 128> mKeyDetune{};   // each key's Detune offset (cents), for the sympathetic keys
    CaptureScene mMicMixer;
    AnalogPreamp mPreamp;
    AnalogLimiter mLimiter;
    OutputHeadroom mOutputHeadroom;
    MonoOversampler mPiezoDecimator;
    std::array<bool, kNumDirectOuts> mDirectRunning{};
    SympatheticMesh mSympathetic;
    FrameBody mFrameBody;
    SympatheticMesh mVelocitySympathetic;
    FrameBody mVelocityFrameBody;
    KalimbaBody mKalimbaBody;
    PiezoPickup mKalimbaPickup;
    std::array<double, 128> mTineSide{};   // each note's tine across the bridge (KalimbaTineSide)
    bool mKalimbaBodyRunning = false;
    bool mKalimbaPickupRunning = false;
    float mCover = 0.0f;               // smoothed kalimba hole cover
    FastPRNG mPrng;
    FastPRNG mRollPrng;

    // Held keys and their roll clocks (output frames).
    struct KeyState {
        float velocity = 0.0f;
        bool held = false;
        bool rolling = false;
        int hand = 0;
        double next = 0.0;
    };
    std::array<KeyState, 128> mKeys{};
    int mHeldKeys = 0;                 // keys down (a held key can start a roll while silent)
    int mRollStrokes = 0;              // roll strokes since the wrapper last asked (its Note lamp)
    bool mMeshDamping = false;         // transport stop: hands damp the ringing-along keys too
    double mMeshDampPole = 1.0;
    double mClock = 0.0;               // output frames since reset

    // Sub-sample buffers for oversampling decimation
    float mSubBufPiezo[kMaxOversample] = {};
};

} // namespace maremba
