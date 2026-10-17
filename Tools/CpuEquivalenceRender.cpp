// Differential render fixture: compile this same source against the frozen
// release DSP and candidate DSP, then compare the binary streams exactly.
// This is offline test code; file IO and allocations never enter the engine.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

using namespace maremba;

struct Case { int rate, model, quality, room; bool dynamic; };

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ofstream stream(argv[1], std::ios::binary);
    if (!stream) return 3;
    std::vector<Case> cases;
    for (int model = 0; model < 4; ++model) {
        for (int rate : {44100, 48000, 96000})
            cases.push_back({rate, model, 0, 0, false});
        for (int quality : {1, 2})
            cases.push_back({48000, model, quality, 0, false});
        for (int room : {1, 2})
            cases.push_back({48000, model, 0, room, false});
        cases.push_back({48000, model, 0, 0, true});
    }
    const auto recordInt = [&](int value) {
        const std::int32_t fixed = value;
        stream.write(reinterpret_cast<const char*>(&fixed), sizeof(fixed));
    };
    constexpr int sizes[] = {64, 7, 1, 32, 63, 16};
    for (const auto& c : cases) {
        recordInt(c.rate); recordInt(c.model); recordInt(c.quality);
        recordInt(c.room); recordInt(c.dynamic);
        auto engine = std::make_unique<MarembaEngine>(c.rate);
        EngineParameters p;
        p.model = c.model; p.oversampling = c.quality; p.roomType = c.room;
        engine->SetParameters(p);
        // Include all 48 steal/fade slots, then normal 16-voice operation.
        for (int note = 0; note < 48; ++note)
            engine->NoteOn(36 + note, .3f + .7f * (note % 9) / 8.f);
        std::array<std::array<float, 64>, 7> samples{};
        const int totalFrames = c.rate * (c.dynamic ? 5 : 3);
        int frame = 0, block = 0;
        while (frame < totalFrames) {
            int count = std::min(sizes[block % 6], totalFrames - frame);
            if (frame < c.rate && block % 23 == 0)
                engine->NoteOn(42 + block % 39, .45f + .5f * (block % 7) / 6.f);
            if (c.dynamic) {
                if (block == 5) engine->SetPitchBendCents(137.f);
                if (block == 17) engine->SetPitchBendCents(-200.f);
                if (block == 12) p.polyphony = 0;
                if (block == 45) p.polyphony = 2;
                if (block == 25) {
                    p.tuneCents = 99.f; p.detune = 88.f; p.resonatorTune = -49.f;
                    p.malletHardness = .91f; p.strikePosition = .17f;
                    p.strikeJitter = 100.f; p.artifacts = .9f; p.buzzAmount = 1.f;
                }
                if (block == 42) p.model = (c.model + 1) % 4;
                if (block == 54) {
                    p.bodyBloom = p.sympathetic = p.farLevel = p.piezoLevel = 0.f;
                    p.closeLevel = .07f; p.preampDrive = 0.f; p.stereoWidth = 0.f;
                    p.warmth = -1.f; p.compAmount = 0.f;
                }
                if (block == 125) {
                    p.bodyBloom = p.sympathetic = p.farLevel = p.piezoLevel = 1.f;
                    p.closeLevel = .93f; p.preampDrive = .77f; p.stereoWidth = 2.f;
                    p.warmth = 1.f; p.compAmount = .9f;
                    p.compAttack = 1.f; p.compRelease = 500.f; p.roomType = 2;
                }
                // Cancel one transition, then complete another with queued notes.
                if (block == 150 || block == 170) p.oversampling = 2;
                if (block == 151) p.oversampling = 0;
                if (block == 151 || block == 171)
                    for (int n = 0; n < 32; ++n) engine->NoteOn(48 + n, .84f);
                if (block == 173) { engine->NoteOff(48); engine->DampAll(); }
                if (block == 230) {
                    p.roomType = 1; p.oversampling = 1; p.malletType = 3;
                }
                if (block == 231) engine->NoteOn(77, .95f);
                if (block == 280) { engine->Reset(); engine->SetPitchBendCents(0.f); }
                if (block == 281) {
                    p.model = c.model; p.roomType = 0; p.oversampling = 0;
                    p.preampDrive = .15f; p.strikeJitter = 15.f;
                }
                if (block == 282)
                    for (int n = 0; n < 16; ++n) engine->NoteOn(42 + n, .7f);
            }
            if (frame < c.rate && frame + count >= c.rate) engine->DampAll();
            // Includes redundant host-style updates while live controls settle.
            engine->SetParameters(p);
            std::array<float*, 7> outputs{};
            for (int channel = 0; channel < 7; ++channel) {
                samples[channel].fill(-1234.f); // absent output remains unwritten
                outputs[channel] = samples[channel].data();
                if (c.dynamic && channel >= 2 && block % 97 < channel * 3)
                    outputs[channel] = nullptr;
            }
            engine->RenderBatch(outputs[0], outputs[1], outputs[2], outputs[3],
                                outputs[4], outputs[5], outputs[6], count);
            recordInt(count); recordInt(engine->GetActiveVoiceCount());
            recordInt(engine->IsSilent());
            for (const auto& channel : samples)
                stream.write(reinterpret_cast<const char*>(channel.data()),
                             count * sizeof(float));
            frame += count;
            ++block;
        }
        std::cout << "rendered rate=" << c.rate << " model=" << c.model
                  << " quality=" << c.quality << " room=" << c.room
                  << " dynamic=" << c.dynamic << " frames=" << frame << '\n';
    }
    return stream ? 0 : 4;
}
