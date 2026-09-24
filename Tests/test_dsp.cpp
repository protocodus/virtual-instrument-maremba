// Maremba DSP verification suite.
//
// Build & run (from Examples/Maremba):
//   clang++ -std=c++17 -O3 -Wall -Wextra -Werror Tests/test_dsp.cpp DSP/*.cpp -o Tests/test_dsp && ./Tests/test_dsp
//
// Active feature tests compare identical seeds with the feature on and off.
// Compatibility tests separately require removed controls to remain inert.

#include "../DSP/MarembaEngine.h"
#include "../DSP/ModalExcitation.h"
#include "../DSP/ContactExcitation.h"
#include "../DSP/BarModes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

// ─── Private engine state for the tests (MarembaEngine befriends this) ──────
namespace maremba {
struct MarembaEngineTestAccess {
    static bool MeshNormal(const MarembaEngine& e) { return e.mSympathetic.HasNoSubnormalState(); }
    static bool BodyNormal(const MarembaEngine& e) { return e.mFrameBody.HasNoSubnormalState(); }
    // Float main-bus chain after the mixer, including limiter control memory.
    static bool MainBusNormal(const MarembaEngine& e) {
        bool ok = e.mPreamp.HasNoSubnormalState() && e.mLimiter.HasNoSubnormalState()
                  && e.mOversampler.HasNoSubnormalState();
        for (const auto& d : e.mDirectDecimators) ok = ok && d.HasNoSubnormalState();
        return ok;
    }
    // True when a live voice holds the note and is marked released
    static bool IsNoteReleased(const MarembaEngine& e, int note) {
        for (const auto& v : e.mVoices) {
            if (v.IsLive() && v.GetNoteNumber() == note) return v.IsReleased();
        }
        return false;
    }
};
} // namespace maremba

// ─── Heap allocation tracking (realtime-safety test) ────────────────────────
namespace {
volatile bool gCountAllocations = false;
volatile long gAllocationCount = 0;
}

void* operator new(std::size_t size)
{
    if (gCountAllocations) ++gAllocationCount;
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (gCountAllocations) ++gAllocationCount;
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }

// C++17 over-aligned allocation (alignas(32/64) types, SIMD buffers)
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
    if (gCountAllocations) ++gAllocationCount;
    const std::size_t alignment = std::max(static_cast<std::size_t>(align), sizeof(void*));
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment, size == 0 ? 1 : size) != 0) return nullptr;
    return memory;
}
void* operator new(std::size_t size, std::align_val_t align)
{
    if (void* memory = ::operator new(size, align, std::nothrow))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t align) { return ::operator new(size, align); }
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& tag) noexcept { return ::operator new(size, align, tag); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }

namespace {

using maremba::EngineParameters;
using maremba::MarembaEngine;

int gFailures = 0;

#define CHECK(cond, msg)                                                              \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << "  FAIL: " << msg << "  [" #cond "]" << std::endl;           \
            ++gFailures;                                                              \
        }                                                                             \
    } while (0)

enum Channel { kMainL = 0, kMainR, kCloseL, kCloseR, kFarL, kFarR, kPiezo, kNumChannels };
const char* kChannelNames[kNumChannels] = { "mainL", "mainR", "closeL", "closeR", "farL", "farR", "piezo" };

struct Render {
    std::array<std::vector<float>, kNumChannels> ch;
    size_t size() const { return ch[0].size(); }
};

// Render 'frames' frames in 64-frame batches, appending to 'out'.
// If 'everyBatch' is set, SetParameters(params) is called before every batch (wrapper pattern).
void RenderFrames(MarembaEngine& engine, Render& out, int frames, const EngineParameters* everyBatch = nullptr)
{
    float buf[kNumChannels][64];
    for (int done = 0; done < frames; done += 64) {
        const int n = std::min(64, frames - done);
        if (everyBatch) engine.SetParameters(*everyBatch);
        engine.RenderBatch(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], n);
        for (int c = 0; c < kNumChannels; ++c) out.ch[c].insert(out.ch[c].end(), buf[c], buf[c] + n);
    }
}

Render RenderNote(double sampleRate, const EngineParameters& params, int note, float velocity, double seconds)
{
    MarembaEngine engine(sampleRate);
    engine.SetParameters(params);
    engine.NoteOn(note, velocity);
    Render r;
    RenderFrames(engine, r, static_cast<int>(seconds * sampleRate));
    return r;
}

double Rms(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    if (to <= from) return 0.0;
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double Peak(const std::vector<float>& x, size_t from = 0, size_t to = std::numeric_limits<size_t>::max())
{
    to = std::min(to, x.size());
    double m = 0.0;
    for (size_t i = from; i < to; ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

double DiffEnergy(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    to = std::min({ to, a.size(), b.size() });
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        sum += d * d;
    }
    return sum;
}

double Energy(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return sum;
}

bool AllFinite(const Render& r)
{
    for (const auto& c : r.ch)
        for (float v : c)
            if (!std::isfinite(v)) return false;
    return true;
}

double Db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-30)); }

// Squared magnitude of x at f (Goertzel). Callers that need low leakage
// window x first (see HannWindowed).
double GoertzelPower(const std::vector<double>& x, double sampleRate, double f)
{
    const double coeff = 2.0 * std::cos(2.0 * M_PI * f / sampleRate);
    double s1 = 0.0, s2 = 0.0;
    for (double v : x) {
        const double s = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

std::vector<double> HannWindowed(std::vector<double> x)
{
    const size_t n = x.size();
    for (size_t i = 0; i < n && n > 1; ++i) x[i] *= 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n - 1));
    return x;
}

// Frequency of the strongest component within +/-25 cents of fNominal.
double EstimateFrequency(const std::vector<float>& left, const std::vector<float>& right,
                         size_t from, size_t to, double sampleRate, double fNominal)
{
    to = std::min(to, left.size());
    const size_t n = to - from;
    std::vector<double> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n - 1));
        x[i] = (static_cast<double>(left[from + i]) + right[from + i]) * w;
    }
    double lo = fNominal * std::exp2(-25.0 / 1200.0);
    double hi = fNominal * std::exp2(25.0 / 1200.0);
    double best = fNominal, bestPower = -1.0;
    for (int i = 0; i <= 25; ++i) {
        const double f = lo * std::pow(hi / lo, i / 25.0);
        const double p = GoertzelPower(x, sampleRate, f);
        if (p > bestPower) { bestPower = p; best = f; }
    }
    lo = best * std::exp2(-2.5 / 1200.0);
    hi = best * std::exp2(2.5 / 1200.0);
    const double g = 0.6180339887498949;
    double c = hi - g * (hi - lo), d = lo + g * (hi - lo);
    double pc = GoertzelPower(x, sampleRate, c), pd = GoertzelPower(x, sampleRate, d);
    for (int it = 0; it < 30; ++it) {
        if (pc > pd) { hi = d; d = c; pd = pc; c = hi - g * (hi - lo); pc = GoertzelPower(x, sampleRate, c); }
        else         { lo = c; c = d; pc = pd; d = lo + g * (hi - lo); pd = GoertzelPower(x, sampleRate, d); }
    }
    return 0.5 * (lo + hi);
}

double Cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }
double NoteHz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

// Clean, deterministic single-note settings for measurements.
EngineParameters CleanParams()
{
    EngineParameters p;
    p.artifacts = 0.0f;
    p.buzzAmount = 0.0f;
    p.strikeJitter = 0.0f;
    p.pitchGlide = 0.0f;
    // Isolate the original physical response from the optional velocity routes.
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0.0f;
    return p;
}

// Energy of a one-pole high-passed copy (fc ~ 2.5 kHz at 44.1 kHz).
double HighPassEnergy(const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min(to, x.size());
    float state = 0.0f;
    double e = 0.0;
    for (size_t i = 0; i < to; ++i) {
        const float hp = x[i] - state;
        state += 0.30f * hp;
        if (i >= from) e += static_cast<double>(hp) * hp;
    }
    return e;
}

// ─── Tests ──────────────────────────────────────────────────────────────────

void TestNoteRangeStability()
{
    std::cout << "[Stability] Notes 21..108, all models, 2x/4x/8x ..." << std::endl;
    for (int os = 0; os < 3; ++os) {
        for (int model = 0; model < 4; ++model) {
            MarembaEngine engine(44100.0);
            EngineParameters p;
            p.model = model;
            p.oversampling = os;
            p.decay = 0.5f;
            engine.SetParameters(p);
            Render r;
            for (int note = 21; note <= 108; note += 3) {
                engine.NoteOn(note, 0.85f);
                RenderFrames(engine, r, 64 * 4);
            }
            CHECK(AllFinite(r), "non-finite output, model " << model << " os " << os);
            CHECK(Peak(r.ch[kMainL]) < 1.5 && Peak(r.ch[kMainR]) < 1.5,
                  "main output exploded, model " << model << " os " << os << " peak " << Peak(r.ch[kMainL]));
        }
    }
}

void TestKeyboardPanning()
{
    std::cout << "[Panning] Low notes lean left, high notes lean right ..." << std::endl;
    EngineParameters p = CleanParams();
    p.farLevel = 0.0f;
    p.piezoLevel = 0.0f;
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    Render low = RenderNote(44100.0, p, 36, 0.8f, 0.2);
    Render high = RenderNote(44100.0, p, 96, 0.8f, 0.2);
    CHECK(Energy(low.ch[kMainL], 0, low.size()) > 1.5 * Energy(low.ch[kMainR], 0, low.size()), "C2 not panned left");
    CHECK(Energy(high.ch[kMainR], 0, high.size()) > 1.5 * Energy(high.ch[kMainL], 0, high.size()), "C7 not panned right");
}

void TestStereoWidth()
{
    std::cout << "[Width] Stereo width 0 makes the whole main bus mono ..." << std::endl;
    EngineParameters p = CleanParams();
    p.stereoWidth = 0.0f;
    p.farLevel = 1.0f; // room and halo included
    p.sympathetic = 1.0f;
    Render r = RenderNote(44100.0, p, 40, 0.9f, 0.5);
    double maxDiff = 0.0;
    for (size_t i = 0; i < r.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(r.ch[kMainL][i] - r.ch[kMainR][i])));
    CHECK(maxDiff < 1e-6, "width 0 is not mono: max |L-R| " << maxDiff);
    p.stereoWidth = 2.0f;
    Render wide = RenderNote(44100.0, p, 40, 0.9f, 0.5);
    CHECK(DiffEnergy(wide.ch[kMainL], wide.ch[kMainR], 0, wide.size()) > 1e-4, "width 2 has no side signal");
}

void TestMirlitonBuzz()
{
    std::cout << "[Buzz] Balafon membrane (differential) and the Rosewood fallback ..." << std::endl;
    EngineParameters p = CleanParams();
    p.model = 2;
    p.compAmount = 0.0f;
    p.preampDrive = 0.0f;
    p.buzzAmount = 0.0f;
    Render hardOff = RenderNote(44100.0, p, 50, 0.95f, 0.4);
    p.buzzAmount = 0.8f;
    Render hardOn = RenderNote(44100.0, p, 50, 0.95f, 0.4);
    const double hardBuzz = DiffEnergy(hardOn.ch[kCloseL], hardOff.ch[kCloseL], 0, hardOn.size())
                          / Energy(hardOff.ch[kCloseL], 0, hardOff.size());
    std::cout << "  -> Balafon buzz (hard hit) " << 10.0 * std::log10(hardBuzz) << " dB re bar" << std::endl;
    CHECK(hardBuzz > 1e-4, "Balafon buzz knob has no audible effect");

    // Rosewood: the lower range now responds, with a clear full-range endpoint.
    EngineParameters r = CleanParams();
    r.buzzAmount = 0.0f;
    Render off = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    r.buzzAmount = 0.2f;
    Render atOnset = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    r.buzzAmount = 1.0f;
    Render on = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    CHECK(DiffEnergy(atOnset.ch[kCloseL], off.ch[kCloseL], 0, off.size()) > 0.0, "Buzz should respond across its lower range");
    const double rosewoodBuzz = DiffEnergy(on.ch[kCloseL], off.ch[kCloseL], 0, off.size()) / Energy(off.ch[kCloseL], 0, off.size());
    std::cout << "  -> Rosewood buzz 1.0: " << 10.0 * std::log10(rosewoodBuzz) << " dB re bar" << std::endl;
    CHECK(rosewoodBuzz > 1e-4, "Rosewood buzz endpoint is inaudible");

    // P2-2: no other instrument buzzes more than the Balafon, the buzzing
    // instrument (the Padauk charleo, the add-on Rosewood membrane and the
    // kalimba buzzers are all measured relative to their Buzz-off note). A
    // layer that stays inaudible (below -60 dB) cannot out-buzz it.
    auto buzzRe = [](int model, int note, float vel, float buzz) {
        EngineParameters q = CleanParams();
        q.model = model;
        q.compAmount = 0.0f;
        q.preampDrive = 0.0f;
        q.sympathetic = 0.0f;
        q.bodyBloom = 0.0f;
        q.farLevel = 0.0f;
        q.buzzAmount = 0.0f;
        const Render dry = RenderNote(44100.0, q, note, vel, 0.4);
        q.buzzAmount = buzz;
        const Render wet = RenderNote(44100.0, q, note, vel, 0.4);
        return 10.0 * std::log10(DiffEnergy(wet.ch[kCloseL], dry.ch[kCloseL], 0, dry.size()) / Energy(dry.ch[kCloseL], 0, dry.size()) + 1e-30);
    };
    double worstExcess = -1e9;
    for (int note : { 36, 60, 84 }) {
        for (float vel : { 0.6f, 1.0f }) {
            for (float buzz : { 0.6f, 1.0f }) {
                const double balafon = buzzRe(2, note, vel, buzz);
                for (int model : { 0, 1, 3 }) {
                    const double other = buzzRe(model, note, vel, buzz);
                    const double excess = other < -60.0 ? -1e9 : other - balafon;
                    worstExcess = std::max(worstExcess, excess);
                    CHECK(excess <= 0.5, "model " << model << " buzz exceeds the Balafon's by " << excess
                          << " dB (note " << note << ", vel " << vel << ", buzz " << buzz << ")");
                }
            }
        }
    }
    std::cout << "  -> other buzz layers vs Balafon: worst " << worstExcess << " dB" << std::endl;
}

void TestAnalogLimiterCurveAndLink()
{
    std::cout << "[Limiter] Settled limiting slope, quiet transparency and stereo peak linking ..." << std::endl;
    auto settled = [](float input) {
        maremba::AnalogLimiter limiter;
        limiter.SetSampleRate(96000.0);
        limiter.SetTimes(2.0f, 100.0f);
        limiter.SetAmount(1.0f);
        limiter.Reset();
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 96000; ++i) limiter.Process(input, -input * .37f, l, r);
        return l;
    };
    const float low = settled(0.0001f), a = settled(2.0f), b = settled(8.0f);
    CHECK(std::abs(low - .0001f) < 1e-8f, "limiter colors a signal far below its threshold");
    const double rise = Db(b / a);
    std::cout << "  -> +12 dB input produces " << rise << " dB output rise" << std::endl;
    CHECK(a > .01f && a < .5f && std::abs(rise) < 1.0,
          "limiter still behaves as gentle compression, rise=" << rise);

    for (double rate : { 88200.0, 96000.0, 384000.0, 1536000.0 }) {
        maremba::AnalogLimiter limiter;
        limiter.SetSampleRate(rate);
        limiter.SetTimes(50.0f, 500.0f); // slowest musical attack must still arrest a peak
        limiter.SetAmount(1.0f);
        limiter.Reset();
        double maxPeak = 0.0, maxLinkError = 0.0;
        for (int i = 0; i < 4096; ++i) {
            const float input = i % 97 == 0 ? 20.0f : static_cast<float>(3.0 * std::sin(i * .17));
            float l = 0.0f, r = 0.0f;
            limiter.Process(input, -.37f * input, l, r);
            limiter.ProtectOutput(l, r);
            CHECK(std::isfinite(l) && std::isfinite(r), "limiter produced non-finite audio at " << rate);
            maxPeak = std::max(maxPeak, static_cast<double>(std::max(std::abs(l), std::abs(r))));
            maxLinkError = std::max(maxLinkError, static_cast<double>(std::abs(r + .37f * l)));
        }
        CHECK(maxPeak <= maremba::AnalogLimiter::kOutputCeiling + 1e-6f, "peak escaped the active limiter");
        CHECK(maxLinkError < 1e-6, "limiter changes the stereo ratio: " << maxLinkError);
        // Reconstruction peaks after the oversampled block also share one gain.
        float l = 1.3f, r = -.52f;
        limiter.ProtectOutput(l, r);
        CHECK(std::abs(l) <= maremba::AnalogLimiter::kOutputCeiling + 1e-6f,
              "post-decimation sample guard missed a reconstruction peak");
        CHECK(std::abs(r + .4f * l) < 1e-6f, "output guard changes stereo balance");
    }
}

void TestAnalogLimiterBallistics()
{
    std::cout << "[Limiter] Attack, release, sustained-overload memory and sample-rate equivalence ..." << std::endl;
    auto probe = [](double rate, float attack, float release, double chargeMs, double recoveryMs) {
        maremba::AnalogLimiter limiter;
        limiter.SetSampleRate(rate);
        limiter.SetTimes(attack, release);
        limiter.SetAmount(1.0f);
        limiter.Reset();
        float l = 0.0f, r = 0.0f;
        for (long i = 0; i < std::lround(rate * chargeMs * .001); ++i)
            limiter.Process(.6f, .3f, l, r);
        if (recoveryMs == 0.0) return static_cast<double>(l / .6f);
        for (long i = 0; i < std::lround(rate * recoveryMs * .001); ++i)
            limiter.Process(.01f, .005f, l, r);
        return static_cast<double>(l / .01f);
    };
    const double fastAttack = probe(96000, 1, 100, 10, 0);
    const double slowAttack = probe(96000, 50, 100, 10, 0);
    CHECK(fastAttack < slowAttack * .9, "attack control is ineffective: " << fastAttack << "/" << slowAttack);
    const double fastRelease = probe(96000, 1, 20, 300, 100);
    const double slowRelease = probe(96000, 1, 500, 300, 100);
    CHECK(fastRelease > slowRelease * 1.2, "release control is ineffective: " << fastRelease << "/" << slowRelease);
    const double brief = probe(96000, 1, 100, 50, 100);
    const double sustained = probe(96000, 1, 100, 1000, 100);
    std::cout << "  -> 100 ms recovery gain: brief " << brief << ", sustained " << sustained << std::endl;
    CHECK(brief > sustained * 1.01, "release has no sustained-overload memory");
    const double reference = probe(88200, 12, 120, 30, 70);
    for (double rate : { 96000.0, 384000.0, 1536000.0 }) {
        const double delta = Db(probe(rate, 12, 120, 30, 70) / reference);
        CHECK(std::abs(delta) < .1, "time constants drift with internal rate " << rate << ": " << delta << " dB");
    }
}

