// Maremba engine fuzz contract: hostile input to maremba::MarembaEngine's public
// API, with invariants checked after every call, plus deterministic scenarios
// for the note-off / transport / oversampling / model-switch state machines.
//
// Build (from Examples/Maremba):
//   clang++ -std=c++17 -O2 -IDSP Tests/EngineFuzzTest.cpp DSP/*.cpp -o engine_fuzz
//   clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
//       -D_GLIBCXX_ASSERTIONS -IDSP Tests/EngineFuzzTest.cpp DSP/*.cpp -o engine_fuzz_asan
//   (g++ takes the same flags; on a box without clang's compiler-rt, use g++ for
//   ASan, or clang++ -fsanitize=undefined,float-cast-overflow -fsanitize-trap=all.)
// Run:
//   ./engine_fuzz                     the contract (Tests/contracts.txt): fixed seeds,
//                                     a few minutes at -O2
//   ./engine_fuzz --only fuzz --seeds 200 --first 1000 --steps 400
//   ./engine_fuzz --only fuzz|determinism|reset|denormal|scenarios [--verbose]
//   ./engine_fuzz --only determinism --determinism-seeds 50
//   ./engine_fuzz --only reset --seeds 12 --reset-rate 192000 --reset-os 2
//   ./engine_fuzz --only denormal --tail-rate 96000 --tail-os 1 --tail-seconds 5
// Exit status 0 = every invariant held; failures are listed (first 60).
//
// API contract exercised (MarembaEngine.h): any EngineParameters value (NaN,
// +-inf, FLT_MAX, subnormal, out of range; the engine clamps), any int note
// number (INT_MIN..INT_MAX) and float velocity (NaN, +-inf, <= 0, > 1), any
// pitch bend, Reset/DampAll/NoteOff at any time, engines at 22.05k-192k and
// 44100.5 Hz. RenderBatch: frameCount 1..kMaxFrames per the header; larger
// counts are clamped to kMaxFrames and <= 0 is a no-op (both exercised), the
// main outs are required (null main pointers make the call a no-op), any
// combination of direct outs may be null.
//
// Invariants (fuzz):
//   I1  every written sample is finite; RenderBatch writes exactly
//       min(frameCount, 64) frames of every non-null output and nothing else
//   I2  |main| <= 0.98 while the limiter is fully engaged (LIMIT > 0 for 3 ms);
//       volume is always <= 1 after sanitising (LIMIT 0 bypasses the guard by design)
//   I3  GetActiveVoiceCount() <= kVoicePoolSize
//   I4  the last-resort output guard (Reset inside RenderBatch) never fires
//   I5  after DampAll + NoteOff of every key the engine reports IsSilent()
//       within the drain limit (no stuck voices, no endless tail)
//   I6  right after Reset(): IsSilent() and no active voice
//   I7  a render after Reset() equals a fresh engine's render with the same
//       parameters and bend (the MarembaEngine.h contract)
//   I8  long decays: no subnormal state in voices or shared tails, and the
//       same render time with and without hardware flush-to-zero
// Determinism (the same MIDI and automation give the same audio, bit for bit):
//   D1  a random performance (notes, releases, parameter jumps to extremes,
//       bends, transport stops at exact sample positions) renders identically
//       in two fresh engines
//   D2  ... and identically when the host splits it into other block sizes
//       (1..64 frames, events at the same sample)
//   D3  ... and identically again after Reset() on the same engine
// Scenarios:
//   S1  every model x striker x note-off mode: strike, release -> silent
//   S2  held keys (dead stroke, resting thumb) through DampAll, then release -> silent
//   S3  a chord larger than the 32-entry queue during an oversampling fade:
//       the newest keys sound
//   S4  Kalimba: the wheel moved while idle applies at once at the next strike
//   S5  Model change bar -> Kalimba while a chord rings: the halo does not jump
//   S6  a fresh Rosewood strike at every offset against the control grid: the
//       rim's harmonics, scaled by the tube's swing at the last tick, stay
//       bounded while the tube rings up from rest
//   S7  MIDI 0, maximum downward tuning/bend, long decay and loose buzz:
//       transport damping releases the sub-audio cavity and every voice

#include "MarembaEngine.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static double Clock(const MarembaEngine& e) { return e.mClock; }
    static int PendingCount(const MarembaEngine& e) { return e.mPendingCount; }
    static bool NoteActive(const MarembaEngine& e, int note) {
        for (const auto& v : e.mVoices) if (v.IsActive() && v.GetNoteNumber() == note) return true;
        return false;
    }
    static bool VoicesNormal(const MarembaEngine& e) {
        for (const auto& v : e.mVoices)
            if (v.IsActive() && !v.HasNoSubnormalState()) return false;
        return true;
    }
    static bool SharedNormal(const MarembaEngine& e) {
        bool ok = e.mSympathetic.HasNoSubnormalState() && e.mFrameBody.HasNoSubnormalState()
               && e.mKalimbaBody.HasNoSubnormalState() && e.mPreamp.HasNoSubnormalState()
               && e.mLimiter.HasNoSubnormalState() && e.mOversampler.HasNoSubnormalState();
        for (const auto& d : e.mDirectDecimators) ok = ok && d.HasNoSubnormalState();
        return ok;
    }
    // One line per active voice: note, model, flags (stuck-voice diagnostics).
    static std::string DescribeVoices(const MarembaEngine& e) {
        std::string s;
        char line[160];
        for (int i = 0; i < MarembaEngine::kVoicePoolSize; ++i) {
            const auto& v = e.mVoices[static_cast<std::size_t>(i)];
            if (!v.IsActive()) continue;
            std::snprintf(line, sizeof(line), " [slot %d note %d model %d %s%s%s%s energy %.3g]", i, v.GetNoteNumber(),
                          static_cast<int>(v.GetModel()), v.IsReleased() ? "released " : "held ",
                          v.IsFading() ? "fading " : "", v.IsStriking() ? "striking " : "",
                          "", v.GetCurrentEnergy());
            s += line;
        }
        return s;
    }
};
} // namespace maremba

namespace {

using maremba::EngineParameters;
using maremba::MarembaEngine;
using Access = maremba::MarembaEngineTestAccess;

constexpr int kOuts = 7;       // main L/R, close L/R, far L/R, piezo
constexpr int kBufFrames = 256; // room for out-of-contract frame counts
constexpr float kSentinel = 12345.678f;
constexpr float kCeiling = 0.98f;
const double kRates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 44100.5 };

struct Options {
    int seeds = 24;
    int determinismSeeds = 8;     // each renders a 3 s performance four times
    uint64_t first = 1;
    int steps = 160;
    double drainSeconds = 30.0;   // a sub-audio key (MIDI 0) under DampAll takes up to ~20 s
    bool verbose = false;
    std::string only;
    double tailRate = 44100.0;
    int tailOversampling = 0;
    double tailSeconds = 12.0;
    double resetRate = 0.0;       // 0 retains the inexpensive default rate selection
    int resetOversampling = -1;  // -1 retains the default 2x/4x selection
};

