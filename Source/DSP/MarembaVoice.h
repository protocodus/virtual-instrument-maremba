#pragma once

#include "MarimbaModel.h"
#include "RandomArtifacts.h"
#include "PiezoPickup.h"
#include "ModalRadiation.h"
#include "MalletContact.h"
#include "SlackMembrane.h"
#include "BuzzerRattle.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maremba {

// A key's spatial radiation readouts, kept by the engine per MIDI note: a
// fresh strike of a key reuses them while its modes sit within a stroke's
// pitch scatter of the frequencies they were projected at.
struct RadiationCacheEntry {
    int model = -1;
    float carving = -1.0f;
    double hz[kNumPitchedModes]{};
    ModalRadiationReadout readout[kNumPitchedModes];
};

// Striker beyond the four heads (malletType 0-3): the mallet's shaft on the
// bar's edge (a fingernail tap on a tine).
static constexpr int kStrikerShaft = 4;
static constexpr int kStrikerCount = 5;

// Per-strike settings beyond the positional contract; the defaults are the
// instrument as built.
struct StrikeOptions {
    float carving = 1.0f;             // 0 uncarved bars / stiff tine profile .. 1 as built
    float membraneTension = 0.5f;     // support and buzz tension: 0 slack .. 1 tight; .5 as voiced
    bool deadStroke = false;          // the mallet (thumb) stays on the key until Release()
    RadiationCacheEntry* radiationCache = nullptr;
};

// One struck bar (or plucked tine) with its resonator, membranes and mounting.
// Modal states are mass-normalized displacements (scaled by kStateScale), so
// a physical mallet can be solved against the bar's driving-point response
// and the bar-air junction exchanges energy in consistent units.
class MarembaVoice {
public:
    MarembaVoice();

    // Deterministic per-slot noise seed (the engine gives every slot its own,
    // so the clack/texture/rattle noise of a chord is decorrelated).
    void SetSeed(uint32_t seed) { mLocalPrng = FastPRNG(seed); }

    // globalCents: engine-wide tuning (tune + pitch bend) at trigger time; the
    // voice can later be retuned by the difference. keyOffsetCents: per-key
    // drift and traditional tuning. positionOffset moves the contact (roll
    // strokes alternate two mallets).
    void Trigger(int noteNumber, float velocity, EMarimbaModel model,
                 float malletHardness, float strikePosition,
                 float resonatorTuneCents, float resonatorCoupling,
                 float decayMultiplier, float buzzAmount, float artifactsAmount,
                 float legacyPitchGlideIgnored, // retained call contract; no pitch sweep
                 double sampleRate, FastPRNG& prng,
                 float globalCents, float keyOffsetCents,
                 int malletType, float strikeJitter,
                 float positionOffset = 0.0f,
                 const StrikeOptions& options = StrikeOptions{});

    // Key released: marks the voice as the preferred steal victim. A dead
    // stroke's mallet (or thumb) lifts off. Otherwise sound-neutral (bars
    // ring freely).
    void Release();

    // Transport stop: gentle per-model hand damping (symmetric, no energy injection).
    void StartDamping();
    // Note-off in Damp mode: a hand, finger or held mallet stops the key.
    void StartReleaseDamping();

    // Fade the voice out over the given time, then free it (steal / polyphony / rate change).
    void StartFade(double seconds);

    // Engine-wide tuning changed (master tune, pitch bend): recompute the
    // resonator coefficients from the stored trigger-time frequencies.
    void Retune(float globalCents);
    // Normalized 0..1 Buzz knob (mapped to 0..kBuzzRange of the physical
    // membrane/buzzer response) and support/attachment tension; both follow
    // automation while a key rings. Tension remains effective with Buzz off.
    static constexpr float kBuzzRange = 0.7f;
    void SetBuzz(float amount, float tension = 0.5f);

    void FastKill();

    // Render one sample at the internal (oversampled) rate.
    // Returns false once the voice has finished.
    bool ProcessSample(float& outClose, float& outFar, float& outPiezo);

    // Control-rate update (every few output frames): phase-independent energy
    // estimate, idle-mode skipping, deactivation below -120 dB and the non-finite guard.
    void UpdateEnvelope();

    inline bool IsActive() const { return mActive; }
    inline bool IsReleased() const { return mReleased; }
    inline bool IsFading() const { return mFading; }
    inline bool IsLive() const { return mActive && !mFading; }
    inline int GetNoteNumber() const { return mNoteNumber; }
    inline EMarimbaModel GetModel() const { return mModel; }   // model of the last strike
    inline float GetMechanicalSample() const { return mMechanicalSample; } // support drive
    inline float GetBridgeForce() const { return mBridgeSample; }          // kalimba bridge (N)
    inline float GetFadeGain() const { return mFadeGain; }     // 1 unless fading out

