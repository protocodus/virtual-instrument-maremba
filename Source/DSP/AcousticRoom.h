#pragma once

#include "DspMath.h"
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
 * Zero latency. The delay lines are allocated ONCE (Allocate, outside the realtime
 * thread) for the highest rate the instance can run at; SetSampleRate only changes
 * the active lengths and clears them, it never allocates. Delay times are in ms
 * and the damping filters are derived from their voicing at the reference rate,
 * so the room sounds the same at every internal rate.
 */
class AcousticRoom {
public:
    AcousticRoom() = default;

    // Not realtime safe: reserve delay memory for rates up to maxSampleRate.
    void Allocate(double maxSampleRate) {
        const double rate = std::max(maxSampleRate, 8000.0);
        for (int i = 0; i < 4; ++i) {
            mCombsL[i].buffer.assign(static_cast<size_t>(kTimesL[i] * 0.001 * rate) + 4, 0.0f);
            mCombsR[i].buffer.assign(static_cast<size_t>(kTimesR[i] * 0.001 * rate) + 4, 0.0f);
        }
        for (int i = 0; i < 2; ++i) {
            mAllpassL[i].buffer.assign(static_cast<size_t>(kApTimesL[i] * 0.001 * rate) + 4, 0.0f);
            mAllpassR[i].buffer.assign(static_cast<size_t>(kApTimesR[i] * 0.001 * rate) + 4, 0.0f);
        }
        SetSampleRate(mSampleRate);
    }

    // Realtime safe (no allocation): set the active delay lengths and clear them.
    void SetSampleRate(double sampleRate) {
        if (!(sampleRate >= 8000.0)) sampleRate = 44100.0;
        mSampleRate = sampleRate;

        // Wall absorption (comb feedback low-pass) and the air-damping filter on
        // the tail, voiced as 0.38 / 0.40 per sample at the reference rate.
        const float combDamp = static_cast<float>(PoleAtRate(0.38, sampleRate));
        mAirAlpha = static_cast<float>(SmootherAtRate(0.40, sampleRate));

        for (int i = 0; i < 4; ++i) {
            mCombsL[i].Init(static_cast<int>(kTimesL[i] * 0.001 * sampleRate), 0.83f, combDamp);
            mCombsR[i].Init(static_cast<int>(kTimesR[i] * 0.001 * sampleRate), 0.83f, combDamp);
        }
        for (int i = 0; i < 2; ++i) {
            mAllpassL[i].Init(static_cast<int>(kApTimesL[i] * 0.001 * sampleRate), 0.55f);
            mAllpassR[i].Init(static_cast<int>(kApTimesR[i] * 0.001 * sampleRate), 0.55f);
        }
        mEarlyStateL = mEarlyStateR = 0.0f;
    }

    // Clear the active part of every delay line (realtime safe).
    void Reset() {
        for (auto& cb : mCombsL) cb.Reset();
        for (auto& cb : mCombsR) cb.Reset();
        for (auto& ap : mAllpassL) ap.Reset();
        for (auto& ap : mAllpassR) ap.Reset();
        mEarlyStateL = mEarlyStateR = 0.0f;
    }

    // Process stereo frame through wooden concert hall acoustics. The dry mic
    // signal and the decorrelated hall return come back separately (the far mic
    // tap is their sum; the main bus widens only the dry part fully).
    void Process(float inL, float inR, float roomLevel,
                 float& dryL, float& dryR, float& wetL, float& wetR) {
        // Dry path is always 0.7x, so the level is continuous when the room
        // switches off at roomLevel 0 (the wet part fades to 0 with the level).
        dryL = inL * kDryGain;
        dryR = inR * kDryGain;
        if (roomLevel <= 0.001f || !IsAllocated()) {
            wetL = 0.0f;
            wetR = 0.0f;
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
        mEarlyStateL += mAirAlpha * (diffL - mEarlyStateL);
        mEarlyStateR += mAirAlpha * (diffR - mEarlyStateR);

        // Lush wooden hall tail, scaled with clean acoustic headroom
        float wetGain = roomLevel * 0.50f;
        wetL = mEarlyStateL * wetGain;
        wetR = mEarlyStateR * wetGain;
    }

private:
    static constexpr float kDryGain = 0.70f;
    static constexpr float kTimesL[4] = { 28.3f, 34.7f, 41.9f, 49.1f };
    static constexpr float kTimesR[4] = { 30.7f, 37.1f, 44.3f, 52.3f };
    static constexpr float kApTimesL[2] = { 5.2f, 1.7f };
    static constexpr float kApTimesR[2] = { 5.8f, 2.1f };

    struct CombFilter {
        std::vector<float> buffer; // capacity fixed by Allocate
        int length = 0;
        int writeIndex = 0;
        float feedback = 0.82f;
        float damp = 0.38f;
        float filterState = 0.0f;

        void Init(int size, float fb, float d) {
            length = std::clamp(size, 4, std::max(4, static_cast<int>(buffer.size())));
            if (buffer.empty()) length = 0;
            feedback = fb;
            damp = d;
            Reset();
        }

        void Reset() {
            std::fill(buffer.begin(), buffer.begin() + length, 0.0f);
            writeIndex = 0;
            filterState = 0.0f;
        }

        inline float Process(float input) {
            float output = buffer[writeIndex];
            // One-pole lowpass feedback: wooden absorption of high overtones
            filterState = output * (1.0f - damp) + filterState * damp;
            buffer[writeIndex] = input + filterState * feedback;
            if (++writeIndex >= length) writeIndex = 0;
            return output;
        }
    };

    struct AllPassFilter {
        std::vector<float> buffer; // capacity fixed by Allocate
        int length = 0;
        int writeIndex = 0;
        float feedback = 0.55f;

        void Init(int size, float fb = 0.55f) {
            length = std::clamp(size, 4, std::max(4, static_cast<int>(buffer.size())));
            if (buffer.empty()) length = 0;
            feedback = fb;
            Reset();
        }

        void Reset() {
            std::fill(buffer.begin(), buffer.begin() + length, 0.0f);
            writeIndex = 0;
        }

        inline float Process(float input) {
            float bufOut = buffer[writeIndex];
            float output = -input + bufOut;
            buffer[writeIndex] = input + (bufOut * feedback);
            if (++writeIndex >= length) writeIndex = 0;
            return output;
        }
    };

    bool IsAllocated() const { return mCombsL[0].length > 0; }

    double mSampleRate = 44100.0;
    float mAirAlpha = 0.40f;
    std::array<CombFilter, 4> mCombsL;
    std::array<CombFilter, 4> mCombsR;
    std::array<AllPassFilter, 2> mAllpassL;
    std::array<AllPassFilter, 2> mAllpassR;
    float mEarlyStateL = 0.0f;
    float mEarlyStateR = 0.0f;
};

} // namespace maremba
