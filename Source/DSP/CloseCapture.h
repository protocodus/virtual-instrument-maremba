#pragma once

#include "CaptureGeometry.h"
#include "MeasuredCloseHrtf.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace maremba {

// Two receiver-specific acoustic pressure inputs per source. Modal surface
// projection is upstream; these measured head/ear filters, range gain and
// common propagation are applied once here. The SOFA response is a 1.2 m,
// diffuse-field-equalized measurement, adapted to the declared scene.
//
// Uniform partitioned convolution sums source spectra before inverse FFTs in
// staggered groups. Its processing delay is deducted from the propagation line:
// it is not another hidden device latency. All storage is reserved by Allocate.
class CloseCapture {
public:
    static constexpr double kReferenceDistance = 0.6;
    static constexpr double kMaximumFlightSeconds = 0.01;
    static constexpr int kMaximumBlock = 256;
    static constexpr int kMaximumFft = 2 * kMaximumBlock;
    static constexpr int kMaximumGroups = kMaximumBlock / 32;

    void Allocate(double maxHostRate, int maxSources = 520) {
        mMaximumRate = std::max(8000.0, maxHostRate);
        mMaximumSources = std::max(1, maxSources);
        mDelayCapacity = static_cast<int>(std::ceil(kMaximumFlightSeconds * mMaximumRate)) + 16;
        mKernelCapacity = static_cast<int>(std::ceil((kCloseHrtfTaps + 16.0) * mMaximumRate
                                                     / kCloseHrtfReferenceRate)) + 32;
        // Partitions have twice as many FFT bins as time samples; permit the
        // final partial block, including the tiny-block low-rate fallback.
        mSpectrumCapacity = 2 * (mKernelCapacity + kMaximumBlock);
        mSources.resize(static_cast<std::size_t>(mMaximumSources));
        mActiveSources.resize(static_cast<std::size_t>(mMaximumSources));
        mFlight.assign(static_cast<std::size_t>(mMaximumSources) * 2 * mDelayCapacity, 0.0f);
        mHistory.assign(static_cast<std::size_t>(mMaximumSources) * 2 * mSpectrumCapacity, {});
        mFilters.assign(static_cast<std::size_t>(kCloseHrtfProfileCount) * 2 * mSpectrumCapacity, {});
        mKernelScratch.resize(static_cast<std::size_t>(mKernelCapacity));
        mRate = 0.0;
        Reset();
    }

    // No allocation. The engine calls this at setup/native rate changes;
    // oversampling changes do not change the Close convolution's native rate.
    void SetSampleRate(double hostRate) {
        if (mSources.empty()) return;
        if (!std::isfinite(hostRate)) return;
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
            minimumFlight = std::min(minimumFlight, flight - 0.5*std::abs(itd));
        }
        const double available = minimumFlight*rate - mResampleDelayFrames - 3.0;
        mBlockSize = 1;
        while (mBlockSize < kMaximumBlock && 2*mBlockSize <= available) mBlockSize *= 2;
        mFftSize = 2*mBlockSize;
        mGroupCount = std::max(1, mBlockSize/32);
        mKernelLength = static_cast<int>(std::ceil((kCloseHrtfTaps - 1.0
                            + 2.0 * mResampleSupport) * ratio)) + 1;
        mPartitions = (mKernelLength + mBlockSize - 1) / mBlockSize;
        for (int i = 0; i < mFftSize / 2; ++i) {
            const double angle = -2.0 * kPi * i / mFftSize;
            mTwiddle[static_cast<std::size_t>(i)] = {static_cast<float>(std::cos(angle)),
                                                    static_cast<float>(std::sin(angle))};
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
            || profileIndex >= kCloseHrtfProfileCount || !std::isfinite(position.x)
            || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
        auto& source = mSources[static_cast<std::size_t>(id)];
        source.position = position;
        source.profile = profileIndex;
        source.configured = ConfigureFlight(source);
        ClearSource(id);
        return source.configured;
    }

