#pragma once

#include "DspMath.h"
#include "RoomScatterMotion.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace maremba {

// Reverberation time (seconds) of a room at 125 Hz, 500 Hz, 4 kHz and 10 kHz.
// Panels absorb the bass and air/wood the treble more than the mids, so
// t125 <= t500 >= t4k >= t10k (a flatter request is realised as flat).
struct DecayProfile { double t125, t500, t4k, t10k; };

// One room voicing of a FeedbackNetwork; every time is in seconds.
struct NetworkVoicing {
    std::array<double, 12> lines;     // loop delays
    std::array<double, 3> diffusers;  // series input allpasses
    double diffusion;                 // allpass coefficient
    DecayProfile decay;
    bool loopDiffusion = false;        // short lossless allpasses inside each loop
};

// Voicing delays are distinct prime numbers of samples at this reference tick
// (44.1 kHz, the 88.2 kHz reference rate's tick), so no two loops share echo
// times; at other rates they are the same times, rounded.
constexpr double kVoicingRate = 44100.0;

// Silence decaying states before they become subnormal (and expensive). Plain
// comparisons: the Rack Extension toolchain builds with -fno-builtin, where
// std::abs is a library call.
inline float FlushTiny(float x) { return (x < 1.0e-20f && x > -1.0e-20f) ? 0.0f : x; }

// Excitation outside any audio range (or not a number) is ignored, so every
// feedback state stays finite for any input.
inline float RoomExcitation(float x) { return (x <= 1.0e5f && x >= -1.0e5f) ? x : 0.0f; }

// Second-order Butterworth low-pass defined in Hz (bilinear, prewarped): the
// air and surface loss that band-limits a room, so its response is the same
// continuous-time signal at every internal rate. It also keeps the room's
// reduced tick rate (RoomTick) free of aliasing.
class RoomBandLimit {
public:
    void SetSampleRate(double cutoff, double rate) {
        const double k = std::tan(kPiD * std::min(cutoff, 0.45 * rate) / rate);
        const double norm = 1.0 / (1.0 + std::sqrt(2.0) * k + k * k);
        mB0 = static_cast<float>(k * k * norm);
        mA1 = static_cast<float>(2.0 * (k * k - 1.0) * norm);
        mA2 = static_cast<float>((1.0 - std::sqrt(2.0) * k + k * k) * norm);
        Reset();
    }
    void Reset() { mS1 = mS2 = 0.0f; }
    void FlushStates() { mS1 = FlushTiny(mS1); mS2 = FlushTiny(mS2); }
    float Process(float x) {
        const float y = mB0 * x + mS1;
        mS1 = 2.0f * mB0 * x - mA1 * y + mS2;
        mS2 = mB0 * x - mA2 * y;
        return y;
    }
private:
    float mB0 = 1.0f, mA1 = 0.0f, mA2 = 0.0f, mS1 = 0.0f, mS2 = 0.0f;
};

// A band-limited room needs no oversampled rate, so it runs on a tick of
// 44.1-88.2 kHz: every `factor`-th internal sample (a power of two), fed the
// mean of the band-limited samples since the previous tick. The wet output is
// interpolated linearly between the last two ticks, one tick (about 20 us)
// late; the dry path is untouched. Below 88.2 kHz every sample is a tick.
class RoomTick {
public:
    // Highest tick rate for internal rates up to maxRate (sizes the delay memory).
    static double MaxRate(double maxRate) { return std::min(maxRate, 2.0 * kMinRate); }

    void SetSampleRate(double sampleRate) {
        mFactor = 1;
        while (sampleRate / (2 * mFactor) >= kMinRate) mFactor *= 2;
        mRate = sampleRate / mFactor;
        mMean = 1.0f / static_cast<float>(mFactor);
        Reset();
    }
    double Rate() const { return mRate; }
    float Mean() const { return mMean; } // turns a sum over one tick into its mean
    void Reset() { mPhase = 0; mFromL = mFromR = mStepL = mStepR = mToL = mToR = 0.0f; }

