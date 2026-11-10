#pragma once

#include "DspMath.h"
#include "CaptureGeometry.h"
#include "CloseHrtf.h"
#include "SimdLanes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace maremba {

// Two receiver-specific acoustic pressure inputs per source. Modal surface
// projection is upstream; the synthesized head/ear filters (CloseHrtf.h, a
// rigid sphere solved at each source's range), range gain and common
// propagation are applied once here.
//
// Uniform partitioned convolution sums source spectra before inverse FFTs in
// staggered groups. Its processing delay is deducted from the propagation line:
// it is not another hidden device latency. All storage is reserved by Allocate.
//
// Lanes selects the four-float kernels of SimdLanes.h for the butterflies, the
// fractional delays and the spectrum sums; without it the same loops run one
// float at a time, which is the reference Maremba.SimdKernel compares with.
template<bool Lanes>
class CloseCaptureKernel {
public:
    static constexpr double kReferenceDistance = 0.6;
    static constexpr double kMaximumFlightSeconds = 0.01;
    static constexpr int kMaximumBlock = 256;
    static constexpr int kMaximumFft = 2 * kMaximumBlock;
    static constexpr int kMaximumGroups = kMaximumBlock / 32;

    void Allocate(double maxHostRate, int maxSources = 520) {
        mMaximumRate = std::max(8000.0, maxHostRate);
        mMaximumSources = std::max(1, maxSources);
        mFlightLimit = static_cast<int>(std::ceil(kMaximumFlightSeconds * mMaximumRate)) + 16;
        // One extra block of line keeps a whole partition's input readable
        // when RenderPartition runs the fractional delays for it at once.
        mDelayCapacity = mFlightLimit + kMaximumBlock;
        mKernelCapacity = static_cast<int>(std::ceil((kCloseHrtfTaps + 16.0) * mMaximumRate
                                                     / kCloseHrtfReferenceRate)) + 32;
        // Partitions have twice as many FFT bins as time samples; permit the
        // final partial block, including the tiny-block low-rate fallback.
        mSpectrumCapacity = 2 * (mKernelCapacity + kMaximumBlock);
        mSources.resize(static_cast<std::size_t>(mMaximumSources));
        mActiveSources.resize(static_cast<std::size_t>(mMaximumSources));
        mFlight.assign(static_cast<std::size_t>(mMaximumSources) * 2 * mDelayCapacity, 0.0f);
        mHistory.assign(static_cast<std::size_t>(mMaximumSources) * 2 * 2 * mSpectrumCapacity, 0.0f);
        mFilters.assign(static_cast<std::size_t>(kCloseHrtfProfileCount) * 2 * 2 * mSpectrumCapacity, 0.0f);
        mKernelScratch.resize(static_cast<std::size_t>(mKernelCapacity));
        mRate = 0.0;
        Reset();
    }

    // No allocation. The engine calls this at setup/native rate changes;
    // oversampling changes do not change the Close convolution's native rate.
    void SetSampleRate(double hostRate) {
        if (mSources.empty()) return;
        if (!FastIsFinite(hostRate)) return;
        const double rate = std::clamp(hostRate, 8000.0, mMaximumRate);
        if (rate == mRate) return;
        mRate = rate;

        const double ratio = rate / kCloseHrtfReferenceRate;
        mResampleCutoff = std::min(1.0, ratio);
        mResampleSupport = 8.0 / mResampleCutoff;
        mResampleDelayFrames = mResampleSupport * ratio;
        // Consume only latency already required by the nearest physical ear
        // arrival. This raises FFT efficiency at high rates without moving
        // the acoustic impulse later, or assuming extra host/device latency.
        double minimumFlight = kMaximumFlightSeconds;
        for (int id = 0; id < kCaptureSourceCount; ++id) {
            const double flight = CaptureDistance(CaptureSourcePosition(id), CloseHeadCentre())
                                / kCaptureSpeedOfSound;
            const double itd = kCloseHrtfProfiles[kCloseHrtfSourceProfile[id]].residualItdSeconds;
            minimumFlight = std::min(minimumFlight, flight - 0.5*FastAbs(itd));
        }
        const double available = minimumFlight*rate - mResampleDelayFrames - 3.0;
        mBlockSize = 1;
        while (mBlockSize < kMaximumBlock && 2*mBlockSize <= available) mBlockSize *= 2;
        mFftSize = 2*mBlockSize;
        // A power of two, as the block is: a source's group is id & (count-1),
        // with no division per source and frame.
        mGroupCount = std::max(1, mBlockSize/32);
        mKernelLength = static_cast<int>(std::ceil((kCloseHrtfTaps - 1.0
                            + 2.0 * mResampleSupport) * ratio)) + 1;
        mPartitions = (mKernelLength + mBlockSize - 1) / mBlockSize;
        for (int i = 0; i < mFftSize / 2; ++i) {
            const double angle = -2.0 * kPi * i / mFftSize;
            mTwiddle[static_cast<std::size_t>(i)] = {static_cast<float>(std::cos(angle)),
                                                    static_cast<float>(std::sin(angle))};
        }
        // The same twiddles, laid out contiguously per butterfly stage for the
        // split-lane forward transform: stage `half` reads entry half-1+j.
        for (int half = 1; half < mFftSize; half *= 2)
            for (int j = 0; j < half; ++j) {
                const Complex twiddle = mTwiddle[static_cast<std::size_t>(j*(mFftSize/2/half))];
                mStageTwiddle.re[static_cast<std::size_t>(half-1+j)] = twiddle.real();
                mStageTwiddle.im[static_cast<std::size_t>(half-1+j)] = twiddle.imag();
                mInverseTwiddleIm[static_cast<std::size_t>(half-1+j)] = -twiddle.imag();
            }
        int bits = 0;
        for (int n = mFftSize; n > 1; n >>= 1) ++bits;
        for (int i = 0; i < mFftSize; ++i) {
            unsigned n = static_cast<unsigned>(i), reversed = 0;
            for (int b = 0; b < bits; ++b) { reversed = (reversed << 1) | (n & 1u); n >>= 1; }
            mReverse[static_cast<std::size_t>(i)] = static_cast<int>(reversed);
        }
        BuildFilters();
        for (auto& source : mSources) if (source.configured) source.configured = ConfigureFlight(source);
        Reset();
    }

