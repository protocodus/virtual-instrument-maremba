#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

// A common presentation for the Main bus, after the physical capture mix.
// Two interleaved allpass cascades have unit magnitude and approximately 90
// degrees differential phase across the instrument band. Deriving Side from
// the complete Mid (including the aligned pickup) keeps relative width steady
// when the pickup polarity changes. Main uses the captures' mono mean; keeping
// natural Side here would reintroduce large image changes when Mid cancels.
// Raw Close, Room and Piezo never pass through this stage. Mono fold-down has
// the original Mid spectrum with the reference cascade's phase response and
// fixed Width energy compensation. This is an authored Main effect.
class MainStereo {
public:
    static constexpr int kStages = 8;
    static constexpr double kFirstPoleHz = 5.0;
    static constexpr double kPoleRatio = 2.5;
    static constexpr float kGeneratedSideGain = .5f;

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

    // Width scales the generated field after its filter history, so reducing
    // Width to zero smoothly extinguishes every Side tail and gives exact mono.
    void Process(float& left, float& right, float width) {
        const double mid = .5 * (double(left) + right);
        const double reference = Cascade(mid, mReference, mMid);
        const double wide = Cascade(mid, mQuadrature, mWide);
        const double amount = std::isfinite(width) ? std::clamp(width, 0.0f, 2.0f) : 0.0f;
        const double side = kGeneratedSideGain * amount * wide;
        // Fixed equal-power compensation prevents creating the common field
        // from simply making narrow presets louder. It follows Width alone,
        // with no envelope detector or model/polarity-dependent adjustment.
        const double field = kGeneratedSideGain * amount;
        const double gain = 1.0 / std::sqrt(1.0 + field * field);
        left = static_cast<float>(gain * (reference - side));
        right = static_cast<float>(gain * (reference + side));
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