// splitmix64; the seed is hashed first so consecutive seeds give unrelated
// streams (not the same stream shifted by one draw).
struct Rng {
    uint64_t s;
    static uint64_t Mix(uint64_t z) {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    explicit Rng(uint64_t seed) : s(Mix(Mix(seed + 0x632BE59BD9B4E019ull))) {}
    uint64_t Next() { return Mix(s += 0x9E3779B97F4A7C15ull); }
    int Int(int lo, int hi) { return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1)); }
    double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
    float Range(float lo, float hi) { return lo + static_cast<float>(Unit()) * (hi - lo); }
    bool Chance(double p) { return Unit() < p; }
};

// ---------------------------------------------------------------- failures --
struct Report {
    int failures = 0;
    std::vector<std::string> lines;
    void Fail(const std::string& what) {
        ++failures;
        if (lines.size() < 60) lines.push_back(what);
    }
};
Report gReport;

std::string Fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

// ------------------------------------------------------------ hostile input --
float HostileFloat(Rng& r, float lo, float hi) {
    switch (r.Int(0, 15)) {
        case 0: return std::numeric_limits<float>::quiet_NaN();
        case 1: return std::numeric_limits<float>::infinity();
        case 2: return -std::numeric_limits<float>::infinity();
        case 3: return std::numeric_limits<float>::max();
        case 4: return -std::numeric_limits<float>::max();
        case 5: return 1.0e-40f;                       // subnormal
        case 6: return -0.0f;
        case 7: return lo - 1.0f - std::fabs(lo);      // just below
        case 8: return hi + 1.0f + std::fabs(hi);      // just above
        case 9: return lo;
        case 10: return hi;
        case 11: return -1.0e9f;
        case 12: return 1.0e9f;
        default: return r.Range(lo - 0.25f * (hi - lo), hi + 0.25f * (hi - lo));
    }
}

int HostileInt(Rng& r, int lo, int hi) {
    switch (r.Int(0, 9)) {
        case 0: return INT_MIN;
        case 1: return INT_MAX;
        case 2: return -1;
        case 3: return hi + 1;
        case 4: return -1000000;
        default: return r.Int(lo, hi);
    }
}

// A playable patch: every field inside its range.
EngineParameters PlausibleParams(Rng& r) {
    EngineParameters p;
    p.model = r.Int(0, 3);
    p.malletType = r.Int(0, maremba::kStrikerCount - 1);
    p.malletHardness = r.Range(0, 1);
    p.strikePosition = r.Range(0, 1);
    p.resonatorTune = r.Range(-50, 50);
    p.resonatorCoupling = r.Range(0, 1);
    p.decay = r.Chance(0.3) ? r.Range(0.1f, 1.0f) : r.Range(0.1f, 8.0f);
    p.buzzAmount = r.Range(0, 1);
    p.artifacts = r.Range(0, 1);
    p.sympathetic = r.Range(0, 1);
    p.pitchGlide = r.Range(0, 1);
    p.bodyBloom = r.Range(0, 1);
    p.closeLevel = r.Range(0, 1);
    p.farLevel = r.Range(0, 1);
    p.roomType = r.Int(0, 2);
    p.piezoLevel = r.Range(0, 1);
    p.stereoWidth = r.Range(0, 2);
    p.preampDrive = r.Range(0, 1);
    p.warmth = r.Range(-1, 1);
    p.compAmount = r.Chance(0.2) ? 0.0f : r.Range(0, 1);
    p.compAttack = r.Range(1, 50);
    p.compRelease = r.Range(20, 500);
    p.volume = r.Range(0, 1);
    p.oversampling = r.Int(0, 2);
    p.velocityCurve = r.Int(0, 3);
    p.polyphony = r.Int(0, 2);
    p.tuneCents = r.Range(-200, 200);
    p.detune = r.Range(0, 100);
    p.strikeJitter = r.Range(0, 100);
    p.velocityToHardness = r.Range(0, 1);
    p.velocityToPosition = r.Range(0, 1);
    p.velocityToDecay = r.Range(0, 1);
    p.tuningScale = r.Int(0, 6);
    p.noteOffMode = r.Int(0, 2);
    p.modWheel = r.Chance(0.6) ? 0.0f : r.Range(0, 1);
    p.carving = r.Range(0, 1);
    p.membraneTension = r.Range(0, 1);
    p.velocityToCarving = r.Range(0, 1);
    p.velocityToCoupling = r.Range(0, 1);
    p.velocityToBloom = r.Range(0, 1);
    p.velocityToSympathetic = r.Range(0, 1);
    p.velocityToBuzz = r.Range(0, 1);
    p.velocityToTension = r.Range(0, 1);
    p.velocityToArtifacts = r.Range(0, 1);
    return p;
}

// Every field at an end of its range, in random combinations (a knob turned
// all the way down or up is the commonest extreme a user reaches), with a few
// left at random.
EngineParameters EdgeParams(Rng& r) {
    EngineParameters p = PlausibleParams(r);
    const auto edge = [&r](float lo, float hi, float value) {
        const int pick = r.Int(0, 9);
        return pick < 4 ? lo : pick < 8 ? hi : value;
    };
    struct F { float* v; float lo, hi; };
    const F floats[] = {
        {&p.malletHardness, 0, 1}, {&p.strikePosition, 0, 1}, {&p.resonatorTune, -50, 50},
        {&p.resonatorCoupling, 0, 1}, {&p.decay, 0.1f, 8}, {&p.buzzAmount, 0, 1}, {&p.artifacts, 0, 1},
        {&p.sympathetic, 0, 1}, {&p.bodyBloom, 0, 1}, {&p.closeLevel, 0, 1},
        {&p.farLevel, 0, 1}, {&p.piezoLevel, 0, 1}, {&p.stereoWidth, 0, 2}, {&p.preampDrive, 0, 1},
        {&p.warmth, -1, 1}, {&p.compAmount, 0, 1}, {&p.compAttack, 1, 50}, {&p.compRelease, 20, 500},
        {&p.volume, 0, 1}, {&p.tuneCents, -200, 200}, {&p.detune, 0, 100}, {&p.strikeJitter, 0, 100},
        {&p.velocityToHardness, 0, 1}, {&p.velocityToPosition, 0, 1}, {&p.velocityToDecay, 0, 1},
        {&p.velocityToCarving, 0, 1}, {&p.velocityToCoupling, 0, 1}, {&p.velocityToBloom, 0, 1},
        {&p.velocityToSympathetic, 0, 1}, {&p.velocityToBuzz, 0, 1}, {&p.velocityToTension, 0, 1},
        {&p.velocityToArtifacts, 0, 1}, {&p.modWheel, 0, 1}, {&p.carving, 0, 1}, {&p.membraneTension, 0, 1},
    };
    for (const F& f : floats) *f.v = edge(f.lo, f.hi, *f.v);
    struct I { int* v; int lo, hi; };
    const I ints[] = {
        {&p.model, 0, 3}, {&p.malletType, 0, maremba::kStrikerCount - 1}, {&p.roomType, 0, 2},
        {&p.oversampling, 0, 2}, {&p.velocityCurve, 0, 3}, {&p.polyphony, 0, 2}, {&p.tuningScale, 0, 6},
        {&p.noteOffMode, 0, 2},
    };
    for (const I& i : ints) {
        const int pick = r.Int(0, 9);
        *i.v = pick < 4 ? i.lo : pick < 8 ? i.hi : r.Int(i.lo, i.hi);
    }
    return p;
}