    // Sum over the resonators of their squared amplitude (phase independent),
    // as of the last control-rate update.
    inline double GetCurrentEnergy() const { return mEnergy; }

    // The same sum, evaluated now (voice stealing). Not cached; 0 when inactive.
    double ComputeEnergy() const;
    // Mean square of the voice's sound over the last ~30 ms.
    inline double GetLoudness() const { return mLoudness; }

    // True when no resonator state is subnormal (unit testing).
    bool HasNoSubnormalState() const;

    // True while the strike is still being delivered (never steal such a voice
    // if anything else can go).
    inline bool IsStriking() const {
        return mContact.Active() || mReleaseSample < mReleaseSamples || mArtifactSample < mArtifactEnd
            || mThumbPending || mHandPulseSample < mHandPulseSamples;
    }

    // Diagnostics for tests and calibration.
    inline double GetContactForce() const { return mContactForce; }
    inline int GetModeCount() const { return mNumModes; }

    // Scale of the modal states: y = kStateScale * sqrt(modal mass) * displacement.
    static constexpr double kStateScale = 2500.0;

private:
    friend struct MarembaVoiceTestAccess; // isolated mechanical-response tests

    struct ModeState {
        double y1 = 0.0;
        double y2 = 0.0;
        double c = 0.0;       // 2 * r * cos(w)
        double s = 0.0;       // r^2
        double omega = 0.0;   // pole angle at the trigger-time tuning
        double w = 0.0;       // pole angle currently in c (tuning and bend applied)
        double r = 0.0;
        double cosW = 1.0, invSin = 0.0;
        double cosOverR = 0.0, sinOverR = 0.0; // cached state-junction coordinate maps
        double invSin2 = 0.0; // 1 / sin^2(w): converts the quadratic invariant to squared amplitude
        double force = 0.0;   // state per newton-second at the contact point
        double contact = 0.0; // mode shape at the contact (footprint-averaged)
        double toDisplacement = 0.0; // physical displacement per unit state
        bool idle = false;    // decayed below -300 dB and not driven: skipped
    };

    void SetModeCoefficients(ModeState& mode, double omega) const;
    void RetuneMode(ModeState& mode, double omega) const;
    void ReconfigureMode(ModeState& mode, double radius, double omega) const;
    double TensionRadius(int mode, float tension) const;
    static double ModeEnergy(const ModeState& mode);
    static double FlushedModeEnergy(ModeState& mode);
    double MembraneEnergy() const;
    void UpdateContactCompliance();
    void UpdateCavityCoupling();
    void TuneOnResonator();
    void CoupleCavity(ModeState& cavity, int barIndex, double cosine, double sine);
    void CoupleCavityAperture(ModeState& cavity, int cavityIndex);
    void ConfigureMembranes(float buzzAmount, bool reshuffle);
    void StartMallet(double velocity, double positionOffset);
    double ContactDisplacement() const;
    void Damp(double pole);
    void StartHand(double strength);
    double FingerForce(const double* predicted, double otherForce);

    bool mActive = false;
    bool mReleased = false;
    bool mDamping = false;     // hand damping (transport stop or Damp mode)
    bool mFading = false;
    bool mTine = false;
    int mNoteNumber = -1;
    EMarimbaModel mModel = EMarimbaModel::ImperialRosewood;
    float mVelocity = 0.0f;
    double mSampleRate = 88200.0;
    double mDt = 1.0 / 88200.0;
    double mEnergy = 0.0;
    double mF0 = 261.625565;

    // Engine-wide tuning at trigger and the current ratio applied to all resonators
    float mTriggerGlobalCents = 0.0f;
    double mPitchRatio = 1.0;

    // Hand damping (transport stop, note-off Damp) and fade-out
    double mHandDampPole = 1.0;
    double mHandDampRate = 15.0;
    double mReleaseDampRate = 24.0;
    float mFadeGain = 1.0f;
    float mFadeStep = 0.0f;

