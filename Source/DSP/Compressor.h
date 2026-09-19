#pragma once

#include "DspMath.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// Gentle bus "glue" compressor. The static curve is intentionally soft: 70% of
// the computed gain reduction is given back, so above the threshold the
// effective ratio is only about 1.2:1 (amount 0.25) to 1.33:1 (amount 1.0),
// i.e. it rounds off loud peaks rather than squashing the dynamics. There is
// no separate makeup gain. The factory patches are voiced with this curve.
class Compressor {
public:
    Compressor() {
        SetSampleRate(kReferenceRate);
        Reset();
    }

    void Reset() {
        mEnvelopeDb = kFloorDb;
        mGainReduction = 1.0f;
    }

    void SetSampleRate(double sampleRate) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
        // Gain smoothing voiced as 0.08 per sample at the reference rate
        mGainAlpha = static_cast<float>(SmootherAtRate(0.08, mSampleRate));
        UpdateBallistics();
    }

    // Attack/release in ms; coefficients are recomputed only when a value changes.
    void SetTimes(float attackMs, float releaseMs) {
        if (attackMs == mAttackMs && releaseMs == mReleaseMs) return;
        mAttackMs = attackMs;
        mReleaseMs = releaseMs;
        UpdateBallistics();
    }

    // Process stereo frame through compressor
    void Process(float inL, float inR, float amount, float& outL, float& outR)
    {
        if (amount <= 0.001f) {
            // Bypassed: relax the detector and the gain toward idle so that
            // re-enabling does not apply a stale gain reduction.
            mEnvelopeDb = kFloorDb + mReleaseAlpha * (mEnvelopeDb - kFloorDb);
            mGainReduction += mGainAlpha * (1.0f - mGainReduction);
            outL = inL;
            outR = inR;
            return;
        }

        // 1. Peak level detection in dB
        float maxIn = std::max(std::abs(inL), std::abs(inR));
        float inDb = (maxIn > 1.0e-5f) ? 20.0f * std::log10(maxIn) : -100.0f;

        if (inDb > mEnvelopeDb) {
            mEnvelopeDb = inDb + mAttackAlpha * (mEnvelopeDb - inDb);
        } else {
            mEnvelopeDb = inDb + mReleaseAlpha * (mEnvelopeDb - inDb);
        }

        // 2. Static Compression Characteristic
        // Threshold: -6 dB (amount 0) down to -30 dB (amount 1)
        float thresholdDb = -6.0f - (amount * 24.0f);
        // Nominal ratio 1:1 .. 6:1, softened below by the 70% give-back
        float ratio = 1.0f + (amount * 5.0f);
        float slope = 1.0f - (1.0f / ratio);

        float grDb = 0.0f;
        if (mEnvelopeDb > thresholdDb) {
            grDb = (mEnvelopeDb - thresholdDb) * slope;
        }

        // Give back 70% of the reduction: net gain is -0.3 x grDb (gentle glue,
        // effective ratio 1.2:1 .. 1.33:1; this is not a makeup gain)
        float giveBackDb = grDb * 0.70f;
        float netGainDb = giveBackDb - grDb;
        float targetLinearGain = std::pow(10.0f, netGainDb / 20.0f);

        // Smooth gain transition
        mGainReduction += mGainAlpha * (targetLinearGain - mGainReduction);

        outL = inL * mGainReduction;
        outR = inR * mGainReduction;
    }

private:
    static constexpr float kFloorDb = -96.0f;

    void UpdateBallistics() {
        mAttackAlpha = static_cast<float>(std::exp(-1000.0 / (std::max(0.5, static_cast<double>(mAttackMs)) * mSampleRate)));
        mReleaseAlpha = static_cast<float>(std::exp(-1000.0 / (std::max(10.0, static_cast<double>(mReleaseMs)) * mSampleRate)));
    }

    double mSampleRate = kReferenceRate;
    float mAttackMs = 10.0f;
    float mReleaseMs = 120.0f;
    float mAttackAlpha = 0.0f;
    float mReleaseAlpha = 0.0f;
    float mGainAlpha = 0.08f;
    float mEnvelopeDb = kFloorDb;
    float mGainReduction = 1.0f;
};

} // namespace maremba
