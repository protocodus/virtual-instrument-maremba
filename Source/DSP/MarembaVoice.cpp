#include "MarembaVoice.h"
#include <algorithm>
#include <cmath>

namespace maremba {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;

MarembaVoice::MarembaVoice()
    : mLocalPrng(0x56789ABC)
{
    FastKill();
}

void MarembaVoice::FastKill() {
    mActive = false;
    mReleased = false;
    mSustained = false;
    mDamping = false;
    mNoteNumber = -1;
    mVelocity = 0.0f;
    mAgeSamples = 0.0f;
    mEnvelopeEstimate = 0.0f;
    mMalletContactSamplesTotal = 0;
    mMalletContactSampleCurrent = 0;
    mMalletPulseExponent = 1.4f;
    mMalletPulseExponentFall = 1.6f;
    mMalletShockImpulse = 0.6f;
    mMalletReboundGain = 0.0f;
    mContactDampFactor = 0.0f;
    mFrictionFilterState = 0.0f;
    mCurrentGlideCents = 0.0f;
    mResY1 = mResY2 = 0.0f;
    mMirlitonFilterState = 0.0f;
    mRattleLevel = 0.0f;
    mClackBpY1 = mClackBpY2 = 0.0f;
    mClackEnvelope = 0.0f;

    for (int i = 0; i < kNumPitchedModes; ++i) {
        mModes[i].y1 = 0.0f;
        mModes[i].y2 = 0.0f;
        mModes[i].c = 0.0f;
        mModes[i].s = 0.0f;
        mModes[i].b0 = 0.0f;
        mModes[i].weight = 1.0f;
        mModes[i].nominalOmega = 0.0f;
        mModes[i].r = 0.0f;
    }

    mMode0b.y1 = mMode0b.y2 = 0.0f;
    mMode0b.c = mMode0b.s = mMode0b.b0 = 0.0f;
    mMode0b.weight = 0.35f;
    mMode0b.nominalOmega = 0.0f;
    mMode0b.r = 0.0f;
}

void MarembaVoice::Release(bool sustainPedalDown) {
    mReleased = true;
    mSustained = sustainPedalDown;
    mDamping = false;
}

void MarembaVoice::ReleaseSustainPedal() {
    mSustained = false;
    mDamping = false;
}

void MarembaVoice::Trigger(int noteNumber, float velocity, EMarimbaModel model,
                           float malletHardness, float strikePosition,
                           float resonatorTuneCents, float resonatorCoupling,
                           float decayMultiplier, float buzzAmount, float artifactsAmount,
                           float pitchGlideAmount,
                           float sampleRate, FastPRNG& prng,
                           float pitchOffsetCents,
                           int malletType,
                           float strikeJitter)
{
    bool isRestrike = mActive && (mNoteNumber == noteNumber);
    mActive = true;
    mReleased = false;
    mSustained = false;
    mDamping = false;
    mNoteNumber = noteNumber;
    mVelocity = std::clamp(velocity, 0.01f, 1.0f);
    mSampleRate = sampleRate > 8000.0f ? sampleRate : 44100.0f;
    mAgeSamples = 0.0f;
    mEnvelopeEstimate = 1.0f;
    mDampRate = 1.0f;
    mFrictionFilterState = 0.0f;

    ModelParams params = GetModelParams(model);
    StrikeArtifacts art = GenerateStrikeArtifacts(prng, mVelocity, malletHardness, artifactsAmount);
    static constexpr float kPhysicalImpulseScale = 0.018f;

    // Compute hand-damp rate from model parameters
    // exp(-rate / sampleRate) gives per-sample multiplier
    mDampRate = std::exp(-params.handDampRate / mSampleRate);

    // Fundamental Frequency with master pitch, per-key drift detuning, and micro-jitter
    float totalPitchCents = static_cast<float>(noteNumber - 69) * 100.0f + pitchOffsetCents + art.pitchJitterCents;
    float f0 = 440.0f * std::pow(2.0f, totalPitchCents / 1200.0f);

    // Dynamic tension-modulation pitch glide: subtle acoustic deflection that settles in ~8 ms
    float velSq = mVelocity * mVelocity;
    mCurrentGlideCents = 18.0f * std::clamp(pitchGlideAmount, 0.0f, 1.0f) * params.tensionModFactor * velSq;
    mGlideDecay = std::exp(-120.0f / mSampleRate); // ~8 ms settling to pure concert pitch

    // Register-dependent decay time: lower register sustains longer, treble is shorter
    float noteFactor = std::clamp(130.81f / f0, 0.25f, 3.5f); // C3 as reference
    float registerDecay = params.baseDecaySec * std::pow(noteFactor, params.trebleDampFactor);
    float t60_f0 = registerDecay * std::clamp(decayMultiplier, 0.10f, 8.0f);

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

    // Store viscoelastic contact damping factor
    mContactDampFactor = params.contactDampingFactor;

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

    float restrikeDamp = 0.25f; // Mallet contact damping factor on restrike

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

        float fm = f0 * ratio;

        // Register-dependent overtone stretching: subtle (+/- 1.5% across range)
        // Higher register bars are shorter/stiffer, slightly stretching upper partials
        if (m >= 2) {
            float registerStretch = 1.0f + 0.0003f * static_cast<float>(m - 1)
                                  * std::clamp(static_cast<float>(noteNumber - 60) / 36.0f, -1.0f, 1.0f);
            fm *= registerStretch;
        }

        if (fm > mSampleRate * 0.48f) {
            fm = mSampleRate * 0.48f;
        }

        // Physics-based frequency-dependent damping
        // Higher register bars have greater air radiation and mounting cord losses
        float regDampMult = 1.0f + 1.2f * trebleMorph;
        float freqRatio = fm / f0;
        float dampingFactor = 1.0f + (params.alphaRadiation * regDampMult) * freqRatio * freqRatio
                                   + (params.alphaInternal * regDampMult) * freqRatio;
        float t60_m = t60_f0 / dampingFactor;

        float tau_m = t60_m / 6.907755f;
        float r_m = std::clamp(std::exp(-1.0f / (tau_m * mSampleRate)), 0.0f, 0.999995f);
        float omega_m = std::clamp(kTwoPi * fm / mSampleRate, 0.001f, kPi - 0.001f);

        if (isRestrike) {
            mModes[m].y1 *= restrikeDamp;
            mModes[m].y2 *= restrikeDamp;
        } else {
            mModes[m].y1 = 0.0f;
            mModes[m].y2 = 0.0f;
        }
        mModes[m].nominalOmega = omega_m;
        mModes[m].r = r_m;
        mModes[m].c = 2.0f * r_m * std::cos(omega_m);
        mModes[m].s = r_m * r_m;

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

        mModes[m].weight = params.modeWeights[m] * posWeight * overtoneScaling * modeWeightMult[m] * velModeBoost;

        // Physical impulse scaling: uniform base scale calibrated for pristine acoustic headroom
        mModes[m].b0 = mModes[m].weight * std::sin(omega_m) * kPhysicalImpulseScale;
    }