    // Bending modes of the bar or tine, plus two torsional and one lateral mode
    ModeState mModes[kNumPitchedModes];
    double mNeutralModeLoss[kNumPitchedModes]{}; // loss exponent/sample at Tension=.5
    double mNeutralModeRadius[kNumPitchedModes]{}; // exact original pole at the centre
    float mRadiationCarving = -1.0f;
    ModalRadiationReadout mRadiation[kNumPitchedModes];
    double mRadiationHz[kNumPitchedModes]{};   // mode frequency the readout was projected at
    double mRadiationGain[kNumPitchedModes]{};   // acceleration x dipole efficiency x calibration
    double mStrainGain[kNumPitchedModes]{};      // pickup strain per unit state
    double mSupportGain[kNumPitchedModes]{};     // force into the rails/frame per unit state
    double mBridgeGain[kNumPitchedModes]{};      // kalimba: bridge reaction (N) per unit state
    int mNumModes = 0;
    static constexpr int kNumTorsionModes = 2;
    ModeState mTorsion[kNumTorsionModes];
    double mTorsionDrive[kNumTorsionModes]{};
    double mTorsionClose[kNumTorsionModes]{};
    double mTorsionFar[kNumTorsionModes]{};
    int mNumTorsionModes = 0;
    ModeState mLateral;
    bool mHasLateral = false;
    double mLateralDrive = 0.0, mLateralRadiation = 0.0;

    // Mallet contact (bars) and its per-sample force
    MalletContact mContact;
    MalletContact::Setup mMalletSetup;
    MalletSpec mMallet{};
    double mContactCompliance = 0.0;
    double mContactForce = 0.0;
    double mPreviousForce = 0.0;
    double mTextureState = 0.0, mTextureAlpha = 0.05, mTextureDepth = 1.0;
    double mLoudness = 0.0, mLoudnessAlpha = 0.0;
    double mStrokePosition = 0.5;

    // Striker (malletType): heads 0-3, shaft/tap.
    int mStriker = 1;
    bool mDeadStroke = false;
    double mSupportLoad = 0.0;      // a held head's steady push, carried by the supports

    // A palm (bars) or fingertip (tines) damping the key. The palm lands
    // where the stroke did and takes each mode by its mean square under the
    // palm; the fingertip is a one-sided spring and damper on the tine, which
    // chatters while the tine still swings further than it presses.
    bool mFingerOn = false;          // fingertip on the tine
    bool mFingerPending = false;     // dead stroke: the thumb comes back to rest after the pluck ...
    int mFingerDelay = 0;            // ... this many samples after it let go
    double mFingerPreload = 0.0, mFingerTarget = 0.0;   // compression at rest (m), landing toward the target
    double mFingerRamp = 1.0;
    double mFingerStiffness = 0.0, mFingerDamping = 0.0;
    double mFingerCompression = 0.0; // dynamic compression last sample (m)
    double mHandShape[kNumPitchedModes]{};   // mean square of each mode under the palm
    double mHandPole[kNumPitchedModes]{};
    double mHandTorsionPole = 1.0, mHandAirPole = 1.0;
    int mHandPulseSample = 0, mHandPulseSamples = 0;
    double mHandPulseImpulse = 0.0, mHandPulseStep = 1.0;
    bool mHandModes = false;         // per-mode palm damping active

    // The mallet shaft's own bending modes, knocked by the contact.
    ModeState mShaftModes[3];
    double mShaftDrive = 0.0, mShaftRadiation = 0.0;
    bool mShaftActive = false;

    // Thumb release (tines): static deflection held by the thumb, then released.
    double mReleaseForce = 0.0;      // thumb force at the start of the release (N)
    bool mThumbPending = false;      // the thumb touches the tine on the next sample
    double mThumbResidual = 0.0;     // fraction of the old ring the thumb leaves
    double mThumbHold[kNumPitchedModes] = {};   // held deflection per mode
    int mReleaseSample = 0, mReleaseSamples = 0;
    double mReleaseShape = 2.0;
    double mBridgeSample = 0.0;
    double mBridgeNonlinear = 0.0;   // bridge-rod slip: quadratic force coefficient (1/N)
    // A thumb presses a tine down over tens of milliseconds before it plucks,
    // so the board has settled under the static load by the release; the
    // voice sets that deflection in one sample. What the board, pickup and
    // buzzers read of the bridge carries the jump at the thumb's touch as an
    // offset that bleeds off well below audio (the board's quasi-static
    // return), so the pluck starts from the tine's continuous motion.
    double mPreloadOffset = 0.0;     // N
    double mPreloadBleed = 1.0;      // per-sample decay of the offset

    // Unmodelled high-frequency impact noise (click), shaped by the force rate
    double mClickGain = 0.0;
    double mClackBpY1 = 0.0, mClackBpY2 = 0.0, mClackBpC = 0.0, mClackBpS = 0.0, mClackBpB = 0.0;
    double mNoiseScale = 1.0;

