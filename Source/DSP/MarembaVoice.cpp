#include "MarembaVoice.h"
#include "DspMath.h"
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
// Mirliton fallback for models without a native membrane: the knob only starts
// to buzz above this value, so patches voiced with buzz <= 0.2 keep their sound.
constexpr float kFallbackBuzzOnset = 0.20f;
constexpr double kFallbackMirlitonThreshold = 0.050;
constexpr double kFallbackMirlitonDrive = 6.0;

constexpr double kPiezoWeight[kNumPitchedModes] = { 0.5, 1.0, 1.2, 1.2, 1.2, 1.2, 1.5, 1.5 };

// Shock spike: 1.0, 0.5, 0.2 x (shock * peak force) over the first three
// reference-rate samples (34 us). Integrated over each internal sample so the
// total impulse is identical at every rate.
inline double ShockCumulative(double t) {
    if (t <= 0.0) return 0.0;
    return std::min(t, 1.0)
         + 0.5 * std::clamp(t - 1.0, 0.0, 1.0)
         + 0.2 * std::clamp(t - 2.0, 0.0, 1.0);
}
constexpr double kShockDurationRef = 3.0;

// Mallet rebound undershoot after contact: 1.0, 0.342, 0.088 per reference sample
// (exp(-2t/3) * (1 - t/3) sampled at t = 0, 1, 2), integrated like the shock.
constexpr double kRebound1 = 0.34227808;
constexpr double kRebound2 = 0.08786580;
inline double ReboundCumulative(double t) {
    if (t <= 0.0) return 0.0;
    return std::min(t, 1.0)
         + kRebound1 * std::clamp(t - 1.0, 0.0, 1.0)
         + kRebound2 * std::clamp(t - 2.0, 0.0, 1.0);
}
constexpr double kReboundDurationRef = 3.0;

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
    mReboundSamplesTotal = 0;
    mReboundSample = 0;
    mRestrikeSamplesLeft = 0;
    mFadeGain = 1.0f;
    mFadeStep = 0.0f;
    mFrictionFilterState = 0.0;
    mCurrentGlideCents = 0.0;
    mMirlitonFilterState = 0.0;
    mRattleLevel = 0.0;
    mClackActive = false;
    mClackBpY1 = mClackBpY2 = 0.0;
    mClackEnvelope = 0.0;

    for (int i = 0; i < kNumPitchedModes; ++i) {
        mModes[i] = ModeState{};
    }
    mNumModes = 0;
    mMode0b = ModeState{};
    mHasTwin = false;
    mTube = ModeState{};
}

void MarembaVoice::SetModeCoefficients(ModeState& mode, double omega) const {
    const double w = std::clamp(omega, 1.0e-9, kMaxOmega);
    const double sinW = std::sin(w);
    mode.c = 2.0 * mode.r * std::cos(w);
    mode.s = mode.r * mode.r;
    mode.invSin2 = 1.0 / (sinW * sinW);
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
    return clean(mMode0b) && clean(mTube);
}

double MarembaVoice::ComputeEnergy() const {
    if (!mActive) return 0.0;
    double energy = 0.0;
    for (int m = 0; m < mNumModes; ++m) {
        energy += ModeEnergy(mModes[m]);
    }
    if (mHasTwin) energy += ModeEnergy(mMode0b);
    energy += ModeEnergy(mTube);
    return energy;
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
        SetModeCoefficients(mModes[m], mModes[m].omega * mPitchRatio);
    }
    if (mHasTwin) SetModeCoefficients(mMode0b, mMode0b.omega * mPitchRatio);
    SetModeCoefficients(mTube, mTube.omega * mPitchRatio);
}

