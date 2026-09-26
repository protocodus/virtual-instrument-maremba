// Kalimba sustain has a useful short range instead of the bars' long extension.
// Read the actual resonator poles, independently of the control-mapping helper:
// this catches applying the taper at the wrong layer or bypassing it with VOL.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>

namespace maremba {
struct MarembaVoiceTestAccess {
    static int Modes(const MarembaVoice& voice) { return voice.mNumModes; }
    static double T60(const MarembaVoice& voice, int mode = 0) {
        return -6.907755 / (voice.mSampleRate * std::log(voice.mModes[mode].r));
    }
};
struct MarembaEngineTestAccess {
    static const MarembaVoice* ActiveVoice(const MarembaEngine& engine) {
        for (const auto& voice : engine.mVoices)
            if (voice.IsActive()) return &voice;
        return nullptr;
    }
};
} // namespace maremba

namespace {
using namespace maremba;
using VoiceAccess = MarembaVoiceTestAccess;
int checks = 0;
int modalCases = 0;
int routeCases = 0;
double maximumSustainRatio = 0;
double maximumFactoryDepthRatio = 0;

void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

void strike(MarembaVoice& voice, double rate, int note, float decay) {
    FastPRNG random(0x61ca11u);
    voice.FastKill();
    voice.Trigger(note, .8f, EMarimbaModel::KalimbaArtisan, .4f, .5f,
                  0, .7f, decay, 0, 0, 0, rate, random, 0, 0, 1, 0);
    require(VoiceAccess::Modes(voice) > 0, "Kalimba fixture has no pitched modes");
}

EngineParameters cleanParameters() {
    EngineParameters p;
    p.model = static_cast<int>(EMarimbaModel::KalimbaArtisan);
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.bodyBloom = p.sympathetic = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    return p;
}

double engineT60(MarembaEngine& engine, const EngineParameters& params,
                 int note, float velocity) {
    engine.Reset();
    engine.SetParameters(params);
    engine.NoteOn(note, velocity);
    const auto* voice = MarembaEngineTestAccess::ActiveVoice(engine);
    require(voice != nullptr, "engine did not start the Kalimba note");
    const double t60 = VoiceAccess::T60(*voice);
    require(std::isfinite(t60) && t60 > 0, "engine produced invalid Kalimba decay");
    return t60;
}
} // namespace

int main() {
    std::cout << std::setprecision(9);
    // Default 44.1 kHz/2x and maximum supported internal rates. Every MIDI
    // note must retain a useful monotonic range, including subaudio extrapolation.
    std::array<double, 128> reference{};
    for (double rate : {88200., 1536000.}) {
        for (int note = 0; note < 128; ++note) {
            MarembaVoice voice;
            strike(voice, rate, note, 1);
            const int modes = VoiceAccess::Modes(voice);
            std::array<double, kNumPitchedModes> natural{};
            for (int mode = 0; mode < modes; ++mode)
                natural[mode] = VoiceAccess::T60(voice, mode);
            require(std::isfinite(natural[0]) && natural[0] > 0,
                    "natural Kalimba decay is invalid");
            if (rate == 88200.) reference[note] = natural[0];
            else require(std::abs(natural[0] / reference[note] - 1) < 1e-6,
                         "natural Kalimba sustain changes with internal rate");

            double previous = 0;
            for (int step = 0; step <= 16; ++step) {
                const float decay = .1f + 7.9f * (static_cast<float>(step) / 16);
                strike(voice, rate, note, decay);
                const double t60 = VoiceAccess::T60(voice);
                const double ratio = t60 / natural[0];
                require(t60 > previous * 1.01,
                        "Kalimba decay contains a plateau or reverses across the knob");
                require(ratio <= 2.000001, "Kalimba sustain exceeds twice natural decay");
                if (step == 0)
                    require(std::abs(ratio - .1) < 1e-6,
                            "shortest Kalimba sustain changed from one tenth natural");
                if (step == 4)
                    require(ratio > 1.1 && ratio < 1.2,
                            "quarter-travel Kalimba decay grows too early or lost its range");
                if (step == 8)
                    require(ratio > 1.4 && ratio < 1.5,
                            "half-travel Kalimba decay grows too early or lost its range");
                if (step == 16) {
                    for (int mode = 0; mode < modes; ++mode)
                        require(std::abs(VoiceAccess::T60(voice, mode) / natural[mode] - 2) < 1e-6,
                                "maximum Kalimba sustain does not double each mode's natural decay");
                    if (rate == 88200. && (note == 48 || note == 60 || note == 84))
                        std::cout << "MODAL note=" << note << " natural_t60_seconds=" << natural[0]
                                  << " maximum_t60_seconds=" << t60 << '\n';
                }
                maximumSustainRatio = std::max(maximumSustainRatio, ratio);
                previous = t60;
                ++modalCases;
            }
            // The damping half of the physical voicing remains untouched.
            strike(voice, rate, note, .4f);
            require(std::abs(VoiceAccess::T60(voice) / natural[0] - .4) < 1e-6,
                    "sub-unity Kalimba decay no longer preserves the short physical voicing");
        }
    }

    auto engine = std::make_unique<MarembaEngine>(48000.);
    for (int note : {0, 24, 48, 60, 84, 108, 127}) {
        auto params = cleanParameters();
        const double natural = engineT60(*engine, params, note, 1);
        for (int curve = 0; curve < 4; ++curve) {
            params.velocityCurve = curve;
            for (float depth : {0.f, .1f, .5f, 1.f}) {
                params.velocityToDecay = depth;
                for (float velocity : {1.f / 127.f, 64.f / 127.f, 1.f}) {
                    const double ratio = engineT60(*engine, params, note, velocity) / natural;
                    require(ratio >= .999999 && ratio <= 2.000001,
                            "Decay VOL bypasses the shortened Kalimba sustain range");
                    if (depth == 0 || velocity == 1.f / 127.f)
                        require(std::abs(ratio - 1) < 1e-6,
                                "zero depth or minimum MIDI velocity changed natural Kalimba decay");
                    if (depth == .1f) {
                        require(ratio < 1.12, "factory Decay VOL makes Kalimba sustain grow too early");
                        maximumFactoryDepthRatio = std::max(maximumFactoryDepthRatio, ratio);
                        if (velocity == 1)
                            require(ratio > 1.1, "factory Decay VOL no longer extends Kalimba sustain");
                    }
                    if (depth == 1 && velocity == 1)
                        require(std::abs(ratio - 2) < 1e-6,
                                "full Decay VOL does not reach the shortened Kalimba maximum");
                    ++routeCases;
                }
            }
            // With the base already at maximum, any depth/velocity must keep
            // exactly that maximum instead of extending or compressing it again.
            params.decay = 8;
            params.velocityToDecay = 1;
            require(std::abs(engineT60(*engine, params, note, 1) / natural - 2) < 1e-6,
                    "maximum base plus full VOL escaped the Kalimba maximum");
            params.decay = 1;
            ++routeCases;
        }
    }
    std::cout << "KalimbaDecayTest passed: " << checks << " checks, " << modalCases
              << " modal cases, " << routeCases << " velocity cases; maximum sustain ratio="
              << maximumSustainRatio << ", maximum 10% VOL ratio=" << maximumFactoryDepthRatio << '\n';
}
