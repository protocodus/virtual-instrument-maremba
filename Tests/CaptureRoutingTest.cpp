// Capture coefficients belong after each complete capture. In particular the
// Room coefficient must not change the wet/dry balance or drain the room, and
// Piezo Drive must not color either air microphone.
#include "MarembaEngine.h"
#include "CaptureScene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 7>;
int checks = 0, mixerCases = 0, driveCases = 0;
std::string context;

void require(bool okay, const char* message) {
    ++checks;
    if (!okay) {
        std::cerr << "FAIL " << context << ": " << message << '\n';
        std::exit(1);
    }
}

double energy(const std::vector<float>& signal, size_t start = 0) {
    double result = 0;
    for (size_t i = start; i < signal.size(); ++i)
        result += static_cast<double>(signal[i]) * signal[i];
    return result;
}

double residual(const std::vector<float>& a, const std::vector<float>& b) {
    double difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        difference += d * d;
    }
    return std::sqrt(difference / std::max(energy(a), 1e-30));
}

Audio mix(double rate, int roomType, const std::array<float, 3>& coefficient) {
    CaptureScene mixer;
    mixer.Allocate(rate);
    mixer.SetSampleRate(rate);
    mixer.SetRoomType(roomType);
    Audio result;
    const size_t frames = static_cast<size_t>(rate * .45);
    for (auto& channel : result) channel.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const double time = static_cast<double>(i) / rate;
        const float envelope = time < .025 ? static_cast<float>(1 - time / .025) : 0;
        const float a = envelope * (.17f * std::sin(kTwoPiD * 373 * time) + (i == 0 ? .2f : 0));
        const float b = envelope * (.12f * std::cos(kTwoPiD * 911 * time) - (i == 0 ? .1f : 0));
        mixer.BeginFrame(2);
        for (int sample = 0; sample < 2; ++sample) {
            mixer.Add(60, sample, a, a, .7f*b);
            mixer.Add(84, sample, b, b, .6f*a);
        }
        mixer.Process(result[2][i], result[3][i], result[4][i], result[5][i]);
        result[6][i] = .3f*(a-b);
        CaptureScene::Mix(result[2][i], result[3][i], result[4][i], result[5][i], result[6][i],
                          coefficient[0], coefficient[1], coefficient[2], 1.6f, result[0][i], result[1][i]);
    }
    return result;
}

void coefficientContracts() {
    for (const double rate : {44100., 48000., 96000.}) for (int room = 0; room < 3; ++room) {
        context = "coefficients room=" + std::to_string(room) + " rate=" + std::to_string(rate);
        const auto close = mix(rate, room, {1, 0, 0});
        const auto far = mix(rate, room, {0, 1, 0});
        const auto piezo = mix(rate, room, {0, 0, 1});
        const auto combined = mix(rate, room, {.25f, .5f, .75f});
        const auto muted = mix(rate, room, {0, 0, 0});
        for (int channel = 2; channel < 7; ++channel) {
            require(close[channel] == far[channel] && close[channel] == piezo[channel]
                        && close[channel] == combined[channel] && close[channel] == muted[channel],
                    "a coefficient modifies its direct capture");
        }
        for (int channel = 0; channel < 2; ++channel) {
            require(energy(muted[channel]) == 0, "all zero coefficients leave audible main output");
            double error = 0;
            for (size_t i = 0; i < combined[channel].size(); ++i) {
                const double expected = .25 * close[channel][i] + .5 * far[channel][i] + .75 * piezo[channel][i];
                const double d = combined[channel][i] - expected;
                error += d * d;
            }
            require(error < 1e-12 * energy(combined[channel]),
                    "combined main is not the independently weighted sum of complete captures");
        }
        const std::array<const Audio*, 3> full = {&close, &far, &piezo};
        for (int capture = 0; capture < 3; ++capture) {
            std::array<float, 3> coefficient{};
            coefficient[capture] = .5f;
            const auto half = mix(rate, room, coefficient);
            for (int channel = 0; channel < 2; ++channel) {
                double error = 0;
                for (size_t i = 0; i < half[channel].size(); ++i) {
                    const double d = half[channel][i] - .5 * (*full[capture])[channel][i];
                    error += d * d;
                }
                require(error < 1e-12 * energy((*full[capture])[channel]),
                        "a half coefficient changes capture timbre or is applied more than once");
            }
        }
        require(energy(muted[4], static_cast<size_t>(rate * .1)) > 1e-9,
                "muting Room destroys the direct room tail");
        ++mixerCases;
    }
}