    // Count one internal sample; true on a tick (then Emit that tick's output).
    bool Advance() {
        if (++mPhase < mFactor) return false;
        mPhase = 0;
        return true;
    }
    void Emit(float left, float right) {
        mFromL = mToL;
        mFromR = mToR;
        mStepL = (left - mToL) * mMean;
        mStepR = (right - mToR) * mMean;
        mToL = left;
        mToR = right;
    }
    void Output(float& left, float& right) const {
        const float phase = static_cast<float>(mPhase);
        left = mFromL + phase * mStepL;
        right = mFromR + phase * mStepR;
    }

private:
    static constexpr double kMinRate = 44100.0;
    int mFactor = 1, mPhase = 0;
    double mRate = 44100.0;
    float mMean = 1.0f, mFromL = 0.0f, mFromR = 0.0f, mStepL = 0.0f, mStepR = 0.0f, mToL = 0.0f, mToR = 0.0f;
};

// First-order Thiran fractional delay. H(z)=(a+z^-1)/(1+a*z^-1),
// a=(1-d)/(1+d), has unity magnitude and DC group delay d samples.
// It is causal and stable for 0<d<1; d=0 is the exact identity. Unlike a
// fractional FIR in a feedback loop, it introduces no extra decay. Its phase
// delay is approximate away from DC, not an exact broadband time shift.
class RoomFractionalDelay {
public:
    void SetFraction(double fraction) {
        const double d = std::clamp(fraction, 0.0, 1.0);
        mCoefficient = static_cast<float>((1.0-d)/(1.0+d));
        Reset();
    }
    float Process(float input) {
        const float output = mCoefficient * input + mState;
        mState = FlushTiny(input - mCoefficient * output);
        return output;
    }
    void Reset() { mState = 0.0f; }
    void FlushStates() { mState = FlushTiny(mState); }
private:
    float mCoefficient = 1.0f, mState = 0.0f;
};

// Short Schroeder allpass, H(z)=(D(z)-g)/(1-g*D(z)). External storage is
// reserved with the FDN; configuration and rendering never allocate. D(z) is
// an integer delay followed by a first-order Thiran allpass of delay [1,2).
// Its magnitude is unity and its maximum group delay is the exact configured
// delay, at DC. The complete diffuser is bounded by delay*(1+|g|)/(1-|g|).
// Keeping the fractional section at >=1 sample avoids its Nyquist delay peak;
// requested delays shorter than one sample are an exact, stateless bypass.
class RoomLoopDiffuser {
public:
    static constexpr float kMaximumCoefficient = .35f;
    void Configure(double delay, float coefficient) {
        mExactDelay = delay >= 1.0 ? delay : 0.0;
        mDelay = mExactDelay > 0.0 ? static_cast<int>(std::floor(mExactDelay)) - 1 : 0;
        mCoefficient = std::clamp(coefficient, -kMaximumCoefficient, kMaximumCoefficient);
        const double fractional = mExactDelay > 0.0 ? mExactDelay - mDelay : 1.0;
        mFractionCoefficient = static_cast<float>((1.0 - fractional) / (1.0 + fractional));
        mImmediateCoefficient = (mFractionCoefficient - mCoefficient)
                              / (1.0f - mFractionCoefficient * mCoefficient);
        mFractionState = 0.0f;
        mWrite = 0;
    }
    float Process(float input, float* storage) {
        if (mExactDelay == 0.0) return input;
        if (mDelay == 0) {
            // The core has feedthrough here. Solve that feedback analytically
            // as one allpass, rather than introducing an extra sample delay.
            const float output = mImmediateCoefficient * input + mFractionState;
            mFractionState = FlushTiny(input - mImmediateCoefficient * output);
            return output;
        }
        const float read = storage[mWrite];
        const float delayed = mFractionCoefficient * read + mFractionState;
        mFractionState = FlushTiny(read - mFractionCoefficient * delayed);
        const float output = delayed - mCoefficient * input;
        storage[mWrite] = FlushTiny(input + mCoefficient * output);
        if (++mWrite == mDelay) mWrite = 0;
        return output;
    }
    void Reset(float* storage) {
        if (mDelay > 0) std::fill(storage, storage + mDelay, 0.0f);
        mFractionState = 0.0f;
        mWrite = 0;
    }
    int Delay() const { return mDelay; }
    double ExactDelay() const { return mExactDelay; }
    float Coefficient() const { return mCoefficient; }
    double MaximumGroupDelay() const {
        const double magnitude = std::abs(static_cast<double>(mCoefficient));
        return mExactDelay * (1.0 + magnitude) / (1.0 - magnitude);
    }
private:
    int mDelay = 0, mWrite = 0;
    double mExactDelay = 0.0;
    float mCoefficient = 0.0f, mFractionCoefficient = 0.0f;
    float mImmediateCoefficient = 0.0f, mFractionState = 0.0f;
};