void TestAnalogLimiterState()
{
    std::cout << "[Limiter] Exact bypass, reset, smooth automation and silent control state ..." << std::endl;
    maremba::AnalogLimiter limiter;
    limiter.SetSampleRate(96000.0);
    limiter.SetAmount(0.0f);
    limiter.Reset();
    for (int i = 0; i < 1000; ++i) {
        const float input = static_cast<float>(2.0 * std::sin(i * .031));
        float l = 0.0f, r = 0.0f;
        limiter.Process(input, -input * .5f, l, r);
        limiter.ProtectOutput(l, r);
        CHECK(l == input && r == -input * .5f, "zero amount is not an exact dry bypass");
    }
    auto sequence = [&]() {
        limiter.SetAmount(1.0f);
        limiter.SetTimes(12.0f, 120.0f);
        limiter.Reset();
        std::vector<float> output;
        for (int i = 0; i < 12000; ++i) {
            float l = 0.0f, r = 0.0f;
            const float in = i < 2400 ? .8f : .02f;
            limiter.Process(in, -.5f * in, l, r);
            output.push_back(l);
        }
        return output;
    };
    const auto first = sequence(), second = sequence();
    CHECK(first == second, "Reset does not clear all limiter memory");

    // Constant input isolates control discontinuities from the input waveform.
    float l = first.back(), r = 0.0f, previous = l;
    limiter.SetAmount(0.0f);
    double maxStep = 0.0;
    for (int i = 0; i < 9600; ++i) {
        limiter.Process(.02f, -.01f, l, r);
        maxStep = std::max(maxStep, static_cast<double>(std::abs(l - previous)));
        previous = l;
    }
    CHECK(maxStep < .001, "bypass automation makes a discontinuity: " << maxStep);
    CHECK(l == .02f && r == -.01f, "bypass never settles to exact dry signal");
    limiter.SetAmount(1.0f);
    for (int i = 0; i < 96000 * 12; ++i) {
        limiter.Process(0.0f, 0.0f, l, r);
        if (i % 128 == 0) {
            limiter.FlushTiny();
            CHECK(limiter.HasNoSubnormalState(), "silent limiter leaves subnormal control state");
        }
    }
    CHECK(l == 0.0f && r == 0.0f, "silent limiter generates a residual output");
    limiter.Process(.0001f, -.0001f, l, r);
    CHECK(std::abs(l - .0001f) < 1e-8f, "limiter resumes with stale attenuation");
}

void TestPreampLimiterContainment()
{
    std::cout << "[Dynamics] Main limiter after drive; raw direct stems stay identical ..." << std::endl;
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        for (int quality : { 0, 1, 2 }) {
            EngineParameters p;
            p.preampDrive = .9f;
            p.warmth = 1.0f;
            p.compAttack = 50.0f;
            p.compRelease = 20.0f;
            p.volume = 1.0f;
            p.oversampling = quality;
            p.stereoWidth = 2.0f;
            auto chord = [&](float amount) {
                MarembaEngine engine(rate);
                p.compAmount = amount;
                engine.SetParameters(p);
                for (int n = 48; n < 64; ++n) engine.NoteOn(n, 1.0f);
                Render out;
                RenderFrames(engine, out, static_cast<int>(rate * .12));
                return out;
            };
            const Render dry = chord(0.0f), limited = chord(1.0f);
            CHECK(AllFinite(limited), "limiter engine path produces non-finite audio");
            CHECK(std::max(Peak(limited.ch[kMainL]), Peak(limited.ch[kMainR]))
                      <= maremba::AnalogLimiter::kOutputCeiling + 1e-6f,
                  "final main bus exceeds sample ceiling at " << rate << "/" << quality);
            CHECK(DiffEnergy(dry.ch[kMainL], limited.ch[kMainL], 0, dry.size()) > 1e-5,
                  "limiter is not active on the main bus");
            for (int channel = kCloseL; channel < kNumChannels; ++channel)
                CHECK(dry.ch[channel] == limited.ch[channel], "limiter leaked into direct stem " << channel);
        }
    }
}

void TestLegacyPitchGlideIgnored()
{
    std::cout << "[Legacy Glide] Stored/automated glide is inert; each attack starts at its tuned pitch ..." << std::endl;
    double maxOnsetError = 0.0;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int model = 0; model < 4; ++model) {
            auto phrase = [&](float legacy) {
                EngineParameters p = CleanParams();
                p.model = model;
                p.pitchGlide = legacy;
                p.tuneCents = 37.0f;
                MarembaEngine engine(rate);
                engine.SetParameters(p);
                CHECK(engine.GetParameters().pitchGlide == 0.0f, "legacy glide remains effective");
                Render r;
                engine.NoteOn(48, .3f);
                RenderFrames(engine, r, static_cast<int>(rate * .02), &p);
                engine.NoteOn(81, 1.0f); // different note must not slide from the old pitch
                RenderFrames(engine, r, static_cast<int>(rate * .04), &p);
                p.pitchGlide = legacy > 0 ? 1.0f - legacy : 0.0f;
                engine.SetParameters(p); // automation while ringing is also inert
                engine.NoteOn(81, .8f);  // repeated note must not restart a sweep
                RenderFrames(engine, r, static_cast<int>(rate * .04), &p);
                engine.SetPitchBendCents(100.0f); // ordinary pitch bend remains functional
                RenderFrames(engine, r, static_cast<int>(rate * .04), &p);
                return r;
            };
            const Render off = phrase(0.0f);
            for (float legacy : {.3f, 1.0f}) {
                const Render on = phrase(legacy);
                for (int channel = 0; channel < kNumChannels; ++channel)
                    CHECK(on.ch[channel] == off.ch[channel], "legacy glide changes audio at rate " << rate << " model " << model);
            }
            for (int note : {69, 81}) {
                EngineParameters p = CleanParams();
                p.model = model;
                p.sympathetic = p.bodyBloom = p.buzzAmount = 0.0f;
                p.pitchGlide = 1.0f;
                p.tuneCents = 37.0f;
                const Render strike = RenderNote(rate, p, note, 1.0f, .05);
                const double target = NoteHz(note) * std::exp2(37.0 / 1200.0);
                const double f = EstimateFrequency(strike.ch[kPiezo], strike.ch[kPiezo],
                    static_cast<size_t>(rate * .004), static_cast<size_t>(rate * .024), rate, target);
                const double error = std::fabs(Cents(f, target));
                maxOnsetError = std::max(maxOnsetError, error);
                CHECK(error < 1.0, "new strike starts off pitch: " << error << " cents, model " << model << " note " << note);
            }
        }
    }
    // Public voice entry point also ignores the legacy argument, even without
    // the wrapper/engine sanitizer. This catches a reintroduced per-sample sweep.
    for (int model = 0; model < 4; ++model) {
        maremba::MarembaVoice off, on;
        maremba::FastPRNG a(123), b(123);
        for (int note : {60, 81, 81}) {
            off.Trigger(note, 1, static_cast<maremba::EMarimbaModel>(model), .4f, .35f, 0, .7f, 1, 0, 0, 0, 88200, a, 0, 0, 1, 0);
            on.Trigger(note, 1, static_cast<maremba::EMarimbaModel>(model), .4f, .35f, 0, .7f, 1, 0, 0, 1, 88200, b, 0, 0, 1, 0);
            bool identical = true;
            for (int frame = 0; frame < 4410; ++frame) {
                float c0, f0, p0, c1, f1, p1;
                off.ProcessSample(c0, f0, p0);
                on.ProcessSample(c1, f1, p1);
                identical = identical && c0 == c1 && f0 == f1 && p0 == p1;
            }
            CHECK(identical, "raw voice still applies legacy glide, model " << model << " note " << note);
        }
    }
    std::cout << "  -> maximum first 4-24 ms pitch error: " << maxOnsetError << " cents" << std::endl;
}

void TestSympatheticHalo()
{
    std::cout << "[Halo] Sympathetic mesh adds a tail (differential) ..." << std::endl;
    EngineParameters p = CleanParams();
    p.decay = 0.5f;
    p.sympathetic = 0.0f;
    Render off = RenderNote(44100.0, p, 60, 0.9f, 1.0);
    p.sympathetic = 0.8f;
    Render on = RenderNote(44100.0, p, 60, 0.9f, 1.0);
    const double rel = DiffEnergy(on.ch[kMainL], off.ch[kMainL], 0, on.size()) / Energy(off.ch[kMainL], 0, off.size());
    std::cout << "  -> halo " << 10.0 * std::log10(rel) << " dB re bar" << std::endl;
    CHECK(rel > 1e-4, "sympathetic halo inaudible");
}

void TestBodyBloom()
{
    std::cout << "[Body] Frame body bloom level (differential, ~-13 dB below the bar) ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    Render off = RenderNote(44100.0, p, 60, 0.8f, 0.6);
    p.bodyBloom = 1.0f;
    Render on = RenderNote(44100.0, p, 60, 0.8f, 0.6);
    std::vector<float> body(off.size());
    for (size_t i = 0; i < body.size(); ++i) body[i] = on.ch[kCloseL][i] - off.ch[kCloseL][i];
    const double db = Db(Peak(body) / Peak(off.ch[kCloseL]));
    std::cout << "  -> body bloom at 1.0: " << db << " dB re bar peak" << std::endl;
    CHECK(db > -18.0 && db < -8.0, "body bloom level out of range: " << db);
}

void TestVelocityAndMixerSilence()
{
    std::cout << "[Mixer] All mic levels at 0 are silent; velocity scales energy ..." << std::endl;
    EngineParameters silent;
    silent.closeLevel = silent.farLevel = silent.piezoLevel = 0.0f;
    silent.sympathetic = 1.0f;
    silent.bodyBloom = 1.0f;
    Render s = RenderNote(44100.0, silent, 60, 1.0f, 0.15);
    CHECK(Peak(s.ch[kMainL]) < 1e-7 && Peak(s.ch[kMainR]) < 1e-7, "mixer leaked with all levels at 0");

    EngineParameters p = CleanParams();
    p.farLevel = p.piezoLevel = 0.0f;
    Render soft = RenderNote(44100.0, p, 60, 0.15f, 0.1);
    Render hard = RenderNote(44100.0, p, 60, 1.0f, 0.1);
    const double ratio = Energy(hard.ch[kMainL], 0, hard.size()) / Energy(soft.ch[kMainL], 0, soft.size());
    CHECK(ratio > 10.0, "velocity sensitivity too weak: " << ratio);
}

void TestDetune()
{
    std::cout << "[Detune] Per-key drift at detune 100 ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    Render exact = RenderNote(44100.0, p, 60, 0.8f, 1.0);
    p.detune = 100.0f;
    Render drift = RenderNote(44100.0, p, 60, 0.8f, 1.0);
    const double f0 = NoteHz(60);
    const double c0 = Cents(EstimateFrequency(exact.ch[kCloseL], exact.ch[kCloseR], 0, exact.size(), 44100.0, f0), f0);
    const double c1 = Cents(EstimateFrequency(drift.ch[kCloseL], drift.ch[kCloseR], 0, drift.size(), 44100.0, f0 * std::exp2(62.43 / 1200.0)), f0);
    std::cout << "  -> exact " << c0 << " c, detune 100: " << c1 << " c (table +62.43 c)" << std::endl;
    CHECK(std::fabs(c0) < 1.0, "detune 0 is not exact");
    CHECK(std::fabs(c1 - 62.43) < 1.0, "detune drift mismatch");
}

void TestStrikerHierarchy()
{
    std::cout << "[Strikers] Mallet / thumb brightness hierarchy ..." << std::endl;
    for (int model : { 0, 3 }) {
        double energies[4];
        for (int m = 0; m < 4; ++m) {
            EngineParameters p;
            p.model = model;
            p.malletType = m;
            Render r = RenderNote(44100.0, p, 60, 0.85f, model == 3 ? 0.025 : 0.5);
            CHECK(Peak(r.ch[kMainL]) > 0.05 && Peak(r.ch[kMainL]) < 1.0, "striker " << m << " peak out of range");
            energies[m] = HighPassEnergy(r.ch[kMainL], 0, r.size());
        }
        CHECK(energies[0] < energies[1] && energies[1] < energies[2] && energies[2] < energies[3],
              "brightness hierarchy broken for model " << model);
    }
}

void TestKalimbaSustain()
{
    std::cout << "[Kalimba] Tine sustain ..." << std::endl;
    EngineParameters p;
    p.model = 3;
    Render r = RenderNote(44100.0, p, 60, 0.85f, 2.1);
    CHECK(Peak(r.ch[kMainL]) > 0.05 && Peak(r.ch[kMainL]) < 0.9, "Kalimba peak out of range");
    EngineParameters w = p;
    w.model = 1; // Padauk: the shortest wooden bar
    Render wood = RenderNote(44100.0, w, 60, 0.85f, 2.1);
    const double tine = Rms(r.ch[kMainL], 44100 * 2 - 2000, 44100 * 2);
    std::cout << "  -> level at 2 s: Kalimba " << Db(tine) << " dBFS, Padauk " << Db(Rms(wood.ch[kMainL], 44100 * 2 - 2000, 44100 * 2)) << " dBFS" << std::endl;
    CHECK(tine > 1e-4, "Kalimba did not sustain to 2 s");
    CHECK(tine > 2.0 * Rms(wood.ch[kMainL], 44100 * 2 - 2000, 44100 * 2), "Kalimba tine does not outlast the Padauk bar");
}

void TestStrikeJitter()
{
    std::cout << "[Jitter] Strike variance is differential and stable ..." << std::endl;
    // Strike note 60, let the engine go fully idle, strike it again: the second
    // strike draws later PRNG values. Zero keeps the natural contact floor;
    // raising Variance should add a larger expressive range.
    auto measure = [](float jitter) {
        MarembaEngine engine(44100.0);
        EngineParameters p;
        p.artifacts = 0.0f;
        p.strikeJitter = jitter;
        p.sympathetic = 0.0f;
        p.farLevel = 0.0f;
        p.decay = 0.1f;
        engine.SetParameters(p);
        Render first, gap, second;
        engine.NoteOn(60, 0.8f);
        RenderFrames(engine, first, 64 * 10);
        for (int b = 0; b < 44100 * 20 / 64 && !engine.IsSilent(); ++b) RenderFrames(engine, gap, 64);
        CHECK(engine.IsSilent(), "engine never idles between strikes");
        engine.NoteOn(60, 0.8f);
        RenderFrames(engine, second, 64 * 10);
        return DiffEnergy(first.ch[kCloseL], second.ch[kCloseL], 0, first.size()) / Energy(first.ch[kCloseL], 0, first.size());
    };
    const double j0 = measure(0.0f);
    const double j50 = measure(50.0f);
    std::cout << "  -> relative strike-to-strike difference: jitter 0 " << j0 << ", jitter 50 " << j50 << std::endl;
    CHECK(j50 > j0 * 1.05, "strikeJitter has no effect");

    // Maximum jitter is stable on every model and octave
    MarembaEngine engine(44100.0);
    for (int m = 0; m < 4; ++m) {
        EngineParameters p;
        p.model = m;
        p.strikeJitter = 100.0f;
        p.artifacts = 1.0f;
        engine.SetParameters(p);
        Render r;
        for (int n = 24; n <= 108; n += 12) {
            engine.NoteOn(n, 0.9f);
            RenderFrames(engine, r, 64 * 8);
        }
        CHECK(AllFinite(r) && Peak(r.ch[kMainL]) < 1.5, "instability at strikeJitter 100, model " << m);
    }
}

