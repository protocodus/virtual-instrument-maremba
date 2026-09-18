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

struct EngineParameters {
    int model = 0;                  // 0: Rosewood, 1: Padauk, 2: Balafon, 3: Kalimba
    int malletType = 1;             // 0: Soft Yarn/Thumb Flesh, 1: Medium Cord/Natural Thumb, 2: Hard Rubber/Thumb Nail, 3: Wood Baton/Thumb Pick
    float malletHardness = 0.40f;
    float strikePosition = 0.50f;
    float resonatorTune = 0.0f;     // Cents
    float resonatorCoupling = 0.70f;
    float decay = 1.00f;
    float buzzAmount = 0.15f;
    float artifacts = 0.35f;

    // Premium physical modeling phenomena
    float sympathetic = 0.40f;      // Inter-bar sympathetic resonance halo
    float pitchGlide = 0.30f;       // Dynamic tension-modulation pitch attack
    float bodyBloom = 0.50f;        // Frame & soundboard wooden body mass
    float rollSpeed = 0.0f;         // Mallet roll / tremolo rate (0 = off, 6-20 Hz)

    float closeLevel = 0.85f;
    float farLevel = 0.45f;
    float piezoLevel = 0.30f;
    float stereoWidth = 1.0f;

    float preampDrive = 0.15f;
    float warmth = 0.10f;
    float compAmount = 0.25f;
    float compAttack = 10.0f;
    float compRelease = 120.0f;

    float volume = 0.80f;
    int oversampling = 0;           // 0: 2x, 1: 4x, 2: 8x
    int velocityCurve = 1;          // 0: Soft, 1: Linear, 2: Hard, 3: Expressive
    int polyphony = 1;              // 0: 8, 1: 16, 2: 24
    float masterTune = 0.50f;
    float detune = 0.0f;            // 0 to 100: key drift amount (0 = exact, 100 = half-tone off)
    float strikeJitter = 15.0f;     // 0 to 100: organic stochastic strike variance
    bool sustainPedalDown = false;  // Sustain pedal state
};

class MarembaEngine {
public:
    static constexpr int kMaxVoices = 24;

    explicit MarembaEngine(double sampleRate = 44100.0);

    void SetSampleRate(double sampleRate);
    void Reset();

    void NoteOn(int noteNumber, float velocity);
    void NoteOff(int noteNumber);
    void AllNotesOff();

    void SetParameters(const EngineParameters& params);
    const EngineParameters& GetParameters() const { return mParams; }

    int GetActiveVoiceCount() const;

    // Render batch of frames (e.g. 64 frames) into output buffers
    void RenderBatch(float* outMainL, float* outMainR,
                     float* outCloseL, float* outCloseR,
                     float* outFarL, float* outFarR,
                     float* outPiezo,
                     int frameCount);

private:
    float MapVelocity(float vel) const;
    int FindVoiceToAllocate(int noteNumber);
    void ProcessMalletRolls(int frames);

    double mBaseSampleRate = 44100.0;
    EngineParameters mParams;

    std::array<MarembaVoice, kMaxVoices> mVoices;
    MicMixer mMicMixer;
    AnalogPreamp mPreamp;
    Compressor mCompressor;
    StereoOversampler mOversampler;
    SympatheticMesh mSympathetic;
    FrameBody mFrameBody;
    FastPRNG mPrng;

    // Mallet Roll State
    struct RollTracker {
        int noteNumber = -1;
        float velocity = 0.0f;
        float timerSamples = 0.0f;
        bool leftHand = true;
    };
    std::array<RollTracker, 4> mRollNotes;

    // Direct sub-sample buffers for oversampling decimation
    static constexpr int kMaxOversample = 8;
    float mSubBufMainL[kMaxOversample];
    float mSubBufMainR[kMaxOversample];
    float mSubBufCloseL[kMaxOversample];
    float mSubBufCloseR[kMaxOversample];
    float mSubBufFarL[kMaxOversample];
    float mSubBufFarR[kMaxOversample];
    float mSubBufPiezo[kMaxOversample];
};

} // namespace maremba
