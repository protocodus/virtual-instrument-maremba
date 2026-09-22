#include "MarembaVoice.h"
#include "DspMath.h"
#include "ModalExcitation.h"
#include "ContactExcitation.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace maremba {

namespace {

// A voice is freed once the summed squared amplitude of its resonators falls
// below this (-120 dB) and no excitation or noise burst is pending.
constexpr double kSilenceEnergy = 1.0e-12;
// A single resonator below this squared amplitude (-300 dB) is flushed to 0.
constexpr double kFlushEnergy = 1.0e-30;
// Longest bar T60 in seconds (a time cap keeps decay independent of the rate).
constexpr double kMaxT60Seconds = 60.0;
// Modes at or above this fraction of the internal rate are dropped (weight 0).
constexpr double kModeDropFraction = 0.45;
// Highest pole angle a retuned (pitch-bent) resonator may reach.
constexpr double kMaxOmega = 0.98 * kPiD;
// Reference treble contact limit, in cycles of the fundamental. The effective
// limit also follows touch, keeping f0 in the pulse main lobe across dynamics.
constexpr float kMaxContactCycles = 0.8f;
// A voice whose summed squared resonator amplitude exceeds this (+40 dB, far
// above any strike) is unstable and is freed (defence in depth).
constexpr double kMaxVoiceEnergy = 1.0e4;

constexpr double kFrameWeight[kNumPitchedModes] = { 0.5, 1.0, 1.2, 1.2, 1.2, 1.2, 1.5, 1.5 };

// Fast contact component: 1.0, 0.5, 0.2 over the first three
// reference-rate samples (34 us). Integrated over each internal sample so the
// total impulse is identical at every rate.
inline double ShockCumulative(double t) {
    if (t <= 0.0) return 0.0;
    return std::min(t, 1.0)
         + 0.5 * std::clamp(t - 1.0, 0.0, 1.0)
         + 0.2 * std::clamp(t - 2.0, 0.0, 1.0);
}
constexpr double kShockDurationRef = 3.0;

} // namespace

MarembaVoice::MarembaVoice()
    : mLocalPrng(0x56789ABC)
{
    FastKill();
}

void MarembaVoice::FastKill() {
    mActive = false;
    mReleased = false;
    mDamping = false;
    mFading = false;
    mNoteNumber = -1;
    mVelocity = 0.0f;
    mEnergy = 0.0;
    mPitchRatio = 1.0;
    mContactSamplesTotal = 0;
    mContactSample = 0;
    mShockSamplesTotal = 0;
    mShockSample = 0;
    mRestrikeSamplesLeft = 0;
    mLocalContactActive = false;
    mMalletContact.Reset();
    mFadeGain = 1.0f;
    mFadeStep = 0.0f;
    mFrictionFilterState = 0.0;
    mMirlitonDisplacement = mMirlitonVelocity = 0.0;
    mArtifactSample = mArtifactEnd = 0;
    mReboundImpulse = mBrushImpulse = 0.0;
    mCollisionEnvelope = 0.0;
    mBrushActive = mCollisionActive = false;
    mCollisionPrevious = mCollisionImpulse = 0.0;
    mCollisionSide = mCollisionSample = mCollisionSamplesTotal = 0;
    mMechanicalSample = 0.0f;
    for (auto& mode : mBrushModes) mode = ModeState{};
    for (auto& mode : mCollisionModes) mode = ModeState{};
    mPickup.Reset();
    mClackActive = false;
    mClackBpY1 = mClackBpY2 = 0.0;
    mClackEnvelope = 0.0;

    for (int i = 0; i < kNumPitchedModes; ++i) {
        mModes[i] = ModeState{};
        mRadiation[i] = ModalRadiationReadout{};
    }
    mNumModes = 0;
    mMode0b = ModeState{};
    mHasTwin = false;
    mTube = ModeState{};
    for (auto& mode : mUpperCavity) mode = ModeState{};
    mNumCavityModes = 0;
    mResReadoutGain = 0.0;
}

void MarembaVoice::SetModeCoefficients(ModeState& mode, double omega) const {
    const double w = std::clamp(omega, 1.0e-9, kMaxOmega);
    const double sinW = std::sin(w);
    mode.w = w;
    mode.cosW = std::cos(w);
    mode.invSin = 1.0 / sinW;
    mode.cosOverR = mode.r > 0.0 ? mode.cosW / mode.r : 0.0;
    mode.sinOverR = mode.r > 0.0 ? 1.0 / (mode.invSin * mode.r) : 0.0;
    mode.c = 2.0 * mode.r * mode.cosW;
    mode.s = mode.r * mode.r;
    mode.invSin2 = 1.0 / (sinW * sinW);
}

void MarembaVoice::UpdateTineStrainScales() {
    if (mModel != EMarimbaModel::KalimbaArtisan) return;
    for (int m = 0; m < mNumModes; ++m)
        mTineStrainScales[m] = mModes[m].invSin * mModes[0].w / mModes[m].w * mStrainWeights[m];
}

void MarembaVoice::UpdateCavityCoupling() {
    // A low-order aperture surrogate: its overlap moves continuously between
    // the bending modes bracketing each upper air frequency. Smooth equal-power
    // weights avoid abrupt source switches while preserving one passive port.
    // These overlaps are voiced, not a measured full spatial coupling matrix.
    for (int i = 0; i < mNumCavityModes; ++i) {
        const auto& air = i == 0 ? mTube : mUpperCavity[i - 1];
        int lower = 0, upper = 0;
        double firstWeight = 1.0, secondWeight = 0.0;
        if (i > 0 && air.w > mModes[0].w) {
            upper = 1;
            while (upper < mNumModes && mModes[upper].w < air.w) ++upper;
            if (upper == mNumModes) {
                lower = upper = mNumModes - 1;
            } else {
                lower = upper - 1;
                const double span = std::log(mModes[upper].w / mModes[lower].w);
                const double t = span > 1.0e-12
                    ? std::clamp(std::log(air.w / mModes[lower].w) / span, 0.0, 1.0) : 0.0;
                const double smooth = t * t * (3.0 - 2.0 * t);
                firstWeight = std::cos(0.5 * kPiD * smooth);
                secondWeight = std::sin(0.5 * kPiD * smooth);
                if (t >= 1.0) { lower = upper; firstWeight = 1.0; secondWeight = 0.0; }
            }
        }
        mCavityBarIndex[i][0] = lower;
        mCavityBarIndex[i][1] = upper;
        mCavityConnections[i] = secondWeight > 0.0 ? 2 : 1;
        const double firstRate = firstWeight * std::sqrt(-std::log(mModes[lower].r) * mSampleRate);
        const double secondRate = secondWeight > 0.0
            ? secondWeight * std::sqrt(-std::log(mModes[upper].r) * mSampleRate) : 0.0;
        const double rate = std::sqrt(firstRate * firstRate + secondRate * secondRate);
        mCavityDirection[i][0] = rate > 0.0 ? firstRate / rate : 1.0;
        mCavityDirection[i][1] = rate > 0.0 ? secondRate / rate : 0.0;
        const double angle = mCavityExchangeScale[i] * rate;
        mCavityCos[i] = std::cos(angle);
        mCavitySin[i] = std::sin(angle);
    }
}

// A direct-form state pair (y1, y2) only means an amplitude and a phase for one
// pole angle. Swapping c alone scales the ring by a phase-dependent factor, and
// a periodic bend then pumps it parametrically. So the delayed state is
// re-expressed for the new angle: with y1 = A cos(phi) and
// r*y2 = A cos(phi - w), A sin(phi) = (r*y2 - y1*cos(wOld)) / sin(wOld) and
// y2' = (y1*cos(wNew) + A sin(phi) * sin(wNew)) / r. Same for the residual (o1, o2).
void MarembaVoice::RetuneMode(ModeState& mode, double omega) const {
    ReconfigureMode(mode, mode.r, omega);
}

void MarembaVoice::ReconfigureMode(ModeState& mode, double radius, double omega) const {
    const double wOld = mode.w;
    const double rOld = mode.r;
    mode.r = radius;
    SetModeCoefficients(mode, omega);
    const double r = mode.r;
    const double sinOld = std::sin(wOld);
    if (!(r > 0.0) || sinOld < 1.0e-12) return;
    const double cosOld = std::cos(wOld);
    const double cosNew = std::cos(mode.w);
    const double k = std::sin(mode.w) / sinOld;
    mode.y2 = (mode.y1 * cosNew + (rOld * mode.y2 - mode.y1 * cosOld) * k) / r;
    mode.o2 = (mode.o1 * cosNew + (rOld * mode.o2 - mode.o1 * cosOld) * k) / r;
}

