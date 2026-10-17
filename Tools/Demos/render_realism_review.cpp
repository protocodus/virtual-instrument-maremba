// Direct-output listening review. Every take gets the same score and reset.
// Build with -I <DSP directory> and that directory's *.cpp sources so the same
// renderer also works with a frozen pre-change engine.
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kRate = 44100;
constexpr int kFrames = kRate * 10;
struct Strike { double time; int note; float velocity; };
constexpr std::array<Strike, 12> kScore{{
    {0.15, 60, 0.72f}, {0.70, 60, 0.72f}, {1.25, 60, 0.72f}, {1.80, 60, 0.72f},
    {2.35, 48, 0.42f}, {2.62, 55, 0.60f}, {3.20, 60, 0.80f}, {3.75, 64, 0.95f},
    {4.30, 67, 0.55f}, {4.85, 72, 0.90f}, {5.40, 67, 0.62f}, {5.95, 60, 1.00f}
}};
struct Event { int frame; int note; float velocity; };
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: render_realism_review OUTPUT_DIRECTORY\n";
        return 2;
    }
    const std::string output = argv[1];
    std::vector<Event> events;
    for (const auto& strike : kScore) {
        const int frame = static_cast<int>(std::lround(strike.time * kRate));
        events.push_back({frame, strike.note, strike.velocity});
        events.push_back({frame + kRate * 12 / 100, strike.note, 0.0f});
    }
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.frame < b.frame;
    });

    maremba::EngineParameters base;
    base.malletType = 1;
    base.malletHardness = 0.52f;
    base.strikePosition = 0.42f;
    base.resonatorCoupling = 0.70f;
    base.decay = 1.0f;
    base.buzzAmount = base.strikeJitter = 0.0f;
    base.sympathetic = base.bodyBloom = 0.0f;
    base.closeLevel = base.piezoLevel = 1.0f;
    base.farLevel = 0.0f;
    base.preampDrive = base.warmth = base.compAmount = 0.0f;
    base.volume = 1.0f;
    base.velocityToHardness = base.velocityToPosition = base.velocityToDecay = 0.0f;
    base.oversampling = 0;
    base.velocityCurve = 1;
    base.detune = 0.0f;
    const std::array<int, 4> artifactPercent{{0, 35, 50, 100}};

    for (int model = 0; model < 4; ++model) {
        for (int amount : artifactPercent) {
            auto params = base;
            params.model = model;
            params.artifacts = amount / 100.0f;
            maremba::MarembaEngine engine(kRate);
            engine.SetParameters(params);
            engine.Reset();
            const std::string name = "model_" + std::to_string(model)
                + "_artifacts_" + std::to_string(amount);
            std::ofstream stream(output + "/" + name + ".f32", std::ios::binary);
            if (!stream) return 3;
            std::array<float, 64> mainL{}, mainR{}, closeL{}, closeR{}, piezo{};
            std::array<float, 64 * 3> samples{};
            size_t eventIndex = 0;
            for (int frame = 0; frame < kFrames;) {
                while (eventIndex < events.size() && events[eventIndex].frame == frame) {
                    const auto& event = events[eventIndex++];
                    if (event.velocity > 0.0f) engine.NoteOn(event.note, event.velocity);
                    else engine.NoteOff(event.note);
                }
                int count = std::min(64, kFrames - frame);
                if (eventIndex < events.size())
                    count = std::min(count, events[eventIndex].frame - frame);
                engine.RenderBatch(mainL.data(), mainR.data(), closeL.data(), closeR.data(),
                                   nullptr, nullptr, piezo.data(), count);
                for (int i = 0; i < count; ++i) {
                    if (!std::isfinite(closeL[i]) || !std::isfinite(closeR[i])
                        || !std::isfinite(piezo[i])) return 4;
                    samples[3 * i] = closeL[i];
                    samples[3 * i + 1] = closeR[i];
                    samples[3 * i + 2] = piezo[i];
                }
                stream.write(reinterpret_cast<const char*>(samples.data()),
                             count * 3 * sizeof(float));
                frame += count;
            }
            if (!stream) return 5;
            std::cout << name << '\n';
        }
    }

    std::ofstream manifest(output + "/render-manifest.json");
    manifest << "{\"sample_rate\":" << kRate << ",\"frames\":" << kFrames
        << ",\"raw_channels\":[\"close_left\",\"close_right\",\"piezo_mono\"],"
        << "\"settings\":{\"oversampling_factor\":2,\"mallet_type\":1,"
        << "\"hardness\":0.52,\"position\":0.42,\"coupling\":0.70,\"decay\":1,"
        << "\"buzz\":0,\"variance\":0,\"sympathetic\":0,\"bloom\":0,"
        << "\"room\":0,\"drive\":0,\"warmth\":0,\"limiter\":0,\"volume\":1,"
        << "\"velocity_curve\":\"linear\",\"velocity_routes\":0},\"score\":[";
    for (size_t i = 0; i < kScore.size(); ++i) {
        const auto& strike = kScore[i];
        if (i) manifest << ',';
        manifest << "{\"seconds\":" << strike.time << ",\"midi_note\":" << strike.note
            << ",\"velocity\":" << strike.velocity << '}';
    }
    manifest << "]}\n";
    return manifest ? 0 : 6;
}
