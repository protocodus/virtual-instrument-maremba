#pragma once

#include "MarimbaModel.h"
#include "RandomArtifacts.h"
#include <cmath>
#include <cstdint>

namespace maremba {

class MarembaVoice {
public:
    MarembaVoice();

    void Trigger(int noteNumber, float velocity, EMarimbaModel model,
                 float malletHardness, float strikePosition,
                 float resonatorTuneCents, float resonatorCoupling,
                 float decayMultiplier, float buzzAmount, float artifactsAmount,
                 float pitchGlideAmount,
                 float sampleRate, FastPRNG& prng,
                 float pitchOffsetCents = 0.0f,
                 int malletType = 1,
                 float strikeJitter = 15.0f);

    void Release(bool sustainPedalDown = false);
    void ReleaseSustainPedal(); // Called when sustain pedal lifts — begin damping sustained notes
    void FastKill();

    // Render one audio sample at current sampleRate (which may be oversampled)
    // Returns true if voice is still active (audible), false if voice has finished.
    bool ProcessSample(float& outClose, float& outFar, float& outPiezo);

    inline bool IsActive() const { return mActive; }
    inline bool IsSustained() const { return mSustained; }
    inline int GetNoteNumber() const { return mNoteNumber; }
    inline float GetAgeSamples() const { return mAgeSamples; }
    inline float GetCurrentAmplitude() const { return mEnvelopeEstimate; }

private:
    struct ModeState {
        float y1 = 0.0f;
        float y2 = 0.0f;
        float c = 0.0f;     // 2 * r * cos(w)
        float s = 0.0f;     // r^2
        float b0 = 0.0f;    // input gain
        float weight = 1.0f;
        float nominalOmega = 0.0f;
        float r = 0.0f;
    };

    bool mActive = false;
    bool mReleased = false;
    bool mSustained = false;   // True if key released but sustain pedal is holding
    bool mDamping = false;     // True when hand-damping is active
    int mNoteNumber = -1;
    float mVelocity = 0.0f;
    float mSampleRate = 44100.0f;
    float mAgeSamples = 0.0f;
    float mEnvelopeEstimate = 0.0f;

    // Mallet contact state & viscoelastic damping
    int mMalletContactSamplesTotal = 0;
    int mMalletContactSampleCurrent = 0;
    float mMalletContactOmega = 0.0f;
    float mMalletFrictionGain = 0.0f;
    float mFrictionFilterState = 0.0f;
    float mMalletPeakForce = 0.0f;
    float mMalletPulseExponent = 1.2f;
    float mMalletPulseExponentFall = 1.4f; // Asymmetric Hertzian: fall exponent
    float mMalletShockImpulse = 0.8f;
    float mMalletReboundGain = 0.0f;  // Tiny rebound undershoot after contact
    float mContactDampFactor = 0.0f;   // Viscoelastic mallet contact damping

    // 8 Pitched Modal Bar Resonators + Anisotropic Twin Mode 0b
    ModeState mModes[kNumPitchedModes];
    ModeState mMode0b; // Orthogonal wood grain split mode for organic acoustic beating

    // Strike clack noise burst (replaces fixed-frequency clack mode)
    float mClackBpY1 = 0.0f;    // Bandpass filter state 1
    float mClackBpY2 = 0.0f;    // Bandpass filter state 2
    float mClackBpC = 0.0f;     // 2*r*cos(w) for bandpass
    float mClackBpS = 0.0f;     // r^2 for bandpass
    float mClackEnvelope = 0.0f; // Decaying envelope for noise burst
    float mClackDecayRate = 0.0f;// Per-sample decay multiplier
    float mClackGain = 0.0f;    // Overall clack amplitude

    // Dynamic tension-modulation pitch glide
    float mCurrentGlideCents = 0.0f;
    float mGlideDecay = 0.995f;

    // Resonator Pipe / Gourd Cavity State
    float mResY1 = 0.0f;
    float mResY2 = 0.0f;
    float mResC = 0.0f;
    float mResS = 0.0f;
    float mResCoupling = 0.5f;

    // Mirliton Buzz Membrane State
    bool mHasMirliton = false;
    float mMirlitonThreshold = 0.05f;
    float mMirlitonDrive = 2.0f;
    float mMirlitonFilterState = 0.0f;

    // Cord / Mechanical rattle state
    float mRattleLevel = 0.0f;
    float mRattleDecay = 0.992f;

    // Hand-damping coefficient on note-off (exponential decay)
    float mDampRate = 1.0f;

    // Random generator reference for yarn noise and clack noise
    FastPRNG mLocalPrng;
};

} // namespace maremba
