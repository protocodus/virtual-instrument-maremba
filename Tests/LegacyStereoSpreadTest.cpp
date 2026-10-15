// Older saved Stereo Spread values are inert. Preserve the natural capture
// mix, ordinary Width, direct outputs, deterministic reset and event splitting.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 7>;
int cases = 0;
void require(bool okay, const char* text) {
    if (!okay) { std::cerr << "FAIL LegacyStereoSpreadTest: " << text << '\n'; std::exit(1); }
}
Audio phrase(MarembaEngine& engine, double rate, EngineParameters p, bool automate, int batch) {
    engine.SetParameters(p); engine.Reset();
    require(engine.GetParameters().gentleWiden == 0, "legacy input survives sanitization/reset");
    const int frames = static_cast<int>(rate * .11), second = static_cast<int>(rate * .025);
    const int change = static_cast<int>(rate * .047);
    Audio audio;
    for (auto& channel : audio) channel.resize(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames;) {
        if (frame == 0) engine.NoteOn(60, .4f);
        if (frame == second) engine.NoteOn(72, .3f);
        if (frame == change && automate) {
            p.gentleWiden = p.gentleWiden == 0 ? 1 : 0;
            engine.SetParameters(p);
            require(engine.GetParameters().gentleWiden == 0, "live legacy input reaches audio parameters");
        }
        const int boundary = frame < second ? second : frame < change ? change : frames;
        const int count = std::min(batch, boundary - frame);
        engine.RenderBatch(audio[0].data()+frame, audio[1].data()+frame,
                           audio[2].data()+frame, audio[3].data()+frame,
                           audio[4].data()+frame, audio[5].data()+frame,
                           audio[6].data()+frame, count);
        frame += count;
    }
    for (const auto& channel : audio) for (float value : channel)
        require(std::isfinite(value), "audio is not finite");
    return audio;
}
void verify(double rate, int model, int quality) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    EngineParameters p;
    p.model = model; p.oversampling = quality;
    p.volume = .05f; p.closeLevel = .4f; p.farLevel = .3f; p.piezoLevel = .7f;
    p.preampDrive = p.compAmount = p.warmth = 0;
    const auto natural = phrase(*engine, rate, p, false, 64);
    for (int legacy : {0, 1, -1, INT_MAX}) {
        p.gentleWiden = legacy;
        require(phrase(*engine, rate, p, true, 7) == natural,
                "legacy input, reset or event splitting changes one of seven outputs");
    }
    p.stereoWidth = 0;
    const auto mono = phrase(*engine, rate, p, false, 64);
    p.stereoWidth = 2;
    const auto wide = phrase(*engine, rate, p, false, 64);
    double sideEnergy = 0;
    for (std::size_t i = 0; i < mono[0].size(); ++i) {
        require(mono[0][i] == mono[1][i], "ordinary Width zero is not mono");
        const double side = .5 * (double(wide[1][i]) - wide[0][i]);
        sideEnergy += side * side;
    }
    require(sideEnergy > 1e-10, "ordinary Width has no stereo witness");
    for (int channel = 2; channel < 7; ++channel)
        require(mono[channel] == wide[channel] && wide[channel] == natural[channel],
                "ordinary Width changes a raw direct capture");
    ++cases;
}
}
int main() {
    require(EngineParameters{}.gentleWiden == 0, "legacy default changed");
    for (double rate : {44100., 192000.}) for (int model = 0; model < 4; ++model)
        for (int quality : {0, 2}) verify(rate, model, quality);
    std::cout << "PASS LegacyStereoSpreadTest cases=" << cases
              << " all_outputs=identical width=active reset=exact legacy=ignored\n";
}
