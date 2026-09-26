#pragma once

#include "DspMath.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

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
        mCachedWarmth = 2.0f;
        mLowGain = mHighGain = mSmoothLow = mSmoothHigh = 1.0f;
    }

    // Idle or reset: the tilt starts at its setting instead of ramping from
    // flat under the next attack.
    void SnapTone(float warmth) {
        mCachedWarmth = warmth;
        mLowGain = std::exp2(warmth);
        mHighGain = 1.0f / mLowGain;
        mSmoothLow = mLowGain;
        mSmoothHigh = mHighGain;
    }

    // Control rate: with a silent input (all mic levels at 0) the DC blocker and
    // the tilt low-pass decay into float subnormals and stay there (a subnormal
    // times a pole close to 1 rounds back to itself). Zero them first.
    void FlushTiny() {
        for (float* x : { &mDcBlockInL, &mDcBlockOutL, &mDcBlockInR, &mDcBlockOutR, &mTiltLowL, &mTiltLowR }) {
            if (std::abs(*x) < kTinyState) *x = 0.0f;
        }
    }

    // True when no filter state is subnormal (unit testing)
    bool HasNoSubnormalState() const {
        for (float x : { mDcBlockInL, mDcBlockOutL, mDcBlockInR, mDcBlockOutR, mTiltLowL, mTiltLowR }) {
            if (std::fpclassify(x) == FP_SUBNORMAL) return false;
        }
        return true;
    }

    void SetSampleRate(double sampleRate) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
        mToneAlpha = static_cast<float>(1.0-std::exp(-1.0/(.005*mSampleRate)));
        mTiltAlpha = static_cast<float>(1.0 - std::exp(-kTwoPiD * 450.0 / mSampleRate));
        // DC blocker corner in Hz (was a fixed per-sample pole, i.e. a corner that moved with the rate)
        mDcPole = static_cast<float>(std::exp(-kTwoPiD * kDcBlockHz / mSampleRate));
    }

    // Stateless pickup amplifier transfer, shared by the mixed and direct
    // Piezo paths. Zero Drive leaves the sensor voltage exactly unchanged.
    static float ApplyDrive(float sample, float drive) {
        if (drive <= 0.001f) return sample;
        const float driveGain = 1.0f + drive * 0.8f;
        const float driveComp = 1.0f / std::sqrt(1.0f + drive * 0.7f);
        const float x = sample * driveGain;
        const float ax = std::abs(x);
        if (ax <= 0.60f) return x * driveComp;
        const float excess = ax - 0.60f;
        const float sat = 0.60f + 0.40f * std::tanh(excess * (1.0f + drive * 1.5f) / 0.40f);
        return std::copysign(sat * driveComp, x);
    }

    // Stereo DC removal and warmth. The engine applies Drive to Piezo before
    // capture mixing and passes zero here, keeping both microphones clean.
    void Process(float inL, float inR, float drive, float warmth, float& outL, float& outR) {
        float satL = ApplyDrive(inL, drive);
        float satR = ApplyDrive(inR, drive);

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
        // +/-6 dB shelf endpoints; cache exponentials at control changes and
        // smooth the actual gains over 5 ms, including transitions through flat.
        if (warmth != mCachedWarmth) {
            mCachedWarmth = warmth;
            mLowGain = std::exp2(warmth);
            mHighGain = 1.0f / mLowGain;
        }
        mSmoothLow += mToneAlpha * (mLowGain - mSmoothLow);
        mSmoothHigh += mToneAlpha * (mHighGain - mSmoothHigh);
        outL = mTiltLowL*mSmoothLow + (satL-mTiltLowL)*mSmoothHigh;
        outR = mTiltLowR*mSmoothLow + (satR-mTiltLowR)*mSmoothHigh;
    }

private:
    static constexpr double kDcBlockHz = 20.0;
    static constexpr float kTinyState = 1.0e-20f;   // about -400 dB

    double mSampleRate = kReferenceRate;
    float mTiltAlpha = 0.031f;
    float mDcPole = 0.9986f;

    float mDcBlockInL = 0.0f;
    float mDcBlockOutL = 0.0f;
    float mDcBlockInR = 0.0f;
    float mDcBlockOutR = 0.0f;
    float mTiltLowL = 0.0f;
    float mTiltLowR = 0.0f;
    float mCachedWarmth = 2.0f;
    float mLowGain = 1.0f, mHighGain = 1.0f;
    float mSmoothLow = 1.0f, mSmoothHigh = 1.0f, mToneAlpha = .004f;
};

} // namespace maremba
