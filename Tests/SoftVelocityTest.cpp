// Public-engine Sigmoid response, independent of optional velocity routes.
// Compile with -I DSP and DSP/*.cpp. --measure prints the complete matrix;
// gates use broad margins from the measured physical response, not audio goldens.
// Loudness is measured on the final main output at Volume=.8. Tone and dynamics
// use direct Close, so safety gain reduction cannot imitate a velocity change.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace maremba;
constexpr double kRate = 48000.0;
constexpr int kFrames = 8640, kHits = 3;
constexpr std::array<int, 7> kVelocities{1, 8, 16, 32, 64, 96, 127};
constexpr std::array<std::array<int, 3>, 4> kNotes{{{48, 60, 84}, {48, 60, 84},
                                                                {48, 60, 84}, {60, 72, 84}}};
int failures = 0, checks = 0;
void check(bool condition, const std::string& context, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << context << ' ' << message << '\n'; }
}
double dbEnergy(double energy) { return 10 * std::log10(std::max(energy, 1.e-30)); }
struct Hit {
    std::vector<double> close;
    double mainEnergy = 0, mainPeak = 0, closeEnergy = 0;
    double attackEnergy = 0, attackHigh = 0, ringEnergy = 0, ringHigh = 0;
};
EngineParameters clean(int model) {
    EngineParameters p;
    p.model = model;
    p.malletType = 1; p.malletHardness = .4f; p.strikePosition = .45f;
    p.resonatorCoupling = .7f; p.decay = .7f; p.volume = .8f;
    p.closeLevel = 1; p.farLevel = p.piezoLevel = 0;
    p.preampDrive = p.compAmount = p.warmth = 0;
    p.strikeJitter = p.artifacts = p.detune = p.buzzAmount = 0;
    p.bodyBloom = p.sympathetic = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.velocityToCarving = p.velocityToCoupling = p.velocityToBloom = p.velocityToSympathetic = 0;
    p.velocityToBuzz = p.velocityToTension = p.velocityToArtifacts = 0;
    return p;
}
std::array<Hit, kHits> render(int model, int note, int velocity, bool explicitCurve = true) {
    MarembaEngine engine(kRate);
    auto parameters = clean(model);
    if (explicitCurve) parameters.velocityCurve = 3;
    engine.SetParameters(parameters);
    std::array<Hit, kHits> hits;
    std::array<float, 64> l{}, r{}, cl{}, cr{};
    const int delay = OutputHeadroom::LatencyFrames(kRate);
    const double cutoff = 3 * 440 * std::exp2((note - 69) / 12.0);
    const double k = std::tan(3.14159265358979323846 * cutoff / kRate);
    const double b0 = 1 / (1 + std::sqrt(2.0) * k + k * k);
    const double a1 = 2 * (k * k - 1) * b0, a2 = (1 - std::sqrt(2.0) * k + k * k) * b0;
    const auto context = "model=" + std::to_string(model) + " note=" + std::to_string(note)
                       + " velocity=" + std::to_string(velocity);
    for (auto& hit : hits) {
        check(engine.IsSilent(), context, "strike starts before previous capture has drained");
        engine.NoteOn(note, velocity / 127.f);
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int frame = 0; frame < kFrames + delay;) {
            const int count = std::min(64, kFrames + delay - frame);
            engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr, nullptr, count);
            for (int i = 0; i < count; ++i, ++frame) {
                check(std::isfinite(l[i]) && std::isfinite(r[i]) && std::isfinite(cl[i]) && std::isfinite(cr[i]),
                      context, "nonfinite output");
                if (frame < delay) continue;
                const double x = cl[i] + cr[i];
                const double high = b0 * (x - 2 * x1 + x2) - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = high;
                hit.close.push_back(x);
                hit.mainEnergy += (double(l[i]) * l[i] + double(r[i]) * r[i]) * .5;
                hit.mainPeak = std::max(hit.mainPeak, double(std::max(std::abs(l[i]), std::abs(r[i]))));
                hit.closeEnergy += x * x;
                const double time = (frame - delay) / kRate;
                if (time < .008) { hit.attackEnergy += x * x; hit.attackHigh += high * high; }
                if (time >= .012 && time < .16) { hit.ringEnergy += x * x; hit.ringHigh += high * high; }
            }
        }
        check(hit.mainEnergy > 0 && hit.attackEnergy > 0 && hit.ringEnergy > 0,
              context, "silent fixture");
        engine.DampAll();
        for (int frames = 0; frames < int(15 * kRate) && !engine.IsSilent(); frames += 64)
            engine.RenderBatch(l.data(), r.data(), nullptr, nullptr, nullptr, nullptr, nullptr, 64);
    }
    return hits;
}
double residual(const Hit& a, const Hit& b) {
    double aa = 0, bb = 0, ab = 0;
    const size_t count = size_t(.008 * kRate);
    for (size_t i = 0; i < count; ++i) { aa += a.close[i] * a.close[i]; bb += b.close[i] * b.close[i]; ab += a.close[i] * b.close[i]; }
    const double gain = ab / std::max(aa, 1.e-300);
    double error = 0;
    for (size_t i = 0; i < count; ++i) { const double d = b.close[i] - gain * a.close[i]; error += d * d; }
    return std::sqrt(error / std::max(bb, 1.e-300));
}
void silentNoteOff() {
    for (int model = 0; model < 4; ++model) {
        MarembaEngine engine(kRate);
        engine.SetParameters(clean(model));
        engine.NoteOn(60, 0);
        std::array<std::array<float, 64>, 7> channels{};
        for (int block = 0; block < 16; ++block) {
            engine.RenderBatch(channels[0].data(), channels[1].data(), channels[2].data(),
                channels[3].data(), channels[4].data(), channels[5].data(), channels[6].data(), 64);
            bool zero = true;
            for (const auto& channel : channels) for (float sample : channel) zero = zero && sample == 0;
            check(zero, "model=" + std::to_string(model), "velocity zero generated audio");
        }
        check(engine.IsSilent() && engine.GetActiveVoiceCount() == 0,
              "model=" + std::to_string(model), "velocity zero started a voice");
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool measureOnly = argc == 2 && std::string(argv[1]) == "--measure";
    if (argc > 1 && !measureOnly) { std::cerr << "usage: SoftVelocityTest [--measure]\n"; return 2; }
    std::cout << std::setprecision(10);
    check(EngineParameters{}.velocityCurve == 3, "default", "engine default is not Sigmoid");
    silentNoteOff();
    double loudestV1 = -300, loudestV8 = -300, leastSoftDynamicRange = 300;
    double leastLowVelocityMellowing = 300, minimumAttackResidual = 1;
    for (int model = 0; model < 4; ++model) for (int note : kNotes[model]) {
        std::array<std::array<Hit, kHits>, kVelocities.size()> renders;
        for (size_t v = 0; v < kVelocities.size(); ++v) renders[v] = render(model, note, kVelocities[v]);
        if (note == 60) {
            const auto defaults = render(model, note, 8, false);
            for (int hit = 0; hit < kHits; ++hit) {
                const auto& explicitSigmoid = renders[1][hit];
                check(defaults[hit].close == explicitSigmoid.close
                      && defaults[hit].mainEnergy == explicitSigmoid.mainEnergy
                      && defaults[hit].mainPeak == explicitSigmoid.mainPeak,
                      "default model=" + std::to_string(model), "default audio differs from explicit Sigmoid");
            }
        }
        for (size_t v = 0; v < kVelocities.size(); ++v) for (int hit = 0; hit < kHits; ++hit) {
            const auto& a = renders[v][hit];
            const auto& forte = renders.back()[hit];
            const double attackBrightness = dbEnergy(a.attackHigh / a.attackEnergy);
            const double ringBrightness = dbEnergy(a.ringHigh / a.ringEnergy);
            const double attackChange = attackBrightness - dbEnergy(forte.attackHigh / forte.attackEnergy);
            const double ringChange = ringBrightness - dbEnergy(forte.ringHigh / forte.ringEnergy);
            const double mainRms = dbEnergy(a.mainEnergy / kFrames);
            const double dynamicRange = -dbEnergy(a.closeEnergy / forte.closeEnergy);
            const double attackResidual = residual(a, forte);
            if (v == 0) loudestV1 = std::max(loudestV1, mainRms);
            if (v == 1) loudestV8 = std::max(loudestV8, mainRms);
            if (v < 3) {
                leastSoftDynamicRange = std::min(leastSoftDynamicRange, dynamicRange);
                leastLowVelocityMellowing = std::min(leastLowVelocityMellowing, -attackChange);
                minimumAttackResidual = std::min(minimumAttackResidual, attackResidual);
            }
            if (!measureOnly) {
                const auto context = "model=" + std::to_string(model) + " note=" + std::to_string(note)
                    + " velocity=" + std::to_string(kVelocities[v]) + " hit=" + std::to_string(hit);
                check(mainRms > -140, context, "valid soft strike is effectively silent");
                // A slow touch must be quiet in absolute level, and much quieter
                // than the same instrument's forte even with the optional VOL
                // controls disabled. These are deliberately broad musical bounds.
                constexpr std::array<double, 4> quietRms{-55, -48, -38, -27};
                constexpr std::array<double, 4> quietRange{40, 32, 24, 12};
                if (v < quietRms.size()) {
                    check(mainRms < quietRms[v], context, "low velocity is too loud at normal Volume");
                    check(dynamicRange > quietRange[v], context, "low velocity lacks physical dynamic range");
                    check(attackChange < (model == 3 ? -.1 : (v < 3 ? -6.0 : -1.0)),
                          context, "slow attack is not spectrally mellower than forte");
                    check(attackResidual > .02, context, "velocity changed only scalar gain at the attack");
                }
                if (v) {
                    const double step = dbEnergy(a.closeEnergy / renders[v - 1][hit].closeEnergy);
                    check(step > 1, context, "physical capture did not grow with velocity");
                }
            }
            std::cout << "SOFT model=" << model << " note=" << note << " velocity=" << kVelocities[v]
                      << " hit=" << hit << " main_rms_dbfs=" << dbEnergy(a.mainEnergy / kFrames)
                      << " main_peak_dbfs=" << 20 * std::log10(a.mainPeak)
                      << " main_relative_db=" << dbEnergy(a.mainEnergy / forte.mainEnergy)
                      << " direct_relative_db=" << dbEnergy(a.closeEnergy / forte.closeEnergy)
                      << " attack_brightness_db=" << attackBrightness
                      << " ring_brightness_db=" << ringBrightness
                      << " attack_brightness_vs_ff_db=" << attackChange
                      << " ring_brightness_vs_ff_db=" << ringChange
                      << " attack_gain_fit_residual=" << attackResidual << '\n';
        }
    }
    std::cout << "SUMMARY loudest_v1_rms_dbfs=" << loudestV1 << " loudest_v8_rms_dbfs=" << loudestV8
              << " minimum_v1_to_v16_dynamic_range_db=" << leastSoftDynamicRange
              << " minimum_v1_to_v16_attack_mellowing_db=" << leastLowVelocityMellowing
              << " minimum_v1_to_v16_attack_gain_fit_residual=" << minimumAttackResidual << '\n';
    std::cout << "SoftVelocityTest " << (failures ? "FAILED" : measureOnly ? "MEASURED" : "PASSED") << " checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