// Twelve delay lines coupled by an orthogonal (lossless) matrix, a feedback
// delay network: every line feeds every other line on each pass, so the echo
// count multiplies twelve-fold per pass and the tail turns noise-like within a
// few passes. All decay comes from each line's absorption filter, whose dB loss
// is proportional to the line length so that every mode at one frequency
// decays at the same rate (Jot). Series allpasses diffuse the input first.
// The lines share one write position (each owns a span as long as the longest
// line), which keeps the per-sample bookkeeping to a single counter. The delay
// memory is owned here but reserved by the room's Allocate (sized by Layout);
// SetSampleRate and Process never allocate.
class FeedbackNetwork {
public:
    static constexpr int kLines = 12;
    static constexpr int kDiffusers = 3;
    static constexpr int kFlushPeriod = 32; // Process calls between FlushStates calls

    std::vector<float> memory; // every line and diffuser, reserved once by the room

    // Spans for rates up to maxRate; returns the samples to reserve in `memory`.
    size_t Layout(const NetworkVoicing& voicing, double maxRate) {
        mLineSpan = Span(voicing.lines, maxRate);
        mDiffuserSpan = Span(voicing.diffusers, maxRate);
        mLoopDiffuserSpan = 0;
        if (voicing.loopDiffusion) {
            for (int i = 0; i < kLines; ++i)
                mLoopDiffuserSpan = std::max(mLoopDiffuserSpan,
                    static_cast<int>(std::floor(voicing.lines[static_cast<size_t>(i)]
                        * maxRate * kLoopDiffuserRatios[static_cast<size_t>(i)])) + 1);
        }
        return static_cast<size_t>(kLines * mLineSpan + kDiffusers * mDiffuserSpan
                                 + kLines * mLoopDiffuserSpan);
    }

