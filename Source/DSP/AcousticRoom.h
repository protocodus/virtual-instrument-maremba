#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace maremba {

/**
 * AcousticRoom:
 * Authentic acoustic wooden concert hall reverberation designed for wooden idiophones.
 * Models early reflection geometry and warm wooden wall boundary absorption with
 * smooth Schroeder-Moorer comb feedback and allpass diffusion.
 *
 * Fully zero-latency, allocated once at startup, with zero CPU overhead when bypassed.
 */
class AcousticRoom {
public:
    AcousticRoom() {
        SetSampleRate(44100.0f);
        Reset();
    }

    void SetSampleRate(float sampleRate) {
        if (sampleRate < 8000.0f) sampleRate = 44100.0f;
        mSampleRate = sampleRate;
        InitBuffers();
    }

    void Reset() {
        for (auto& cb : mCombsL) cb.Reset();
        for (auto& cb : mCombsR) cb.Reset();
        for (auto& ap : mAllpassL) ap.Reset();
        for (auto& ap : mAllpassR) ap.Reset();
        mEarlyStateL = mEarlyStateR = 0.0f;
    }

    // Process stereo frame in-place through wooden concert hall acoustics
    void Process(float inL, float inR, float roomLevel, float& outL, float& outR) {
        if (roomLevel <= 0.001f) {
            outL = inL;
            outR = inR;
            return;
        }

        // Sum to mono for diffuse hall excitation + stereo decorrelation
        float mono = (inL + inR) * 0.5f;

        // Comb bank with lowpass feedback (absorptive wooden walls)
        float combSumL = 0.0f;
        for (auto& cb : mCombsL) {
            combSumL += cb.Process(mono);
        }

        float combSumR = 0.0f;
        for (auto& cb : mCombsR) {
            combSumR += cb.Process(mono);
        }

        // Serial Allpass Diffusors (smear echoes into smooth, silky spatial reverberation)
        float diffL = combSumL;
        for (auto& ap : mAllpassL) {
            diffL = ap.Process(diffL);
        }

        float diffR = combSumR;
        for (auto& ap : mAllpassR) {
            diffR = ap.Process(diffR);
        }

        // High frequency air-damping filter on reverb tail
        mEarlyStateL += 0.40f * (diffL - mEarlyStateL);
        mEarlyStateR += 0.40f * (diffR - mEarlyStateR);

        // Mix: direct dry mic sound + lush wooden hall tail
        // Scaled with clean acoustic headroom
        float wetGain = roomLevel * 0.50f;
        outL = inL * 0.70f + mEarlyStateL * wetGain;
        outR = inR * 0.70f + mEarlyStateR * wetGain;
    }

private:
    struct CombFilter {
        std::vector<float> buffer;
        int writeIndex = 0;
        float feedback = 0.82f;
        float damp = 0.38f;
        float filterState = 0.0f;

        void Init(int size, float fb, float d) {
            buffer.assign(std::max(size, 4), 0.0f);
            writeIndex = 0;
            feedback = fb;
            damp = d;
            filterState = 0.0f;
        }

        void Reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
            filterState = 0.0f;
        }

        inline float Process(float input) {
            if (buffer.empty()) return 0.0f;
            float output = buffer[writeIndex];
            // One-pole lowpass feedback: wooden absorption of high overtones
            filterState = output * (1.0f - damp) + filterState * damp;
            buffer[writeIndex] = input + filterState * feedback;
            if (++writeIndex >= static_cast<int>(buffer.size())) writeIndex = 0;
            return output;
        }
    };

    struct AllPassFilter {
        std::vector<float> buffer;
        int writeIndex = 0;
        float feedback = 0.55f;

        void Init(int size, float fb = 0.55f) {
            buffer.assign(std::max(size, 4), 0.0f);
            writeIndex = 0;
            feedback = fb;
        }

        void Reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        inline float Process(float input) {
            if (buffer.empty()) return input;
            float bufOut = buffer[writeIndex];
            float output = -input + bufOut;
            buffer[writeIndex] = input + (bufOut * feedback);
            if (++writeIndex >= static_cast<int>(buffer.size())) writeIndex = 0;
            return output;
        }
    };

    void InitBuffers() {
        // Base delay times (in ms) mutually prime to avoid flutter resonances
        // Scaled to model a ~12m x 9m x 5m wooden chamber hall
        static constexpr float kTimesL[4] = { 28.3f, 34.7f, 41.9f, 49.1f };
        static constexpr float kTimesR[4] = { 30.7f, 37.1f, 44.3f, 52.3f };
        static constexpr float kApTimesL[2] = { 5.2f, 1.7f };
        static constexpr float kApTimesR[2] = { 5.8f, 2.1f };

        for (int i = 0; i < 4; ++i) {
            int lenL = static_cast<int>(kTimesL[i] * 0.001f * mSampleRate);
            mCombsL[i].Init(lenL, 0.83f, 0.38f);

            int lenR = static_cast<int>(kTimesR[i] * 0.001f * mSampleRate);
            mCombsR[i].Init(lenR, 0.83f, 0.38f);
        }

        for (int i = 0; i < 2; ++i) {
            int lenL = static_cast<int>(kApTimesL[i] * 0.001f * mSampleRate);
            mAllpassL[i].Init(lenL, 0.55f);

            int lenR = static_cast<int>(kApTimesR[i] * 0.001f * mSampleRate);
            mAllpassR[i].Init(lenR, 0.55f);
        }
    }

    float mSampleRate = 44100.0f;
    std::array<CombFilter, 4> mCombsL;
    std::array<CombFilter, 4> mCombsR;
    std::array<AllPassFilter, 2> mAllpassL;
    std::array<AllPassFilter, 2> mAllpassR;
    float mEarlyStateL = 0.0f;
    float mEarlyStateR = 0.0f;
};

} // namespace maremba