// Poison some fields of p with hostile values (every field is a candidate).
void Poison(Rng& r, EngineParameters& p, double chance) {
    struct F { float* v; float lo, hi; };
    const F floats[] = {
        {&p.malletHardness, 0, 1}, {&p.strikePosition, 0, 1}, {&p.resonatorTune, -50, 50},
        {&p.resonatorCoupling, 0, 1}, {&p.decay, 0.1f, 8}, {&p.buzzAmount, 0, 1}, {&p.artifacts, 0, 1},
        {&p.sympathetic, 0, 1}, {&p.pitchGlide, 0, 1}, {&p.bodyBloom, 0, 1}, {&p.closeLevel, 0, 1},
        {&p.farLevel, 0, 1}, {&p.piezoLevel, 0, 1}, {&p.stereoWidth, 0, 2}, {&p.preampDrive, 0, 1},
        {&p.warmth, -1, 1}, {&p.compAmount, 0, 1}, {&p.compAttack, 1, 50}, {&p.compRelease, 20, 500},
        {&p.volume, 0, 1}, {&p.tuneCents, -200, 200}, {&p.detune, 0, 100}, {&p.strikeJitter, 0, 100},
        {&p.velocityToHardness, 0, 1}, {&p.velocityToPosition, 0, 1}, {&p.velocityToDecay, 0, 1},
        {&p.velocityToCarving, 0, 1}, {&p.velocityToCoupling, 0, 1}, {&p.velocityToBloom, 0, 1},
        {&p.velocityToSympathetic, 0, 1}, {&p.velocityToBuzz, 0, 1}, {&p.velocityToTension, 0, 1},
        {&p.velocityToArtifacts, 0, 1},
        {&p.modWheel, 0, 1}, {&p.carving, 0, 1}, {&p.membraneTension, 0, 1},
    };
    for (const F& f : floats) if (r.Chance(chance)) *f.v = HostileFloat(r, f.lo, f.hi);
    struct I { int* v; int lo, hi; };
    const I ints[] = {
        {&p.model, 0, 3}, {&p.malletType, 0, maremba::kStrikerCount - 1}, {&p.roomType, 0, 2}, {&p.oversampling, 0, 2},
        {&p.velocityCurve, 0, 3}, {&p.polyphony, 0, 2}, {&p.tuningScale, 0, 6}, {&p.noteOffMode, 0, 2},
    };
    for (const I& i : ints) if (r.Chance(chance)) *i.v = HostileInt(r, i.lo, i.hi);
}

int HostileNote(Rng& r) {
    switch (r.Int(0, 19)) {
        case 0: return INT_MIN;
        case 1: return INT_MAX;
        case 2: return -1;
        case 3: return 128;
        case 4: return r.Int(-100000, 100000);
        case 5: return 0;
        case 6: return 127;
        default: return r.Int(21, 108);
    }
}

float HostileVelocity(Rng& r) {
    switch (r.Int(0, 15)) {
        case 0: return std::numeric_limits<float>::quiet_NaN();
        case 1: return std::numeric_limits<float>::infinity();
        case 2: return -std::numeric_limits<float>::infinity();
        case 3: return -0.5f;
        case 4: return 0.0f;
        case 5: return 1.0e-40f;
        case 6: return 2.0f;
        case 7: return 1.0e30f;
        case 8: return 1.0f / 127.0f;
        default: return r.Range(0.05f, 1.0f);
    }
}

float HostileBend(Rng& r) {
    switch (r.Int(0, 9)) {
        case 0: return std::numeric_limits<float>::quiet_NaN();
        case 1: return std::numeric_limits<float>::infinity();
        case 2: return -std::numeric_limits<float>::infinity();
        case 3: return 1.0e30f;
        case 4: return -1.0e30f;
        case 5: return 1200.0f;
        case 6: return -1200.0f;
        default: return r.Range(-200, 200);
    }
}

// -------------------------------------------------------------- rendering --
struct Buffers {
    float ch[kOuts][kBufFrames];
    void Fill() { for (auto& c : ch) std::fill(std::begin(c), std::end(c), kSentinel); }
};

struct Tracker {
    double rate = 44100.0;
    long framesLimiterEngaged = 0;   // frames since LIMIT went above 0 (sanitised)
    double peakMain = 0.0;
    double worstBatchNs = 0.0;
    std::string where;               // seed / step label for failure messages
};

// Render one call and check I1-I4. mask: bit k set = direct out k connected.
// Returns frames actually rendered.
int RenderChecked(MarembaEngine& e, Tracker& t, int frameCount, unsigned mask, bool nullMain = false) {
    static Buffers b;
    b.Fill();
    float* direct[5];
    for (int k = 0; k < 5; ++k) direct[k] = (mask >> k) & 1u ? b.ch[2 + k] : nullptr;
    const double clockBefore = Access::Clock(e);
    const bool silentBefore = e.IsSilent();
    const auto t0 = std::chrono::steady_clock::now();
    e.RenderBatch(nullMain ? nullptr : b.ch[0], nullMain ? nullptr : b.ch[1],
                  direct[0], direct[1], direct[2], direct[3], direct[4], frameCount);
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    t.worstBatchNs = std::max(t.worstBatchNs, ns);

    const int expected = (nullMain || frameCount <= 0) ? 0 : std::min(frameCount, MarembaEngine::kMaxFrames);
    // I1: exact frame count, finite samples
    for (int c = 0; c < kOuts; ++c) {
        const bool connected = c < 2 ? !nullMain : ((mask >> (c - 2)) & 1u) != 0;
        for (int f = 0; f < kBufFrames; ++f) {
            const float v = b.ch[c][f];
            const bool shouldWrite = connected && f < expected;
            if (!shouldWrite && v != kSentinel) {
                gReport.Fail(Fmt("%s: RenderBatch(frameCount=%d) wrote output %d frame %d outside its contract",
                                 t.where.c_str(), frameCount, c, f));
                return expected;
            }
            if (shouldWrite && (v == kSentinel || !std::isfinite(v))) {
                gReport.Fail(Fmt("%s: output %d frame %d %s (frameCount %d)", t.where.c_str(), c, f,
                                 v == kSentinel ? "left unwritten" : "is not finite", frameCount));
                return expected;
            }
        }
    }
    // I2: the peak guard
    const EngineParameters& p = e.GetParameters();
    if (p.compAmount > 0.0f) t.framesLimiterEngaged += expected; else t.framesLimiterEngaged = 0;
    for (int f = 0; f < expected; ++f) {
        const double m = std::max(std::fabs(b.ch[0][f]), std::fabs(b.ch[1][f]));
        t.peakMain = std::max(t.peakMain, m);
        if (t.framesLimiterEngaged > static_cast<long>(0.003 * t.rate) + 64 && m > kCeiling * 1.000001) {
            gReport.Fail(Fmt("%s: |main| %.6f above the 0.98 guard with LIMIT %.3f volume %.3f",
                             t.where.c_str(), m, p.compAmount, p.volume));
            break;
        }
    }
    // I3
    if (e.GetActiveVoiceCount() > MarembaEngine::kVoicePoolSize)
        gReport.Fail(Fmt("%s: %d active voices > pool %d", t.where.c_str(), e.GetActiveVoiceCount(), MarembaEngine::kVoicePoolSize));
    // I4: Reset() inside RenderBatch rewinds the clock
    if (expected > 0 && !silentBefore && Access::Clock(e) < clockBefore)
        gReport.Fail(Fmt("%s: the output guard fired (engine reset inside RenderBatch)", t.where.c_str()));
    return expected;
}

