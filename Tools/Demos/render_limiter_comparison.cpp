// Same harness is compiled against the pinned old DSP and current DSP.
// Writes unattenuated, little-endian float32 frames: main L/R, close L/R,
// room L/R, piezo. Delivery conversion happens separately in Python.
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kRate = 44100;
constexpr int kFrames = 253575; // 5.75 seconds
struct Event { int frame, note, velocity; };
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "Usage: render_limiter_comparison factory_like|driven amount output.f32\n";
        return 2;
    }
    const std::string program = argv[1];
    char* end = nullptr;
    const double amount = std::strtod(argv[2], &end);
    if ((program != "factory_like" && program != "driven") || end == argv[2]
        || *end || !std::isfinite(amount) || amount < 0.0 || amount > 1.0) {
        std::cerr << "Invalid program or amount\n";
        return 2;
    }
    const uint32_t endian = 1;
    if (*reinterpret_cast<const unsigned char*>(&endian) != 1 || sizeof(float) != 4) {
        std::cerr << "This raw-audio format requires little-endian float32\n";
        return 2;
    }
    std::ofstream output(argv[3], std::ios::binary);
    if (!output) {
        std::cerr << "Cannot open raw output\n";
        return 1;
    }

    maremba::EngineParameters p;
    // Near the Concert Grand patch, with exact round-number physical settings.
    // This is a dedicated comparison program, not a factory-patch loader.
    p.resonatorCoupling = 0.75f;
    p.decay = 1.0f;
    p.buzzAmount = 0.0f;
    p.sympathetic = 0.45f;
    p.bodyBloom = 0.55f;
    p.farLevel = 0.50f;
    p.piezoLevel = 0.25f;
    p.preampDrive = 0.12f;
    p.compAmount = static_cast<float>(amount);
    p.compAttack = 12.0f;
    p.compRelease = 120.0f;
    if (program == "driven") {
        p.malletType = 3;
        p.malletHardness = 0.85f;
        p.strikePosition = 0.35f;
        p.closeLevel = p.farLevel = p.piezoLevel = 1.0f;
        p.sympathetic = p.bodyBloom = 1.0f;
        p.preampDrive = 0.75f;
        p.volume = 1.0f;
        p.polyphony = 2;
    }
    maremba::MarembaEngine engine(kRate);
    engine.SetParameters(p);
    engine.Reset();

    std::vector<Event> events;
    const auto note = [&](double seconds, int pitch, int velocity) {
        events.push_back({static_cast<int>(std::lround(seconds * kRate)), pitch, velocity});
    };
    note(0.08, 60, 32); note(0.42, 64, 64);
    note(0.76, 67, 96); note(1.10, 72, 127);
    for (int pitch : {48, 52, 55, 60, 64, 67}) note(1.50, pitch, 127);
    note(1.70, 60, 32);
    for (int pitch : {45, 52, 57, 60, 64, 69, 72, 76}) note(2.25, pitch, 127);
    note(2.45, 64, 32); note(3.10, 60, 64); note(3.65, 67, 48);
    note(4.25, 60, 32); note(4.60, 64, 48);
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    std::array<std::array<float, 64>, 7> channels{};
    std::array<float, 64 * 7> interleaved{};
    size_t next = 0;
    for (int frame = 0; frame < kFrames;) {
        while (next < events.size() && events[next].frame == frame) {
            engine.NoteOn(events[next].note, events[next].velocity / 127.0f);
            ++next;
        }
        int count = std::min(64, kFrames - frame);
        if (next < events.size()) count = std::min(count, events[next].frame - frame);
        engine.RenderBatch(channels[0].data(), channels[1].data(), channels[2].data(),
                           channels[3].data(), channels[4].data(), channels[5].data(), channels[6].data(), count);
        for (int i = 0; i < count; ++i) {
            for (size_t ch = 0; ch < channels.size(); ++ch) {
                if (!std::isfinite(channels[ch][i])) {
                    std::cerr << "Non-finite raw output\n";
                    return 1;
                }
                interleaved[i * 7 + ch] = channels[ch][i];
            }
        }
        output.write(reinterpret_cast<const char*>(interleaved.data()), count * 7 * sizeof(float));
        frame += count;
    }
    output.flush();
    if (!output) {
        std::cerr << "Failed writing raw output\n";
        return 1;
    }
    std::cout << "{\"program\":\"" << program << "\",\"amount\":" << amount
              << ",\"sample_rate\":" << kRate << ",\"frames\":" << kFrames
              << ",\"channels\":7,\"attack_ms\":12,\"release_ms\":120,\"oversampling\":2,\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[' << events[i].frame << ',' << events[i].note << ',' << events[i].velocity << ']';
    }
    std::cout << "]}\n";
}
