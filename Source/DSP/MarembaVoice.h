#pragma once

#include "MarimbaModel.h"
#include "RandomArtifacts.h"
#include "PiezoPickup.h"
#include <cmath>
#include <cstdint>

namespace maremba {

class MarembaVoice {
public:
    MarembaVoice();

    // Deterministic per-slot noise seed (the engine gives every slot its own,
    // so the clack/friction/rattle noise of a chord is decorrelated).
    void SetSeed(uint32_t seed) { mLocalPrng = FastPRNG(seed); }

    // globalCents: engine-wide tuning (tune + pitch bend) at trigger time; the
    // voice can later be retuned by the difference. keyOffsetCents: per-key drift.
    void Trigger(int noteNumber, float velocity, EMarimbaModel model,
                 float malletHardness, float strikePosition,
                 float resonatorTuneCents, float resonatorCoupling,
                 float decayMultiplier, float buzzAmount, float artifactsAmount,
                 float legacyPitchGlideIgnored, // retained call contract; no pitch sweep
                 double sampleRate, FastPRNG& prng,
                 float globalCents, float keyOffsetCents,
                 int malletType, float strikeJitter);

    // Key released: sound-neutral by design (bars ring freely); only marks the
    // voice as the preferred steal victim.
    void Release() { mReleased = true; }

    // Transport stop: gentle per-model hand damping (symmetric, no energy injection).
    void StartDamping();

    // Fade the voice out over the given time, then free it (steal / polyphony / rate change).
    void StartFade(double seconds);

    // Engine-wide tuning changed (master tune, pitch bend): recompute the
    // resonator coefficients from the stored trigger-time frequencies.
    void Retune(float globalCents);
    // Membrane amount follows automation while the bar is ringing.
    void SetBuzz(float amount);

    void FastKill();

    // Render one sample at the internal (oversampled) rate.
    // Returns false once the voice has finished.
    bool ProcessSample(float& outClose, float& outFar, float& outPiezo);

    // Control-rate update (every few output frames): phase-independent energy
    // estimate, deactivation below -120 dB and the non-finite guard.
    void UpdateEnvelope();

    inline bool IsActive() const { return mActive; }
    inline bool IsReleased() const { return mReleased; }
    inline bool IsFading() const { return mFading; }
    inline bool IsLive() const { return mActive && !mFading; }
    inline int GetNoteNumber() const { return mNoteNumber; }
    inline EMarimbaModel GetModel() const { return mModel; }   // model of the last strike
    inline float GetMechanicalSample() const { return mMechanicalSample; } // before pickup electronics
    inline float GetFadeGain() const { return mFadeGain; }     // 1 unless fading out

    // Sum over the resonators of their squared amplitude (phase independent),
    // as of the last control-rate update.
    inline double GetCurrentEnergy() const { return mEnergy; }

    // The same sum, evaluated now (voice stealing). Not cached; 0 when inactive.
    double ComputeEnergy() const;

    // True when no resonator state (modes, twin, tube, restrike residuals) is
    // subnormal (unit testing).
    bool HasNoSubnormalState() const;

    // True while the strike is still being delivered (never steal such a voice
    // if anything else can go).
    inline bool IsStriking() const { return mContactSample < mContactSamplesTotal || mShockSample < mShockSamplesTotal
        || mArtifactSample < mArtifactEnd; }

private:
    friend struct MarembaVoiceTestAccess; // isolated mechanical-response tests

    struct ModeState {
        double y1 = 0.0;
        double y2 = 0.0;
        double o1 = 0.0;      // residual of the previous strike during a restrike (damped separately)
        double o2 = 0.0;
        double c = 0.0;       // 2 * r * cos(w)
        double s = 0.0;       // r^2
        double b0 = 0.0;      // input gain
        double omega = 0.0;   // pole angle at the trigger-time tuning
        double w = 0.0;       // pole angle currently in c (tuning and bend applied)
        double r = 0.0;
        double cosW = 1.0, invSin = 0.0; // quadrature displacement of a tine
        double invSin2 = 0.0; // 1 / sin^2(w): converts the quadratic invariant to squared amplitude
        double contactPole = 1.0; // loss at this mode's contact footprint
        double restrikePole = 1.0;
    };

    void SetModeCoefficients(ModeState& mode, double omega) const;
    // New pole angle for a ringing resonator, keeping its amplitude and phase.
    void RetuneMode(ModeState& mode, double omega) const;
    // Also preserve a ringing state when a restrike changes its decay radius.
    void ReconfigureMode(ModeState& mode, double radius, double omega) const;
    static double ModeEnergy(const ModeState& mode);
    // ModeEnergy, and a mode that has decayed below -300 dB is zeroed so it
    // never cycles on subnormal values while the rest of the voice rings on.
    static double FlushedModeEnergy(ModeState& mode);