// Remove the best scalar gain before comparing waveforms. This rejects a
// louder/quieter clone as evidence of changing contact timbre or phase.
double GainFitResidual(const std::vector<float>& a, const std::vector<float>& b,
                       size_t from, size_t to)
{
    double aa = 0.0, ab = 0.0, bb = 0.0;
    to = std::min({to, a.size(), b.size()});
    for (size_t i = from; i < to; ++i) {
        aa += static_cast<double>(a[i]) * a[i];
        ab += static_cast<double>(a[i]) * b[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    // A silent analysis window provides no evidence of varying pitched tone.
    // Returning a maximal residual here could let attack-only output pass.
    if (aa == 0.0 || bb == 0.0) return 0.0;
    return std::sqrt(std::max(0.0, 1.0 - ab * ab / (aa * bb)));
}

double Median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct RepeatMetrics {
    double attack, ring, spectrumDb, levelDb, pitchCents, minAdjacent, minLag;
};

RepeatMetrics MeasureRepeats(const std::vector<std::vector<float>>& hits, int model,
                            double sampleRate, bool measurePitch)
{
    const size_t attackEnd = static_cast<size_t>(0.008 * sampleRate);
    const size_t ringStart = static_cast<size_t>(0.040 * sampleRate);
    const size_t ringEnd = static_cast<size_t>(0.160 * sampleRate);
    const auto instrument = static_cast<maremba::EMarimbaModel>(model);
    const auto barGeometry = maremba::GetBarModeGeometry(instrument, 60.0);
    const auto mount = maremba::GetKalimbaMount(NoteHz(60));
    const auto keyVariation = maremba::GetKeyVariation(instrument, 60);
    std::vector<std::array<double, 4>> spectra;
    std::vector<double> levels, pitch, attack, ring, spectral;
    for (const auto& hit : hits) {
        levels.push_back(Db(Rms(hit, ringStart, ringEnd)));
        if (measurePitch) pitch.push_back(Cents(EstimateFrequency(hit, hit, ringStart, ringEnd, sampleRate, NoteHz(60)), NoteHz(60)));
        std::vector<double> segment(hit.begin() + static_cast<size_t>(0.008 * sampleRate),
                                    hit.begin() + static_cast<size_t>(0.045 * sampleRate));
        segment = HannWindowed(segment);
        std::array<double, 4> powers{};
        // Measure the actual solved partial frequencies, including the fixed
        // per-key tuning scatter. A nominal-frequency bin can miss a weak
        // partial and mistake spectral leakage for the varying pitched mode.
        for (size_t mode = 0; mode < powers.size(); ++mode)
            powers[mode] = GoertzelPower(segment, sampleRate, NoteHz(60)
                * (model == 3 ? mount.ratio[mode] : maremba::BarModeRatio(barGeometry, static_cast<int>(mode)))
                * keyVariation.partial[mode]);
        spectra.push_back(powers);
    }
    for (size_t h = 1; h < hits.size(); ++h) {
        attack.push_back(GainFitResidual(hits[h - 1], hits[h], 0, attackEnd));
        ring.push_back(GainFitResidual(hits[h - 1], hits[h], ringStart, ringEnd));
        double change = 0.0;
        int count = 0;
        for (size_t mode = 1; mode < spectra[h].size(); ++mode) {
            const double before = spectra[h - 1][mode] / spectra[h - 1][0];
            const double after = spectra[h][mode] / spectra[h][0];
            // Ignore effectively absent partials and leakage at symmetry nodes.
            if (std::min(before, after) < 1e-5) continue;
            const double delta = 10.0 * std::log10(after / before);
            change += delta * delta;
            ++count;
        }
        spectral.push_back(count ? std::sqrt(change / count) : 0.0);
    }
    double lagFloor = 1.0;
    for (size_t lag = 1; lag <= 4; ++lag) {
        std::vector<double> differences;
        for (size_t h = lag; h < hits.size(); ++h)
            differences.push_back(GainFitResidual(hits[h - lag], hits[h], ringStart, ringEnd));
        lagFloor = std::min(lagFloor, Median(differences));
    }
    return {Median(attack), Median(ring), Median(spectral),
            *std::max_element(levels.begin(), levels.end()) - *std::min_element(levels.begin(), levels.end()),
            measurePitch ? *std::max_element(pitch.begin(), pitch.end()) - *std::min_element(pitch.begin(), pitch.end()) : 0.0,
            *std::min_element(ring.begin(), ring.end()), lagFloor};
}

void TestOrganicRepeatVariation(bool measurementsOnly = false)
{
    std::cout << "[Organic strikes] Same note/velocity with Variance and Artifacts at zero ..." << std::endl;
    constexpr double rate = 88200.0;
    constexpr int hitCount = 16;
    const std::vector<float> silence(1600, 0.0f), constant(1600, 0.1f);
    CHECK(GainFitResidual(silence, silence, 0, silence.size()) == 0.0
          && GainFitResidual(silence, constant, 0, silence.size()) == 0.0,
          "silent windows count as evidence of tonal variation");
    // All comparisons hold the local noise sequence identical. FastKill clears
    // every resonator without touching the shared event PRNG. For restrikes a
    // copied ringing anchor also holds old-tail phase/energy identical, so old
    // tails and noise cannot explain the observed differences.
    auto renderVoice = [](maremba::MarembaVoice& voice, int frames) {
        std::vector<float> audio;
        audio.reserve(frames);
        for (int i = 0; i < frames; ++i) {
            float close, far, piezo;
            voice.ProcessSample(close, far, piezo);
            if ((i & 31) == 31) voice.UpdateEnvelope();
            CHECK(std::isfinite(close) && std::isfinite(far) && std::isfinite(piezo), "non-finite organic strike");
            audio.push_back(piezo);
        }
        return audio;
    };
    std::cout << "REPEAT,model,velocity,position,path,attack_residual,ring_residual,spectral_delta_db,level_span_db,pitch_span_cents,min_adjacent,min_lag_1_to_4" << std::endl;
    for (int model = 0; model < 4; ++model) {
        for (int velocity : {48, 112}) {
            for (float position : {0.10f, 0.50f}) {
                for (int path = 0; path < 3; ++path) {
                    const bool restrike = path == 2;
                    maremba::FastPRNG eventRandom(0x12345678), anchorRandom(0xA34B29CD);
                    maremba::MarembaVoice anchor, voice;
                    auto trigger = [&](maremba::MarembaVoice& target, float v, maremba::FastPRNG& random) {
                        target.Trigger(60, v, static_cast<maremba::EMarimbaModel>(model),
                                       0.60f, position, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                       rate, random, 0.0f, 0.0f, 1, 0.0f);
                    };
                    if (restrike) {
                        anchor.SetSeed(0x2468ACE1);
                        trigger(anchor, 0.4f * velocity / 127.0f, anchorRandom);
                        renderVoice(anchor, static_cast<int>(0.060 * rate));
                        CHECK(anchor.IsActive(), "restrike anchor unexpectedly died");
                    }
                    std::vector<std::vector<float>> hits;
                    for (int hit = 0; hit < hitCount; ++hit) {
                        if (restrike) voice = anchor;
                        else {
                            if (path == 1) voice = maremba::MarembaVoice();
                            else voice.FastKill();
                            voice.SetSeed(0x2468ACE1); // freeze noise, not event randomness
                        }
                        trigger(voice, velocity / 127.0f, eventRandom);
                        hits.push_back(renderVoice(voice, static_cast<int>(0.180 * rate)));
                    }
                    const RepeatMetrics m = MeasureRepeats(hits, model, rate, true);
                    std::cout << "REPEAT," << model << ',' << velocity << ',' << position << ','
                              << (restrike ? "restrike" : path == 1 ? "new_voice" : "cleared") << ',' << m.attack << ',' << m.ring << ','
                              << m.spectrumDb << ',' << m.levelDb << ',' << m.pitchCents << ',' << m.minAdjacent << ',' << m.minLag << std::endl;
                    CHECK(m.levelDb < 0.6 && m.pitchCents < 0.3, "organic floor changes level/pitch too far, model " << model);
                    CHECK(m.attack < 0.08 && m.ring < 0.06 && m.spectrumDb < 1.0, "organic floor changes tone too far, model " << model);
                    if (!measurementsOnly) {
                        // Regression floors distinguish tonal contact changes
                        // from numerical/noise residue; they are not calibrated
                        // perceptual thresholds. Lags 1..4 cover short loops in
                        // this 16-hit phrase, not a claim of infinite aperiodicity.
                        CHECK(m.attack > 0.0005 && m.ring > 0.0005,
                              "no substantive gain-independent contact change, model " << model << " position " << position);
                        CHECK(m.minAdjacent > 0.00001 && m.minLag > m.ring * 0.15,
                              "organic strikes repeat or form a short pattern, model " << model);
                        if (position < 0.2f)
                            CHECK(m.spectrumDb > 0.005, "partial balance does not vary, model " << model);
                    }
                    // Negative controls: scalar gain and changed attack noise
                    // alone must not satisfy the late pitched-waveform metric.
                    auto changed = hits.front();
                    for (float& sample : changed) sample *= 1.01f;
                    CHECK(GainFitResidual(hits.front(), changed, static_cast<size_t>(0.040 * rate), static_cast<size_t>(0.160 * rate)) < 1e-6,
                          "gain-fit metric accepts scalar gain as timbre");
                    changed = hits.front();
                    for (size_t i = 0; i < static_cast<size_t>(0.008 * rate); ++i) changed[i] += 0.01f * std::sin(i * 0.7f);
                    CHECK(GainFitResidual(hits.front(), changed, static_cast<size_t>(0.040 * rate), static_cast<size_t>(0.160 * rate)) < 1e-6,
                          "late-ring metric accepts attack noise alone");
                }
            }
        }
    }

    // Exercise the public engine path too: natural wake after full silence,
    // continued PRNG state, then exact seven-output replay after explicit Reset.
    for (int model = 0; model < 4; ++model) {
        EngineParameters p = CleanParams();
        p.model = model;
        p.decay = 0.1f;
        p.sympathetic = p.bodyBloom = p.farLevel = p.buzzAmount = 0.0f;
        p.preampDrive = p.warmth = p.compAmount = 0.0f;
        MarembaEngine engine(44100.0);
        engine.SetParameters(p);
        std::vector<Render> firstPass;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass) engine.Reset();
            for (int hit = 0; hit < 8; ++hit) {
                CHECK(engine.IsSilent(), "repeat note must wake from full engine silence");
                engine.NoteOn(60, 90.0f / 127.0f);
                Render audio;
                RenderFrames(engine, audio, 7938);
                CHECK(AllFinite(audio) && Rms(audio.ch[kPiezo], 0, audio.size()) > 1e-5, "idle engine failed to wake audibly");
                if (!pass) firstPass.push_back(audio);
                else for (int channel = 0; channel < kNumChannels; ++channel)
                    CHECK(audio.ch[channel] == firstPass[hit].ch[channel], "Reset did not reproduce organic phrase exactly");
                float discard[kNumChannels][64];
                int batches = 0;
                while (!engine.IsSilent() && batches++ < 44100 * 25 / 64)
                    engine.RenderBatch(discard[0], discard[1], discard[2], discard[3], discard[4], discard[5], discard[6], 64);
                CHECK(engine.IsSilent(), "repeat probe engine never reaches natural silence");
            }
        }
        std::vector<std::vector<float>> hits;
        for (const auto& r : firstPass) hits.push_back(r.ch[kPiezo]);
        const RepeatMetrics m = MeasureRepeats(hits, model, 44100.0, false);
        std::cout << "REPEAT," << model << ",90,0.5,engine_idle," << m.attack << ',' << m.ring << ',' << m.spectrumDb
                  << ',' << m.levelDb << ",0," << m.minAdjacent << ',' << m.minLag << std::endl;
        if (!measurementsOnly)
            CHECK(m.ring > 0.0005 && m.minAdjacent > 0.00001 && m.minLag > m.ring * 0.15,
                  "engine silence resets organic variation, model " << model);
    }
}

void TestMeshCoupling()
{
    std::cout << "[Mesh] Mechanical neighbors respond; resonant selectivity rejects remote frequencies ..." << std::endl;
    maremba::SympatheticMesh mesh;
    mesh.Configure(88200.0);
    double energy[maremba::SympatheticMesh::kNumResonators] = {};
    // Transmit a C4 component from B3: only OTHER keys may respond.
    for (int s = 0; s < 44100; ++s) {
        mesh.BeginSample();
        mesh.AccumulateVoice(59, static_cast<float>(std::sin(2.0 * M_PI * NoteHz(60) * s / 88200.0)));
        float l, r;
        mesh.Process(1.0f, l, r);
        if (s >= 22050) for (int i = 0; i < maremba::SympatheticMesh::kNumResonators; ++i) {
            const double out = mesh.GetResonatorOutput(i);
            energy[i] += out * out;
        }
    }
    const auto response = [&](int note) { return energy[note - maremba::SympatheticMesh::kFirstNote]; };
    CHECK(response(59) == 0.0, "support duplicates the driven key");
    CHECK(response(60) > 0.0, "neighboring resonant receiver not excited");
    CHECK(response(58) > 0.0 && response(61) > 0.0, "support suppresses nearby keys artificially");
    CHECK(response(60) > 4.0 * std::max(response(58), response(61)), "support loses resonance selectivity");
    CHECK(response(60) > 100.0 * response(54), "remote detuned support muddies the played note");

    for (int n = 0; n < 128; ++n) {
        const auto& list = mesh.GetCouplingList(n);
        CHECK(list.count <= maremba::VoiceCouplingList::kCapacity, "coupling list overflow");
        int previous = -1;
        for (int k = 0; k < list.count; ++k) {
            const int index = list.entries[k].resonatorIdx;
            const int target = maremba::SympatheticMesh::kFirstNote + index;
            CHECK(index > previous && index < maremba::SympatheticMesh::kNumResonators, "invalid or duplicate receiver");
            CHECK(target != n && std::abs(target - n) <= 12, "invalid support neighborhood");
            CHECK(list.entries[k].weight > 0.0f && list.entries[k].weight <= 1.0f, "invalid transmission weight");
            previous = index;
        }
    }
}

void TestRestrikeContinuity()
{
    std::cout << "[Restrike] Soft restrike damps the ring gradually (no instant cut) ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    p.malletType = 0; // soft yarn: long contact
    auto run = [&](bool restrike) {
        MarembaEngine engine(44100.0);
        engine.SetParameters(p);
        engine.NoteOn(45, 1.0f);
        Render r;
        RenderFrames(engine, r, 64 * 15);
        if (restrike) engine.NoteOn(45, 0.15f);
        RenderFrames(engine, r, 64 * 4);
        return r;
    };
    Render cont = run(false);
    Render hit = run(true);
    const size_t at = 64 * 15;
    const double ringPeak = Peak(cont.ch[kCloseL], at, at + 64);
    // Within the first 0.2 ms the restruck bar must still be close to the continuation
    double early = 0.0;
    for (size_t i = at; i < at + 8; ++i) early = std::max(early, static_cast<double>(std::fabs(hit.ch[kCloseL][i] - cont.ch[kCloseL][i])));
    std::cout << "  -> first 8 samples: max deviation " << early / ringPeak * 100.0 << "% of the ring" << std::endl;
    CHECK(early < 0.2 * ringPeak, "restrike cuts the ringing bar instantly");
    // ... and the damping does happen over the contact (end level well below the continuation)
    const double after = Rms(hit.ch[kCloseL], at + 64 * 3, at + 64 * 4) / Rms(cont.ch[kCloseL], at + 64 * 3, at + 64 * 4);
    CHECK(after < 0.9, "restrike damping missing: " << after);
}

void TestVoicesDieAndEngineIdles()
{
    std::cout << "[Lifetime] Voices deactivate, IsSilent() and exact zeros after decay ..." << std::endl;
    MarembaEngine engine(44100.0);
    EngineParameters p;
    engine.SetParameters(p);
    CHECK(engine.IsSilent(), "fresh engine not silent");
    for (int n : { 60, 64, 67, 72, 84 }) engine.NoteOn(n, 1.0f);
    CHECK(!engine.IsSilent(), "engine silent while notes play");
    Render r;
    int seconds = 0;
    while (seconds < 20 && !engine.IsSilent()) {
        RenderFrames(engine, r, 44100);
        ++seconds;
    }
    std::cout << "  -> idle after " << seconds << " s, active voices " << engine.GetActiveVoiceCount() << std::endl;
    CHECK(engine.GetActiveVoiceCount() == 0, "voices never deactivate");
    CHECK(engine.IsSilent(), "engine never reaches IsSilent()");
    Render tail;
    RenderFrames(engine, tail, 64 * 4);
    CHECK(Peak(tail.ch[kMainL]) == 0.0 && Peak(tail.ch[kFarL]) == 0.0, "outputs not exactly zero when idle");

    // No premature cut of a quiet low note (zero crossings must not end it early)
    EngineParameters k = CleanParams();
    k.model = 3;
    MarembaEngine low(44100.0);
    low.SetParameters(k);
    low.NoteOn(21, 0.1f);
    Render lr;
    int blocks = 0;
    while (low.GetActiveVoiceCount() > 0 && blocks < 44100 * 60 / 64) {
        RenderFrames(low, lr, 64);
        ++blocks;
    }
    const size_t end = lr.size();
    const double lastPeak = Peak(lr.ch[kCloseL], end > 2205 ? end - 2205 : 0, end);
    std::cout << "  -> Kalimba A0 vel 0.1: freed after " << end / 44100.0 << " s, last 50 ms peak " << Db(lastPeak) << " dBFS" << std::endl;
    CHECK(low.GetActiveVoiceCount() == 0, "quiet low note never freed");
    CHECK(lastPeak < 3e-5, "voice freed while still audible (premature cut)");
}

void TestFarReverbWithPerBatchParameters()
{
    std::cout << "[Room] Far reverb tail survives SetParameters every batch ..." << std::endl;
    EngineParameters p = CleanParams();
    p.closeLevel = 0.0f;
    p.piezoLevel = 0.0f;
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    p.decay = 0.1f;
    auto run = [&](float far) {
        p.farLevel = far;
        MarembaEngine engine(44100.0);
        engine.SetParameters(p);
        engine.NoteOn(96, 1.0f);
        Render r;
        RenderFrames(engine, r, 44100, &p);
        return r;
    };
    Render room = run(1.0f);
    Render dry = run(0.0005f); // room bypassed, dry path identical (0.7x)
    const double wet = Rms(room.ch[kFarL], 44100 * 3 / 10, 44100 * 6 / 10);
    const double dryTail = Rms(dry.ch[kFarL], 44100 * 3 / 10, 44100 * 6 / 10);
    std::cout << "  -> far tail 0.3-0.6 s: room " << Db(wet) << " dBFS, dry " << Db(dryTail) << " dBFS" << std::endl;
    CHECK(wet > 10.0 * dryTail && wet > 1e-5, "far room reverb tail missing");
    // Continuous dry gain: the room on/off switch at level 0 must not jump the direct level
    const double onsetRoom = Peak(room.ch[kFarL], 0, 400), onsetDry = Peak(dry.ch[kFarL], 0, 400);
    CHECK(std::fabs(Db(onsetRoom / onsetDry)) < 0.5, "far dry path not continuous across the room bypass");
}

void TestNoAllocations()
{
    std::cout << "[Realtime] Zero heap allocations in SetParameters / RenderBatch / notes ..." << std::endl;
    MarembaEngine engine(48000.0);
    EngineParameters p;
    engine.SetParameters(p);
    float buf[kNumChannels][64];
    auto render = [&](int batches) {
        for (int b = 0; b < batches; ++b) {
            engine.SetParameters(p);
            engine.RenderBatch(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], 64);
        }
    };
    gAllocationCount = 0;
    gCountAllocations = true;
    for (int n = 40; n < 70; ++n) engine.NoteOn(n, 0.9f);
    render(20);
    p.oversampling = 2; render(20);   // 2x -> 8x while ringing (fade + switch)
    p.oversampling = 1; render(20);
    p.polyphony = 0; render(20);      // 24 -> 8 voices
    p.polyphony = 2; render(5);
    p.model = 3; p.tuneCents = 30.0f; render(5);
    engine.SetPitchBendCents(150.0f); render(5);
    engine.DampAll(); render(20);
    engine.Reset(); render(2);
    gCountAllocations = false;
    CHECK(gAllocationCount == 0, gAllocationCount << " heap allocations on the realtime path");

    // The trap itself sees over-aligned and nothrow allocations
    struct alignas(64) Aligned { float x[16]; };
    static Aligned* volatile sinkAligned = nullptr;
    static float* volatile sinkNothrow = nullptr;
    gAllocationCount = 0;
    gCountAllocations = true;
    sinkAligned = new Aligned;
    sinkNothrow = new (std::nothrow) float[8];
    gCountAllocations = false;
    delete sinkAligned;
    delete[] sinkNothrow;
    CHECK(gAllocationCount == 2, "allocation trap misses aligned/nothrow new: " << gAllocationCount << " of 2");
}

void TestPitchAccuracy()
{
    std::cout << "[Tuning] Notes 24..108 settle within +/-1 cent (onset +/-2) at 44.1/48/96/192 kHz x 2/4/8 ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    p.farLevel = 0.0f;
    double worst = 0.0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (int os = 0; os < 3; ++os) {
            p.oversampling = os;
            for (int note = 24; note <= 108; note += 12) {
                const double seconds = note < 48 ? 1.0 : 0.3;
                Render r = RenderNote(sr, p, note, 0.8f, seconds + 0.15);
                // While the tube blooms, its field (in quadrature with the
                // bar's) takes over the fundamental and the summed phase drifts
                // by up to ~1.5 cents' worth; the settled pitch stays exact.
                const size_t onsetEnd = static_cast<size_t>(seconds * sr);
                const size_t settle = static_cast<size_t>(0.15 * sr);
                const double onset = Cents(EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], 0, onsetEnd, sr, NoteHz(note)), NoteHz(note));
                const double c = Cents(EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], settle, r.size(), sr, NoteHz(note)), NoteHz(note));
                worst = std::max(worst, std::fabs(c));
                CHECK(std::fabs(c) < 1.0, "note " << note << " at " << sr << "/" << (2 << os) << "x settles " << c << " cents off");
                CHECK(std::fabs(onset) < 2.0, "note " << note << " at " << sr << "/" << (2 << os) << "x onset is " << onset << " cents off");
            }
        }
    }
    std::cout << "  -> worst deviation " << worst << " cents" << std::endl;
}

void TestRateInvariance()
{
    std::cout << "[Invariance] Level (+/-0.5 dB per output), pitch and T60 across rates/factors ..." << std::endl;
    EngineParameters p = CleanParams();
    const double configs[6][2] = { { 44100, 0 }, { 44100, 1 }, { 44100, 2 }, { 48000, 0 }, { 96000, 0 }, { 192000, 0 } };
    for (int note : { 36, 60, 84 }) {
        double refDb[kNumChannels] = {};
        double refT60 = 0.0;
        for (int c = 0; c < 6; ++c) {
            const double sr = configs[c][0];
            p.oversampling = static_cast<int>(configs[c][1]);
            Render r = RenderNote(sr, p, note, 0.8f, 1.4);
            const size_t half = static_cast<size_t>(0.5 * sr);
            const double t60 = 60.0 * 0.9 / Db(Rms(r.ch[kCloseL], static_cast<size_t>(0.3 * sr), static_cast<size_t>(0.4 * sr))
                                            / Rms(r.ch[kCloseL], static_cast<size_t>(1.2 * sr), static_cast<size_t>(1.3 * sr)));
            const double f = EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], 0, static_cast<size_t>(0.6 * sr), sr, NoteHz(note));
            CHECK(std::fabs(Cents(f, NoteHz(note))) < 1.0, "pitch drift at " << sr << " os " << p.oversampling);
            if (c == 0) refT60 = t60;
            CHECK(std::fabs(t60 / refT60 - 1.0) < 0.05, "T60 of note " << note << " changes with rate: " << t60 << " vs " << refT60);
            for (int ch = 0; ch < kNumChannels; ++ch) {
                const double db = Db(Rms(r.ch[ch], 0, half));
                if (c == 0) refDb[ch] = db;
                CHECK(std::fabs(db - refDb[ch]) < 0.5, kChannelNames[ch] << " level of note " << note << " at " << sr << "/"
                      << (2 << p.oversampling) << "x differs by " << db - refDb[ch] << " dB");
            }
        }
    }
}