// Render `seconds` in 64-frame batches, all outs connected.
void RenderFor(MarembaEngine& e, Tracker& t, double seconds, unsigned mask = 0x1Fu) {
    const long frames = static_cast<long>(seconds * t.rate);
    for (long done = 0; done < frames; done += 64) RenderChecked(e, t, 64, mask);
}

// I5: release everything and wait for IsSilent(). Returns seconds taken (or -1).
double DrainUntilSilent(MarembaEngine& e, Tracker& t, double limitSeconds) {
    e.DampAll();
    for (int n = 0; n < 128; ++n) e.NoteOff(n);
    const long limit = static_cast<long>(limitSeconds * t.rate);
    long frames = 0;
    while (!e.IsSilent() && frames < limit) {
        RenderChecked(e, t, 64, 0x1Fu);
        frames += 64;
    }
    return e.IsSilent() ? frames / t.rate : -1.0;
}

void CheckResetSilent(MarembaEngine& e, const Tracker& t) {
    e.Reset();
    if (!e.IsSilent() || e.GetActiveVoiceCount() != 0)
        gReport.Fail(Fmt("%s: after Reset() IsSilent()=%d active=%d", t.where.c_str(), e.IsSilent(), e.GetActiveVoiceCount()));
}

// ------------------------------------------------------------------- fuzz --
std::unique_ptr<MarembaEngine> MakeUnique(double rate) { return std::make_unique<MarembaEngine>(rate); }

void FuzzSeed(uint64_t seed, const Options& o) {
    Rng r(seed);
    Tracker t;
    t.rate = kRates[r.Int(0, static_cast<int>(sizeof(kRates) / sizeof(kRates[0])) - 1)];
    auto engine = std::make_unique<MarembaEngine>(t.rate);
    MarembaEngine& e = *engine;
    EngineParameters p = r.Chance(0.5) ? EdgeParams(r) : PlausibleParams(r);
    e.SetParameters(p);
    for (int step = 0; step < o.steps; ++step) {
        t.where = Fmt("fuzz seed %llu rate %.1f step %d", static_cast<unsigned long long>(seed), t.rate, step);
        const int op = r.Int(0, 99);
        if (op < 14) {                      // new patch: extremes or plausible, possibly poisoned
            p = r.Chance(0.5) ? EdgeParams(r) : PlausibleParams(r);
            if (r.Chance(0.5)) Poison(r, p, 0.3);
            e.SetParameters(p);
        } else if (op < 24) {               // one live change mid-note (switch storms)
            switch (r.Int(0, 7)) {
                case 0: p.model = HostileInt(r, 0, 3); break;
                case 1: p.oversampling = HostileInt(r, 0, 2); break;
                case 2: p.polyphony = HostileInt(r, 0, 2); break;
                case 3: p.tuningScale = HostileInt(r, 0, 6); break;
                case 4: p.noteOffMode = HostileInt(r, 0, 2); break;
                case 5: p.malletType = HostileInt(r, 0, maremba::kStrikerCount - 1); break;
                case 6: p.roomType = HostileInt(r, 0, 2); break;
                default: p.modWheel = HostileFloat(r, 0, 1); break;
            }
            e.SetParameters(p);
        } else if (op < 44) {               // notes
            const int count = r.Chance(0.15) ? r.Int(20, 70) : r.Int(1, 3);
            for (int i = 0; i < count; ++i) {
                // Two statements: argument evaluation order differs between
                // compilers, and the stream must not.
                const int note = HostileNote(r);
                const float velocity = HostileVelocity(r);
                e.NoteOn(note, velocity);
            }
        } else if (op < 52) {
            e.NoteOff(r.Chance(0.5) ? HostileNote(r) : r.Int(0, 127));
        } else if (op < 55) {
            e.DampAll();
        } else if (op < 57) {
            CheckResetSilent(e, t);
        } else if (op < 62) {
            e.SetPitchBendCents(HostileBend(r));
        } else {                            // render
            const int kind = r.Int(0, 19);
            static const int kOutOfContract[] = {0, -1, 65, 200, INT_MIN};
            const int frames = kind < 6 ? 64 : kind < 9 ? 1 : kind < 12 ? 63 : kind < 18 ? r.Int(1, 64)
                             : kOutOfContract[r.Int(0, 4)];
            const unsigned mask = static_cast<unsigned>(r.Int(0, 31));
            const bool nullMain = r.Chance(0.03);
            const int batches = r.Int(1, 40);
            for (int b = 0; b < batches; ++b) RenderChecked(e, t, frames, mask, nullMain);
        }
        if (Access::PendingCount(e) > 32) gReport.Fail(Fmt("%s: pending queue overflow", t.where.c_str()));
    }
    t.where = Fmt("fuzz seed %llu rate %.1f drain", static_cast<unsigned long long>(seed), t.rate);
    const double drained = DrainUntilSilent(e, t, o.drainSeconds);
    if (drained < 0.0)
        gReport.Fail(Fmt("%s: not silent %.0f s after DampAll + NoteOffs (%d active; model %d mallet %d noteOff %d wheel %.2f):%s",
                         t.where.c_str(), o.drainSeconds, e.GetActiveVoiceCount(), e.GetParameters().model,
                         e.GetParameters().malletType, e.GetParameters().noteOffMode, e.GetParameters().modWheel,
                         Access::DescribeVoices(e).c_str()));
    CheckResetSilent(e, t);
    if (o.verbose)
        std::printf("  seed %llu rate %.1f: drained in %.2f s, peak main %.3f, worst batch %.0f us\n",
                    static_cast<unsigned long long>(seed), t.rate, drained, t.peakMain, t.worstBatchNs / 1000.0);
}

// ------------------------------------------------------ reset equivalence --
struct Capture { std::vector<float> ch[kOuts]; };

// The same scripted performance on an engine, recorded.
Capture Perform(MarembaEngine& e, double rate, const std::vector<std::pair<int, float>>& notes, double seconds) {
    Capture c;
    static Buffers b;
    const long frames = static_cast<long>(seconds * rate);
    size_t next = 0;
    for (long done = 0; done < frames; done += 64) {
        // a note every ~40 ms
        while (next < notes.size() && static_cast<long>(next) * static_cast<long>(0.04 * rate) <= done) {
            e.NoteOn(notes[next].first, notes[next].second);
            ++next;
        }
        b.Fill();
        e.RenderBatch(b.ch[0], b.ch[1], b.ch[2], b.ch[3], b.ch[4], b.ch[5], b.ch[6], 64);
        for (int k = 0; k < kOuts; ++k) c.ch[k].insert(c.ch[k].end(), b.ch[k], b.ch[k] + 64);
    }
    return c;
}