// For y[n] = A r^n cos(wn + phi): y1^2 - c*y1*y2 + s*y2^2 = (A r^n sin w)^2, so
// dividing by sin^2 w gives the squared amplitude independent of the phase
// (no false "silence" at zero crossings).
double MarembaVoice::ModeEnergy(const ModeState& mode) {
    const double q = mode.y1 * mode.y1 - mode.c * mode.y1 * mode.y2 + mode.s * mode.y2 * mode.y2;
    const double qo = mode.o1 * mode.o1 - mode.c * mode.o1 * mode.o2 + mode.s * mode.o2 * mode.o2;
    return (q + qo) * mode.invSin2;
}

double MarembaVoice::FlushedModeEnergy(ModeState& mode) {
    const double e = ModeEnergy(mode);
    if (e < kFlushEnergy) {
        mode.y1 = mode.y2 = mode.o1 = mode.o2 = 0.0;
    }
    return e;
}

double MarembaVoice::MembraneEnergy() const {
    const double displacement2 = mMirlitonDisplacement * mMirlitonDisplacement;
    return displacement2 + mMirlitonVelocity * mMirlitonVelocity
        + 0.5 * mMirlitonStiffness * displacement2 * displacement2;
}

bool MarembaVoice::HasNoSubnormalState() const {
    auto clean = [](const ModeState& mode) {
        for (double v : { mode.y1, mode.y2, mode.o1, mode.o2 }) {
            if (std::fpclassify(v) == FP_SUBNORMAL) return false;
        }
        return true;
    };
    for (int m = 0; m < kNumPitchedModes; ++m) {
        if (!clean(mModes[m])) return false;
    }
    for (const auto& mode : mBrushModes) if (!clean(mode)) return false;
    for (const auto& mode : mCollisionModes) if (!clean(mode)) return false;
    for (const auto& mode : mUpperCavity) if (!clean(mode)) return false;
    if (std::fpclassify(mMirlitonDisplacement) == FP_SUBNORMAL
        || std::fpclassify(mMirlitonVelocity) == FP_SUBNORMAL) return false;
    return clean(mMode0b) && clean(mTube) && mPickup.HasNoSubnormalState()
        && mMalletContact.HasNoSubnormalState();
}

double MarembaVoice::ComputeEnergy() const {
    if (!mActive) return 0.0;
    double energy = 0.0;
    for (int m = 0; m < mNumModes; ++m) {
        energy += ModeEnergy(mModes[m]);
    }
    if (mHasTwin) energy += ModeEnergy(mMode0b);
    energy += ModeEnergy(mTube);
    for (const auto& mode : mUpperCavity) energy += ModeEnergy(mode);
    for (const auto& mode : mBrushModes) energy += ModeEnergy(mode);
    for (const auto& mode : mCollisionModes) energy += ModeEnergy(mode);
    return energy + mPickup.StateEnergy() + MembraneEnergy() + mMalletContact.Energy();
}

void MarembaVoice::StartDamping() {
    if (!mActive) return;
    mDamping = true;
    mHandDampPole = std::exp(-mHandDampRate / mSampleRate);
}

void MarembaVoice::StartFade(double seconds) {
    if (!mActive) return;
    const float step = static_cast<float>(1.0 / std::max(1.0, seconds * mSampleRate));
    if (!mFading || step > mFadeStep) mFadeStep = step;
    mFading = true;
}

void MarembaVoice::Retune(float globalCents) {
    if (!mActive) return;
    mPitchRatio = std::exp2(static_cast<double>(globalCents - mTriggerGlobalCents) / 1200.0);
    for (int m = 0; m < mNumModes; ++m) {
        double omega = mModes[m].omega * mPitchRatio;
        RetuneMode(mModes[m], omega);
    }
    UpdateTineStrainScales();
    if (mHasTwin) RetuneMode(mMode0b, mMode0b.omega * mPitchRatio);
    // A shared-size kalimba box does not change dimensions when a tine bends.
    if (mModel != EMarimbaModel::KalimbaArtisan) {
        RetuneMode(mTube, mTube.omega * mPitchRatio);
        for (auto& mode : mUpperCavity) if (mode.r > 0.0) RetuneMode(mode, mode.omega * mPitchRatio);
    }
    for (auto& mode : mBrushModes) if (mode.r > 0.0) RetuneMode(mode, mode.omega * mPitchRatio);
    UpdateCavityCoupling();
}