void TestOversamplingSwitch()
{
    std::cout << "[Switch] Oversampling change while ringing: no pitch jump, no click ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;

    // (a) Switch alone: the ringing note fades out over ~5 ms at the old rate, no click
    {
        EngineParameters q = p;
        MarembaEngine engine(48000.0);
        engine.SetParameters(q);
        engine.NoteOn(57, 0.9f);
        Render r;
        RenderFrames(engine, r, 48000 / 4, &q);
        const size_t switchAt = r.size();
        q.oversampling = 2;          // 2x -> 8x
        RenderFrames(engine, r, 48000 / 4, &q);
        double stepBefore = 0.0, stepSwitch = 0.0;
        for (size_t i = switchAt - 2400; i < switchAt; ++i) stepBefore = std::max(stepBefore, static_cast<double>(std::fabs(r.ch[kMainL][i] - r.ch[kMainL][i - 1])));
        for (size_t i = switchAt; i < switchAt + 2400; ++i) stepSwitch = std::max(stepSwitch, static_cast<double>(std::fabs(r.ch[kMainL][i] - r.ch[kMainL][i - 1])));
        std::cout << "  -> largest step: before " << stepBefore << ", during switch " << stepSwitch << std::endl;
        CHECK(stepSwitch <= stepBefore * 1.1, "click at oversampling switch");
        CHECK(Peak(r.ch[kMainL], switchAt + 480, r.size()) == 0.0, "old voices still sound after the switch");
        CHECK(engine.GetActiveVoiceCount() == 0, "voices survive the oversampling switch: " << engine.GetActiveVoiceCount());
    }

    // (b) A note in the same batch as the switch plays at the new rate, at its pitch
    {
        MarembaEngine engine(48000.0);
        engine.SetParameters(p);
        engine.NoteOn(57, 0.9f);
        Render r;
        RenderFrames(engine, r, 48000 / 4, &p);
        const size_t switchAt = r.size();
        p.oversampling = 2;
        engine.SetParameters(p);
        engine.NoteOn(57, 0.9f);
        RenderFrames(engine, r, 48000 / 2, &p);
        const double f = EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], switchAt + 2400, r.size(), 48000.0, 220.0);
        std::cout << "  -> pitch after switch " << f << " Hz" << std::endl;
        CHECK(std::fabs(Cents(f, 220.0)) < 1.0, "pitch after oversampling switch: " << f);
        std::vector<double> x;
        for (size_t i = switchAt + 2400; i < r.size(); ++i) x.push_back(r.ch[kCloseL][i]);
        CHECK(GoertzelPower(x, 48000.0, 880.0) < GoertzelPower(x, 48000.0, 220.0) * 1e-2, "energy 2 octaves up after the switch");
        CHECK(Peak(r.ch[kCloseL], switchAt + 480, r.size()) > 0.01, "note played across the switch is lost");
    }
}

void TestPolyphonyReduction()
{
    std::cout << "[Polyphony] Lowering polyphony fades the excess; raising it brings no ghosts ..." << std::endl;
    EngineParameters p = CleanParams();
    p.polyphony = 2;
    p.decay = 4.0f;
    auto run = [&](bool raiseAgain) {
        EngineParameters q = p;
        MarembaEngine engine(44100.0);
        engine.SetParameters(q);
        for (int n = 40; n < 64; ++n) engine.NoteOn(n, 0.9f);
        Render r;
        RenderFrames(engine, r, 44100 / 2, &q);
        CHECK(engine.GetActiveVoiceCount() == 24, "24 voices expected, got " << engine.GetActiveVoiceCount());
        q.polyphony = 0;
        RenderFrames(engine, r, 44100 / 20, &q); // 50 ms
        CHECK(engine.GetActiveVoiceCount() == 8, "excess voices not faded out: " << engine.GetActiveVoiceCount());
        if (raiseAgain) q.polyphony = 2;
        RenderFrames(engine, r, 44100 / 4, &q);
        CHECK(engine.GetActiveVoiceCount() == 8, "ghost voices came back");
        return r;
    };
    Render raised = run(true);
    Render kept = run(false);
    // Raising the limit again must not change the sound at all (no frozen voices resume)
    CHECK(DiffEnergy(raised.ch[kCloseL], kept.ch[kCloseL], 0, raised.size()) == 0.0, "ghost notes after raising polyphony");
    // The excess voices fade (no cut): no sample step larger than during the chord itself
    double stepChord = 0.0, stepFade = 0.0;
    const size_t at = 44100 / 2;
    for (size_t i = 1000; i < at; ++i) stepChord = std::max(stepChord, static_cast<double>(std::fabs(kept.ch[kCloseL][i] - kept.ch[kCloseL][i - 1])));
    for (size_t i = at; i < at + 441; ++i) stepFade = std::max(stepFade, static_cast<double>(std::fabs(kept.ch[kCloseL][i] - kept.ch[kCloseL][i - 1])));
    CHECK(stepFade <= stepChord, "polyphony reduction clicks: " << stepFade << " vs " << stepChord);

    // Stealing at the limit fades the victim in its slot; the new note always sounds
    p.polyphony = 0;
    MarembaEngine steal(44100.0);
    steal.SetParameters(p);
    Render s;
    for (int n = 0; n < 12; ++n) {
        steal.NoteOn(48 + n, 1.0f);
        RenderFrames(steal, s, 64 * 20, &p);
        CHECK(steal.GetActiveVoiceCount() <= 9, "too many voices: " << steal.GetActiveVoiceCount());
    }
    double maxStep = 0.0;
    for (size_t i = 1; i < s.size(); ++i) maxStep = std::max(maxStep, static_cast<double>(std::fabs(s.ch[kCloseL][i] - s.ch[kCloseL][i - 1])));
    CHECK(AllFinite(s) && maxStep < 0.5, "steal produced a discontinuity: " << maxStep);
}

void TestDampAll()
{
    std::cout << "[Stop] DampAll silences within 1.5 s without a low-frequency thump ..." << std::endl;
    EngineParameters p;
    p.decay = 8.0f;
    auto run = [&](bool damp) {
        MarembaEngine engine(44100.0);
        engine.SetParameters(p);
        for (int n : { 36, 43, 48, 55, 60, 64 }) engine.NoteOn(n, 1.0f);
        Render r;
        RenderFrames(engine, r, 44100 / 2, &p);
        if (damp) engine.DampAll();
        RenderFrames(engine, r, 44100 * 3 / 2, &p);
        return std::make_pair(r, engine.GetActiveVoiceCount());
    };
    auto undamped = run(false);
    auto damped = run(true);
    std::cout << "  -> active voices 1.5 s after DampAll: " << damped.second << std::endl;
    CHECK(damped.second == 0, "voices still sounding 1.5 s after DampAll");
    // Low-passed (~100 Hz) energy per 10 ms window never exceeds the undamped render
    auto lowPass = [](const std::vector<float>& x) {
        std::vector<double> y(x.size());
        double s1 = 0.0, s2 = 0.0;
        const double a = 1.0 - std::exp(-2.0 * M_PI * 100.0 / 44100.0);
        for (size_t i = 0; i < x.size(); ++i) { s1 += a * (x[i] - s1); s2 += a * (s1 - s2); y[i] = s2; }
        return y;
    };
    auto lu = lowPass(undamped.first.ch[kMainL]);
    auto ld = lowPass(damped.first.ch[kMainL]);
    int worse = 0;
    for (size_t w = 44100 / 2; w + 441 <= lu.size(); w += 441) {
        double eu = 0.0, ed = 0.0;
        for (size_t i = w; i < w + 441; ++i) { eu += lu[i] * lu[i]; ed += ld[i] * ld[i]; }
        if (ed > eu * 1.02 + 1e-12) ++worse;
    }
    CHECK(worse == 0, worse << " windows where damping increased low-frequency energy (thump)");
}

void TestParameterFuzz()
{
    std::cout << "[Robustness] NaN/Inf/huge parameters and bends give finite outputs ..." << std::endl;
    const float specials[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), 1e30f, -1e30f, 0.0f, 0.5f, 1.0f, 3.0f };
    uint32_t rng = 12345u;
    auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    auto pick = [&]() { return specials[next() % (sizeof(specials) / sizeof(specials[0]))]; };
    // Sanitised parameters: every field finite and inside its documented range
    auto inRange = [](const EngineParameters& q) {
        auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        return q.model >= 0 && q.model <= 3 && q.malletType >= 0 && q.malletType <= 3
            && in(q.malletHardness, 0, 1) && in(q.strikePosition, 0, 1) && in(q.resonatorTune, -50, 50)
            && in(q.resonatorCoupling, 0, 1) && in(q.decay, 0.1f, 8) && in(q.buzzAmount, 0, 1) && in(q.artifacts, 0, 1)
            && in(q.sympathetic, 0, 1) && in(q.pitchGlide, 0, 1) && in(q.bodyBloom, 0, 1) && in(q.closeLevel, 0, 1)
            && in(q.farLevel, 0, 1) && in(q.piezoLevel, 0, 1) && in(q.stereoWidth, 0, 2) && in(q.preampDrive, 0, 1)
            && in(q.warmth, -1, 1) && in(q.compAmount, 0, 1) && in(q.compAttack, 1, 50) && in(q.compRelease, 20, 500)
            && in(q.volume, 0, 1) && q.oversampling >= 0 && q.oversampling <= 2 && q.velocityCurve >= 0 && q.velocityCurve <= 3
            && q.polyphony >= 0 && q.polyphony <= 2 && in(q.tuneCents, -200, 200) && in(q.detune, 0, 100)
            && q.roomType >= 0 && q.roomType <= 2
            && in(q.velocityToHardness, 0, 1) && in(q.velocityToPosition, 0, 1) && in(q.velocityToDecay, 0, 1)
            && in(q.strikeJitter, 0, 100);
    };
    for (double sr : { 44100.0, 192000.0 }) {
        MarembaEngine engine(sr);
        bool finite = true;
        int outOfRange = 0, lostStrikes = 0;
        for (int round = 0; round < 40; ++round) {
            EngineParameters p;
            float* fields[] = { &p.malletHardness, &p.strikePosition, &p.resonatorTune, &p.resonatorCoupling, &p.decay,
                                &p.buzzAmount, &p.artifacts, &p.sympathetic, &p.pitchGlide, &p.bodyBloom, &p.closeLevel,
                                &p.farLevel, &p.piezoLevel, &p.stereoWidth, &p.preampDrive, &p.warmth, &p.compAmount,
                                &p.compAttack, &p.compRelease, &p.volume, &p.tuneCents, &p.detune, &p.strikeJitter,
                                &p.velocityToHardness, &p.velocityToPosition, &p.velocityToDecay };
            for (float* f : fields) if (next() % 3 == 0) *f = pick();
            p.model = static_cast<int>(next() % 9) - 2;
            p.malletType = static_cast<int>(next() % 9) - 2;
            p.oversampling = static_cast<int>(next() % 7) - 2;
            p.polyphony = static_cast<int>(next() % 7) - 2;
            p.roomType = static_cast<int>(next() % 7) - 2;
            p.velocityCurve = static_cast<int>(next() % 9) - 2;
            engine.SetParameters(p);
            if (!inRange(engine.GetParameters())) ++outOfRange;
            engine.SetPitchBendCents(pick());
            engine.NoteOn(static_cast<int>(next() % 140) - 6, pick());
            engine.NoteOn(static_cast<int>(next() % 128), 1.0f);
            Render r;
            RenderFrames(engine, r, 64 * 6, &p);
            // Let a pending oversampling fade (5 ms) finish, then a valid strike
            // under the same fuzzed parameters must survive a batch. The output
            // guard resets the engine on any non-finite sample, which would kill it.
            RenderFrames(engine, r, static_cast<int>(0.006 * sr) + 64, &p);
            engine.NoteOn(60, 1.0f);
            RenderFrames(engine, r, 64, &p);
            if (engine.GetActiveVoiceCount() == 0 || engine.IsSilent()) ++lostStrikes;
            finite = finite && AllFinite(r);
        }
        CHECK(finite, "non-finite output under parameter fuzz at " << sr);
        CHECK(outOfRange == 0, outOfRange << " fuzz rounds left a parameter non-finite or out of range at " << sr);
        CHECK(lostStrikes == 0, lostStrikes << " valid strikes lost under parameter fuzz at " << sr << " (engine guard fired)");
    }
}

void TestRollStability()
{
    std::cout << "[NU-2] Fast repeated strikes on a low Kalimba bar stay bounded (96k/8x) ..." << std::endl;
    EngineParameters p;
    p.model = 3;
    p.oversampling = 2;
    MarembaEngine single(96000.0);
    single.SetParameters(p);
    single.NoteOn(21, 100.0f / 127.0f);
    Render one;
    RenderFrames(single, one, 96000 / 2);
    MarembaEngine engine(96000.0);
    engine.SetParameters(p);
    Render r;
    for (int i = 0; i < 100; ++i) {
        engine.NoteOn(21, 100.0f / 127.0f);
        RenderFrames(engine, r, 96000 * 30 / 1000);
    }
    const double peak = Peak(r.ch[kCloseL]), ref = Peak(one.ch[kCloseL]);
    std::cout << "  -> roll peak " << peak << " vs single strike " << ref << std::endl;
    CHECK(AllFinite(r) && peak < 4.0 * ref, "repeated strikes grow without bound");
}

void TestSubRangeRendering()
{
    std::cout << "[Blocks] Rendering 64 frames in pieces equals one call ..." << std::endl;
    EngineParameters p;
    p.decay = 0.1f;          // short notes: the engine also goes idle in between
    p.oversampling = 1;
    MarembaEngine a(44100.0), b(44100.0);
    a.SetParameters(p);
    b.SetParameters(p);
    float bufA[kNumChannels][64], bufB[kNumChannels][64];
    const int pieces[] = { 1, 7, 13, 43 };
    double maxDiff = 0.0;
    bool wentIdle = false;
    for (int batch = 0; batch < 3000; ++batch) {
        if (batch == 1000 || batch == 2000) {
            p.oversampling = (batch == 1000) ? 2 : 0; // switch while ringing
            a.SetParameters(p);
            b.SetParameters(p);
        }
        if (batch % 397 == 0 || batch % 397 == 5) {
            a.NoteOn(40 + batch % 30, 0.8f);
            b.NoteOn(40 + batch % 30, 0.8f);
        }
        wentIdle = wentIdle || a.IsSilent();
        a.RenderBatch(bufA[0], bufA[1], bufA[2], bufA[3], bufA[4], bufA[5], bufA[6], 64);
        int offset = 0;
        for (int n : pieces) {
            b.RenderBatch(bufB[0] + offset, bufB[1] + offset, bufB[2] + offset, bufB[3] + offset,
                          bufB[4] + offset, bufB[5] + offset, bufB[6] + offset, n);
            offset += n;
        }
        for (int c = 0; c < kNumChannels; ++c)
            for (int i = 0; i < 64; ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(bufA[c][i] - bufB[c][i])));
    }
    CHECK(wentIdle, "test did not cover the idle transition");
    CHECK(maxDiff == 0.0, "piecewise rendering differs: " << maxDiff);
}

void TestResetDeterminism()
{
    std::cout << "[Reset] A render after Reset() equals a fresh engine ..." << std::endl;
    EngineParameters p; // artifacts and jitter on: PRNG state matters
    p.oversampling = 1;
    MarembaEngine used(44100.0);
    used.SetParameters(p);
    for (int n = 50; n < 60; ++n) used.NoteOn(n, 0.7f);
    used.SetPitchBendCents(0.0f);
    Render junk;
    RenderFrames(used, junk, 44100 / 4);
    used.Reset();
    used.SetParameters(p);
    used.NoteOn(62, 0.9f);
    used.NoteOn(66, 0.6f);
    Render a;
    RenderFrames(used, a, 44100 / 2);

    MarembaEngine fresh(44100.0);
    fresh.SetParameters(p);
    fresh.NoteOn(62, 0.9f);
    fresh.NoteOn(66, 0.6f);
    Render b;
    RenderFrames(fresh, b, 44100 / 2);
    double maxDiff = 0.0;
    for (int c = 0; c < kNumChannels; ++c) maxDiff = std::max(maxDiff, std::sqrt(DiffEnergy(a.ch[c], b.ch[c], 0, a.size())));
    CHECK(maxDiff == 0.0, "render after Reset differs from a fresh engine: " << maxDiff);
}

void TestChordNoiseDecorrelated()
{
    std::cout << "[Noise] Voices of a chord have independent noise ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    p.malletType = 3; // wood baton: strong clack
    p.malletHardness = 0.8f;
    // Note 60 alone (slot 0) vs note 60 as the second note of a pair (slot 1) minus the first note alone.
    MarembaEngine alone(44100.0), pair(44100.0), other(44100.0);
    alone.SetParameters(p);
    pair.SetParameters(p);
    other.SetParameters(p);
    alone.NoteOn(60, 1.0f);
    pair.NoteOn(72, 1.0f);
    pair.NoteOn(60, 1.0f);
    other.NoteOn(72, 1.0f);
    Render ra, rp, ro;
    RenderFrames(alone, ra, 441);
    RenderFrames(pair, rp, 441);
    RenderFrames(other, ro, 441);
    std::vector<float> second(rp.size());
    for (size_t i = 0; i < second.size(); ++i) second[i] = rp.ch[kCloseL][i] - ro.ch[kCloseL][i];
    const double rel = DiffEnergy(second, ra.ch[kCloseL], 0, second.size()) / Energy(ra.ch[kCloseL], 0, second.size());
    std::cout << "  -> slot-to-slot noise difference " << 10.0 * std::log10(rel) << " dB re note" << std::endl;
    CHECK(rel > 1e-5, "all voices share one noise sequence (coherent clack)");
}

void TestDirectOutLevels()
{
    std::cout << "[Gain] Direct outs stay near main level on a forte chord; null pointers are fine ..." << std::endl;
    EngineParameters p;
    MarembaEngine engine(44100.0);
    engine.SetParameters(p);
    for (int n : { 48, 52, 55, 60, 64, 67 }) engine.NoteOn(n, 1.0f);
    Render r;
    RenderFrames(engine, r, 44100 / 2);
    const double mainPeak = std::max(Peak(r.ch[kMainL]), Peak(r.ch[kMainR]));
    double directPeak = 0.0;
    for (int c = kCloseL; c <= kPiezo; ++c) directPeak = std::max(directPeak, Peak(r.ch[c]));
    std::cout << "  -> main peak " << Db(mainPeak) << " dBFS, direct peak " << Db(directPeak) << " dBFS" << std::endl;
    CHECK(directPeak < 2.0 && directPeak < mainPeak * 2.5, "direct outs far above the main level");

    float l[64], rr[64];
    engine.NoteOn(70, 1.0f);
    engine.RenderBatch(l, rr, nullptr, nullptr, nullptr, nullptr, nullptr, 64);
    bool finite = true;
    for (int i = 0; i < 64; ++i) finite = finite && std::isfinite(l[i]) && std::isfinite(rr[i]);
    CHECK(finite, "rendering without direct outs failed");
}

void TestPitchBendRetunesRingingNotes()
{
    std::cout << "[Bend] Pitch bend and tune retune ringing notes ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    MarembaEngine engine(44100.0);
    engine.SetParameters(p);
    engine.NoteOn(69, 0.9f);
    Render r;
    RenderFrames(engine, r, 4410, &p);
    engine.SetPitchBendCents(200.0f);
    const size_t at = r.size();
    RenderFrames(engine, r, 44100 / 2, &p);
    const double f = EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], at + 441, r.size(), 44100.0, NoteHz(71));
    CHECK(std::fabs(Cents(f, NoteHz(71))) < 1.0, "bend did not reach the ringing note: " << Cents(f, 440.0) << " c");
    engine.SetPitchBendCents(0.0f);
    p.tuneCents = -50.0f;
    const size_t at2 = r.size();
    RenderFrames(engine, r, 44100 / 2, &p);
    const double f2 = EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], at2 + 441, r.size(), 44100.0, 440.0 * std::exp2(-50.0 / 1200.0));
    CHECK(std::fabs(Cents(f2, 440.0) + 50.0) < 1.0, "tune did not reach the ringing note: " << Cents(f2, 440.0) << " c");
}

// ─── Review round 2: regression guards ──────────────────────────────────────

// Owner decision: note-off never damps. NoteOff is bit-neutral on every output.
void TestNoteOffIsSoundNeutral()
{
    std::cout << "[NoteOff] A note-off leaves the ringing bar untouched (bit-exact) ..." << std::endl;
    EngineParameters p;
    auto run = [&](bool release) {
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        e.NoteOn(48, 0.8f);
        e.NoteOn(60, 0.8f);
        Render r;
        RenderFrames(e, r, 64 * 10, &p);
        if (release) e.NoteOff(60);
        RenderFrames(e, r, 44100, &p);
        return r;
    };
    const Render held = run(false), released = run(true);
    CHECK(Peak(held.ch[kMainL]) > 1e-3, "test note is silent");
    double diff = 0.0;
    for (int c = 0; c < kNumChannels; ++c) diff += DiffEnergy(held.ch[c], released.ch[c], 0, held.size());
    CHECK(diff == 0.0, "NoteOff changed the sound (energy difference " << diff << ")");
}

