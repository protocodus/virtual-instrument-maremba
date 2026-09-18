#pragma once

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

private:
    uint32_t mState;
};

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
