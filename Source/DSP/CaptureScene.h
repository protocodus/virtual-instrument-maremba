#pragma once

#include "CaptureGeometry.h"
#include "CloseCapture.h"
#include "RoomCapture.h"
#include "Oversampler.h"
#include <array>
#include <algorithm>

namespace maremba {

// Mechanics run oversampled. Linear microphone propagation runs at the host
// rate, after proper anti-alias decimation of each physical emitter. Slots
// belong to keys, not voice allocations, so stealing a voice cannot erase a
// wave already travelling to a microphone.
class CaptureScene {
public:
    static constexpr float kMasterMixScale = .65f;
    void Allocate(double hostRate) {
        mClose.Allocate(hostRate, kCaptureSourceCount);
        mRoom.Allocate(hostRate, kCaptureSourceCount);
        mRate = 0.0;
        SetSampleRate(hostRate);
        for (int id = 0; id < kCaptureSourceCount; ++id) {
            const auto position = CaptureSourcePosition(id);
            mClose.AddSource(id, position, kCloseHrtfSourceProfile[id]);
            mRoom.AddSource(id, position);
        }
    }
    void SetSampleRate(double hostRate) {
        if (hostRate == mRate) return;
        mRate = hostRate;
        mClose.SetSampleRate(hostRate);
        mRoom.SetSampleRate(hostRate);
        Reset();
    }
    void SetRoomType(int type) { mRoom.SetType(type); }
    void Reset(bool resetRoomMotion = true) {
        mClose.Reset(); mRoom.Reset(resetRoomMotion);
        for (auto& source : mSource) {
            source.equalEars = true;
            for (auto& decimator : source.decimator) decimator.Reset();
            source.active = source.receiving = false;
            source.tail = 0;
        }
        for (auto& input : mInput) input.fill(0.0f);
        mCount = 0;
        mSourcesAdded = false;
        mInvalidInput = false;
    }
    void BeginFrame(int factor) {
        mFactor = factor;
        mInvalidInput = false;
        for (int n = 0; n < mCount; ++n) {
            auto& source = mSource[mActive[n]];
            for (auto& lane : source.buffer) lane.fill(0.0f);
            source.receiving = false;
        }
    }
    void Add(int id, int subsample, float left, float right, float room) {
        // Capture modules defensively sanitize their public inputs. Do not let
        // that conceal a failed physical emitter from the engine's fail-safe.
        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(room)) {
            mInvalidInput = true;
            return;
        }
        if (left == 0.0f && right == 0.0f && room == 0.0f) return;
        auto& source = mSource[id];
        if (!source.active) {
            source.active = true;
            mActive[mCount++] = id;
            mSourcesAdded = true;
            for (auto& lane : source.buffer) lane.fill(0.0f);
        }
        source.receiving = true;
        if (source.equalEars && left != right) {
            // Until this contribution both ear inputs and their entire
            // decimator history were identical. Materialize the right state
            // before processing the first asymmetric frame.
            source.decimator[1] = source.decimator[0];
            source.buffer[1] = source.buffer[0];
            source.equalEars = false;
        }
        source.buffer[0][subsample] += left;
        if (!source.equalEars) source.buffer[1][subsample] += right;
        source.buffer[2][subsample] += room;
        mInvalidInput = mInvalidInput || !std::isfinite(source.buffer[0][subsample])
            || (!source.equalEars && !std::isfinite(source.buffer[1][subsample]))
            || !std::isfinite(source.buffer[2][subsample]);
    }
    bool HasInvalidInput() const { return mInvalidInput; }
    // Compact sources declare pressure at the Close reference distance. Each
    // ear receives that same emitted pressure before its own propagation/HRTF.
    // Room expects pressure at its own reference distance, so inverse-distance
    // propagation requires this conversion before the Room path gain applies.
    void AddCompactPressure(int id, int subsample, float pressureAtCloseReference) {
        constexpr float roomReferenceScale = static_cast<float>(CloseCapture::kReferenceDistance
                                                               / RoomCapture::kReferenceDistance);
        Add(id, subsample, pressureAtCloseReference, pressureAtCloseReference,
            pressureAtCloseReference * roomReferenceScale);
    }
    void Process(float& closeL, float& closeR, float& roomL, float& roomR) {
        // The capture renderers need only discover nonzero emitters, not scan
        // all 515 persistent slots. Sorting new IDs preserves their previous
        // ascending discovery order and therefore the exact FFT sum order.
        if (mSourcesAdded) {
            std::sort(mActive.begin(), mActive.begin() + mCount);
            mSourcesAdded = false;
        }
        if (mFactor == 2) DownsampleSources<2>();
        else if (mFactor == 4) DownsampleSources<4>();
        else DownsampleSources<8>();
        mClose.Process(mInput[0].data(), mInput[1].data(), kCaptureSourceCount, closeL, closeR,
                       mActive.data(), mCount);
        mRoom.Process(mInput[2].data(), kCaptureSourceCount, roomL, roomR,
                      mActive.data(), mCount);
    }
    static void Mix(float closeL, float closeR, float roomL, float roomR, float piezo,
                    float closeGain, float roomGain, float piezoGain, float width,
                    float& mainL, float& mainR) {
        const float dryL = closeL*closeGain + piezo*.707f*piezoGain;
        const float dryR = closeR*closeGain + piezo*.707f*piezoGain;
        const float mid = .5f*(dryL+dryR+(roomL+roomR)*roomGain);
        const float side = .5f*((dryR-dryL)*std::clamp(width,0.0f,2.0f)
                            +(roomR-roomL)*roomGain*std::clamp(width,0.0f,1.0f));
        mainL = kMasterMixScale*(mid-side);
        mainR = kMasterMixScale*(mid+side);
    }
    void FlushTiny() {
        for (int n = 0; n < mCount; ++n)
            for (auto& decimator : mSource[mActive[n]].decimator) decimator.FlushTiny();
        mClose.FlushTiny(); mRoom.FlushTiny();
    }
    bool HasNoSubnormalState() const {
        for (const auto& source : mSource)
            for (const auto& decimator : source.decimator)
                if (!decimator.HasNoSubnormalState()) return false;
        return mClose.HasNoSubnormalState() && mRoom.HasNoSubnormalState();
    }
private:
    friend struct CaptureSceneTestAccess;
    template<int Factor>
    void DownsampleSources() {
        int nextCount = 0;
        for (int n = 0; n < mCount; ++n) {
            const int id = mActive[n];
            auto& source = mSource[id];
            if (source.receiving) source.tail = 256;
            mInput[0][id] = source.decimator[0].DownsampleFrame<Factor>(source.buffer[0].data());
            mInput[1][id] = source.equalEars ? mInput[0][id]
                : source.decimator[1].DownsampleFrame<Factor>(source.buffer[1].data());
            mInput[2][id] = source.decimator[2].DownsampleFrame<Factor>(source.buffer[2].data());
            // 256 silent host frames take the slowest allpass state far below
            // float audio precision. Acoustic delay and room tails live in
            // the capture modules and are not stopped with this emitter.
            if (--source.tail <= 0) {
                source.active = false;
                source.equalEars = true;
                for (int lane = 0; lane < 3; ++lane) {
                    source.decimator[lane].Reset();
                    mInput[lane][id] = 0.0f;
                }
            } else mActive[nextCount++] = id;
        }
        mCount = nextCount;
    }
    struct Source {
        std::array<std::array<float,8>,3> buffer{};
        std::array<MonoOversampler,3> decimator;
        bool active = false, receiving = false;
        bool equalEars = true;
        int tail = 0;
    };
    std::array<Source,kCaptureSourceCount> mSource;
    std::array<int,kCaptureSourceCount> mActive{};
    std::array<std::array<float,kCaptureSourceCount>,3> mInput{};
    int mCount = 0, mFactor = 2;
    bool mInvalidInput = false, mSourcesAdded = false;
    double mRate = 0.0;
    CloseCapture mClose;
    RoomCapture mRoom;
};

} // namespace maremba
