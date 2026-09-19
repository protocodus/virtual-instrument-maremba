// Maremba DSP verification suite.
//
// Build & run (from Examples/Maremba):
//   clang++ -std=c++17 -O3 -Wall -Wextra -Werror Tests/test_dsp.cpp DSP/*.cpp -o Tests/test_dsp && ./Tests/test_dsp
//
// Every feature test is differential: it renders with the feature on and off
// (from identical seeds) and asserts a clear difference, so a test cannot pass
// with the feature switched off.

#include "../DSP/MarembaEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

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
    p.strikeJitter = 0.0f;
    p.pitchGlide = 0.0f;
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

    // Rosewood: inert up to 0.2 (factory patches keep their sound), audible above.
    EngineParameters r = CleanParams();
    r.buzzAmount = 0.0f;
    Render off = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    r.buzzAmount = 0.2f;
    Render atOnset = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    r.buzzAmount = 0.6f;
    Render on = RenderNote(44100.0, r, 48, 0.8f, 0.4);
    CHECK(DiffEnergy(atOnset.ch[kCloseL], off.ch[kCloseL], 0, off.size()) == 0.0, "Rosewood buzz <= 0.2 must be inert");
    const double rosewoodBuzz = DiffEnergy(on.ch[kCloseL], off.ch[kCloseL], 0, off.size()) / Energy(off.ch[kCloseL], 0, off.size());
    std::cout << "  -> Rosewood buzz 0.6: " << 10.0 * std::log10(rosewoodBuzz) << " dB re bar" << std::endl;
    CHECK(rosewoodBuzz > 1e-4, "Rosewood buzz above 0.2 is inaudible");
}

void TestPreampCompressorContainment()
{
    std::cout << "[Dynamics] Preamp + compressor keep a hard chord contained ..." << std::endl;
    MarembaEngine engine(44100.0);
    EngineParameters p;
    p.preampDrive = 0.9f;
    p.compAmount = 0.8f;
    p.compAttack = 5.0f;
    p.compRelease = 50.0f;
    engine.SetParameters(p);
    for (int n : { 48, 52, 55, 60 }) engine.NoteOn(n, 1.0f);
    Render r;
    RenderFrames(engine, r, 64 * 40);
    CHECK(AllFinite(r), "non-finite output");
    CHECK(Peak(r.ch[kMainL]) <= 1.5, "compressor/preamp failed to contain level: " << Peak(r.ch[kMainL]));
}

