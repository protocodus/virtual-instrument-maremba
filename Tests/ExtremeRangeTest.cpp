// Deterministic release boundary matrix, complementary to EngineFuzzTest.
// Default/--compact: representative note/velocity/profile axes, exact low-pluck
// regressions, every quality, hostile API notes and bounded chord/drain cases.
// --full retains every historical MIDI key/velocity/profile plus every boundary
// velocity at all rates/qualities, dense driven chords and long transport drains.
// --stratified uses one
// rotating endpoint profile per key and velocity for a quicker diagnostic.
// --chords-only runs the identical 116 dense-chord cases from the full matrix.
// Safety does not assert equal timbre/level at physically extrapolated notes.
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace maremba {
struct MarembaEngineTestAccess {
    static double Clock(const MarembaEngine& e) { return e.mClock; }
    static double Rate(const MarembaEngine& e) { return e.mBaseSampleRate; }
    static int Live(const MarembaEngine& e) { return e.CountLiveVoices(); }
    static bool HasNote(const MarembaEngine& e, int note) {
        for (const auto& voice : e.mVoices)
            if (voice.IsActive() && voice.GetNoteNumber() == note) return true;
        return false;
    }
    static const MarembaVoice* NoteVoice(const MarembaEngine& e, int note) {
        for (const auto& voice : e.mVoices)
            if (voice.IsActive() && voice.GetNoteNumber() == note) return &voice;
        return nullptr;
    }
};
} // namespace maremba

namespace {
using maremba::EngineParameters;
using maremba::MarembaEngine;
using Access = maremba::MarembaEngineTestAccess;
constexpr std::array<double, 5> kRates{{22050, 44100, 48000, 96000, 192000}};
constexpr std::array<float, 3> kVelocities{{1.0f / 127, .5f, 1}};
constexpr std::array<int, 4> kBoundaryNotes{{0, 21, 108, 127}};
// The measured physical-capture run completed its first 12024 cases in 681.7s
// and reached the old 900s guard during dense chords, without an audio failure.
// This is a finite suite hang guard, not a realtime CPU or sound-quality gate.
constexpr int kWallBudgetSeconds = 3600;
const char* const kProfiles[] = {"minimum", "maximum", "loose-coupled", "stiff-dry-muted"};
const auto start = std::chrono::steady_clock::now();
std::string where;
std::array<double, 7> peaks{};
std::array<std::string, 7> peakCases;
double limitedPeak = 0, unlimitedPeak = 0, longestDrain = 0;
double limiterSettlesAt = 0, transitionPeak = 0, chordStep = 0;
std::array<double, 2> previousMain{};
bool havePreviousMain = false, observeChord = false;
int renderBlock = 64;
std::array<std::uint64_t, 7> chordDigests{};
double pluckPeak = 0;
double pluckVoiceEnergy = 0;
bool constrainPluck = false;
int constrainedNote = -1;
std::uint64_t cases = 0, frames = 0, guardChecks = 0, noteStarts = 0;
std::uint64_t fullNoteCases = 0, rateCases = 0, rateNoteCases = 0, hostileCases = 0, drainCases = 0;
std::uint64_t pluckCases = 0, pluckSurvivalChecks = 0;
std::uint64_t chordCases = 0, chordBatchPairs = 0, transitionFrames = 0;
std::uint64_t limiterEngagementCases = 0;
std::uint64_t polyphonyCases = 0, polyphonyTransitions = 0;
int maximumActive = 0, maximumLive = 0;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(where + ": " + message);
}

double elapsed() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void context(const char* stage, int model, double rate, int quality, int note, float velocity, int profile) {
    where = std::string(stage) + " model=" + std::to_string(model) + " rate=" + std::to_string(rate)
          + " quality=" + std::to_string(2 << quality) + "x note=" + std::to_string(note)
          + " velocity=" + std::to_string(velocity) + " profile=" + kProfiles[profile];
    require(elapsed() < kWallBudgetSeconds,
            "matrix exceeded its " + std::to_string(kWallBudgetSeconds) + "-second wall-time bound");
}

