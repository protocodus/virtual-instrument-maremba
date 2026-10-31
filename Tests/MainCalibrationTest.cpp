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
// 1.6.0 re-measured them with the synthesized sphere head in Close: the bars
// +0.11 to +0.25 dB, Kalimba -0.30 dB (CloseHrtf.h's trim keeps their mean).
constexpr std::array<double, 4> kInitRmsDb {{-21.8421, -21.7794, -20.8799, -22.5710}};
constexpr double kRmsToleranceDb = .35;
constexpr double kCeiling = .940002;
constexpr double kMainCeiling = .8912509381337456; // -1 dBFS, independent of stem ceilings.
struct FactoryReference {
    const char* name; double rmsDb, peakMinimumDb, peakMaximumDb; bool maximumVca;
};
// Frozen installed-gain/final-bank three-rate measurements; RMS includes natural crest and tails.
// Retain the existing +/-0.35 dB RMS tolerance and peak-window widths. The
// rebalanced bank no longer needs the former full-VCA soft-preset exceptions.
// 1.5.8 re-froze three rows at 44.1/48/96 kHz: the cord and yarn cores and
// the bars' longer upper partials raised Concert Grand's and Mayan Dance's
// peaks by 0.7-0.9 dB and Roll Chorale's yarn rolls by 0.9 dB RMS. No VCA
// was re-trimmed; every peak stays under the -1 dBFS ceiling.
// 1.5.9 re-trimmed three Volumes against the Rack bank's velocity-100
// single/pair reference (-22.2 dBFS +/-0.35, Direct Piezo -22.6): Roll Chorale
// -0.73 dB (back to its 1.5.7 loudness; its row re-pinned), Equipentatonic
// -0.30 dB (its row still holds), and Direct Piezo -0.50 dB, whose pair had
// reached 2.3 dB of Main guard energy loss against a 2 dB bound (RMS row only;
// its peak still sits on the ceiling).
// 1.5.10 set Position VEL to 0% on thirteen bar presets: Dampened Etude's
// velocity-80 peak rose 1.1 dB and Roll Chorale's 0.2 dB out of their windows
// (strokes nearer the centre's full fundamental) and were re-pinned; every
// RMS row holds.
// 1.5.10 also moved four balafon/padauk peaks out of their windows (Mandinka
// +0.13, Midnight +0.06, Sambla +0.09, Mayan Dance -1.1 dB) and re-pinned them,
// and trimmed Roll Chorale -0.56 dB and Mayan Dance -0.36 dB, which centring
// had lifted above the Rack bank's velocity-100 RMS reference.
// 1.5.11 damped the rosewood bars' upper modes at the cords: Direct Piezo
// (whose film reads them) fell 0.41 dB and Hard Acrylic 0.43 dB in RMS, both
// re-pinned as intended sound changes; their peaks stay in their windows.
// Its longer fundamentals lifted Soft Yarn Nocturne's peak 0.1 dB out of its
// window, re-centred on the three rates' mean with the same 0.6 dB width.
// 1.5.12 raised Direct Piezo's Volume 0.46 dB and 1.5.13 took it back: the
// Rack's Main guard bound (2 dB energy loss) holds it at its 1.5.11 level.
// 1.6.0 replaced Close's measured head responses with a synthesized rigid
// sphere and re-trimmed eight Volumes against the Rack's velocity-100
// reference. Every row was re-pinned on the three rates' mean, peak windows
// re-centred at their former widths: peaks fell 0.1-2.2 dB (the sphere's
// minimum-phase onsets are less peaky than the recorded ears'), RMS moved
// +0.08 to -0.75 dB. Equipentatonic's window is wider (1.85 dB): its log
// keys' single-strike peaks differ by up to 5 dB between 48 and 96 kHz at
// equal RMS, so its peak spans -3.56 to -2.17 dB across rates; its former
// row sat near the -1 dBFS ceiling, which compressed that spread.
constexpr std::array<FactoryReference, 20> kFactoryReferences {{
    {"Ambient Cathedral Bloom", -24.0203, -8.5613, -7.7613, false},
    {"Concert Grand Rosewood", -24.5943, -9.5392, -8.7392, false},
    {"Direct Piezo Mallet", -23.6736, -1.4388, -0.6388, false},
    {"Hard Acrylic Virtuoso", -24.5087, -3.4984, -2.6984, false},
    {"Rosewood Dampened Etude", -25.5681, -11.1106, -10.3106, false},
    {"Rosewood Roll Chorale", -24.2215, -8.8889, -8.0889, false},
    {"Soft Yarn Nocturne", -25.2900, -10.9300, -10.3300, false},
    {"Chiapas Folk Fiesta", -24.7462, -3.6414, -2.8414, false},
    {"Guatemalan Charleo", -25.1957, -8.4914, -7.6914, false},
    {"Mayan Dance Padauk", -25.5066, -11.9151, -11.1151, false},
    {"Ancestral Balafon", -24.7183, -6.4775, -5.6775, false},
    {"Birifor Gyil Buzz", -24.5593, -6.8194, -6.0194, false},
    {"Chopi Timbila Ensemble", -24.3288, -5.1557, -4.3557, false},
    {"Equipentatonic Log Keys", -23.9961, -3.8000, -1.9500, false},
    {"Mandinka Bala Heptatonic", -24.3632, -4.9644, -4.1644, false},
    {"Midnight Village Ritual", -25.4276, -9.0605, -8.2605, false},
    {"Sambla Talking Balafon", -24.1807, -4.8980, -4.0980, false},
    {"Kalimba Muted Thumbs", -21.4790, -7.2964, -6.4964, false},
    {"Kalimba Pick Sparkle", -23.1419, -9.3601, -8.5601, false},
    {"Kalimba Thumb Lullaby", -25.6665, -11.7815, -10.9815, false},
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
