#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "OutputPeakKernel.h"

namespace maremba {

// Clean overload protection after decimation and Volume. Gain moves ahead of
// the transient instead of clipping its waveform. Below the detector ceiling
// each signal is an exact delayed copy. Main, Close, Room and Piezo have
// independent gain control; each stereo pair shares its gain. All seven
// channels receive the same delay, keeping their phase relationship.
class OutputHeadroom {
public:
    static constexpr float kCeiling = 0.94f;
    static constexpr int kMaxDelay = 384; // 2 ms at the highest native rate
    static constexpr int kCapacity = kMaxDelay + 1;

    static int LatencyFrames(double rate) {
        return std::clamp(static_cast<int>(std::ceil(rate * 0.002)), 1, kMaxDelay);
    }

    static int SmoothingFrames(double rate) {
        // Reserve the whole interpolation support, including its earliest
        // contributing sample, inside the existing audio lookahead.
        return std::max(1, LatencyFrames(rate) + 1 - (output_peak::kTaps - 1));
    }

    void SetSampleRate(double rate) {
        mWindow = LatencyFrames(rate) + 1;
        mGainWindow = SmoothingFrames(rate);
        mReleasePole = std::exp(-1.0 / (0.060 * rate));
        mHoldFrames = static_cast<int>(std::ceil(0.030 * rate));
        Reset();
    }

    void Reset() {
        for (auto& channel : mAudio) channel.fill(0.0f);
        for (auto& group : mGroups) {
            group.gainHistory.fill(1.0);
            group.gainSum = mGainWindow;
            group.control = 1.0;
            group.head = group.count = group.holdRemaining = 0;
        }
        mWrite = 0;
        mGainWrite = mFirWrite = 0;
        mFrame = 0;
        mPrevious1.fill(0.0);
        mPrevious2.fill(0.0);
        for (auto& channel : mFirHistory) channel.fill(0.0f);
        mFirHotFrames.fill(0);
    }

    void ResetDirect(int index) {
        const int channel = index + 2;
        mAudio[channel].fill(0.0f);
        mPrevious1[channel] = mPrevious2[channel] = 0.0;
        mFirHistory[channel].fill(0.0f);
        mFirHotFrames[channel] = 0;
        // Keep the group's gain history: its other stereo channel may still
        // contain a queued overload. Returning to unity here could expose it.
    }

    void Process(std::array<float, 7>& frame) {
        std::array<double, 4> peaks{};
        for (int channel = 0; channel < 7; ++channel) {
            double peak = std::abs(frame[channel]);
            const double a = mPrevious2[channel], b = mPrevious1[channel], c = frame[channel];
            if (std::abs(b) > 1.0e-12 && std::abs(b) >= std::abs(a) && std::abs(b) >= std::abs(c)) {
                // A sinusoid obeys a+c=2*b*cos(w). Its amplitude follows from
                // the centre sample and the quadrature difference c-a. Using
                // that estimate at a local peak prevents gain from following
                // the beat between a sustained high note and the sample grid.
                // Complex transients need not be sinusoidal: bound the estimate
                // and always retain the actual sample peak as the safety floor.
                const double cosw = (a + c) / (2.0 * b);
                const double denominator = 1.0 - cosw * cosw;
                if (denominator > 1.0e-12) {
                    const double amplitudeSquared = b*b + (c-a)*(c-a) / (4.0 * denominator);
                    peak = std::max(peak, std::sqrt(std::min(2.0*b*b, amplitudeSquared)));
                }
            }
            mPrevious2[channel] = b;
            mPrevious1[channel] = c;
            auto& history = mFirHistory[channel];
            history[mFirWrite] = history[mFirWrite + output_peak::kTaps] = frame[channel];
            // The L1 bound makes this skip exact: a history containing only
            // small samples cannot reconstruct above the detector ceiling.
            if (std::abs(c) > kCeiling / output_peak::kMaximumL1)
                mFirHotFrames[channel] = output_peak::kTaps;
            if (mFirHotFrames[channel] > 0) {
                --mFirHotFrames[channel];
                const float* samples = history.data() + mFirWrite + 1;
                for (const auto& phase : output_peak::kPhases) {
                    double interpolated = 0.0;
                    for (int tap = 0; tap < output_peak::kTaps; ++tap)
                        interpolated += samples[tap] * phase[tap];
                    peak = std::max(peak, std::abs(interpolated));
                }
            }
            peaks[channel / 2] = std::max(peaks[channel / 2], peak);
        }
        std::array<float, 4> gains{};
        for (int group = 0; group < 4; ++group)
            gains[group] = UpdateGain(mGroups[group], peaks[group]);

        const int read = (mWrite + 1) % mWindow;
        for (int channel = 0; channel < 7; ++channel) {
            mAudio[channel][mWrite] = frame[channel];
            frame[channel] = mAudio[channel][read] * gains[channel / 2];
        }
        mWrite = read;
        mGainWrite = (mGainWrite + 1) % mGainWindow;
        mFirWrite = (mFirWrite + 1) % output_peak::kTaps;
        ++mFrame;
    }

private:
    struct GainState {
        std::array<double, kCapacity> gainHistory{}, minimum{};
        std::array<std::uint64_t, kCapacity> time{};
        int head = 0, count = 0, holdRemaining = 0;
        double control = 1.0, gainSum = 97.0;
    };