void invalidSourceContracts() {
    context = "invalid physical source";
    CaptureScene scene;
    scene.Allocate(48000);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        for (int lane = 0; lane < 3; ++lane) {
            scene.BeginFrame(2);
            std::array<float,3> input{.01f,.02f,.03f};
            input[lane] = invalid;
            scene.Add(60, 0, input[0], input[1], input[2]);
            require(scene.HasInvalidInput(), "capture concealed a nonfinite acoustic emitter");
        }
    }
    scene.BeginFrame(2);
    const float huge = std::numeric_limits<float>::max();
    scene.Add(60, 0, huge, 0, 0);
    scene.Add(60, 0, huge, 0, 0);
    require(scene.HasInvalidInput(), "capture concealed emitter-sum overflow");
    scene.Reset();
    scene.BeginFrame(2);
    scene.Add(60, 0, .01f, .02f, .03f);
    require(!scene.HasInvalidInput(), "reset retained the invalid-source flag");
}

Audio strike(double rate, const EngineParameters& p) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(p);
    engine->NoteOn(60, 1);
    engine->NoteOn(67, 1);
    Audio result;
    const int frames = static_cast<int>(rate * .16);
    for (auto& channel : result) channel.resize(static_cast<size_t>(frames));
    for (int frame = 0; frame < frames; frame += 64) {
        const int count = std::min(64, frames - frame);
        engine->RenderBatch(result[0].data() + frame, result[1].data() + frame,
                            result[2].data() + frame, result[3].data() + frame,
                            result[4].data() + frame, result[5].data() + frame,
                            result[6].data() + frame, count);
    }
    for (const auto& channel : result) for (const float value : channel)
        require(std::isfinite(value), "nonfinite capture output");
    return result;
}

void driveContracts() {
    struct Setup { double rate; int quality; };
    for (const Setup setup : {Setup{48000, 0}, Setup{44100, 1}, Setup{96000, 2}})
        for (int model = 0; model < 4; ++model) {
            context = "drive model=" + std::to_string(model) + " rate=" + std::to_string(setup.rate)
                    + " quality=" + std::to_string(setup.quality);
            EngineParameters p;
            p.model = model; p.oversampling = setup.quality; p.roomType = model % 3;
            p.preampDrive = p.compAmount = p.warmth = p.detune = p.strikeJitter = 0;
            p.closeLevel = .6f; p.farLevel = .4f; p.piezoLevel = 0;
            const auto airClean = strike(setup.rate, p);
            p.preampDrive = 1;
            const auto airDriven = strike(setup.rate, p);
            for (int channel = 0; channel < 6; ++channel)
                require(airClean[channel] == airDriven[channel],
                        "Drive changes an air microphone or the acoustic-only main mix");
            require(residual(airClean[6], airDriven[6]) > 1e-4,
                    "Drive does not reach the direct Piezo when its main coefficient is zero");

            p.closeLevel = p.farLevel = 0; p.piezoLevel = 1; p.preampDrive = 0;
            const auto contactClean = strike(setup.rate, p);
            p.preampDrive = 1;
            const auto contactDriven = strike(setup.rate, p);
            for (int channel = 2; channel < 6; ++channel)
                require(contactClean[channel] == contactDriven[channel], "Piezo Drive leaks into an acoustic stem");
            require(contactClean[6] == airClean[6] && contactDriven[6] == airDriven[6],
                    "capture coefficient changes the pickup drive response");
            require(energy(contactClean[0]) > 1e-9 && residual(contactClean[0], contactDriven[0]) > 1e-4,
                    "Drive does not reach the Piezo-only main output");
            ++driveCases;
        }
}
} // namespace

int main() {
    invalidSourceContracts();
    coefficientContracts();
    driveContracts();
    std::cout << "CaptureRoutingTest PASSED checks=" << checks << " coefficient_cases=" << mixerCases
              << " drive_cases=" << driveCases << '\n';
}