void MarembaVoice::Trigger(int noteNumber, float velocity, EMarimbaModel model,
                           float malletHardness, float strikePosition,
                           float resonatorTuneCents, float resonatorCoupling,
                           float decayMultiplier, float buzzAmount, float artifactsAmount,
                           float /*legacyPitchGlideIgnored*/,
                           double sampleRate, FastPRNG& prng,
                           float globalCents, float keyOffsetCents,
                           int malletType, float strikeJitter)
{
    const bool isRestrike = mActive && !mFading && (mNoteNumber == noteNumber);
    if (!isRestrike && mActive) {
        FastKill();
    }
    mActive = true;
    mReleased = false;
    mDamping = false;
    mFading = false;
    mFadeGain = 1.0f;
    mFadeStep = 0.0f;
    mNoteNumber = noteNumber;
    mModel = model;
    mVelocity = std::clamp(velocity, 0.01f, 1.0f);
    mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
    mRefStep = kReferenceRate / mSampleRate;
    mPickupContactGain = 0.060 / mRefStep;
    mNoiseScale = std::sqrt(mSampleRate / kReferenceRate);
    mFrictionFilterState = 0.0;
    mTriggerGlobalCents = globalCents;
    mPitchRatio = 1.0;

    const double fs = mSampleRate;
    const ModelParams params = GetModelParams(model);
    const auto response = GetInstrumentResponseProfile(model);
    mPickup.Configure(fs, model);
    for (int m = 0; m < kNumPitchedModes; ++m)
        mStrainWeights[m] = PiezoPickup::ModalStrainWeight(model, m);
    const StrikeCharacter character = GenerateStrikeCharacter(prng, strikeJitter);
    StrikeArtifacts art = GenerateStrikeArtifacts(prng, mVelocity, malletHardness, artifactsAmount, model);
    static constexpr double kPhysicalImpulseScale = 0.018;

    mHandDampRate = params.handDampRate;
    mHandDampPole = std::exp(-mHandDampRate / fs);

    // Fundamental Frequency with global tuning, per-key drift detuning, and micro-jitter
    const double totalPitchCents = static_cast<double>(noteNumber - 69) * 100.0
                                 + globalCents + keyOffsetCents + art.pitchJitterCents;
    const double f0 = 440.0 * std::exp2(totalPitchCents / 1200.0);

    // Register-dependent decay time: lower register sustains longer, treble is shorter
    const double noteFactor = std::clamp(130.81 / f0, 0.25, 3.5); // C3 as reference
    const double registerDecay = params.baseDecaySec * std::pow(noteFactor, static_cast<double>(params.trebleDampFactor));
    const double t60_f0 = std::min(registerDecay * std::pow(std::clamp(decayMultiplier, 0.10f, 8.0f), response.decayExponent), kMaxT60Seconds);

    // Strike position modulation
    float effectivePos = std::clamp(strikePosition + art.strikePositionOffset, 0.05f, 0.95f);

    // Compression exposes a firmer core at higher speed. This is a material
    // voicing law, not a calibrated measurement of a particular mallet.
    const bool tine = model == EMarimbaModel::KalimbaArtisan;
    const float baseHardness = std::pow(std::clamp(malletHardness + art.malletHardnessJitter, 0.05f, 0.95f), response.hardnessExponent);
    const float effectiveHardness = std::clamp(baseHardness
        + response.velocityCoreCompression * (mVelocity - 0.70f) * (1.0f - baseHardness), 0.02f, 0.98f);
    // The public velocity curve maps controller touch, then this common drive
    // feeds the tone AND release/impact noises. Quiet touches have no noise floor.
    const double impactStrength = std::pow(static_cast<double>(mVelocity), 1.25) * character.impulseScale * art.impulseScale;

    // ────────────────────────────────────────────────────────
    // Striker Physics: Marimba Mallets vs Kalimba Thumbs
    // ────────────────────────────────────────────────────────
    float modeWeightMult[kNumPitchedModes];
    for (int m = 0; m < kNumPitchedModes; ++m) modeWeightMult[m] = 1.0f;
    double contactTimeSec = 0.0005;
    float frictionMult = 1.0f;
    int mt = std::clamp(malletType, 0, 3);

    if (model != EMarimbaModel::KalimbaArtisan) {
        // Marimba Mallets: Soft Yarn, Medium Cord, Hard Rubber, Wood Baton
        // Nonlinear contact shortens with speed. Exponents and times are voiced
        // per material; this is not a solved Hertzian collision.
        switch (mt) {
            case 0: // Soft Wool Yarn
                contactTimeSec = (0.0016f + (1.0f - effectiveHardness) * 0.0008f) * std::pow(mVelocity, -0.28f);
                mMalletShockImpulse = 0.05f;
                frictionMult = 0.90f;
                modeWeightMult[0] = 1.10f;
                modeWeightMult[1] = 0.80f;
                modeWeightMult[2] = 0.25f;
                modeWeightMult[3] = 0.15f;
                modeWeightMult[4] = 0.08f;
                modeWeightMult[5] = 0.04f;
                modeWeightMult[6] = 0.02f;
                modeWeightMult[7] = 0.01f;
                break;
            case 1: // Medium Concert Cord
            default:
                contactTimeSec = (0.00065f + (1.0f - effectiveHardness) * 0.00045f) * std::pow(mVelocity, -0.25f);
                mMalletShockImpulse = 0.50f;
                frictionMult = 0.50f;
                // Balanced: all modes at 1.0
                break;
            case 2: // Hard Rubber
                contactTimeSec = (0.00030f + (1.0f - effectiveHardness) * 0.00018f) * std::pow(mVelocity, -0.30f);
                mMalletShockImpulse = 1.10f;
                frictionMult = 0.15f;
                modeWeightMult[0] = 0.95f;
                modeWeightMult[1] = 1.15f;
                modeWeightMult[2] = 1.25f;
                modeWeightMult[3] = 1.30f;
                modeWeightMult[4] = 1.25f;
                modeWeightMult[5] = 1.15f;
                modeWeightMult[6] = 1.05f;
                modeWeightMult[7] = 0.95f;
                break;
            case 3: // Wood Baton
                contactTimeSec = (0.00014f + (1.0f - effectiveHardness) * 0.00008f) * std::pow(mVelocity, -0.35f);
                mMalletShockImpulse = 1.80f;
                frictionMult = 0.05f;
                modeWeightMult[0] = 0.90f;
                modeWeightMult[1] = 1.25f;
                modeWeightMult[2] = 1.40f;
                modeWeightMult[3] = 1.50f;
                modeWeightMult[4] = 1.45f;
                modeWeightMult[5] = 1.35f;
                modeWeightMult[6] = 1.20f;
                modeWeightMult[7] = 1.05f;
                break;
        }
    } else {
        // Kalimba Thumbs: Thumb Flesh, Natural Thumb, Thumbnail Snap, Thumb Pick
        switch (mt) {
            case 0: // Thumb Flesh Pad
                contactTimeSec = (0.00095f + (1.0f - effectiveHardness) * 0.00045f) * std::pow(mVelocity, -0.18f);
                mMalletShockImpulse = 0.15f;
                frictionMult = 0.20f;
                modeWeightMult[0] = 1.05f;
                modeWeightMult[1] = 0.60f;
                modeWeightMult[2] = 0.30f;
                modeWeightMult[3] = 0.15f;
                modeWeightMult[4] = 0.08f;
                modeWeightMult[5] = 0.04f;
                modeWeightMult[6] = 0.02f;
                modeWeightMult[7] = 0.01f;
                break;
            case 1: // Natural Thumb
            default:
                contactTimeSec = (0.00045f + (1.0f - effectiveHardness) * 0.00025f) * std::pow(mVelocity, -0.18f);
                mMalletShockImpulse = 0.50f;
                frictionMult = 0.50f;
                modeWeightMult[0] = 1.00f;
                modeWeightMult[1] = 1.00f;
                modeWeightMult[2] = 0.85f;
                modeWeightMult[3] = 0.70f;
                modeWeightMult[4] = 0.55f;
                modeWeightMult[5] = 0.40f;
                modeWeightMult[6] = 0.30f;
                modeWeightMult[7] = 0.20f;
                break;
            case 2: // Thumbnail Snap
                contactTimeSec = (0.00020f + (1.0f - effectiveHardness) * 0.00010f) * std::pow(mVelocity, -0.20f);
                mMalletShockImpulse = 1.30f;
                frictionMult = 0.30f;
                modeWeightMult[0] = 0.95f;
                modeWeightMult[1] = 1.20f;
                modeWeightMult[2] = 1.35f;
                modeWeightMult[3] = 1.45f;
                modeWeightMult[4] = 1.40f;
                modeWeightMult[5] = 1.30f;
                modeWeightMult[6] = 1.15f;
                modeWeightMult[7] = 0.95f;
                break;
            case 3: // Thumb Pick
                contactTimeSec = (0.00012f + (1.0f - effectiveHardness) * 0.00006f) * std::pow(mVelocity, -0.22f);
                mMalletShockImpulse = 1.90f;
                frictionMult = 0.15f;
                modeWeightMult[0] = 0.90f;
                modeWeightMult[1] = 1.30f;
                modeWeightMult[2] = 1.45f;
                modeWeightMult[3] = 1.55f;
                modeWeightMult[4] = 1.50f;
                modeWeightMult[5] = 1.40f;
                modeWeightMult[6] = 1.25f;
                modeWeightMult[7] = 1.05f;
                break;
        }
    }

    // Flesh releases a tine progressively, whereas a firm nail contact slips
    // off quickly. Hardness changes this release interval as well as amplitude.
    contactTimeSec *= response.contactTimeScale * (tine ? 0.55 + 1.8 * (1.0 - effectiveHardness) : 1.0);

    // A small treble bar has a shorter contact, but the limiting duration
    // still shortens with touch. A smooth fourth-order minimum avoids the old
    // plateau where soft/medium mallets lost ALL velocity-dependent filtering.
    const double registerLimit = kMaxContactCycles / f0 * (1.10 - 0.25 * mVelocity);
    const double relativeDuration = contactTimeSec / registerLimit;
    contactTimeSec /= std::sqrt(std::sqrt(1.0 + std::pow(relativeDuration, 4.0)));
    mContactSkew = tine ? 0.0 : 0.12 + 0.18 * (1.0 - effectiveHardness);

    // A quiet organic floor remains at Variance = 0; the control increases it.
    // Fractional duration and independent shape/partial balance avoid relying
    // on an integer sample count or on random clack alone to distinguish hits.
    contactTimeSec *= character.contactTimeScale;
    mContactSkew = std::clamp(mContactSkew + character.pulseSkewOffset, -0.3, 0.6);
    mMalletShockImpulse = std::clamp(static_cast<float>(mMalletShockImpulse * character.shockScale), 0.0f, 3.0f);
    for (int m = 0; m < kNumPitchedModes; ++m)
        modeWeightMult[m] = std::clamp(static_cast<float>(modeWeightMult[m] * character.modalScale[m]), 0.01f, 4.0f);

    // Viscoelastic contact damping of the upper modes: the per-sample factor
    // voiced at the reference rate, converted to a per-second constant.
    const double contactDampLog = std::log(1.0 - params.contactDampingFactor * 0.003) * mRefStep;

    // ────────────────────────────────────────────────────────
    // Resonator air loss and reciprocal coupling setup
    // ────────────────────────────────────────────────────────
    double fRes = (tine ? 245.0 : f0) * std::exp2(resonatorTuneCents / 1200.0);
    fRes = std::min(fRes, fs * 0.45);
    const double decayScale = std::clamp(decayMultiplier, 0.10f, 4.0f);
    const double qRes = params.resonatorQ * (0.8 + 0.4 * resonatorCoupling) * std::pow(decayScale, response.resonatorDecayExponent);
    mResCoupling = params.couplingStrength * std::pow(std::clamp(resonatorCoupling, 0.0f, 1.0f), response.couplingExponent);
    // A restrike can change the aperture immediately, but the old air pressure
    // must remain audible while its microphone radiation gain moves smoothly.
    mResReadoutStep = 1.0 - std::exp(-1.0 / (0.005 * fs));
    if (!isRestrike) mResReadoutGain = mResCoupling;
    // Air loading is now produced by the pressure feeding back on the bar.
    // Do not also bake the old frequency-independent loading into its pole:
    // that double-counts radiation and misses the effect of cavity detuning.
    const double tau_f0 = t60_f0 / 6.907755;
    const double r_0 = std::exp(-1.0 / (tau_f0 * fs));

    // Restrike: the previous ring keeps sounding in the residual states and is
    // damped gradually over the new contact (no instant cut); the new stroke
    // excites the main states from zero.
    const int contactSamples = static_cast<int>(std::ceil(std::max(3.0, contactTimeSec * fs)));

    // ────────────────────────────────────────────────────────
    // Configure 8 Pitched Bar Modes with physics-based damping
    // ────────────────────────────────────────────────────────
    // Keep the tuned undercut ratios through C5. Only the high register moves
    // smoothly toward a uniform beam; the old C4 onset made the entire middle
    // octave prematurely inharmonic. This transition is an instrument-voicing
    // choice, not a claim that all real treble bars share one geometry.
    float trebleMorph = 0.0f;
    if (model != EMarimbaModel::KalimbaArtisan) {
        trebleMorph = std::clamp((static_cast<float>(noteNumber) - 72.0f) / 24.0f, 0.0f, 1.0f);
        trebleMorph = trebleMorph * trebleMorph * (3.0f - 2.0f * trebleMorph);
    }
    // A thumb releases the playable end of a clamped tine, whereas a mallet
    // traverses the whole free bar. Soft/wide contact spatially averages modes
    // instead of applying a generic treble boost at every off-centre position.
    const double spatialPosition = tine ? 0.65 + 0.35 * effectivePos : effectivePos;
    const double footprint = response.contactWidthScale * (tine ? 0.025 + 0.04 * (1.0 - effectiveHardness)
        : (0.025 + 0.06 * (1.0 - baseHardness) + (mt == 0 ? 0.045 : 0.0))
          * (0.72 + 0.36 * std::pow(mVelocity, 0.4f)));
    // Soft fingers/yarn absorb more of the old ring; hard, brief strokes let
    // more sustain through. Tines retain the diagonal release approximation;
    // struck wood uses these losses in a local finite-mass contact correction.
    const double contactLoss = (tine ? 2.0 - 0.85 * effectiveHardness - 0.12 * mt
        : 0.35 + 1.9 * (1.0 - effectiveHardness) * (1.20 - 0.15 * mt))
                             * (0.55 + 0.45 * std::sqrt(mVelocity));
    static constexpr float kUniformBarRatios[kNumPitchedModes] = {
        1.0f, 2.75654f, 5.40392f, 8.93295f, 13.3443f, 18.6382f, 24.8146f, 31.8735f
    };

    double contactMobility[kNumPitchedModes]{};
    const int previousModes = isRestrike ? mNumModes : 0;
    mNumModes = kNumPitchedModes;
    const auto radiationGeometry = GetModalRadiationGeometry(model, f0);
    for (int m = 0; m < kNumPitchedModes; ++m) {
        float ratio = params.overtoneRatio[m];
        if (trebleMorph > 0.0f) {
            ratio = (1.0f - trebleMorph) * ratio + trebleMorph * kUniformBarRatios[m];
        }

        double fm = f0 * ratio;

        // Register-dependent overtone stretching: subtle (+/- 1.5% across range)
        // Higher register bars are shorter/stiffer, slightly stretching upper partials
        if (m >= 2) {
            double registerStretch = 1.0 + 0.0003 * static_cast<double>(m - 1)
                                   * std::clamp(static_cast<double>(noteNumber - 60) / 36.0, -1.0, 1.0);
            fm *= registerStretch;
        }

        // Modes near or above Nyquist are dropped rather than stacked at one
        // frequency (ratios increase with m, so all higher modes go too).
        if (fm >= fs * kModeDropFraction) {
            mNumModes = m;
            break;
        }

        // Physics-based frequency-dependent damping
        // Higher register bars have greater air radiation and mounting cord losses
        double regDampMult = 1.0 + 1.2 * trebleMorph;
        double freqRatio = fm / f0;
        double dampingFactor = 1.0 + (params.alphaRadiation * regDampMult) * freqRatio * freqRatio
                                   + (params.alphaInternal * regDampMult) * freqRatio;
        double t60_m = std::min(t60_f0 / dampingFactor, kMaxT60Seconds);
        double tau_m = t60_m / 6.907755;
        double omega_m = kTwoPiD * fm / fs;

        ModeState& mode = mModes[m];
        if (isRestrike && m < previousModes) {
            mode.o1 += mode.y1;
            mode.o2 += mode.y2;
        } else {
            mode.o1 = 0.0;
            mode.o2 = 0.0;
        }
        mode.y1 = 0.0;
        mode.y2 = 0.0;
        mode.omega = omega_m;
        const double radius = (m == 0) ? r_0 : std::exp(-1.0 / (tau_m * fs));
        ReconfigureMode(mode, radius, omega_m);
        mRadiation[m] = ComputeModalRadiation(m, fm, radiationGeometry, tine);
        const double posWeight = ModalContactWeight(m, spatialPosition, footprint, tine);

        // Mallet hardness scaling: hard mallets excite more high overtones
        float overtoneScaling = (m == 0) ? 1.0f : (0.40f + 0.60f * effectiveHardness);

        // The bar spectrum follows contact duration, core compression and
        // spatial averaging, rather than an unrelated per-mode velocity EQ.
        // A tine starts from static deflection (1/omega²); its velocity-like
        // readout contributes omega, hence 1/ratio. These separately voiced
        // readout weights include the bridge/radiation response, not modal mass.
        static constexpr double kTineReadout[kNumPitchedModes] = {
            1.0, 1.38, 0.79, 0.52, 0.34, 0.168, 0.117, 0.078
        };
        const double modalWeight = tine ? kTineReadout[m] / ratio : params.modeWeights[m];
        const double weight = modalWeight * posWeight * overtoneScaling * modeWeightMult[m];
        contactMobility[m] = modalWeight * overtoneScaling * modeWeightMult[m];
        const double participation = ModalContactParticipation(m, spatialPosition, footprint, tine);
        mode.contactPole = (m >= 2) ? std::exp(contactDampLog * participation) : 1.0;
        mode.restrikePole = std::exp(-contactLoss * participation / contactSamples);

        // sin(w) gives a unit excitation an approximately rate-independent
        // sine amplitude (up to the pole radius over one sample).
        mode.b0 = weight * std::sin(omega_m) * kPhysicalImpulseScale;
    }
    for (int m = mNumModes; m < kNumPitchedModes; ++m) {
        mModes[m] = ModeState{};
    }
    UpdateTineStrainScales();

    // ────────────────────────────────────────────────────────
    // Weak voiced split mode (subtle beating, not calibrated wood anisotropy)
    // ────────────────────────────────────────────────────────
    // Fade split down to 0.0 in high register to prevent flutter/beating on octaves and high notes
    const bool hadTwin = isRestrike && mHasTwin;
    float effectiveSplitCents = params.anisotropicSplitCents * (1.0f - trebleMorph);
    double f0b = f0 * std::exp2(effectiveSplitCents / 1200.0);
    mHasTwin = effectiveSplitCents > 0.01f && mNumModes > 0 && f0b < fs * kModeDropFraction;
    if (mHasTwin) {
        if (hadTwin) {
            mMode0b.o1 += mMode0b.y1;
            mMode0b.o2 += mMode0b.y2;
        } else {
            mMode0b.o1 = mMode0b.o2 = 0.0;
        }
        mMode0b.y1 = mMode0b.y2 = 0.0;
        double omega_0b = kTwoPiD * f0b / fs;
        mMode0b.omega = omega_0b;
        ReconfigureMode(mMode0b, r_0, omega_0b);
        // Twin mode provides subtle natural wood grain beating (15% amplitude)
        mMode0b.contactPole = mModes[0].contactPole;
        mMode0b.restrikePole = mModes[0].restrikePole;
        mMode0b.b0 = 0.15 * mModes[0].b0 / std::sin(mModes[0].omega) * std::sin(omega_0b);
    } else {
        // No anisotropic split (Kalimba steel tines or high treble bars)
        mMode0b = ModeState{};
    }

    // ────────────────────────────────────────────────────────
    // Integrated mallet force / finite thumb release
    // ────────────────────────────────────────────────────────
    // For a tine, the same sine bank represents velocity after release from
    // static deflection. A slower flesh release progressively frees the modes;
    // a nail/pick releases faster. No impact shock is fed into the tine itself.
    const double contactSamplesExact = std::max(3.0, contactTimeSec * fs);
    mContactSamplesTotal = contactSamples;
    mContactSample = 0;
    mContactStep = 1.0 / contactSamplesExact;
    static constexpr double kMalletIntegral[4] = {0.47, 0.57, 0.64, 0.75};
    static constexpr double kThumbIntegral[4] = {0.51, 0.58, 0.69, 0.82};
    const double referenceContact = tine ? 0.00045 : 0.00065;
    const double impulse = impactStrength * (0.9 + 0.8 * effectiveHardness)
        * referenceContact * kReferenceRate * (tine ? kThumbIntegral[mt] : kMalletIntegral[mt]);
    // A hard core contributes a small, positive fast force at high velocity.
    // It shares the impulse budget instead of adding velocity-dependent energy.
    const double shockFraction = tine ? 0.0
        : 0.08 * mMalletShockImpulse * std::pow(mVelocity, 0.45f);
    const double primaryShare = 1.0 - art.reboundGain - art.brushGain;
    mContactImpulse = impulse * primaryShare * (1.0 - shockFraction);
    mShockGain = impulse * primaryShare * shockFraction / 1.7;
    mShockSamplesTotal = mShockGain > 0.0 ? static_cast<int>(std::ceil(kShockDurationRef / mRefStep)) : 0;
    mShockSample = 0;

    // Friction modulates the contact force; it cannot overpower a quiet stroke
    // or pull the mallet toward the bar after separation.
    mFrictionGain = art.frictionNoiseBurst * (1.0f - effectiveHardness * 0.6f) * frictionMult;
    mFrictionAlpha = SmootherAtRate(0.05, fs);
    // The integrated pulse already contains dt. White noise feeding this
    // bandlimited multiplier therefore needs sqrt(fs / reference), not dt.
    mFrictionNoiseScale = mNoiseScale;

    mRestrikeSamplesLeft = isRestrike ? contactSamples : 0;
    mRestrikePole = mNumModes > 0 ? mModes[0].restrikePole : 1.0;
    mLocalContactActive = isRestrike && !tine && mNumModes > 0;
    mMalletContact.Reset();
    if (mLocalContactActive)
        mMalletContact.Configure(mNumModes, spatialPosition, footprint, contactLoss, contactMobility);

    // ────────────────────────────────────────────────────────
    // Strike Clack Noise Burst (replaces artificial tonal clack mode)
    // ────────────────────────────────────────────────────────
    {
        double clackF = std::clamp(params.clackFrequency + (f0 * 0.35), 2500.0, 8000.0);
        clackF = std::min(clackF, fs * 0.45);
        // Bandpass Q derived from bandwidth: Q = f_center / bandwidth
        double clackBW = std::clamp(params.clackBandwidth, 800.0f, 5000.0f);
        double clackQ = clackF / clackBW;
        double rClack = std::clamp(std::exp(-kPiD * clackF / (clackQ * fs)), 0.0, 0.999);
        double wClack = kTwoPiD * clackF / fs;
        double rClackRef = std::clamp(std::exp(-kPiD * clackF / (clackQ * kReferenceRate)), 0.0, 0.999);
        double wClackRef = kTwoPiD * clackF / kReferenceRate;
        mClackBpC = 2.0 * rClack * std::cos(wClack);
        mClackBpS = rClack * rClack;
        // Centre gain as voiced at the reference rate ((1 - r^2) input gain there)
        mClackBpB = RateInvariantResonatorGain(1.0 - rClackRef * rClackRef, rClackRef, wClackRef, rClack, wClack);
        if (!isRestrike) mClackBpY1 = mClackBpY2 = 0.0; // a ringing clack of a restruck bar rings out

        // Clack decay: very short (2-5 ms)
        double clackDecayMs = 2.0 + (1.0 - effectiveHardness) * 3.0;
        mClackDecayRate = std::exp(-1000.0 / (clackDecayMs * fs));

        // Clack gain: scales with hardness, velocity, and wood friction
        double clackIntensity = mMalletShockImpulse * effectiveHardness * impactStrength * std::pow(mVelocity, 0.55f);
        mClackEnvelope = clackIntensity * 0.05 * (1.0 + params.woodInternalFriction * 100.0);
        // This short contact transient is independent of Artifacts. Raising
        // playing imperfections must never become a broadband noise fader.
        mClackEnvelope *= primaryShare;
        mClackActive = mClackEnvelope > 0.0;
    }

    // Three acoustic pressure modes. Only the concert tube has the odd
    // closed/open sequence. Gourd higher modes are weak and lossy; the kalimba
    // box frequencies stay fixed across keys. These are instrument-voicing
    // dimensions and loss estimates, not measurements of a named specimen.
    double ratios[3] = {1.0, 3.0, 5.0};
    double qScales[3] = {1.0, 0.65, 0.45};
    double apertures[3] = {1.0, 0.48, 0.28};
    mCavityReadout[0] = 3.8;
    mCavityReadout[1] = 7.0;
    mCavityReadout[2] = 4.5;
    if (model == EMarimbaModel::MayanPadauk) {
        ratios[1] = 2.63; ratios[2] = 4.37;
        qScales[1] = 0.60; qScales[2] = 0.40;
        apertures[1] = 0.65; apertures[2] = 0.40;
        mCavityReadout[1] = 2.1; mCavityReadout[2] = 1.4;
    } else if (model == EMarimbaModel::BalafonAncestral) {
        ratios[1] = 2.81; ratios[2] = 4.62;
        qScales[1] = 0.38; qScales[2] = 0.28;
        apertures[1] = 0.32; apertures[2] = 0.20;
        mCavityReadout[1] = 6.0; mCavityReadout[2] = 4.0;
    } else if (tine) {
        ratios[1] = 2.31; ratios[2] = 4.17;
        qScales[0] = 0.35; qScales[1] = 0.45; qScales[2] = 0.45;
        mCavityReadout[0] = 18.0; mCavityReadout[1] = 8.0; mCavityReadout[2] = 5.4;
        apertures[1] = 0.80; apertures[2] = 0.60;
    }
    mNumCavityModes = 0;
    for (int i = 0; i < 3; ++i) {
        auto& mode = i == 0 ? mTube : mUpperCavity[i - 1];
        const double frequency = fRes * ratios[i];
        if (frequency >= fs * kModeDropFraction || mNumModes == 0) {
            mode = ModeState{};
            mCavityCos[i] = 1.0; mCavitySin[i] = 0.0;
            continue;
        }
        ++mNumCavityModes;
        const double airLoss = kPiD * frequency / std::max(1.5, qRes * qScales[i]);
        mode.omega = kTwoPiD * frequency / fs;
        ReconfigureMode(mode, std::exp(-airLoss / fs), mode.omega);
        mode.b0 = 0.0;
        if (!isRestrike) mode.y1 = mode.y2 = 0.0;
        mode.o1 = mode.o2 = 0.0;
        // For weak resonant coupling, additional bar loss is g²/(4*airLoss).
        // This expresses aperture strength in the existing control range while
        // the actual detuning and pressure state determine instantaneous load.
        mCavityExchangeScale[i] = 2.0 * std::sqrt(airLoss
            * response.radiationLoading * mResCoupling) * apertures[i] * (tine ? 1.6 : 1.0) / fs;
    }

    UpdateCavityCoupling();
    SetBuzz(buzzAmount);
    // A lightly tensioned thin membrane has a short pressure-driven response.
    // Its stiffness rises with excursion (positive quartic elastic potential).
    // Broad resonances avoid turning each gourd into another pitched voice.
    const double membraneHz = std::clamp(fRes * 2.7, 430.0, 3400.0);
    mMirlitonStep = kTwoPiD * membraneHz / fs;
    mMirlitonLoss = kTwoPiD * membraneHz / (2.0 * 0.85 * fs);
    mMirlitonForceStep = mMirlitonStep * mMirlitonStep / (2.0 * (1.0 + mMirlitonLoss));
    mMirlitonVelocityStep = mMirlitonStep / (1.0 + mMirlitonLoss);
    mMirlitonInverseStep = 2.0 / mMirlitonStep;
    // A restrike keeps the membrane moving; with Buzz off it releases freely.
    if (!isRestrike) {
        mMirlitonDisplacement = mMirlitonVelocity = 0.0;
    }

    // Schedule a small rebound and/or an accidental brush of a neighboring
    // key. Both take energy from the primary hit; no delayed random noise.
    mArtifactSample = mArtifactEnd = 0;
    mReboundImpulse = impulse * art.reboundGain;
    mBrushImpulse = impulse * art.brushGain;
    mReboundStart = static_cast<int>(art.reboundDelaySec * fs);
    mBrushStart = static_cast<int>(art.brushDelaySec * fs);
    const double secondaryDuration = std::max(3.0, contactTimeSec * 1.35 * fs);
    mSecondaryStep = 1.0 / secondaryDuration;
    if (mReboundImpulse > 0.0) mArtifactEnd = mReboundStart + static_cast<int>(std::ceil(secondaryDuration));
    if (mBrushImpulse > 0.0) mArtifactEnd = std::max(mArtifactEnd, mBrushStart + static_cast<int>(std::ceil(secondaryDuration)));
    const double brushF0 = f0 * std::exp2(art.brushSemitones / 12.0);
    // A clean restrike cannot retune a neighboring key that is still ringing.
    // Rebase its current bend as the new trigger reference, then change its
    // modal coefficients only when another brush actually excites it.
    for (auto& mode : mBrushModes) mode.omega = mode.w;
    for (int m = 0; m < 2 && mBrushImpulse > 0.0; ++m) {
        auto& mode = mBrushModes[m];
        const double frequency = brushF0 * (m == 0 ? 1.0 : params.overtoneRatio[1]);
        if (frequency >= fs * kModeDropFraction) { mode = ModeState{}; continue; }
        mode.omega = kTwoPiD * frequency / fs;
        ReconfigureMode(mode, std::exp(-6.907755 / (std::max(0.025, t60_f0 * (m == 0 ? 0.4 : 0.06)) * fs)), mode.omega);
        mode.b0 = std::sin(mode.w) * kPhysicalImpulseScale * (m == 0 ? 0.8 : 0.12);
    }
    mBrushPickupWeight = PiezoPickup::ModalStrainWeight(model, 0);
    mBrushActive = mBrushImpulse > 0.0 || ModeEnergy(mBrushModes[0]) + ModeEnergy(mBrushModes[1]) > kFlushEnergy;

    // A loose mounting is struck as bar/tine motion crosses its clearance.
    // Closing velocity sets a bounded impact, spread over a finite contact.
    // Two short modes ring at rail/bridge frequencies, independent of the note.
    mCollisionEnvelope = art.collisionAmount * response.collisionLevel;
    mCollisionActive = mCollisionEnvelope > 0.0 || ModeEnergy(mCollisionModes[0]) + ModeEnergy(mCollisionModes[1]) > kFlushEnergy;
    mCollisionGap = std::max(0.00005, impulse * kPhysicalImpulseScale * (0.08 + 0.22 * (1.0 - art.collisionAmount)));
    mCollisionDecay = std::exp(-1.0 / ((tine ? 0.09 : 0.055) * fs));
    mCollisionPrevious = mModes[0].y1 + mModes[0].o1;
    mCollisionSide = 0;
    mCollisionImpulse = 0.0;
    mCollisionImpulseLimit = impulse * kPhysicalImpulseScale * 0.4;
    // Contacts must finish before the next half-cycle, including in treble.
    const double collisionDuration = std::max(1.0, std::min(tine ? 0.00012 : 0.00025, 0.18 / f0) * fs);
    mCollisionAmplitudeRatio = 0.35 * art.collisionAmount;
    mCollisionPulseStep = 1.0 / collisionDuration;
    mCollisionSamplesTotal = static_cast<int>(std::ceil(collisionDuration));
    mCollisionSample = mCollisionSamplesTotal;
    for (int m = 0; m < 2; ++m) {
        auto& mode = mCollisionModes[m];
        const double frequency = std::min(fs * 0.4, params.frameBodyFreq * response.collisionRatio * (m == 0 ? 1.0 : 2.71));
        mode.omega = kTwoPiD * frequency / fs;
        ReconfigureMode(mode, std::exp(-6.907755 / (response.collisionDecaySec * (m == 0 ? 1.0 : 0.43) * fs)), mode.omega);
        mode.b0 = std::sin(mode.w) * (m == 0 ? 1.0 : 0.45);
    }
}