    bool mActive = false;
    bool mReleased = false;
    bool mDamping = false;     // hand damping (DampAll)
    bool mFading = false;
    int mNoteNumber = -1;
    EMarimbaModel mModel = EMarimbaModel::ImperialRosewood;
    float mVelocity = 0.0f;
    double mSampleRate = 88200.0;
    double mRefStep = 1.0;     // kReferenceRate / mSampleRate: one internal sample in reference samples
    double mEnergy = 0.0;

    // Engine-wide tuning at trigger and the current ratio applied to all resonators
    float mTriggerGlobalCents = 0.0f;
    double mPitchRatio = 1.0;

    // Mallet contact state & viscoelastic damping
    int mContactSamplesTotal = 0;
    int mContactSample = 0;
    double mContactStep = 0.0;       // one sample as a fraction of the contact duration
    double mContactImpulse = 0.0;    // integral of the main contact pulse
    double mContactSkew = 0.0;
    float mMalletShockImpulse = 0.8f;
    double mShockGain = 0.0;         // integral of the initial hard-contact component / 1.7
    int mShockSamplesTotal = 0;
    int mShockSample = 0;
    double mFrictionGain = 0.0;
    double mFrictionAlpha = 0.05;
    double mFrictionNoiseScale = 1.0;
    double mFrictionFilterState = 0.0;

    // Gradual restrike damping of the previous ring (applied to the residual states)
    int mRestrikeSamplesLeft = 0;
    double mRestrikePole = 1.0;

    // Hand damping (transport stop) and fade-out
    double mHandDampPole = 1.0;
    double mHandDampRate = 15.0;
    float mFadeGain = 1.0f;
    float mFadeStep = 0.0f;

    // 8 Pitched Modal Bar Resonators + Anisotropic Twin Mode 0b
    ModeState mModes[kNumPitchedModes];
    int mNumModes = 0;                 // modes below 0.45 * fs; higher ones are dropped
    ModeState mMode0b; // Orthogonal wood grain split mode for organic acoustic beating
    bool mHasTwin = false;

    // Strike clack noise burst (band-passed noise, not a tonal mode)
    bool mClackActive = false;
    double mClackBpY1 = 0.0;
    double mClackBpY2 = 0.0;
    double mClackBpC = 0.0;
    double mClackBpS = 0.0;
    double mClackBpB = 0.0;
    double mClackEnvelope = 0.0;
    double mClackDecayRate = 0.0;
    double mNoiseScale = 1.0;          // sqrt(fs / reference): keeps output noise density rate-invariant

    // Resonator Pipe / Gourd Cavity State
    ModeState mTube;
    double mResCoupling = 0.5;

    // Mirliton Buzz Membrane State
    bool mHasMirliton = false;
    double mMirlitonThreshold = 0.05;
    double mMirlitonDrive = 2.0;
    double mMirlitonFilterState = 0.0;
    double mMirlitonAttack = 0.35;
    double mMirlitonRelease = 0.92;

    // Secondary physical contacts share the initial stroke's impulse budget.
    int mArtifactSample = 0, mArtifactEnd = 0;
    int mReboundStart = 0, mBrushStart = 0;
    double mSecondaryStep = 1.0;
    double mReboundImpulse = 0.0, mBrushImpulse = 0.0;
    ModeState mBrushModes[2];       // incidental adjacent bar / tine
    ModeState mCollisionModes[2];   // mount/rail/bridge knock
    bool mBrushActive = false, mCollisionActive = false;
    double mCollisionGap = 1.0;
    double mCollisionPrevious = 0.0, mCollisionImpulse = 0.0, mCollisionImpulseLimit = 0.0;
    double mCollisionPulseStep = 1.0, mCollisionAmplitudeRatio = 0.0;
    int mCollisionSide = 0, mCollisionSample = 0, mCollisionSamplesTotal = 0;
    double mCollisionEnvelope = 0.0, mCollisionDecay = 0.0;
    double mBrushPickupWeight = 1.0;
    double mStrainWeights[kNumPitchedModes]{};
    float mMechanicalSample = 0.0f;
    PiezoPickup mPickup;

    // Per-voice noise source for yarn friction and the brief primary clack
    FastPRNG mLocalPrng;
};

} // namespace maremba