    // Realtime safe: lengths and absorption for this rate, then clear. Without
    // memory (before Allocate) every length stays 0.
    void SetSampleRate(const NetworkVoicing& voicing, double rate, bool fractionalTiming = false,
                       uint32_t scatterSeed = 0) {
        mFractionalTiming = fractionalTiming;
        mScatterEnabled = scatterSeed != 0;
        if (mScatterEnabled) mScatter.Configure(rate, scatterSeed);
        const bool allocated = !memory.empty();
        mLoopDiffusionEnabled = allocated && voicing.loopDiffusion && mLoopDiffuserSpan > 0;
        mLength = mDiffuserLength = 0;
        double all = 0.0, panels = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const double exact = voicing.lines[static_cast<size_t>(i)] * rate;
            mDelay[i] = allocated ? (fractionalTiming
                ? std::clamp(static_cast<int>(std::floor(exact)), 1, mLineSpan-1)
                : Samples(voicing.lines[static_cast<size_t>(i)], rate, mLineSpan)) : 1;
            mFraction[i].SetFraction(exact - std::floor(exact));
            const double loopDelay = mLoopDiffusionEnabled ? std::min(
                exact * kLoopDiffuserRatios[static_cast<size_t>(i)],
                static_cast<double>(mLoopDiffuserSpan)) : 0.0;
            mLoopDiffuser[i].Configure(loopDelay, kLoopDiffuserCoefficients[static_cast<size_t>(i)]);
            if (allocated) mLength = std::max(mLength, mDelay[i] + 1);
            const double length = fractionalTiming ? exact : mDelay[i];
            all += length;
            if (i % 3 == 0) panels += length;
        }
        // Air and wood (high shelf) absorb in every line in proportion to its
        // length. The panels' extra bass absorption sits in the first line of
        // each group only, scaled up so that the energy, which visits every
        // line alike, loses the same on average (four low shelves, not twelve).
        const DecayProfile& p = voicing.decay;
        const double bass = 60.0 / p.t500 + all / panels * (60.0 / p.t125 - 60.0 / p.t500);
        for (int i = 0; i < kLines; ++i) {
            const DecayProfile line = { i % 3 == 0 ? 60.0 / bass : p.t500, p.t500, p.t4k, p.t10k };
            FitAbsorption(i, fractionalTiming ? voicing.lines[static_cast<size_t>(i)]
                                             : mDelay[i] / rate, line, rate);
        }
        for (int d = 0; d < kDiffusers; ++d) {
            const double exact = voicing.diffusers[static_cast<size_t>(d)] * rate;
            mDiffuserDelay[d] = allocated ? (fractionalTiming
                ? std::clamp(static_cast<int>(std::floor(exact)), 1, mDiffuserSpan-1)
                : Samples(voicing.diffusers[static_cast<size_t>(d)], rate, mDiffuserSpan)) : 1;
            mDiffuserFraction[d].SetFraction(exact - std::floor(exact));
            if (allocated) mDiffuserLength = std::max(mDiffuserLength, mDiffuserDelay[d] + 1);
        }
        mDiffusion = static_cast<float>(voicing.diffusion);
        Reset();
    }

    bool IsAllocated() const { return mLength > 0; }

    void Reset(bool resetMotion = true) {
        for (int i = 0; i < kLines; ++i)
            std::fill(memory.begin() + i * mLineSpan, memory.begin() + i * mLineSpan + mLength, 0.0f);
        for (int d = 0; d < kDiffusers; ++d) {
            const int at = kLines * mLineSpan + d * mDiffuserSpan;
            std::fill(memory.begin() + at, memory.begin() + at + mDiffuserLength, 0.0f);
        }
        mWrite = mDiffuserWrite = 0;
        mLowState.fill(0.0f);
        mHighState.fill(0.0f);
        for (auto& delay : mFraction) delay.Reset();
        for (auto& delay : mDiffuserFraction) delay.Reset();
        if (mLoopDiffusionEnabled) {
            float* const base = memory.data() + kLines * mLineSpan + kDiffusers * mDiffuserSpan;
            for (int i = 0; i < kLines; ++i)
                mLoopDiffuser[i].Reset(base + i * mLoopDiffuserSpan);
        }
        if (resetMotion && mScatterEnabled) mScatter.Reset();
    }

    // One sample: `in` is diffused and spread over the lines by the mixing
    // matrix; left/right are two orthogonal (uncorrelated) mixes of the lines.
    void Process(float in, float& left, float& right,
                 const RoomScatterMotion::Coefficients* sharedScattering = nullptr) {
        float* const m = memory.data();
        float* const diffusers = m + kLines * mLineSpan;
        float x = in;
        for (int d = 0; d < kDiffusers; ++d) {
            float* const line = diffusers + d * mDiffuserSpan;
            const int at = mDiffuserWrite - mDiffuserDelay[d];
            const float read = line[at < 0 ? at + mDiffuserLength : at];
            const float delayed = mFractionalTiming ? mDiffuserFraction[d].Process(read) : read;
            const float y = delayed - mDiffusion * x;
            line[mDiffuserWrite] = FlushTiny(x + mDiffusion * y);
            x = y;
        }
        if (++mDiffuserWrite == mDiffuserLength) mDiffuserWrite = 0;

        // Orthogonal 12 x 12 mixing of the absorbed line outputs: a Householder
        // reflection I - 2/3 * ones within each group of three lines, then 4-point
        // Hadamard butterflies across the groups (their 1/2 scale is folded into
        // the line gains). Every entry is nonzero: each line reaches all twelve.
        float v[kLines];
        for (int g = 0; g < kLines; g += 3) {
            const float a = Absorb(g, m, true), b = Absorb(g + 1, m, false), c = Absorb(g + 2, m, false);
            const float t = (2.0f / 3.0f) * (a + b + c);
            v[g] = a - t;
            v[g + 1] = b - t;
            v[g + 2] = c - t;
        }
        // Slowly varying orthogonal scattering rotates feedback only. It
        // redistributes stored echo energy without adding gain, moving delay
        // taps, or modulating the direct input. Legacy networks leave it off.
        if (mScatterEnabled) {
            if (sharedScattering) {
                RoomScatterMotion::Apply(v, *sharedScattering);
                mScatter.Advance();
            } else {
                mScatterCoefficients = mScatter.Next();
                RoomScatterMotion::Apply(v, mScatterCoefficients);
            }
        }
        // The input joins the second Hadamard input of each position, so it
        // reaches lines 0-2 and 6-8 with +1 and lines 3-5 and 9-11 with -1.
        v[3] += x;
        v[4] += x;
        v[5] += x;
        for (int b = 0; b < 3; ++b) {
            const float s0 = v[b] + v[b + 3], d0 = v[b] - v[b + 3];
            const float s1 = v[b + 6] + v[b + 9], d1 = v[b + 6] - v[b + 9];
            v[b] = s0 + s1;
            v[b + 3] = d0 + d1;
            v[b + 6] = s0 - s1;
            v[b + 9] = d0 - d1;
        }
        // Outputs: two orthogonal rows of the mix, without the direct input
        left = v[3] + x;
        right = v[8] - x;
        for (int i = 0; i < kLines; ++i)
            m[i * mLineSpan + mWrite] = FlushTiny(v[i]);
        if (++mWrite == mLength) mWrite = 0;
    }

    // Available after Process without shared coefficients. A disabled legacy
    // network returns null, keeping static reference and old room paths exact.
    const RoomScatterMotion::Coefficients* ScatteringCoefficients() const {
        return mScatterEnabled ? &mScatterCoefficients : nullptr;
    }

    // The line writes are flushed on every Process call; the filter states only
    // need it every kFlushPeriod calls (too few for a state to decay from 1e-20
    // into the subnormal range in between), which the owning room schedules.
    void FlushStates() {
        for (int i = 0; i < kLines; ++i) {
            // Only the first line of each three-line group has a low shelf.
            // Other low states stay zero after Reset. Fractional-delay states
            // are already flushed at every write in RoomFractionalDelay.
            if (i % 3 == 0) mLowState[i] = FlushTiny(mLowState[i]);
            mHighState[i] = FlushTiny(mHighState[i]);
        }
    }

