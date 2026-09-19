#pragma once

#include "DspMath.h"
#include <algorithm>
#include <cmath>

namespace maremba {

class AnalogPreamp {
public:
    AnalogPreamp() {
        SetSampleRate(kReferenceRate);
        Reset();
    }

    void Reset() {
        mDcBlockInL = mDcBlockOutL = 0.0f;
        mDcBlockInR = mDcBlockOutR = 0.0f;
        mTiltLowL = mTiltLowR = 0.0f;
    }

    void SetSampleRate(double sampleRate) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
        mTiltAlpha = static_cast<float>(1.0 - std::exp(-kTwoPiD * 450.0 / mSampleRate));
        // DC blocker corner in Hz (was a fixed per-sample pole, i.e. a corner that moved with the rate)
        mDcPole = static_cast<float>(std::exp(-kTwoPiD * kDcBlockHz / mSampleRate));
    }

    // Process stereo frame through analog saturation and warmth tilt EQ
    void Process(float inL, float inR, float drive, float warmth, float& outL, float& outR) {
        float satL = inL;
        float satR = inR;

        // 1. High-Headroom Soft-Knee Saturation (clean bypass when drive <= 0.001)
        if (drive > 0.001f) {
            float driveGain = 1.0f + drive * 0.8f;
            float driveComp = 1.0f / std::sqrt(1.0f + drive * 0.7f);

            auto SoftSaturate = [drive](float x) -> float {
                float ax = std::abs(x);
                if (ax <= 0.60f) {
                    return x; // 100% linear, zero distortion below 0.60 (-4.4 dBFS)
                }
                float sign = (x > 0.0f) ? 1.0f : -1.0f;
                float excess = ax - 0.60f;
                float sat = 0.60f + 0.40f * std::tanh(excess * (1.0f + drive * 1.5f) / 0.40f);
                return sign * sat;
            };

            satL = SoftSaturate(inL * driveGain) * driveComp;
            satR = SoftSaturate(inR * driveGain) * driveComp;
        }

        // DC Blocker (20 Hz). Runs continuously, so toggling the drive never
        // resumes from a stale state.
        float dcL = satL - mDcBlockInL + mDcPole * mDcBlockOutL;
        mDcBlockInL = satL;
        mDcBlockOutL = dcL;
        satL = dcL;

        float dcR = satR - mDcBlockInR + mDcPole * mDcBlockOutR;
        mDcBlockInR = satR;
        mDcBlockOutR = dcR;
        satR = dcR;

        // 2. Transparent Baxandall / Tilt Tone Filter (flat at warmth = 0.0).
        // The low band tracks continuously so re-enabling the tilt is click-free.
        mTiltLowL += mTiltAlpha * (satL - mTiltLowL);
        mTiltLowR += mTiltAlpha * (satR - mTiltLowR);
        if (std::abs(warmth) > 0.001f) {
            float highL = satL - mTiltLowL;
            float highR = satR - mTiltLowR;

            float lowGain = 1.0f + warmth * 0.20f;
            float highGain = 1.0f - warmth * 0.20f;

            outL = (mTiltLowL * lowGain) + (highL * highGain);
            outR = (mTiltLowR * lowGain) + (highR * highGain);
        } else {
            outL = satL;
            outR = satR;
        }
    }

private:
    static constexpr double kDcBlockHz = 20.0;

    double mSampleRate = kReferenceRate;
    float mTiltAlpha = 0.031f;
    float mDcPole = 0.9986f;

    float mDcBlockInL = 0.0f;
    float mDcBlockOutL = 0.0f;
    float mDcBlockInR = 0.0f;
    float mDcBlockOutR = 0.0f;
    float mTiltLowL = 0.0f;
    float mTiltLowR = 0.0f;
};

} // namespace maremba