// DampAll is a gentle hand damp (~0.3-0.6 s T60) at every rate, not a cut.
void TestDampAllIsGentle()
{
    std::cout << "[Stop] DampAll is gentle at 44.1k/2x and 192k/8x ..." << std::endl;
    for (auto cfg : { std::make_pair(44100.0, 0), std::make_pair(192000.0, 2) }) {
        const double sr = cfg.first;
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.oversampling = cfg.second;
        p.sympathetic = 0.0f;
        p.farLevel = 0.0f;
        const int before = static_cast<int>(sr * 0.3) / 64 * 64;
        auto run = [&](bool damp) {
            MarembaEngine e(sr);
            e.SetParameters(p);
            e.NoteOn(48, 1.0f);
            Render r;
            RenderFrames(e, r, before);
            if (damp) e.DampAll();
            RenderFrames(e, r, static_cast<int>(sr * 0.4));
            return r;
        };
        const Render ring = run(false), damped = run(true);
        const size_t at = static_cast<size_t>(before);
        const size_t a = at + static_cast<size_t>(0.05 * sr), b = at + static_cast<size_t>(0.10 * sr);
        const double first = Rms(damped.ch[kCloseL], at, at + 64) / Rms(ring.ch[kCloseL], at, at + 64);
        const double later = Rms(damped.ch[kCloseL], a, b) / Rms(ring.ch[kCloseL], a, b);
        std::cout << "  -> " << sr << "/" << (2 << cfg.second) << "x: first batch " << first << ", 50-100 ms " << later << std::endl;
        CHECK(first > 0.9, "DampAll cuts instantly at " << sr);
        CHECK(later > 0.1 && later < 0.7, "DampAll rate off contract at " << sr << ": " << later);
    }
}

// EN-3: a transport stop during an oversampling fade also damps the notes that
// were queued before the stop (and only those).
void TestDampAllReachesQueuedNotes()
{
    std::cout << "[Stop] DampAll during an oversampling fade reaches queued notes ..." << std::endl;
    auto run = [](bool noteBeforeStop) {
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.sympathetic = 0.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        e.NoteOn(60, 0.9f);
        Render r;
        RenderFrames(e, r, 64 * 10, &p);
        p.oversampling = 1;              // 2x -> 4x: fade pending while the note rings
        e.SetParameters(p);
        if (noteBeforeStop) e.NoteOn(45, 0.9f);
        e.DampAll();
        if (!noteBeforeStop) e.NoteOn(45, 0.9f);
        RenderFrames(e, r, 44100 * 3 / 2, &p);
        return e.GetActiveVoiceCount();
    };
    const int queuedBefore = run(true), playedAfter = run(false);
    std::cout << "  -> voices 1.5 s later: queued before stop " << queuedBefore << ", played after stop " << playedAfter << std::endl;
    CHECK(queuedBefore == 0, "a note queued before the stop rings on undamped");
    CHECK(playedAfter == 1, "a note played after the stop was damped too");
}

// After an oversampling switch nothing old may resume when a different note comes.
void TestSwitchLeavesNoFrozenVoices()
{
    std::cout << "[Switch] Oversampling switch leaves no voice behind ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    MarembaEngine e(48000.0);
    e.SetParameters(p);
    e.NoteOn(57, 0.9f);
    Render r;
    RenderFrames(e, r, 48000 / 4, &p);
    p.oversampling = 2;
    RenderFrames(e, r, 960, &p);
    CHECK(e.GetActiveVoiceCount() == 0, "voices survive the oversampling switch: " << e.GetActiveVoiceCount());
    e.NoteOn(45, 0.9f); // a different note wakes the engine
    const size_t at = r.size();
    RenderFrames(e, r, 48000 / 2, &p);
    std::vector<double> x;
    for (size_t i = at + 2400; i < r.size(); ++i) x.push_back(r.ch[kCloseL][i]);
    CHECK(GoertzelPower(x, 48000.0, 880.0) < 1e-3 * GoertzelPower(x, 48000.0, 110.0), "old note resumed 2 octaves up");
}

double NotePower(const Render& r, size_t from, size_t to, int note)
{
    std::vector<double> x;
    for (size_t i = from; i < std::min(to, r.size()); ++i) x.push_back(r.ch[kCloseL][i] + r.ch[kCloseR][i]);
    // Windowed: a rectangular window leaks loud neighbouring fundamentals
    // into a very quiet note's bin.
    return GoertzelPower(HannWindowed(x), 44100.0, NoteHz(note));
}

// Stealing takes the quietest voice (and prefers a released key), with a fade.
void TestStealVictim()
{
    std::cout << "[Steal] Voice stealing picks the quiet / released voice and fades it ..." << std::endl;
    const int notes[8] = { 48, 52, 55, 60, 64, 67, 72, 76 };
    auto run = [&](int poly, int quietIndex, int releasedIndex) {
        EngineParameters p = CleanParams();
        p.sympathetic = 0.0f;
        p.decay = 4.0f;
        p.polyphony = poly;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int i = 0; i < 8; ++i) e.NoteOn(notes[i], i == quietIndex ? 0.05f : 0.9f);
        if (releasedIndex >= 0) e.NoteOff(notes[releasedIndex]);
        Render r;
        RenderFrames(e, r, 4410, &p);
        e.NoteOn(84, 0.9f);
        RenderFrames(e, r, 44100 / 2, &p);
        return r;
    };
    const size_t from = 4410 + 4410, to = 4410 + 44100 / 2;
    // The quiet victim is E3: louder neighbours put real energy near G4 (C3's
    // tube resonance at its twelfth) and E4 (C3's first torsional mode), which
    // would mask a correctly stolen quiet note there.
    for (int mode = 0; mode < 2; ++mode) {
        const int quiet = mode == 0 ? 1 : -1, released = mode == 0 ? -1 : 3;
        const int victim = mode == 0 ? quiet : released;
        const Render stolen = run(0, quiet, released), ref = run(2, quiet, released);
        for (int i = 0; i < 8; ++i) {
            const double ratio = NotePower(stolen, from, to, notes[i]) / NotePower(ref, from, to, notes[i]);
            if (i == victim) CHECK(ratio < 0.1, "victim note " << notes[i] << " still sounds: " << ratio);
            else CHECK(ratio > 0.5, "wrong victim: note " << notes[i] << " lost (" << ratio << ")");
        }
        // The victim fades (no click): right after the steal the output is still
        // close to the unstolen reference; 10-20 ms later the victim is gone.
        double early = 0.0, late = 0.0;
        for (size_t i = 4410; i < 4410 + 16; ++i) early = std::max(early, static_cast<double>(std::fabs(stolen.ch[kCloseL][i] - ref.ch[kCloseL][i])));
        for (size_t i = 4410 + 441; i < 4410 + 882; ++i) late = std::max(late, static_cast<double>(std::fabs(stolen.ch[kCloseL][i] - ref.ch[kCloseL][i])));
        if (mode == 1) CHECK(early < 0.3 * late, "stolen voice is cut, not faded: " << early << " vs " << late);
    }
}

// EN-2: a note struck a few frames earlier is never the steal victim while
// quieter old voices are available (energy is evaluated at steal time).
void TestStealSparesJustStruckNote()
{
    std::cout << "[Steal] A just-struck loud note is not stolen by the next one ..." << std::endl;
    const int quietNotes[8] = { 49, 51, 54, 56, 58, 61, 63, 66 };
    const int noteA = 71, noteB = 75;
    auto run = [&](int poly, int malletType, int gap) {
        EngineParameters p = CleanParams();
        p.sympathetic = 0.0f;
        p.decay = 4.0f;
        p.polyphony = poly;
        p.malletType = malletType;
        p.malletHardness = 0.9f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int n : quietNotes) e.NoteOn(n, 0.3f);
        Render r;
        RenderFrames(e, r, 8820, &p);
        e.NoteOn(noteA, 1.0f);
        RenderFrames(e, r, gap, &p);
        e.NoteOn(noteB, 1.0f);
        RenderFrames(e, r, 44100 / 2, &p);
        return r;
    };
    int stolen = 0;
    for (int mallet : { 2, 3 }) {
        for (int gap = 4; gap <= 24; ++gap) {
            const Render limited = run(0, mallet, gap), free = run(1, mallet, gap);
            const size_t from = 8820 + gap + 2205, to = limited.size();
            const double ratio = NotePower(limited, from, to, noteA) / NotePower(free, from, to, noteA);
            if (ratio < 0.5) {
                ++stolen;
                std::cerr << "  mallet " << mallet << " gap " << gap << ": note A kept " << ratio << std::endl;
            }
        }
    }
    CHECK(stolen == 0, stolen << " cases stole the note struck a few frames earlier");
}

// Every active sound parameter changes the output (dead-knob sweep), and the velocity
// curves go the documented way: Soft (0) plays louder than Linear, Hard (2) quieter.
void TestNoDeadParameters()
{
    std::cout << "[Knobs] Every active engine parameter changes the output ..." << std::endl;
    auto render = [](const EngineParameters& p) {
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int n : { 48, 55, 60, 64 }) e.NoteOn(n, 0.6f);
        Render r;
        RenderFrames(e, r, 44100 / 2, &p);
        return r;
    };
    EngineParameters base = CleanParams();
    base.compAmount = 1.0f;
    base.preampDrive = 0.5f;
    const Render ref = render(base);
    const double refEnergy = Energy(ref.ch[kMainL], 0, ref.size());
    struct Knob { const char* name; void (*set)(EngineParameters&); };
    const Knob knobs[] = {
        { "model", [](EngineParameters& p) { p.model = 1; } },
        { "malletType", [](EngineParameters& p) { p.malletType = 3; } },
        { "malletHardness", [](EngineParameters& p) { p.malletHardness = 0.9f; } },
        { "strikePosition", [](EngineParameters& p) { p.strikePosition = 0.9f; } },
        { "resonatorTune", [](EngineParameters& p) { p.resonatorTune = 40.0f; } },
        { "resonatorCoupling", [](EngineParameters& p) { p.resonatorCoupling = 0.1f; } },
        { "decay", [](EngineParameters& p) { p.decay = 0.3f; } },
        { "buzzAmount", [](EngineParameters& p) { p.buzzAmount = 0.8f; } },
        { "artifacts", [](EngineParameters& p) { p.artifacts = 1.0f; } },
        { "sympathetic", [](EngineParameters& p) { p.sympathetic = 1.0f; } },
        { "bodyBloom", [](EngineParameters& p) { p.bodyBloom = 1.0f; } },
        { "closeLevel", [](EngineParameters& p) { p.closeLevel = 0.2f; } },
        { "farLevel", [](EngineParameters& p) { p.farLevel = 1.0f; } },
        { "piezoLevel", [](EngineParameters& p) { p.piezoLevel = 1.0f; } },
        { "stereoWidth", [](EngineParameters& p) { p.stereoWidth = 2.0f; } },
        { "preampDrive", [](EngineParameters& p) { p.preampDrive = 1.0f; } },
        { "warmth", [](EngineParameters& p) { p.warmth = -1.0f; } },
        { "compAmount", [](EngineParameters& p) { p.compAmount = 0.2f; } },
        { "compAttack", [](EngineParameters& p) { p.compAttack = 50.0f; } },
        { "compRelease", [](EngineParameters& p) { p.compRelease = 500.0f; } },
        { "volume", [](EngineParameters& p) { p.volume = 0.5f; } },
        { "oversampling", [](EngineParameters& p) { p.oversampling = 2; } },
        { "velocityCurve Soft", [](EngineParameters& p) { p.velocityCurve = 0; } },
        { "velocityCurve Hard", [](EngineParameters& p) { p.velocityCurve = 2; } },
        { "velocityCurve Expressive", [](EngineParameters& p) { p.velocityCurve = 3; } },
        { "tuneCents", [](EngineParameters& p) { p.tuneCents = 50.0f; } },
        { "detune", [](EngineParameters& p) { p.detune = 100.0f; } },
        { "strikeJitter", [](EngineParameters& p) { p.strikeJitter = 100.0f; } },
        // polyphony cannot change a 4-note chord; TestPolyphonyReduction covers it
    };
    for (const auto& k : knobs) {
        EngineParameters p = base;
        k.set(p);
        const Render r = render(p);
        const double rel = DiffEnergy(r.ch[kMainL], ref.ch[kMainL], 0, ref.size()) / refEnergy;
        CHECK(rel > 1e-5, k.name << " has no effect on the output (" << rel << ")");
    }

    // Velocity-curve direction, measured on the bar itself (close mic, no dynamics)
    EngineParameters v = CleanParams();
    v.sympathetic = 0.0f;
    auto level = [&](int curve) {
        v.velocityCurve = curve;
        const Render r = RenderNote(44100.0, v, 60, 0.4f, 0.2);
        return Energy(r.ch[kCloseL], 0, r.size());
    };
    const double soft = level(0), linear = level(1), hard = level(2);
    std::cout << "  -> velocity 0.4: Soft " << 10.0 * std::log10(soft / linear) << " dB, Hard "
              << 10.0 * std::log10(hard / linear) << " dB re Linear" << std::endl;
    CHECK(soft > 1.5 * linear, "'Soft' curve does not play light touch louder");
    CHECK(hard < linear / 1.5, "'Hard' curve does not need more force");
}

// Maximum Decay keeps its T60 at the highest internal rate.
void TestMaxDecayRateInvariant()
{
    std::cout << "[Decay] T60 at decay 8.0 is the same at 44.1k/2x and 192k/8x ..." << std::endl;
    auto t60 = [](double sr, int os) {
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.oversampling = os;
        p.sympathetic = 0.0f;
        p.farLevel = 0.0f;
        const Render r = RenderNote(sr, p, 48, 0.8f, 3.2);
        return 60.0 * 2.0 / Db(Rms(r.ch[kCloseL], static_cast<size_t>(0.9 * sr), static_cast<size_t>(1.1 * sr))
                               / Rms(r.ch[kCloseL], static_cast<size_t>(2.9 * sr), static_cast<size_t>(3.1 * sr)));
    };
    const double a = t60(44100.0, 0), b = t60(192000.0, 2);
    std::cout << "  -> " << a << " s vs " << b << " s" << std::endl;
    CHECK(std::fabs(b / a - 1.0) < 0.05, "decay 8.0 T60 depends on the rate");
}

// Halo (sympathetic) level is rate invariant, measured differentially.
void TestHaloRateInvariant()
{
    std::cout << "[Halo] Sympathetic halo level across rates (differential) ..." << std::endl;
    double ref = 0.0;
    for (auto cfg : { std::make_pair(44100.0, 0), std::make_pair(96000.0, 1), std::make_pair(192000.0, 2) }) {
        EngineParameters p = CleanParams();
        p.oversampling = cfg.second;
        p.sympathetic = 0.0f;
        const Render off = RenderNote(cfg.first, p, 60, 0.9f, 0.5);
        p.sympathetic = 0.8f;
        const Render on = RenderNote(cfg.first, p, 60, 0.9f, 0.5);
        std::vector<float> halo(off.size());
        for (size_t i = 0; i < halo.size(); ++i) halo[i] = on.ch[kMainL][i] - off.ch[kMainL][i];
        const double db = Db(Rms(halo, 0, halo.size()));
        if (ref == 0.0) ref = db;
        std::cout << "  -> " << cfg.first << "/" << (2 << cfg.second) << "x halo " << db << " dBFS" << std::endl;
        CHECK(std::fabs(db - ref) < 1.0, "halo level changes with rate: " << db - ref << " dB");
    }
}

// Direct close out is the close part of the main bus through the same half-band
// decimator (same response and phase), only trimmed by the fixed direct-out gain.
void TestDirectOutMatchesMain()
{
    std::cout << "[Gain] Direct close out matches the main bus close path ..." << std::endl;
    constexpr double kDirectOutTrim = 0.5; // MarembaEngine.cpp
    EngineParameters p = CleanParams();
    p.farLevel = 0.0f; p.piezoLevel = 0.0f; p.closeLevel = 1.0f; p.stereoWidth = 1.0f;
    p.compAmount = 0.0f; p.preampDrive = 0.0f; p.warmth = 0.0f;
    p.malletType = 3; p.malletHardness = 0.9f;
    for (int os : { 0, 2 }) {
        p.oversampling = os;
        const Render r = RenderNote(44100.0, p, 96, 1.0f, 0.1);
        double err = 0.0, lvl = 0.0;
        for (size_t i = 0; i < r.size(); ++i) {
            const double close = r.ch[kCloseL][i] / kDirectOutTrim;
            const double d = r.ch[kMainL][i] - close;
            err += d * d;
            lvl += close * close;
        }
        std::cout << "  -> os " << os << ": direct vs main " << Db(std::sqrt(err / lvl)) << " dB" << std::endl;
        CHECK(err < 1e-3 * lvl, "direct close differs from the main close path (os " << os << ")");
    }
}

// SND-4/EN-5 (synthetic: engine defaults for every model; the factory bank
// itself is checked by AllPatchRenderTest, which loads the real .repatch files
// through the wrapper): a vel-127 4-note chord keeps every direct out <= 0 dBFS.
void TestDirectOutHeadroom()
{
    std::cout << "[Gain] Direct outs <= 0 dBFS on a vel-127 chord (defaults, every model) ..." << std::endl;
    double worst = 0.0;
    for (int model = 0; model < 4; ++model) {
        EngineParameters p;
        p.model = model;
        for (const auto& chord : { std::array<int, 4>{ 48, 52, 55, 60 }, std::array<int, 4>{ 60, 64, 67, 72 } }) {
            MarembaEngine e(44100.0);
            e.SetParameters(p);
            for (int n : chord) e.NoteOn(n, 1.0f);
            Render r;
            RenderFrames(e, r, 44100 / 2, &p);
            for (int c = kCloseL; c <= kPiezo; ++c) {
                const double peak = Peak(r.ch[c]);
                worst = std::max(worst, peak);
                CHECK(peak <= 1.0, "model " << model << " chord " << chord[0] << " " << kChannelNames[c] << " direct out peaks at " << Db(peak) << " dBFS");
            }
        }
    }
    std::cout << "  -> hottest direct out " << Db(worst) << " dBFS" << std::endl;
}

// A volume change is smoothed (no zipper / step).
void TestVolumeSmoothing()
{
    std::cout << "[Gain] Volume change is smoothed ..." << std::endl;
    EngineParameters p = CleanParams();
    p.model = 3;
    p.decay = 4.0f;
    MarembaEngine e(44100.0);
    e.SetParameters(p);
    e.NoteOn(69, 1.0f);
    Render r;
    RenderFrames(e, r, 4410, &p);
    const size_t at = r.size();
    p.volume = 0.1f;
    RenderFrames(e, r, 4410, &p);
    double stepBefore = 0.0, stepAt = 0.0;
    for (size_t i = at - 441; i < at; ++i) stepBefore = std::max(stepBefore, static_cast<double>(std::fabs(r.ch[kMainL][i] - r.ch[kMainL][i - 1])));
    for (size_t i = at; i < at + 64; ++i) stepAt = std::max(stepAt, static_cast<double>(std::fabs(r.ch[kMainL][i] - r.ch[kMainL][i - 1])));
    std::cout << "  -> step before " << stepBefore << ", at the change " << stepAt << std::endl;
    CHECK(stepAt <= stepBefore * 1.05, "volume change steps the output");
}