private:
    friend struct FeedbackNetworkTestAccess;
    // Output of line i through its absorption: gain (with the panels' low shelf
    // on the first line of a group), then the high shelf.
    float Absorb(int i, float* m, bool panel) {
        const int at = mWrite - mDelay[i];
        const float read = m[i * mLineSpan + (at < 0 ? at + mLength : at)];
        float s = mFractionalTiming ? mFraction[i].Process(read) : read;
        if (mLoopDiffusionEnabled) {
            float* const storage = m + kLines * mLineSpan + kDiffusers * mDiffuserSpan
                                   + i * mLoopDiffuserSpan;
            s = mLoopDiffuser[i].Process(s, storage);
        }
        float u = mLowB0[i] * s;
        if (panel) {
            u += mLowState[i];
            mLowState[i] = mLowB1[i] * s - mLowA1[i] * u;
        }
        const float y = u + mHighState[i];
        mHighState[i] = mHighB1[i] * u - mHighA1[i] * y;
        return y;
    }

    // Samples for the longest of the delays at maxRate (plus the write cell).
    template <size_t N>
    static int Span(const std::array<double, N>& seconds, double maxRate) {
        return static_cast<int>(std::ceil(*std::max_element(seconds.begin(), seconds.end()) * maxRate)) + 1;
    }

    // A delay rounded to whole samples (within half a sample of its time at every
    // rate). Retained for legacy room voicings; coherent new capture uses the
    // optional fractional timing to avoid rate-dependent narrowband resonances.
    static int Samples(double seconds, double rate, int span) {
        return std::clamp(static_cast<int>(std::lround(seconds * rate)), 1, std::max(1, span - 1));
    }

    // Line gain g, low shelf (s + wz)/(s + wp) and high shelf (1 + s a)/(1 + s b),
    // fitted in the prewarped s-plane so the bilinear filter loses exactly
    // seconds*60/T60 dB at the four profile frequencies (the shelves barely
    // interact, so alternating the two closed-form fits converges in a few rounds).
    void FitAbsorption(int line, double seconds, const DecayProfile& p, double rate) {
        const double k = 2.0 * rate;
        auto warped = [&](double hz) { const double w = k * std::tan(kPiD * hz / rate); return w * w; };
        const double top = std::min(10000.0, 0.4 * rate);
        const double w1 = warped(125.0), w2 = warped(500.0), w3 = warped(0.4 * top), w4 = warped(top);
        const double wp2 = warped(kLowShelfHz);
        auto loss = [&](double t60) { return std::pow(10.0, -6.0 * seconds / t60); }; // power per pass
        const double t1 = loss(p.t125), t2 = loss(p.t500), t3 = loss(p.t4k), t4 = loss(p.t10k);
        double g2 = t2, z2 = wp2, a2 = 0.0, b2 = 0.0;
        auto high = [&](double w) { return (1.0 + w * a2) / (1.0 + w * b2); };
        auto low = [&](double w) { return (w + z2) / (w + wp2); };
        for (int round = 0; round < 4; ++round) {
            const double r1 = t1 / high(w1) * (w1 + wp2), r2 = t2 / high(w2) * (w2 + wp2);
            g2 = (r1 - r2) / (w1 - w2);
            z2 = g2 > 0.0 ? std::clamp(r1 / g2 - w1, 0.0, wp2) : wp2;
            g2 = t2 / (high(w2) * low(w2));
            const double s1 = t3 / (g2 * low(w3)), s2 = t4 / (g2 * low(w4));
            const double det = w3 * w4 * (s1 - s2);
            if (!(std::abs(det) > 0.0)) { a2 = b2 = 0.0; continue; }
            b2 = std::max(0.0, (w3 * (s2 - 1.0) - w4 * (s1 - 1.0)) / det);
            a2 = std::clamp((s1 * w3 * (s2 - 1.0) - s2 * w4 * (s1 - 1.0)) / det, 0.0, b2);
        }
        g2 = t2 / (high(w2) * low(w2));
        // Bilinear sections; the high shelf's b0 and the mixing's 1/2 join the gain
        const double a = std::sqrt(a2) * k, b = std::sqrt(b2) * k;
        const double g = 0.5 * std::sqrt(g2) * (1.0 + a) / (1.0 + b);
        const double wz = std::sqrt(z2), wp = std::sqrt(wp2), den = k + wp;
        mLowB0[line] = static_cast<float>(g * (k + wz) / den);
        mLowB1[line] = static_cast<float>(g * (wz - k) / den);
        mLowA1[line] = static_cast<float>((wp - k) / den);
        mHighB1[line] = static_cast<float>((1.0 - a) / (1.0 + a));
        mHighA1[line] = static_cast<float>((1.0 - b) / (1.0 + b));
    }

    static constexpr double kLowShelfHz = 200.0;
    // At |g|<=.35 the maximum added group delay is less than 3.95% of each
    // original loop. At low rates an unrepresentable sub-sample delay bypasses.
    // The small added phase delay is measured by decay contracts; absorption
    // remains the same monotone four-band fit used by the undiffused network.
    static constexpr std::array<double, kLines> kLoopDiffuserRatios{
        .008, .011, .014, .017, .019, .010, .013, .016, .018, .012, .015, .019};
    static constexpr std::array<float, kLines> kLoopDiffuserCoefficients{
        .31f, -.33f, .35f, -.30f, .34f, -.32f, -.35f, .30f, -.31f, .33f, -.34f, .32f};

    int mLineSpan = 0, mDiffuserSpan = 0, mLoopDiffuserSpan = 0; // capacity per line (Allocate)
    int mLength = 0, mDiffuserLength = 0;             // active circular length
    int mWrite = 0, mDiffuserWrite = 0;
    std::array<int, kLines> mDelay{};
    std::array<int, kDiffusers> mDiffuserDelay{};
    std::array<float, kLines> mLowB0{}, mLowB1{}, mLowA1{}, mHighB1{}, mHighA1{};
    std::array<float, kLines> mLowState{}, mHighState{};
    std::array<RoomFractionalDelay, kLines> mFraction{};
    std::array<RoomFractionalDelay, kDiffusers> mDiffuserFraction{};
    std::array<RoomLoopDiffuser, kLines> mLoopDiffuser{};
    bool mFractionalTiming = false;
    bool mScatterEnabled = false;
    bool mLoopDiffusionEnabled = false;
    RoomScatterMotion mScatter;
    RoomScatterMotion::Coefficients mScatterCoefficients{};
    float mDiffusion = 0.0f;
};