void MarembaVoice::SetBuzz(float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const bool native = mModel == EMarimbaModel::BalafonAncestral;
    mHasMirliton = amount > 0.001f;
    mMirlitonThreshold = (native ? 0.008 : 0.012) * (1.5 - amount);
    mMirlitonDrive = amount * (native ? 2.8 : 1.4);
    mMirlitonStiffness = native ? 28.0 : 18.0;
}

void MarembaVoice::CoupleCavity(ModeState& cavity, int barIndex, double cosine, double sine, bool residual) {
    auto& bar = mModes[barIndex];
    if (sine == 0.0 || !(bar.r > 0.0)) return;
    const double pressure = cavity.y1;
    // Bars read displacement; the tine bank reads velocity after release from
    // static deflection. Normalize both by their free-oscillator invariant.
    const bool tine = mModel == EMarimbaModel::KalimbaArtisan;
    double& first = residual ? bar.o1 : bar.y1;
    double& second = residual ? bar.o2 : bar.y2;
    const double barPosition = first;
    const double velocity = tine ? barPosition
        : -(bar.r * second - barPosition * bar.cosW) * bar.invSin;
    const double nextVelocity = cosine * velocity - sine * pressure;
    const double nextPressure = sine * velocity + cosine * pressure;
    cavity.y1 = nextPressure;
    // Holding the air quadrature fixed requires only the pressure delta in
    // its delayed state. Avoid decoding/re-encoding that unchanged coordinate.
    cavity.y2 += (nextPressure - pressure) * cavity.cosOverR;
    if (tine) {
        const double delta = nextVelocity - velocity;
        first += delta;
        second += delta * bar.cosOverR;
    } else {
        second -= (nextVelocity - velocity) * bar.sinOverR;
    }
}