EngineParameters endpoint(int model, int quality, int profile, int note) {
    EngineParameters p;
    p.model = model; p.oversampling = quality;
    p.malletType = (note + profile) % 5;
    p.noteOffMode = (note + profile) % 3;
    p.velocityCurve = (note + profile) % 4;
    p.tuningScale = (note + profile) % 7;
    p.roomType = profile == 1 ? 2 : profile == 2 ? 1 : 0;
    p.polyphony = profile == 0 ? 0 : 2;
    p.closeLevel = p.farLevel = p.piezoLevel = p.volume = 1;
    p.carving = profile == 0 || profile == 2 ? 0 : 1;
    p.membraneTension = profile == 0 || profile == 2 ? 0 : 1;
    p.buzzAmount = profile == 1 || profile == 2 ? 1 : 0;
    p.malletHardness = profile == 1 || profile == 2 ? 1 : 0;
    p.strikePosition = profile == 1 || profile == 2 ? 1 : 0;
    p.decay = profile == 0 ? .1f : 8;
    p.resonatorCoupling = profile == 0 ? 0 : 1;
    p.artifacts = profile == 1 || profile == 2 ? 1 : 0;
    p.bodyBloom = p.sympathetic = profile == 0 ? 0 : 1;
    p.resonatorTune = profile == 1 || profile == 3 ? 50 : -50;
    p.tuneCents = profile == 1 || profile == 3 ? 200 : -200;
    p.detune = p.strikeJitter = profile == 0 ? 0 : 100;
    p.stereoWidth = profile == 0 ? 0 : 2;
    p.preampDrive = p.compAmount = profile == 1 || profile == 3 ? 1 : 0;
    p.warmth = profile == 1 || profile == 3 ? 1 : -1;
    p.compAttack = profile == 1 || profile == 3 ? 50 : 1;
    p.compRelease = profile == 1 || profile == 3 ? 500 : 20;
    p.modWheel = profile == 2 ? 1 : 0;
    const float depth = profile >= 2 ? 1 : 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = depth;
    p.velocityToCarving = p.velocityToCoupling = p.velocityToBloom = depth;
    p.velocityToSympathetic = p.velocityToBuzz = p.velocityToTension = p.velocityToArtifacts = p.velocityToVariance = depth;
    if (profile == 3) p.closeLevel = p.farLevel = p.piezoLevel = p.volume = 0;
    return p;
}

void render(MarembaEngine& engine, int count, bool mustBeZero = false) {
    std::array<std::array<float, 66>, 7> output;
    for (auto& channel : output) channel.fill(12345);
    const double before = Access::Clock(engine);
    const bool active = !engine.IsSilent();
    engine.RenderBatch(output[0].data() + 1, output[1].data() + 1, output[2].data() + 1,
                       output[3].data() + 1, output[4].data() + 1, output[5].data() + 1,
                       output[6].data() + 1, count);
    if (active) {
        ++guardChecks;
        require(Access::Clock(engine) > before, "output safety guard reset the engine during rendering");
    }
    if (constrainPluck) {
        const auto* voice = Access::NoteVoice(engine, constrainedNote);
        require(voice, "low pluck was prematurely killed while its tine should still ring");
        const double energy = voice->ComputeEnergy();
        require(std::isfinite(energy), "nonfinite low-pluck mechanical energy");
        // The verified 1.4.0f3 boundary matrix peaked at 6.3864 in this
        // physical state. A generous 5x bound catches runaway preload even
        // when output gain protection would hide it from the main waveform.
        require(energy <= 32, "pathological low-register Kalimba mechanical energy");
        pluckVoiceEnergy = std::max(pluckVoiceEnergy, energy);
        ++pluckSurvivalChecks;
    }
    const auto& p = engine.GetParameters();
    for (int channel = 0; channel < 7; ++channel) {
        for (int i = 0; i < 66; ++i) {
            if (i == 0 || i > count) {
                require(output[channel][i] == 12345, "render wrote outside requested buffer bounds");
                continue;
            }
            const double sample = output[channel][i];
            require(std::isfinite(sample) && sample != 12345, "unwritten/nonfinite audio on output " + std::to_string(channel));
            if (std::abs(sample) > peaks[channel]) {
                peaks[channel] = std::abs(sample);
                peakCases[channel] = where + " frame=" + std::to_string(static_cast<long long>(before) + i - 1);
            }
            if (mustBeZero) require(sample == 0, "reset/ignored note leaked audio");
            require(std::abs(sample) <= .940002,
                    "output escaped smooth headroom ceiling, including limiter bypass/automation");
            if (channel < 2) {
                const bool limited = p.compAmount > 0;
                if (observeChord) {
                    if (havePreviousMain || i > 1) chordStep = std::max(chordStep, std::abs(sample - previousMain[channel]));
                    previousMain[channel] = sample;
                    if (limited && before + i - 1 < limiterSettlesAt) {
                        transitionPeak = std::max(transitionPeak, std::abs(sample));
                        if (channel == 0) ++transitionFrames;
                    }
                }
                if (constrainPluck) {
                    pluckPeak = std::max(pluckPeak, std::abs(sample));
                }
                if (p.compAmount == 0) unlimitedPeak = std::max(unlimitedPeak, std::abs(sample));
                if (limited) {
                    limitedPeak = std::max(limitedPeak, std::abs(sample));
                }
            }
            if (observeChord) {
                std::uint32_t bits;
                const float value = static_cast<float>(sample);
                std::memcpy(&bits, &value, sizeof(bits));
                chordDigests[channel] ^= bits;
                chordDigests[channel] *= 1099511628211ULL;
            }
        }
    }
    if (observeChord) havePreviousMain = true;
    const int activeVoices = engine.GetActiveVoiceCount(), live = Access::Live(engine);
    const int limit = p.polyphony == 0 ? 8 : p.polyphony == 1 ? 16 : 24;
    maximumActive = std::max(maximumActive, activeVoices); maximumLive = std::max(maximumLive, live);
    require(activeVoices >= 0 && activeVoices <= MarembaEngine::kVoicePoolSize, "active voice count outside pool");
    require(live >= 0 && live <= limit, "live voice count exceeds selected polyphony");
    frames += count;
}