    // ────────────────────────────────────────────────────────
    // Anisotropic Twin Mode 0b (wood grain beating)
    // ────────────────────────────────────────────────────────
    // Fade split down to 0.0 in high register to prevent flutter/beating on octaves and high notes
    float effectiveSplitCents = params.anisotropicSplitCents * (1.0f - trebleMorph);
    if (effectiveSplitCents > 0.01f) {
        // Create a twin of mode 0 detuned by effectiveSplitCents
        float f0b = f0 * std::pow(2.0f, effectiveSplitCents / 1200.0f);
        if (f0b > mSampleRate * 0.48f) f0b = mSampleRate * 0.48f;

        // Same damping as mode 0
        float regDampMult = 1.0f + 1.2f * trebleMorph;
        float dampingFactor0 = 1.0f + (params.alphaRadiation * regDampMult) + (params.alphaInternal * regDampMult);
        float t60_0b = t60_f0 / dampingFactor0;
        float tau_0b = t60_0b / 6.907755f;
        float r_0b = std::clamp(std::exp(-1.0f / (tau_0b * mSampleRate)), 0.0f, 0.999995f);
        float omega_0b = std::clamp(kTwoPi * f0b / mSampleRate, 0.001f, kPi - 0.001f);

        if (isRestrike) {
            mMode0b.y1 *= restrikeDamp;
            mMode0b.y2 *= restrikeDamp;
        } else {
            mMode0b.y1 = 0.0f;
            mMode0b.y2 = 0.0f;
        }
        mMode0b.nominalOmega = omega_0b;
        mMode0b.r = r_0b;
        mMode0b.c = 2.0f * r_0b * std::cos(omega_0b);
        mMode0b.s = r_0b * r_0b;
        // Twin mode provides subtle natural wood grain beating (15% amplitude)
        mMode0b.weight = 0.15f * mModes[0].weight;
        mMode0b.b0 = mMode0b.weight * std::sin(omega_0b) * kPhysicalImpulseScale;
    } else {
        // No anisotropic split (Kalimba steel tines or high treble bars)
        mMode0b.y1 = 0.0f;
        mMode0b.y2 = 0.0f;
        mMode0b.nominalOmega = 0.0f;
        mMode0b.r = 0.0f;
        mMode0b.c = 0.0f;
        mMode0b.s = 0.0f;
        mMode0b.weight = 0.0f;
        mMode0b.b0 = 0.0f;
    }

