// Each exposed sound control must reach all four models. Exercise conditional
// controls in their active context; Buzz=0 remains completely dry at any VOL
// depth, and Tension must still work with that factory setting.
#include "MarembaEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static bool PassiveTension(MarembaVoice& voice) {
        // Turning a support control must not inject energy or retune a mode.
        const double energy = voice.ComputeEnergy();
        std::vector<double> pitch, loose;
        for (int m = 0; m < voice.mNumModes; ++m) pitch.push_back(voice.mModes[m].w);
        voice.SetBuzz(0, 0);
        for (int m = 0; m < voice.mNumModes; ++m) loose.push_back(voice.mModes[m].r);
        const bool conservedLoose = std::abs(voice.ComputeEnergy() - energy) < 1e-8 * energy;
        voice.SetBuzz(0, 1);
        for (int m = 0; m < voice.mNumModes; ++m)
            if (!(voice.mModes[m].r > 0 && voice.mModes[m].r < loose[m] && loose[m] < 1)
                || voice.mModes[m].w != pitch[m]) return false;
        return conservedLoose && std::abs(voice.ComputeEnergy() - energy) < 1e-8 * energy;
    }
};
struct MarembaEngineTestAccess {
    static bool PassiveTension(MarembaEngine& engine) {
        int tested = 0;
        for (auto& voice : engine.mVoices) if (voice.IsActive()) {
            if (!MarembaVoiceTestAccess::PassiveTension(voice)) return false;
            ++tested;
        }
        return tested > 0;
    }
};
} // namespace maremba

namespace {
using maremba::EngineParameters;
constexpr double rate = 48000;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}

std::vector<float> render(const EngineParameters& p, bool crowded = false,
                          const EngineParameters* live = nullptr, bool buzzerStroke = false) {
    auto engine = std::make_unique<maremba::MarembaEngine>(rate);
    engine->SetParameters(p);
    const int root = p.model == 3 ? (buzzerStroke ? 76 : 60) : 48;
    if (crowded) {
        for (int n = 0; n < 20; ++n) engine->NoteOn(root + n, .72f);
    } else {
        for (int n : {0, 4, 7, 11}) engine->NoteOn(root + n, buzzerStroke ? .85f : .62f);
    }
    std::vector<float> result;
    float l[64], r[64];
    for (int b = 0; b < 600; ++b) {
        if (live && b == 150) engine->SetParameters(*live);
        if (b == 250)
            for (int n = 0; n < 20; ++n) engine->NoteOff(root + n);
        engine->RenderBatch(l, r, nullptr, nullptr, nullptr, nullptr, nullptr, 64);
        for (int i = 0; i < 64; ++i) {
            check(std::isfinite(l[i]) && std::isfinite(r[i]), "nonfinite reachability render");
            result.push_back(l[i]); result.push_back(r[i]);
        }
    }
    return result;
}

double difference(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
    double residual = 0, energy = 0;
    for (size_t i = from; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        residual += d * d; energy += static_cast<double>(a[i]) * a[i];
    }
    return std::sqrt(residual / std::max(energy, 1e-30));
}

struct FloatControl {
    const char* name;
    float EngineParameters::*value;
    float low, high;
    int context = 0; // 1: buzz active; 2: limiter driven; 3: a velocity route
};
struct IntControl { const char* name; int EngineParameters::*value; int low, high; };
} // namespace