    float UpdateGain(GainState& state, double peak) {
        const double required = peak > kCeiling ? static_cast<double>(kCeiling) / peak : 1.0;

        // Sliding minimum, amortized O(1), using preallocated circular storage.
        // Keep each requirement for the entire lookahead/averaging window.
        while (state.count && mFrame - state.time[state.head] >= static_cast<std::uint64_t>(mWindow)) {
            state.head = (state.head + 1) % kCapacity;
            --state.count;
        }
        while (state.count) {
            const int last = (state.head + state.count - 1) % kCapacity;
            if (state.minimum[last] < required) break;
            --state.count;
        }
        const int tail = (state.head + state.count) % kCapacity;
        state.minimum[tail] = required;
        state.time[tail] = mFrame;
        ++state.count;
        const double held = state.minimum[state.head];
        // Hold through at least one 20 Hz half-cycle. Without this, recovery
        // between successive bass peaks modulates the waveform, even though
        // the ceiling is met. This hold adds no audio latency.
        if (held <= state.control) {
            state.control = held;
            state.holdRemaining = mHoldFrames;
        } else if (held < 1.0 && held <= state.control * 1.001) {
            // A periodic peak rarely lands on the same sample phase twice.
            // Renew the hold for essentially equal peaks without raising the
            // gain to their slightly weaker sampled maximum (0.009 dB band).
            state.holdRemaining = mHoldFrames;
        } else if (state.holdRemaining > 0) {
            --state.holdRemaining;
        } else {
            state.control = held + mReleasePole * (state.control - held);
        }
        if (1.0 - state.control < 1.0e-12) state.control = 1.0;

        // Every term in this mean sees the actual sample requirement before
        // its D-frame delayed output. The shorter mean also lets delayed FIR
        // requirements arrive before the earliest sample in their support.
        // The minimum remains held for the full D+1 audio window.
        state.gainSum += state.control - state.gainHistory[mGainWrite];
        state.gainHistory[mGainWrite] = state.control;
        return static_cast<float>(std::clamp(state.gainSum / mGainWindow, 0.0, 1.0));
    }

    std::array<std::array<float, kCapacity>, 7> mAudio{};
    std::array<double, 7> mPrevious1{}, mPrevious2{};
    std::array<std::array<float, output_peak::kTaps * 2>, 7> mFirHistory{};
    std::array<int, 7> mFirHotFrames{};
    std::array<GainState, 4> mGroups{};
    int mWindow = 97, mWrite = 0;
    int mGainWindow = 97 - (output_peak::kTaps - 1), mGainWrite = 0, mFirWrite = 0;
    int mHoldFrames = 1440;
    std::uint64_t mFrame = 0;
    double mReleasePole = 0.99965;
};

} // namespace maremba
