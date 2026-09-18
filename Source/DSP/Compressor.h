#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

class Compressor {
public:
    Compressor() {
        Reset();
    }

    void Reset() {
        mEnvelopeDb = -96.0f;
        mGainReduction = 1.0f;
    }

    void SetSampleRate(float sampleRate) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 44100.0f;
    }

    // Process stereo frame through compressor
    void Process(float inL, float inR,
                 float amount, float attackMs, float releaseMs,
                 float& outL, float& outR)
    {
        if (amount <= 0.001f) {
            outL = inL;
            outR = inR;
            return;
        }

        // 1. Peak level detection in dB
        float maxIn = std::max(std::abs(inL), std::abs(inR));
        float inDb = (maxIn > 1.0e-5f) ? 20.0f * std::log10(maxIn) : -100.0f;

        // Ballistics time constants
        float attackAlpha = std::exp(-1000.0f / (std::max(0.5f, attackMs) * mSampleRate));
        float releaseAlpha = std::exp(-1000.0f / (std::max(10.0f, releaseMs) * mSampleRate));

        if (inDb > mEnvelopeDb) {
            mEnvelopeDb = inDb + attackAlpha * (mEnvelopeDb - inDb);
        } else {
            mEnvelopeDb = inDb + releaseAlpha * (mEnvelopeDb - inDb);
        }

        // 2. Static Compression Characteristic
        // Threshold: -30 dB at amount=1, -6 dB at amount=0.1
        float thresholdDb = -6.0f - (amount * 24.0f);
        // Ratio: 1:1 up to 6:1
        float ratio = 1.0f + (amount * 5.0f);
        float slope = 1.0f - (1.0f / ratio);

        float grDb = 0.0f;
        if (mEnvelopeDb > thresholdDb) {
            grDb = (mEnvelopeDb - thresholdDb) * slope;
        }

        // Transparent makeup gain compensating for gain reduction without boosting peaks
        float makeupDb = grDb * 0.70f;
        float netGainDb = makeupDb - grDb;
        float targetLinearGain = std::pow(10.0f, netGainDb / 20.0f);

        // Smooth gain transition
        mGainReduction += 0.08f * (targetLinearGain - mGainReduction);

        outL = inL * mGainReduction;
        outR = inR * mGainReduction;
    }

private:
    float mSampleRate = 44100.0f;
    float mEnvelopeDb = -96.0f;
    float mGainReduction = 1.0f;
};

} // namespace maremba
