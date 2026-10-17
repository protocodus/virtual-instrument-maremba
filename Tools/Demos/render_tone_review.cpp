// Deterministic comparison renderer. Same score, gain and reset for each take.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    constexpr int rate = 44100, frames = rate * 5;
    struct Take { std::string name; maremba::EngineParameters p; };
    std::vector<Take> takes;
    maremba::EngineParameters base;
    base.artifacts = base.strikeJitter = base.buzzAmount = 0;
    base.sympathetic = base.bodyBloom = base.farLevel = base.piezoLevel = 0;
    base.preampDrive = base.warmth = base.compAmount = 0;
    base.closeLevel = .85f;
    takes.push_back({"dry", base});
    const std::array<std::pair<const char*, float maremba::EngineParameters::*>, 5> controls{{
        {"artifacts", &maremba::EngineParameters::artifacts},
        {"bloom", &maremba::EngineParameters::bodyBloom},
        {"buzz", &maremba::EngineParameters::buzzAmount},
        {"res_tune", &maremba::EngineParameters::resonatorTune},
        {"warmth", &maremba::EngineParameters::warmth}}};
    for (const auto& control : controls) {
        for (int step = 0; step <= 2; ++step) {
            auto p = base;
            p.*(control.second) = .5f * step;
            if (control.first == std::string("warmth")) p.warmth = step - 1.0f;
            if (control.first == std::string("res_tune")) p.resonatorTune = (step - 1.0f) * 50;
            takes.push_back({control.first + std::string("_") + std::to_string(step), p});
        }
    }
    for (int model = 0; model < 4; ++model) {
        auto p = base; p.model = model;
        takes.push_back({"model_" + std::to_string(model), p});
    }
#ifdef MAREMBA_HAS_ROOM_TYPES
    for (int room = 0; room < 3; ++room) {
        auto p = base; p.roomType = room; p.farLevel = .8f;
        takes.push_back({"room_" + std::to_string(room), p});
    }
#endif
    for (const auto& take : takes) {
        maremba::MarembaEngine engine(rate);
        engine.SetParameters(take.p); engine.Reset();
        std::ofstream out(std::string(argv[1]) + "/" + take.name + ".f32", std::ios::binary);
        if (!out) return 3;
        std::array<float,64> left{},right{};
        std::array<float,128> stereo{};
        const int notes[] = {48,60,67,72};
        for (int frame = 0; frame < frames;) {
            if (frame % rate == 0 && frame / rate < 4)
                engine.NoteOn(notes[frame / rate], .72f);
            int n = std::min({64,frames-frame,rate-frame%rate});
            engine.RenderBatch(left.data(),right.data(),nullptr,nullptr,nullptr,nullptr,nullptr,n);
            for (int i=0;i<n;++i) {
                if (!std::isfinite(left[i]) || !std::isfinite(right[i])) return 4;
                stereo[2*i]=left[i]; stereo[2*i+1]=right[i];
            }
            out.write(reinterpret_cast<const char*>(stereo.data()),n*2*sizeof(float));
            frame += n;
        }
        if (!out) return 5;
        std::cout << take.name << '\n';
    }
}
