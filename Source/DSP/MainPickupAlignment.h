#pragma once

#include "CaptureGeometry.h"
#include "Oversampler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace maremba {

// The electrical pickup reaches the mixer before sound can reach Close.
// Delay only its Main contribution by the source-to-head flight time. The
// direct electrical output retains its original circuit response and timing.
// This is neither a microphone filter nor a change to electrode polarity.
// Persistent key identities retain travelling samples when voices are stolen.
class MainPickupAlignment {
public:
    void Allocate(double maximumHostRate) {
        mMaximumRate = std::max(8000.0, maximumHostRate);
        mCapacity = static_cast<int>(std::ceil(.01 * mMaximumRate)) + 2 * kTaps;
        mFlight.assign(static_cast<std::size_t>(kCaptureSourceCount) * mCapacity, 0.0f);
        mRate = 0.0;
        SetSampleRate(maximumHostRate);
    }

    // Control-rate only; no allocation on rate/quality changes.
    void SetSampleRate(double hostRate) {
        if (mFlight.empty() || !std::isfinite(hostRate)) return;
        const double rate = std::clamp(hostRate, 8000.0, mMaximumRate);
        if (rate == mRate) return;
        mRate = rate;
        for (int id = 0; id < kCaptureSourceCount; ++id) {
            auto& source = mSources[static_cast<std::size_t>(id)];
            const double delay = rate * CaptureDistance(CaptureSourcePosition(id), CloseHeadCentre())
                              / kCaptureSpeedOfSound;
            source.integerDelay = static_cast<int>(std::floor(delay));
            const double fraction = delay - source.integerDelay;
            double sum = 0.0;
            for (int tap = 0; tap < kTaps; ++tap) {
                const double delta = tap - kCentre - fraction;
                const double sinc = std::abs(delta) < 1.0e-12 ? 1.0
                                  : std::sin(kPiD * delta) / (kPiD * delta);
                source.coefficient[tap] = static_cast<float>(sinc * (.5 + .5 * std::cos(kPiD * delta / (kTaps / 2.0))));
                sum += source.coefficient[tap];
            }
            for (float& coefficient : source.coefficient)
                coefficient = static_cast<float>(coefficient / sum);
            source.tailFrames = 256 + source.integerDelay + kTaps;
        }
        Reset();
    }

    void Reset() {
        std::fill(mFlight.begin(), mFlight.end(), 0.0f);
        for (auto& source : mSources) {
            source.decimator.Reset();
            source.buffer.fill(0.0f);
            source.remaining = 0;
            source.active = source.receiving = false;
        }
        mCount = mWrite = 0;
        mAdded = false;
    }

    void BeginFrame(int factor) {
        mFactor = factor;
        for (int n = 0; n < mCount; ++n) {
            auto& source = mSources[mActive[n]];
            source.buffer.fill(0.0f);
            source.receiving = false;
        }
    }

    void Add(int id, int subsample, float pickup) {
        if (pickup == 0.0f) return;
        auto& source = mSources[static_cast<std::size_t>(id)];
        if (!source.active) {
            source.active = true;
            source.decimator.Reset();
            source.buffer.fill(0.0f);
            std::fill(Flight(id), Flight(id) + mCapacity, 0.0f);
            mActive[mCount++] = id;
            mAdded = true;
        }
        source.receiving = true;
        source.buffer[static_cast<std::size_t>(subsample)] += pickup;
    }

    float Process() {
        if (mAdded) {
            std::sort(mActive.begin(), mActive.begin() + mCount);
            mAdded = false;
        }
        float output = 0.0f;
        int nextCount = 0;
        for (int n = 0; n < mCount; ++n) {
            const int id = mActive[n];
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (source.receiving) source.remaining = source.tailFrames;
            float* line = Flight(id);
            line[mWrite] = Flush(source.decimator.DownsampleFrame(source.buffer.data(), mFactor));
            int at = mWrite - source.integerDelay + kCentre;
            if (at < 0) at += mCapacity;
            if (at >= mCapacity) at -= mCapacity;
            float value = 0.0f;
            if (at >= kTaps - 1) {
                // No tap wraps (nearly every frame): the same products,
                // summed in the same order, without a wrap test per tap.
                const float* taps = line + at;
                for (int tap = 0; tap < kTaps; ++tap)
                    value += taps[-tap] * source.coefficient[tap];
            } else {
                for (int tap = 0; tap < kTaps; ++tap) {
                    value += line[at] * source.coefficient[tap];
                    if (--at < 0) at = mCapacity - 1;
                }
            }
            output += value;
            if (--source.remaining > 0) mActive[nextCount++] = id;
            else {
                source.active = false;
                source.decimator.Reset();
            }
        }
        mCount = nextCount;
        if (++mWrite == mCapacity) mWrite = 0;
        return Flush(output);
    }

    void FlushTiny() {
        for (int n = 0; n < mCount; ++n) mSources[mActive[n]].decimator.FlushTiny();
    }
    bool HasNoSubnormalState() const {
        for (int n = 0; n < mCount; ++n)
            if (!mSources[mActive[n]].decimator.HasNoSubnormalState()) return false;
        return true; // every persistent flight write is explicitly flushed
    }

private:
    // A longer interpolation window keeps the aligned pickup flat through
    // the mallet/body band; it consumes existing physical flight, not latency.
    static constexpr int kTaps = 16, kCentre = 7;
    struct Source {
        MonoOversampler decimator;
        std::array<float, 8> buffer{};
        std::array<float, kTaps> coefficient{};
        int integerDelay = 0, tailFrames = 0, remaining = 0;
        bool active = false, receiving = false;
    };
    static float Flush(float value) { return std::abs(value) < 1.0e-30f ? 0.0f : value; }
    float* Flight(int id) { return mFlight.data() + static_cast<std::size_t>(id) * mCapacity; }
    std::array<Source, kCaptureSourceCount> mSources;
    std::array<int, kCaptureSourceCount> mActive{};
    std::vector<float> mFlight;
    double mMaximumRate = 0.0, mRate = 0.0;
    int mCapacity = 0, mCount = 0, mWrite = 0, mFactor = 2;
    bool mAdded = false;
};

} // namespace maremba