    // Scene setup, before rendering (or followed by Reset): moving an active
    // receiver is not supported by this fixed-scene renderer.
    bool AddSource(int id, CapturePoint position, int profileIndex) {
        if (id < 0 || id >= mMaximumSources || mRate <= 0.0 || profileIndex < 0
            || profileIndex >= kCloseHrtfProfileCount || !FastIsFinite(position.x)
            || !FastIsFinite(position.y) || !FastIsFinite(position.z)) return false;
        auto& source = mSources[static_cast<std::size_t>(id)];
        source.position = position;
        source.profile = profileIndex;
        source.configured = ConfigureFlight(source);
        ClearSource(id);
        return source.configured;
    }

    // A silent source's line, history and partition blocks are never read,
    // and Process clears them (ClearSource) before the source sounds again.
    // Reset therefore only retires every source: sweeping all 515 sources'
    // buffers (megabytes) took longer than a whole Reason batch each time the
    // instrument fell idle.
    void Reset() {
        for (auto& source : mSources) {
            source.remaining = 0;
            source.active = false;
        }
        mOutput = {};
        mActiveCount = 0;
        mFlightPosition = 0;
        mHistoryPosition.fill(0);
        for (int group = 0; group < mGroupCount; ++group)
            mBlockPosition[group] = group*mBlockSize/mGroupCount;
    }

    // Each input index has a persistent source identity, even while its signal
    // is zero. Tails continue when count shrinks or a source is currently mute.
    // Optional sorted IDs identify all possibly nonzero input slots. This only
    // narrows discovery; every already-active propagation/convolution tail is
    // still processed. Omitting them preserves the dense public-input path.
    void Process(const float* leftInputs, const float* rightInputs, int count,
                 float& left, float& right, const int* sourceIds = nullptr, int sourceCount = 0) {
        left = right = 0.0f;
        if (mRate <= 0.0 || mSources.empty()) return;
        count = std::clamp(count, 0, mMaximumSources);
        for (int group = 0; group < mGroupCount; ++group) {
            left += mOutput[group][0][static_cast<std::size_t>(mBlockPosition[group])];
            right += mOutput[group][1][static_cast<std::size_t>(mBlockPosition[group])];
        }
        // Inputs are contiguous, but each source owns a large FFT history.
        // Avoid touching every dormant history/metadata page at every frame.
        const int candidates = sourceIds ? std::clamp(sourceCount, 0, count) : count;
        for (int candidate = 0; candidate < candidates; ++candidate) {
            const int id = sourceIds ? sourceIds[candidate] : candidate;
            if (id < 0 || id >= count) continue;
            const float rawL = leftInputs ? leftInputs[id] : 0.0f;
            const float rawR = rightInputs ? rightInputs[id] : 0.0f;
            if (rawL == 0.0f && rawR == 0.0f) continue;
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (!source.configured) continue;
            if (!FastIsFinite(rawL) || !FastIsFinite(rawR)) {
                Reset(); left = right = 0.0f; return;
            }
            if (Flush(rawL) == 0.0f && Flush(rawR) == 0.0f) continue;
            if (source.remaining == 0) ClearSource(id);
            if (!source.active) {
                source.active = true;
                mActiveSources[static_cast<std::size_t>(mActiveCount++)] = id;
            }
            source.remaining = source.tailFrames;
        }
        int nextCount = 0;
        for (int active = 0; active < mActiveCount; ++active) {
            const int id = mActiveSources[static_cast<std::size_t>(active)];
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (!source.configured || source.remaining == 0) { source.active = false; continue; }
            const int position = mBlockPosition[id & (mGroupCount-1)];
            // The fractional delay into `current` runs once per partition,
            // in RenderPartition, the only reader of `current`.
            Flight(id, 0)[mFlightPosition] = Flush(id < count && leftInputs ? leftInputs[id] : 0.0f);
            Flight(id, 1)[mFlightPosition] = Flush(id < count && rightInputs ? rightInputs[id] : 0.0f);
            // Keep a completed last block in the convolution until its next
            // boundary. Every source then has entirely zero overlap/history.
            if (source.remaining > 1 || position == mBlockSize - 1) --source.remaining;
            if (source.remaining != 0) mActiveSources[static_cast<std::size_t>(nextCount++)] = id;
            else source.active = false;
        }
        mActiveCount = nextCount;
        if (++mFlightPosition == mDelayCapacity) mFlightPosition = 0;
        for (int group = 0; group < mGroupCount; ++group) if (++mBlockPosition[group] == mBlockSize) {
            RenderPartition(group);
            mBlockPosition[group] = 0;
            if (++mHistoryPosition[group] == mPartitions) mHistoryPosition[group] = 0;
        }
    }

