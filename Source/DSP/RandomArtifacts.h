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

// A new contact/release character for every strike, including Variance = 0.
// It changes the pitched excitation itself, not just the separate clack noise.
// No position/pitch offsets: exact strike nodes and settled tuning are retained.
struct StrikeCharacter {
    double contactTimeScale;
    double pulseSkewOffset;
    double impulseScale;
    double shockScale;
    double modalScale[kNumPitchedModes];
};

inline StrikeCharacter GenerateStrikeCharacter(FastPRNG& prng, float variance) {
    const double amount = std::isfinite(variance) ? std::clamp(variance, 0.0f, 100.0f) * 0.01 : 0.0;
    StrikeCharacter strike;
    // Fixed draw count at note-on, independent of amount and sample rate. The
    // engine's strike PRNG survives voice reuse/silence; explicit Reset reseeds it.
    strike.contactTimeScale = 1.0 + prng.NextGaussianDouble() * (0.0075 + 0.04 * amount);
    strike.pulseSkewOffset = prng.NextGaussianDouble() * (0.006 + 0.025 * amount);
    strike.impulseScale = 1.0 + prng.NextGaussianDouble() * (0.0035 + 0.01 * amount);
    strike.shockScale = 1.0 + prng.NextGaussianDouble() * (0.06 * amount);
    for (int mode = 0; mode < kNumPitchedModes; ++mode)
        strike.modalScale[mode] = 1.0 + prng.NextGaussianDouble() * (0.004 + 0.035 * amount);
    return strike;
}

struct StrikeArtifacts {
    float strikePositionOffset; // +/- jitter
    float malletHardnessJitter; // +/- human strike firmness
    float pitchJitterCents;     // Wood grain micro-detuning
    float frictionNoiseBurst;   // Initial yarn friction amplitude
    float mechanicalRattle;     // Frame / cord grommet buzz on hard strikes
};

inline StrikeArtifacts GenerateStrikeArtifacts(FastPRNG& prng, float velocity, float baseHardness, float artifactsAmount) {
    StrikeArtifacts art;
    if (artifactsAmount <= 0.001f) {
        art.strikePositionOffset = 0.0f;
        art.malletHardnessJitter = 0.0f;
        art.pitchJitterCents = 0.0f;
        art.frictionNoiseBurst = 0.0f;
        art.mechanicalRattle = 0.0f;
        return art;
    }

    // Position jitter (+/- 5% max)
    art.strikePositionOffset = prng.NextGaussian() * 0.04f * artifactsAmount;
    
    // Mallet firmness jitter (+/- 4% max)
    art.malletHardnessJitter = prng.NextGaussian() * 0.035f * artifactsAmount;
    
    // Wood grain micro-detuning (+/- 0.6 cents)
    art.pitchJitterCents = prng.NextGaussian() * 0.65f * artifactsAmount;
    
    // Yarn friction burst: increases with softer yarn mallets and higher velocity
    float yarnFactor = (1.0f - baseHardness);
    art.frictionNoiseBurst = (0.02f + 0.08f * yarnFactor) * (0.3f + 0.7f * velocity) * artifactsAmount;
    
    // Cord / frame rattle on forte hits
    if (velocity > 0.65f) {
        float excess = (velocity - 0.65f) / 0.35f;
        art.mechanicalRattle = excess * excess * 0.06f * artifactsAmount;
    } else {
        art.mechanicalRattle = 0.0f;
    }

    return art;
}

} // namespace maremba