/**
 * AcousticRoom ("Wood"):
 * A small timber-panelled studio (6 x 4.5 x 3 m, 81 m3) heard by a spaced room
 * pair 1.7 m from the instrument. The first reflections are the image sources
 * of its six surfaces, 4-14 ms after the direct sound, at their own delay and
 * 1/r gain per microphone; a 12-line feedback delay network with wood
 * absorption continues them as a dense tail: T60 1.0 s at 125 Hz (panel
 * absorption), 1.1 s at 500 Hz, 0.75 s at 4 kHz and 0.45 s at 10 kHz.
 *
 * Zero latency on the dry path. Memory is allocated ONCE (Allocate, outside the
 * realtime thread) for the highest rate the instance can run at; SetSampleRate
 * only changes the active lengths and clears them, it never allocates. Every
 * delay is in seconds and every filter is derived in Hz, so the room sounds the
 * same at every internal rate.
 */
class AcousticRoom {
public:
    AcousticRoom() = default;

    // Not realtime safe: reserve delay memory for rates up to maxSampleRate.
    void Allocate(double maxSampleRate) {
        const double tickRate = RoomTick::MaxRate(std::max(maxSampleRate, 8000.0));
        mTail.memory.assign(mTail.Layout(kTail, tickRate), 0.0f);
        const size_t early = 2 * (static_cast<size_t>(std::ceil(kEarlySpan * tickRate)) + 1);
        mEarlyL.assign(early, 0.0f);
        mEarlyR.assign(early, 0.0f);
        SetSampleRate(mSampleRate);
    }