void forSeconds(MarembaEngine& e, double rate, double seconds) {
    int remaining = static_cast<int>(std::ceil(rate * seconds));
    while (remaining) {
        const int count = std::min(remaining, renderBlock);
        render(e, count);
        remaining -= count;
    }
}

void resetChecked(MarembaEngine& e) {
    e.Reset();
    limiterSettlesAt = 0;
    havePreviousMain = false;
    require(e.IsSilent() && e.GetActiveVoiceCount() == 0, "Reset did not clear voices and tails");
    render(e, 64, true);
}

void attackCase(MarembaEngine& e, const char* stage, double rate, int model, int quality,
                int note, float velocity, int profile) {
    context(stage, model, rate, quality, note, velocity, profile);
    auto p = endpoint(model, quality, profile, note);
    e.SetParameters(p); resetChecked(e);
    e.SetPitchBendCents(profile == 2 ? -200 : profile == 3 ? 200 : 0);
    e.NoteOn(note, velocity); ++noteStarts;
    require(e.GetActiveVoiceCount() == 1 && Access::HasNote(e, note), "valid MIDI note was ignored or remapped");
    // Sub-audio plucks can peak well after the first 10 ms (a previous
    // Kalimba preload defect peaked at 33--79 ms), so retain the full onset.
    forSeconds(e, rate, .150);
    p.membraneTension = 1 - p.membraneTension;
    e.SetParameters(p); // live opposite tension while the endpoint strike rings
    forSeconds(e, rate, .004);
    e.NoteOff(note);
    forSeconds(e, rate, .004);
    e.DampAll();
    forSeconds(e, rate, .004);
    resetChecked(e);
    ++cases;
}

void hostileNotes(MarembaEngine& e, double rate, int model, int quality) {
    for (int note : {INT_MIN, -16384, -1, 128, 16384, INT_MAX}) {
        context("hostile-note", model, rate, quality, note, 1, 0);
        e.SetParameters(endpoint(model, quality, 0, 60)); resetChecked(e);
        e.NoteOn(note, 1); e.NoteOff(note);
        require(e.IsSilent() && e.GetActiveVoiceCount() == 0, "out-of-range note was clamped or activated");
        render(e, 64, true);
        e.NoteOn(60, .5f);
        e.NoteOff(note); e.NoteOn(note, 1);
        require(e.GetActiveVoiceCount() == 1 && Access::HasNote(e, 60), "hostile note changed valid held note");
        render(e, 64); resetChecked(e);
        ++cases; ++hostileCases;
    }
}