// After a full bend nothing of the ringing note stays at the unbent pitch
// (twin mode, tube resonator and sympathetic halo are retuned too).
void TestBendLeavesNothingBehind()
{
    std::cout << "[Bend] Bend retunes every partial of a ringing note ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 1.0f;
    p.decay = 4.0f;
    MarembaEngine e(44100.0);
    e.SetParameters(p);
    e.NoteOn(57, 0.9f);
    Render r;
    RenderFrames(e, r, 4410, &p);
    e.SetPitchBendCents(200.0f);
    const size_t at = r.size();
    RenderFrames(e, r, 44100, &p);
    std::vector<double> x;
    for (size_t i = at + 4410; i < r.size(); ++i) x.push_back(static_cast<double>(r.ch[kMainL][i]) + r.ch[kMainR][i]);
    x = HannWindowed(x);
    const double bent = GoertzelPower(x, 44100.0, NoteHz(59)), stale = GoertzelPower(x, 44100.0, NoteHz(57));
    std::cout << "  -> energy left at the unbent pitch: " << 10.0 * std::log10(stale / bent) << " dB" << std::endl;
    CHECK(stale < 1e-4 * bent, "part of the ringing note stays at the unbent pitch");
}

// EN-1: decayed resonators are flushed at control rate instead of cycling on
// subnormal values (voice modes / twin / tube, sympathetic mesh, frame body).
void TestNoSubnormalStates()
{
    std::cout << "[Denormals] No subnormal resonator state while voices, mesh and body ring out ..." << std::endl;
    constexpr int kTick = 32; // 16 output frames at 2x: the engine's control interval
    {
        // Default C4 at 44.1k/2x (upper modes go subnormal within 0.5 s unflushed),
        // and a restruck low bar at maximum decay.
        for (int note : { 60, 36 }) {
            maremba::MarembaVoice voice;
            maremba::FastPRNG prng(1234u);
            const float decay = note == 60 ? 1.0f : 8.0f;
            voice.Trigger(note, 0.8f, maremba::EMarimbaModel::ImperialRosewood, 0.4f, 0.5f, 0.0f, 0.7f, decay,
                          0.15f, 0.0f, 0.3f, 88200.0, prng, 0.0f, 0.0f, 1, 0.0f);
            int badTicks = 0;
            long samples = 0;
            const long limit = 88200L * 70;
            float c = 0.0f, f = 0.0f, pz = 0.0f;
            for (; samples < limit && voice.IsActive(); ++samples) {
                if (samples == 88200 / 10 && note == 36) {
                    voice.Trigger(note, 0.5f, maremba::EMarimbaModel::ImperialRosewood, 0.4f, 0.5f, 0.0f, 0.7f, decay,
                                  0.15f, 0.0f, 0.3f, 88200.0, prng, 0.0f, 0.0f, 0, 0.0f);
                }
                voice.ProcessSample(c, f, pz);
                if ((samples + 1) % kTick == 0) {
                    if (!voice.HasNoSubnormalState()) ++badTicks;
                    voice.UpdateEnvelope();
                }
            }
            std::cout << "  -> note " << note << ": freed after " << samples / 88200.0 << " s, " << badTicks << " ticks with subnormal state" << std::endl;
            CHECK(!voice.IsActive(), "voice " << note << " never freed");
            CHECK(badTicks == 0, "voice " << note << " cycles on subnormal state (" << badTicks << " ticks)");
        }
    }
    {
        maremba::SympatheticMesh mesh;
        mesh.Configure(88200.0);
        int badTicks = 0;
        for (long s = 0; s < 88200L * 60; ++s) {
            mesh.BeginSample();
            if (s < 8820) {
                for (int n : { 48, 60, 64, 71 }) mesh.AccumulateVoice(n, static_cast<float>(std::sin(2.0 * M_PI * NoteHz(n) * s / 88200.0)));
            }
            float l = 0.0f, r = 0.0f;
            mesh.Process(1.0f, l, r);
            if ((s + 1) % kTick == 0) {
                if (!mesh.HasNoSubnormalState()) ++badTicks;
                mesh.FlushTiny();
            }
        }
        std::cout << "  -> mesh: " << badTicks << " ticks with subnormal state" << std::endl;
        CHECK(badTicks == 0, "sympathetic mesh cycles on subnormal state (" << badTicks << " ticks)");
    }
    {
        maremba::FrameBody body;
        body.Configure(88200.0, 110.0, 1.2);
        int badTicks = 0;
        for (long s = 0; s < 88200L * 20; ++s) {
            body.Process(s < 64 ? 0.5f : 0.0f);
            if ((s + 1) % kTick == 0) {
                if (!body.HasNoSubnormalState()) ++badTicks;
                body.FlushTiny();
            }
        }
        std::cout << "  -> frame body: " << badTicks << " ticks with subnormal state" << std::endl;
        CHECK(badTicks == 0, "frame body cycles on subnormal state (" << badTicks << " ticks)");
    }
}

// Side/mid ratio (dB) and mono fold-down (dB, (L+R)/2 vs the stereo RMS) of the main bus.
std::pair<double, double> StereoImage(const Render& r)
{
    double m = 0.0, s = 0.0, st = 0.0;
    for (size_t i = 0; i < r.size(); ++i) {
        const double l = r.ch[kMainL][i], rr = r.ch[kMainR][i];
        m += 0.25 * (l + rr) * (l + rr);
        s += 0.25 * (rr - l) * (rr - l);
        st += 0.5 * (l * l + rr * rr);
    }
    return { 10.0 * std::log10(s / m), 10.0 * std::log10(m / st) };
}

// SND-2: width widens the dry mics, never the decorrelated room return past
// natural; width 0 is exactly mono; a far-heavy, wide setting stays mono-safe.
void TestWidthAndRoomImage()
{
    std::cout << "[Width] Room return is not widened; far-heavy wide mix stays mono compatible ..." << std::endl;
    EngineParameters p = CleanParams();
    p.closeLevel = 0.3f;
    p.farLevel = 1.0f;
    p.piezoLevel = 0.0f;
    p.decay = 2.0f;
    auto phrase = [&](float width) {
        EngineParameters q = p;
        q.stereoWidth = width;
        MarembaEngine e(44100.0);
        e.SetParameters(q);
        Render r;
        const int notes[] = { 48, 55, 60, 64, 67, 72, 76, 79 };
        for (int n : notes) {
            e.NoteOn(n, 0.8f);
            RenderFrames(e, r, 44100 / 8, &q);
        }
        RenderFrames(e, r, 44100, &q);
        return r;
    };
    const Render mono = phrase(0.0f);
    double maxDiff = 0.0;
    for (size_t i = 0; i < mono.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(mono.ch[kMainL][i] - mono.ch[kMainR][i])));
    CHECK(maxDiff == 0.0, "width 0 with the room is not exactly mono: " << maxDiff);

    const auto natural = StereoImage(phrase(1.0f));
    const auto wide = StereoImage(phrase(1.8f));
    std::cout << "  -> far-heavy phrase: width 1 S/M " << natural.first << " dB, mono " << natural.second
              << " dB; width 1.8 S/M " << wide.first << " dB, mono " << wide.second << " dB" << std::endl;
    CHECK(wide.first <= 0.0, "wide far-heavy mix is side-heavy: S/M " << wide.first << " dB");
    CHECK(wide.second >= -4.0, "wide far-heavy mix loses too much in mono: " << wide.second << " dB");

    // The far direct out (dry + room) does not depend on the width at all
    const Render a = phrase(0.5f), b = phrase(2.0f);
    CHECK(DiffEnergy(a.ch[kFarL], b.ch[kFarL], 0, a.size()) == 0.0, "width leaks into the far direct out");
}

// SND-3: Far down to 0 and back while bars ring: the room restarts clean, no
// stale tail bursts back.
void TestRoomBypassNoStaleTail()
{
    std::cout << "[Room] Far 1 -> 0 -> 1 while ringing brings back no stale tail ..." << std::endl;
    auto run = [](bool toggle) {
        EngineParameters p = CleanParams();
        p.decay = 4.0f;
        p.farLevel = 1.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int n : { 48, 55, 60 }) e.NoteOn(n, 0.9f);
        Render r;
        RenderFrames(e, r, 44100 / 2, &p);
        if (toggle) p.farLevel = 0.0f;
        RenderFrames(e, r, 44100, &p);
        p.farLevel = 1.0f;
        RenderFrames(e, r, 44100 / 2, &p);
        return r;
    };
    const Render steady = run(false), toggled = run(true);
    const size_t at = 44100 / 2 + 44100 / 2 * 2, win = 882;
    const double back = Rms(toggled.ch[kFarL], at, at + win), ref = Rms(steady.ch[kFarL], at, at + win);
    double step = 0.0;
    for (size_t i = at; i < at + win; ++i) step = std::max(step, static_cast<double>(std::fabs(toggled.ch[kMainL][i] - toggled.ch[kMainL][i - 1])));
    double stepRef = 0.0;
    for (size_t i = at; i < at + win; ++i) stepRef = std::max(stepRef, static_cast<double>(std::fabs(steady.ch[kMainL][i] - steady.ch[kMainL][i - 1])));
    std::cout << "  -> far L 20 ms after re-enable: " << Db(back) << " dBFS (steady " << Db(ref) << "), main step " << step << " (steady " << stepRef << ")" << std::endl;
    CHECK(back <= ref, "stale room tail bursts back when Far returns");
}

// ─── Review round 3: regression guards ──────────────────────────────────────

// Render whole batches until the engine idles (at most maxSeconds); returns the seconds rendered.
double RenderUntilSilent(MarembaEngine& e, double sampleRate, double maxSeconds, const EngineParameters* everyBatch = nullptr)
{
    const long maxBatches = static_cast<long>(maxSeconds * sampleRate / 64.0);
    float buf[kNumChannels][64];
    long b = 0;
    for (; b < maxBatches && !e.IsSilent(); ++b) {
        if (everyBatch) e.SetParameters(*everyBatch);
        e.RenderBatch(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], 64);
    }
    return static_cast<double>(b) * 64.0 / sampleRate;
}

// E2-1/S2-1: a restrike after the membrane went away (Buzz down, or another
// model) releases the left-over buzz: the voice is freed and the engine idles.
void TestMembraneRestrikeFreesVoice()
{
    std::cout << "[Lifetime] A restrike without the membrane frees the voice (no zombie) ..." << std::endl;
    struct Case { const char* name; int model0; float buzz0; int model1; float buzz1; int gapBatches; float vel1; };
    const Case cases[] = {
        { "Balafon 0.5 -> Rosewood 0.0", 2, 0.5f, 0, 0.0f, 20, 0.5f },
        { "Rosewood 0.8 -> 0.1", 0, 0.8f, 0, 0.1f, 20, 0.5f },
        { "Balafon 0.75 -> Kalimba", 2, 0.75f, 3, 0.15f, 69, 0.8f },
    };
    for (const auto& c : cases) {
        for (bool damp : { false, true }) {
            EngineParameters p;
            p.model = c.model0;
            p.buzzAmount = c.buzz0;
            MarembaEngine e(44100.0);
            e.SetParameters(p);
            e.NoteOn(48, 1.0f);
            Render r;
            RenderFrames(e, r, 64 * c.gapBatches, &p);
            p.model = c.model1;
            p.buzzAmount = c.buzz1;
            e.SetParameters(p);
            e.NoteOn(48, c.vel1);
            e.NoteOff(48);
            if (damp) {
                RenderFrames(e, r, 64 * 4, &p);
                e.DampAll();
                RenderFrames(e, r, 44100 * 2, &p);
                CHECK(e.GetActiveVoiceCount() == 0, c.name << ": voice still active 2 s after DampAll");
            } else {
                const double t = RenderUntilSilent(e, 44100.0, 20.0, &p);
                std::cout << "  -> " << c.name << ": idle after " << t << " s" << std::endl;
                CHECK(e.GetActiveVoiceCount() == 0 && e.IsSilent(), c.name << ": engine never idles after the restrike");
            }
        }
    }
}

// C2-1: restriking a bar that was struck with another model fades the old ring
// in its own slot (no instant cut of its twin / upper modes) and strikes fresh.
void TestModelChangeRestrikeNoClick()
{
    std::cout << "[Restrike] A restrike after a model change fades the old ring, no click ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    p.bodyBloom = 0.0f;
    p.farLevel = 0.0f;
    const int ring = 44100 * 3 / 10 / 64 * 64;
    auto run = [&](int oldModel, bool restrike, int* activeAfter) {
        EngineParameters q = p;
        q.model = oldModel;
        MarembaEngine e(44100.0);
        e.SetParameters(q);
        e.NoteOn(48, 1.0f);
        Render r;
        RenderFrames(e, r, ring, &q);
        q.model = 3;   // Kalimba: no twin mode, fewer modes
        e.SetParameters(q);
        if (restrike) e.NoteOn(48, 0.05f);
        if (activeAfter) *activeAfter = e.GetActiveVoiceCount();
        RenderFrames(e, r, 44100 / 20, &q);
        return r;
    };
    auto freshKalimba = [&]() {
        EngineParameters q = p;
        q.model = 3;
        return RenderNote(44100.0, q, 48, 0.05f, 0.05);
    };
    // HF energy (second difference) of the close out over 3 ms after the restrike
    auto hf = [&](const std::vector<float>& x) {
        double e = 0.0;
        for (size_t i = ring + 2; i < ring + 132; ++i) {
            const double d = static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2];
            e += d * d;
        }
        return e;
    };
    const Render kal = freshKalimba();
    for (int oldModel : { 0, 1, 2 }) {
        int active = 0;
        const Render hit = run(oldModel, true, &active);
        const Render cont = run(oldModel, false, nullptr);
        std::vector<float> contPlusKal(cont.ch[kCloseL]);
        for (size_t i = ring; i < contPlusKal.size(); ++i) contPlusKal[i] += kal.ch[kCloseL][i - ring];
        const double eHit = hf(hit.ch[kCloseL]), eRef = hf(contPlusKal);
        std::cout << "  -> model " << oldModel << " -> Kalimba: HF after restrike " << 10.0 * std::log10(eHit / eRef)
                  << " dB re ring + fresh strike, voices " << active << std::endl;
        CHECK(active == 2, "old ring not faded in its own slot (voices " << active << ")");
        CHECK(eHit < 4.0 * eRef, "model-change restrike clicks: " << 10.0 * std::log10(eHit / eRef) << " dB");
    }
    // A same-model restrike stays in place
    MarembaEngine same(44100.0);
    same.SetParameters(p);
    same.NoteOn(48, 1.0f);
    Render r;
    RenderFrames(same, r, 64 * 10, &p);
    same.NoteOn(48, 0.5f);
    CHECK(same.GetActiveVoiceCount() == 1, "a same-model restrike took a second slot");
}

// E2-2: bend and tune changes keep the ringing resonators' amplitude and phase.
// A per-batch square bend and a 2*f0 sine tune modulation (the parametric-pump
// cases) on bass notes stay at the unmodulated level (voices and mesh).
void TestBendModulationStability()
{
    std::cout << "[Bend] Square bend / 2*f0 tune modulation cannot pump ringing resonators ..." << std::endl;
    const EngineParameters p = CleanParams();   // sympathetic mesh on
    const int batches = 44100 / 2 / 64;
    double worst = 0.0;
    int worstNote = 0;
    for (int note = 21; note <= 45; ++note) {
        auto run = [&](int mode) {
            EngineParameters q = p;
            MarembaEngine e(44100.0);
            e.SetParameters(q);
            e.NoteOn(note, 1.0f);
            float buf[kNumChannels][64];
            double peak = 0.0;
            for (int b = 0; b < batches; ++b) {
                if (mode == 1) e.SetPitchBendCents((b & 1) ? 200.0f : -200.0f);
                if (mode == 2) {
                    q.tuneCents = static_cast<float>(200.0 * std::sin(2.0 * M_PI * 2.0 * NoteHz(note) * b * 64.0 / 44100.0));
                    e.SetParameters(q);
                }
                e.RenderBatch(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], 64);
                for (int i = 0; i < 64; ++i) {
                    peak = std::max({ peak, static_cast<double>(std::fabs(buf[kCloseL][i])), static_cast<double>(std::fabs(buf[kCloseR][i])) });
                }
            }
            return peak;
        };
        const double ref = run(0);
        const double ratio = std::max(run(1), run(2)) / ref;
        if (ratio > worst) { worst = ratio; worstNote = note; }
        CHECK(ratio < 1.5, "note " << note << " grows " << ratio << "x under bend / tune modulation");
    }
    std::cout << "  -> worst peak ratio " << worst << " (note " << worstNote << ")" << std::endl;
}

// E2-3/S2-2: at Polyphony 24 with 24 bars ringing, a 12-note chord fades 12
// victims in their own slots (no hard reuse of a voice that is still loud).
void TestChordBurstFadesVictims()
{
    std::cout << "[Steal] A 12-note chord on 24 ringing voices fades every victim ..." << std::endl;
    EngineParameters p = CleanParams();
    p.polyphony = 2;
    p.decay = 4.0f;
    MarembaEngine e(44100.0);
    e.SetParameters(p);
    for (int n = 36; n < 60; ++n) e.NoteOn(n, 0.9f);
    Render r;
    RenderFrames(e, r, 44100 / 2, &p);
    for (int n = 72; n < 84; ++n) e.NoteOn(n, 0.9f);
    const int afterChord = e.GetActiveVoiceCount();
    RenderFrames(e, r, 441, &p);
    std::cout << "  -> voices right after the chord " << afterChord << ", 10 ms later " << e.GetActiveVoiceCount() << std::endl;
    CHECK(afterChord == 36, "chord voices cut instead of faded: " << afterChord << " active, expected 24 live + 12 fading");
    CHECK(e.GetActiveVoiceCount() == 24, "voice limit not kept after the fades: " << e.GetActiveVoiceCount());
}

// E2-4: an oversampling change set back within the fade cancels the switch:
// the ringing bars survive, the output ramps back without a click, queued notes
// start (released / damped as they should be).
void TestOversamplingRevertKeepsNotes()
{
    std::cout << "[Switch] An oversampling change undone within the fade keeps every note ..." << std::endl;
    auto run = [](bool change, int extra) { // extra: 0 none, 1 queued note, 2 queued note + DampAll
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.sympathetic = 0.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int n : { 48, 55, 60, 64 }) e.NoteOn(n, 0.9f);
        Render r;
        RenderFrames(e, r, 64 * 150, &p);
        if (change) {
            p.oversampling = 1;
            e.SetParameters(p);
        }
        if (extra >= 1) e.NoteOn(67, 0.9f);
        if (extra == 2) e.DampAll();
        RenderFrames(e, r, 64, &p);
        p.oversampling = 0;
        RenderFrames(e, r, 44100 / 10, &p);
        const int active = e.GetActiveVoiceCount();
        if (extra == 2) RenderFrames(e, r, 44100 * 3 / 2, &p);
        return std::make_pair(r, extra == 2 ? e.GetActiveVoiceCount() : active);
    };
    const auto ref = run(false, 0), reverted = run(true, 0);
    const size_t at = 64 * 150;
    double stepBefore = 0.0, stepFade = 0.0;
    for (size_t i = at - 2400; i < at; ++i) stepBefore = std::max(stepBefore, static_cast<double>(std::fabs(reverted.first.ch[kMainL][i] - reverted.first.ch[kMainL][i - 1])));
    for (size_t i = at; i < at + 441; ++i) stepFade = std::max(stepFade, static_cast<double>(std::fabs(reverted.first.ch[kMainL][i] - reverted.first.ch[kMainL][i - 1])));
    std::cout << "  -> voices 100 ms after the revert " << reverted.second << ", step " << stepFade << " (before " << stepBefore << ")" << std::endl;
    CHECK(reverted.second == 4, "an undone oversampling change killed the ringing notes (" << reverted.second << " left)");
    CHECK(stepFade <= stepBefore * 1.1, "click while the switch is cancelled");
    CHECK(DiffEnergy(reverted.first.ch[kMainL], ref.first.ch[kMainL], at + 441, ref.first.size()) == 0.0,
          "output does not return to the undisturbed render after the cancelled switch");
    CHECK(run(true, 1).second == 5, "a note queued during the cancelled fade did not start");
    CHECK(run(true, 2).second == 0, "a note queued before a stop in the cancelled fade was not damped");
}

