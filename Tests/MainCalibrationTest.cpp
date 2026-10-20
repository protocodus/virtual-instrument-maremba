// Product-level Main calibration through the same player and control mapping
// used by the plug-in. The five-second reference includes its complete tail;
// RMS is sqrt((sum(L^2) + sum(R^2)) / (2 * frames)), relative to full scale.
// No rendered samples are normalized. --quiet measures the source at low
// Volume .2125 at any supported rate; --measure reports without the level gates.
// Initialized models retain their recording-gain RMS anchor; factory presets
// target normal peaks around -8 dBFS with measured per-preset RMS contracts.
#include "MarembaPlayer.h"
#include "FactoryPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace maremba;

// Historical initialized-model recording-gain anchor, retained independently
// of the factory VCA balance below.
constexpr double kTargetRmsDb = -27.0;
constexpr double kRmsToleranceDb = .35;
constexpr double kCeiling = .940002;
constexpr double kMainCeiling = .8912509381337456; // -1 dBFS, independent of stem ceilings.
struct FactoryReference {
    const char* name; double rmsDb, peakMinimumDb, peakMaximumDb; bool maximumVca;
};
// Frozen three-rate measurements: per-preset RMS includes natural crest and tails.
// Soft Yarn and Thumb Lullaby reach their existing VCA range limit without
// changing the soft striker or the global Volume law.
constexpr std::array<FactoryReference, 20> kFactoryReferences {{
    {"Ambient Cathedral Bloom", -24.1420, -8.40, -7.60, false},
    {"Concert Grand Rosewood", -23.2491, -8.40, -7.60, false},
    {"Direct Piezo Mallet", -29.3001, -8.40, -7.60, false},
    {"Hard Acrylic Virtuoso", -29.8926, -8.40, -7.60, false},
    {"Rosewood Dampened Etude", -23.0172, -8.40, -7.60, false},
    {"Rosewood Roll Chorale", -23.4324, -8.40, -7.60, false},
    {"Soft Yarn Nocturne", -23.3697, -8.85, -8.25, true},
    {"Chiapas Folk Fiesta", -30.0878, -8.40, -7.60, false},
    {"Guatemalan Charleo", -26.8384, -8.40, -7.60, false},
    {"Mayan Dance Padauk", -23.6442, -8.40, -7.60, false},
    {"Ancestral Balafon", -27.5884, -8.40, -7.60, false},
    {"Birifor Gyil Buzz", -27.3120, -8.40, -7.60, false},
    {"Chopi Timbila Ensemble", -28.6327, -8.40, -7.60, false},
    {"Equipentatonic Log Keys", -30.0088, -8.40, -7.60, false},
    {"Mandinka Bala Heptatonic", -28.5980, -8.40, -7.60, false},
    {"Midnight Village Ritual", -24.5500, -8.40, -7.60, false},
    {"Sambla Talking Balafon", -28.6696, -8.40, -7.60, false},
    {"Kalimba Muted Thumbs", -22.1407, -8.40, -7.60, false},
    {"Kalimba Pick Sparkle", -21.9231, -8.40, -7.60, false},
    {"Kalimba Thumb Lullaby", -26.4239, -12.85, -12.05, true},
}};
constexpr double kSeconds = 5.0;
// Shared musical reference: Rack Tests/AllPatchRenderTest.cpp, kPhraseNotes.
constexpr std::array<int, 20> kPhraseNotes {{
    60, 64, 67, 72, 67, 64, 60, 60, 62, 65, 69, 74, 69, 65, 62, 62, 48, 55, 36, 84
}};
constexpr std::array<int, 8> kLowChord {{24, 28, 31, 36, 40, 43, 48, 52}};

struct Options {
    double rate = 48000;
    double targetRmsDb = kTargetRmsDb;
    bool measure = false, quiet = false, stress = false, customRms = false;
    std::filesystem::path captureDirectory;
};
struct Event { int frame, note, velocity; };
struct State { std::string name; ControlValues controls; };
struct Result {
    std::array<double, 4> rms {}, peak {};
};

void Require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error(message);
}

double Number(const char* text) {
    std::size_t used = 0;
    const std::string value(text);
    const double result = std::stod(value, &used);
    Require(used == value.size() && std::isfinite(result), "invalid numeric argument: " + value);
    return result;
}

