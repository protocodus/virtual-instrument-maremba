// Adversarial differential fixture for the CPU-only patch. Compile this SAME
// source against the frozen pre-optimization DSP and candidate DSP and compare
// the output bytes. No mutation of private DSP state and no timing claims.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>

namespace maremba {
// Observe naturally reachable mixed settled/ramping apertures. Observation is
// verifies coverage retained from the earlier aperture optimization experiments.
struct MarembaVoiceTestAccess {
    static bool MixedApertures(const MarembaVoice& voice) {
        if (!voice.mCavityAngleRamp) return false;
        int settled = 0;
        for (int i = 0; i < voice.mNumCavityModes; ++i)
            settled += voice.mCavityAngle[i] == voice.mCavityAngleTarget[i];
        return settled > 0 && settled < voice.mNumCavityModes;
    }
    static void RecordApertures(const MarembaVoice& voice, std::ofstream& stream) {
        for (int i = 0; i < voice.mNumCavityModes; ++i) {
            const double values[] = {voice.mCavityAngle[i], voice.mCavityAngleTarget[i],
                                     voice.mCavityCos[i], voice.mCavitySin[i]};
            stream.write(reinterpret_cast<const char*>(values), sizeof(values));
        }
    }
};
}

namespace {
void recordInt(std::ofstream& stream, int value) {
    const std::int32_t fixed = value;
    stream.write(reinterpret_cast<const char*>(&fixed), sizeof(fixed));
}
bool finite(float value) { return std::isfinite(value); }
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ofstream stream(argv[1], std::ios::binary);
    if (!stream) return 3;
    int mixedApertureSamples = 0;
    // Low/middle/top registers include three, two and dropped upper modes.
    // Repeated retunes and coupling/decay changes interrupt an existing ramp.
    for (int model = 0; model < 4; ++model) {
        for (int note : {36, 84, 120}) for (int rate : {88200, 192000}) {
            recordInt(stream, model); recordInt(stream, note); recordInt(stream, rate);
            maremba::MarembaVoice voice;
            voice.SetSeed(0x84567321u);
            maremba::FastPRNG random(0x12345678u);
            float bend = 0;
            const auto strike = [&](float coupling, float decay, float velocity) {
                voice.Trigger(note, velocity, static_cast<maremba::EMarimbaModel>(model),
                              .65f, .27f, 37.f, coupling, decay, .7f, .8f, 0.f,
                              rate, random, bend, 0.f, 2, 100.f);
            };
            strike(1.f, 1.f, .8f);
            const int frames = rate / 3;
            for (int frame = 0; frame < frames; ++frame) {
                if (frame == 256) strike(.01f, .1f, .23f);
                if (frame == 277 || frame == 390 || frame == 570) {
                    bend = frame == 390 ? -173.f : 197.f;
                    voice.Retune(bend);
                }
                if (frame == 343) strike(.95f, 6.75f, .91f);
                if (frame == 500) strike(0.f, .13f, .41f);
                if (frame == 1000) strike(1.f, .1f, .7f);
                if (frame == rate / 4) voice.StartDamping();
                float samples[4]{};
                const bool active = voice.ProcessSample(samples[0], samples[1], samples[2]);
                samples[3] = voice.GetMechanicalSample();
                for (float sample : samples) if (!finite(sample)) return 5;
                stream.write(reinterpret_cast<const char*>(samples), sizeof(samples));
                recordInt(stream, active);
                mixedApertureSamples += maremba::MarembaVoiceTestAccess::MixedApertures(voice);
                if ((frame & 31) == 31) {
                    voice.UpdateEnvelope();
                    maremba::MarembaVoiceTestAccess::RecordApertures(voice, stream);
                }
            }
        }
    }
    // A real wrapper leaves parameters untouched between notifications. Long
    // gaps cover settled controls, branch off/on resets, Drive changes,
    // fresh notes and pending quality-switch notes from the candidate audit
    // without relying on SetParameters being called before every block.
    for (int model = 0; model < 4; ++model) {
        recordInt(stream, model);
        auto engine = std::make_unique<maremba::MarembaEngine>(48000);
        maremba::EngineParameters p;
        p.model = model; p.roomType = 2; p.decay = 1.8f;
        p.preampDrive = .15f;
        engine->SetParameters(p);
        int block = 0;
        const auto render = [&](int frames) {
            constexpr int sizes[] = {64, 7, 1, 32, 63, 16};
            std::array<std::array<float, 64>, 7> samples{};
            while (frames > 0) {
                const int count = std::min(frames, sizes[block++ % 6]);
                engine->RenderBatch(samples[0].data(), samples[1].data(), samples[2].data(),
                                    samples[3].data(), samples[4].data(), samples[5].data(),
                                    samples[6].data(), count);
                recordInt(stream, count); recordInt(stream, engine->GetActiveVoiceCount());
                recordInt(stream, engine->IsSilent());
                for (const auto& channel : samples) {
                    for (int i = 0; i < count; ++i) if (!finite(channel[i])) return false;
                    stream.write(reinterpret_cast<const char*>(channel.data()), count * sizeof(float));
                }
                frames -= count;
            }
            return true;
        };
        for (int note : {36, 43, 48, 55, 60, 67}) engine->NoteOn(note, .8f);
        if (!render(18000)) return 5;
        p.sympathetic = p.bodyBloom = p.farLevel = p.preampDrive = 0.f;
        engine->SetParameters(p);
        if (!render(18000)) return 5;
        engine->NoteOn(60, .37f); // no parameter notification before this restrike
        if (!render(2400)) return 5;
        p.sympathetic = .8f; p.bodyBloom = .9f; p.farLevel = .7f;
        p.preampDrive = .15f; // return to the previous nonzero Drive value
        engine->SetParameters(p);
        if (!render(18000)) return 5;
        p.oversampling = 2;
        engine->SetParameters(p);
        for (int note = 44; note < 68; ++note) engine->NoteOn(note, .77f);
        if (!render(12000)) return 5;
        p.preampDrive = .001f; p.farLevel = .001f; p.bodyBloom = .001f;
        p.sympathetic = .001f; // exact branch thresholds after smoothing
        engine->SetParameters(p);
        if (!render(12000)) return 5;
        engine->Reset();
        engine->NoteOn(60, .69f); // same retained parameters, no reload after Reset
        if (!render(6000)) return 5;
        p.preampDrive = .0010001f; p.sympathetic = .0010001f;
        p.bodyBloom = .0010001f; p.farLevel = .0010001f;
        engine->SetParameters(p);
        if (!render(6000)) return 5;
        engine->DampAll();
        if (!render(96000)) return 5;
        engine->NoteOn(72, .95f);
        if (!render(6000)) return 5;
    }
    std::cout << "mixed_aperture_samples=" << mixedApertureSamples
              << " direct_voice_cases=24 sparse_engine_cases=4\n";
    if (mixedApertureSamples == 0) return 6;
    return stream ? 0 : 4;
}