    void Reset() {
        std::fill(mFlight.begin(), mFlight.end(), 0.0f);
        std::fill(mHistory.begin(), mHistory.end(), Complex{});
        for (auto& source : mSources) {
            source.remaining = 0;
            source.active = false;
            source.previous = {};
            source.current = {};
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
    void Process(const float* leftInputs, const float* rightInputs, int count,
                 float& left, float& right) {
        left = right = 0.0f;
        if (mRate <= 0.0 || mSources.empty()) return;
        count = std::clamp(count, 0, mMaximumSources);
        for (int group = 0; group < mGroupCount; ++group) {
            left += mOutput[group][0][static_cast<std::size_t>(mBlockPosition[group])];
            right += mOutput[group][1][static_cast<std::size_t>(mBlockPosition[group])];
        }
        // Inputs are contiguous, but each source owns a large FFT history.
        // Avoid touching every dormant history/metadata page at every frame.
        for (int id = 0; id < count; ++id) {
            const float rawL = leftInputs ? leftInputs[id] : 0.0f;
            const float rawR = rightInputs ? rightInputs[id] : 0.0f;
            if (rawL == 0.0f && rawR == 0.0f) continue;
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (!source.configured) continue;
            if (!std::isfinite(rawL) || !std::isfinite(rawR)) {
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
            const int position = mBlockPosition[id % mGroupCount];
            const float input[2] = {Flush(id < count && leftInputs ? leftInputs[id] : 0.0f),
                                    Flush(id < count && rightInputs ? rightInputs[id] : 0.0f)};
            for (int channel = 0; channel < 2; ++channel) {
                float* line = Flight(id, channel);
                line[mFlightPosition] = input[channel];
                int at = mFlightPosition - source.integerDelay[channel] + 3;
                if (at < 0) at += mDelayCapacity;
                if (at >= mDelayCapacity) at -= mDelayCapacity;
                float value = 0.0f;
                for (int tap = 0; tap < 8; ++tap) {
                    value += line[at] * source.fractional[channel][tap];
                    if (--at < 0) at = mDelayCapacity - 1;
                }
                source.current[channel][static_cast<std::size_t>(position)] = Flush(static_cast<float>(value));
            }
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
        const auto okay = [](float value) { return std::isfinite(value) && std::fpclassify(value) != FP_SUBNORMAL; };
        for (float value : mFlight) if (!okay(value)) return false;
        for (const auto& value : mHistory) if (!okay(value.real()) || !okay(value.imag())) return false;
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

    static float Flush(float value) { return std::abs(value) < 1e-30f ? 0.0f : value; }
    static double Sinc(double x) { return std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi*x)/(kPi*x); }

    bool ConfigureFlight(Source& source) {
        const double distance = CaptureDistance(source.position, CloseHeadCentre());
        const double itd = kCloseHrtfProfiles[source.profile].residualItdSeconds;
        const double flight = distance / kCaptureSpeedOfSound;
        if (!(flight > 0.0 && flight < kMaximumFlightSeconds)) return false;
        double maxDelay = 0.0;
        for (int channel = 0; channel < 2; ++channel) {
            // The minimum-phase kernels already carry their own phase/ILD.
            // This is only the measured *residual* interaural delay.
            const double delay = (flight + (channel ? 0.5 : -0.5) * itd) * mRate
                               - mBlockSize - mResampleDelayFrames;
            if (!(delay >= 3.0 && delay < mDelayCapacity - 5.0)) return false;
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
            std::fill(History(id, channel), History(id, channel) + mSpectrumCapacity, Complex{});
        }
    }

    float* Flight(int source, int channel) {
        return mFlight.data() + (static_cast<std::size_t>(source)*2 + channel)*mDelayCapacity;
    }
    Complex* History(int source, int channel) {
        return mHistory.data() + (static_cast<std::size_t>(source)*2 + channel)*mSpectrumCapacity;
    }
    const Complex* Filter(int profile, int channel) const {
        return mFilters.data() + (static_cast<std::size_t>(profile)*2 + channel)*mSpectrumCapacity;
    }

    void Fft(std::array<Complex, kMaximumFft>& data, bool inverse) const {
        for (int i = 0; i < mFftSize; ++i) {
            const int j = mReverse[static_cast<std::size_t>(i)];
            if (j > i) std::swap(data[static_cast<std::size_t>(i)], data[static_cast<std::size_t>(j)]);
        }
        for (int length = 2; length <= mFftSize; length <<= 1) {
            const int half = length/2, stride = mFftSize/length;
            for (int first = 0; first < mFftSize; first += length)
                for (int j = 0; j < half; ++j) {
                    const Complex twiddle = mTwiddle[static_cast<std::size_t>(j*stride)];
                    const Complex value = data[static_cast<std::size_t>(first+j+half)];
                    const float imaginary = inverse ? -twiddle.imag() : twiddle.imag();
                    const Complex odd{value.real()*twiddle.real()-value.imag()*imaginary,
                                      value.real()*imaginary+value.imag()*twiddle.real()};
                    const Complex even = data[static_cast<std::size_t>(first+j)];
                    data[static_cast<std::size_t>(first+j)] = even+odd;
                    data[static_cast<std::size_t>(first+j+half)] = even-odd;
                }
        }
        if (inverse) for (int i = 0; i < mFftSize; ++i) data[static_cast<std::size_t>(i)] /= static_cast<float>(mFftSize);
    }

    void BuildFilters() {
        std::fill(mFilters.begin(), mFilters.end(), Complex{});
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
            Complex* filter = mFilters.data() + (static_cast<std::size_t>(profile)*2 + channel)*mSpectrumCapacity;
            for (int part = 0; part < mPartitions; ++part) {
                std::array<Complex, kMaximumFft> block{};
                for (int i = 0; i < mBlockSize; ++i) {
                    const int sample = part*mBlockSize+i;
                    if (sample < mKernelLength) block[static_cast<std::size_t>(i)] = mKernelScratch[static_cast<std::size_t>(sample)];
                }
                Fft(block, false);
                std::copy_n(block.data(), mFftSize, filter + part*mFftSize);
            }
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
        std::array<std::array<Complex, kMaximumFft>, 2> sum{};
        for (int active = 0; active < mActiveCount; ++active) {
            const int id = mActiveSources[static_cast<std::size_t>(active)];
            if (id % mGroupCount != group) continue;
            auto& source = mSources[static_cast<std::size_t>(id)];
            if (!source.configured || source.remaining == 0) continue;
            for (int channel = 0; channel < 2; ++channel) {
                std::array<Complex, kMaximumFft> block;
                for (int i = 0; i < mBlockSize; ++i) {
                    block[static_cast<std::size_t>(i)] = source.previous[channel][static_cast<std::size_t>(i)];
                    block[static_cast<std::size_t>(i+mBlockSize)] = source.current[channel][static_cast<std::size_t>(i)];
                }
                std::copy_n(source.current[channel].data(), mBlockSize, source.previous[channel].data());
                Fft(block, false);
                Complex* history = History(id, channel);
                for (int i = 0; i <= mFftSize/2; ++i) history[mHistoryPosition[group]*mFftSize+i] =
                    {Flush(block[static_cast<std::size_t>(i)].real()*source.rangeGain),
                     Flush(block[static_cast<std::size_t>(i)].imag()*source.rangeGain)};
                const Complex* filter = Filter(source.profile, channel);
                int previous = mHistoryPosition[group];
                for (int part = 0; part < mPartitions; ++part) {
                    const Complex* h = filter+part*mFftSize;
                    const Complex* x = history+previous*mFftSize;
                    for (int i = 0; i <= mFftSize/2; ++i)
                        sum[channel][static_cast<std::size_t>(i)] += Complex(
                            x[i].real()*h[i].real()-x[i].imag()*h[i].imag(),
                            x[i].real()*h[i].imag()+x[i].imag()*h[i].real());
                    if (--previous < 0) previous = mPartitions-1;
                }
            }
        }
        for (int channel = 0; channel < 2; ++channel) {
            for (int i = 1; i < mFftSize/2; ++i)
                sum[channel][static_cast<std::size_t>(mFftSize-i)] = std::conj(sum[channel][static_cast<std::size_t>(i)]);
            Fft(sum[channel], true);
            for (int i = 0; i < mBlockSize; ++i)
                mOutput[group][channel][static_cast<std::size_t>(i)] = Flush(sum[channel][static_cast<std::size_t>(i+mBlockSize)].real());
        }
    }

    double mMaximumRate = 0.0, mRate = 0.0;
    double mResampleCutoff = 1.0, mResampleSupport = 8.0, mResampleDelayFrames = 8.0;
    int mMaximumSources = 0, mDelayCapacity = 0, mKernelCapacity = 0, mSpectrumCapacity = 0;
    int mBlockSize = 32, mFftSize = 64, mKernelLength = 0, mPartitions = 0;
    int mFlightPosition = 0, mGroupCount = 1;
    std::array<int, kMaximumGroups> mBlockPosition{}, mHistoryPosition{};
    int mActiveCount = 0;
    std::vector<Source> mSources;
    std::vector<int> mActiveSources;
    std::vector<float> mFlight, mKernelScratch;
    std::vector<Complex> mHistory, mFilters;
    std::array<Complex, kMaximumBlock> mTwiddle{};
    std::array<int, kMaximumFft> mReverse{};
    std::array<std::array<std::array<float, kMaximumBlock>, 2>, kMaximumGroups> mOutput{};
};

} // namespace maremba
