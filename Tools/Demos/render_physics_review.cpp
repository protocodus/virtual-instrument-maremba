// Identical fixed score and explicit settings for frozen before/after DSP trees.
// This native renderer does not exercise the Reason wrapper or host.
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
constexpr int kFrames = kRate * 14;
struct Strike { double time; int note; float velocity; };
constexpr std::array<Strike, 14> kScore{{
    {0.15, 60, 0.25f}, {1.00, 60, 0.45f}, {1.85, 60, 0.70f}, {2.70, 60, 1.00f},
    {3.70, 67, 0.75f}, {4.05, 67, 0.75f}, {4.40, 67, 0.75f}, {4.75, 67, 0.75f},
    {5.80, 48, 0.65f}, {6.40, 55, 0.70f}, {7.00, 72, 0.80f},
    {8.20, 60, 0.72f}, {8.20, 64, 0.72f}, {8.20, 67, 0.72f}
}};
// A loud note followed by a quiet restrike while its bar is still moving.
// Varying the delay exposes contact reaction at different ringing phases.
constexpr std::array<Strike, 15> kRestrikeScore{{
    {.15,48,.95f},{.190,48,.05f}, {1.50,48,.95f},{1.544,48,.05f},
    {2.85,48,.95f},{2.897,48,.05f}, {4.20,60,.95f},{4.240,60,.05f},
    {5.55,60,.95f},{5.594,60,.05f}, {6.90,60,.95f},{6.947,60,.05f},
    {8.20,60,.72f},{8.20,64,.72f},{8.20,67,.72f}
}};
struct Event { int frame; int note; float velocity; };

maremba::EngineParameters Parameters(int model, int setup) {
    maremba::EngineParameters p;
    // Explicit values keep this comparison reproducible if defaults change.
    p.model = model;
    p.malletType = 1;
    p.malletHardness = 0.40f;
    p.strikePosition = 0.50f;
    p.resonatorTune = 0.0f;
    p.resonatorCoupling = 0.70f;
    p.decay = 1.0f;
    p.buzzAmount = 0.15f;
    p.artifacts = 0.35f;
    p.sympathetic = 0.40f;
    p.pitchGlide = 0.0f;
    p.bodyBloom = 0.50f;
    p.closeLevel = 0.85f;
    p.farLevel = 0.45f;
    p.roomType = 0;
    p.piezoLevel = 0.30f;
    p.stereoWidth = 1.0f;
    p.preampDrive = 0.15f;
    p.warmth = 0.10f;
    p.compAmount = 0.25f;
    p.compAttack = 10.0f;
    p.compRelease = 120.0f;
    p.volume = 0.80f;
    p.oversampling = 0;
    p.velocityCurve = 1;
    p.polyphony = 1;
    p.tuneCents = p.detune = 0.0f;
    p.strikeJitter = 15.0f;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0.20f;
    if (setup == 0 || setup == 3) {
        p.closeLevel = 1.0f;
        p.farLevel = p.piezoLevel = 0.0f;
        p.sympathetic = p.bodyBloom = 0.0f;
        p.preampDrive = p.warmth = p.compAmount = 0.0f;
    } else if (setup == 2) {
        // A deliberately exposed upper-mode comparison: hard off-centre contact
        // into the Far direct tap. Far level zero also disables its room blend;
        // the direct tap is independent of the main microphone faders.
        p.malletType = 3;
        p.malletHardness = .90f;
        p.strikePosition = .35f;
        p.resonatorCoupling = 1.0f;
        p.buzzAmount = p.artifacts = 0.0f;
        p.sympathetic = p.bodyBloom = 0.0f;
        p.closeLevel = p.farLevel = p.piezoLevel = 0.0f;
        p.preampDrive = p.warmth = p.compAmount = 0.0f;
    }
    if (setup == 3) {
        p.malletType = 0;
        p.malletHardness = .40f;
        p.strikePosition = .35f;
        p.decay = 4.0f;
        p.buzzAmount = p.artifacts = p.strikeJitter = 0.0f;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0.0f;
    }
    return p;
}