void MarembaVoice::CoupleCavityAperture(ModeState& cavity, int cavityIndex, bool residual) {
    const double sine = mCavitySin[cavityIndex];
    if (sine == 0.0) return;
    const double cosine = mCavityCos[cavityIndex];
    if (mCavityConnections[cavityIndex] == 1) {
        CoupleCavity(cavity, mCavityBarIndex[cavityIndex][0], cosine, sine, residual);
        return;
    }
    auto& firstBar = mModes[mCavityBarIndex[cavityIndex][0]];
    auto& secondBar = mModes[mCavityBarIndex[cavityIndex][1]];
    const bool tine = mModel == EMarimbaModel::KalimbaArtisan;
    double& firstQ = residual ? firstBar.o1 : firstBar.y1;
    double& firstDelayed = residual ? firstBar.o2 : firstBar.y2;
    double& secondQ = residual ? secondBar.o1 : secondBar.y1;
    double& secondDelayed = residual ? secondBar.o2 : secondBar.y2;
    const double firstVelocity = tine ? firstQ
        : -(firstBar.r * firstDelayed - firstQ * firstBar.cosW) * firstBar.invSin;
    const double secondVelocity = tine ? secondQ
        : -(secondBar.r * secondDelayed - secondQ * secondBar.cosW) * secondBar.invSin;
    const double firstDirection = mCavityDirection[cavityIndex][0];
    const double secondDirection = mCavityDirection[cavityIndex][1];
    const double projectedVelocity = firstDirection * firstVelocity + secondDirection * secondVelocity;
    const double pressure = cavity.y1;
    const double nextPressure = sine * projectedVelocity + cosine * pressure;
    const double velocityDelta = cosine * projectedVelocity - sine * pressure - projectedVelocity;
    cavity.y1 = nextPressure;
    cavity.y2 += (nextPressure - pressure) * cavity.cosOverR;
    const double firstDelta = firstDirection * velocityDelta;
    const double secondDelta = secondDirection * velocityDelta;
    if (tine) {
        firstQ += firstDelta;
        firstDelayed += firstDelta * firstBar.cosOverR;
        secondQ += secondDelta;
        secondDelayed += secondDelta * secondBar.cosOverR;
    } else {
        firstDelayed -= firstDelta * firstBar.sinOverR;
        secondDelayed -= secondDelta * secondBar.sinOverR;
    }
}