    // ────────────────────────────────────────────────────────
    // Mallet / Thumb Impact Non-linear Contact Time (Hertzian)
    // ────────────────────────────────────────────────────────
    mMalletContactSamplesTotal = std::max(3, static_cast<int>(contactTimeSec * mSampleRate));
    mMalletContactSampleCurrent = 0;
    mMalletContactOmega = kPi / static_cast<float>(mMalletContactSamplesTotal);

    float refContact = (model == EMarimbaModel::KalimbaArtisan) ? 0.00045f : 0.00065f;
    float forceDurationComp = refContact / contactTimeSec;
    mMalletPeakForce = std::pow(mVelocity, 1.25f) * (0.9f + 0.8f * effectiveHardness) * forceDurationComp;
    mMalletFrictionGain = art.frictionNoiseBurst * (1.0f - effectiveHardness * 0.6f) * frictionMult;

    // ────────────────────────────────────────────────────────
    // Strike Clack Noise Burst (replaces artificial tonal clack mode)
    // ────────────────────────────────────────────────────────
    {
        float clackF = std::clamp(params.clackFrequency + (f0 * 0.35f), 2500.0f, 8000.0f);
        if (clackF > mSampleRate * 0.45f) clackF = mSampleRate * 0.45f;
        // Bandpass Q derived from bandwidth: Q = f_center / bandwidth
        float clackBW = std::clamp(params.clackBandwidth, 800.0f, 5000.0f);
        float clackQ = clackF / clackBW;
        float rClack = std::clamp(std::exp(-kPi * clackF / (clackQ * mSampleRate)), 0.0f, 0.999f);
        float wClack = std::clamp(kTwoPi * clackF / mSampleRate, 0.001f, kPi - 0.001f);
        mClackBpC = 2.0f * rClack * std::cos(wClack);
        mClackBpS = rClack * rClack;
        mClackBpY1 = mClackBpY2 = 0.0f;

        // Clack decay: very short (2-5 ms)
        float clackDecayMs = 2.0f + (1.0f - effectiveHardness) * 3.0f;
        mClackDecayRate = std::exp(-1000.0f / (clackDecayMs * mSampleRate));

        // Clack gain: scales with hardness, velocity, and wood friction
        float clackIntensity = mMalletShockImpulse * effectiveHardness * std::pow(mVelocity, 0.8f);
        mClackGain = clackIntensity * 0.05f * (1.0f + params.woodInternalFriction * 100.0f);
        mClackEnvelope = mClackGain;
    }

    // ────────────────────────────────────────────────────────
    // Resonator Tube / Gourd Setup
    // ────────────────────────────────────────────────────────
    float fRes = f0 * std::pow(2.0f, resonatorTuneCents / 1200.0f);
    if (fRes > mSampleRate * 0.45f) {
        fRes = mSampleRate * 0.45f;
    }
    float decayScale = std::clamp(decayMultiplier, 0.10f, 4.0f);
    float qRes = params.resonatorQ * (0.8f + 0.4f * resonatorCoupling) * std::sqrt(decayScale);
    float rRes = std::clamp(std::exp(-kPi * fRes / (qRes * mSampleRate)), 0.0f, 0.999995f);
    float wRes = std::clamp(kTwoPi * fRes / mSampleRate, 0.001f, kPi - 0.001f);
    mResC = 2.0f * rRes * std::cos(wRes);
    mResS = rRes * rRes;
    mResCoupling = params.couplingStrength * std::clamp(resonatorCoupling, 0.05f, 1.0f);
    if (isRestrike) {
        mResY1 *= restrikeDamp;
        mResY2 *= restrikeDamp;
    } else {
        mResY1 = 0.0f;
        mResY2 = 0.0f;
    }