int main() {
    const FloatControl controls[] = {
        {"Hardness", &EngineParameters::malletHardness, .1f, .9f},
        {"Position", &EngineParameters::strikePosition, .1f, .9f},
        {"Resonator Tune", &EngineParameters::resonatorTune, -50, 50},
        {"Coupling", &EngineParameters::resonatorCoupling, .1f, 1},
        {"Decay", &EngineParameters::decay, .3f, 2},
        {"Buzz", &EngineParameters::buzzAmount, 0, 1},
        {"Artifacts", &EngineParameters::artifacts, 0, 1},
        {"Sympathetic", &EngineParameters::sympathetic, 0, 1},
        {"Bloom", &EngineParameters::bodyBloom, 0, 1},
        {"Close", &EngineParameters::closeLevel, 0, 1},
        {"Room", &EngineParameters::farLevel, 0, 1},
        {"Piezo", &EngineParameters::piezoLevel, 0, 1},
        {"Width", &EngineParameters::stereoWidth, 0, 2},
        {"Drive", &EngineParameters::preampDrive, 0, 1},
        {"Warmth", &EngineParameters::warmth, -1, 1},
        {"Limit", &EngineParameters::compAmount, 0, 1, 2},
        {"Attack", &EngineParameters::compAttack, 1, 50, 2},
        {"Release", &EngineParameters::compRelease, 20, 500, 2},
        {"Volume", &EngineParameters::volume, .3f, 1},
        {"Master Tune", &EngineParameters::tuneCents, -50, 50},
        {"Detune", &EngineParameters::detune, 0, 40},
        {"Variance", &EngineParameters::strikeJitter, 0, 100},
        {"Carving", &EngineParameters::carving, 0, 1},
        {"Tension (Buzz off)", &EngineParameters::membraneTension, 0, 1},
        {"Tension (Buzz on)", &EngineParameters::membraneTension, 0, 1, 1},
        {"Wheel", &EngineParameters::modWheel, 0, 1},
        {"Hardness VOL", &EngineParameters::velocityToHardness, 0, 1, 3},
        {"Position VOL", &EngineParameters::velocityToPosition, 0, 1, 3},
        {"Decay VOL", &EngineParameters::velocityToDecay, 0, 1, 3},
        {"Carving VOL", &EngineParameters::velocityToCarving, 0, 1, 3},
        {"Coupling VOL", &EngineParameters::velocityToCoupling, 0, 1, 3},
        {"Bloom VOL", &EngineParameters::velocityToBloom, 0, 1, 3},
        {"Sympathetic VOL", &EngineParameters::velocityToSympathetic, 0, 1, 3},
        {"Buzz VOL", &EngineParameters::velocityToBuzz, 0, 1, 1},
        {"Tension VOL", &EngineParameters::velocityToTension, 0, 1, 3},
        {"Artifacts VOL", &EngineParameters::velocityToArtifacts, 0, 1, 3},
        {"Variance VOL", &EngineParameters::velocityToVariance, 0, 1, 3},
    };
    const IntControl selectors[] = {
        {"Invert Piezo", &EngineParameters::invertPiezo, 0, 1},
        {"Striker", &EngineParameters::malletType, 0, 4},
        {"Room Space", &EngineParameters::roomType, 0, 2},
        {"Quality", &EngineParameters::oversampling, 0, 1},
        {"Velocity Response", &EngineParameters::velocityCurve, 0, 2},
        {"Tuning", &EngineParameters::tuningScale, 0, 3},
        {"Note Off", &EngineParameters::noteOffMode, 0, 1},
        {"Polyphony", &EngineParameters::polyphony, 0, 1},
    };
    for (int model = 0; model < 4; ++model) {
        EngineParameters base;
        base.model = model;
        base.buzzAmount = 0; // the new factory default must leave Tension useful
        base.velocityToHardness = base.velocityToPosition = base.velocityToDecay = 0;
        for (const auto& c : controls) {
            auto a = base;
            const bool buzzerStroke = c.context == 1 || c.value == &EngineParameters::buzzAmount;
            if (buzzerStroke && model == 3) {
                // Board buzzers need a firm treble nail/pick pluck to lift.
                a.malletType = 3; a.malletHardness = .9f;
            }
            if (c.context == 1) a.buzzAmount = c.value == &EngineParameters::velocityToBuzz ? .2f : 1.0f;
            if (c.context == 2) {
                a.preampDrive = a.compAmount = a.closeLevel = a.piezoLevel = a.farLevel = 1;
                a.compAttack = 1;
            }
            if (c.context == 3) a.artifacts = a.bodyBloom = a.sympathetic = a.carving = .2f;
            auto b = a;
            a.*c.value = c.low; b.*c.value = c.high;
            const double effect = difference(render(a, false, nullptr, buzzerStroke),
                                             render(b, false, nullptr, buzzerStroke));
            std::cout << "CONTROL model=" << model << " name=" << c.name << " relative_rms=" << effect << '\n';
            check(effect > 1e-4, "model=" + std::to_string(model) + " dead control " + c.name);
        }
        for (const auto& c : selectors) {
            auto a = base, b = base;
            a.*c.value = c.low; b.*c.value = c.high;
            const bool crowded = c.value == &EngineParameters::polyphony;
            const double effect = difference(render(a, crowded), render(b, crowded));
            std::cout << "SELECTOR model=" << model << " name=" << c.name << " relative_rms=" << effect << '\n';
            check(effect > 1e-4, "model=" + std::to_string(model) + " dead selector " + c.name);
        }
        // Factory Buzz and its VOL are both zero. A user can deliberately
        // open Buzz from zero with a positive additive velocity depth.
        auto routedBuzz = base;
        routedBuzz.velocityToBuzz = 0;
        check(difference(render(base), render(routedBuzz)) == 0,
              "zero Buzz VOL changes the dry factory setting model=" + std::to_string(model));
        auto loose = base, tight = base;
        loose.membraneTension = 0; tight.membraneTension = 1;
        const auto fixed = render(loose), moved = render(loose, false, &tight);
        check(std::equal(fixed.begin(), fixed.begin() + 150 * 64 * 2, moved.begin()),
              "live Tension changes audio before the control arrives");
        check(difference(fixed, moved, 150 * 64 * 2) > .005,
              "live Tension has no dry support response model=" + std::to_string(model));
        auto engine = std::make_unique<maremba::MarembaEngine>(rate);
        engine->SetParameters(base);
        for (int note : {36, 60, 84}) engine->NoteOn(note, .8f);
        float l[64], r[64];
        for (int b = 0; b < 100; ++b)
            engine->RenderBatch(l, r, nullptr, nullptr, nullptr, nullptr, nullptr, 64);
        engine->SetPitchBendCents(75);
        check(maremba::MarembaEngineTestAccess::PassiveTension(*engine),
              "Tension injects energy, shifts pitch or makes an unstable pole model=" + std::to_string(model));
    }
    std::cout << "ParameterReachabilityTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