    // Realtime safe (no allocation): set the active delay lengths and clear them.
    void SetSampleRate(double sampleRate) {
        if (!(sampleRate >= 8000.0)) sampleRate = 44100.0;
        mSampleRate = sampleRate;
        mBandL.SetSampleRate(kBandwidthHz, sampleRate);
        mBandR.SetSampleRate(kBandwidthHz, sampleRate);
        mTick.SetSampleRate(sampleRate);
        const double tickRate = mTick.Rate();
        mTail.SetSampleRate(kTail, tickRate);
        mEarlyLength = std::min(static_cast<int>(kEarlySpan * tickRate) + 1, static_cast<int>(mEarlyL.size() / 2));
        auto tap = [&](double seconds) {
            return mEarlyLength - std::clamp(static_cast<int>(std::lround(seconds * tickRate)), 1, std::max(1, mEarlyLength - 1));
        };
        for (int k = 0; k < kTaps; ++k) {
            mTapL[k] = tap(kEarlyL[k].seconds);
            mTapR[k] = tap(kEarlyR[k].seconds);
            mGainL[k] = kEarlyL[k].gain;
            mGainR[k] = kEarlyR[k].gain;
        }
        Reset();
    }

    // Clear the active part of every delay line (realtime safe).
    void Reset() {
        mTail.Reset();
        std::fill(mEarlyL.begin(), mEarlyL.begin() + 2 * mEarlyLength, 0.0f);
        std::fill(mEarlyR.begin(), mEarlyR.begin() + 2 * mEarlyLength, 0.0f);
        mEarlyIndex = mFlushTicks = 0;
        mBandL.Reset();
        mBandR.Reset();
        mSumL = mSumR = 0.0f;
        mTick.Reset();
    }

    // Process a stereo frame through the room. The dry mic signal and the room
    // return come back separately (the far mic tap is their sum; the main bus
    // widens only the dry part fully).
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

        mSumL += mBandL.Process(RoomExcitation(inL));
        mSumR += mBandR.Process(RoomExcitation(inR));
        if (mTick.Advance()) {
            Tick(mSumL * mTick.Mean(), mSumR * mTick.Mean());
            mSumL = mSumR = 0.0f;
        }
        float roomL = 0.0f, roomR = 0.0f;
        mTick.Output(roomL, roomR);
        wetL = roomLevel * roomL;
        wetR = roomLevel * roomR;
    }

