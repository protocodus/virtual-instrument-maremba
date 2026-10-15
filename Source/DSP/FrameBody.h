#pragma once

#include "DspMath.h"
#include "MarimbaModel.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace maremba {

// Reduced force-to-velocity-shaped response of the support and wooden
// radiating surfaces to normalized mechanical drive (not calibrated force). Each damped mode has zeros at DC and Nyquist: static displacement
// cannot radiate, and a low note cannot leave a large unpitched displacement
// offset in the microphone. Frequencies/Q/levels are voiced, not measurements.
class FrameBody {
public:
    void Reset() {
        for (auto& mode : mModes) mode.y1 = mode.y2 = 0.0;
        mInput1 = mInput2 = mHpInPrev = mHpOutPrev = 0.0;
    }

    void FlushTiny() {
        for (auto& mode : mModes) {
            if (std::abs(mode.y1) + std::abs(mode.y2) < 1.0e-15)
                mode.y1 = mode.y2 = 0.0;
        }
        if (std::abs(mInput1) < 1.0e-15) mInput1 = 0.0;
        if (std::abs(mInput2) < 1.0e-15) mInput2 = 0.0;
        if (std::abs(mHpInPrev) < 1.0e-15) mHpInPrev = 0.0;
        if (std::abs(mHpOutPrev) < 1.0e-15) mHpOutPrev = 0.0;
    }

    bool HasNoSubnormalState() const {
        for (const auto& mode : mModes)
            if (std::fpclassify(mode.y1) == FP_SUBNORMAL || std::fpclassify(mode.y2) == FP_SUBNORMAL)
                return false;
        return std::fpclassify(mInput1) != FP_SUBNORMAL && std::fpclassify(mInput2) != FP_SUBNORMAL
            && std::fpclassify(mHpInPrev) != FP_SUBNORMAL && std::fpclassify(mHpOutPrev) != FP_SUBNORMAL;
    }

    void Configure(double sampleRate, double frameFreq, double frameQ,
                   EMarimbaModel model = EMarimbaModel::ImperialRosewood) {
        const double fs = sampleRate > 8000.0 ? sampleRate : 44100.0;
        const int index = std::clamp(static_cast<int>(model), 0, 3);
        const double f1 = std::clamp(frameFreq, 40.0, 300.0);
        const double q1 = std::clamp(frameQ, 1.0, 2.5);
        // Rail/end-cheek, wooden box panels, bamboo frame, or kalimba board.
        static constexpr double secondRatio[4] = {2.85, 2.35, 3.15, 1.83};
        static constexpr double thirdRatio[4] = {5.2, 4.1, 5.6, 3.47};
        const double frequencies[3] = {f1, f1 * secondRatio[index], f1 * thirdRatio[index]};
        // Keep each frame beneath its instrument's accepted bar/cavity balance.
        // Calibrate radiation here, retaining Bloom's full control range.
        const double radiationScale = GetModelParams(model).frameRadiationGain;
        const double weights[3] = {0.15 * radiationScale, 0.10 * radiationScale,
                                  (index == 3 ? 0.055 : 0.035) * radiationScale};
        for (int i = 0; i < 3; ++i) {
            auto& mode = mModes[i];
            const double frequency = std::min(frequencies[i], fs * 0.1);
            const double q = q1 * (i == 2 ? 0.75 : 1.0);
            const double w = kTwoPiD * frequency / fs;
            const double r = std::exp(-kPiD * frequency / (q * fs));
            // Preserve amplitude/phase across a live instrument change.
            // Reusing the old delayed state with a new frequency can pump it.
            if (mode.r > 0.0 && mode.w > 1.0e-12) {
                const double quadrature = (mode.r * mode.y2 - mode.y1 * std::cos(mode.w)) / std::sin(mode.w);
                mode.y2 = (mode.y1 * std::cos(w) + quadrature * std::sin(w)) / r;
            }
            mode.r = r;
            mode.w = w;
            mode.c = 2.0 * r * std::cos(w);
            mode.s = r * r;
            const double rRef = std::exp(-kPiD * frequency / (q * kReferenceRate));
            const double wRef = kTwoPiD * frequency / kReferenceRate;
            const double centreGain = (1.0 - rRef) * weights[i] / ResonatorDenominatorAt(rRef, wRef);
            mode.b = centreGain * ResonatorDenominatorAt(r, w) / (2.0 * std::sin(w));
        }
        mHpAlpha = std::exp(-kTwoPiD * 70.0 / fs);
    }

    float Process(float mechanicalIn) {
        const double forceDifference = mechanicalIn - mInput2;
        mInput2 = mInput1;
        mInput1 = mechanicalIn;
        double raw = 0.0;
        for (auto& mode : mModes) {
            const double y = mode.c * mode.y1 - mode.s * mode.y2 + mode.b * forceDifference;
            mode.y2 = mode.y1;
            mode.y1 = y;
            raw += y;
        }
        const double out = mHpAlpha * (mHpOutPrev + raw - mHpInPrev);
        mHpInPrev = raw;
        mHpOutPrev = out;
        return static_cast<float>(out);
    }

private:
    struct Mode { double y1 = 0.0, y2 = 0.0, c = 0.0, s = 0.0, b = 0.0, r = 0.0, w = 0.0; };
    std::array<Mode, 3> mModes{};
    double mInput1 = 0.0, mInput2 = 0.0;
    double mHpAlpha = 0.99, mHpInPrev = 0.0, mHpOutPrev = 0.0;
};

} // namespace maremba