// C2-2(a): a key pressed and released during an oversampling fade starts released.
void TestReleaseWhileQueued()
{
    std::cout << "[NoteOff] A key released while queued in the fade starts released ..." << std::endl;
    EngineParameters p = CleanParams();
    p.decay = 4.0f;
    MarembaEngine e(44100.0);
    e.SetParameters(p);
    e.NoteOn(48, 0.9f);
    Render r;
    RenderFrames(e, r, 64 * 10, &p);
    p.oversampling = 1;
    e.SetParameters(p);
    e.NoteOn(62, 0.9f);
    e.NoteOff(62);
    e.NoteOn(64, 0.9f);
    RenderFrames(e, r, 44100 / 50, &p);
    CHECK(e.GetActiveVoiceCount() == 2, "queued notes did not start: " << e.GetActiveVoiceCount());
    CHECK(maremba::MarembaEngineTestAccess::IsNoteReleased(e, 62), "a key released while queued starts as held");
    CHECK(!maremba::MarembaEngineTestAccess::IsNoteReleased(e, 64), "a held queued key starts released");
}

// N2-1: the contact time is capped per register: the top of the keyboard stays
// audible on the default patch (C7 within 15 dB of C4).
void TestTrebleNotBuried()
{
    std::cout << "[Voicing] Default patch: C7 within 15 dB of C4 ..." << std::endl;
    for (auto cfg : { std::make_pair(44100.0, 0), std::make_pair(96000.0, 2) }) {
        EngineParameters p;
        p.oversampling = cfg.second;
        auto level = [&](int note) {
            const Render r = RenderNote(cfg.first, p, note, 100.0f / 127.0f, 0.3);
            return 0.5 * (Db(Rms(r.ch[kMainL], 0, r.size())) + Db(Rms(r.ch[kMainR], 0, r.size())));
        };
        const double c4 = level(60), c7 = level(96);
        std::cout << "  -> " << cfg.first << "/" << (2 << cfg.second) << "x: C4 " << c4 << " dBFS, C7 " << c7 << " dBFS" << std::endl;
        CHECK(c7 > c4 - 15.0, "C7 is " << c4 - c7 << " dB below C4 at " << cfg.first);
    }
}

// N2-2: the level rises strictly with the key velocity on every curve, down to 1.
void TestVelocityStrictlyMonotonic()
{
    std::cout << "[Velocity] Level rises strictly over velocities 1..8 on every curve ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = 0.0f;
    int flat = 0;
    for (int curve = 0; curve < 4; ++curve) {
        p.velocityCurve = curve;
        double previous = 0.0;
        for (int v = 1; v <= 8; ++v) {
            const Render r = RenderNote(44100.0, p, 60, v / 127.0f, 0.2);
            const double e = Energy(r.ch[kCloseL], 0, r.size());
            if (!(e > previous)) {
                ++flat;
                std::cerr << "  curve " << curve << " velocity " << v << " is not louder than " << v - 1 << std::endl;
            }
            previous = e;
        }
    }
    CHECK(flat == 0, flat << " velocity steps without a level change");
}

// N2-3: with every mic level at 0 while a bar rings, the float main bus sits at
// exact 0 and its filter states must not stick at subnormal values.
void TestMainBusNoSubnormalAtZeroLevels()
{
    std::cout << "[Denormals] Main bus with all mic levels at 0 while a bar rings ..." << std::endl;
    for (auto cfg : { std::make_pair(44100.0, 0), std::make_pair(44100.0, 2), std::make_pair(192000.0, 2) }) {
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.oversampling = cfg.second;
        MarembaEngine e(cfg.first);
        e.SetParameters(p);
        e.NoteOn(48, 1.0f);
        Render r;
        RenderFrames(e, r, static_cast<int>(cfg.first), &p);
        p.closeLevel = p.farLevel = p.piezoLevel = 0.0f;
        RenderFrames(e, r, static_cast<int>(cfg.first), &p);
        float buf[kNumChannels][64];
        int bad = 0;
        for (int b = 0; b < static_cast<int>(cfg.first * 2.0 / 64); ++b) {
            e.SetParameters(p);
            e.RenderBatch(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], 64);
            if (!maremba::MarembaEngineTestAccess::MainBusNormal(e)) ++bad;
        }
        std::cout << "  -> " << cfg.first << "/" << (2 << cfg.second) << "x: " << bad << " batches with subnormal preamp/decimator state, voices "
                  << e.GetActiveVoiceCount() << std::endl;
        CHECK(e.GetActiveVoiceCount() == 1, "the bar stopped ringing (test needs it alive)");
        CHECK(bad == 0, "main bus cycles on subnormal state at zero mic levels");
    }
}

// T2-11: the engine's control tick flushes the mesh and the frame body.
void TestEngineFlushesSharedTails()
{
    std::cout << "[Denormals] The engine's control tick flushes mesh and frame body ..." << std::endl;
    float b[kNumChannels][64];
    {   // Mesh: B5 excites a resonator that the long A1... C2 ring does not drive
        EngineParameters p = CleanParams();
        p.model = 3;
        p.decay = 8.0f;
        p.sympathetic = 1.0f;
        p.bodyBloom = 1.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        e.NoteOn(71, 1.0f);
        e.NoteOn(36, 1.0f);
        int bad = 0, batches = 0;
        for (; batches < 44100 * 40 / 64 && e.GetActiveVoiceCount() > 0; ++batches) {
            e.SetParameters(p);
            e.RenderBatch(b[0], b[1], b[2], b[3], b[4], b[5], b[6], 64);
            if (!maremba::MarembaEngineTestAccess::MeshNormal(e) || !maremba::MarembaEngineTestAccess::BodyNormal(e)) ++bad;
        }
        std::cout << "  -> mesh: " << batches * 64 / 44100.0 << " s, " << bad << " batches with subnormal state" << std::endl;
        CHECK(bad == 0, "engine lets the mesh cycle on subnormal state (" << bad << " batches)");
    }
    {   // Body: a short note ends, the room tail keeps the engine awake for seconds
        EngineParameters p = CleanParams();
        p.decay = 0.1f;
        p.bodyBloom = 1.0f;
        p.farLevel = 1.0f;
        p.sympathetic = 0.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        e.NoteOn(84, 1.0f);
        int bad = 0, batches = 0;
        for (; batches < 44100 * 10 / 64 && !e.IsSilent(); ++batches) {
            e.SetParameters(p);
            e.RenderBatch(b[0], b[1], b[2], b[3], b[4], b[5], b[6], 64);
            if (!maremba::MarembaEngineTestAccess::BodyNormal(e)) ++bad;
        }
        std::cout << "  -> body: engine awake " << batches * 64 / 44100.0 << " s, " << bad << " batches with subnormal state" << std::endl;
        CHECK(batches * 64 > 44100, "test needs the engine awake after the note");
        CHECK(bad == 0, "engine lets the frame body cycle on subnormal state (" << bad << " batches)");
    }
}

// T2-3: notes struck during an oversampling fade are queued; a chord as large
// as the voice limit starts in full after the switch, and a larger burst
// neither overflows the queue nor loses the first kMaxPendingNotes notes.
void TestQueuedChordDuringSwitch()
{
    std::cout << "[Switch] A full chord struck during the oversampling fade starts after the switch ..." << std::endl;
    for (const int count : { 24, 40 }) {
        EngineParameters p = CleanParams();
        p.polyphony = 2;
        p.decay = 4.0f;
        p.sympathetic = 0.0f;
        MarembaEngine e(48000.0);
        e.SetParameters(p);
        e.NoteOn(90, 0.9f);
        Render r;
        RenderFrames(e, r, 64 * 4, &p);
        p.oversampling = 1;
        e.SetParameters(p);
        for (int i = 0; i < count; ++i) e.NoteOn(30 + i, 0.8f);
        RenderFrames(e, r, 48000 / 10, &p);
        std::cout << "  -> " << count << " queued: " << e.GetActiveVoiceCount() << " voices 100 ms later" << std::endl;
        CHECK(AllFinite(r), "non-finite output after a queued burst");
        CHECK(e.GetActiveVoiceCount() == 24, count << " queued notes: " << e.GetActiveVoiceCount() << " voices, expected 24");
    }
}

// T2-9: DampAll reaches the notes queued in the fade it interrupted, and only
// those: a later fade (after the switch or after a Reset) queues undamped notes.
void TestDampedQueueIsPerFade()
{
    std::cout << "[Stop] A stop during one fade does not damp notes queued in a later fade ..." << std::endl;
    for (bool viaReset : { false, true }) {
        EngineParameters p = CleanParams();
        p.decay = 8.0f;
        p.sympathetic = 0.0f;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        e.NoteOn(60, 0.9f);
        Render r;
        RenderFrames(e, r, 64 * 10, &p);
        p.oversampling = 1;
        e.SetParameters(p);
        e.NoteOn(45, 0.9f);
        e.DampAll();
        if (viaReset) {
            e.Reset();
            e.NoteOn(60, 0.9f);   // wakes the engine, so the next change fades
        }
        RenderFrames(e, r, 64 * 10, &p);
        p.oversampling = 0;
        e.SetParameters(p);       // second fade
        e.NoteOn(50, 0.9f);       // queued, no stop
        RenderFrames(e, r, 44100 * 3 / 2, &p);
        std::cout << "  -> " << (viaReset ? "after Reset and a new fade: " : "after a second fade: ") << e.GetActiveVoiceCount() << " voice(s)" << std::endl;
        CHECK(e.GetActiveVoiceCount() == 1, (viaReset ? "Reset left a stale damped-queue count" : "a note queued in a later fade was damped by an old stop"));
    }
}

// T2-6: Sympathetic (mesh) and Body Bloom to 0 and back while bars ring: the
// resonators restart from rest, no frozen tail resumes.
void TestMeshAndBodyBypassNoStaleTail()
{
    std::cout << "[Halo/Body] Sympathetic / Body 1 -> 0 -> 1 while ringing brings back no stale tail ..." << std::endl;
    for (int which = 0; which < 2; ++which) {
        auto run = [&](int mode) { // 0 steady on, 1 toggled, 2 always off, 3 first enabled at 1.5 s
            EngineParameters p = CleanParams();
            p.decay = 4.0f;
            p.farLevel = 0.0f;
            p.sympathetic = which == 0 ? 1.0f : 0.0f;
            p.bodyBloom = which == 1 ? 1.0f : 0.0f;
            float& knob = which == 0 ? p.sympathetic : p.bodyBloom;
            if (mode == 2 || mode == 3) knob = 0.0f;
            MarembaEngine e(44100.0);
            e.SetParameters(p);
            for (int n : { 48, 55, 60 }) e.NoteOn(n, 0.9f);
            Render r;
            RenderFrames(e, r, 44100 / 2, &p);
            if (mode == 1) knob = 0.0f;
            RenderFrames(e, r, 44100, &p);
            if (mode == 1 || mode == 3) knob = 1.0f;
            RenderFrames(e, r, 44100 / 2, &p);
            return r;
        };
        const Render steady = run(0), toggled = run(1), dry = run(2), fresh = run(3);
        const size_t at = 44100 / 2 + 44100, win = which == 0 ? 882 : 220;
        double eToggled = 0.0, eSteady = 0.0;
        for (size_t i = at; i < at + win; ++i) {
            const double t = toggled.ch[kCloseL][i] - dry.ch[kCloseL][i];
            const double s = steady.ch[kCloseL][i] - dry.ch[kCloseL][i];
            eToggled += t * t;
            eSteady += s * s;
        }
        std::cout << "  -> " << (which == 0 ? "halo" : "body") << " right after re-enable: " << Db(std::sqrt(eToggled / win))
                  << " dBFS (steady " << Db(std::sqrt(eSteady / win)) << ")" << std::endl;
        CHECK(eSteady > 0.0, "no " << (which == 0 ? "halo" : "body") << " to compare");
        // A freshly driven bank can exceed its steady output: detuned neighbors
        // can cancel differently during the build-up. Compare identical input
        // histories from rest instead of assuming an ordering of their energy.
        // This detects ANY revived old state, even one too quiet to exceed the
        // continuously running branch, throughout the entire post-enable tail.
        CHECK(eToggled > 0.0, "re-enabled " << (which == 0 ? "halo" : "body") << " stays silent");
        const double stale = DiffEnergy(toggled.ch[kCloseL], fresh.ch[kCloseL], at, toggled.size());
        std::cout << "  -> difference from a never-before-enabled branch: " << stale << std::endl;
        CHECK(stale == 0.0, "stale " << (which == 0 ? "halo" : "body") << " state differs from a fresh branch");
    }
}

// T2-10: a chord struck at the polyphony limit keeps every note of the chord
// (a voice being struck has no energy yet and must not be the steal victim).
void TestSameFrameChordAtLimit()
{
    std::cout << "[Steal] A chord struck at the voice limit keeps all of its notes ..." << std::endl;
    const int quietNotes[8] = { 49, 51, 54, 56, 58, 61, 63, 66 };
    auto run = [&](int poly) {
        EngineParameters p = CleanParams();
        p.sympathetic = 0.0f;
        p.decay = 4.0f;
        p.polyphony = poly;
        MarembaEngine e(44100.0);
        e.SetParameters(p);
        for (int n : quietNotes) e.NoteOn(n, 0.3f);
        Render r;
        RenderFrames(e, r, 8820, &p);
        e.NoteOn(71, 1.0f);
        e.NoteOn(75, 1.0f);
        RenderFrames(e, r, 44100 / 2, &p);
        return r;
    };
    const Render limited = run(0), free = run(1);
    const size_t from = 8820 + 2205, to = limited.size();
    for (int n : { 71, 75 }) {
        const double ratio = NotePower(limited, from, to, n) / NotePower(free, from, to, n);
        std::cout << "  -> note " << n << " kept " << ratio << std::endl;
        CHECK(ratio > 0.5, "chord note " << n << " stolen by its own chord");
    }
}

// T2-7: a volume change while the engine idles applies at once to the next note
// (no ramp from the stale gain).
void TestVolumeChangeWhileIdle()
{
    std::cout << "[Gain] A volume change while idle applies fully to the next strike ..." << std::endl;
    EngineParameters p = CleanParams();
    MarembaEngine moved(44100.0);
    moved.SetParameters(p);
    Render r;
    RenderFrames(moved, r, 64 * 4, &p);
    p.volume = 0.2f;
    moved.SetParameters(p);
    moved.NoteOn(72, 0.9f);
    Render a;
    RenderFrames(moved, a, 4410, &p);
    MarembaEngine fresh(44100.0);
    fresh.SetParameters(p);
    fresh.Reset();
    fresh.NoteOn(72, 0.9f);
    Render b;
    RenderFrames(fresh, b, 4410, &p);
    CHECK(DiffEnergy(a.ch[kMainL], b.ch[kMainL], 0, a.size()) == 0.0, "strike after an idle volume change ramps from the old gain");
}

void TestPhysicalStrikePositions()
{
    std::cout << "[Acoustics] Bar nodes, centre parity, and distinct cantilever excitation ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = p.bodyBloom = p.farLevel = p.piezoLevel = p.buzzAmount = 0.0f;
    p.malletType = 3;
    p.malletHardness = 0.9f;
    p.decay = 4.0f;
    auto partial = [](const Render& r, double frequency) {
        std::vector<double> x;
        for (size_t i = 662; i < 4410; ++i) x.push_back(r.ch[kCloseL][i] + r.ch[kCloseR][i]);
        return GoertzelPower(HannWindowed(x), 44100.0, frequency);
    };
    for (int model = 0; model < 3; ++model) {
        p.model = model;
        p.strikePosition = 0.5f;
        const Render centre = RenderNote(44100.0, p, 48, 0.9f, 0.12);
        p.strikePosition = 0.35f;
        const Render offCentre = RenderNote(44100.0, p, 48, 0.9f, 0.12);
        p.strikePosition = static_cast<float>(maremba::BarModeFundamentalNode(
            maremba::GetBarModeGeometry(static_cast<maremba::EMarimbaModel>(model), 48)));
        const Render node = RenderNote(44100.0, p, 48, 0.9f, 0.12);
        const double symmetryRatio = partial(centre, 4.0 * NoteHz(48)) / partial(offCentre, 4.0 * NoteHz(48));
        const double nodalRatio = partial(node, NoteHz(48)) / partial(centre, NoteHz(48));
        std::cout << "  -> model " << model << ": centre second-mode " << 10.0 * std::log10(symmetryRatio)
                  << " dB vs off-centre; fundamental at node " << 10.0 * std::log10(nodalRatio) << " dB" << std::endl;
        CHECK(symmetryRatio < 0.01, "a centred strike excites the antisymmetric bar mode");
        CHECK(nodalRatio < 0.01, "striking the fundamental node does not suppress its ring");
    }
    p.model = 3;
    p.strikePosition = 0.05f;
    const Render inner = RenderNote(44100.0, p, 48, 0.9f, 0.12);
    p.strikePosition = 0.95f;
    const Render tip = RenderNote(44100.0, p, 48, 0.9f, 0.12);
    const double tineRatio = partial(tip, NoteHz(48)) / partial(inner, NoteHz(48));
    std::cout << "  -> tine tip/inner fundamental " << 10.0 * std::log10(tineRatio) << " dB" << std::endl;
    CHECK(tineRatio > 2.0, "kalimba incorrectly has a free-bar mirror-symmetric excitation");

    // Independent physical boundary conditions and spatial averaging limit.
    CHECK(std::abs(maremba::ModalContactWeight(0, 0.0, 0.0, true)) < 1e-12, "a clamped tine moves at its root");
    CHECK(std::abs(maremba::ModalContactWeight(0, 0.2242, 0.0, false)) < 0.001, "free-bar fundamental node misplaced");
    const double narrow = maremba::ModalContactWeight(6, 0.5, 0.0, false);
    const double wide = maremba::ModalContactWeight(6, 0.5, 0.20, false);
    CHECK(std::abs(wide) < 0.4 * std::abs(narrow), "a wide mallet does not cancel short spatial wavelengths");
    CHECK(maremba::ModalContactParticipation(1, 0.5, 0.12, false) > 0.01,
          "wide contact over an antisymmetric node must still dissipate energy");
}

void TestContactImpulseConservation()
{
    std::cout << "[Contact] Finite contact is nonadhesive and conserves impulse across rates ..." << std::endl;
    for (double fs : {44100.0, 88200.0, 192000.0, 768000.0, 1536000.0}) {
        for (double duration : {0.00012, 0.0006537, 0.004}) {
            for (double skew : {-0.3, 0.0, 0.6}) {
                const double samples = std::max(3.0, fs * duration);
                double total = 0.0;
                for (int i = 0; i < static_cast<int>(std::ceil(samples)); ++i) {
                    const double impulse = maremba::ContactCumulative((i + 1) / samples, skew)
                                         - maremba::ContactCumulative(i / samples, skew);
                    CHECK(impulse >= -1e-14, "nonadhesive contact exerts a pulling force");
                    total += impulse;
                }
                CHECK(std::abs(total - 1.0) < 1e-12, "contact impulse depends on rate or duration");
                CHECK(maremba::ContactCumulative(1.1, skew) == maremba::ContactCumulative(1.0, skew),
                      "contact exerts force after separation");
            }
        }
    }
}

void TestTunedMiddleRegister()
{
    std::cout << "[Acoustics] The tuned second bar mode survives through C5 ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = p.bodyBloom = p.farLevel = p.buzzAmount = 0.0f;
    p.strikePosition = 0.35f;
    p.malletHardness = 0.9f;
    p.malletType = 3;
    p.decay = 4.0f;
    const Render r = RenderNote(44100.0, p, 72, 1.0f, 0.1);
    std::vector<double> x;
    for (size_t i = 441; i < 3528; ++i) x.push_back(r.ch[kCloseL][i] + r.ch[kCloseR][i]);
    x = HannWindowed(x);
    const double tuned = GoertzelPower(x, 44100.0, 4.0 * NoteHz(72));
    const double oldUntuned = GoertzelPower(x, 44100.0, 2.756 * NoteHz(72));
    std::cout << "  -> tuned/untuned line power " << 10.0 * std::log10(tuned / oldUntuned) << " dB" << std::endl;
    CHECK(tuned > oldUntuned * 100.0, "middle-register second mode loses its concert tuning");
}