private:
    static constexpr float kDryGain = 0.70f;
    static constexpr double kBandwidthHz = 11000.0;

    // The six first-order image sources of the 6 x 4.5 x 3 m room for a source at
    // (1.9, 2.0, 1.0) m and microphones at (3.6, 1.95, 1.9) / (3.55, 2.55, 1.9) m:
    // floor, ceiling, the two side walls, the wall behind the instrument and the
    // one behind the pair. Delay after the direct sound and gain
    // d_direct/d_image * 0.93 (wood), times the 0.7 dry gain so each reflection
    // sits at its natural level against the direct sound. Floor, ceiling and
    // end-wall images lie in the source's direction, so each microphone's
    // reflections image what it hears directly (its own channel).
    struct Reflection { double seconds; float gain; };
    static constexpr int kTaps = 6;
    static constexpr double kEarlySpan = 0.0140;
    static constexpr Reflection kEarlyL[kTaps] = {
        { 0.00419, 0.7f * 0.532f }, { 0.00470, 0.7f * 0.506f }, { 0.00720, 0.7f * 0.407f },
        { 0.01015, 0.7f * 0.331f }, { 0.01064, 0.7f * 0.321f }, { 0.01352, 0.7f * 0.273f } };
    static constexpr Reflection kEarlyR[kTaps] = {
        { 0.00415, 0.7f * 0.539f }, { 0.00465, 0.7f * 0.512f }, { 0.00837, 0.7f * 0.377f },
        { 0.00864, 0.7f * 0.370f }, { 0.01047, 0.7f * 0.328f }, { 0.01363, 0.7f * 0.275f } };

    // Late tail: loops of 15-74 ms (the room's axial and oblique path lengths)
    // and three input diffusers of 2.5-6.3 ms (panel and furniture scattering).
    static constexpr NetworkVoicing kTail = {
        { 661 / kVoicingRate, 863 / kVoicingRate, 1117 / kVoicingRate, 1373 / kVoicingRate,
          1637 / kVoicingRate, 1901 / kVoicingRate, 2143 / kVoicingRate, 2383 / kVoicingRate,
          2617 / kVoicingRate, 2837 / kVoicingRate, 3061 / kVoicingRate, 3271 / kVoicingRate },
        { 109 / kVoicingRate, 173 / kVoicingRate, 277 / kVoicingRate }, 0.62,
        { 1.0, 1.1, 0.75, 0.45 } };
    static constexpr float kTailGain = 3.38f; // with the reflections: the 1.3.0f5 Wood's loudness

    // One tick: early reflections and the late network (fed the mono sum).
    void Tick(float left, float right) {
        // Each image source reaches each microphone at its own delay. The history
        // is written twice, so the sample d ticks ago is always at offset
        // (length - d) from the write position, without wrapping.
        float* const historyL = mEarlyL.data() + mEarlyIndex;
        float* const historyR = mEarlyR.data() + mEarlyIndex;
        historyL[0] = historyL[mEarlyLength] = left;
        historyR[0] = historyR[mEarlyLength] = right;
        float earlyL = 0.0f, earlyR = 0.0f;
        for (int k = 0; k < kTaps; ++k) {
            earlyL += mGainL[k] * historyL[mTapL[k]];
            earlyR += mGainR[k] * historyR[mTapR[k]];
        }
        if (++mEarlyIndex == mEarlyLength) mEarlyIndex = 0;

        float tailL = 0.0f, tailR = 0.0f;
        mTail.Process(0.5f * (left + right), tailL, tailR);
        mTick.Emit(earlyL + kTailGain * tailL, earlyR + kTailGain * tailR);
        if (++mFlushTicks == FeedbackNetwork::kFlushPeriod) {
            mFlushTicks = 0;
            mTail.FlushStates();
            mBandL.FlushStates();
            mBandR.FlushStates();
        }
    }

    bool IsAllocated() const { return mTail.IsAllocated() && mEarlyLength > 1; }

    double mSampleRate = 44100.0;
    RoomBandLimit mBandL, mBandR;
    RoomTick mTick;
    FeedbackNetwork mTail;
    float mSumL = 0.0f, mSumR = 0.0f; // band-limited input since the last tick
    // Input histories, written twice; capacity fixed by Allocate
    std::vector<float> mEarlyL, mEarlyR;
    int mEarlyLength = 0, mEarlyIndex = 0, mFlushTicks = 0;
    // Per-rate tap table: offset (length - delay) from the write position, and
    // gain (flat member arrays keep the per-tick loop to plain loads)
    std::array<int, kTaps> mTapL{}, mTapR{};
    std::array<float, kTaps> mGainL{}, mGainR{};
};

} // namespace maremba