void pluckRegression(MarembaEngine& e, double rate, int quality, int note,
                     int carving, int buzz, int noteOff) {
    // Keep technique/tuning selectors fixed: deriving them from the MIDI key
    // previously hid the MIDI 11/12 guard-reset and late thumb-return cases.
    context("kalimba-preload", 3, rate, quality, note, 1, 2);
    where += " carving=" + std::to_string(carving) + " buzz=" + std::to_string(buzz)
           + " noteOff=" + std::to_string(noteOff);
    auto p = endpoint(3, quality, 2, 0);
    p.carving = static_cast<float>(carving); p.buzzAmount = static_cast<float>(buzz);
    p.noteOffMode = noteOff;
    e.SetParameters(p); resetChecked(e); e.SetPitchBendCents(-200);
    e.NoteOn(note, 1); ++noteStarts;
    constrainPluck = true;
    constrainedNote = note;
    forSeconds(e, rate, .020);
    forSeconds(e, rate, .180);
    // With Decay=8 these low tines remain active at 200 ms even after the
    // Dead thumb return. All Buzz=0 controls retain them. The one-way board
    // buzzer must not kill a still-ringing tine through a numerical guard.
    constrainPluck = false;
    constrainedNote = -1;
    e.NoteOff(note); e.DampAll(); forSeconds(e, rate, .004); resetChecked(e);
    ++cases; ++pluckCases;
}

void drainExtremeChord(MarembaEngine& e, int model, int noteOff) {
    context("drain", model, 48000, 0, 0, 1, 1);
    where += " noteOff=" + std::to_string(noteOff);
    auto p = endpoint(model, 0, 1, 0);
    p.noteOffMode = noteOff; p.malletType = model == 3 ? 3 : 4;
    p.membraneTension = 0; p.modWheel = 1;
    e.SetParameters(p); resetChecked(e); e.SetPitchBendCents(0);
    for (int note : {0, 21, 60, 108, 127}) { e.NoteOn(note, 1); ++noteStarts; }
    forSeconds(e, 48000, .050);
    for (int note : {0, 21, 60, 108, 127}) e.NoteOff(note);
    forSeconds(e, 48000, .050);
    e.DampAll();
    std::uint64_t count = 0;
    while (!e.IsSilent() && count < 30 * 48000) { render(e, 64); count += 64; }
    require(e.IsSilent() && e.GetActiveVoiceCount() == 0, "extreme chord remains active 30s after transport stop");
    longestDrain = std::max(longestDrain, count / 48000.0);
    render(e, 64, true); resetChecked(e);
    ++cases; ++drainCases;
}

std::array<std::uint64_t, 7> denseChord(MarembaEngine& e, int model, double rate, int quality,
                         int polyphony, int attack, int release, int drive, int block) {
    context("dense-chord", model, rate, quality, 0, 1, 2);
    where += " polyphony=" + std::to_string(8 * (polyphony + 1)) + " attack=" + std::to_string(attack)
           + " release=" + std::to_string(release) + " drive=" + std::to_string(drive)
           + " block=" + std::to_string(block);
    auto p = endpoint(model, quality, 2, 0);
    p.polyphony = polyphony; p.noteOffMode = 1; p.modWheel = 0;
    p.compAmount = 1; p.compAttack = static_cast<float>(attack); p.compRelease = static_cast<float>(release);
    p.preampDrive = static_cast<float>(drive); p.warmth = 1;
    e.SetParameters(p); resetChecked(e); e.SetPitchBendCents(-200);
    constexpr int notes[] = {0, 1, 2, 7, 11, 12, 13, 20, 21, 28, 36, 48,
                            60, 72, 84, 96, 108, 112, 119, 123, 124, 125, 126, 127};
    renderBlock = block; observeChord = true; chordDigests.fill(1469598103934665603ULL);
    for (int note : notes) { e.NoteOn(note, 1); ++noteStarts; }
    forSeconds(e, rate, .150);
    // Repeated extremes exercise restrikes and voice stealing during the tail.
    for (int note : {0, 11, 12, 13, 21, 127}) { e.NoteOn(note, 1); ++noteStarts; }
    forSeconds(e, rate, .050);
    p.compAmount = 0; e.SetParameters(p);
    forSeconds(e, rate, .010);
    p.compAmount = 1; e.SetParameters(p);
    // Observe the audible 2 ms Amount transition after the common output
    // delay. Smooth headroom already applies during bypass and stays active.
    limiterSettlesAt = Access::Clock(e) + maremba::OutputHeadroom::LatencyFrames(rate)
                     + std::ceil(.002 * rate) + 1;
    forSeconds(e, rate, .040);
    p.compAttack = attack == 1 ? 50 : 1; p.compRelease = release == 20 ? 500 : 20;
    e.SetParameters(p); // change ballistics while the limiter stays engaged
    forSeconds(e, rate, .050);
    for (int note : {0, 11, 12, 13, 21})
        require(Access::HasNote(e, note), "recently restruck low chord voice vanished before note-off");
    for (int note : notes) e.NoteOff(note);
    forSeconds(e, rate, .100); e.DampAll();
    const auto digest = chordDigests;
    observeChord = false; // compare the same event-bounded interval at 32/64
    std::uint64_t count = 0;
    while (!e.IsSilent() && count < 30 * rate) {
        render(e, renderBlock); count += renderBlock;
    }
    require(e.IsSilent() && e.GetActiveVoiceCount() == 0, "dense chord remains active 30s after transport stop");
    longestDrain = std::max(longestDrain, count / rate);
    render(e, renderBlock, true);
    renderBlock = 64; resetChecked(e);
    ++cases; ++chordCases;
    return digest;
}

