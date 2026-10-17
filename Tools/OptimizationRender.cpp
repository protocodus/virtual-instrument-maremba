// Render the same deterministic workload against frozen and candidate DSP
// trees, then compare the binary files. This is an optimization oracle, not a
// golden generated from the implementation under test. File I/O stays outside
// RenderBatch. Usage: optimization_render output.bin [sample-rate]
#include "MarembaEngine.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    const int rate = argc == 3 ? std::atoi(argv[2]) : 48000;
    if (rate < 22050 || rate > 192000) return 2;
    FILE* output = std::fopen(argv[1], "wb");
    if (!output) return 2;
    std::array<std::array<float, 64>, 7> samples{};
    std::uint64_t frames = 0;
    for (int model = 0; model < 4; ++model)
    for (int quality = 0; quality < 3; ++quality)
    for (int scenario = 0; scenario < 3; ++scenario) {
        maremba::MarembaEngine engine(rate);
        maremba::EngineParameters p;
        p.model = model; p.oversampling = quality; p.roomType = scenario;
        if (scenario == 1) {
            p.polyphony = 2; p.malletType = 4; p.malletHardness = 1;
            p.strikePosition = .001f; p.decay = 8; p.buzzAmount = 1;
            p.artifacts = 1; p.carving = 0; p.membraneTension = 1;
            p.velocityToBloom = .5f; p.velocityToSympathetic = .7f;
            p.preampDrive = 1; p.compAmount = 0;
        } else if (scenario == 2) {
            p.noteOffMode = 2; p.malletType = 0; p.modWheel = .7f;
            p.velocityToCoupling = .6f; p.velocityToBuzz = .4f;
            p.velocityToArtifacts = .5f; p.velocityToTension = .8f;
            p.preampDrive = 0; p.compAmount = 1;
        }
        engine.SetParameters(p);
        for (int block = 0; block < 640; ++block) {
            if (block == 320) engine.Reset();
            if (block < 384 && block % 32 == 0) {
                const int count = scenario == 1 ? 28 : 6;
                for (int n = 0; n < count; ++n) {
                    const int note = (n * 13 + block / 8) % 128;
                    const float velocity = (n % 4 == 0) ? 1.f / 127.f : (n % 4 == 1 ? 1.f : .67f);
                    engine.NoteOn(note, velocity);
                }
            }
            if (block % 32 == 17)
                for (int note = 0; note < 128; note += 3) engine.NoteOff(note);
            if (block == 96) engine.SetPitchBendCents(197.f);
            if (block == 144) engine.SetPitchBendCents(-193.f);
            if (block == 192) { p.oversampling = (quality + 1) % 3; engine.SetParameters(p); }
            if (block == 196) engine.NoteOn(127, 1.f);
            if (block == 240) {
                p.model = (model + 1) % 4; p.preampDrive = .63f;
                p.sympathetic = .9f; p.bodyBloom = .8f; p.modWheel = .8f;
                engine.SetParameters(p);
            }
            if (block == 300) { p.polyphony = 0; engine.SetParameters(p); }
            if (block == 400) engine.DampAll();
            const int count = block % 5 == 0 ? 1 : (block % 5 == 1 ? 17 : 64);
            // Output connection changes exercise independent headroom state.
            const bool direct = block % 7 != 0;
            for (auto& channel : samples) channel.fill(0);
            engine.RenderBatch(samples[0].data(), samples[1].data(),
                direct ? samples[2].data() : nullptr, direct ? samples[3].data() : nullptr,
                direct ? samples[4].data() : nullptr, direct ? samples[5].data() : nullptr,
                direct ? samples[6].data() : nullptr, count);
            for (const auto& channel : samples)
                if (std::fwrite(channel.data(), sizeof(float), count, output) != static_cast<std::size_t>(count)) return 3;
            const int state[] = {engine.GetActiveVoiceCount(), engine.IsSilent(), engine.TakeRollStrokes()};
            if (std::fwrite(state, sizeof(int), 3, output) != 3) return 3;
            frames += count;
        }
    }
    if (std::fclose(output) != 0) return 3;
    std::cout << "rate=" << rate << " frames=" << frames << " channels=7 cases=36\n";
}
