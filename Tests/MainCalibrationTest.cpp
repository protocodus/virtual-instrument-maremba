// Product-level Main calibration through the same player and control mapping
// used by the plug-in. The five-second reference includes its complete tail;
// RMS is sqrt((sum(L^2) + sum(R^2)) / (2 * frames)), relative to full scale.
// No rendered samples are normalized. --quiet measures the source at low
// Volume .2125 at any supported rate; --measure reports without the level gates.
// This tail-inclusive velocity-80 phrase is a secondary level fingerprint,
// separate from normal velocity-100 single/pair playing and Reason's VU meter.
// Init anchors reflect the installed recording gains and held-body Kalimba.
// Factory RMS and peak references are frozen from the final rebalanced bank.
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

// Measured Init anchors with the installed 1.5.5 recording gains and shorter
// Kalimba lifetime; independent of factory VCA settings. Order follows Model.
constexpr std::array<double, 4> kInitRmsDb {{-22.0903, -21.8999, -20.9930, -22.2698}};
constexpr double kRmsToleranceDb = .35;
constexpr double kCeiling = .940002;
constexpr double kMainCeiling = .8912509381337456; // -1 dBFS, independent of stem ceilings.
struct FactoryReference {
    const char* name; double rmsDb, peakMinimumDb, peakMaximumDb; bool maximumVca;
};
// Frozen preset-only 1.5.7 three-rate measurements; RMS includes natural crest and tails.
// Retain the existing +/-0.35 dB RMS tolerance and peak-window widths. The
// rebalanced bank no longer needs the former full-VCA soft-preset exceptions.
constexpr std::array<FactoryReference, 20> kFactoryReferences {{
    {"Ambient Cathedral Bloom", -23.6076, -7.8603, -7.0603, false},
    {"Concert Grand Rosewood", -24.3869, -9.6317, -8.8317, false},
    {"Direct Piezo Mallet", -24.5365, -3.3566, -2.5566, false},
    {"Hard Acrylic Virtuoso", -24.0889, -2.7067, -1.9067, false},
    {"Rosewood Dampened Etude", -25.3015, -10.1926, -9.3926, false},
    {"Rosewood Roll Chorale", -23.9397, -8.8702, -8.0702, false},
    {"Soft Yarn Nocturne", -25.0764, -10.4512, -9.8512, false},
    {"Chiapas Folk Fiesta", -24.6479, -3.2005, -2.4005, false},
    {"Guatemalan Charleo", -24.6991, -6.3785, -5.5785, false},
    {"Mayan Dance Padauk", -25.3177, -10.3702, -9.5702, false},
    {"Ancestral Balafon", -24.5968, -5.4641, -4.6641, false},
    {"Birifor Gyil Buzz", -24.4074, -5.5336, -4.7336, false},
    {"Chopi Timbila Ensemble", -24.0593, -3.8334, -3.0334, false},
    {"Equipentatonic Log Keys", -23.2555, -1.5204, -0.7204, false},
    {"Mandinka Bala Heptatonic", -24.1025, -3.8688, -3.0688, false},
    {"Midnight Village Ritual", -24.9417, -8.7486, -7.9486, false},
    {"Sambla Talking Balafon", -24.0055, -3.7722, -2.9722, false},
    {"Kalimba Muted Thumbs", -21.0533, -6.9600, -6.1600, false},
    {"Kalimba Pick Sparkle", -22.7781, -8.8209, -8.0209, false},
    {"Kalimba Thumb Lullaby", -25.4674, -11.7098, -10.9098, false},
}};
constexpr double kSeconds = 5.0;
// Shared musical reference: Rack Tests/AllPatchRenderTest.cpp, kPhraseNotes.
constexpr std::array<int, 20> kPhraseNotes {{
    60, 64, 67, 72, 67, 64, 60, 60, 62, 65, 69, 74, 69, 65, 62, 62, 48, 55, 36, 84
}};
constexpr std::array<int, 8> kLowChord {{24, 28, 31, 36, 40, 43, 48, 52}};

struct Options {
    double rate = 48000;
    double targetRmsDb = kInitRmsDb[0];
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
        double rmsTarget = options.targetRmsDb;
        if (!options.customRms) {
            if (factory) rmsTarget = reference->rmsDb;
            else rmsTarget = kInitRmsDb[static_cast<std::size_t>(controls[kModel])];
        }
        Require(std::abs(db(result.rms[0]) - rmsTarget) <= kRmsToleranceDb,
                context + ": Main RMS " + std::to_string(db(result.rms[0]))
                + " dBFS differs from " + std::to_string(rmsTarget) + " dBFS target");
        if (factory) {
            Require(db(result.peak[0]) >= reference->peakMinimumDb
                        && db(result.peak[0]) <= reference->peakMaximumDb,
                    context + ": reference-phrase Main peak differs from the frozen factory measurement");
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
            std::cerr << "; Init/factory phrase RMS references +/- " << kRmsToleranceDb << " dB";
        std::cerr << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL MainCalibrationTest: " << error.what() << '\n';
        return 1;
    }
}