bool MarembaVoice::ProcessSample(float& outClose, float& outFar, float& outPiezo) {
    if (!mActive) {
        outClose = 0.0f;
        outFar = 0.0f;
        outPiezo = 0.0f;
        mMechanicalSample = 0.0f;
        return false;
    }

    // 1. Integrate positive contact force / progressive tine release over
    // this sample. The sum telescopes to the same impulse at every rate.
    double excitation = 0.0;
    double contactFraction = 0.0;
    const bool inContact = (mContactSample < mContactSamplesTotal);
    if (inContact) {
        const double t0 = mContactSample * mContactStep;
        // Roundoff at the last interval can subtract almost equal values.
        const double pulse = std::max(0.0, ContactCumulative(t0 + mContactStep, mContactSkew)
                           - ContactCumulative(t0, mContactSkew));
        contactFraction = pulse;
        excitation = mContactImpulse * pulse;
        if (mFrictionGain > 0.0001) {
            const double noise = mLocalPrng.NextBipolar() * mFrictionNoiseScale;
            mFrictionFilterState += mFrictionAlpha * (noise - mFrictionFilterState);
            excitation *= std::clamp(1.0 + mFrictionFilterState * mFrictionGain, 0.0, 2.0);
        }
        ++mContactSample;
    }

    // Initial contact shock impulse spike (first 34 us)
    if (mShockSample < mShockSamplesTotal) {
        const double t0 = static_cast<double>(mShockSample) * mRefStep;
        excitation += mShockGain * (ShockCumulative(t0 + mRefStep) - ShockCumulative(t0));
        mShockSample++;
    }

    // Integrated delayed contacts are positive force pulses at every rate.
    double brushForce = 0.0;
    if (mArtifactSample < mArtifactEnd) {
        const auto pulse = [&](int start) {
            const double t = (mArtifactSample - start) * mSecondaryStep;
            return std::max(0.0, ContactCumulative(t + mSecondaryStep, 0.0) - ContactCumulative(t, 0.0));
        };
        const double damp = mDamping ? 0.0 : 1.0;
        excitation += damp * mReboundImpulse * pulse(mReboundStart);
        brushForce = damp * mBrushImpulse * pulse(mBrushStart);
        ++mArtifactSample;
    }

    // The old ring changes the reaction at the finite mallet footprint.
    // A local relative-velocity loss redistributes its energy between bending
    // modes and the mallet inertia, instead of attenuating every modal phase
    // equally. The nominal fresh strike remains the existing positive pulse.
    if (mLocalContactActive && mRestrikeSamplesLeft > 0) {
        double velocities[kNumPitchedModes];
        for (int m = 0; m < mNumModes; ++m) {
            const auto& mode = mModes[m];
            velocities[m] = -(mode.r * mode.o2 - mode.o1 * mode.cosW) * mode.invSin;
        }
        double previous[kNumPitchedModes];
        for (int m = 0; m < mNumModes; ++m) previous[m] = velocities[m];
        mMalletContact.Process(velocities, contactFraction);
        for (int m = 0; m < mNumModes; ++m)
            mModes[m].o2 -= (velocities[m] - previous[m]) * mModes[m].sinOverR;
        if (mDamping) mMalletContact.Damp(mHandDampPole);
    }

    // Air oscillators and the bar form one system. Advance their lossy free
    // motion, then rotate pressure/velocity at each aperture. Unlike a driven
    // output filter, stored air pressure pushes back on the bar and modifies
    // its envelope according to detuning; all junctions preserve energy.
    double yRes = 0.0;
    for (int i = 0; i < mNumCavityModes; ++i) {
        auto& air = i == 0 ? mTube : mUpperCavity[i - 1];
        const double y = air.c * air.y1 - air.s * air.y2;
        const double pole = (mDamping ? mHandDampPole : 1.0)
            * (mRestrikeSamplesLeft > 0 ? mRestrikePole : 1.0);
        air.y2 = air.y1 * pole;
        air.y1 = y * pole;
        CoupleCavityAperture(air, i);
        // During the brief restrike, old and new motion are separately damped
        // bookkeeping oscillators. Exchange with each state independently so
        // their summed invariant still cannot grow at the junction.
        if (mRestrikeSamplesLeft > 0)
            CoupleCavityAperture(air, i, true);
        yRes += air.y1 * mCavityReadout[i];
    }

    // 3. Bar Modal Resonator Updates (8 pitched modes + twin mode 0b)
    double barCloseSum = 0.0, barFarSum = 0.0;
    double barMechanicalSum = 0.0;
    double mountingStrain = 0.0;

    const bool restriking = mRestrikeSamplesLeft > 0;
    const bool scaled = inContact || restriking || mDamping;
    const double handPole = mDamping ? mHandDampPole : 1.0;

    if (!scaled) {
        for (int m = 0; m < mNumModes; ++m) {
            ModeState& mode = mModes[m];
            const double ym = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * excitation;
            mode.y2 = mode.y1;
            mode.y1 = ym;
            const double quadrature = (mode.r * mode.y2 - ym * mode.cosW) * mode.invSin;
            barCloseSum += mRadiation[m].close.Read(ym, quadrature);
            barFarSum += mRadiation[m].far.Read(ym, quadrature);
            barMechanicalSum += ym * kFrameWeight[m];
            // The released-tine bank is velocity-like: integrate its modal
            // quadrature for bending strain. Bar modes already read displacement.
            mountingStrain += mModel == EMarimbaModel::KalimbaArtisan
                ? (mode.r * mode.y2 - mode.y1 * mode.cosW) * mTineStrainScales[m]
                : ym * mStrainWeights[m];
        }
        if (mHasTwin) {
            const double y0b = mMode0b.c * mMode0b.y1 - mMode0b.s * mMode0b.y2 + mMode0b.b0 * excitation;
            mMode0b.y2 = mMode0b.y1;
            mMode0b.y1 = y0b;
            barCloseSum += y0b;
            barFarSum += y0b;
            barMechanicalSum += y0b * 0.6;
            mountingStrain += y0b * mStrainWeights[0] * 0.6;
        }
    } else {
        // Damping here always scales BOTH states (a smaller pole radius), which
        // removes energy; scaling only y[n] would inject energy into low modes.
        auto step = [&](ModeState& mode, double pole, double* quadrature, bool localContact = false) -> double {
            const double ym = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * excitation;
            mode.y2 = mode.y1;
            mode.y1 = ym;
            if (quadrature)
                *quadrature = (mode.r * mode.y2 - ym * mode.cosW) * mode.invSin;
            double out = ym;
            if (restriking) {
                const double yo = mode.c * mode.o1 - mode.s * mode.o2;
                mode.o2 = mode.o1;
                mode.o1 = yo;
                out += yo;
                if (quadrature)
                    *quadrature += (mode.r * mode.o2 - yo * mode.cosW) * mode.invSin;
                if (!localContact) {
                    mode.o1 *= mode.restrikePole;
                    mode.o2 *= mode.restrikePole;
                }
            }
            if (pole != 1.0) {
                mode.y1 *= pole;
                mode.y2 *= pole;
                mode.o1 *= pole;
                mode.o2 *= pole;
            }
            return out;
        };
        for (int m = 0; m < mNumModes; ++m) {
            // Viscoelastic contact damping: the mallet damps the upper modes during contact
            double quadrature = 0.0;
            const double out = step(mModes[m], handPole * (inContact ? mModes[m].contactPole : 1.0), &quadrature,
                                    mLocalContactActive);
            barCloseSum += mRadiation[m].close.Read(out, quadrature);
            barFarSum += mRadiation[m].far.Read(out, quadrature);
            barMechanicalSum += out * kFrameWeight[m];
            const auto& mode = mModes[m];
            mountingStrain += mModel == EMarimbaModel::KalimbaArtisan
                ? (mode.r * (mode.y2 + mode.o2) - (mode.y1 + mode.o1) * mode.cosW)
                    * mTineStrainScales[m]
                : out * mStrainWeights[m];
        }
        if (mHasTwin) {
            const double out = step(mMode0b, handPole, nullptr);
            barCloseSum += out;
            barFarSum += out;
            barMechanicalSum += out * 0.6;
            mountingStrain += out * mStrainWeights[0] * 0.6;
        }
        if (restriking && --mRestrikeSamplesLeft == 0) {
            // The departing mallet carries away its stored perturbation energy.
            mMalletContact.Reset();
            mLocalContactActive = false;
            // Contact over: fold the damped previous ring back into the main states
            for (int m = 0; m < mNumModes; ++m) {
                mModes[m].y1 += mModes[m].o1;
                mModes[m].y2 += mModes[m].o2;
                mModes[m].o1 = mModes[m].o2 = 0.0;
            }
            mMode0b.y1 += mMode0b.o1;
            mMode0b.y2 += mMode0b.o2;
            mMode0b.o1 = mMode0b.o2 = 0.0;
        }
    }

    // 4. Strike Clack Noise Burst (broadband transient, not a tonal mode)
    double clackSound = 0.0;
    if (mClackActive) {
        double bpInput = 0.0;
        if (mClackEnvelope > 0.0) {
            bpInput = mLocalPrng.NextBipolar() * mNoiseScale * mClackEnvelope;
            mClackEnvelope *= mClackDecayRate;
            if (mClackEnvelope < 1.0e-7) mClackEnvelope = 0.0; // burst over: let the band-pass ring out
        }
        const double bpOut = mClackBpC * mClackBpY1 - mClackBpS * mClackBpY2 + mClackBpB * bpInput;
        mClackBpY2 = mClackBpY1;
        mClackBpY1 = bpOut;
        clackSound = bpOut;
        if (mClackEnvelope == 0.0 && std::abs(mClackBpY1) + std::abs(mClackBpY2) < 1.0e-10) {
            mClackBpY1 = mClackBpY2 = 0.0;
            mClackActive = false;
        }
    }

    const ModeState& mode0 = mModes[0];

    // 6. Dynamic hardening membrane. Pressure opens its slack region, then
    // stretching progressively raises the restoring force. A discrete-gradient
    // update of V(x)=x²/2+beta*x⁴/4 accounts for work and dissipated energy;
    // three monotone Newton iterations solve the tiny implicit step. There is
    // no autonomous noise oscillator or post-excitation energy source.
    double buzzComponent = 0.0;
    if (mHasMirliton || mMirlitonDisplacement != 0.0 || mMirlitonVelocity != 0.0) {
        // Local pressure is independent of microphone radiation calibration.
        const double cavityPressure = 3.8 * mTube.y1
            + 1.6 * mUpperCavity[0].y1 + 0.8 * mUpperCavity[1].y1;
        const double pressure = mHasMirliton ? cavityPressure * mMirlitonDrive : 0.0;
        const double magnitude = std::abs(pressure);
        const double force = magnitude > mMirlitonThreshold
            ? std::copysign(magnitude - mMirlitonThreshold, pressure) : 0.0;
        // A slack, resting membrane below threshold has exactly zero state.
        // Avoid three implicit solver iterations for this common default case;
        // pressure and its onset threshold are still evaluated every sample.
        if (force != 0.0 || mMirlitonDisplacement != 0.0 || mMirlitonVelocity != 0.0) {
            const double oldX = mMirlitonDisplacement;
            const double k = mMirlitonForceStep;
            const double target = mMirlitonVelocityStep * mMirlitonVelocity + k * force;
            double x = oldX + target;
            for (int iteration = 0; iteration < 3; ++iteration) {
                const double sum = x + oldX;
                const double sq = x * x + oldX * oldX;
                const double restoring = 0.5 * sum + 0.25 * mMirlitonStiffness * sum * sq;
                const double slope = 0.5 + 0.25 * mMirlitonStiffness
                    * (3.0 * x * x + 2.0 * x * oldX + oldX * oldX);
                x -= (x - oldX + k * restoring - target) / (1.0 + k * slope);
            }
            const double velocity = (x - oldX) * mMirlitonInverseStep - mMirlitonVelocity;
            const double pole = mDamping ? mHandDampPole : 1.0;
            mMirlitonDisplacement = x * pole;
            mMirlitonVelocity = velocity * pole;
            if (force == 0.0 && std::abs(x) + std::abs(velocity) < 1.0e-12)
                mMirlitonDisplacement = mMirlitonVelocity = 0.0;
        }
        // Membrane volume velocity radiates the buzz, naturally attenuating
        // low-frequency displacement and exposing its generated harmonics.
        buzzComponent = mMirlitonVelocity * 1.5;
    }

    // 7. Adjacent-key brush and vibration-driven mount/bridge collision.
    // No independent random excitation is generated after the primary contact.
    double brushSound = 0.0;
    if (mBrushActive) for (auto& mode : mBrushModes) {
        const double y = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * brushForce;
        mode.y2 = mode.y1 * handPole;
        mode.y1 = y * handPole;
        brushSound += y;
    }
    double collisionDrive = 0.0;
    if (mCollisionActive) {
        const double motion = mode0.y1 + mode0.o1;
        const double magnitude = std::abs(motion);
        const int side = motion < 0.0 ? -1 : 1;
        // Hysteresis prevents a noisy threshold from retriggering the same
        // contact. Sub-sample contact integration keeps the impulse rate-safe.
        if (magnitude < 0.7 * mCollisionGap) mCollisionSide = 0;
        if (mCollisionEnvelope > 1.0e-8 && magnitude > mCollisionGap && side != mCollisionSide
            && mCollisionSample >= mCollisionSamplesTotal) {
            const double approach = std::max(0.0, magnitude - std::abs(mCollisionPrevious)) * mode0.invSin;
            // Repeated collisions can coincide with a mounting resonance.
            // Reserve only the amplitude headroom below a fraction of the
            // current bar ring, so coherent impacts cannot swamp the note.
            const double ringBound = std::sqrt(2.0 * (ModeEnergy(mCollisionModes[0]) + ModeEnergy(mCollisionModes[1])));
            const double barBound = std::sqrt(std::max(0.0, ModeEnergy(mode0))) * mCollisionAmplitudeRatio;
            const double available = std::max(0.0, barBound - ringBound) / 1.45;
            mCollisionImpulse = side * std::min(available,
                std::min(approach, mCollisionImpulseLimit) * mCollisionEnvelope * 4.0);
            mCollisionSample = 0;
            mCollisionSide = side;
        }
        if (mCollisionSample < mCollisionSamplesTotal) {
            const double t = mCollisionSample++ * mCollisionPulseStep;
            collisionDrive = mCollisionImpulse * (ContactCumulative(t + mCollisionPulseStep, 0.0) - ContactCumulative(t, 0.0));
        }
        mCollisionPrevious = motion;
        mCollisionEnvelope *= mCollisionDecay * handPole;
        if (mCollisionEnvelope < 1.0e-8) mCollisionEnvelope = 0.0;
    }
    double collisionSound = 0.0;
    if (mCollisionActive) for (auto& mode : mCollisionModes) {
        const double y = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * collisionDrive;
        mode.y2 = mode.y1 * handPole;
        mode.y1 = y * handPole;
        collisionSound += y;
    }
    barCloseSum += brushSound;
    barFarSum += brushSound;
    barMechanicalSum += brushSound * 0.5;
    mountingStrain += brushSound * mBrushPickupWeight;
    // Keep the acoustic frame's input ahead of the pickup electronics. Changing
    // pickup mounting/loading must not change acoustic body resonance.
    mMechanicalSample = static_cast<float>(barMechanicalSum * 0.90 + collisionSound);

    // 8. Air microphones and an adhesive/bridge mounted contact transducer.
    if (mResReadoutGain != mResCoupling) {
        mResReadoutGain += mResReadoutStep * (mResCoupling - mResReadoutGain);
        if (std::abs(mResReadoutGain - mResCoupling) < 1.0e-12) mResReadoutGain = mResCoupling;
    }
    double close = (barCloseSum * 0.90) + (yRes * 0.25 * mResReadoutGain) + (buzzComponent * 0.35)
                 + collisionSound * 0.65 + (clackSound * 0.50);
    double far = (yRes * 0.50 * mResReadoutGain) + (barFarSum * 0.55) + (buzzComponent * 0.50)
               + collisionSound * 0.22 + (clackSound * 0.20);
    double piezo = mPickup.Process(mountingStrain + collisionSound * 1.6,
        (excitation + brushForce) * mPickupContactGain,
        (mode0.y1 + mode0.o1) * 0.35 + brushSound * 0.2 + collisionSound);

    // 9. Fade-out (steal, polyphony reduction, oversampling change)
    if (mFading) {
        close *= mFadeGain;
        far *= mFadeGain;
        piezo *= mFadeGain;
        mMechanicalSample *= mFadeGain;
        mFadeGain -= mFadeStep;
        if (mFadeGain <= 0.0f) {
            outClose = static_cast<float>(close);
            outFar = static_cast<float>(far);
            outPiezo = static_cast<float>(piezo);
            FastKill();
            return true;
        }
    }

    outClose = static_cast<float>(close);
    outFar = static_cast<float>(far);
    outPiezo = static_cast<float>(piezo);
    return true;
}

