// Hardest-hit integration contract. This complements ExtremeRangeTest with every
// physical striker on every model, then crosses all requested rates/qualities
// under repeated 24-note chords. --measure records baseline ceiling violations
// without stopping; the default requires every output below the safety ceiling.
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static double Clock(const MarembaEngine& e) { return e.mClock; }
};
}

namespace {
using maremba::EngineParameters;
using maremba::MarembaEngine;
constexpr int kChannels = 7;
constexpr double kCeiling = .940002;
constexpr std::array<double, 4> kRates{{44100, 48000, 96000, 192000}};
constexpr std::array<int, 6> kNotes{{0, 21, 48, 60, 108, 127}};
constexpr std::array<int, 24> kWideChord{{0, 12, 21, 24, 33, 36, 40, 43, 48, 52, 55, 60,
                                        64, 67, 72, 76, 79, 84, 88, 96, 100, 108, 120, 127}};
constexpr std::array<float, 3> kLimits{{0, .01f, 1}};
constexpr std::array<const char*, kChannels> kNames{{"main_l", "main_r", "close_l", "close_r",
                                                   "room_l", "room_r", "piezo"}};
struct Case {
    int id, model, striker, rateIndex, quality, profile, note, limitIndex, drive, warmth;
    bool chord;
};
struct Result {
    Case c;
    int frames = 0;
    std::array<double, kChannels> peak{}, rms{};
    std::array<int, kChannels> peakFrame{};
    std::array<std::uint64_t, kChannels> overCeiling{}, overFullScale{};
    std::shared_ptr<std::vector<float>> audio;
};
struct Best { double peak = -1; std::size_t result = 0; };
std::vector<Result> results;
std::array<std::array<Best, kChannels>, 12> bestByRateQuality{};
std::array<double, kChannels> peaks{};
std::array<std::uint64_t, kChannels> overCeiling{}, overFullScale{};
std::uint64_t totalFrames = 0, clockChecks = 0;
int maximumActive = 0;
const auto started = std::chrono::steady_clock::now();

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

EngineParameters parameters(const Case& c) {
    EngineParameters p;
    p.model = c.model;
    p.malletType = c.striker;
    p.malletHardness = 1;
    p.oversampling = c.quality;
    p.polyphony = 2;
    p.noteOffMode = 0;
    p.velocityCurve = 3;
    p.volume = p.closeLevel = p.farLevel = p.piezoLevel = 1;
    p.stereoWidth = 2;
    p.warmth = static_cast<float>(c.warmth);
    p.preampDrive = static_cast<float>(c.drive);
    p.compAmount = kLimits[c.limitIndex];
    p.compAttack = 50;
    p.compRelease = 500;
    p.roomType = c.profile;
    p.decay = 8;
    p.resonatorCoupling = p.bodyBloom = p.sympathetic = p.artifacts = p.buzzAmount = 1;
    p.strikePosition = c.profile == 1 ? .42f : 1;
    p.carving = p.membraneTension = c.profile == 1 ? 0 : 1;
    p.resonatorTune = c.profile == 1 ? -50 : c.profile == 0 ? 50 : 0;
    p.tuneCents = c.profile == 1 ? -200 : c.profile == 0 ? 200 : 0;
    p.detune = 0;
    p.strikeJitter = c.profile == 1 ? 0 : 100;
    const float depth = c.profile == 2 ? 1.f : 0.f;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = depth;
    p.velocityToCarving = p.velocityToCoupling = p.velocityToBloom = depth;
    p.velocityToSympathetic = p.velocityToBuzz = p.velocityToTension = p.velocityToArtifacts = depth;
    if (c.profile == 2) {
        // All ten VOL depths are at full scale. The nine additional controls
        // start at minimum; Hardness stays at 1 as required for this matrix.
        p.strikePosition = p.carving = p.membraneTension = 0;
        p.resonatorCoupling = p.bodyBloom = p.sympathetic = p.artifacts = p.buzzAmount = 0;
        p.decay = .1f;
    }
    return p;
}

Result renderCase(const Case& c) {
    const double rate = kRates[c.rateIndex];
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(parameters(c));
    engine->Reset();
    Result r;
    r.c = c;
    r.frames = static_cast<int>(std::ceil(rate * (c.chord ? .50 : .20)));
    r.audio = std::make_shared<std::vector<float>>(static_cast<std::size_t>(r.frames) * kChannels);
    std::array<double, kChannels> sums{};
    const int strikes = c.chord ? 6 : 1;
    int nextStrike = 0;
    for (int frame = 0; frame < r.frames;) {
        const int eventFrame = nextStrike < strikes
            ? static_cast<int>(std::lround(rate * .080 * nextStrike)) : r.frames;
        if (frame == eventFrame && nextStrike < strikes) {
            if (c.chord) {
                for (int i = 0; i < 24; ++i) {
                    const int note = nextStrike % 2 == 0 ? 48 + i : kWideChord[i];
                    engine->NoteOn(note, 1.f);
                }
            } else {
                engine->NoteOn(c.note, 1.f);
            }
            ++nextStrike;
            continue;
        }
        // Uneven blocks also prove that the detector is independent of the
        // event's placement within a normal 64-frame host block.
        const int block = std::min({(frame / 64) % 3 == 0 ? 17 : 64, r.frames - frame,
                                    eventFrame - frame});
        require(block > 0, "invalid event schedule");
        std::array<std::array<float, 66>, kChannels> out;
        for (auto& channel : out) channel.fill(12345.f);
        const double before = maremba::MarembaEngineTestAccess::Clock(*engine);
        const bool active = !engine->IsSilent();
        engine->RenderBatch(out[0].data() + 1, out[1].data() + 1, out[2].data() + 1,
                            out[3].data() + 1, out[4].data() + 1, out[5].data() + 1,
                            out[6].data() + 1, block);
        if (active) {
            require(maremba::MarembaEngineTestAccess::Clock(*engine) > before,
                    "case " + std::to_string(c.id) + ": emergency reset concealed an overload");
            ++clockChecks;
        }
        maximumActive = std::max(maximumActive, engine->GetActiveVoiceCount());
        require(maximumActive <= MarembaEngine::kVoicePoolSize, "voice-pool overflow");
        for (int ch = 0; ch < kChannels; ++ch) {
            require(out[ch][0] == 12345.f, "output underrun");
            for (int i = block + 1; i < 66; ++i) require(out[ch][i] == 12345.f, "output overrun");
            for (int i = 0; i < block; ++i) {
                const double x = out[ch][i + 1];
                require(std::isfinite(x) && x != 12345., "unwritten/nonfinite output");
                (*r.audio)[static_cast<std::size_t>(frame + i) * kChannels + ch] = static_cast<float>(x);
                const double a = std::abs(x);
                if (a > r.peak[ch]) { r.peak[ch] = a; r.peakFrame[ch] = frame + i; }
                sums[ch] += x * x;
                if (a > kCeiling) ++r.overCeiling[ch];
                if (a > 1.) ++r.overFullScale[ch];
            }
        }
        frame += block;
    }
    require(*std::max_element(r.peak.begin(), r.peak.end()) > 1e-8,
            "case " + std::to_string(c.id) + ": no audible physical response");
    for (int ch = 0; ch < kChannels; ++ch) r.rms[ch] = std::sqrt(sums[ch] / r.frames);
    return r;
}

void run(const Case& c) {
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    require(elapsed < 900, "hard-hit matrix exceeded 900-second wall-time bound");
    Result r = renderCase(c);
    const std::size_t index = results.size();
    const int group = c.rateIndex * 3 + c.quality;
    for (int ch = 0; ch < kChannels; ++ch) {
        peaks[ch] = std::max(peaks[ch], r.peak[ch]);
        overCeiling[ch] += r.overCeiling[ch];
        overFullScale[ch] += r.overFullScale[ch];
        if (r.peak[ch] > bestByRateQuality[group][ch].peak)
            bestByRateQuality[group][ch] = {r.peak[ch], index};
    }
    totalFrames += r.frames;
    results.push_back(std::move(r));
    // Retain only clips currently worst on at least one output/rate/quality.
    // Their complete seven-channel frames are later written without clipping.
    std::vector<bool> retained(results.size(), false);
    for (const auto& groupBest : bestByRateQuality)
        for (const auto& best : groupBest)
            if (best.peak >= 0) retained[best.result] = true;
    for (std::size_t i = 0; i < results.size(); ++i) if (!retained[i]) results[i].audio.reset();
    if (results.size() % 60 == 0)
        std::cout << "cases=" << results.size() << " frames=" << totalFrames
                  << " elapsed=" << elapsed << "s\n" << std::flush;
}

template<class T, std::size_t N>
void arrayJson(std::ostream& out, const std::array<T, N>& a) {
    out << '[';
    for (std::size_t i = 0; i < N; ++i) { if (i) out << ','; out << a[i]; }
    out << ']';
}

void report(const std::filesystem::path& directory, bool measure) {
    std::filesystem::create_directories(directory);
    std::ofstream out(directory / "matrix.json");
    require(static_cast<bool>(out), "cannot create report");
    out << std::setprecision(12);
    out << "{\n\"schema\":1,\"measurement_only\":" << (measure ? "true" : "false")
        << ",\"ceiling_with_tolerance\":" << kCeiling << ",\"channels\":[";
    for (int ch = 0; ch < kChannels; ++ch) { if (ch) out << ','; out << '"' << kNames[ch] << '"'; }
    out << "],\"sample_format\":\"little-endian interleaved float32; 7 channels; unprocessed engine outputs\""
        << ",\"cases\":" << results.size() << ",\"frames\":" << totalFrames
        << ",\"guard_clock_checks\":" << clockChecks << ",\"maximum_active_voices\":" << maximumActive
        << ",\"peaks\":";
    arrayJson(out, peaks);
    out << ",\"samples_over_ceiling\":"; arrayJson(out, overCeiling);
    out << ",\"samples_over_full_scale\":"; arrayJson(out, overFullScale);
    out << ",\"results\":[\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i) out << ",\n";
        const auto& r = results[i]; const auto& c = r.c;
        out << "{\"id\":" << c.id << ",\"kind\":\"" << (c.chord ? "repeated-24-note-chords" : "isolated")
            << "\",\"model\":" << c.model << ",\"striker\":" << c.striker
            << ",\"sample_rate\":" << kRates[c.rateIndex] << ",\"oversampling_factor\":" << (2 << c.quality)
            << ",\"profile\":" << c.profile << ",\"note\":" << c.note << ",\"midi_velocity\":127"
            << ",\"limit\":" << kLimits[c.limitIndex] << ",\"drive\":" << c.drive
            << ",\"warmth\":" << c.warmth << ",\"frames\":" << r.frames << ",\"peak\":";
        arrayJson(out, r.peak); out << ",\"peak_frame\":"; arrayJson(out, r.peakFrame);
        out << ",\"rms\":"; arrayJson(out, r.rms);
        out << ",\"samples_over_ceiling\":"; arrayJson(out, r.overCeiling);
        out << ",\"samples_over_full_scale\":"; arrayJson(out, r.overFullScale);
        if (r.audio) {
            const std::string filename = "case-" + std::to_string(c.id) + ".f32";
            std::ofstream raw(directory / filename, std::ios::binary);
            raw.write(reinterpret_cast<const char*>(r.audio->data()),
                      static_cast<std::streamsize>(r.audio->size() * sizeof(float)));
            require(static_cast<bool>(raw), "cannot write raw audio");
            out << ",\"audio_file\":\"" << filename << "\"";
        }
        out << '}';
    }
    out << "\n]}\n";
    require(static_cast<bool>(out), "cannot finish report");
}
}