void TestPitchGlide()
{
    std::cout << "[Glide] Attack pitch glide is audible (differential) ..." << std::endl;
    EngineParameters p = CleanParams();
    p.model = 3; // clean tine
    p.sympathetic = 0.0f;
    p.pitchGlide = 0.0f;
    Render off = RenderNote(44100.0, p, 69, 1.0f, 0.3);
    p.pitchGlide = 1.0f;
    Render on = RenderNote(44100.0, p, 69, 1.0f, 0.3);
    // A ~18-cent glide at 440 Hz shifts the phase by ~0.1 cycle during the attack.
    const double rel = DiffEnergy(on.ch[kCloseL], off.ch[kCloseL], 0, 2205) / Energy(off.ch[kCloseL], 0, 2205);
    std::cout << "  -> glide difference in the first 50 ms: " << 10.0 * std::log10(rel) << " dB" << std::endl;
    CHECK(rel > 1e-3, "pitch glide is inaudible");
    // Settled pitch is unaffected
    const double f = EstimateFrequency(on.ch[kCloseL], on.ch[kCloseR], 4410, on.size(), 44100.0, 440.0);
    CHECK(std::fabs(Cents(f, 440.0)) < 1.0, "glide leaves the note detuned: " << Cents(f, 440.0) << " c");
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
    std::cout << "[Body] Frame body bloom level (differential, ~-26 dB below the bar) ..." << std::endl;
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
    CHECK(db > -32.0 && db < -20.0, "body bloom level out of range: " << db);
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
    // strike draws later PRNG values. With jitter 0 only the clack noise differs.
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

void TestMeshCoupling()
{
    std::cout << "[Mesh] Zero semitone bleed and every consonant interval coupled ..." << std::endl;
    maremba::SympatheticMesh mesh;
    mesh.Configure(88200.0);
    for (int s = 0; s < 1000; ++s) {
        mesh.BeginSample();
        mesh.AccumulateVoice(60, static_cast<float>(std::sin(2.0 * M_PI * 261.63 * s / 88200.0)));
        float l, r;
        mesh.Process(1.0f, l, r);
    }
    CHECK(std::fabs(mesh.GetResonatorOutput(12)) > 0.0f, "C4 unison resonator not excited");
    CHECK(mesh.GetResonatorOutput(11) == 0.0f && mesh.GetResonatorOutput(13) == 0.0f, "semitone resonators excited");

    int missing = 0;
    for (int n = 0; n < 128; ++n) {
        const auto& list = mesh.GetCouplingList(n);
        CHECK(list.count <= maremba::VoiceCouplingList::kCapacity, "coupling list overflow");
        for (int i = 0; i < maremba::SympatheticMesh::kNumResonators; ++i) {
            const int delta = std::abs(n - (48 + i));
            const int cls = delta % 12;
            const bool consonant = (cls == 0 || cls == 4 || cls == 5 || cls == 7);
            bool present = false;
            for (int k = 0; k < list.count; ++k) present = present || list.entries[k].resonatorIdx == i;
            if (consonant != present) ++missing;
        }
    }
    CHECK(missing == 0, missing << " consonant couplings missing or dissonant ones present");
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
    std::cout << "[Tuning] Notes 24..108 within +/-1 cent at 44.1/48/96/192 kHz x 2/4/8 ..." << std::endl;
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
                Render r = RenderNote(sr, p, note, 0.8f, seconds);
                const double c = Cents(EstimateFrequency(r.ch[kCloseL], r.ch[kCloseR], 0, r.size(), sr, NoteHz(note)), NoteHz(note));
                worst = std::max(worst, std::fabs(c));
                CHECK(std::fabs(c) < 1.0, "note " << note << " at " << sr << "/" << (2 << os) << "x is " << c << " cents off");
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
    for (double sr : { 44100.0, 192000.0 }) {
        MarembaEngine engine(sr);
        bool finite = true;
        for (int round = 0; round < 40; ++round) {
            EngineParameters p;
            float* fields[] = { &p.malletHardness, &p.strikePosition, &p.resonatorTune, &p.resonatorCoupling, &p.decay,
                                &p.buzzAmount, &p.artifacts, &p.sympathetic, &p.pitchGlide, &p.bodyBloom, &p.closeLevel,
                                &p.farLevel, &p.piezoLevel, &p.stereoWidth, &p.preampDrive, &p.warmth, &p.compAmount,
                                &p.compAttack, &p.compRelease, &p.volume, &p.tuneCents, &p.detune, &p.strikeJitter };
            for (float* f : fields) if (next() % 3 == 0) *f = pick();
            p.model = static_cast<int>(next() % 9) - 2;
            p.malletType = static_cast<int>(next() % 9) - 2;
            p.oversampling = static_cast<int>(next() % 7) - 2;
            p.polyphony = static_cast<int>(next() % 7) - 2;
            p.velocityCurve = static_cast<int>(next() % 9) - 2;
            engine.SetParameters(p);
            engine.SetPitchBendCents(pick());
            engine.NoteOn(static_cast<int>(next() % 140) - 6, pick());
            engine.NoteOn(static_cast<int>(next() % 128), 1.0f);
            Render r;
            RenderFrames(engine, r, 64 * 6, &p);
            finite = finite && AllFinite(r);
        }
        CHECK(finite, "non-finite output under parameter fuzz at " << sr);
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
    return GoertzelPower(x, 44100.0, NoteHz(note));
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
    for (int mode = 0; mode < 2; ++mode) {
        const int quiet = mode == 0 ? 5 : -1, released = mode == 0 ? -1 : 3;
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

// Every sound parameter changes the output (dead-knob sweep), and the velocity
// curves go the documented way: Soft (0) plays louder than Linear, Hard (2) quieter.
void TestNoDeadParameters()
{
    std::cout << "[Knobs] Every engine parameter changes the output ..." << std::endl;
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
        { "pitchGlide", [](EngineParameters& p) { p.pitchGlide = 1.0f; } },
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

// SND-4/EN-5: every factory patch, at its stored mallet and levels, keeps a
// vel-127 4-note chord at or below 0 dBFS on every direct out.
void TestDirectOutHeadroomFactoryPatches()
{
    std::cout << "[Gain] Direct outs <= 0 dBFS on a vel-127 chord for every factory patch ..." << std::endl;
    // Stored 0..1 values of Resources/Public/*.repatch in motherboard order:
    // model, malletType, malletHardness, strikePosition, strikeJitter, resonatorTune,
    // resonatorCoupling, decay, buzzAmount, artifacts, sympathetic, pitchGlide,
    // bodyBloom, closeLevel, farLevel, piezoLevel, stereoWidth, preampDrive, warmth,
    // compAmount, compAttack, compRelease, volume, oversampling, velocityCurve,
    // polyphony, masterTune, detune. (A velocity of 1.0 maps to 1.0 on every curve.)
    struct Patch { const char* name; double v[28]; };
    static const Patch kPatches[] = {
        { "Ambient Cathedral Bloom", { 0, 0, 0.3000, 0.5000, 0.1500, 0.5000, 0.9500, 0.2658, 0.0000, 0.2000, 0.8500, 0.2500, 0.9000, 0.3000, 1.0000, 0.0000, 0.9000, 0.1000, 0.4250, 0.2000, 0.5918, 0.4792, 0.8500, 0, 0, 2, 0.5000, 0.0000 } },
        { "Ancestral Balafon", { 2, 2, 0.6000, 0.5200, 0.2500, 0.5000, 0.7500, 0.1076, 0.5000, 0.6500, 0.5500, 0.3500, 0.7500, 0.8500, 0.5500, 0.3000, 0.6000, 0.3500, 0.4500, 0.3500, 0.1224, 0.1875, 0.8000, 0, 3, 1, 0.5000, 0.0800 } },
        { "Chiapas Folk Fiesta", { 1, 2, 0.6800, 0.5800, 0.2000, 0.3800, 0.6500, 0.0949, 0.1000, 0.5500, 0.4000, 0.4500, 0.5000, 0.8800, 0.3500, 0.4000, 0.6500, 0.3000, 0.6250, 0.4000, 0.1020, 0.1458, 0.7800, 0, 3, 1, 0.5000, 0.0500 } },
        { "Concert Grand Rosewood", { 0, 1, 0.4000, 0.5000, 0.1500, 0.5000, 0.7500, 0.1139, 0.0000, 0.3500, 0.4500, 0.3000, 0.5500, 0.8500, 0.5000, 0.2500, 0.5000, 0.1200, 0.5500, 0.2500, 0.2245, 0.2083, 0.8000, 0, 1, 1, 0.5000, 0.0000 } },
        { "Direct Piezo Mallet", { 0, 3, 0.7000, 0.6500, 0.1000, 0.5000, 0.3000, 0.0633, 0.0000, 0.3000, 0.2000, 0.3500, 0.3000, 0.2000, 0.0000, 1.0000, 0.2500, 0.4500, 0.6500, 0.5000, 0.0204, 0.0833, 0.7500, 0, 2, 1, 0.5000, 0.0000 } },
        { "Hard Acrylic Virtuoso", { 0, 2, 0.8500, 0.6200, 0.1000, 0.5500, 0.6000, 0.0823, 0.0000, 0.4000, 0.3000, 0.5000, 0.4000, 0.9500, 0.3000, 0.4500, 0.4500, 0.2500, 0.7000, 0.4500, 0.0612, 0.1250, 0.7800, 0, 2, 1, 0.5000, 0.0000 } },
        { "Kalimba Pick Sparkle", { 3, 3, 0.7500, 0.6000, 0.1500, 0.5000, 0.6000, 0.1139, 0.0000, 0.4000, 0.5000, 0.2500, 0.4500, 0.9000, 0.3500, 0.4000, 0.6500, 0.2000, 0.6000, 0.3500, 0.1020, 0.1458, 0.7800, 0, 2, 1, 0.5000, 0.0000 } },
        { "Kalimba Thumb Lullaby", { 3, 0, 0.2500, 0.4500, 0.2000, 0.5000, 0.8000, 0.1772, 0.0000, 0.2000, 0.6000, 0.1000, 0.7500, 0.8500, 0.5500, 0.0500, 0.6000, 0.0800, 0.3500, 0.1500, 0.4898, 0.3333, 0.8500, 0, 0, 2, 0.5000, 0.0300 } },
        { "Mayan Dance Padauk", { 1, 1, 0.5000, 0.5000, 0.2000, 0.4200, 0.7000, 0.1139, 0.0500, 0.4500, 0.5000, 0.4000, 0.6000, 0.8000, 0.4500, 0.3500, 0.5500, 0.2000, 0.5000, 0.3000, 0.1429, 0.1667, 0.8000, 0, 1, 1, 0.5000, 0.0000 } },
        { "Midnight Village Ritual", { 2, 1, 0.4500, 0.4500, 0.3000, 0.6500, 0.9000, 0.1519, 0.7500, 0.7000, 0.7000, 0.4000, 0.8500, 0.7500, 0.7000, 0.2000, 0.7500, 0.4000, 0.3750, 0.4500, 0.1837, 0.2708, 0.8200, 0, 1, 2, 0.5000, 0.1000 } },
        { "Soft Yarn Nocturne", { 0, 0, 0.1500, 0.4800, 0.1500, 0.5000, 0.8800, 0.1709, 0.0000, 0.2500, 0.6500, 0.1500, 0.7000, 0.9000, 0.6000, 0.1000, 0.6000, 0.0800, 0.3500, 0.1500, 0.4898, 0.3333, 0.8500, 0, 0, 2, 0.5000, 0.0000 } },
    };
    auto map = [](const double* v) {
        // The wrapper's 0..1 -> physical mapping (CONTRACT.md), without CV / wheels
        auto f = [&](int i) { return static_cast<float>(v[i]); };
        EngineParameters p;
        p.model = static_cast<int>(v[0]);
        p.malletType = static_cast<int>(v[1]);
        p.malletHardness = f(2);
        p.strikePosition = f(3);
        p.strikeJitter = f(4) * 100.0f;
        p.resonatorTune = (f(5) - 0.5f) * 100.0f;
        p.resonatorCoupling = f(6);
        p.decay = 0.10f + f(7) * 7.90f;
        p.buzzAmount = f(8);
        p.artifacts = f(9);
        p.sympathetic = f(10);
        p.pitchGlide = f(11);
        p.bodyBloom = f(12);
        p.closeLevel = f(13);
        p.farLevel = f(14);
        p.piezoLevel = f(15);
        p.stereoWidth = f(16) * 2.0f;
        p.preampDrive = f(17);
        p.warmth = (f(18) - 0.5f) * 2.0f;
        p.compAmount = f(19);
        p.compAttack = 1.0f + f(20) * 49.0f;
        p.compRelease = 20.0f + f(21) * 480.0f;
        p.volume = f(22);
        p.oversampling = static_cast<int>(v[23]);
        p.velocityCurve = static_cast<int>(v[24]);
        p.polyphony = static_cast<int>(v[25]);
        p.tuneCents = (f(26) - 0.5f) * 200.0f;
        p.detune = f(27) * 100.0f;
        return p;
    };
    double worst = 0.0;
    const char* worstName = "";
    for (const auto& patch : kPatches) {
        const EngineParameters p = map(patch.v);
        for (const auto& chord : { std::array<int, 4>{ 48, 52, 55, 60 }, std::array<int, 4>{ 60, 64, 67, 72 } }) {
            MarembaEngine e(44100.0);
            e.SetParameters(p);
            for (int n : chord) e.NoteOn(n, 1.0f);
            Render r;
            RenderFrames(e, r, 44100 / 2, &p);
            for (int c = kCloseL; c <= kPiezo; ++c) {
                const double peak = Peak(r.ch[c]);
                if (peak > worst) { worst = peak; worstName = patch.name; }
                CHECK(peak <= 1.0, patch.name << " " << kChannelNames[c] << " direct out peaks at " << Db(peak) << " dBFS");
            }
        }
    }
    std::cout << "  -> hottest direct out " << Db(worst) << " dBFS (" << worstName << ")" << std::endl;
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

} // namespace

int main()
{
    std::cout << "=== Running Maremba DSP Verification Suite ===" << std::endl;

    TestNoteRangeStability();
    TestKeyboardPanning();
    TestStereoWidth();
    TestMirlitonBuzz();
    TestPreampCompressorContainment();
    TestPitchGlide();
    TestSympatheticHalo();
    TestBodyBloom();
    TestVelocityAndMixerSilence();
    TestDetune();
    TestStrikerHierarchy();
    TestKalimbaSustain();
    TestStrikeJitter();
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
    TestDirectOutHeadroomFactoryPatches();
    TestVolumeSmoothing();
    TestBendLeavesNothingBehind();
    TestNoSubnormalStates();
    TestWidthAndRoomImage();
    TestRoomBypassNoStaleTail();

    if (gFailures > 0) {
        std::cerr << "\n>>> " << gFailures << " MAREMBA DSP CHECK(S) FAILED <<<" << std::endl;
        return 1;
    }
    std::cout << "\n>>> ALL MAREMBA DSP VERIFICATION CHECKS PASSED SUCCESSFULLY! <<<" << std::endl;
    return 0;
}