void MarembaVoice::UpdateEnvelope() {
    if (!mActive) return;

    double energy = 0.0;
    for (int m = 0; m < mNumModes; ++m) {
        energy += FlushedModeEnergy(mModes[m]);
    }
    if (mHasTwin) energy += FlushedModeEnergy(mMode0b);
    energy += FlushedModeEnergy(mTube);
    for (auto& mode : mUpperCavity) energy += FlushedModeEnergy(mode);
    double brushEnergy = 0.0, collisionEnergy = 0.0;
    for (auto& mode : mBrushModes) brushEnergy += FlushedModeEnergy(mode);
    for (auto& mode : mCollisionModes) collisionEnergy += FlushedModeEnergy(mode);
    if (mArtifactSample >= mArtifactEnd && brushEnergy < kFlushEnergy) mBrushActive = false;
    if (mCollisionEnvelope == 0.0 && mCollisionSample >= mCollisionSamplesTotal && collisionEnergy < kFlushEnergy) mCollisionActive = false;
    energy += brushEnergy + collisionEnergy;
    mPickup.FlushTiny();
    energy += mPickup.StateEnergy();
    mMalletContact.FlushTiny();
    energy += mMalletContact.Energy();
    const double membraneEnergy = MembraneEnergy();
    if (membraneEnergy < kFlushEnergy)
        mMirlitonDisplacement = mMirlitonVelocity = 0.0;
    energy += membraneEnergy;

    if (!std::isfinite(energy) || energy > kMaxVoiceEnergy
        || !std::isfinite(mClackBpY1) || !std::isfinite(mMirlitonDisplacement)
        || !std::isfinite(mMirlitonVelocity) || !mMalletContact.IsFinite()) {
        FastKill();
        return;
    }
    mEnergy = energy;

    // A membrane state keeps the voice alive while the membrane buzzes, or while
    // a left-over buzz releases out after the membrane went away (both reach 0).
    const bool buzzing = mMirlitonDisplacement != 0.0 || mMirlitonVelocity != 0.0;
    const bool exciting = IsStriking();
    if (exciting || mClackActive || buzzing) return;

    // -120 dB and nothing left to excite the bar: free the voice (state flushed)
    if (energy < kSilenceEnergy) {
        FastKill();
    }
}

} // namespace maremba