void WriteParameters(std::ostream& out, const maremba::EngineParameters& p) {
    out << "{\"mallet_type\":" << p.malletType
        << ",\"hardness\":" << p.malletHardness << ",\"position\":" << p.strikePosition
        << ",\"resonator_tune_cents\":" << p.resonatorTune << ",\"coupling\":" << p.resonatorCoupling
        << ",\"decay\":" << p.decay << ",\"buzz\":" << p.buzzAmount << ",\"artifacts\":" << p.artifacts
        << ",\"sympathetic\":" << p.sympathetic << ",\"bloom\":" << p.bodyBloom
        << ",\"close\":" << p.closeLevel << ",\"far\":" << p.farLevel << ",\"piezo\":" << p.piezoLevel
        << ",\"room_type\":" << p.roomType << ",\"width\":" << p.stereoWidth
        << ",\"drive\":" << p.preampDrive << ",\"warmth\":" << p.warmth << ",\"limiter\":" << p.compAmount
        << ",\"limiter_attack_ms\":" << p.compAttack << ",\"limiter_release_ms\":" << p.compRelease
        << ",\"volume\":" << p.volume << ",\"oversampling_factor\":2,\"polyphony\":16"
        << ",\"velocity_curve\":\"linear\",\"tune_cents\":0,\"detune\":0,\"legacy_glide\":0"
        << ",\"variance\":" << p.strikeJitter << ",\"velocity_to_hardness\":" << p.velocityToHardness
        << ",\"velocity_to_position\":" << p.velocityToPosition << ",\"velocity_to_decay\":" << p.velocityToDecay
        << '}';
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: render_physics_review OUTPUT_DIRECTORY\n";
        return 2;
    }
    const std::string output = argv[1];
    const std::array<const char*, 4> setups{{"dry_close", "default_mix", "hard_far", "quiet_restrike"}};
    for (int model = 0; model < 4; ++model) for (int setup = 0; setup < 4; ++setup) {
        const std::vector<Strike> score = setup == 3
            ? std::vector<Strike>(kRestrikeScore.begin(), kRestrikeScore.end())
            : std::vector<Strike>(kScore.begin(), kScore.end());
        std::vector<Event> events;
        for (const auto& strike : score) {
            const int frame = static_cast<int>(std::lround(strike.time * kRate));
            events.push_back({frame, strike.note, strike.velocity});
            events.push_back({frame + kRate * 12 / 100, strike.note, 0.0f});
        }
        // Stable order fixes voice allocation and random sequences within chords.
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.frame < b.frame;
        });
        maremba::MarembaEngine engine(kRate);
        engine.SetParameters(Parameters(model, setup));
        engine.Reset();
        const std::string name = "model_" + std::to_string(model) + "_" + setups[setup];
        std::ofstream stream(output + "/" + name + ".f32", std::ios::binary);
        if (!stream) return 3;
        std::array<float, 64> left{}, right{};
        std::array<float, 64> farLeft{}, farRight{};
        std::array<float, 128> samples{};
        size_t eventIndex = 0;
        for (int frame = 0; frame < kFrames;) {
            while (eventIndex < events.size() && events[eventIndex].frame == frame) {
                const auto& event = events[eventIndex++];
                if (event.velocity > 0.0f) engine.NoteOn(event.note, event.velocity);
                else engine.NoteOff(event.note);
            }
            int count = std::min(64, kFrames - frame);
            if (eventIndex < events.size()) count = std::min(count, events[eventIndex].frame - frame);
            engine.RenderBatch(left.data(), right.data(), nullptr, nullptr,
                setup == 2 ? farLeft.data() : nullptr, setup == 2 ? farRight.data() : nullptr,
                nullptr, count);
            const float* takeLeft = setup == 2 ? farLeft.data() : left.data();
            const float* takeRight = setup == 2 ? farRight.data() : right.data();
            for (int i = 0; i < count; ++i) {
                if (!std::isfinite(takeLeft[i]) || !std::isfinite(takeRight[i])) return 4;
                samples[2 * i] = takeLeft[i];
                samples[2 * i + 1] = takeRight[i];
            }
            stream.write(reinterpret_cast<const char*>(samples.data()), count * 2 * sizeof(float));
            frame += count;
        }
        if (!stream) return 5;
        std::cout << name << '\n';
    }

    std::ofstream manifest(output + "/render-manifest.json");
    manifest << "{\"sample_rate\":" << kRate << ",\"frames\":" << kFrames
        << ",\"raw_channels_by_setup\":{\"dry_close\":[\"main_left\",\"main_right\"],"
        << "\"default_mix\":[\"main_left\",\"main_right\"],\"hard_far\":[\"direct_far_left\",\"direct_far_right\"],\"quiet_restrike\":[\"main_left\",\"main_right\"]}"
        << ",\"note_gate_seconds\":0.12,\"settings\":{";
    for (int setup = 0; setup < 4; ++setup) {
        if (setup) manifest << ',';
        manifest << '"' << setups[setup] << "\":";
        WriteParameters(manifest, Parameters(0, setup));
    }
    manifest << "},\"score\":[";
    for (size_t i = 0; i < kScore.size(); ++i) {
        if (i) manifest << ',';
        const auto& strike = kScore[i];
        manifest << "{\"seconds\":" << strike.time << ",\"midi_note\":" << strike.note
                 << ",\"velocity\":" << strike.velocity << '}';
    }
    manifest << "],\"quiet_restrike_score\":[";
    for (size_t i = 0; i < kRestrikeScore.size(); ++i) {
        if (i) manifest << ',';
        const auto& strike = kRestrikeScore[i];
        manifest << "{\"seconds\":" << strike.time << ",\"midi_note\":" << strike.note
                 << ",\"velocity\":" << strike.velocity << '}';
    }
    manifest << "]}\n";
    return manifest ? 0 : 6;
}