    // Persistent writes are flushed at 1e-30, well before float subnormals.
    // There are no recursive microphone states to sweep at control rate.
    void FlushTiny() {}
    bool HasNoSubnormalState() const {
        const auto okay = [](float value) { return FastIsFinite(value) && std::fpclassify(value) != FP_SUBNORMAL; };
        for (float value : mFlight) if (!okay(value)) return false;
        for (float value : mHistory) if (!okay(value)) return false;
        for (const auto& source : mSources)
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < kMaximumBlock; ++i)
                    if (!okay(source.previous[channel][i]) || !okay(source.current[channel][i])) return false;
        for (const auto& group : mOutput) for (const auto& channel : group) for (float value : channel) if (!okay(value)) return false;
        return true;
    }
    int PartitionFrames() const { return mBlockSize; }
    int KernelFrames() const { return mKernelLength; }
    bool HasTail() const {
        for (const auto& source : mSources) if (source.remaining != 0) return true;
        for (const auto& group : mOutput) for (const auto& channel : group) for (float value : channel) if (value != 0.0f) return true;
        return false;
    }

private:
    using Complex = std::complex<float>;
    static constexpr double kPi = 3.14159265358979323846;
    struct Source {
        CapturePoint position{};
        int profile = 0;
        bool configured = false, active = false;
        int remaining = 0, tailFrames = 0;
        int integerDelay[2]{};
        float fractional[2][8]{};
        float rangeGain = 1.0f;
        std::array<std::array<float, kMaximumBlock>, 2> previous{}, current{};
    };

    static float Flush(float value) { return FastAbs(value) < 1e-30f ? 0.0f : value; }
    static double Sinc(double x) { return FastAbs(x) < 1e-12 ? 1.0 : std::sin(kPi*x)/(kPi*x); }

    bool ConfigureFlight(Source& source) {
        const double distance = CaptureDistance(source.position, CloseHeadCentre());
        const double itd = kCloseHrtfProfiles[source.profile].residualItdSeconds;
        const double flight = distance / kCaptureSpeedOfSound;
        if (!(flight > 0.0 && flight < kMaximumFlightSeconds)) return false;
        double maxDelay = 0.0;
        for (int channel = 0; channel < 2; ++channel) {
            // The minimum-phase kernels already carry their own phase/ILD.
            // This is only the *residual* interaural delay.
            const double delay = (flight + (channel ? 0.5 : -0.5) * itd) * mRate
                               - mBlockSize - mResampleDelayFrames;
            if (!(delay >= 3.0 && delay < mFlightLimit - 5.0)) return false;
            const int integer = static_cast<int>(std::floor(delay));
            const double fraction = delay - integer;
            source.integerDelay[channel] = integer;
            double sum = 0.0;
            for (int tap = 0; tap < 8; ++tap) {
                const double delta = tap - 3.0 - fraction;
                const double window = 0.5 + 0.5 * std::cos(kPi * delta / 4.0);
                source.fractional[channel][tap] = static_cast<float>(Sinc(delta) * window);
                sum += source.fractional[channel][tap];
            }
            for (float& coefficient : source.fractional[channel]) coefficient = static_cast<float>(coefficient / sum);
            maxDelay = std::max(maxDelay, delay);
        }
        source.rangeGain = static_cast<float>(kReferenceDistance / distance);
        source.tailFrames = static_cast<int>(std::ceil(maxDelay)) + 8 + mKernelLength + 3*mBlockSize;
        return true;
    }

    void ClearSource(int id) {
        auto& source = mSources[static_cast<std::size_t>(id)];
        source.remaining = 0;
        source.current = {}; source.previous = {};
        for (int channel = 0; channel < 2; ++channel) {
            std::fill(Flight(id, channel), Flight(id, channel) + mDelayCapacity, 0.0f);
            std::fill(History(id, channel), History(id, channel) + 2*mSpectrumCapacity, 0.0f);
        }
    }

    float* Flight(int source, int channel) {
        return mFlight.data() + (static_cast<std::size_t>(source)*2 + channel)*mDelayCapacity;
    }
    // One ear's spectra, a partition after another: each partition is
    // mFftSize real parts followed by mFftSize imaginary parts (bins
    // 0..mFftSize/2 are used), so the spectrum sums read whole lanes.
    float* History(int source, int channel) {
        return mHistory.data() + (static_cast<std::size_t>(source)*2 + channel)*2*mSpectrumCapacity;
    }
    const float* Filter(int profile, int channel) const {
        return mFilters.data() + (static_cast<std::size_t>(profile)*2 + channel)*2*mSpectrumCapacity;
    }

    template<int Size, int Length, bool Inverse>
    void FftStage(std::array<Complex, kMaximumFft>& data) const {
        constexpr int half = Length/2, stride = Size/Length;
        for (int first = 0; first < Size; first += Length)
            for (int j = 0; j < half; ++j) {
                const Complex twiddle = mTwiddle[static_cast<std::size_t>(j*stride)];
                const Complex value = data[static_cast<std::size_t>(first+j+half)];
                const float imaginary = Inverse ? -twiddle.imag() : twiddle.imag();
                const Complex odd{value.real()*twiddle.real()-value.imag()*imaginary,
                                  value.real()*imaginary+value.imag()*twiddle.real()};
                const Complex even = data[static_cast<std::size_t>(first+j)];
                data[static_cast<std::size_t>(first+j)] = even+odd;
                data[static_cast<std::size_t>(first+j+half)] = even-odd;
            }
        if constexpr (Length < Size) FftStage<Size, Length*2, Inverse>(data);
    }

    template<int Size, bool Inverse, bool Reorder>
    void FftFixed(std::array<Complex, kMaximumFft>& data) const {
        if constexpr (Reorder) {
            for (int i = 0; i < Size; ++i) {
                const int j = mReverse[static_cast<std::size_t>(i)];
                if (j > i) std::swap(data[static_cast<std::size_t>(i)], data[static_cast<std::size_t>(j)]);
            }
        }
        FftStage<Size, 2, Inverse>(data);
        if constexpr (Inverse)
            for (int i = 0; i < Size; ++i) data[static_cast<std::size_t>(i)] /= static_cast<float>(Size);
    }

    template<bool Inverse, bool Reorder = true>
    void FftDispatch(std::array<Complex, kMaximumFft>& data) const {
        // Native-rate setup selects one of these powers of two. Compile each
        // stage with its fixed stride and direction, keeping the original
        // butterfly order while removing runtime stage/stride arithmetic.
        switch (mFftSize) {
            case 2: FftFixed<2, Inverse, Reorder>(data); break;
            case 4: FftFixed<4, Inverse, Reorder>(data); break;
            case 8: FftFixed<8, Inverse, Reorder>(data); break;
            case 16: FftFixed<16, Inverse, Reorder>(data); break;
            case 32: FftFixed<32, Inverse, Reorder>(data); break;
            case 64: FftFixed<64, Inverse, Reorder>(data); break;
            case 128: FftFixed<128, Inverse, Reorder>(data); break;
            case 256: FftFixed<256, Inverse, Reorder>(data); break;
            case 512: FftFixed<512, Inverse, Reorder>(data); break;
        }
    }

    // The forward transform of one real source block, on separate real and
    // imaginary lanes so each stage's butterflies vectorize. Every butterfly
    // evaluates exactly the expressions of FftStage<..., false>, in the same
    // stage order, so the spectrum is bit-identical to the interleaved form.
    struct SplitBlock { std::array<float, kMaximumFft> re, im; };

    // Inverse stages negate the twiddle's imaginary part, as FftStage<..., true>
    // does. The final stage computes only the outputs its caller reads: bins
    // 0..Size/2 of a forward transform, the real upper half of an inverse.
    template<int Size, int Length, bool Inverse>
    void SplitStage(SplitBlock& data) const {
        constexpr int half = Length/2;
#if MAREMBA_SIMD
        if constexpr (Lanes && Length == 4 && Size >= 8) {
            // Two spans a pass: their even pairs side by side in one vector,
            // their odd pairs in another, so each lane is one butterfly of
            // the loop below.
            const float* const twiddleRe = mStageTwiddle.re.data() + 1;
            const float* const twiddleIm = (Inverse ? mInverseTwiddleIm.data() : mStageTwiddle.im.data()) + 1;
            const Float4 rotateRe{twiddleRe[0], twiddleRe[1], twiddleRe[0], twiddleRe[1]};
            const Float4 rotateIm{twiddleIm[0], twiddleIm[1], twiddleIm[0], twiddleIm[1]};
            float* const re = data.re.data();
            float* const im = data.im.data();
            for (int first = 0; first < Size; first += 8) {
                const Float4 re0 = LoadFloat4(re+first), re1 = LoadFloat4(re+first+4);
                const Float4 im0 = LoadFloat4(im+first), im1 = LoadFloat4(im+first+4);
                const Float4 valueRe = HighPairs(re0, re1), valueIm = HighPairs(im0, im1);
                const Float4 oddRe = valueRe*rotateRe-valueIm*rotateIm;
                const Float4 oddIm = valueRe*rotateIm+valueIm*rotateRe;
                const Float4 evenRe = LowPairs(re0, re1), evenIm = LowPairs(im0, im1);
                const Float4 lowRe = evenRe+oddRe, lowIm = evenIm+oddIm;
                const Float4 highRe = evenRe-oddRe, highIm = evenIm-oddIm;
                StoreFloat4(re+first, LowPairs(lowRe, highRe));
                StoreFloat4(re+first+4, HighPairs(lowRe, highRe));
                StoreFloat4(im+first, LowPairs(lowIm, highIm));
                StoreFloat4(im+first+4, HighPairs(lowIm, highIm));
            }
            SplitStage<Size, 8, Inverse>(data);
            return;
        }
        if constexpr (Lanes && half >= 4) {
            // Four butterflies a vector, each lane evaluating the expressions
            // of the loops below. The vectors are loaded before any store, so
            // the twiddle table needs no local copy here.
            const float* const twiddleRe = mStageTwiddle.re.data() + (half-1);
            const float* const twiddleIm = (Inverse ? mInverseTwiddleIm.data() : mStageTwiddle.im.data()) + (half-1);
            float* const re = data.re.data();
            float* const im = data.im.data();
            if constexpr (Length == Size) {
                for (int j = 0; j < half; j += 4) {
                    const Float4 valueRe = LoadFloat4(re+j+half), valueIm = LoadFloat4(im+j+half);
                    const Float4 rotateRe = LoadFloat4(twiddleRe+j), rotateIm = LoadFloat4(twiddleIm+j);
                    const Float4 oddRe = valueRe*rotateRe-valueIm*rotateIm;
                    const Float4 evenRe = LoadFloat4(re+j);
                    if constexpr (Inverse) {
                        StoreFloat4(re+j+half, evenRe-oddRe);
                    } else {
                        const Float4 oddIm = valueRe*rotateIm+valueIm*rotateRe;
                        const Float4 evenIm = LoadFloat4(im+j);
                        StoreFloat4(re+j, evenRe+oddRe);
                        StoreFloat4(im+j, evenIm+oddIm);
                        // Bin Size/2, the one upper output a forward transform keeps.
                        if (j == 0) {
                            re[half] = evenRe[0]-oddRe[0];
                            im[half] = evenIm[0]-oddIm[0];
                        }
                    }
                }
            } else {
                for (int first = 0; first < Size; first += Length)
                    for (int j = 0; j < half; j += 4) {
                        const int low = first+j, high = first+j+half;
                        const Float4 valueRe = LoadFloat4(re+high), valueIm = LoadFloat4(im+high);
                        const Float4 rotateRe = LoadFloat4(twiddleRe+j), rotateIm = LoadFloat4(twiddleIm+j);
                        const Float4 oddRe = valueRe*rotateRe-valueIm*rotateIm;
                        const Float4 oddIm = valueRe*rotateIm+valueIm*rotateRe;
                        const Float4 evenRe = LoadFloat4(re+low), evenIm = LoadFloat4(im+low);
                        StoreFloat4(re+low, evenRe+oddRe);
                        StoreFloat4(im+low, evenIm+oddIm);
                        StoreFloat4(re+high, evenRe-oddRe);
                        StoreFloat4(im+high, evenIm-oddIm);
                    }
                SplitStage<Size, Length*2, Inverse>(data);
            }
            return;
        }
#endif
        // Local copies: the compiler cannot otherwise rule out `data` being
        // the twiddle table, and versions every span's loop on that check.
        std::array<float, half> twiddleRe, twiddleIm;
        std::copy_n(mStageTwiddle.re.data() + (half-1), half, twiddleRe.data());
        std::copy_n((Inverse ? mInverseTwiddleIm.data() : mStageTwiddle.im.data()) + (half-1), half, twiddleIm.data());
        if constexpr (Length == Size) {
            if constexpr (Inverse) {
                for (int j = 0; j < half; ++j) {
                    const float valueRe = data.re[static_cast<std::size_t>(j+half)];
                    const float valueIm = data.im[static_cast<std::size_t>(j+half)];
                    const float oddRe = valueRe*twiddleRe[j]-valueIm*twiddleIm[j];
                    data.re[static_cast<std::size_t>(j+half)] = data.re[static_cast<std::size_t>(j)]-oddRe;
                }
            } else {
                {
                    const float valueRe = data.re[static_cast<std::size_t>(half)];
                    const float valueIm = data.im[static_cast<std::size_t>(half)];
                    const float oddRe = valueRe*twiddleRe[0]-valueIm*twiddleIm[0];
                    const float oddIm = valueRe*twiddleIm[0]+valueIm*twiddleRe[0];
                    const float evenRe = data.re[0], evenIm = data.im[0];
                    data.re[0] = evenRe+oddRe;
                    data.im[0] = evenIm+oddIm;
                    data.re[static_cast<std::size_t>(half)] = evenRe-oddRe;
                    data.im[static_cast<std::size_t>(half)] = evenIm-oddIm;
                }
                for (int j = 1; j < half; ++j) {
                    const float valueRe = data.re[static_cast<std::size_t>(j+half)];
                    const float valueIm = data.im[static_cast<std::size_t>(j+half)];
                    const float oddRe = valueRe*twiddleRe[j]-valueIm*twiddleIm[j];
                    const float oddIm = valueRe*twiddleIm[j]+valueIm*twiddleRe[j];
                    data.re[static_cast<std::size_t>(j)] += oddRe;
                    data.im[static_cast<std::size_t>(j)] += oddIm;
                }
            }
            return;
        }
        for (int first = 0; first < Size; first += Length)
            for (int j = 0; j < half; ++j) {
                const int low = first+j, high = first+j+half;
                const float valueRe = data.re[static_cast<std::size_t>(high)];
                const float valueIm = data.im[static_cast<std::size_t>(high)];
                const float oddRe = valueRe*twiddleRe[j]-valueIm*twiddleIm[j];
                const float oddIm = valueRe*twiddleIm[j]+valueIm*twiddleRe[j];
                const float evenRe = data.re[static_cast<std::size_t>(low)];
                const float evenIm = data.im[static_cast<std::size_t>(low)];
                data.re[static_cast<std::size_t>(low)] = evenRe+oddRe;
                data.im[static_cast<std::size_t>(low)] = evenIm+oddIm;
                data.re[static_cast<std::size_t>(high)] = evenRe-oddRe;
                data.im[static_cast<std::size_t>(high)] = evenIm-oddIm;
            }
        if constexpr (Length < Size) SplitStage<Size, Length*2, Inverse>(data);
    }

    // Input: bit-reversed real samples in re. The first stage's twiddle is
    // exactly (1, -0) and every input imaginary part is +0, so its butterfly
    // is exactly (a+b, +0) and (a-b, +0) for finite samples.
    template<int Size>
    void ForwardSplitFixed(SplitBlock& data) const {
        for (int first = 0; first < Size; first += 2) {
            const float even = data.re[static_cast<std::size_t>(first)];
            const float value = data.re[static_cast<std::size_t>(first+1)];
            data.re[static_cast<std::size_t>(first)] = even+value;
            data.re[static_cast<std::size_t>(first+1)] = even-value;
            data.im[static_cast<std::size_t>(first)] = 0.0f;
            data.im[static_cast<std::size_t>(first+1)] = 0.0f;
        }
        if constexpr (Size > 2) SplitStage<Size, 4, false>(data);
    }

    void ForwardSplit(SplitBlock& data) const {
        switch (mFftSize) {
            case 2: ForwardSplitFixed<2>(data); break;
            case 4: ForwardSplitFixed<4>(data); break;
            case 8: ForwardSplitFixed<8>(data); break;
            case 16: ForwardSplitFixed<16>(data); break;
            case 32: ForwardSplitFixed<32>(data); break;
            case 64: ForwardSplitFixed<64>(data); break;
            case 128: ForwardSplitFixed<128>(data); break;
            case 256: ForwardSplitFixed<256>(data); break;
            case 512: ForwardSplitFixed<512>(data); break;
        }
    }

    template<int Size>
    void InverseSplitFixed(SplitBlock& data) const { SplitStage<Size, 2, true>(data); }

    // Bit-reversed spectrum in, the real upper half out (unscaled).
    void InverseSplit(SplitBlock& data) const {
        switch (mFftSize) {
            case 2: InverseSplitFixed<2>(data); break;
            case 4: InverseSplitFixed<4>(data); break;
            case 8: InverseSplitFixed<8>(data); break;
            case 16: InverseSplitFixed<16>(data); break;
            case 32: InverseSplitFixed<32>(data); break;
            case 64: InverseSplitFixed<64>(data); break;
            case 128: InverseSplitFixed<128>(data); break;
            case 256: InverseSplitFixed<256>(data); break;
            case 512: InverseSplitFixed<512>(data); break;
        }
    }

    void Fft(std::array<Complex, kMaximumFft>& data, bool inverse) const {
        if (inverse) FftDispatch<true>(data);
        else FftDispatch<false>(data);
    }

    void BuildFilters() {
        std::fill(mFilters.begin(), mFilters.end(), 0.0f);
        const double ratio = mRate / kCloseHrtfReferenceRate;
        for (int profile = 0; profile < kCloseHrtfProfileCount; ++profile) for (int channel = 0; channel < 2; ++channel) {
            const auto& original = kCloseHrtfProfiles[profile].impulse[channel];
            for (int sample = 0; sample < mKernelLength; ++sample) {
                const double t = sample/ratio - mResampleSupport;
                const int first = std::max(0, static_cast<int>(std::ceil(t-mResampleSupport)));
                const int last = std::min(kCloseHrtfTaps-1, static_cast<int>(std::floor(t+mResampleSupport)));
                double value = 0.0;
                for (int n = first; n <= last; ++n) {
                    const double delta = t-n;
                    value += original[n] * mResampleCutoff * Sinc(delta*mResampleCutoff)
                           * Sinc(delta/mResampleSupport);
                }
                mKernelScratch[static_cast<std::size_t>(sample)] = Flush(static_cast<float>(value/ratio));
            }
            float* filter = mFilters.data() + (static_cast<std::size_t>(profile)*2 + channel)*2*mSpectrumCapacity;
            for (int part = 0; part < mPartitions; ++part) {
                std::array<Complex, kMaximumFft> block{};
                for (int i = 0; i < mBlockSize; ++i) {
                    const int sample = part*mBlockSize+i;
                    if (sample < mKernelLength) block[static_cast<std::size_t>(i)] = mKernelScratch[static_cast<std::size_t>(sample)];
                }
                Fft(block, false);
                for (int i = 0; i < mFftSize; ++i) {
                    filter[part*2*mFftSize+i] = block[static_cast<std::size_t>(i)].real();
                    filter[part*2*mFftSize+mFftSize+i] = block[static_cast<std::size_t>(i)].imag();
                }
            }
        }
    }

    // The block just completed through each ear's 8-tap fractional delay.
    // Every output sums its taps from 0.0f in tap order, exactly as one
    // frame at a time would; across frames the sums are independent and
    // vectorize. A source is cleared, line included, when it activates, so
    // frames before its activation read zeros and give +0, as they did.
    void DelayBlock(int id, Source& source) {
        for (int channel = 0; channel < 2; ++channel) {
            const float* line = Flight(id, channel);
            const float* fractional = source.fractional[channel];
            // Frame i of the block was written at mFlightPosition-mBlockSize+i.
            int start = mFlightPosition - mBlockSize - source.integerDelay[channel] + 3;
            if (start < 0) start += mDelayCapacity;
            std::array<float, kMaximumBlock> value;
            std::fill_n(value.data(), mBlockSize, 0.0f);
#if MAREMBA_SIMD
            Float4 taps[8];
            if constexpr (Lanes) for (int tap = 0; tap < 8; ++tap) taps[tap] = SplatFloat4(fractional[tap]);
#endif
            for (int i = 0; i < mBlockSize;) {
                int at = start + i;
                if (at >= mDelayCapacity) at -= mDelayCapacity;
                if (at >= 7) {
                    // A run of frames whose eight taps all lie before the wrap.
                    const int end = i + std::min(mBlockSize - i, mDelayCapacity - at);
                    const float* x = line + (at - i);
#if MAREMBA_SIMD
                    if constexpr (Lanes) for (; i + 4 <= end; i += 4) {
                        Float4 sum = LoadFloat4(value.data() + i);
                        for (int tap = 0; tap < 8; ++tap) sum += LoadFloat4(x + i - tap) * taps[tap];
                        StoreFloat4(value.data() + i, sum);
                    }
#endif
                    for (int tap = 0; tap < 8; ++tap)
                        for (int k = i; k < end; ++k) value[static_cast<std::size_t>(k)] += x[k - tap] * fractional[tap];
                    i = end;
                } else {
                    float& sum = value[static_cast<std::size_t>(i++)];
                    for (int tap = 0; tap < 8; ++tap) {
                        sum += line[at] * fractional[tap];
                        if (--at < 0) at = mDelayCapacity - 1;
                    }
                }
            }
            int i = 0;
#if MAREMBA_SIMD
            if constexpr (Lanes) for (; i + 4 <= mBlockSize; i += 4)
                StoreFloat4(source.current[channel].data()+i, FlushFloat4(LoadFloat4(value.data()+i), 1e-30f));
#endif
            for (; i < mBlockSize; ++i)
                source.current[channel][static_cast<std::size_t>(i)] = Flush(value[static_cast<std::size_t>(i)]);
        }
    }

    // out[i] = Flush(in[i]*factor), or Flush(in[i]/factor).
    template<bool Divide>
    static void ScaleFlush(float* out, const float* in, float factor, int count) {
        int i = 0;
#if MAREMBA_SIMD
        if constexpr (Lanes) {
            const Float4 lanes = SplatFloat4(factor);
            for (; i + 4 <= count; i += 4) {
                const Float4 value = LoadFloat4(in+i);
                StoreFloat4(out+i, FlushFloat4(Divide ? value/lanes : value*lanes, 1e-30f));
            }
        }
#endif
        for (; i < count; ++i) out[i] = Flush(Divide ? in[i]/factor : in[i]*factor);
    }

    // sum += x*h for `count` bins on split lanes: each bin receives exactly
    // the complex product and += of the interleaved form.
    static void AccumulateSpectrum(float* sumRe, float* sumIm, const float* xRe, const float* xIm,
                                   const float* hRe, const float* hIm, int count) {
        int i = 0;
#if MAREMBA_SIMD
        if constexpr (Lanes) for (; i + 4 <= count; i += 4) {
            const Float4 xr = LoadFloat4(xRe+i), xi = LoadFloat4(xIm+i);
            const Float4 hr = LoadFloat4(hRe+i), hi = LoadFloat4(hIm+i);
            StoreFloat4(sumRe+i, LoadFloat4(sumRe+i) + (xr*hr-xi*hi));
            StoreFloat4(sumIm+i, LoadFloat4(sumIm+i) + (xr*hi+xi*hr));
        }
#endif
        for (; i < count; ++i) {
            sumRe[i] += xRe[i]*hRe[i]-xIm[i]*hIm[i];
            sumIm[i] += xRe[i]*hIm[i]+xIm[i]*hRe[i];
        }
    }

    // Independent groups start their partitions at different native frames.
    // Their outputs are summed in time; each still has exactly one block of
    // algorithmic delay. This prevents every source FFT landing in a single
    // host callback at high rates, where a 256-frame block spans 4 callbacks.
    void RenderPartition(int group) {
        if (mActiveCount == 0) {
            for (auto& channel : mOutput[group]) std::fill_n(channel.data(), mBlockSize, 0.0f);
            return;
        }
        // The spectra sum on split lanes; each bin receives exactly the
        // complex product and += of the interleaved form, in the same order.
        std::array<SplitBlock, 2> sum;
        for (auto& channel : sum) {
            std::fill_n(channel.re.data(), mFftSize/2+1, 0.0f);
            std::fill_n(channel.im.data(), mFftSize/2+1, 0.0f);
        }
        for (int active = 0; active < mActiveCount; ++active) {
            const int id = mActiveSources[static_cast<std::size_t>(active)];
            if ((id & (mGroupCount-1)) != group) continue;
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (!source.configured || source.remaining == 0) continue;
            DelayBlock(id, source);
            for (int channel = 0; channel < 2; ++channel) {
                SplitBlock block;
                for (int i = 0; i < mBlockSize; ++i) {
                    // Fill directly in the exact order the FFT's former swap
                    // pass produced. The transform needs no reorder pass for
                    // these real source inputs; no butterfly is changed.
                    block.re[static_cast<std::size_t>(mReverse[i])] = source.previous[channel][static_cast<std::size_t>(i)];
                    block.re[static_cast<std::size_t>(mReverse[i+mBlockSize])] = source.current[channel][static_cast<std::size_t>(i)];
                }
                std::copy_n(source.current[channel].data(), mBlockSize, source.previous[channel].data());
                ForwardSplit(block);
                const int bins = mFftSize/2+1;
                float* history = History(id, channel);
                float* newest = history+mHistoryPosition[group]*2*mFftSize;
                ScaleFlush<false>(newest, block.re.data(), source.rangeGain, bins);
                ScaleFlush<false>(newest+mFftSize, block.im.data(), source.rangeGain, bins);
                const float* filter = Filter(source.profile, channel);
                float* sumRe = sum[static_cast<std::size_t>(channel)].re.data();
                float* sumIm = sum[static_cast<std::size_t>(channel)].im.data();
                int previous = mHistoryPosition[group];
                for (int part = 0; part < mPartitions; ++part) {
                    const float* h = filter+part*2*mFftSize;
                    const float* x = history+previous*2*mFftSize;
                    AccumulateSpectrum(sumRe, sumIm, x, x+mFftSize, h, h+mFftSize, bins);
                    if (--previous < 0) previous = mPartitions-1;
                }
            }
        }
        const float scale = static_cast<float>(mFftSize);
        for (int channel = 0; channel < 2; ++channel) {
            // The conjugate-symmetric spectrum, bit-reversed into place as
            // the inverse FFT's swap pass would leave it.
            const SplitBlock& spectrum = sum[static_cast<std::size_t>(channel)];
            SplitBlock block;
            for (int i = 0; i < mFftSize; ++i) {
                const int bin = mReverse[static_cast<std::size_t>(i)];
                const bool mirrored = bin > mFftSize/2;
                const int source = mirrored ? mFftSize-bin : bin;
                block.re[static_cast<std::size_t>(i)] = spectrum.re[static_cast<std::size_t>(source)];
                block.im[static_cast<std::size_t>(i)] = mirrored ? -spectrum.im[static_cast<std::size_t>(source)]
                                                                 : spectrum.im[static_cast<std::size_t>(source)];
            }
            InverseSplit(block);
            // std::complex<float> /= float divides each part by the float.
            ScaleFlush<true>(mOutput[group][channel].data(), block.re.data()+mBlockSize, scale, mBlockSize);
        }
    }

    double mMaximumRate = 0.0, mRate = 0.0;
    double mResampleCutoff = 1.0, mResampleSupport = 8.0, mResampleDelayFrames = 8.0;
    int mMaximumSources = 0, mFlightLimit = 0, mDelayCapacity = 0, mKernelCapacity = 0, mSpectrumCapacity = 0;
    int mBlockSize = 32, mFftSize = 64, mKernelLength = 0, mPartitions = 0;
    int mFlightPosition = 0, mGroupCount = 1;
    std::array<int, kMaximumGroups> mBlockPosition{}, mHistoryPosition{};
    int mActiveCount = 0;
    std::vector<Source> mSources;
    std::vector<int> mActiveSources;
    std::vector<float> mFlight, mKernelScratch, mHistory, mFilters;
    std::array<Complex, kMaximumBlock> mTwiddle{};
    std::array<int, kMaximumFft> mReverse{};
    SplitBlock mStageTwiddle{};
    std::array<float, kMaximumFft> mInverseTwiddleIm{};
    std::array<std::array<std::array<float, kMaximumBlock>, 2>, kMaximumGroups> mOutput{};
};

using CloseCapture = CloseCaptureKernel<MAREMBA_SIMD != 0>;

} // namespace maremba
