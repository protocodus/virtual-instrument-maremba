// Offline, VCA-only factory-preset calibration. This emits proposed controls;
// it never edits patches, model gains, the Volume law, or audio source files.
// Search finds the first passing point on a 1/32 VCA grid, then refines only
// that bracket. It does not assert global monotonicity or prove that a narrower
// passing interval cannot exist between earlier grid points.
// The -7 dBFS normal reference leaves 6 dB below Main's -1 dBFS protection
// ceiling; louder playing may use the existing gentle lookahead protection.
//
// Capture each rate with MainCalibrationTest --quiet --capture FRESH_DIRECTORY.
// Quiet Volume .2125 has gain .0625, below both Main dynamics thresholds.
// Usage: CalibratePresetVca [--target DBFS=-7.0] DIR_44100 DIR_48000 DIR_96000
//
// The input already includes the quiet engine's output delay. Replaying it
// through the production protector adds another D-frame delay and truncates
// D additional tail frames from the five-second measurement. This is an
// offline proposal: independently render the actual authored controls after
// applying it. Reduction compares output against its correctly delayed,
// float-rounded input, so tail truncation is not counted as gain reduction.
#include "FactoryPresets.h"
#include "MainCalibration.h"
#include "MainVolume.h"
#include "OutputHeadroom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr double kQuietVolume = .2125;
constexpr double kQuietGain = .0625;
constexpr int kFactoryOffset = 4;
constexpr int kGridSteps = 32;
constexpr int kSearchIterations = 18;
constexpr double kSeconds = 5.;
constexpr std::array<int, 3> kRates {{44100, 48000, 96000}};

void Require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error(message);
}

double Number(const std::string& value, const std::string& context) {
    std::size_t used = 0;
    const double result = std::stod(value, &used);
    Require(used == value.size() && std::isfinite(result), context + ": invalid number");
    return result;
}

double Db(double amplitude) {
    return 20. * std::log10(std::max(amplitude, 1e-30));
}

// Metadata uses quoted state names. Accept doubled quotes and CRLF, and reject
// malformed quoting rather than silently matching the wrong capture to a patch.
std::vector<std::string> CsvFields(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    bool quoted = false, closed = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else field += c;
        } else if (c == ',') {
            result.push_back(field);
            field.clear();
            closed = false;
        } else if (c == '"') {
            Require(field.empty() && !closed, "malformed CSV quote");
            quoted = true;
        } else if (c == '\r' && i + 1 == line.size()) {
            // Ignore the carriage return terminating a CRLF record.
        } else {
            Require(!closed, "characters after a closing CSV quote");
            field += c;
        }
    }
    Require(!quoted, "unterminated CSV quote");
    result.push_back(field);
    return result;
}

std::string Quoted(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        result += c;
        if (c == '"') result += c;
    }
    return result + '"';
}

struct Capture {
    int rate = 0;
    int state = 0;
    fs::path directory;
    std::vector<float> samples;
    double samplePeak = 0.;
};

using Bank = std::array<Capture, maremba::kFactoryPresets.size()>;