void ResetEquivalenceSeed(uint64_t seed, const Options& o) {
    Rng r(seed ^ 0xA5A5A5A5ull);
    const double rate = o.resetRate > 0.0 ? o.resetRate : kRates[r.Int(0, 3)];
    EngineParameters p = PlausibleParams(r);
    p.oversampling = o.resetOversampling >= 0 ? o.resetOversampling : r.Int(0, 1);
    const float bend = r.Chance(0.5) ? 0.0f : r.Range(-200, 200);
    std::vector<std::pair<int, float>> notes;
    const int base = r.Int(40, 80);
    for (int i = 0; i < 6; ++i) notes.push_back({base + r.Int(-5, 5), r.Range(0.2f, 1.0f)});

    auto used = std::make_unique<MarembaEngine>(rate);
    {
        // History: the same keys with other patches, bends and wheel moves,
        // or (odd seeds) the very same patch with the PRNGs advanced further.
        const bool samePatch = (seed & 1u) != 0;
        EngineParameters h = samePatch ? p : PlausibleParams(r);
        h.model = p.model;
        h.oversampling = p.oversampling;
        if (samePatch) { h.buzzAmount = 1.0f; used->SetPitchBendCents(bend); }
        used->SetParameters(h);
        if (!samePatch) used->SetPitchBendCents(r.Range(-100, 100));
        const int historyNotes = samePatch ? r.Int(7, 17) : 12;
        for (int i = 0; i < historyNotes; ++i) {
            used->NoteOn(notes[static_cast<size_t>(i) % notes.size()].first, r.Range(0.3f, 1.0f));
            if (!samePatch && r.Chance(0.3)) { h.modWheel = r.Range(0, 1); used->SetParameters(h); }
            static Buffers b;
            for (int k = 0; k < 20; ++k)
                used->RenderBatch(b.ch[0], b.ch[1], b.ch[2], b.ch[3], b.ch[4], b.ch[5], b.ch[6], 64);
        }
        h.modWheel = p.modWheel;
        used->SetParameters(h);
    }
    const char* history = (seed & 1u) ? "same patch" : "other patches";
    used->Reset();
    used->SetParameters(p);
    used->SetPitchBendCents(bend);
    const Capture a = Perform(*used, rate, notes, 0.35);

    auto fresh = std::make_unique<MarembaEngine>(rate);
    fresh->SetParameters(p);
    fresh->SetPitchBendCents(bend);
    const Capture b = Perform(*fresh, rate, notes, 0.35);

    double worst = 0.0;
    int worstCh = -1;
    long firstDiff = -1;
    for (int k = 0; k < kOuts; ++k)
        for (size_t i = 0; i < a.ch[k].size(); ++i) {
            const double d = std::fabs(static_cast<double>(a.ch[k][i]) - b.ch[k][i]);
            if (d > 0.0 && (firstDiff < 0 || static_cast<long>(i) < firstDiff)) firstDiff = static_cast<long>(i);
            if (d > worst) { worst = d; worstCh = k; }
        }
    if (worst > 0.0)
        gReport.Fail(Fmt("reset seed %llu rate %.1f model %d mallet %d wheel %.2f history %s: render after Reset() differs "
                         "from a fresh engine (max |diff| %.3g on output %d, first at frame %ld)",
                         static_cast<unsigned long long>(seed), rate, p.model, p.malletType, p.modWheel, history,
                         worst, worstCh, firstDiff));
    else if (o.verbose)
        std::printf("  reset seed %llu rate %.1f quality %dx model %d: identical\n",
                    static_cast<unsigned long long>(seed), rate, 2 << p.oversampling, p.model);
}

// ------------------------------------------------ long tails / denormals --
// x86 MXCSR flush-to-zero / denormals-are-zero, to compare the tail's cost
// with and without subnormal arithmetic (a host may or may not set them).
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
struct DenormalMode {
    unsigned saved;
    explicit DenormalMode(bool flush) : saved(_mm_getcsr()) {
        _mm_setcsr(flush ? (saved | 0x8040u) : (saved & ~0x8040u));
    }
    ~DenormalMode() { _mm_setcsr(saved); }
};
#else
struct DenormalMode { explicit DenormalMode(bool) {} };
#endif

