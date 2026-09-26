#pragma once

#include "MarimbaModel.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace maremba {

class FastPRNG {
public:
    explicit FastPRNG(uint32_t seed = 0x12345678) : mState(seed ? seed : 0x12345678) {}

    inline uint32_t Next() {
        uint32_t x = mState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        mState = x;
        return x;
    }

    // Uniform float in [0, 1)
    inline float NextFloat() {
        return (Next() & 0x00FFFFFF) * (1.0f / 16777216.0f);
    }

    // Uniform float in [-1, +1)
    inline float NextBipolar() {
        return NextFloat() * 2.0f - 1.0f;
    }

    // Gaussian-like approximation via central limit
    inline float NextGaussian() {
        return (NextBipolar() + NextBipolar() + NextBipolar()) * 0.33333333f;
    }

    // Full-state precision for event-rate contact parameters. The audio noise
    // helpers above retain their existing sequence and cost.
    inline double NextGaussianDouble() {
        constexpr double scale = 1.0 / 2147483648.0;
        const double a = static_cast<double>(Next()) * scale - 1.0;
        const double b = static_cast<double>(Next()) * scale - 1.0;
        const double c = static_cast<double>(Next()) * scale - 1.0;
        return (a + b + c) / 3.0;
    }

private:
    uint32_t mState;
};

// A new physical stroke for every strike, including Variance = 0: where the
// mallet lands along and across the bar, how fast, and how the felt happens
// to compress. Variance widens the spread; zero keeps a player's restrained
// consistency rather than identical waveforms. No pitch offsets.
struct StrikeCharacter {
    double stiffnessScale;   // felt/yarn compression varies stroke to stroke
    double velocityScale;    // stick speed
    double positionOffset;   // along the bar, fraction of its length
    double lateralOffset;    // across the bar, fraction of the half width (twist)
    double hysteresisScale;  // felt loss
};

inline StrikeCharacter GenerateStrikeCharacter(FastPRNG& prng, float variance) {
    const double amount = std::isfinite(variance) ? std::clamp(variance, 0.0f, 100.0f) * 0.01 : 0.0;
    // Widen contact duration (the head's stiffness), release shape (its loss),
    // partial balance (where it lands) and speed from the first movement of
    // the control. This smooth concave curve gives low settings useful
    // expression while retaining the natural floor and maximum spread.
    const double expressive = amount * (2.0 - amount);
    StrikeCharacter strike;
    // Fixed draw count at note-on, independent of amount and sample rate. The
    // engine's strike PRNG survives voice reuse/silence; explicit Reset reseeds it.
    strike.stiffnessScale = 1.0 + prng.NextGaussianDouble() * (0.03 + 0.35 * expressive);
    strike.velocityScale = 1.0 + prng.NextGaussianDouble() * (0.004 + 0.08 * expressive);
    // At Variance 0 a player still lands within about half a millimetre.
    strike.positionOffset = prng.NextGaussianDouble() * (0.0012 + 0.07 * expressive);
    strike.lateralOffset = prng.NextGaussianDouble() * (0.25 + 0.30 * expressive);
    strike.hysteresisScale = 1.0 + prng.NextGaussianDouble() * (0.02 + 0.15 * expressive);
    return strike;
}

// An event plan, sampled once per stroke. Amount controls the frequency and
// severity of playing imperfections, not an independent broadband noise bed.
struct StrikeArtifacts {
    float strikePositionOffset = 0.0f;
    float malletHardnessJitter = 0.0f;
    float pitchJitterCents = 0.0f;
    float frictionNoiseBurst = 0.0f;
    float impulseScale = 1.0f;       // glancing/missed contact loses energy
    float reboundGain = 0.0f;       // fraction of stroke impulse held for bounce
    float reboundDelaySec = 0.0f;
    float brushGain = 0.0f;         // fraction transferred to an adjacent key
    float brushDelaySec = 0.0f;
    float brushSemitones = 0.0f;
    float collisionAmount = 0.0f;   // loose mounting driven by bar motion
    float brushDurationScale = 1.0f;
    float brushDecayScale = 1.0f;
    float collisionPitchScale = 1.0f;
    float collisionDecayScale = 1.0f;
    float collisionGapScale = 1.0f;
    float collisionPulseScale = 1.0f;
};