void limiterEngagement() {
    for (double rate : kRates) for (int quality = 0; quality < 3; ++quality)
        for (float amount : {.0001f, .25f, 1.0f})
            for (float attack : {1.0f, 50.0f}) for (float release : {20.0f, 500.0f}) {
                where = "limiter-engagement rate=" + std::to_string(rate)
                      + " quality=" + std::to_string(2 << quality) + " amount=" + std::to_string(amount)
                      + " attack=" + std::to_string(attack) + " release=" + std::to_string(release);
                const double internalRate = rate * (2 << quality);
                maremba::AnalogLimiter limiter;
                limiter.SetSampleRate(internalRate); limiter.SetTimes(attack, release);
                limiter.SetAmount(0); limiter.Reset();
                float left, right;
                limiter.Process(24, -12, left, right);
                require(left == 24 && right == -12, "settled zero-amount limiter changed bypass signal");
                maremba::OutputHeadroom headroom;
                headroom.SetSampleRate(rate);
                const int interval = static_cast<int>(std::ceil(.005 * rate));
                const int latency = maremba::OutputHeadroom::LatencyFrames(rate);
                double peak = 0;
                for (int i = 0; i < 3 * interval; ++i) {
                    if (i == interval) limiter.SetAmount(amount);
                    if (i == 2 * interval) limiter.SetAmount(0);
                    for (int sub = 0; sub < (2 << quality); ++sub)
                        limiter.Process(24, -12, left, right);
                    std::array<float, 7> frame{{left, right, 0, 0, 0, 0, 0}};
                    headroom.Process(frame);
                    left = frame[0]; right = frame[1];
                    require(std::isfinite(left) && std::isfinite(right), "limiter engagement produced nonfinite output");
                    require(std::abs(left) <= .940002f && std::abs(right) <= .940002f,
                            "headroom leaked over-ceiling output during live limiter bypass/engagement");
                    if (i < latency) require(left == 0 && right == 0, "headroom latency is not exact");
                    else require(left > 0 && right < 0 && std::abs(left + 2 * right) < 1.0e-6f,
                                 "limiter engagement changed stereo balance or polarity");
                    peak = std::max(peak, static_cast<double>(left));
                }
                require(peak > .1, "limiter engagement fixture produced no audible output");
                ++cases; ++limiterEngagementCases;
            }
}

void compactPolyphony(MarembaEngine& e, int model) {
    context("polyphony-transitions", model, 48000, 0, 48, 1, 1);
    auto p = endpoint(model, 0, 1, 48);
    p.polyphony = 0; p.noteOffMode = 1; p.modWheel = 0;
    e.SetParameters(p); resetChecked(e); e.SetPitchBendCents(0);
    for (int note = 48; note < 56; ++note) { e.NoteOn(note, 1); ++noteStarts; }
    require(Access::Live(e) == 8, "eight-voice fixture did not fill its live pool");
    forSeconds(e, 48000, .004);
    p.polyphony = 2; e.SetParameters(p);
    for (int note = 56; note < 72; ++note) { e.NoteOn(note, 1); ++noteStarts; }
    require(Access::Live(e) == 24, "8-to-24 transition did not admit the extra voices");
    ++polyphonyTransitions;
    forSeconds(e, 48000, .004);
    p.polyphony = 0; e.SetParameters(p);
    require(Access::Live(e) == 8, "24-to-8 transition did not enforce the new live limit");
    ++polyphonyTransitions;
    forSeconds(e, 48000, .010); // include the existing stealing fade
    for (int note = 48; note < 72; ++note) e.NoteOff(note);
    e.DampAll(); forSeconds(e, 48000, .010); resetChecked(e);
    ++polyphonyCases; ++cases;
}

