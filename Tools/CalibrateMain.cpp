// Historical equal-RMS model-gain protocol. Current factory VCA/peak balance
// uses CalibratePresetVca; do not apply these 24-state RMS gains as preset trims.
// Offline fixed-gain solver. First capture the unprotected, velocity-80 Linear
// reference with MainCalibrationTest --quiet --capture DIR. Each state-N.f32
// contains five seconds of native float32 stereo at 48 kHz, Volume .2125.
// Replaying through the production lookahead at each trial gain includes its
// envelope reduction in the RMS solution. No RMS controller enters the engine.
// Usage: CalibrateMain CAPTURE_DIR [target_rms_dbfs=-27]
#include "MainCalibration.h"
#include "OutputHeadroom.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr double rate = 48000.;
struct Measurement { double rms = 0, peak = 0, reduction = 0, loss = 0; };
double Db(double x) { return 20 * std::log10(std::max(x, 1e-15)); }
Measurement Measure(const std::vector<float>& samples, double gain) {
    maremba::OutputHeadroom guard;
    guard.SetSampleRate(rate);
    Measurement result;
    double energy = 0, sourceEnergy = 0;
    const int delay = guard.LatencyFrames(rate);
    const int frames = static_cast<int>(samples.size() / 2);
    for (int i = 0; i < frames; ++i) {
        std::array<float, 7> frame {};
        for (int channel = 0; channel < 2; ++channel)
            frame[channel] = static_cast<float>(samples[2*i + channel] * gain);
        guard.Process(frame, maremba::kMainPeakCeiling);
        for (int channel = 0; channel < 2; ++channel) {
            const double x = frame[channel];
            energy += x*x;
            result.peak = std::max(result.peak, std::abs(x));
            const double source = i < delay ? 0 : samples[2*(i-delay) + channel] * gain;
            sourceEnergy += source*source;
            if (std::abs(source) > 1e-6)
                result.reduction = std::max(result.reduction, Db(std::abs(source) / std::max(std::abs(x), 1e-30)));
        }
    }
    result.rms = std::sqrt(energy / samples.size());
    result.loss = 10 * std::log10(sourceEnergy / energy);
    return result;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("usage: CalibrateMain CAPTURE_DIR [target_rms_dbfs]");
        const double targetDb = argc == 3 ? std::stod(argv[2]) : maremba::kMainReferenceRmsDb;
        if (!std::isfinite(targetDb) || targetDb < -60 || targetDb > -12)
            throw std::runtime_error("target RMS must be finite, -60..-12 dBFS");
        std::cout << std::setprecision(12) << "state_index,gain,rms_dbfs,peak_dbfs,max_reduction_db,energy_loss_db\n";
        for (int state = 0; state < 24; ++state) {
            const auto path = std::filesystem::path(argv[1]) / ("state-" + std::to_string(state) + ".f32");
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() != 240000 * 2 * sizeof(float))
                throw std::runtime_error("expected five-second stereo float capture: " + path.string());
            std::vector<float> samples(240000 * 2);
            input.seekg(0); input.read(reinterpret_cast<char*>(samples.data()), samples.size() * sizeof(float));
            for (float sample : samples) if (!std::isfinite(sample)) throw std::runtime_error("nonfinite capture");
            double low = -10, high = 70;
            for (int iteration = 0; iteration < 18; ++iteration) {
                const double middle = (low + high) * .5;
                if (Db(Measure(samples, std::pow(10., middle / 20)).rms) < targetDb) low = middle;
                else high = middle;
            }
            const double gain = std::pow(10., (low + high) / 40);
            const auto final = Measure(samples, gain);
            if (std::abs(Db(final.rms) - targetDb) > .01) throw std::runtime_error("RMS solution did not converge");
            std::cout << state << ',' << gain << ',' << Db(final.rms) << ',' << Db(final.peak)
                      << ',' << final.reduction << ',' << final.loss << std::endl;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