inline StrikeArtifacts GenerateStrikeArtifacts(FastPRNG& prng, float velocity,
        float baseHardness, float artifactsAmount,
        EMarimbaModel model = EMarimbaModel::ImperialRosewood) {
    const auto response = GetInstrumentResponseProfile(model);
    const float amount = std::clamp(artifactsAmount, 0.0f, 1.0f);
    const float activity = amount * amount;
    const float touch = std::clamp(velocity, 0.0f, 1.0f);
    // Fixed draw count, including at zero. A/B control changes use the same
    // event sequence and audio-rate noise cannot affect playing decisions.
    const float position = prng.NextGaussian();
    const float firmness = prng.NextGaussian();
    const float tuning = prng.NextGaussian();
    const float graze = prng.NextFloat();
    const float rebound = prng.NextFloat();
    const float brush = prng.NextFloat();
    const float collision = prng.NextFloat();
    const uint32_t detailSeed = prng.Next();
    const float severity = (detailSeed & 0x00FFFFFF) * (1.0f / 16777216.0f);
    const float timing = prng.NextFloat();
    const float direction = prng.NextFloat() < 0.5f ? -1.0f : 1.0f;
    // Independent continuous contact details, without consuming extra draws
    // from the stroke stream. Even at zero Artifacts, later physical strikes
    // keep their deterministic sequence. Events are synthesized, not selected
    // from a small bank of repeating samples.
    FastPRNG detail(detailSeed ^ 0x9e3779b9u);
    const float bounceStrength = detail.NextFloat();
    const float brushStrength = detail.NextFloat();
    const float brushTiming = detail.NextFloat();
    StrikeArtifacts art;
    art.strikePositionOffset = position * 0.020f * amount;
    art.malletHardnessJitter = firmness * 0.025f * amount;
    art.pitchJitterCents = tuning * 0.25f * amount;
    // Small friction remains local to the contact pulse; no boosted clack bed.
    art.frictionNoiseBurst = 0.008f * (1.0f - baseHardness) * amount;
    if (graze < response.grazingProbability * activity) {
        art.strikePositionOffset += direction * (0.025f + 0.075f * severity) * amount;
        art.malletHardnessJitter -= (0.06f + 0.04f * severity) * amount;
        art.impulseScale -= (0.05f + 0.15f * severity) * amount;
    }
    if (rebound < response.reboundProbability * activity * (0.35f + 0.65f * touch)) {
        art.reboundGain = (0.035f + 0.10f * bounceStrength * bounceStrength) * amount;
        art.reboundDelaySec = response.reboundDelayMs * 0.001f
            * (0.65f + 0.7f * timing) * (1.15f - 0.3f * baseHardness);
    }
    if (brush < response.neighborBrushProbability * activity) {
        art.brushGain = (0.025f + 0.075f * brushStrength * brushStrength) * amount;
        art.brushDelaySec = 0.002f + 0.009f * brushTiming;
        // Adjacent physical keys need not be a chromatic semitone apart.
        art.brushSemitones = direction * (model == EMarimbaModel::KalimbaArtisan ? 3.0f : 2.0f);
    }
    if (collision < response.collisionProbability * amount) {
        art.collisionAmount = 0.65f * amount * (0.35f + 0.65f * touch) * (0.55f + 0.45f * severity);
    }
    art.brushDurationScale += 0.35f * detail.NextGaussian() * amount;
    art.brushDecayScale += 0.30f * detail.NextGaussian() * amount;
    art.collisionPitchScale = std::exp2(0.35f * detail.NextGaussian() * amount);
    art.collisionDecayScale += 0.40f * detail.NextGaussian() * amount;
    art.collisionGapScale += 0.20f * detail.NextGaussian() * amount;
    art.collisionPulseScale += 0.35f * detail.NextGaussian() * amount;
    return art;
}

} // namespace maremba
