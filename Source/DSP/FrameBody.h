#pragma once

#include "DspMath.h"
#include <cmath>
#include <algorithm>
#include <initializer_list>

namespace maremba {

class FrameBody {
public:
    FrameBody() {
        Reset();
    }

    void Reset() {
        mY1_1 = mY2_1 = 0.0;
        mY1_2 = mY2_2 = 0.0;
        mHpInPrev = 0.0;
        mHpOutPrev = 0.0;
    }

    // Control rate: once every state has decayed below 1e-15 (about -300 dB),
    // clear the body so it never cycles on subnormal values.
    void FlushTiny() {
        if (std::abs(mY1_1) < 1.0e-15 && std::abs(mY2_1) < 1.0e-15
            && std::abs(mY1_2) < 1.0e-15 && std::abs(mY2_2) < 1.0e-15
            && std::abs(mHpInPrev) < 1.0e-15 && std::abs(mHpOutPrev) < 1.0e-15) {
            Reset();
        }
    }

    // True when no state is subnormal (unit testing)
    bool HasNoSubnormalState() const {
        for (double v : { mY1_1, mY2_1, mY1_2, mY2_2, mHpInPrev, mHpOutPrev }) {
            if (std::fpclassify(v) == FP_SUBNORMAL) return false;
        }
        return true;
    }

    // Coefficients only (realtime safe); call when the rate or the model changes.
    void Configure(double sampleRate, double frameFreq, double frameQ) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;

        // Mode 1: Primary frame / rail mass resonance (e.g. ~86-145 Hz)
        // High internal friction in wood rails gives heavily damped mechanical Q (1.0 - 2.5)
        double f1 = std::clamp(frameFreq, 40.0, 300.0);
        double q1 = std::clamp(frameQ, 1.0, 2.5);
        ConfigureMode(f1, q1, 0.15, mC1, mS1, mB1);

        // Mode 2: Secondary wooden end-cheek / rail impulse thump
        // Damped identically to prevent pitched sine-wave ringing
        double f2 = std::min(f1 * 2.85, 600.0);
        ConfigureMode(f2, q1, 0.10, mC2, mS2, mB2);

        // High-pass filter coefficient (~70 Hz cutoff to prevent sub rumble)
        double hpCutoff = 70.0;
        mHpAlpha = std::exp(-kTwoPiD * hpCutoff / mSampleRate);
    }

    // Process a mechanical excitation sample from the bars
    inline float Process(float mechanicalIn) {
        double y1 = mC1 * mY1_1 - mS1 * mY2_1 + mB1 * mechanicalIn;
        mY2_1 = mY1_1;
        mY1_1 = y1;

        double y2 = mC2 * mY1_2 - mS2 * mY2_2 + mB2 * mechanicalIn;
        mY2_2 = mY1_2;
        mY1_2 = y2;

        double rawOut = y1 + y2;

        // 1-pole high-pass: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        double hpOut = mHpAlpha * (mHpOutPrev + rawOut - mHpInPrev);
        mHpInPrev = rawOut;
        mHpOutPrev = hpOut;

        return static_cast<float>(hpOut);
    }

private:
    // Two-pole resonator whose centre gain equals that of b = (1 - r) * k at the
    // reference rate (the plain (1 - r) * k form grows in proportion to fs).
    void ConfigureMode(double f, double q, double k, double& c, double& s, double& b) const {
        double r = std::exp(-kPiD * f / (q * mSampleRate));
        double w = kTwoPiD * f / mSampleRate;
        c = 2.0 * r * std::cos(w);
        s = r * r;
        double rRef = std::exp(-kPiD * f / (q * kReferenceRate));
        double wRef = kTwoPiD * f / kReferenceRate;
        b = RateInvariantResonatorGain((1.0 - rRef) * k, rRef, wRef, r, w);
    }

    double mSampleRate = 44100.0;
    double mY1_1 = 0.0, mY2_1 = 0.0;
    double mC1 = 0.0, mS1 = 0.0, mB1 = 0.0;
    double mY1_2 = 0.0, mY2_2 = 0.0;
    double mC2 = 0.0, mS2 = 0.0, mB2 = 0.0;
    double mHpAlpha = 0.99;
    double mHpInPrev = 0.0;
    double mHpOutPrev = 0.0;
};

} // namespace maremba