Bank ReadBank(const fs::path& directory) {
    std::ifstream metadata(directory / "captures.csv");
    Require(static_cast<bool>(metadata), "cannot read " + (directory / "captures.csv").string());
    std::string line;
    Require(static_cast<bool>(std::getline(metadata, line)), "empty capture metadata");
    const auto header = CsvFields(line);
    std::map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.size(); ++i)
        Require(columns.emplace(header[i], i).second, "duplicate capture metadata column");
    constexpr std::array<const char*, 10> required {{"state", "model", "scenario", "sample_rate", "volume",
        "main_rms_dbfs", "main_peak_dbfs", "capture_file", "frames", "channels"}};
    for (const auto* column : required)
        Require(columns.count(column) != 0, std::string("missing capture column ") + column);
    Require(columns.count("sample_format") != 0, "missing sample_format column");
    Bank bank;
    std::array<bool, maremba::kFactoryPresets.size() + kFactoryOffset> seen {};
    int bankRate = 0;
    while (std::getline(metadata, line)) {
        if (line.empty() || line == "\r") continue;
        const auto fields = CsvFields(line);
        Require(fields.size() == header.size(), "wrong number of capture metadata fields");
        const auto& value = [&](const char* column) -> const std::string& { return fields[columns.at(column)]; };
        const auto filename = fs::path(value("capture_file"));
        const auto name = filename.string();
        Require(filename == filename.filename() && name.rfind("state-", 0) == 0 &&
                name.size() > 10 && name.substr(name.size() - 4) == ".f32", "invalid capture filename");
        const auto stateNumber = Number(name.substr(6, name.size() - 10), "capture state");
        Require(stateNumber == std::floor(stateNumber) && stateNumber >= 0 &&
                stateNumber < static_cast<double>(seen.size()), "capture state outside the 24-state bank");
        const int state = static_cast<int>(stateNumber);
        Require(!seen[state], "duplicate capture state");
        seen[state] = true;
        const auto rateNumber = Number(value("sample_rate"), "sample rate");
        Require(std::find(kRates.begin(), kRates.end(), rateNumber) != kRates.end(), "expected 44.1, 48 or 96 kHz");
        const int rate = static_cast<int>(rateNumber);
        if (!bankRate) bankRate = rate;
        Require(rate == bankRate, "mixed sample rates in one capture directory");
        Require(value("scenario") == "phrase_v80_linear", "capture is not the fixed-80 Linear phrase");
        Require(std::abs(Number(value("volume"), "quiet Volume") - kQuietVolume) < 1e-9,
                "capture Volume is not .2125");
        Require(Number(value("frames"), "frames") == std::lround(rate * kSeconds) &&
                Number(value("channels"), "channels") == 2 && value("sample_format") == "float32_native",
                "expected five-second native float32 stereo capture");
        const auto expectedName = state < kFactoryOffset ? "Init model " + std::to_string(state)
            : std::string(maremba::kFactoryPresets[state - kFactoryOffset].name);
        const int expectedModel = state < kFactoryOffset ? state
            : static_cast<int>(maremba::kFactoryPresets[state - kFactoryOffset].values[maremba::kModel]);
        Require(value("state") == expectedName && Number(value("model"), "model") == expectedModel,
                "capture metadata does not match the compiled factory bank");
        if (state < kFactoryOffset) continue; // Model gains are calibrated separately from init peaks.

        auto& capture = bank[state - kFactoryOffset];
        capture.rate = rate;
        capture.state = state;
        capture.directory = directory;
        const std::size_t count = static_cast<std::size_t>(std::lround(rate * kSeconds)) * 2;
        std::ifstream input(directory / filename, std::ios::binary | std::ios::ate);
        Require(input && input.tellg() == static_cast<std::streamoff>(count * sizeof(float)),
                "unexpected capture size: " + (directory / filename).string());
        capture.samples.resize(count);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(capture.samples.data()), static_cast<std::streamsize>(count * sizeof(float)));
        Require(static_cast<bool>(input), "cannot read complete capture");
        double energy = 0., peak = 0.;
        for (float sample : capture.samples) {
            Require(std::isfinite(sample), "nonfinite capture sample");
            energy += static_cast<double>(sample) * sample;
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
        Require(peak > 1e-12 && energy > 1e-18, "silent capture");
        capture.samplePeak = peak;
        Require(peak < maremba::kMainPeakCeiling / maremba::output_peak::kMaximumL1,
                "quiet capture is not safely below the guard detector threshold");
        Require(std::abs(Db(peak) - Number(value("main_peak_dbfs"), "capture peak")) < 1e-6 &&
                std::abs(Db(std::sqrt(energy / count)) - Number(value("main_rms_dbfs"), "capture RMS")) < 1e-6,
                "capture samples do not match metadata levels");
    }
    Require(std::all_of(seen.begin(), seen.end(), [](bool present) { return present; }),
            "capture directory does not contain all 24 state records");
    return bank;
}

struct Measurement {
    double rms = 0., peak = 0., maximumReduction = 0., energyLoss = 0., weightedReduction = 0.;
    double overOneTotalMs = 0., overOneLongestMs = 0.;
};

float Scaled(const Capture& capture, std::size_t frame, int channel, double gain) {
    return static_cast<float>(capture.samples[frame * 2 + channel] * gain);
}