void TestRestrikePreservesStoredEnergy()
{
    std::cout << "[Restrike] Changed decay/tuning preserves the existing ring before contact ..." << std::endl;
    for (double fs : {88200.0, 384000.0}) {
        for (int model = 0; model < 4; ++model) {
            for (int note : {0, 36, 72}) {
                maremba::MarembaVoice voice;
                maremba::FastPRNG random(12345);
                const auto instrument = static_cast<maremba::EMarimbaModel>(model);
                auto strike = [&](float decay, float tune) {
                    voice.Trigger(note, 0.8f, instrument, 0.4f, 0.35f, tune * 0.5f,
                                  0.7f, decay, 0.0f, 0.0f, 0.0f, fs, random, tune, 0.0f, 1, 0.0f);
                };
                strike(8.0f, -100.0f);
                float close, far, piezo;
                for (int i = 0; i < static_cast<int>(0.023 * fs); ++i) voice.ProcessSample(close, far, piezo);
                const double before = voice.ComputeEnergy();
                strike(0.1f, 100.0f);
                const double after = voice.ComputeEnergy();
                CHECK(before > 1.0e-8, "restrike test requires an audible stored ring");
                CHECK(std::abs(after / before - 1.0) < 1.0e-5,
                      "restrike pumps stored energy: model " << model << ", note " << note << ", rate " << fs << ", ratio " << after / before);
                for (int i = 0; i < 2048; ++i) voice.ProcessSample(close, far, piezo);
                CHECK(std::isfinite(close) && std::isfinite(far) && std::isfinite(piezo), "restrike transition produces non-finite audio");
            }
        }
    }
}

void TestVelocityDepths()
{
    std::cout << "[Velocity routes] Defaults, independent destinations, curve, full-velocity reference and strike boundaries ..." << std::endl;
    EngineParameters defaults;
    CHECK(defaults.velocityToHardness==.20f && defaults.velocityToPosition==.20f && defaults.velocityToDecay==.20f,
          "velocity sensitivities must all default to 20%");
    using Field = float EngineParameters::*;
    const std::array<std::pair<Field,Field>,3> routes{{
        {&EngineParameters::velocityToHardness,&EngineParameters::malletHardness},
        {&EngineParameters::velocityToPosition,&EngineParameters::strikePosition},
        {&EngineParameters::velocityToDecay,&EngineParameters::decay}}};
    auto p=CleanParams();
    p.sympathetic=p.bodyBloom=p.farLevel=p.preampDrive=p.compAmount=p.warmth=0;
    for (const auto& route:routes) {
        for (int curve:{0,1}) for (float velocity:{.25f,1.f}) {
            p.velocityCurve=curve;
            const auto unmodulated=RenderNote(44100,p,60,velocity,.2);
            for (float depth:{0.f,.2f,1.f}) {
                auto routed=p; routed.*route.first=depth;
                auto reference=p;
                // The existing velocity mapper adds a 1% physical drive floor.
                const float curved=curve==0?std::sqrt(velocity):velocity;
                const float mapped=.01f+(1.f-.01f)*curved;
                reference.*route.second *= 1-depth*(1-mapped);
                reference.decay=std::max(.1f,reference.decay);
                const auto actual=RenderNote(44100,routed,60,velocity,.2);
                const auto expected=RenderNote(44100,reference,60,velocity,.2);
                CHECK(actual.ch==expected.ch, "velocity route misses its destination, uses raw velocity or changes another parameter");
                if (depth==0 || velocity==1)
                    CHECK(actual.ch==unmodulated.ch, "zero depth/full-velocity reference changes the base setting");
                else if (depth==1) {
                    const double relative=DiffEnergy(actual.ch[kCloseL],unmodulated.ch[kCloseL],0,actual.size())
                        / Energy(unmodulated.ch[kCloseL],0,unmodulated.size());
                    CHECK(relative>1e-4, "velocity route has no audible response to a soft stroke");
                }
            }
        }
    }
    p.velocityCurve=1;
    auto ring=[&](bool change, bool restrike) {
        MarembaEngine e(44100);e.SetParameters(p);e.NoteOn(60,.4f);
        Render r;RenderFrames(e,r,4410);
        auto q=p;
        if(change) q.velocityToHardness=q.velocityToPosition=q.velocityToDecay=1;
        e.SetParameters(q);
        CHECK(e.GetParameters().malletHardness==p.malletHardness && e.GetParameters().strikePosition==p.strikePosition
              && e.GetParameters().decay==p.decay, "per-note modulation changed the stored base knobs");
        if(restrike)e.NoteOn(60,.4f);
        RenderFrames(e,r,8820);return r;
    };
    CHECK(ring(true,false).ch==ring(false,false).ch, "depth automation altered an already ringing note");
    const auto changed=ring(true,true),fixed=ring(false,true);
    CHECK(DiffEnergy(changed.ch[kCloseL],fixed.ch[kCloseL],4410,changed.size())>1e-4,
          "restrikes do not pick up the new velocity depths");
}

void TestCharacterRangeAndLiveBuzz()
{
    std::cout << "[Character] Medium-velocity audibility and live Buzz ..." << std::endl;
    EngineParameters p = CleanParams();
    p.sympathetic = p.bodyBloom = p.farLevel = p.piezoLevel = 0;
    p.compAmount = p.preampDrive = p.warmth = 0;
    const auto dry = RenderNote(44100, p, 60, .72f, .5);
    // Artifacts produces occasional imperfect hits, so its audibility and
    // delayed mechanical content are covered by the 64-strike ensemble in
    // EngineRealismTest. A single arbitrary strike must be allowed to be clean.
    for (auto field : {&EngineParameters::bodyBloom, &EngineParameters::buzzAmount}) {
        auto q = p; q.*field = 1;
        const auto wet = RenderNote(44100, q, 60, .72f, .5);
        const size_t end = wet.size();
        const double relative = DiffEnergy(dry.ch[kCloseL], wet.ch[kCloseL], 0, end) / Energy(dry.ch[kCloseL], 0, end);
        std::cout << "  -> " << (field == &EngineParameters::bodyBloom ? "Bloom" : "Buzz")
                  << " difference " << 10*std::log10(relative) << " dB re dry" << std::endl;
        CHECK(relative > .005, "character endpoint remains too subtle at normal velocity");
    }
    auto live = [&](bool change) {
        MarembaEngine e(44100); e.SetParameters(p); e.NoteOn(60, .72f);
        Render r; RenderFrames(e, r, 2205);
        auto q = p; q.buzzAmount = change ? 1 : 0; e.SetParameters(q);
        RenderFrames(e, r, 8820); return r;
    };
    const auto off = live(false), on = live(true);
    CHECK(DiffEnergy(off.ch[kCloseL], on.ch[kCloseL], 0, 2205) == 0, "Buzz modifies earlier audio");
    CHECK(DiffEnergy(off.ch[kCloseL], on.ch[kCloseL], 2205, on.size()) / Energy(off.ch[kCloseL], 2205, on.size()) > .005,
          "Buzz automation waits for another note");
    // The membrane drains the resonator that loads the bar, so the bar-mounted
    // pickup may change indirectly, but it must never carry the buzz itself.
    CHECK(DiffEnergy(off.ch[kPiezo], on.ch[kPiezo], 0, on.size()) < 1e-3 * Energy(off.ch[kPiezo], 0, on.size()),
          "Buzz leaked into direct Piezo");
}

void TestWarmthRange()
{
    std::cout << "[Warmth] Broad low/high tilt, smooth live changes, repeatable reset ..." << std::endl;
    auto tone = [](double hz, float warmth) {
        maremba::AnalogPreamp amp; amp.SetSampleRate(48000); amp.Reset();
        double energy = 0;
        for (int i=0;i<24000;++i) {
            float l,r; const float x = .1f*std::sin(2*M_PI*hz*i/48000);
            amp.Process(x,x,0,warmth,l,r);
            if (i>=12000) energy += l*l;
        }
        return std::sqrt(energy/12000);
    };
    CHECK(Db(tone(80,1)/tone(80,-1)) > 9, "Warmth low shelf lacks range");
    CHECK(Db(tone(8000,-1)/tone(8000,1)) > 9, "Warmth high shelf lacks range");
    maremba::AnalogPreamp changed, steady;
    changed.SetSampleRate(48000); steady.SetSampleRate(48000);
    float a=0,b=0,unused=0;
    for (int i=0;i<4800;++i) {
        const float x=.1f*std::sin(2*M_PI*250*i/48000);
        changed.Process(x,x,0,-1,a,unused); steady.Process(x,x,0,-1,b,unused);
    }
    changed.Process(.1f,.1f,0,1,a,unused); steady.Process(.1f,.1f,0,-1,b,unused);
    CHECK(std::abs(a-b)<.002, "Warmth jumps at an automation edge");
}

void TestWoodModelSeparation()
{
    std::cout << "[Models] Padauk has a shorter ring than Rosewood at matched settings ..." << std::endl;
    auto p = CleanParams();
    p.sympathetic=p.bodyBloom=p.farLevel=p.preampDrive=p.compAmount=p.warmth=0;
    for (int note : {48,60,72}) {
        p.model=0; auto rose = RenderNote(44100,p,note,.72f,.6);
        p.model=1; auto padauk = RenderNote(44100,p,note,.72f,.6);
        auto envelope = [](const Render& r) { return Db(Rms(r.ch[kCloseL],8820,22050)/Rms(r.ch[kCloseL],0,4410)); };
        const double delta=envelope(padauk)-envelope(rose);
        std::cout << "  -> note " << note << " Padauk late/early " << delta << " dB below Rosewood" << std::endl;
        CHECK(delta < -4, "Padauk and Rosewood decay remain too similar");
    }
}

void TestRoomAlgorithms()
{
    std::cout << "[Rooms] Distinct tails, stereo, reset, switching, legacy Wood and realtime safety ..." << std::endl;
    for (double rate : {44100.,48000.,96000.}) {
        std::array<std::vector<float>,3> response;
        std::array<double,3> late{};
        for (int type=0;type<3;++type) {
            maremba::RoomBank room; room.Allocate(rate); room.SetSampleRate(rate); room.SetType(type);
            auto& left=response[type]; left.resize(static_cast<size_t>(4*rate));
            std::vector<float> right(left.size());
            for (int repeat=0;repeat<2;++repeat) {
                if (repeat) room.Reset();
                gAllocationCount=0; gCountAllocations=true;
                bool exact=true,finite=true;
                for (size_t i=0;i<left.size();++i) {
                    float dl,dr,l,r; const float x=i==0?.1f:0;
                    room.Process(x,x,1,dl,dr,l,r);
                    finite &= std::isfinite(l) && std::isfinite(r);
                    if (repeat) exact &= l==left[i] && r==right[i];
                    else {left[i]=l;right[i]=r;}
                    exact &= dl==.7f*x && dr==.7f*x;
                }
                gCountAllocations=false;
                CHECK(exact && finite && gAllocationCount==0, "room reset/dry path/finite/allocation contract failed");
            }
            const double total=Energy(left,0,left.size());
            late[type]=Energy(left,static_cast<size_t>(rate),static_cast<size_t>(3*rate))/total;
            CHECK(total>1e-5 && Peak(left)<.2, "room impulse lost or unstable");
            CHECK(DiffEnergy(left,right,0,left.size())/total > .1, "room has no stereo spread");
            CHECK(Energy(left,static_cast<size_t>(3*rate),left.size())/total<1e-5, "room fails to decay");
        }
        CHECK(late[2] > 20*late[1], "Hall is not appreciably longer than Chamber");
        for (int a=0;a<3;++a) for (int b=a+1;b<3;++b)
            CHECK(DiffEnergy(response[a],response[b],0,response[a].size())/Energy(response[a],0,response[a].size()) > .2,
                  "room algorithms sound identical");
        maremba::AcousticRoom legacy; legacy.Allocate(rate); legacy.SetSampleRate(rate); legacy.Reset();
        for (size_t i=0;i<response[0].size();++i) {
            float dl,dr,l,r; const float x=i==0?.1f:0;
            legacy.Process(x,x,1,dl,dr,l,r);
            CHECK(l==response[0][i], "Wood changes the legacy room");
        }
        maremba::RoomBank switched, reference;
        for (auto* room : {&switched,&reference}) {room->Allocate(rate);room->SetSampleRate(rate);}
        gAllocationCount=0; gCountAllocations=true;
        bool finite=true,continuous=true;
        for (int i=0;i<static_cast<int>(rate);++i) {
            const int at=static_cast<int>(.1*rate);
            if (i==at) switched.SetType(2);
            if (i==at+10) switched.SetType(1); // request during an active fade
            if (i==static_cast<int>(.4*rate)) switched.SetType(0);
            float dl,dr,l,r,refL,refR; float x=.01f*std::sin(i*.07f);
            switched.Process(x,x,1,dl,dr,l,r); reference.Process(x,x,1,dl,dr,refL,refR);
            if (i==at) continuous &= l==refL && r==refR;
            finite &= std::isfinite(l) && std::isfinite(r) && std::abs(l)<1 && std::abs(r)<1;
        }
        gCountAllocations=false;
        CHECK(finite && continuous && gAllocationCount==0, "live room switch clicks, allocates or diverges");
        std::cout << "  -> " << rate << " Hz: late energy Wood=" << late[0] << " Chamber=" << late[1] << " Hall=" << late[2] << std::endl;
    }
    auto p=CleanParams(); p.roomType=2; p.decay=.1f; p.farLevel=1;
    MarembaEngine e(44100); e.SetParameters(p); e.NoteOn(60,.72f);
    Render r; RenderFrames(e,r,44100*12,&p);
    CHECK(AllFinite(r) && e.IsSilent(), "Hall never lets the engine idle");
}

// Opt-in native timing probe, so machine scheduling cannot make correctness
// tests flaky. Each block includes parameter delivery and periodic note-on.
void RunPerformanceProbe()
{
    constexpr int blocks = 4096;
    constexpr int frames = 64;
    volatile float sink = 0.0f;
    for (int quality = 0; quality < 3; ++quality) {
        EngineParameters p; // shipped defaults, changing only quality
        MarembaEngine e(48000.0);
        p.oversampling = quality;
        e.SetParameters(p);
        for (int n = 0; n < 16; ++n) e.NoteOn(42 + n, 0.8f);
        std::vector<double> wall, cpu;
        wall.reserve(blocks);
        cpu.reserve(blocks);
        float out[kNumChannels][frames];
        int wallMisses = 0, cpuMisses = 0;
        const double deadlineUs = 1e6 * frames / 48000.0;
        for (int b = -256; b < blocks; ++b) {
            timespec t0{}, t1{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
            const auto start = std::chrono::steady_clock::now();
            e.SetParameters(p);
            if (b >= 0 && b % 32 == 0) e.NoteOn(42 + (b / 32) % 16, 0.8f);
            e.RenderBatch(out[0], out[1], out[2], out[3], out[4], out[5], out[6], frames);
            const double elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
            const double charged = 1e6 * (t1.tv_sec - t0.tv_sec) + 1e-3 * (t1.tv_nsec - t0.tv_nsec);
            sink = sink + out[0][0];
            if (b < 0) continue;
            wall.push_back(elapsed);
            cpu.push_back(charged);
            wallMisses += elapsed > deadlineUs;
            cpuMisses += charged > deadlineUs;
        }
        std::sort(wall.begin(), wall.end());
        std::sort(cpu.begin(), cpu.end());
        std::cout << "48000 Hz, 64 frames, 16 voices, " << (2 << quality) << "x; " << blocks << " blocks; wall us p50="
                  << wall[blocks / 2] << " p95=" << wall[blocks * 95 / 100] << " p99=" << wall[blocks * 99 / 100]
                  << " max=" << wall.back() << "; CPU us p50=" << cpu[blocks / 2] << " p99=" << cpu[blocks * 99 / 100]
                  << " max=" << cpu.back() << "; deadline misses wall=" << wallMisses << " CPU=" << cpuMisses << std::endl;
    }
    (void)sink;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--velocity-depths") {
        TestVelocityDepths();
        return gFailures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        RunPerformanceProbe();
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--limiter") {
        TestAnalogLimiterCurveAndLink();
        TestAnalogLimiterBallistics();
        TestAnalogLimiterState();
        TestPreampLimiterContainment();
        return gFailures ? 1 : 0;
    }
    if (argc == 2 && (std::string(argv[1]) == "--repeat-variation" || std::string(argv[1]) == "--repeat-measure")) {
        TestOrganicRepeatVariation(std::string(argv[1]) == "--repeat-measure");
        return gFailures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--buzz") {
        TestMirlitonBuzz();
        TestCharacterRangeAndLiveBuzz();
        return gFailures ? 1 : 0;
    }
    std::cout << "=== Running Maremba DSP Verification Suite ===" << std::endl;

    TestNoteRangeStability();
    TestKeyboardPanning();
    TestStereoWidth();
    TestMirlitonBuzz();
    TestAnalogLimiterCurveAndLink();
    TestAnalogLimiterBallistics();
    TestAnalogLimiterState();
    TestPreampLimiterContainment();
    TestLegacyPitchGlideIgnored();
    TestSympatheticHalo();
    TestBodyBloom();
    TestVelocityAndMixerSilence();
    TestDetune();
    TestStrikerHierarchy();
    TestKalimbaSustain();
    TestStrikeJitter();
    TestOrganicRepeatVariation();
    TestMeshCoupling();
    TestRestrikeContinuity();
    TestVoicesDieAndEngineIdles();
    TestFarReverbWithPerBatchParameters();
    TestNoAllocations();
    TestPitchAccuracy();
    TestRateInvariance();
    TestOversamplingSwitch();
    TestPolyphonyReduction();
    TestDampAll();
    TestParameterFuzz();
    TestRollStability();
    TestSubRangeRendering();
    TestResetDeterminism();
    TestChordNoiseDecorrelated();
    TestDirectOutLevels();
    TestPitchBendRetunesRingingNotes();
    TestNoteOffIsSoundNeutral();
    TestDampAllIsGentle();
    TestDampAllReachesQueuedNotes();
    TestSwitchLeavesNoFrozenVoices();
    TestStealVictim();
    TestStealSparesJustStruckNote();
    TestNoDeadParameters();
    TestMaxDecayRateInvariant();
    TestHaloRateInvariant();
    TestDirectOutMatchesMain();
    TestDirectOutHeadroom();
    TestVolumeSmoothing();
    TestBendLeavesNothingBehind();
    TestNoSubnormalStates();
    TestWidthAndRoomImage();
    TestRoomBypassNoStaleTail();
    TestMembraneRestrikeFreesVoice();
    TestModelChangeRestrikeNoClick();
    TestBendModulationStability();
    TestChordBurstFadesVictims();
    TestOversamplingRevertKeepsNotes();
    TestReleaseWhileQueued();
    TestTrebleNotBuried();
    TestVelocityStrictlyMonotonic();
    TestMainBusNoSubnormalAtZeroLevels();
    TestEngineFlushesSharedTails();
    TestQueuedChordDuringSwitch();
    TestDampedQueueIsPerFade();
    TestMeshAndBodyBypassNoStaleTail();
    TestSameFrameChordAtLimit();
    TestVolumeChangeWhileIdle();
    TestPhysicalStrikePositions();
    TestContactImpulseConservation();
    TestTunedMiddleRegister();
    TestRestrikePreservesStoredEnergy();
    TestVelocityDepths();
    TestCharacterRangeAndLiveBuzz();
    TestWarmthRange();
    TestWoodModelSeparation();
    TestRoomAlgorithms();

    if (gFailures > 0) {
        std::cerr << "\n>>> " << gFailures << " MAREMBA DSP CHECK(S) FAILED <<<" << std::endl;
        return 1;
    }
    std::cout << "\n>>> ALL MAREMBA DSP VERIFICATION CHECKS PASSED SUCCESSFULLY! <<<" << std::endl;
    return 0;
}