    // Air pressure modes: tube, box or gourd (bars only).
    ModeState mTube;
    ModeState mUpperCavity[2];
    int mNumCavityModes = 0;
    double mCavityCos[3] = {1.0, 1.0, 1.0};
    double mCavitySin[3] = {};
    double mCavityReadout[3] = {};
    int mCavityBarIndex[3][2] = {};
    int mCavityConnections[3] = {};
    double mCavityDirection[3][2] = {};
    double mCavityRate[3] = {};
    double mCavityAngle[3] = {}, mCavityAngleTarget[3] = {};
    bool mCavityAngleRamp = false;
    double mResCoupling = 0.5;
    double mResReadoutGain = 0.0;
    double mResReadoutStep = 1.0;
    double mFarAirWeight = 0.22;
    double mMembranePressure[3] = {3.8, 1.6, 0.8};
    // Flow separation at the tube's rim under the bar: odd harmonics of the
    // air's own motion (3 f0, 5 f0), growing with the mouth's velocity.
    double mRimHarmonic3 = 0.0, mRimHarmonic5 = 0.0;
    double mRimGain3 = 0.0, mRimGain5 = 0.0;   // per unit cubed / fifth-power state (control rate)
    double mRimLevel3 = 0.0, mRimLevel5 = 0.0; // each harmonic's level per unit squared swing

    // Buzzing membranes over holes in the resonator (Buzz): up to three
    // mirlitons on a gourd, one charleo at a box's bottom, one add-on on a tube.
    static constexpr int kMaxMembranes = 3;
    SlackMembrane mMembranes[kMaxMembranes];
    int mNumMembranes = 0;
    bool mHasMirliton = false;
    double mMembraneDrive[kMaxMembranes]{};
    double mMembraneRadiation[kMaxMembranes]{};
    double mMembraneThreshold[kMaxMembranes]{};   // per-strike gap scale
    double mMembraneGapScale = 1.0;
    float mMembraneTension = 0.5f;
    double mMembraneDrain = 0.0;
    double mSlapJitter = 0.0;
    bool mMembraneTaut[kMaxMembranes]{};
    FastPRNG mSlapPrng;              // one draw per slap: the same sequence at every rate
    double mSlapNoise = 0.0, mSlapY1 = 0.0, mSlapY2 = 0.0, mSlapC = 0.0, mSlapS = 0.0, mSlapB = 0.0;
    double mSlapNoiseGain = 0.0;
    // The buzz's own share of the note's fundamental belongs to the air's
    // radiation: a fourth-order high-pass at 1.5 f0 (at least 350 Hz; two
    // Butterworth sections) and a notch on f0 keep the membranes to their
    // snaps. A clipped membrane passes less of the fundamental than its slack
    // shadow; that flow stays in the resonator and must not radiate as an
    // out-of-phase fundamental. Both are set at the strike: bent 2 semitones
    // up, the fundamental still lies about 20 dB down.
    struct BuzzSection { double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; };
    BuzzSection mBuzzFilter[3];
    float mBuzzAmount = 0.0f;
    // Kalimba Buzz: loose buzzers on the board, driven by the bridge motion.
    BuzzerRattle mBuzzer;

    // Secondary physical contacts share the initial stroke's budget.
    int mArtifactSample = 0, mArtifactEnd = 0;
    int mReboundStart = -1, mBrushStart = 0;
    double mReboundVelocity = 0.0;
    double mSecondaryStep = 1.0;
    double mBrushImpulse = 0.0;
    ModeState mBrushModes[2];       // incidental adjacent bar / tine
    ModeState mCollisionModes[2];   // mount/rail/bridge knock
    bool mBrushActive = false, mCollisionActive = false;
    double mCollisionGap = 1.0;
    double mCollisionPrevious = 0.0, mCollisionImpulse = 0.0, mCollisionImpulseLimit = 0.0;
    double mCollisionPulseStep = 1.0, mCollisionAmplitudeRatio = 0.0;
    int mCollisionSide = 0, mCollisionSample = 0, mCollisionSamplesTotal = 0;
    double mCollisionEnvelope = 0.0, mCollisionDecay = 0.0;
    double mCollisionGapFactor = 0.3, mCollisionOutput = 0.0, mCollisionOut = 0.0;
    double mCollisionPickup = 0.0, mCollisionFrame = 0.0;
    bool mCollisionCalibrated = false, mCollisionSawContact = false;
    int mCollisionClock = 0, mCollisionCalibrationSample = 0;
    double mBrushPickupWeight = 1.0;
    double mPickupContactGain = 0.0;
    float mMechanicalSample = 0.0f;
    PiezoPickup mPickup;

    // Per-voice noise source for texture, click and slap noise
    FastPRNG mLocalPrng;
};

} // namespace maremba