Measurement Measure(const Capture& capture, float volume) {
    maremba::OutputHeadroom guard;
    guard.SetSampleRate(capture.rate);
    const int delay = maremba::OutputHeadroom::LatencyFrames(capture.rate);
    const double gain = static_cast<double>(maremba::MainVolumeGain(volume)) / kQuietGain;
    const std::size_t frames = capture.samples.size() / 2;
    Measurement result;
    double outputEnergy = 0., delayedEnergy = 0., weightedReduction = 0.;
    std::size_t overOneFrames = 0, episode = 0, longest = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        std::array<float, 7> frame {};
        frame[0] = Scaled(capture, i, 0, gain);
        frame[1] = Scaled(capture, i, 1, gain);
        guard.Process(frame, maremba::kMainPeakCeiling);
        std::array<double, 2> source {};
        if (i >= static_cast<std::size_t>(delay)) {
            source[0] = Scaled(capture, i - delay, 0, gain);
            source[1] = Scaled(capture, i - delay, 1, gain);
        }
        const int dominant = std::abs(source[1]) > std::abs(source[0]) ? 1 : 0;
        const double inputPeak = std::abs(source[dominant]);
        const double reduction = inputPeak > 1e-20
            ? std::max(0., Db(inputPeak / std::max(std::abs(static_cast<double>(frame[dominant])), 1e-30))) : 0.;
        result.maximumReduction = std::max(result.maximumReduction, reduction);
        const double sourceEnergy = source[0] * source[0] + source[1] * source[1];
        delayedEnergy += sourceEnergy;
        weightedReduction += sourceEnergy * reduction;
        for (int channel = 0; channel < 2; ++channel) {
            Require(std::isfinite(frame[channel]), "nonfinite protector output");
            outputEnergy += static_cast<double>(frame[channel]) * frame[channel];
            result.peak = std::max(result.peak, std::abs(static_cast<double>(frame[channel])));
        }
        if (reduction > 1.) {
            ++overOneFrames;
            longest = std::max(longest, ++episode);
        } else episode = 0;
    }
    Require(outputEnergy > 0. && delayedEnergy > 0., "silent replay");
    Require(result.peak <= maremba::kMainPeakCeiling + 2e-6, "replay escaped the Main ceiling");
    result.rms = std::sqrt(outputEnergy / capture.samples.size());
    result.energyLoss = std::max(0., 10. * std::log10(delayedEnergy / outputEnergy));
    result.weightedReduction = weightedReduction / delayedEnergy;
    result.overOneTotalMs = overOneFrames * 1000. / capture.rate;
    result.overOneLongestMs = longest * 1000. / capture.rate;
    return result;
}

// Search only needs to know whether the crossing occurred. Passing candidates
// may stop on their first qualifying sample; full metrics are always replayed
// independently after the search. No alternate detector or gain law is used.
bool Reaches(const Capture& capture, float volume, double target) {
    maremba::OutputHeadroom guard;
    guard.SetSampleRate(capture.rate);
    const double gain = static_cast<double>(maremba::MainVolumeGain(volume)) / kQuietGain;
    // Guard gain never exceeds unity. This exact float-rounded sample bound
    // rejects impossible low-VCA grid points without running the detector.
    if (std::abs(static_cast<float>(capture.samplePeak * gain)) < target) return false;
    for (std::size_t i = 0; i < capture.samples.size() / 2; ++i) {
        std::array<float, 7> frame {};
        frame[0] = Scaled(capture, i, 0, gain);
        frame[1] = Scaled(capture, i, 1, gain);
        guard.Process(frame, maremba::kMainPeakCeiling);
        if (std::max(std::abs(static_cast<double>(frame[0])), std::abs(static_cast<double>(frame[1]))) >= target)
            return true;
    }
    return false;
}

void PrintMetrics(const Measurement& result) {
    std::cout << ',' << Db(result.rms) << ',' << Db(result.peak) << ',' << result.maximumReduction
              << ',' << result.energyLoss << ',' << result.weightedReduction
              << ',' << result.overOneTotalMs << ',' << result.overOneLongestMs;
}

} // namespace