// A dense, long-decaying chord (decay 8, every shared tail on, buzz, shaft
// knock) rendered for `seconds`: no subnormal state anywhere, and the same
// cost with and without hardware flush-to-zero. Returns the render seconds.
double RenderTail(int model, double rate, int oversampling, double seconds, bool flush,
                  int* badVoice, int* badShared, double* silentAt) {
    DenormalMode mode(flush);
    Tracker t;
    t.rate = rate;
    t.where = Fmt("tail model %d rate %.0f os %d", model, rate, oversampling);
    auto engine = std::make_unique<MarembaEngine>(rate);
    MarembaEngine& e = *engine;
    EngineParameters p;
    p.model = model;
    p.oversampling = oversampling;
    p.decay = 8.0f;
    p.sympathetic = 1.0f;
    p.bodyBloom = 1.0f;
    p.farLevel = 1.0f;
    p.roomType = 2;
    p.buzzAmount = 1.0f;
    p.artifacts = 1.0f;
    p.polyphony = 2;
    p.malletType = 4;   // shaft: its own knock modes decay while the bar rings
    e.SetParameters(p);
    for (int n : {36, 43, 48, 55, 60, 67, 72, 84}) e.NoteOn(model == 3 ? n + 24 : n, 0.9f);
    const long batches = static_cast<long>(seconds * rate / 64);
    *badVoice = *badShared = 0;
    *silentAt = -1.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long b = 0; b < batches; ++b) {
        RenderChecked(e, t, 64, 0x1Fu);
        if (b % 8 == 0) {
            if (!Access::VoicesNormal(e)) ++*badVoice;
            if (!Access::SharedNormal(e)) ++*badShared;
        }
        if (e.IsSilent()) { *silentAt = b * 64.0 / rate; break; }
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void DenormalTail(int model, double rate, int oversampling, double seconds, const Options& o) {
    int badVoice = 0, badShared = 0, v2 = 0, s2 = 0;
    double silent = 0.0, silent2 = 0.0;
    const double plain = RenderTail(model, rate, oversampling, seconds, false, &badVoice, &badShared, &silent);
    const double flushed = RenderTail(model, rate, oversampling, seconds, true, &v2, &s2, &silent2);
    const std::string where = Fmt("tail model %d rate %.0f os %d", model, rate, oversampling);
    if (badVoice || badShared)
        gReport.Fail(Fmt("%s: subnormal state on %d voice / %d shared checks", where.c_str(), badVoice, badShared));
    if (plain > 1.5 * flushed + 0.2)
        gReport.Fail(Fmt("%s: %.2f s without flush-to-zero vs %.2f s with it (subnormal arithmetic)", where.c_str(), plain, flushed));
    if (o.verbose)
        std::printf("  %s: %.1f s of tail in %.2f s (FTZ/DAZ: %.2f s), silent at %.1f s\n", where.c_str(), seconds,
                    plain, flushed, silent);
}

// Every model's default patch: a strike must end on its own (no stuck voice)
// well within the longest T60 (60 s at decay 8; 20 s at decay 1).
void VoiceEnds(int model, const Options& o) {
    Tracker t;
    t.rate = 44100.0;
    t.where = Fmt("decay model %d", model);
    auto e = std::make_unique<MarembaEngine>(t.rate);
    EngineParameters p;
    p.model = model;
    e->SetParameters(p);
    for (int n : {36, 60, 96}) e->NoteOn(model == 3 ? std::max(n, 60) : n, 1.0f);
    for (int n : {36, 60, 96}) e->NoteOff(model == 3 ? std::max(n, 60) : n);
    long frames = 0;
    const long limit = static_cast<long>(40.0 * t.rate);
    while (!e->IsSilent() && frames < limit) { RenderChecked(*e, t, 64, 0x1Fu); frames += 64; }
    if (!e->IsSilent())
        gReport.Fail(Fmt("%s: default patch still sounding after 40 s:%s", t.where.c_str(), Access::DescribeVoices(*e).c_str()));
    else if (o.verbose)
        std::printf("  %s: silent after %.1f s\n", t.where.c_str(), frames / t.rate);
}

// ------------------------------------------------------------ determinism --
struct Event {
    long frame;
    int kind;                 // 0 note on, 1 note off, 2 parameters, 3 bend, 4 DampAll
    int note = 0;
    float value = 0.0f;       // velocity or bend cents
    EngineParameters params{};
};

struct Performance {
    double rate = 48000.0;
    EngineParameters initial{};
    std::vector<Event> events;   // in frame order
    long frames = 0;
};

Performance MakePerformance(uint64_t seed) {
    Rng r(seed * 7919u + 17u);
    Performance perf;
    perf.rate = kRates[r.Int(0, 6)];   // the in-range rates
    perf.initial = r.Chance(0.5) ? EdgeParams(r) : PlausibleParams(r);
    perf.frames = static_cast<long>(3.0 * perf.rate);
    long frame = 0;
    while (true) {
        frame += r.Int(0, static_cast<int>(0.12 * perf.rate));
        if (frame >= perf.frames - static_cast<long>(0.5 * perf.rate)) break;
        Event e{frame, 0};
        const int op = r.Int(0, 99);
        if (op < 45) {
            e.kind = 0;
            e.note = r.Int(24, 108);
            e.value = r.Chance(0.2) ? (r.Chance(0.5) ? 1.0f : 1.0f / 127.0f) : r.Range(0.05f, 1.0f);
        } else if (op < 75) {
            e.kind = 1;
            e.note = r.Int(24, 108);
        } else if (op < 90) {
            e.kind = 2;
            e.params = r.Chance(0.5) ? EdgeParams(r) : PlausibleParams(r);
            e.params.oversampling = perf.initial.oversampling;   // a quality switch is a fade, tested elsewhere
        } else if (op < 97) {
            e.kind = 3;
            e.value = r.Range(-200, 200);
        } else {
            e.kind = 4;
        }
        perf.events.push_back(e);
        // Two events can share a sample: a chord, or a release and a strike.
        if (r.Chance(0.2)) { Event same = e; same.kind = 0; same.note = r.Int(24, 108); same.value = r.Range(0.1f, 1.0f); perf.events.push_back(same); }
    }
    return perf;
}

// Renders the performance in blocks from `blockSize` (0: random 1..64 drawn
// from `split`), cutting each block at the next event as a host does.
std::vector<float> Play(MarembaEngine& e, const Performance& perf, int blockSize, uint64_t split) {
    Rng r(split);
    e.SetParameters(perf.initial);
    e.SetPitchBendCents(0.0f);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(perf.frames) * kOuts);
    static Buffers b;
    size_t next = 0;
    long frame = 0;
    while (frame < perf.frames) {
        while (next < perf.events.size() && perf.events[next].frame == frame) {
            const Event& ev = perf.events[next++];
            switch (ev.kind) {
                case 0: e.NoteOn(ev.note, ev.value); break;
                case 1: e.NoteOff(ev.note); break;
                case 2: e.SetParameters(ev.params); break;
                case 3: e.SetPitchBendCents(ev.value); break;
                default: e.DampAll(); break;
            }
        }
        long block = blockSize > 0 ? blockSize : r.Int(1, 64);
        if (next < perf.events.size()) block = std::min(block, perf.events[next].frame - frame);
        block = std::min(block, perf.frames - frame);
        const int n = static_cast<int>(block);
        if (e.IsSilent()) {
            // A host may skip a silent engine; its outputs read as zero.
            out.insert(out.end(), static_cast<size_t>(n) * kOuts, 0.0f);
        } else {
            e.RenderBatch(b.ch[0], b.ch[1], b.ch[2], b.ch[3], b.ch[4], b.ch[5], b.ch[6], n);
            for (int f = 0; f < n; ++f)
                for (int c = 0; c < kOuts; ++c) out.push_back(b.ch[c][f]);
        }
        frame += block;
    }
    return out;
}

void DeterminismSeed(uint64_t seed, const Options& o) {
    const Performance perf = MakePerformance(seed);
    auto a = std::make_unique<MarembaEngine>(perf.rate);
    auto b = std::make_unique<MarembaEngine>(perf.rate);
    const auto first = Play(*a, perf, 64, 0);
    const auto second = Play(*b, perf, 64, 0);
    const auto split = Play(*MakeUnique(perf.rate), perf, 0, seed);
    a->Reset();
    const auto replay = Play(*a, perf, 64, 0);
    const auto where = Fmt("determinism seed %llu rate %.1f (%zu events)", static_cast<unsigned long long>(seed),
                           perf.rate, perf.events.size());
    const auto firstDifference = [&](const std::vector<float>& x) -> long {
        for (size_t i = 0; i < first.size(); ++i)
            if (std::memcmp(&first[i], &x[i], sizeof(float)) != 0) return static_cast<long>(i / kOuts);
        return -1;
    };
    bool finite = true;
    for (float v : first) finite = finite && std::isfinite(v);
    if (!finite) gReport.Fail(where + ": non-finite output");
    if (const long f = firstDifference(second); f >= 0)
        gReport.Fail(Fmt("%s: two fresh engines differ from frame %ld", where.c_str(), f));
    if (const long f = firstDifference(split); f >= 0)
        gReport.Fail(Fmt("%s: other block sizes differ from frame %ld", where.c_str(), f));
    if (const long f = firstDifference(replay); f >= 0)
        gReport.Fail(Fmt("%s: the replay after Reset() differs from frame %ld", where.c_str(), f));
    if (o.verbose) std::printf("  %s: identical\n", where.c_str());
}

// --------------------------------------------------------------- scenarios --
// Deterministic regressions for the state machines.
void FreshStrikeOnTheControlGrid();