int main(int argc, char** argv) {
    bool measure = false;
    std::filesystem::path directory;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--measure") measure = true;
            else if (arg == "--output-dir" && i + 1 < argc) directory = argv[++i];
            else throw std::runtime_error("usage: HardHitOutputTest [--measure] [--output-dir DIRECTORY]");
        }
        int id = 0;
        // Every model/striker gets all six note positions and all three physical
        // profiles at 48 kHz/2x. Endpoints rotate independently of note/profile.
        for (int model = 0; model < 4; ++model)
            for (int striker = 0; striker < 5; ++striker)
                for (int ni = 0; ni < 6; ++ni)
                    for (int profile = 0; profile < 3; ++profile)
                        run({id++, model, striker, 1, 0, profile, kNotes[ni],
                             (ni + profile) % 3, (ni / 2 + profile) % 2,
                             (ni + profile) % 2 ? 1 : -1, false});
        // Full rate/quality coverage per model/striker; six repeated dense/wide
        // chords exercise 24 live notes and their overlapping stolen tails.
        for (int model = 0; model < 4; ++model)
            for (int striker = 0; striker < 5; ++striker)
                for (int ri = 0; ri < 4; ++ri)
                    for (int quality = 0; quality < 3; ++quality)
                        run({id++, model, striker, ri, quality, (ri + 2 * quality + model) % 3,
                             -1, (ri + quality) % 3, (ri / 2 + quality) % 2,
                             (ri + quality + striker) % 2 ? 1 : -1, true});
        if (!directory.empty()) report(directory, measure);
        std::uint64_t violations = 0;
        for (int ch = 0; ch < kChannels; ++ch) {
            std::cout << std::setprecision(12) << kNames[ch] << " peak=" << peaks[ch]
                      << " over_ceiling=" << overCeiling[ch]
                      << " over_full_scale=" << overFullScale[ch] << '\n';
            violations += overCeiling[ch];
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "Hard-hit matrix: " << results.size() << " cases, " << totalFrames
                  << " frames, " << clockChecks << " reset guards, " << maximumActive
                  << " maximum active voices, " << seconds << " seconds\n";
        require(measure || violations == 0, "one or more outputs exceeded the smooth safety ceiling");
        std::cout << (measure ? "MEASURED (ceiling violations reported, not waived for release)\n" : "PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