Options Parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--measure") options.measure = true;
        else if (arg == "--quiet") options.quiet = true;
        else if (arg == "--stress") options.stress = true;
        else if (arg == "--rate" && i + 1 < argc) options.rate = Number(argv[++i]);
        else if (arg == "--target-rms" && i + 1 < argc) { options.targetRmsDb = Number(argv[++i]); options.customRms = true; }
        else if (arg == "--capture" && i + 1 < argc) options.captureDirectory = argv[++i];
        else throw std::runtime_error("usage: MainCalibrationTest [--measure] [--quiet] [--stress] "
                                      "[--rate HZ] [--target-rms DBFS] [--capture DIR]");
    }
    Require(MarembaPlayer::SupportsSampleRate(options.rate), "sample rate outside 22050..192000 Hz");
    Require(!(options.quiet && options.stress), "--quiet and --stress require different Volume settings");
    Require(options.captureDirectory.empty() || options.quiet,
            "--capture requires --quiet");
    return options;
}

std::vector<State> States() {
    std::vector<State> states;
    for (int model = 0; model < 4; ++model) {
        auto controls = kControlDefaults;
        controls[kModel] = model;
        states.push_back({"Init model " + std::to_string(model), controls});
    }
    for (const auto& preset : kFactoryPresets) states.push_back({preset.name, preset.values});
    return states;
}

std::vector<Event> Phrase(double rate, int velocity, int transpose) {
    std::vector<Event> events;
    for (std::size_t step = 0; step < kPhraseNotes.size(); ++step) {
        const int note = kPhraseNotes[step] + transpose;
        Require(note >= 0 && note <= 127, "reference phrase exceeds MIDI note range");
        const double onset = static_cast<double>(step) * .15;
        events.push_back({static_cast<int>(std::lround(onset * rate)), note, velocity});
        events.push_back({static_cast<int>(std::lround((onset + .10) * rate)), note, 0});
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.frame < b.frame;
    });
    return events;
}

std::vector<Event> LowChord(double rate) {
    std::vector<Event> events;
    for (int note : kLowChord) events.push_back({0, note, 127});
    for (int note : kLowChord)
        events.push_back({static_cast<int>(std::lround(.7 * rate)), note, 0});
    return events;
}

