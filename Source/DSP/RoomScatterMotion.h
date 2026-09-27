#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maremba {

// Slowly changing, lossless scattering for a twelve-line late FDN. This is
// subtle statistical room motion, not changing propagation paths or Doppler.
// Each disjoint pair joins different three-line groups in FeedbackNetwork.
// Apply BEFORE injecting the current input into that network: its output
// subtraction relies on the existing, fixed injection vector.
class RoomScatterMotion {
public:
    static constexpr int kPairs = 6;
    static constexpr double kMinimumSeconds = 2.5;
    static constexpr double kMaximumSeconds = 6.5;
    static constexpr float kMaximumHalfAngle = .015f;
    // Actual maximum angle is 2*atan(.015), slightly below this bound.
    static constexpr double kMaximumAngleRadians = .030;
    // Cubic interpolation has maximum slope 1.5. Even opposite endpoints
    // cannot rotate faster than 2 * 1.5 * .030 / 2.5 radians per second.
    static constexpr double kMaximumAngularSpeed = .036;
    static constexpr std::array<std::array<int, 2>, kPairs> kIndices{{
        {{0, 3}}, {{1, 6}}, {{2, 9}}, {{4, 7}}, {{5, 10}}, {{8, 11}}
    }};

    RoomScatterMotion() { Configure(48000.0, 0); }

    // Configuration/reset are deterministic and allocation-free. Seconds,
    // not callbacks or oversampling factors, determine the motion rate.
    void Configure(double sampleRate, std::uint32_t seed) {
        const double rate = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 1536000.0) : 48000.0;
        mStepSeconds = 1.0 / rate;
        mSeed = seed;
        Reset();
    }

    void Reset() {
        for (int index = 0; index < kPairs; ++index) {
            auto& motion = mMotion[static_cast<std::size_t>(index)];
            motion = {};
            motion.random = Mix(mSeed + 0x9e3779b9u * static_cast<std::uint32_t>(index + 1));
            if (motion.random == 0) motion.random = 0x6d2b79f5u;
            NextSegment(motion);
        }
    }

    // No signal-dependent RNG or state advancement. Zero input stays exactly
    // zero while the same motion continues. The first call after Reset is the
    // identity. Call once per FDN sample, including quiet samples.
    void Process(float* twelveValues) {
        for (int index = 0; index < kPairs; ++index) {
            auto& motion = mMotion[static_cast<std::size_t>(index)];
            const float phase = static_cast<float>(motion.elapsed * motion.inverseDuration);
            const float smooth = phase * phase * (3.0f - 2.0f * phase);
            const float halfAngle = motion.from + smooth * (motion.to - motion.from);
            const float squared = halfAngle * halfAngle;
            const float inverse = 1.0f / (1.0f + squared);
            // Cayley/half-angle parameterization: c*c+s*s = 1 in exact
            // arithmetic. Interpolating c/s separately would add gain loss.
            const float cosine = 1.0f - 2.0f * squared * inverse;
            const float sine = 2.0f * halfAngle * inverse;
            const auto pair = kIndices[static_cast<std::size_t>(index)];
            const float a = twelveValues[pair[0]], b = twelveValues[pair[1]];
            twelveValues[pair[0]] = cosine * a - sine * b;
            twelveValues[pair[1]] = sine * a + cosine * b;

            motion.elapsed += mStepSeconds;
            if (motion.elapsed >= motion.duration) {
                motion.elapsed -= motion.duration;
                NextSegment(motion);
            }
        }
    }

private:
    struct Motion {
        double elapsed = 0.0, duration = 1.0, inverseDuration = 1.0;
        float from = 0.0f, to = 0.0f;
        std::uint32_t random = 1;
    };

    static std::uint32_t Mix(std::uint32_t value) {
        value ^= value >> 16; value *= 0x7feb352du;
        value ^= value >> 15; value *= 0x846ca68bu;
        return value ^ (value >> 16);
    }
    static float Uniform(Motion& motion) {
        // Independent streams avoid rate-dependent draw order when two pairs
        // cross their segment boundaries during the same output sample.
        auto value = motion.random;
        value ^= value << 13; value ^= value >> 17; value ^= value << 5;
        motion.random = value;
        return static_cast<float>(value >> 8) * (1.0f / 16777216.0f);
    }
    static void NextSegment(Motion& motion) {
        motion.from = motion.to;
        motion.to = (2.0f * Uniform(motion) - 1.0f) * kMaximumHalfAngle;
        motion.duration = kMinimumSeconds
            + (kMaximumSeconds - kMinimumSeconds) * Uniform(motion);
        motion.inverseDuration = 1.0 / motion.duration;
    }

    std::array<Motion, kPairs> mMotion{};
    double mStepSeconds = 1.0 / 48000.0;
    std::uint32_t mSeed = 0;
};

} // namespace maremba
