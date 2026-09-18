#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

class AnalogPreamp {
public:
    AnalogPreamp() {
        Reset();
    }

    void Reset() {
        mDcBlockInL = mDcBlockOutL = 0.0f;
        mDcBlockInR = mDcBlockOutR = 0.0f;
        mTiltLowL = mTiltLowR = 0.0f;
        mPresLowL = mPresLowR = 0.0f;
    }

    void SetSampleRate(float sampleRate) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 88200.0f;
        mTiltAlpha = 1.0f - std::exp(-6.283185307f * 900.0f / mSampleRate);
        mPresAlpha = 1.0f - std::exp(-6.283185307f * 3200.0f / mSampleRate);
    }

    // Process stereo frame through analog saturation and warmth tilt EQ
    void Process(float inL, float inR, float drive, float warmth, float& outL, float& outR) {
        // 1. Input Gain & Saturation Drive with clean headroom
        float driveGain = 1.0f + drive * 1.8f;
        float driveComp = 1.0f / std::sqrt(1.0f + drive * 1.5f);

        float xL = inL * driveGain;
        float xR = inR * driveGain;

        // Smooth transformer/tube saturation with subtle harmonic warmth
        auto Saturate = [](float x) -> float {
            float bias = 0.03f * x * x;
            float saturated = std::tanh(x + bias) - std::tanh(bias);
            return saturated;
        };

        float satL = Saturate(xL) * driveComp;
        float satR = Saturate(xR) * driveComp;

        // 2. DC Blocker
        float dcL = satL - mDcBlockInL + 0.995f * mDcBlockOutL;
        mDcBlockInL = satL;
        mDcBlockOutL = dcL;

        float dcR = satR - mDcBlockInR + 0.995f * mDcBlockOutR;
        mDcBlockInR = satR;
        mDcBlockOutR = dcR;

        // 3. Baxandall / Tilt Tone Filter
        // Center freq ~ 900 Hz. warmth in [-1, +1].
        mTiltLowL += mTiltAlpha * (dcL - mTiltLowL);
        mTiltLowR += mTiltAlpha * (dcR - mTiltLowR);

        float highL = dcL - mTiltLowL;
        float highR = dcR - mTiltLowR;

        float lowGain = 1.0f - warmth * 0.35f;
        float highGain = 1.0f + warmth * 0.35f;

        float toneL = (mTiltLowL * lowGain) + (highL * highGain);
        float toneR = (mTiltLowR * lowGain) + (highR * highGain);

        // 4. Acoustic Presence Shelf (+1.2 dB above 3.2 kHz)
        mPresLowL += mPresAlpha * (toneL - mPresLowL);
        mPresLowR += mPresAlpha * (toneR - mPresLowR);

        float presHighL = toneL - mPresLowL;
        float presHighR = toneR - mPresLowR;

        outL = toneL + 0.15f * presHighL;
        outR = toneR + 0.15f * presHighR;
    }

private:
    float mSampleRate = 88200.0f;
    float mTiltAlpha = 0.062f;
    float mPresAlpha = 0.205f;

    float mDcBlockInL = 0.0f;
    float mDcBlockOutL = 0.0f;
    float mDcBlockInR = 0.0f;
    float mDcBlockOutR = 0.0f;
    float mTiltLowL = 0.0f;
    float mTiltLowR = 0.0f;
    float mPresLowL = 0.0f;
    float mPresLowR = 0.0f;
};

} // namespace maremba