void MarembaVoice::Trigger(int noteNumber, float velocity, EMarimbaModel model,
                           float malletHardness, float strikePosition,
                           float resonatorTuneCents, float resonatorCoupling,
                           float decayMultiplier, float buzzAmount, float artifactsAmount,
                           float pitchGlideAmount,
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
    mVelocity = std::clamp(velocity, 0.01f, 1.0f);
    mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
    mRefStep = kReferenceRate / mSampleRate;
    mNoiseScale = std::sqrt(mSampleRate / kReferenceRate);
    mFrictionFilterState = 0.0;
    mTriggerGlobalCents = globalCents;
    mPitchRatio = 1.0;

    const double fs = mSampleRate;
    ModelParams params = GetModelParams(model);
    StrikeArtifacts art = GenerateStrikeArtifacts(prng, mVelocity, malletHardness, artifactsAmount);
    static constexpr double kPhysicalImpulseScale = 0.018;

    mHandDampRate = params.handDampRate;
    mHandDampPole = std::exp(-mHandDampRate / fs);

    // Fundamental Frequency with global tuning, per-key drift detuning, and micro-jitter
    const double totalPitchCents = static_cast<double>(noteNumber - 69) * 100.0
                                 + globalCents + keyOffsetCents + art.pitchJitterCents;
    const double f0 = 440.0 * std::exp2(totalPitchCents / 1200.0);

    // Dynamic tension-modulation pitch glide: the bar starts sharp and settles
    // to concert pitch (~8 ms time constant). About 30 cents at full knob,
    // full velocity and tensionModFactor 0.05 (Rosewood).
    const double velSq = static_cast<double>(mVelocity) * mVelocity;
    mCurrentGlideCents = 30.0 * std::clamp(pitchGlideAmount, 0.0f, 1.0f) * velSq * (params.tensionModFactor / 0.05);
    mGlideDecay = std::exp(-120.0 / fs);

    // Register-dependent decay time: lower register sustains longer, treble is shorter
    const double noteFactor = std::clamp(130.81 / f0, 0.25, 3.5); // C3 as reference
    const double registerDecay = params.baseDecaySec * std::pow(noteFactor, static_cast<double>(params.trebleDampFactor));
    const double t60_f0 = std::min(registerDecay * std::clamp(decayMultiplier, 0.10f, 8.0f), kMaxT60Seconds);

    // Strike position modulation
    float effectivePos = std::clamp(strikePosition + art.strikePositionOffset, 0.05f, 0.95f);
    float distFromCenter = std::abs(effectivePos - 0.5f) * 2.0f; // 0.0 at center, 1.0 at edge/node

    // Mallet / Thumb hardness & firmness modulation
    float effectiveHardness = std::clamp(malletHardness + art.malletHardnessJitter, 0.05f, 0.95f);

    // ────────────────────────────────────────────────────────
    // Striker Physics: Marimba Mallets vs Kalimba Thumbs
    // ────────────────────────────────────────────────────────
    float modeWeightMult[kNumPitchedModes];
    for (int m = 0; m < kNumPitchedModes; ++m) modeWeightMult[m] = 1.0f;
    float contactTimeSec = 0.0005f;
    float frictionMult = 1.0f;
    int mt = std::clamp(malletType, 0, 3);

    if (model != EMarimbaModel::KalimbaArtisan) {
        // Marimba Mallets: Soft Yarn, Medium Cord, Hard Rubber, Wood Baton
        // Contact time follows Hertzian law: t_contact ∝ v^(-0.28) for felt/rubber
        switch (mt) {
            case 0: // Soft Wool Yarn
                contactTimeSec = (0.0016f + (1.0f - effectiveHardness) * 0.0008f) * std::pow(mVelocity, -0.28f);
                mMalletPulseExponent = 2.2f;     // Rise exponent (Hertzian)
                mMalletPulseExponentFall = 2.8f;  // Slower fall (felt compression)
                mMalletShockImpulse = 0.05f;
                mMalletReboundGain = 0.008f;      // Tiny soft bounce
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
                mMalletPulseExponent = 1.4f;
                mMalletPulseExponentFall = 1.7f;
                mMalletShockImpulse = 0.50f;
                mMalletReboundGain = 0.015f;
                frictionMult = 0.50f;
                // Balanced: all modes at 1.0
                break;
            case 2: // Hard Rubber
                contactTimeSec = (0.00030f + (1.0f - effectiveHardness) * 0.00018f) * std::pow(mVelocity, -0.30f);
                mMalletPulseExponent = 1.2f;
                mMalletPulseExponentFall = 1.4f;
                mMalletShockImpulse = 1.10f;
                mMalletReboundGain = 0.025f;
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
                mMalletPulseExponent = 1.05f;
                mMalletPulseExponentFall = 1.15f;
                mMalletShockImpulse = 1.80f;
                mMalletReboundGain = 0.04f;      // Wood bounces noticeably
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
                mMalletPulseExponent = 1.8f;
                mMalletPulseExponentFall = 2.2f;
                mMalletShockImpulse = 0.15f;
                mMalletReboundGain = 0.005f;
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
                mMalletPulseExponent = 1.3f;
                mMalletPulseExponentFall = 1.6f;
                mMalletShockImpulse = 0.50f;
                mMalletReboundGain = 0.012f;
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
                mMalletPulseExponent = 1.15f;
                mMalletPulseExponentFall = 1.3f;
                mMalletShockImpulse = 1.30f;
                mMalletReboundGain = 0.03f;
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
                mMalletPulseExponent = 1.05f;
                mMalletPulseExponentFall = 1.15f;
                mMalletShockImpulse = 1.90f;
                mMalletReboundGain = 0.05f;
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

    // Organic Strike Stochastic Micro-Jitter (0 to 100%)
    if (strikeJitter > 0.001f) {
        float normJitter = std::clamp(strikeJitter, 0.0f, 100.0f) * 0.01f;

        // 1. Mallet / Tine Contact Time noise: +/- 4% max at 100%
        float contactJitter = std::clamp(prng.NextGaussian() * 0.04f * normJitter, -0.10f, 0.10f);
        contactTimeSec *= (1.0f + contactJitter);

        // 2. Mallet Force Pulse Exponent noise: +/- 5% max at 100%
        float pulseJitter = std::clamp(prng.NextGaussian() * 0.05f * normJitter, -0.12f, 0.12f);
        mMalletPulseExponent = std::clamp(mMalletPulseExponent * (1.0f + pulseJitter), 0.8f, 3.0f);
        mMalletPulseExponentFall = std::clamp(mMalletPulseExponentFall * (1.0f + pulseJitter * 0.8f), 0.8f, 3.5f);

        // 3. Initial Impact Shock Impulse noise: +/- 6% max at 100%
        float shockJitter = std::clamp(prng.NextGaussian() * 0.06f * normJitter, -0.15f, 0.15f);
        mMalletShockImpulse = std::clamp(mMalletShockImpulse * (1.0f + shockJitter), 0.0f, 3.0f);

        // 4. Modal Overtone Excitation Weights: independent +/- 3.5% per mode at 100%
        for (int m = 0; m < kNumPitchedModes; ++m) {
            float modeJitter = std::clamp(prng.NextGaussian() * 0.035f * normJitter, -0.10f, 0.10f);
            modeWeightMult[m] = std::clamp(modeWeightMult[m] * (1.0f + modeJitter), 0.01f, 4.0f);
        }
    }

    // Viscoelastic contact damping of the upper modes: the per-sample factor
    // voiced at the reference rate, converted to a per-second constant.
    mContactDampPole = PoleAtRate(1.0 - params.contactDampingFactor * 0.003, fs);

    // ────────────────────────────────────────────────────────
    // Resonator Tube / Gourd Setup (needed first: its coupling damps mode 0)
    // ────────────────────────────────────────────────────────
    double fRes = f0 * std::exp2(resonatorTuneCents / 1200.0);
    fRes = std::min(fRes, fs * 0.45);
    const double decayScale = std::clamp(decayMultiplier, 0.10f, 4.0f);
    const double qRes = params.resonatorQ * (0.8 + 0.4 * resonatorCoupling) * std::sqrt(decayScale);
    mResCoupling = params.couplingStrength * std::clamp(resonatorCoupling, 0.05f, 1.0f);
    // Radiation damping applied to the fundamental bar mode (and its twin)
    const double radDamping = 1.0 + 0.65 * mResCoupling;
    const double tau_f0 = t60_f0 / 6.907755;
    const double r_0 = std::exp(-radDamping / (tau_f0 * fs));

    // Restrike: the previous ring keeps sounding in the residual states and is
    // damped gradually over the new contact (no instant cut); the new stroke
    // excites the main states from zero.
    const int contactSamples = static_cast<int>(std::ceil(std::max(3.0, contactTimeSec * fs)));

    // ────────────────────────────────────────────────────────
    // Configure 8 Pitched Bar Modes with physics-based damping
    // ────────────────────────────────────────────────────────
    // Register-dependent overtone morphing:
    // Undercut arch in bass/mid (4:10 tuning) -> Euler-Bernoulli beam in treble (2.76:5.40:8.93).
    // Treble bars (> C5, MIDI 72) are small un-arched rectangular blocks.
    float trebleMorph = 0.0f;
    if (model != EMarimbaModel::KalimbaArtisan) {
        trebleMorph = std::clamp((static_cast<float>(noteNumber) - 60.0f) / 24.0f, 0.0f, 1.0f);
    }

    const int previousModes = isRestrike ? mNumModes : 0;
    mNumModes = kNumPitchedModes;
    for (int m = 0; m < kNumPitchedModes; ++m) {
        float ratio = params.overtoneRatio[m];
        if (trebleMorph > 0.0f) {
            float eulerRatio = ratio;
            if (m == 1) eulerRatio = 2.756f;
            else if (m == 2) eulerRatio = 5.404f;
            else if (m == 3) eulerRatio = 8.932f;
            else if (m == 4) eulerRatio = 13.35f;
            ratio = (1.0f - trebleMorph) * ratio + trebleMorph * eulerRatio;
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
        mode.r = (m == 0) ? r_0 : std::exp(-1.0 / (tau_m * fs));
        SetModeCoefficients(mode, omega_m);

        // Strike position modulation — physically motivated nodal weighting
        float posWeight = 1.0f;
        if (m == 0) {
            posWeight = 1.0f - 0.50f * distFromCenter * distFromCenter;
        } else if (m == 1) {
            posWeight = 0.85f - 0.20f * distFromCenter;
        } else if (m < 4) {
            posWeight = 0.70f + 0.55f * distFromCenter;
        } else {
            // Upper modes increasingly excited off-center
            posWeight = 0.55f + 0.80f * distFromCenter;
        }

        // Mallet hardness scaling: hard mallets excite more high overtones
        float overtoneScaling = (m == 0) ? 1.0f : (0.40f + 0.60f * effectiveHardness);

        // Velocity-to-spectrum coupling: pianissimo → pure fundamental, forte → rich overtones
        float velModeBoost = (m == 0) ? 1.0f : std::pow(mVelocity, 0.3f + 0.12f * static_cast<float>(m));

        const double weight = params.modeWeights[m] * posWeight * overtoneScaling * modeWeightMult[m] * velModeBoost;

        // b0 = weight * sin(w) makes a unit impulse ring at amplitude 'weight' at any rate
        mode.b0 = weight * std::sin(omega_m) * kPhysicalImpulseScale;
    }
    for (int m = mNumModes; m < kNumPitchedModes; ++m) {
        mModes[m] = ModeState{};
    }

    // ────────────────────────────────────────────────────────
    // Anisotropic Twin Mode 0b (wood grain beating)
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
        mMode0b.r = r_0; // same damping as mode 0
        SetModeCoefficients(mMode0b, omega_0b);
        // Twin mode provides subtle natural wood grain beating (15% amplitude)
        mMode0b.b0 = 0.15 * mModes[0].b0 / std::sin(mModes[0].omega) * std::sin(omega_0b);
    } else {
        // No anisotropic split (Kalimba steel tines or high treble bars)
        mMode0b = ModeState{};
    }

    // ────────────────────────────────────────────────────────
    // Mallet / Thumb Impact Non-linear Contact Time (Hertzian)
    // ────────────────────────────────────────────────────────
    // The force is integrated (force x dt in reference samples), so the struck
    // level does not depend on the internal rate.
    const double contactSamplesExact = std::max(3.0, contactTimeSec * fs);
    mContactSamplesTotal = contactSamples;
    mContactSample = 0;
    mContactOmega = kPiD / contactSamplesExact;

    float refContact = (model == EMarimbaModel::KalimbaArtisan) ? 0.00045f : 0.00065f;
    float forceDurationComp = refContact / contactTimeSec;
    const double peakForce = std::pow(mVelocity, 1.25f) * (0.9f + 0.8f * effectiveHardness) * forceDurationComp;
    mContactForceStep = peakForce * mRefStep;

    mShockGain = mMalletShockImpulse * peakForce;
    mShockSamplesTotal = static_cast<int>(std::ceil(kShockDurationRef / mRefStep));
    mShockSample = 0;

    mReboundImpulse = (mMalletReboundGain > 0.001f) ? mMalletReboundGain * peakForce : 0.0;
    mReboundSamplesTotal = (mReboundImpulse > 0.0) ? static_cast<int>(std::ceil(kReboundDurationRef / mRefStep)) : 0;
    mReboundSample = 0;

    // Yarn friction: white force noise (density rate-normalised) through a ~720 Hz low-pass
    mFrictionGain = art.frictionNoiseBurst * (1.0f - effectiveHardness * 0.6f) * frictionMult;
    mFrictionAlpha = SmootherAtRate(0.05, fs);
    mFrictionNoiseScale = std::sqrt(mRefStep);

    // Restrike: damp the previous ring to 25% over the contact time
    if (isRestrike) {
        mRestrikeSamplesLeft = contactSamples;
        mRestrikePole = std::pow(0.25, 1.0 / static_cast<double>(contactSamples));
    } else {
        mRestrikeSamplesLeft = 0;
        mRestrikePole = 1.0;
    }

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
        double clackIntensity = mMalletShockImpulse * effectiveHardness * std::pow(mVelocity, 0.8f);
        mClackEnvelope = clackIntensity * 0.05 * (1.0 + params.woodInternalFriction * 100.0);
        mClackActive = mClackEnvelope > 0.0;
    }

    // Tube resonator coefficients (states follow the bar; damped gradually on restrike)
    {
        const double wRes = kTwoPiD * fRes / fs;
        mTube.omega = wRes;
        mTube.r = std::exp(-kPiD * fRes / (qRes * fs));
        SetModeCoefficients(mTube, wRes);
        mTube.b0 = 1.0 - mTube.s;
        if (!isRestrike) {
            mTube.y1 = mTube.y2 = 0.0;
        }
        mTube.o1 = mTube.o2 = 0.0;
    }

    // Mirliton Buzz Setup: native membrane (Balafon) or a fallback membrane on
    // the other models that only starts above kFallbackBuzzOnset.
    if (params.hasMirliton) {
        mHasMirliton = true;
        mMirlitonThreshold = params.mirlitonThreshold * std::max(0.1f, 1.3f - 0.9f * buzzAmount);
        mMirlitonDrive = params.mirlitonDrive * (0.3f + 1.2f * buzzAmount);
    } else if (buzzAmount > kFallbackBuzzOnset) {
        const double amount = (buzzAmount - kFallbackBuzzOnset) / (1.0 - kFallbackBuzzOnset);
        mHasMirliton = true;
        mMirlitonThreshold = kFallbackMirlitonThreshold * (1.3 - 0.9 * amount);
        mMirlitonDrive = kFallbackMirlitonDrive * amount; // 0 at the onset: no jump when the knob crosses it
    } else {
        mHasMirliton = false;
    }
    mMirlitonAttack = SmootherAtRate(0.35, fs);
    mMirlitonRelease = PoleAtRate(0.92, fs);
    if (!isRestrike) mMirlitonFilterState = 0.0;

    // Cord rattle
    mRattleLevel = art.mechanicalRattle;
    mRattleDecay = std::exp(-80.0 / fs);
}

bool MarembaVoice::ProcessSample(float& outClose, float& outFar, float& outPiezo) {
    if (!mActive) {
        outClose = 0.0f;
        outFar = 0.0f;
        outPiezo = 0.0f;
        return false;
    }

    // 1. Mallet Contact Excitation with asymmetric Hertzian pulse + shock spike + rebound
    double excitation = 0.0;
    const bool inContact = (mContactSample < mContactSamplesTotal);

    if (inContact) {
        const double phase = mContactOmega * static_cast<double>(mContactSample);
        const double pulse = std::max(0.0, std::sin(phase));

        // Asymmetric Hertzian contact: faster rise, slower fall
        const float shapedPulse = std::pow(static_cast<float>(pulse),
                                           phase < 0.5 * kPiD ? mMalletPulseExponent : mMalletPulseExponentFall);
        excitation = shapedPulse * mContactForceStep;

        if (mFrictionGain > 0.0001) {
            double noise = mLocalPrng.NextBipolar() * mFrictionNoiseScale;
            mFrictionFilterState += mFrictionAlpha * (noise - mFrictionFilterState);
            excitation += mFrictionFilterState * mFrictionGain * pulse;
        }

        mContactSample++;
    } else if (mReboundSample < mReboundSamplesTotal) {
        // Mallet rebound: tiny negative undershoot right after contact ends
        const double t0 = static_cast<double>(mReboundSample) * mRefStep;
        excitation = -mReboundImpulse * (ReboundCumulative(t0 + mRefStep) - ReboundCumulative(t0));
        mReboundSample++;
    }

    // Initial contact shock impulse spike (first 34 us)
    if (mShockSample < mShockSamplesTotal) {
        const double t0 = static_cast<double>(mShockSample) * mRefStep;
        excitation += mShockGain * (ShockCumulative(t0 + mRefStep) - ShockCumulative(t0));
        mShockSample++;
    }

    // 2. Dynamic Tension Modulation Pitch Glide on Mode 0
    if (mCurrentGlideCents > 0.02) {
        if (mNumModes > 0) {
            const double w = std::min(mModes[0].omega * mPitchRatio * std::exp2(mCurrentGlideCents / 1200.0), kMaxOmega);
            mModes[0].c = 2.0 * mModes[0].r * std::cos(w);
        }
        mCurrentGlideCents *= mGlideDecay;
    } else if (mCurrentGlideCents > 0.0) {
        if (mNumModes > 0) SetModeCoefficients(mModes[0], mModes[0].omega * mPitchRatio);
        mCurrentGlideCents = 0.0;
    }

    // 3. Bar Modal Resonator Updates (8 pitched modes + twin mode 0b)
    double barAcousticSum = 0.0;
    double barMechanicalSum = 0.0;

    const bool restriking = mRestrikeSamplesLeft > 0;
    const bool scaled = inContact || restriking || mDamping;
    const double handPole = mDamping ? mHandDampPole : 1.0;

    if (!scaled) {
        for (int m = 0; m < mNumModes; ++m) {
            ModeState& mode = mModes[m];
            const double ym = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * excitation;
            mode.y2 = mode.y1;
            mode.y1 = ym;
            barAcousticSum += ym;
            barMechanicalSum += ym * kPiezoWeight[m];
        }
        if (mHasTwin) {
            const double y0b = mMode0b.c * mMode0b.y1 - mMode0b.s * mMode0b.y2 + mMode0b.b0 * excitation;
            mMode0b.y2 = mMode0b.y1;
            mMode0b.y1 = y0b;
            barAcousticSum += y0b;
            barMechanicalSum += y0b * 0.6;
        }
    } else {
        // Damping here always scales BOTH states (a smaller pole radius), which
        // removes energy; scaling only y[n] would inject energy into low modes.
        const double contactPole = inContact ? mContactDampPole : 1.0;
        auto step = [&](ModeState& mode, double pole) -> double {
            const double ym = mode.c * mode.y1 - mode.s * mode.y2 + mode.b0 * excitation;
            mode.y2 = mode.y1;
            mode.y1 = ym;
            double out = ym;
            if (restriking) {
                const double yo = mode.c * mode.o1 - mode.s * mode.o2;
                mode.o2 = mode.o1;
                mode.o1 = yo;
                out += yo;
                mode.o1 *= mRestrikePole;
                mode.o2 *= mRestrikePole;
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
            const double out = step(mModes[m], (m >= 2) ? handPole * contactPole : handPole);
            barAcousticSum += out;
            barMechanicalSum += out * kPiezoWeight[m];
        }
        if (mHasTwin) {
            const double out = step(mMode0b, handPole);
            barAcousticSum += out;
            barMechanicalSum += out * 0.6;
        }
        if (restriking && --mRestrikeSamplesLeft == 0) {
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

    // 5. Resonator Tube / Gourd Acoustic Coupling (driven by the velocity of mode 0)
    const ModeState& mode0 = mModes[0];
    const double barVel0 = (mode0.y1 + mode0.o1) - (mode0.y2 + mode0.o2);
    const double driveRes = barVel0 * (0.35 + 0.65 * mResCoupling);

    const double yRes = mTube.c * mTube.y1 - mTube.s * mTube.y2 + mTube.b0 * driveRes;
    mTube.y2 = mTube.y1;
    mTube.y1 = yRes;
    if (scaled) {
        const double tubePole = handPole * (restriking ? mRestrikePole : 1.0);
        mTube.y1 *= tubePole;
        mTube.y2 *= tubePole;
    }

    // 6. Mirliton Buzz Membrane Simulation
    double buzzComponent = 0.0;
    if (mHasMirliton) {
        const double absRes = std::abs(yRes);
        if (absRes > mMirlitonThreshold) {
            const double excess = absRes - mMirlitonThreshold;
            const double rawBuzz = (yRes > 0.0 ? 1.0 : -1.0) * std::tanh(mMirlitonDrive * excess * excess);
            mMirlitonFilterState += mMirlitonAttack * (rawBuzz - mMirlitonFilterState);
        } else {
            mMirlitonFilterState *= mMirlitonRelease;
            if (std::abs(mMirlitonFilterState) < 1.0e-12) mMirlitonFilterState = 0.0;
        }
        buzzComponent = mMirlitonFilterState * 0.75;
    }

    // 7. Cord / Mechanical Rattle
    double rattleSound = 0.0;
    if (mRattleLevel > 0.0) {
        rattleSound = mLocalPrng.NextBipolar() * mNoiseScale * mRattleLevel;
        mRattleLevel *= mRattleDecay;
        if (mRattleLevel < 1.0e-7) mRattleLevel = 0.0;
    }

    // 8. Synthesize Microphone Outputs (with clack noise burst in close and piezo)
    double close = (barAcousticSum * 0.90) + (yRes * 0.25 * mResCoupling) + (buzzComponent * 0.35)
                 + rattleSound + (clackSound * 0.50);
    double far = (yRes * 0.50 * mResCoupling) + (barAcousticSum * 0.55) + (buzzComponent * 0.50)
               + (clackSound * 0.20); // Clack is less prominent in far mic
    double piezo = (barMechanicalSum * 0.90) + (rattleSound * 1.0) + (clackSound * 0.60);

    // 9. Fade-out (steal, polyphony reduction, oversampling change)
    if (mFading) {
        close *= mFadeGain;
        far *= mFadeGain;
        piezo *= mFadeGain;
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

    if (!std::isfinite(energy) || !std::isfinite(mClackBpY1) || !std::isfinite(mMirlitonFilterState)) {
        FastKill();
        return;
    }
    mEnergy = energy;

    const bool exciting = IsStriking() || mReboundSample < mReboundSamplesTotal;
    if (exciting || mClackActive || mRattleLevel > 0.0 || mMirlitonFilterState != 0.0) return;

    // -120 dB and nothing left to excite the bar: free the voice (state flushed)
    if (energy < kSilenceEnergy) {
        FastKill();
    }
}

} // namespace maremba