int main(int argc, char** argv) {
    try {
        double targetDb = maremba::kMainReferencePeakDb;
        bool targetSeen = false;
        std::vector<fs::path> directories;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--target") {
                Require(!targetSeen && i + 1 < argc, "--target requires one numeric argument");
                targetDb = Number(argv[++i], "target peak");
                targetSeen = true;
            } else if (argument.rfind("--", 0) == 0) {
                throw std::runtime_error("unknown option: " + argument);
            } else directories.emplace_back(argument);
        }
        Require(directories.size() == 3,
                "usage: CalibratePresetVca [--target DBFS=-7.0] DIR_44100 DIR_48000 DIR_96000");
        Require(targetDb >= -60. && targetDb <= Db(maremba::kMainPeakCeiling) - .001,
                "target peak must be finite and between -60 and "
                + std::to_string(Db(maremba::kMainPeakCeiling) - .001) + " dBFS");
        Require(maremba::MainVolumeGain(static_cast<float>(kQuietVolume)) == kQuietGain,
                "quiet capture gain no longer matches the production Volume law");
        std::array<Bank, 3> banks {{ReadBank(directories[0]), ReadBank(directories[1]), ReadBank(directories[2])}};
        std::sort(banks.begin(), banks.end(), [](const Bank& a, const Bank& b) { return a[0].rate < b[0].rate; });
        for (std::size_t i = 0; i < banks.size(); ++i)
            Require(banks[i][0].rate == kRates[i], "supply exactly one capture directory for each required rate");
        const double target = std::pow(10., targetDb / 20.);
        std::cout << std::setprecision(12)
            << "state_index,preset,model,sample_rate,scenario,target_peak_dbfs,old_vca,new_vca,status,"
               "coarse_grid_steps,coarse_points_tested,search_iterations,search_bracket_vca,replay_additional_delay_frames,"
               "before_rms_dbfs,before_peak_dbfs,before_max_gr_db,before_energy_loss_db,before_energy_weighted_gr_db,"
               "before_over_1db_total_ms,before_over_1db_longest_ms,"
               "after_rms_dbfs,after_peak_dbfs,after_max_gr_db,after_energy_loss_db,after_energy_weighted_gr_db,"
               "after_over_1db_total_ms,after_over_1db_longest_ms,"
               "full_vca_rms_dbfs,full_vca_peak_dbfs,full_vca_max_gr_db,full_vca_energy_loss_db,"
               "full_vca_energy_weighted_gr_db,full_vca_over_1db_total_ms,full_vca_over_1db_longest_ms,"
               "full_vca_shortfall_db,capture_directory\n";
        int exceptions = 0;
        for (std::size_t preset = 0; preset < maremba::kFactoryPresets.size(); ++preset) {
            const auto& authored = maremba::kFactoryPresets[preset];
            Require(authored.values[maremba::kCompAmount] <= .05,
                    "factory musical Limit is not bypassed; the VCA replay would be invalid");
            const float oldVolume = maremba::ControlUnit(authored.values, maremba::kVolume);
            std::array<Measurement, 3> before, full, after;
            double minimumFullPeak = std::numeric_limits<double>::infinity();
            std::array<std::size_t, 3> order {{0, 1, 2}};
            for (std::size_t rate = 0; rate < banks.size(); ++rate) {
                before[rate] = Measure(banks[rate][preset], oldVolume);
                full[rate] = Measure(banks[rate][preset], 1.f);
                minimumFullPeak = std::min(minimumFullPeak, full[rate].peak);
            }
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return full[a].peak < full[b].peak;
            });
            float low = 0.f, high = 1.f;
            const auto reachesAllRates = [&](float volume) {
                for (std::size_t rate : order)
                    if (!Reaches(banks[rate][preset], volume, target)) return false;
                return true;
            };
            bool reachable = false;
            int coarsePoints = 0;
            for (int step = 1; step <= kGridSteps; ++step) {
                ++coarsePoints;
                const float candidate = static_cast<float>(step) / kGridSteps;
                const bool reaches = step == kGridSteps ? minimumFullPeak >= target : reachesAllRates(candidate);
                if (reaches) {
                    high = candidate;
                    reachable = true;
                    break;
                }
                low = candidate;
            }
            int iterations = 0;
            if (reachable) {
                for (; iterations < kSearchIterations; ++iterations) {
                    const float middle = static_cast<float>((static_cast<double>(low) + high) * .5);
                    if (middle <= low || middle >= high) break;
                    if (reachesAllRates(middle)) high = middle;
                    else low = middle;
                }
            } else ++exceptions;
            for (std::size_t rate = 0; rate < banks.size(); ++rate) {
                after[rate] = high == 1.f ? full[rate] : Measure(banks[rate][preset], high);
                Require(!reachable || after[rate].peak >= target, "first-crossing replay did not converge");
                const auto& capture = banks[rate][preset];
                std::cout << capture.state << ',' << Quoted(authored.name) << ','
                          << static_cast<int>(authored.values[maremba::kModel]) << ',' << capture.rate
                          << ",phrase_v80_linear," << targetDb << ',' << authored.values[maremba::kVolume] << ',' << high << ','
                          << (reachable ? "first_sampled_crossing" : "exception_vca_at_maximum") << ','
                          << kGridSteps << ',' << coarsePoints << ',' << iterations << ','
                          << (reachable ? static_cast<double>(high) - low : 0.) << ','
                          << maremba::OutputHeadroom::LatencyFrames(capture.rate);
                PrintMetrics(before[rate]);
                PrintMetrics(after[rate]);
                PrintMetrics(full[rate]);
                std::cout << ',' << std::max(0., targetDb - Db(full[rate].peak)) << ','
                          << Quoted(capture.directory.string()) << '\n';
            }
            std::cout.flush();
            std::cerr << (reachable ? "PROPOSE " : "EXCEPTION ") << authored.name << ": VCA "
                      << std::setprecision(9) << oldVolume << " -> " << high
                      << "; weakest full-VCA peak " << Db(minimumFullPeak) << " dBFS\n";
        }
        std::cerr << "CalibratePresetVca: 20 factory proposals, 60 rate rows, " << exceptions
                  << " full-VCA exceptions; four initialized-model gains unchanged. Search scope: first passing 1/32-grid point, "
                     "then local binary refinement; no global monotonicity claim. Independently render proposals before applying.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CalibratePresetVca: " << error.what() << '\n';
        return 1;
    }
}