void MinimumNoteStops(const Options& o) {
    // A constant-Q hand loss used to approach zero below hearing: MIDI 0
    // bent to about 3.6 Hz kept the air/membranes alive past the 30 s drain
    // bound even after the bar was silent. Cover a head and a shaft, every
    // model, the default rate/quality and 96 kHz at the highest quality.
    for (double rate : {48000.0, 96000.0})
        for (int model = 0; model < 4; ++model)
            for (int striker : {1, 4}) {
                auto e = std::make_unique<MarembaEngine>(rate);
                EngineParameters p;
                p.model = model; p.malletType = striker;
                p.oversampling = rate == 48000.0 ? 0 : 2;
                p.decay = 8; p.buzzAmount = 1; p.membraneTension = 0;
                p.artifacts = p.strikeJitter = 0; p.resonatorCoupling = 1;
                p.tuneCents = -200;
                p.velocityToHardness = p.velocityToDecay = 0;
                e->SetParameters(p);
                e->SetPitchBendCents(-1200);
                e->NoteOn(0, 1);
                Tracker t;
                t.rate = rate;
                t.where = Fmt("minimum-note stop rate %.0f quality %dx model %d striker %d",
                              rate, 2 << p.oversampling, model, striker);
                RenderFor(*e, t, 0.05);
                const double drained = DrainUntilSilent(*e, t, o.drainSeconds);
                if (drained < 0)
                    gReport.Fail(Fmt("%s: still active after %.0f s:%s", t.where.c_str(), o.drainSeconds,
                                     Access::DescribeVoices(*e).c_str()));
                else if (o.verbose)
                    std::printf("  %s: silent %.2f s after stop\n", t.where.c_str(), drained);
            }
}

void Scenarios(const Options& o) {
    MinimumNoteStops(o);
    // S1: every striker x note-off mode, per model: strike, hold, release -> silent.
    for (int model = 0; model < 4; ++model)
        for (int mallet = 0; mallet < maremba::kStrikerCount; ++mallet)
            for (int mode = 0; mode < 3; ++mode) {
                Tracker t;
                t.rate = 44100.0;
                t.where = Fmt("scenario release model %d mallet %d noteOff %d", model, mallet, mode);
                auto e = std::make_unique<MarembaEngine>(t.rate);
                EngineParameters p;
                p.model = model;
                p.malletType = mallet;
                p.noteOffMode = mode;
                p.decay = 0.25f;
                e->SetParameters(p);
                e->NoteOn(60, 0.8f);
                RenderFor(*e, t, 0.3);
                e->NoteOff(60);
                const long limit = static_cast<long>(15.0 * t.rate);
                long frames = 0;
                while (!e->IsSilent() && frames < limit) { RenderChecked(*e, t, 64, 0x1Fu); frames += 64; }
                if (!e->IsSilent())
                    gReport.Fail(Fmt("%s: voice never ends after the key is released (15 s):%s", t.where.c_str(),
                                     Access::DescribeVoices(*e).c_str()));
                else if (o.verbose)
                    std::printf("  %s: silent %.2f s after release\n", t.where.c_str(), frames / t.rate);
            }

    // S2: transport stop (DampAll) while the key is held, then the key is
    // released: silent within the limit (a held dead stroke keeps its key
    // until the note-off, by design).
    // (the held states: a dead stroke, a resting thumb)
    for (int model = 0; model < 4; ++model)
        for (int mallet : {1, 4})
            for (int mode : {0, 2}) {
                Tracker t;
                t.rate = 44100.0;
                t.where = Fmt("scenario DampAll-held model %d mallet %d noteOff %d", model, mallet, mode);
                auto e = std::make_unique<MarembaEngine>(t.rate);
                EngineParameters p;
                p.model = model;
                p.malletType = mallet;
                p.noteOffMode = mode;
                p.decay = 0.25f;
                e->SetParameters(p);
                e->NoteOn(60, 0.8f);
                RenderFor(*e, t, 0.3);
                e->DampAll();
                RenderFor(*e, t, 0.3);
                e->NoteOff(60);
                const long limit = static_cast<long>(15.0 * t.rate);
                long frames = 0;
                while (!e->IsSilent() && frames < limit) { RenderChecked(*e, t, 64, 0x1Fu); frames += 64; }
                if (!e->IsSilent())
                    gReport.Fail(Fmt("%s: still sounding 15 s after DampAll and the release:%s", t.where.c_str(),
                                     Access::DescribeVoices(*e).c_str()));
            }

    // S3: a chord larger than the pending queue during an oversampling fade:
    // the newest keys must sound (a steal would take the oldest).
    {
        Tracker t;
        t.rate = 48000.0;
        t.where = "scenario queued chord";
        auto e = std::make_unique<MarembaEngine>(t.rate);
        EngineParameters p;
        p.polyphony = 2;
        e->SetParameters(p);
        e->NoteOn(40, 0.8f);
        RenderFor(*e, t, 0.05);
        p.oversampling = 1;
        e->SetParameters(p);            // fade starts
        for (int n = 50; n < 90; ++n) e->NoteOn(n, 0.8f);   // 40 keys within the 5 ms fade
        RenderFor(*e, t, 0.02);
        int lost = 0;
        for (int n = 82; n < 90; ++n) lost += !Access::NoteActive(*e, n);
        if (lost)
            gReport.Fail(Fmt("%s: %d of the 8 newest keys struck during the oversampling fade never sound "
                             "(pending queue holds 32)", t.where.c_str(), lost));
    }

    // S4 (behaviour): Kalimba hole cover. The wheel moved while the engine is
    // idle must apply at the next strike, as if it had been moved while the
    // previous note still rang (Reset() and idle agree).
    {
        const double rate = 44100.0;
        EngineParameters p;
        p.model = 3;
        p.decay = 0.25f;                      // idle sooner
        auto a = std::make_unique<MarembaEngine>(rate);
        auto c = std::make_unique<MarembaEngine>(rate);
        Tracker ta, tc;
        ta.rate = tc.rate = rate;
        ta.where = tc.where = "scenario kalimba idle cover";
        for (auto* e : {a.get(), c.get()}) { e->SetParameters(p); e->NoteOn(62, 0.8f); }
        RenderFor(*a, ta, 0.1);
        RenderFor(*c, tc, 0.1);
        EngineParameters up = p;
        up.modWheel = 1.0f;
        c->SetParameters(up);                 // C: while the note rings
        for (long n = 0; n < static_cast<long>(30.0 * rate / 64) && !(a->IsSilent() && c->IsSilent()); ++n) {
            RenderChecked(*a, ta, 64, 0x1Fu);
            RenderChecked(*c, tc, 64, 0x1Fu);
        }
        if (!a->IsSilent() || !c->IsSilent()) gReport.Fail(ta.where + ": engines did not go idle");
        a->SetParameters(up);                 // A: while idle
        a->NoteOn(62, 0.8f);
        c->NoteOn(62, 0.8f);
        static Buffers ba, bc;
        double diff = 0.0, sig = 0.0;
        for (int k = 0; k < 200; ++k) {
            a->RenderBatch(ba.ch[0], ba.ch[1], ba.ch[2], ba.ch[3], ba.ch[4], ba.ch[5], ba.ch[6], 64);
            c->RenderBatch(bc.ch[0], bc.ch[1], bc.ch[2], bc.ch[3], bc.ch[4], bc.ch[5], bc.ch[6], 64);
            for (int f = 0; f < 64; ++f) {
                const double d = static_cast<double>(ba.ch[0][f]) - bc.ch[0][f];
                diff += d * d;
                sig += static_cast<double>(bc.ch[0][f]) * bc.ch[0][f];
            }
        }
        if (diff > 1.0e-6 * sig)
            gReport.Fail(Fmt("%s: a strike after the wheel moved while idle differs from one after it moved while "
                             "ringing by %.1f dB re the note (cover glides from its stale value)",
                             ta.where.c_str(), 10.0 * std::log10(diff / sig)));
    }

    // S5 (behaviour): switching Model from a bar instrument to Kalimba while a
    // chord and its sympathetic halo ring must not make the halo louder.
    for (int from : {0, 1, 2}) {
        const double rate = 44100.0;
        auto run = [&](float sym, std::vector<float>& out) {
            auto e = std::make_unique<MarembaEngine>(rate);
            EngineParameters p;
            p.model = from;
            p.sympathetic = sym;
            p.strikeJitter = 0.0f;
            p.artifacts = 0.0f;
            e->SetParameters(p);
            for (int n : {36, 43, 48, 55}) e->NoteOn(n, 0.8f);
            static Buffers b;
            for (long i = 0; i < static_cast<long>(2.0 * rate / 64); ++i) {
                if (i == static_cast<long>(1.0 * rate / 64)) { p.model = 3; e->SetParameters(p); }
                e->RenderBatch(b.ch[0], b.ch[1], b.ch[2], b.ch[3], b.ch[4], b.ch[5], b.ch[6], 64);
                out.insert(out.end(), b.ch[0], b.ch[0] + 64);
            }
        };
        std::vector<float> wet, dry;
        run(0.4f, wet);
        run(0.0f, dry);
        auto halo = [&](double t0, double t1) {
            double h = 0.0;
            for (size_t i = static_cast<size_t>(t0 * rate); i < static_cast<size_t>(t1 * rate); ++i) {
                const double d = static_cast<double>(wet[i]) - dry[i];
                h += d * d;
            }
            return h / ((t1 - t0) * rate);
        };
        const double before = halo(0.5, 1.0), after = halo(1.0, 1.5);
        if (after > 2.0 * before)
            gReport.Fail(Fmt("scenario model switch %d->3: sympathetic halo jumps %.1f dB at the switch",
                             from, 10.0 * std::log10(after / before)));
    }

    FreshStrikeOnTheControlGrid();
}

