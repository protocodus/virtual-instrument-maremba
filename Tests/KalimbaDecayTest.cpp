// Kalimba sustain has a useful short range instead of the bars' long extension.
// Read the actual resonator poles, independently of the control-mapping helper:
// this catches applying the taper at the wrong layer or bypassing it with VOL.
#include "MarembaEngine.h"
#include "DecayControl.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <cstdint>
#include <cstring>

namespace maremba {
struct MarembaVoiceTestAccess {
    static int Modes(const MarembaVoice& voice) { return voice.mNumModes; }
    static double T60(const MarembaVoice& voice, int mode = 0) {
        return -6.907755 / (voice.mSampleRate * std::log(voice.mModes[mode].r));
    }
    static double FundamentalSample(const MarembaVoice& voice) { return voice.mModes[0].y1; }
    static bool HasBrush(const MarembaVoice& voice) { return voice.mBrushImpulse > 0; }
    static double BrushT60(const MarembaVoice& voice, int mode) {
        return -6.907755 / (voice.mSampleRate * std::log(voice.mBrushModes[mode].r));
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

struct TailMeasurement {
    std::array<double, 3> modalT60{};
    double renderedT60 = 0;
    std::uint64_t audioHash = UINT64_C(14695981039346656037);
};

TailMeasurement measureTail(int model, int note, double rate) {
    MarembaVoice voice;
    FastPRNG random(0x61ca11u);
    voice.SetSeed(0x3e192u);
    voice.Trigger(note, .8f, static_cast<EMarimbaModel>(model), .4f, .5f,
                  0, .7f, DecayFromControl(.5f), 0, 0, 0, rate, random, 0, 0, 1, 0);
    TailMeasurement result;
    require(VoiceAccess::Modes(voice) >= 3, "tail fixture needs three physical modes");
    for (int mode = 0; mode < 3; ++mode) result.modalT60[mode] = VoiceAccess::T60(voice, mode);
    // Fit an actual freely evolving fundamental's RMS, not its configured
    // radius. The first 200 ms excludes thumb/mallet contact. No audio is kept.
    constexpr double windowSeconds = .05;
    const int windowFrames = static_cast<int>(std::lround(windowSeconds * rate));
    const int windows = 44;
    double sumT = 0, sumY = 0, sumTT = 0, sumTY = 0;
    int fitCount = 0;
    for (int window = 0; window < windows; ++window) {
        double energy = 0;
        for (int frame = 0; frame < windowFrames; ++frame) {
            float close = 0, far = 0, piezo = 0;
            voice.ProcessSample(close, far, piezo);
            require(std::isfinite(close) && std::isfinite(far) && std::isfinite(piezo),
                    "tail probe produced nonfinite audio");
            const double sample = VoiceAccess::FundamentalSample(voice);
            energy += sample * sample;
            for (float value : {close, far, piezo, voice.GetBridgeForce()}) {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                for (int byte = 0; byte < 4; ++byte) {
                    result.audioHash ^= (bits >> (8 * byte)) & 255;
                    result.audioHash *= UINT64_C(1099511628211);
                }
            }
        }
        if (window < 4) continue;
        require(energy > 0 && std::isfinite(energy), "tail probe lost its physical fundamental");
        const double time = (window + .5) * windowFrames / rate;
        const double logRms = .5 * std::log(energy / windowFrames);
        sumT += time; sumY += logRms; sumTT += time * time; sumTY += time * logRms;
        ++fitCount;
    }
    const double slope = (fitCount * sumTY - sumT * sumY)
                       / (fitCount * sumTT - sumT * sumT);
    require(slope < 0, "tail probe has no decaying fundamental");
    result.renderedT60 = -std::log(1000.) / slope;
    return result;
}

void heldBodyDamping() {
    // Frozen from the unmodified physical voice at commit 26debb941face41a,
    // with the fixed-seed middle-Decay fixture above. The bars' partials above
    // about 2 kHz were re-frozen in 1.5.8, when wood's rising loss levelled off
    // there (LossFactor); the fundamentals and every rendered value are as
    // frozen. Modal data covers three
    // partials; rendered data comes from fitting actual free-motion samples.
    struct Before { double modal[3]; double rendered; };
    constexpr Before before[4][3] {
        {
            {{2.17820204922239, .482146705839344, .141601595067597}, 1.58546530337724},
            {{1.31305643499282, .231957268988989, .0952913711876526}, 1.07953469783532},
            {{.512468170215102, .124200906765345, .0643348012183555}, .497574432125066},
        },
        {
            {{1.74943881707308, .430665840788638, .153789954892504}, 1.0517091051653},
            {{.983795479530495, .213486786285099, .100137038370138}, .602185403944601},
            {{.378854480007922, .109056152038101, .0578995313776798}, .316253819436722},
        },
        {
            {{1.24486854193209, .442782076452662, .191373726825125}, 1.14441355156171},
            {{.747553158222781, .194102709916413, .0992619785900664}, .66135758012706},
            {{.289131549390259, .0928894246026861, .0500136670471184}, .267022494680073},
        },
        {
            {{5.26881134803693, .676474599728351, .248355483530231}, 5.26889478758063},
            {{2.69005445134239, .434402371573943, .216445451116375}, 2.69005455998566},
            {{.64828918279109, .107013978706266, .117922789964677}, .648296612711026},
        },
    };
    constexpr int notes[] {60, 69, 84};
    for (int model = 0; model < 4; ++model)
        for (int key = 0; key < 3; ++key)
            for (double rate : {44100., 48000., 96000.}) {
                const auto tail = measureTail(model, notes[key], rate);
                const double expected = model == 3 ? .7 : 1.;
                const double renderedRatio = tail.renderedT60 / before[model][key].rendered;
                std::cout << "HELD_BODY model=" << model << " note=" << notes[key]
                          << " rate=" << rate << " rendered_t60_seconds=" << tail.renderedT60
                          << " before_ratio=" << renderedRatio << '\n';
                require(std::abs(renderedRatio - expected) < .0002,
                        "middle-Decay rendered tail must shorten only Kalimba to 70% of its prior lifetime");
                for (int mode = 0; mode < 3; ++mode)
                    require(std::abs(tail.modalT60[mode] / before[model][key].modal[mode] - expected) < 1e-7,
                            "held-body modal damping changed another model or missed a Kalimba partial");
            }
}

void reportTails() {
    std::cout << "model,note,rate,modal_t60_seconds,rendered_t60_seconds,audio_hash,mode1_t60_seconds,mode2_t60_seconds\n";
    for (int model = 0; model < 4; ++model)
        for (int note : {60, 69, 84})
            for (double rate : {44100., 48000., 96000.}) {
                const auto tail = measureTail(model, note, rate);
                std::cout << model << ',' << note << ',' << rate << ',' << tail.modalT60[0]
                          << ',' << tail.renderedT60 << ',' << tail.audioHash << ','
                          << tail.modalT60[1] << ',' << tail.modalT60[2] << '\n';
            }
}

void reportBrushes() {
    std::cout << "model,note,seed,brush0_t60_seconds,brush1_t60_seconds\n";
    for (int model = 0; model < 4; ++model)
        for (int note : {60, 69, 84}) {
            bool found = false;
            for (unsigned seed = 1; seed <= 64 && !found; ++seed) {
                MarembaVoice voice;
                FastPRNG random(seed);
                voice.Trigger(note, .8f, static_cast<EMarimbaModel>(model), .4f, .5f,
                              0, .7f, 1, 0, 1, 0, 48000, random, 0, 0, 1, 0);
                if (!VoiceAccess::HasBrush(voice)) continue;
                found = true;
                std::cout << model << ',' << note << ',' << seed << ','
                          << VoiceAccess::BrushT60(voice, 0) << ',' << VoiceAccess::BrushT60(voice, 1) << '\n';
            }
            require(found, "brush fixture failed to activate the neighboring-key path");
        }
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::setprecision(15);
    if (argc == 2 && std::strcmp(argv[1], "--report-hand-damping") == 0) {
        reportTails();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--report-brush-damping") == 0) {
        reportBrushes();
        return 0;
    }
    heldBodyDamping();
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
                const float decay = DecayFromControl(static_cast<float>(step) / 16);
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
                    require(std::abs(ratio - std::sqrt(.1)) < 1e-6,
                            "quarter-travel Kalimba decay must be the geometric midpoint of the short range");
                if (step == 8)
                    require(std::abs(ratio - 1) < 1e-6,
                            "half-travel Kalimba decay must match the natural instrument");
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
            // The lower half retains its original relative damping taper.
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
                        require(ratio < 1.12, "10% Decay VOL makes Kalimba sustain grow too early");
                        maximumFactoryDepthRatio = std::max(maximumFactoryDepthRatio, ratio);
                        if (velocity == 1)
                            require(ratio > 1.07, "10% Decay VOL no longer extends Kalimba sustain");
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