Result Render(const State& state, const Options& options, const std::string& scenario,
              const std::vector<Event>& events, bool stress, std::size_t stateIndex,
              std::ostream* metadata) {
    auto controls = state.controls;
    controls[kVelocityCurve] = 1; // Calibrate explicitly on the Linear curve.
    if (stress) controls[kVolume] = 1;
    else if (options.quiet) controls[kVolume] = .2125;
    MarembaPlayer player;
    player.Prepare(options.rate);
    player.SetControls(controls);
    player.Reset();

    Result result;
    std::array<double, 4> squares {};
    const int frames = static_cast<int>(std::lround(kSeconds * options.rate));
    std::vector<float> capture;
    if (metadata) capture.reserve(static_cast<std::size_t>(frames) * 2);
    std::size_t nextEvent = 0;
    const std::string context = state.name + " / " + scenario + " / " + std::to_string(options.rate);
    for (int frame = 0; frame < frames;) {
        while (nextEvent < events.size() && events[nextEvent].frame == frame) {
            const auto& event = events[nextEvent++];
            const std::array<std::uint8_t, 3> midi {{
                static_cast<std::uint8_t>(event.velocity ? 0x90 : 0x80),
                static_cast<std::uint8_t>(event.note), static_cast<std::uint8_t>(event.velocity)
            }};
            player.HandleMidi(midi.data(), static_cast<int>(midi.size()));
        }
        const int end = nextEvent < events.size() ? std::min(frames, events[nextEvent].frame) : frames;
        const int count = std::min(MarembaEngine::kMaxFrames, end - frame);
        Require(count > 0, context + ": event schedule made no progress");
        std::array<std::array<float, MarembaEngine::kMaxFrames>, 7> audio;
        for (auto& channel : audio) channel.fill(12345.f);
        player.Render(audio[0].data(), audio[1].data(), audio[2].data(), audio[3].data(),
                      audio[4].data(), audio[5].data(), audio[6].data(), count);
        for (int channel = 0; channel < 7; ++channel) {
            const int group = channel / 2;
            for (int sample = 0; sample < count; ++sample) {
                const double value = audio[channel][sample];
                if (!std::isfinite(value) || value == 12345.)
                    throw std::runtime_error(context + ": nonfinite/unwritten output");
                const double ceiling = channel < 2 ? kMainCeiling + 2e-6 : kCeiling;
                if (std::abs(value) > ceiling)
                    throw std::runtime_error(context + ": output " + std::to_string(channel)
                                             + " exceeds the smooth protection ceiling");
                result.peak[group] = std::max(result.peak[group], std::abs(value));
                squares[group] += value * value;
            }
            for (int sample = count; sample < MarembaEngine::kMaxFrames; ++sample)
                if (audio[channel][sample] != 12345.f)
                    throw std::runtime_error(context + ": output overrun");
        }
        if (metadata) for (int sample = 0; sample < count; ++sample) {
            capture.push_back(audio[0][sample]);
            capture.push_back(audio[1][sample]);
        }
        frame += count;
    }
    Require(result.peak[0] > 1e-8 && squares[0] > 1e-12, context + ": silent Main output");
    for (int group = 0; group < 4; ++group)
        result.rms[group] = std::sqrt(squares[group] / ((group == 3 ? 1. : 2.) * frames));
    const std::string captureName = metadata ? "state-" + std::to_string(stateIndex) + ".f32" : "";
    if (metadata) {
        // Native-endian IEEE float32, stereo interleaved L,R, without normalization.
        std::ofstream output(options.captureDirectory / captureName, std::ios::binary);
        output.write(reinterpret_cast<const char*>(capture.data()),
                     static_cast<std::streamsize>(capture.size() * sizeof(float)));
        Require(static_cast<bool>(output), "cannot write capture " + captureName);
    }
    const auto db = [](double value) { return 20. * std::log10(std::max(value, 1e-15)); };
    const auto row = [&](std::ostream& out) {
        out << std::setprecision(12) << '"' << state.name << "\"," << static_cast<int>(controls[kModel])
            << ',' << scenario << ',' << options.rate << ',' << controls[kVolume];
        for (int group = 0; group < 4; ++group)
            out << ',' << db(result.rms[group]) << ',' << db(result.peak[group]);
        out << ',' << captureName << ',' << frames << ",2,float32_native\n" << std::flush;
    };
    row(std::cout);
    if (metadata) row(*metadata);
    if (!options.measure && !options.quiet && !stress) {
        const auto reference = std::find_if(kFactoryReferences.begin(), kFactoryReferences.end(),
            [&](const FactoryReference& value) { return state.name == value.name; });
        const bool factory = reference != kFactoryReferences.end();
        Require(factory || state.name.rfind("Init model ", 0) == 0,
                context + ": missing independent factory level reference");
        const double rmsTarget = factory && !options.customRms ? reference->rmsDb : options.targetRmsDb;
        Require(std::abs(db(result.rms[0]) - rmsTarget) <= kRmsToleranceDb,
                context + ": Main RMS " + std::to_string(db(result.rms[0]))
                + " dBFS differs from " + std::to_string(rmsTarget) + " dBFS target");
        if (factory) {
            Require(db(result.peak[0]) >= reference->peakMinimumDb
                        && db(result.peak[0]) <= reference->peakMaximumDb,
                    context + ": normal Main peak departs from the authored -8 dBFS balance / soft-preset bound");
            Require(!reference->maximumVca || controls[kVolume] == 1.,
                    context + ": quiet exception must exhaust its existing VCA range");
        }
        Require(db(result.peak[0]) <= -1. + .01,
                context + ": normal Main peak " + std::to_string(db(result.peak[0]))
                + " dBFS exceeds the -1 dBFS safety ceiling");
    }
    return result;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = Parse(argc, argv);
        std::ofstream metadata;
        if (!options.captureDirectory.empty()) {
            Require(!std::filesystem::exists(options.captureDirectory), "capture directory already exists");
            Require(std::filesystem::create_directories(options.captureDirectory), "cannot create capture directory");
            metadata.open(options.captureDirectory / "captures.csv");
            Require(static_cast<bool>(metadata), "cannot open capture metadata");
        }
        constexpr auto header = "state,model,scenario,sample_rate,volume,main_rms_dbfs,main_peak_dbfs,"
            "close_rms_dbfs,close_peak_dbfs,room_rms_dbfs,room_peak_dbfs,piezo_rms_dbfs,piezo_peak_dbfs,"
            "capture_file,frames,channels,sample_format\n";
        std::cout << header;
        if (metadata.is_open()) metadata << header;
        const auto normal = Phrase(options.rate, 80, 0);
        const auto low = Phrase(options.rate, 127, -24);
        const auto chord = LowChord(options.rate);
        const auto states = States();
        for (std::size_t index = 0; index < states.size(); ++index) {
            const auto& state = states[index];
            Render(state, options, "phrase_v80_linear", normal, false, index,
                   metadata.is_open() ? &metadata : nullptr);
            if (options.stress) {
                Render(state, options, "phrase_low_v127_max_volume", low, true, index, nullptr);
                Render(state, options, "chord_low_v127_max_volume", chord, true, index, nullptr);
            }
        }
        std::cerr << "PASS MainCalibrationTest: 24 states, " << (options.stress ? 3 : 1)
                  << " scenarios, " << options.rate << " Hz; Main <= -1 dBFS, stems <= " << kCeiling;
        if (!options.measure && !options.quiet)
            std::cerr << "; init RMS " << options.targetRmsDb << ", factory normal peaks around -8 dBFS, "
                      << "bounded soft exceptions; measured RMS +/- " << kRmsToleranceDb << " dB";
        std::cerr << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL MainCalibrationTest: " << error.what() << '\n';
        return 1;
    }
}