void FreshStrikeOnTheControlGrid() {
    // The rim's harmonics are scaled at control rate by the tube's swing. A
    // fresh strike's tube starts from rest, so a tick a few samples after
    // the mallet lands sees almost no swing, and the tube grows a thousand-
    // fold before the next tick (128 samples at 8x): every offset of the
    // strike against the 16-frame control grid, soft and long contacts.
    for (int offset = 0; offset < 16; ++offset)
        for (int note : {36, 41, 48, 55}) {
            Tracker t;
            t.rate = 48000.0;
            t.where = Fmt("scenario fresh rosewood strike, note %d, %d frames into the control grid", note, offset);
            auto e = std::make_unique<MarembaEngine>(t.rate);
            EngineParameters p;
            p.model = 0;
            p.malletType = 0;
            p.malletHardness = 0.0f;
            p.oversampling = 2;
            p.strikeJitter = p.artifacts = 0.0f;
            e->SetParameters(p);
            e->NoteOn(96, 0.05f);            // keeps the engine awake while the grid moves
            RenderChecked(*e, t, 64, 0x1Fu);
            if (offset > 0) RenderChecked(*e, t, offset, 0x1Fu);
            e->NoteOn(note, 1.0f);
            RenderFor(*e, t, 0.2);
            if (t.peakMain > 1.0) gReport.Fail(Fmt("%s: main peak %.3g", t.where.c_str(), t.peakMain));
        }
}

void Usage() {
    std::printf("EngineFuzzTest [--seeds N] [--determinism-seeds N] [--first S] [--steps K] [--drain SECONDS]\n"
                "               [--only fuzz|determinism|reset|denormal|scenarios]\n"
                "               [--tail-rate HZ] [--tail-os 0|1|2] [--tail-seconds S]\n"
                "               [--reset-rate HZ] [--reset-os 0|1|2] [--verbose]\n");
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() { if (i + 1 >= argc) { Usage(); std::exit(2); } return std::string(argv[++i]); };
        if (a == "--seeds") o.seeds = std::atoi(value().c_str());
        else if (a == "--determinism-seeds") o.determinismSeeds = std::atoi(value().c_str());
        else if (a == "--first") o.first = std::strtoull(value().c_str(), nullptr, 10);
        else if (a == "--steps") o.steps = std::atoi(value().c_str());
        else if (a == "--drain") o.drainSeconds = std::atof(value().c_str());
        else if (a == "--only") o.only = value();
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--tail-rate") o.tailRate = std::atof(value().c_str());
        else if (a == "--reset-rate") o.resetRate = std::atof(value().c_str());
        else if (a == "--reset-os") o.resetOversampling = std::atoi(value().c_str());
        else if (a == "--tail-os") o.tailOversampling = std::atoi(value().c_str());
        else if (a == "--tail-seconds") o.tailSeconds = std::atof(value().c_str());
        else { Usage(); return 2; }
    }
    if (!std::isfinite(o.resetRate) || (o.resetRate != 0.0 && (o.resetRate < 22050.0 || o.resetRate > 192000.0))
        || o.resetOversampling < -1 || o.resetOversampling > 2) {
        Usage();
        return 2;
    }
    const auto want = [&](const char* name) { return o.only.empty() || o.only == name; };
    const auto t0 = std::chrono::steady_clock::now();

    if (want("scenarios")) {
        std::printf("[Scenarios] release, transport stop and queued-note state machines ...\n");
        Scenarios(o);
    }
    if (want("fuzz") || want("drain")) {
        std::printf("[Fuzz] %d seeds from %llu, %d steps each ...\n", o.seeds, static_cast<unsigned long long>(o.first), o.steps);
        for (int s = 0; s < o.seeds; ++s) FuzzSeed(o.first + static_cast<uint64_t>(s), o);
    }
    if (want("determinism")) {
        std::printf("[Determinism] the same performance, bit for bit: fresh engines, block sizes, Reset(), %d seeds ...\n", o.determinismSeeds);
        for (int s = 0; s < o.determinismSeeds; ++s) DeterminismSeed(o.first + static_cast<uint64_t>(s), o);
    }
    if (want("reset")) {
        std::printf("[Reset] render after Reset() == fresh engine, %d seeds ...\n", o.seeds);
        for (int s = 0; s < o.seeds; ++s) ResetEquivalenceSeed(o.first + static_cast<uint64_t>(s), o);
    }
    if (want("denormal")) {
        std::printf("[Tails] long decays: no subnormal state, no CPU growth ...\n");
        for (int model = 0; model < 4; ++model) DenormalTail(model, o.tailRate, o.tailOversampling, o.tailSeconds, o);
        for (int model = 0; model < 4; ++model) VoiceEnds(model, o);
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& line : gReport.lines) std::printf("  FAIL: %s\n", line.c_str());
    std::printf("%s: %d failure(s) in %.1f s\n", gReport.failures ? "FAILED" : "PASSED", gReport.failures, seconds);
    return gReport.failures ? 1 : 0;
}
