#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

// Optional gentle widening after the physical Main capture mix. Preserve the
// original L/R, Mid and natural Side. Two unit-magnitude allpass branches supply
// only a small additional Side: their half-difference is bounded by the input
// magnitude at every frequency and cancels at DC. No compensation, model gain
// or polarity-dependent adjustment replaces the physical recording scene.
// Off is an exact bypass; histories remain warm for smooth live re-enabling.
class MainStereo {
public:
    static constexpr int kStages = 8;
    static constexpr double kFirstPoleHz = 5.0;
    static constexpr double kPoleRatio = 2.5;
    static constexpr float kGeneratedSideGain = .15f;

    MainStereo() { SetSampleRate(44100.0); }
    void SetSampleRate(double rate) {
        rate = std::isfinite(rate) && rate > 8000.0 ? rate : 44100.0;
        if (rate == mRate) return;
        mRate = rate;
        double pole = kFirstPoleHz;
        for (int i = 0; i < kStages; ++i) {
            mReference[i] = Coefficient(pole, rate);
            mQuadrature[i] = Coefficient(pole * kPoleRatio, rate);
            pole *= kPoleRatio * kPoleRatio;
        }
        Reset();
    }
    void Reset() { mMid.fill(0.0); mWide.fill(0.0); }

    // The engine smooths wet and Width before this call. Both scale after the
    // histories, so Off/Width zero extinguish all added Side without tail snaps.
    void Process(float& left, float& right, float width, float wet = 0.0f) {
        const double mid = .5 * (double(left) + right);
        const double reference = Cascade(mid, mReference, mMid);
        const double wide = Cascade(mid, mQuadrature, mWide);
        const double amount = std::isfinite(width) ? std::clamp(width, 0.0f, 2.0f) : 0.0f;
        const double blend = std::isfinite(wet) ? std::clamp(wet, 0.0f, 1.0f) : 0.0f;
        if (blend == 0.0 || amount == 0.0) return;
        const double side = kGeneratedSideGain * amount * blend * .5 * (wide - reference);
        left = static_cast<float>(double(left) - side);
        right = static_cast<float>(double(right) + side);
    }
    void FlushTiny() {
        for (auto* state : {&mMid, &mWide})
            for (double& value : *state) if (std::abs(value) < 1.e-30) value = 0.0;
    }
    bool HasNoSubnormalState() const {
        for (const auto* state : {&mMid, &mWide})
            for (double value : *state) if (std::fpclassify(value) == FP_SUBNORMAL) return false;
        return true;
    }

private:
    static double Coefficient(double poleHz, double rate) {
        // Bilinear transform of an analog first-order allpass pole. Using the
        // pole frequency directly (not tan prewarping) preserves the ordered
        // analog network even when its upper poles lie above Nyquist.
        const double pole = 6.2831853071795864769 * poleHz;
        return (pole - 2.0 * rate) / (pole + 2.0 * rate);
    }
    static double Cascade(double value, const std::array<double, kStages>& coefficient,
                          std::array<double, kStages>& state) {
        for (int i = 0; i < kStages; ++i) {
            const double output = coefficient[i] * value + state[i];
            state[i] = value - coefficient[i] * output;
            value = output;
        }
        return value;
    }
    double mRate = 0.0;
    std::array<double, kStages> mReference{}, mQuadrature{};
    std::array<double, kStages> mMid{}, mWide{};
};

} // namespace maremba
