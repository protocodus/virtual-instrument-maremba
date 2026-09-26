#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace maremba {

// Clean overload protection after decimation and Volume. Gain moves ahead of
// the transient instead of clipping its waveform. Below the detector ceiling
// the main signal is an exact delayed copy. The five pre-fader stems receive only the
// same delay, keeping their phase relationship and independent gain staging.
class OutputHeadroom {
public:
    static constexpr float kCeiling = 0.94f;
    static constexpr int kMaxDelay = 384; // 2 ms at the highest native rate
    static constexpr int kCapacity = kMaxDelay + 1;

    static int LatencyFrames(double rate) {
        return std::clamp(static_cast<int>(std::ceil(rate * 0.002)), 1, kMaxDelay);
    }

    void SetSampleRate(double rate) {
        mWindow = LatencyFrames(rate) + 1;
        mReleasePole = std::exp(-1.0 / (0.060 * rate));
        mHoldFrames = static_cast<int>(std::ceil(0.030 * rate));
        Reset();
    }

    void Reset() {
        for (auto& channel : mAudio) channel.fill(0.0f);
        mGainHistory.fill(1.0);
        mGainSum = mWindow;
        mControl = 1.0;
        mWrite = mHead = mCount = 0;
        mHoldRemaining = 0;
        mFrame = 0;
        mPrevious1.fill(0.0);
        mPrevious2.fill(0.0);
    }

    void ResetDirect(int index) { mAudio[index + 2].fill(0.0f); }

    void Process(std::array<float, 7>& frame) {
        double peak = std::max(std::abs(frame[0]), std::abs(frame[1]));
        for (int channel = 0; channel < 2; ++channel) {
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
        }
        const double required = peak > kCeiling ? static_cast<double>(kCeiling) / peak : 1.0;

        // Sliding minimum, amortized O(1), using preallocated circular storage.
        // Keep each requirement for the entire lookahead/averaging window.
        while (mCount && mFrame - mTime[mHead] >= static_cast<std::uint64_t>(mWindow)) {
            mHead = (mHead + 1) % kCapacity;
            --mCount;
        }
        while (mCount) {
            const int last = (mHead + mCount - 1) % kCapacity;
            if (mMinimum[last] < required) break;
            --mCount;
        }
        const int tail = (mHead + mCount) % kCapacity;
        mMinimum[tail] = required;
        mTime[tail] = mFrame++;
        ++mCount;
        const double held = mMinimum[mHead];
        // Hold through at least one 20 Hz half-cycle. Without this, recovery
        // between successive bass peaks modulates the waveform, even though
        // the ceiling is met. This hold adds no audio latency.
        if (held <= mControl) {
            mControl = held;
            mHoldRemaining = mHoldFrames;
        } else if (held < 1.0 && held <= mControl * 1.001) {
            // A periodic peak rarely lands on the same sample phase twice.
            // Renew the hold for essentially equal peaks without raising the
            // gain to their slightly weaker sampled maximum (0.009 dB band).
            mHoldRemaining = mHoldFrames;
        } else if (mHoldRemaining > 0) {
            --mHoldRemaining;
        } else {
            mControl = held + mReleasePole * (mControl - held);
        }
        if (1.0 - mControl < 1.0e-12) mControl = 1.0;

        // For a sample delayed D frames, every term in this D+1-point mean
        // has seen (and held) that sample's requirement. Thus the smooth gain
        // cannot exceed it, even on an arbitrarily fast rise. No sample clip
        // or nonlinear audio transfer is needed to repair attack overshoot.
        mGainSum += mControl - mGainHistory[mWrite];
        mGainHistory[mWrite] = mControl;
        const float gain = static_cast<float>(std::clamp(mGainSum / mWindow, 0.0, 1.0));
        const int read = (mWrite + 1) % mWindow;
        for (int c = 0; c < 7; ++c) {
            mAudio[c][mWrite] = frame[c];
            frame[c] = mAudio[c][read];
            if (c < 2) frame[c] *= gain;
        }
        mWrite = read;
    }

private:
    std::array<std::array<float, kCapacity>, 7> mAudio{};
    std::array<double, 2> mPrevious1{}, mPrevious2{};
    std::array<double, kCapacity> mGainHistory{}, mMinimum{};
    std::array<std::uint64_t, kCapacity> mTime{};
    int mWindow = 97, mWrite = 0, mHead = 0, mCount = 0;
    int mHoldFrames = 1440, mHoldRemaining = 0;
    std::uint64_t mFrame = 0;
    double mControl = 1.0, mGainSum = 97.0, mReleasePole = 0.99965;
};

} // namespace maremba