void compactMatrix() {
    // Independent axes, not their Cartesian product. All control midpoints and
    // every selector/radio state are enumerated by EngineFuzzTest's bounded
    // control matrix; this suite owns the costly physical boundary witnesses.
    constexpr std::array<double, 3> rates{{22050, 48000, 192000}};
    constexpr std::array<int, 5> lowPlucks{{0, 11, 12, 21, 28}};
    constexpr std::array<int, 5> notes{{0, 21, 60, 108, 127}};
    limiterEngagement(); // all five historical rates and all three qualities
    for (double rate : rates) {
        auto e = std::make_unique<MarembaEngine>(rate);
        for (int quality = 0; quality < 3; ++quality) {
            if (rate != 48000 && quality != 0) continue;
            for (int note : lowPlucks)
                for (int carving : {0, 1}) for (int buzz : {0, 1}) for (int noteOff : {0, 2})
                    pluckRegression(*e, rate, quality, note, carving, buzz, noteOff);
        }
        std::cout << "PLUCK_RATE_COMPLETE rate=" << rate << " cases=" << cases << " seconds=" << elapsed() << std::endl;
    }
    for (int model = 0; model < 4; ++model) {
        auto e = std::make_unique<MarembaEngine>(48000);
        for (int note : notes) for (float velocity : kVelocities)
            for (int profile = 0; profile < 4; ++profile) {
                attackCase(*e, "representative-notes", 48000, model, 0, note, velocity, profile);
                ++fullNoteCases;
            }
        // Ring mode is the worst release case. Dense chords separately retain
        // Damp; exact preload fixtures retain both Ring and Dead behavior.
        drainExtremeChord(*e, model, 0);
        compactPolyphony(*e, model);
        std::cout << "MODEL_COMPLETE model=" << model << " cases=" << cases << " seconds=" << elapsed() << std::endl;
    }
    for (double rate : rates) {
        auto e = std::make_unique<MarembaEngine>(rate);
        for (int model = 0; model < 4; ++model) for (int quality = 0; quality < 3; ++quality) {
            for (int index = 0; index < 2; ++index) for (int velocity = 0; velocity < 2; ++velocity) {
                // Rotate maximum and loose-coupled witnesses independently of
                // the key value; both profiles reach every rate/quality/model.
                const int profile = 1 + (index + velocity) % 2;
                attackCase(*e, "rate-quality", rate, model, quality, index == 0 ? 0 : 127,
                           velocity == 0 ? kVelocities.front() : kVelocities.back(), profile);
                ++rateCases;
            }
            hostileNotes(*e, rate, model, quality);
        }
        std::cout << "RATE_COMPLETE rate=" << rate << " cases=" << cases << " seconds=" << elapsed() << std::endl;
    }
    for (int model = 0; model < 4; ++model) {
        auto e = std::make_unique<MarembaEngine>(48000);
        auto split = std::make_unique<MarembaEngine>(48000);
        const auto reference = denseChord(*e, model, 48000, 0, 2, 50, 20, 1, 64);
        const auto alternate = denseChord(*split, model, 48000, 0, 2, 50, 20, 1, 32);
        require(reference == alternate, "dense chord/limiter transitions depend on host batch size");
        ++chordBatchPairs;
        std::cout << "CHORD_MODEL_COMPLETE model=" << model << " cases=" << cases << " seconds=" << elapsed() << std::endl;
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool compact = argc == 1 || (argc == 2 && std::string(argv[1]) == "--compact");
    const bool full = argc == 2 && std::string(argv[1]) == "--full";
    const bool velocityOnly = argc == 2 && std::string(argv[1]) == "--velocity-only";
    const bool chordsOnly = argc == 2 && std::string(argv[1]) == "--chords-only";
    if (argc > 2 || (argc == 2 && !compact && !full && !velocityOnly && !chordsOnly && std::string(argv[1]) != "--stratified")) {
        std::cerr << "Usage: ExtremeRangeTest [--compact|--full|--stratified|--velocity-only|--chords-only]\n"; return 2;
    }
    const char* mode = compact ? "compact" : full ? "full" : velocityOnly ? "velocity-only" : chordsOnly ? "chords-only" : "stratified";
    const char* phase = compact ? "bounded-boundaries" : full ? "complete-matrix" : chordsOnly ? "dense-chords"
                                                          : velocityOnly ? "velocity" : "stratified-matrix";
    const std::uint64_t expectedCases = compact ? 996 : full ? 12140 : velocityOnly ? 5056 : chordsOnly ? 116 : 3156;
    std::cout << std::setprecision(9) << "ExtremeRangeTest mode=" << mode << " phase=" << phase
              << " expected_cases=" << expectedCases << " wall_budget_seconds=" << kWallBudgetSeconds << std::endl;
    try {
        if (compact) compactMatrix();
        if (!compact) {
        if (!velocityOnly && !chordsOnly) limiterEngagement();
        if (!velocityOnly && !chordsOnly) for (double rate : kRates) {
            auto e = std::make_unique<MarembaEngine>(rate);
            for (int quality = 0; quality < 3; ++quality)
                for (int note = 0; note < 36; ++note) {
                    if ((!full || quality != 0) && note != 0 && note != 11 && note != 12 && note != 21 && note != 28) continue;
                    for (int carving : {0, 1}) for (int buzz : {0, 1}) for (int noteOff : {0, 2})
                        pluckRegression(*e, rate, quality, note, carving, buzz, noteOff);
                }
            std::cout << "PLUCK_RATE_COMPLETE rate=" << rate << " cases=" << cases << " seconds=" << elapsed() << std::endl;
        }
        if (!chordsOnly) for (int model = 0; model < 4; ++model) {
            auto e = std::make_unique<MarembaEngine>(48000);
            for (int note = 0; note < 128; ++note)
                for (float velocity : kVelocities) {
                    if (velocityOnly && velocity == 1) continue;
                    for (int i = 0; i < (full || velocityOnly ? 4 : 1); ++i) {
                        attackCase(*e, "all-notes", 48000, model, 0, note, velocity, full || velocityOnly ? i : note % 4);
                        ++fullNoteCases;
                    }
                }
            if (full) for (int noteOff = 0; noteOff < 3; ++noteOff) drainExtremeChord(*e, model, noteOff);
            std::cout << "MODEL_COMPLETE model=" << model << " cases=" << cases << " seconds=" << elapsed() << std::endl;
        }
        if (!chordsOnly) for (double rate : kRates) {
            auto e = std::make_unique<MarembaEngine>(rate);
            for (int model = 0; model < 4; ++model) for (int quality = 0; quality < 3; ++quality) {
                for (int note : kBoundaryNotes) for (int profile : {1, 2})
                    for (int v = 0; v < (full || velocityOnly ? 3 : 1); ++v) {
                        if (velocityOnly && v == 2) continue;
                        attackCase(*e, "rate-quality", rate, model, quality, note, full || velocityOnly ? kVelocities[v] : 1, profile);
                        ++rateCases;
                    }
                if (!velocityOnly) hostileNotes(*e, rate, model, quality);
            }
            // Every key also crosses the entire documented sample-rate axis.
            // The 48 kHz sweep above already includes this exact profile.
            if (full && rate != 48000) for (int model = 0; model < 4; ++model)
                for (int note = 0; note < 128; ++note) {
                    attackCase(*e, "rate-note-axis", rate, model, 0, note, 1, 2);
                    ++rateNoteCases;
                }
            std::cout << "RATE_COMPLETE rate=" << rate << " cases=" << cases << " seconds=" << elapsed() << std::endl;
        }
        if (full || chordsOnly) for (int model = 0; model < 4; ++model) {
            for (int polyphony = 0; polyphony < 3; ++polyphony)
                for (int attack : {1, 50}) for (int release : {20, 500}) for (int drive : {0, 1}) {
                    auto e = std::make_unique<MarembaEngine>(48000);
                    const auto reference = denseChord(*e, model, 48000, 0, polyphony, attack, release, drive, 64);
                    if (polyphony == 2 && attack == 50 && release == 20 && drive == 1) {
                        auto split = std::make_unique<MarembaEngine>(48000);
                        const auto alternate = denseChord(*split, model, 48000, 0, polyphony, attack, release, drive, 32);
                        require(reference == alternate, "dense chord/limiter transitions depend on host batch size");
                        ++chordBatchPairs;
                    }
                }
            // Rate/quality corner cases complement the full 48k/2x axis.
            for (double rate : {22050., 192000.}) for (int quality : {1, 2}) {
                auto e = std::make_unique<MarembaEngine>(rate);
                denseChord(*e, model, rate, quality, 2, 50, 20, 1, 64);
            }
            std::cout << "CHORD_MODEL_COMPLETE model=" << model << " cases=" << cases << " seconds=" << elapsed() << std::endl;
        }
        }
        where = std::string("completed matrix mode=") + mode;
        require(cases == expectedCases, "matrix case count differs from selected mode");
        require(cases == fullNoteCases + rateCases + rateNoteCases + hostileCases + drainCases
                      + pluckCases + chordCases + limiterEngagementCases + polyphonyCases,
                "phase counts do not sum to the completed matrix");
        if (compact)
            require(fullNoteCases == 240 && rateCases == 144 && hostileCases == 216
                    && rateNoteCases == 0 && pluckCases == 200 && drainCases == 4
                    && chordCases == 8 && chordBatchPairs == 4 && limiterEngagementCases == 180
                    && polyphonyCases == 4 && polyphonyTransitions == 8,
                    "compact boundary category coverage is incomplete");
        if (full || chordsOnly)
            require(chordCases == 116 && chordBatchPairs == 4, "dense-chord phase is incomplete");
        std::cout << "SUMMARY {\"result\":\"pass\",\"mode\":\"" << mode
                  << "\",\"phase\":\"" << phase << "\",\"expected_cases\":" << expectedCases
                  << ",\"wall_budget_seconds\":" << kWallBudgetSeconds
                  << ",\"cases\":" << cases << ",\"full_note_cases\":" << fullNoteCases
                  << ",\"rate_quality_cases\":" << rateCases << ",\"hostile_note_cases\":" << hostileCases
                  << ",\"rate_note_axis_cases\":" << rateNoteCases
                  << ",\"kalimba_pluck_cases\":" << pluckCases << ",\"pluck_survival_checks\":" << pluckSurvivalChecks
                  << ",\"dense_chord_cases\":" << chordCases << ",\"chord_batch_pairs\":" << chordBatchPairs
                  << ",\"limiter_engagement_cases\":" << limiterEngagementCases
                  << ",\"limiter_transition_frames\":" << transitionFrames
                  << ",\"drain_cases\":" << drainCases << ",\"note_starts\":" << noteStarts
                  << ",\"polyphony_cases\":" << polyphonyCases << ",\"polyphony_transitions\":" << polyphonyTransitions
                  << ",\"output_frames\":" << frames << ",\"output_guard_checks\":" << guardChecks
                  << ",\"max_active_voices\":" << maximumActive << ",\"max_live_voices\":" << maximumLive
                  << ",\"max_limited_main_peak\":" << limitedPeak << ",\"max_bypassed_main_peak\":" << unlimitedPeak
                  << ",\"max_regression_pluck_peak\":" << pluckPeak
                  << ",\"max_regression_voice_energy\":" << pluckVoiceEnergy
                  << ",\"max_limiter_transition_peak\":" << transitionPeak << ",\"max_chord_adjacent_sample_step\":" << chordStep
                  << ",\"longest_transport_drain_seconds\":" << longestDrain << ",\"wall_seconds\":" << elapsed()
                  << ",\"channel_peaks\":[";
        for (int ch = 0; ch < 7; ++ch) std::cout << (ch ? "," : "") << peaks[ch];
        std::cout << "],\"peak_cases\":[";
        for (int ch = 0; ch < 7; ++ch) std::cout << (ch ? "," : "") << std::quoted(peakCases[ch]);
        std::cout << "]}" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " completed_cases=" << cases << " seconds=" << elapsed() << std::endl;
        return 1;
    }
    return 0;
}
