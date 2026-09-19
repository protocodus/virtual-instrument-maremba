#pragma once

#include "MarimbaModel.h"
#include "RandomArtifacts.h"
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
                 float pitchGlideAmount,
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
    inline bool IsStriking() const { return mContactSample < mContactSamplesTotal || mShockSample < mShockSamplesTotal; }

private:
    struct ModeState {
        double y1 = 0.0;
        double y2 = 0.0;
        double o1 = 0.0;      // residual of the previous strike during a restrike (damped separately)
        double o2 = 0.0;
        double c = 0.0;       // 2 * r * cos(w)
        double s = 0.0;       // r^2
        double b0 = 0.0;      // input gain
        double omega = 0.0;   // pole angle at the trigger-time tuning
        double r = 0.0;
        double invSin2 = 0.0; // 1 / sin^2(w): converts the quadratic invariant to squared amplitude
    };

    void SetModeCoefficients(ModeState& mode, double omega) const;
    static double ModeEnergy(const ModeState& mode);
    // ModeEnergy, and a mode that has decayed below -300 dB is zeroed so it
    // never cycles on subnormal values while the rest of the voice rings on.
    static double FlushedModeEnergy(ModeState& mode);

    bool mActive = false;
    bool mReleased = false;
    bool mDamping = false;     // hand damping (DampAll)
    bool mFading = false;
    int mNoteNumber = -1;
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
    double mContactOmega = 0.0;
    double mContactForceStep = 0.0;   // peak force x dt (rate-normalised)
    float mMalletPulseExponent = 1.2f;
    float mMalletPulseExponentFall = 1.4f; // Asymmetric Hertzian: fall exponent
    float mMalletShockImpulse = 0.8f;
    float mMalletReboundGain = 0.0f;  // Tiny rebound undershoot after contact
    double mShockGain = 0.0;           // shock impulse x peak force, spread over 3 reference samples
    int mShockSamplesTotal = 0;
    int mShockSample = 0;
    double mReboundImpulse = 0.0;
    int mReboundSamplesTotal = 0;
    int mReboundSample = 0;
    double mContactDampPole = 1.0;     // per-sample symmetric damping of modes >= 2 during contact
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

    // Dynamic tension-modulation pitch glide
    double mCurrentGlideCents = 0.0;
    double mGlideDecay = 0.995;

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

    // Cord / Mechanical rattle state
    double mRattleLevel = 0.0;
    double mRattleDecay = 0.992;

    // Per-voice noise source for yarn friction, clack and rattle
    FastPRNG mLocalPrng;
};

} // namespace maremba