    // Radiation damping applied to fundamental bar mode
    float radDamping = 1.0f + 0.65f * mResCoupling;
    float tau_f0 = t60_f0 / 6.907755f;
    float r_0 = std::clamp(std::exp(-radDamping / (tau_f0 * mSampleRate)), 0.0f, 0.999995f);
    mModes[0].r = r_0;
    mModes[0].s = r_0 * r_0;
    mModes[0].c = 2.0f * r_0 * std::cos(mModes[0].nominalOmega);
    mModes[0].b0 = mModes[0].weight * std::sin(mModes[0].nominalOmega) * kPhysicalImpulseScale;

    // Mirliton Buzz Setup
    mHasMirliton = params.hasMirliton || (buzzAmount > 0.05f);
    mMirlitonThreshold = params.mirlitonThreshold * std::max(0.1f, 1.3f - 0.9f * buzzAmount);
    mMirlitonDrive = params.mirlitonDrive * (0.3f + 1.2f * buzzAmount);
    mMirlitonFilterState = 0.0f;

    // Cord rattle
    mRattleLevel = art.mechanicalRattle;
    mRattleDecay = std::exp(-80.0f / mSampleRate);
}

bool MarembaVoice::ProcessSample(float& outClose, float& outFar, float& outPiezo) {
    if (!mActive) {
        outClose = 0.0f;
        outFar = 0.0f;
        outPiezo = 0.0f;
        return false;
    }

    mAgeSamples += 1.0f;

    // 1. Mallet Contact Excitation with asymmetric Hertzian pulse + shock spike
    float excitation = 0.0f;
    bool inContact = (mMalletContactSampleCurrent < mMalletContactSamplesTotal);

    if (inContact) {
        float phase = mMalletContactOmega * static_cast<float>(mMalletContactSampleCurrent);
        float pulse = std::sin(phase);

        // Asymmetric Hertzian contact: faster rise, slower fall
        float halfPhase = kPi * 0.5f;
        float currentPhase = phase;
        float shapedPulse;
        if (currentPhase < halfPhase) {
            // Rising phase: use rise exponent
            shapedPulse = std::pow(pulse, mMalletPulseExponent);
        } else {
            // Falling phase: use fall exponent (slower decay = softer release)
            shapedPulse = std::pow(pulse, mMalletPulseExponentFall);
        }
        excitation = shapedPulse * mMalletPeakForce;

        // Initial contact shock impulse spike (first 3 samples)
        if (mMalletContactSampleCurrent == 0) {
            excitation += mMalletShockImpulse * mMalletPeakForce;
        } else if (mMalletContactSampleCurrent == 1) {
            excitation += (mMalletShockImpulse * 0.5f) * mMalletPeakForce;
        } else if (mMalletContactSampleCurrent == 2) {
            excitation += (mMalletShockImpulse * 0.2f) * mMalletPeakForce;
        }

        if (mMalletFrictionGain > 0.0001f) {
            float noise = mLocalPrng.NextBipolar();
            mFrictionFilterState += 0.05f * (noise - mFrictionFilterState);
            excitation += mFrictionFilterState * mMalletFrictionGain * pulse;
        }

        mMalletContactSampleCurrent++;
    } else if (mMalletReboundGain > 0.001f) {
        // Mallet rebound: tiny negative undershoot right after contact ends
        int reboundSample = mMalletContactSampleCurrent - mMalletContactSamplesTotal;
        if (reboundSample < 4) {
            float reboundPhase = static_cast<float>(reboundSample) / 3.0f;
            excitation = -mMalletReboundGain * mMalletPeakForce
                       * std::exp(-reboundPhase * 2.0f) * (1.0f - reboundPhase);
            mMalletContactSampleCurrent++;
        }
    }

    // 2. Dynamic Tension Modulation Pitch Glide on Mode 0
    if (mCurrentGlideCents > 0.02f) {
        float glideMult = std::pow(2.0f, mCurrentGlideCents / 1200.0f);
        float w_g = std::clamp(mModes[0].nominalOmega * glideMult, 0.001f, kPi - 0.001f);
        mModes[0].c = 2.0f * mModes[0].r * std::cos(w_g);
        mCurrentGlideCents *= mGlideDecay;
    } else if (mCurrentGlideCents > 0.0f) {
        mModes[0].c = 2.0f * mModes[0].r * std::cos(mModes[0].nominalOmega);
        mCurrentGlideCents = 0.0f;
    }

    // 3. Bar Modal Resonator Updates (8 pitched modes)
    float barAcousticSum = 0.0f;
    float barMechanicalSum = 0.0f;
    float totalEnergy = 0.0f;

    for (int m = 0; m < kNumPitchedModes; ++m) {
        float ym = mModes[m].c * mModes[m].y1 - mModes[m].s * mModes[m].y2 + mModes[m].b0 * excitation;

        // Viscoelastic contact damping: mallet actively damps upper modes during contact
        if (inContact && m >= 2 && mContactDampFactor > 0.01f) {
            ym *= (1.0f - mContactDampFactor * 0.003f);
        }

        mModes[m].y2 = mModes[m].y1;
        mModes[m].y1 = ym;

        barAcousticSum += ym;
        
        float piezoWeight = (m == 0) ? 0.5f : (m == 1) ? 1.0f : (m >= 6) ? 1.5f : 1.2f;
        barMechanicalSum += ym * piezoWeight;

        totalEnergy += std::abs(ym);
    }

    // Update Anisotropic Twin Mode 0b (wood grain beating)
    if (mMode0b.b0 != 0.0f) {
        float y0b = mMode0b.c * mMode0b.y1 - mMode0b.s * mMode0b.y2 + mMode0b.b0 * excitation;
        mMode0b.y2 = mMode0b.y1;
        mMode0b.y1 = y0b;
        barAcousticSum += y0b;
        barMechanicalSum += y0b * 0.6f;
        totalEnergy += std::abs(y0b);
    }

    // 4. Strike Clack Noise Burst (broadband transient, not a tonal mode)
    float clackSound = 0.0f;
    if (mClackEnvelope > 0.0001f) {
        // Generate filtered noise through the bandpass
        float noise = mLocalPrng.NextBipolar();
        float bpInput = noise * mClackEnvelope;
        float bpOut = mClackBpC * mClackBpY1 - mClackBpS * mClackBpY2 + (1.0f - mClackBpS) * bpInput;
        mClackBpY2 = mClackBpY1;
        mClackBpY1 = bpOut;
        clackSound = bpOut;
        mClackEnvelope *= mClackDecayRate;
    }

    // 5. Resonator Tube / Gourd Acoustic Coupling
    float barVel0 = (mModes[0].y1 - mModes[0].y2);
    float driveRes = barVel0 * (0.35f + 0.65f * mResCoupling);

    float yRes = mResC * mResY1 - mResS * mResY2 + (1.0f - mResS) * driveRes;
    mResY2 = mResY1;
    mResY1 = yRes;

    // 6. Mirliton Buzz Membrane Simulation
    float buzzComponent = 0.0f;
    if (mHasMirliton) {
        float absRes = std::abs(yRes);
        if (absRes > mMirlitonThreshold) {
            float excess = absRes - mMirlitonThreshold;
            float rawBuzz = (yRes > 0.0f ? 1.0f : -1.0f) * std::tanh(mMirlitonDrive * excess * excess);
            mMirlitonFilterState += 0.35f * (rawBuzz - mMirlitonFilterState);
            buzzComponent = mMirlitonFilterState * 0.75f;
        } else {
            mMirlitonFilterState *= 0.92f;
            buzzComponent = mMirlitonFilterState * 0.75f;
        }
    }

    // 7. Cord / Mechanical Rattle
    float rattleSound = 0.0f;
    if (mRattleLevel > 0.0001f) {
        rattleSound = mLocalPrng.NextBipolar() * mRattleLevel;
        mRattleLevel *= mRattleDecay;
    }

    // 8. Synthesize Microphone Outputs (with clack noise burst in close and piezo)
    outClose = (barAcousticSum * 0.90f) + (yRes * 0.25f * mResCoupling) + (buzzComponent * 0.35f)
             + rattleSound + (clackSound * 0.50f);
    outFar = (yRes * 0.50f * mResCoupling) + (barAcousticSum * 0.55f) + (buzzComponent * 0.50f)
           + (clackSound * 0.20f); // Clack is less prominent in far mic
    outPiezo = (barMechanicalSum * 0.90f) + (rattleSound * 1.0f) + (clackSound * 0.60f);

    // 9. Voice Inactivity Detection
    mEnvelopeEstimate = totalEnergy + std::abs(yRes) + mClackEnvelope;
    bool stillInContact = (mMalletContactSampleCurrent < mMalletContactSamplesTotal + 4);
    if (!stillInContact && mEnvelopeEstimate < 1.0e-6f) {
        mActive = false;
        return false;
    }

    return true;
}

} // namespace maremba
