#pragma once

#include <cmath>
#include <algorithm>

namespace maremba {

class FrameBody {
public:
    FrameBody() {
        Reset();
    }

    void Reset() {
        mY1_1 = mY2_1 = 0.0f;
        mY1_2 = mY2_2 = 0.0f;
        mHpInPrev = 0.0f;
        mHpOutPrev = 0.0f;
    }

    void Configure(float sampleRate, float frameFreq, float frameQ) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 44100.0f;
        
        // Mode 1: Primary frame / rail mass resonance (e.g. ~86-145 Hz)
        // High internal friction in wood rails gives heavily damped mechanical Q (1.0 - 2.5)
        float f1 = std::clamp(frameFreq, 40.0f, 300.0f);
        float q1 = std::clamp(frameQ, 1.0f, 2.5f);
        float r1 = std::exp(-3.1415926535f * f1 / (q1 * mSampleRate));
        float w1 = 6.283185307f * f1 / mSampleRate;
        mC1 = 2.0f * r1 * std::cos(w1);
        mS1 = r1 * r1;
        mB1 = (1.0f - r1) * 0.15f;

        // Mode 2: Secondary wooden end-cheek / rail impulse thump
        // Damped identically to prevent pitched sine-wave ringing
        float f2 = f1 * 2.85f;
        if (f2 > 600.0f) f2 = 600.0f;
        float q2 = q1;
        float r2 = std::exp(-3.1415926535f * f2 / (q2 * mSampleRate));
        float w2 = 6.283185307f * f2 / mSampleRate;
        mC2 = 2.0f * r2 * std::cos(w2);
        mS2 = r2 * r2;
        mB2 = (1.0f - r2) * 0.10f;

        // High-pass filter coefficient (~70 Hz cutoff to prevent sub rumble)
        float hpCutoff = 70.0f;
        float hpR = std::exp(-6.283185307f * hpCutoff / mSampleRate);
        mHpAlpha = hpR;
    }

    // Process a mechanical excitation sample from the bars
    inline float Process(float mechanicalIn) {
        float y1 = mC1 * mY1_1 - mS1 * mY2_1 + mB1 * mechanicalIn;
        mY2_1 = mY1_1;
        mY1_1 = y1;

        float y2 = mC2 * mY1_2 - mS2 * mY2_2 + mB2 * mechanicalIn;
        mY2_2 = mY1_2;
        mY1_2 = y2;

        float rawOut = y1 + y2;

        // 1-pole high-pass: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        float hpOut = mHpAlpha * (mHpOutPrev + rawOut - mHpInPrev);
        mHpInPrev = rawOut;
        mHpOutPrev = hpOut;

        return hpOut;
    }

private:
    float mSampleRate = 44100.0f;
    float mY1_1 = 0.0f, mY2_1 = 0.0f;
    float mC1 = 0.0f, mS1 = 0.0f, mB1 = 0.0f;
    float mY1_2 = 0.0f, mY2_2 = 0.0f;
    float mC2 = 0.0f, mS2 = 0.0f, mB2 = 0.0f;
    float mHpAlpha = 0.99f;
    float mHpInPrev = 0.0f;
    float mHpOutPrev = 0.0f;
};

} // namespace maremba
