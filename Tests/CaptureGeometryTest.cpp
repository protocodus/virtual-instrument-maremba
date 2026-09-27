#include "../DSP/CaptureGeometry.h"
#include "../DSP/MarembaVoice.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace maremba;

int main() {
    int checks = 0;
    for (int model = 0; model < 4; ++model) {
        const auto family = static_cast<EMarimbaModel>(model);
        for (int note = 0; note < 128; ++note) {
            const auto source = CaptureSourcePosition(family, note);
            assert(std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.z));
            const double close = CaptureDistance(source, CloseHeadCentre());
            assert(close > .4 && close < 1.7);
            for (int channel = 0; channel < 2; ++channel) {
                assert(CaptureDistance(source, RoomMicrophone(channel)) > close);
                for (int room = 0; room < 3; ++room) {
                    const auto d = RoomDefinition(room);
                    for (const auto p : {source, CloseEar(channel), RoomMicrophone(channel)}) {
                        assert(p.x+d.origin.x > 0 && p.x+d.origin.x < d.dimensions.x);
                        assert(p.y+d.origin.y > 0 && p.y+d.origin.y < d.dimensions.y);
                        assert(p.z+d.origin.z > 0 && p.z+d.origin.z < d.dimensions.z);
                        ++checks;
                    }
                }
            }
            const int index = CaptureSourceIndex(family, note);
            assert(CaptureDistance(source, CaptureSourcePosition(index)) == 0.0);
        }
        if (model < 3) {
            int previous = 36;
            for (int note = 37; note <= 96; ++note) {
                if (!CaptureNaturalKey(note)) continue;
                const double gap = CaptureSourcePosition(family,note).x-CaptureSourcePosition(family,previous).x
                    -.5*(CaptureBarWidth(family,note)+CaptureBarWidth(family,previous));
                assert(std::abs(gap-.005) < 1e-12);
                previous = note;
                ++checks;
            }
        }
        // Receiving the same physical states at a second point must not alter
        // their evolution or the electrical pickup, including a voice's fade.
        MarembaVoice reference, binaural;
        FastPRNG r1(1234), r2(1234);
        reference.SetSeed(5678); binaural.SetSeed(5678);
        auto trigger = [&](MarembaVoice& voice, FastPRNG& rng) {
            voice.Trigger(model == 3 ? 86 : 48, .9f, family, .8f, .5f, 0, .7f, 1, 0, 0, 0,
                          96000, rng, 0, 0, 2, 0);
        };
        trigger(reference, r1); trigger(binaural, r2);
        double energyL = 0, energyR = 0, dot = 0, delta = 0;
        for (int frame = 0; frame < 12000; ++frame) {
            if (frame == 11000) { reference.StartFade(.003); binaural.StartFade(.003); }
            float a,b,c,d,e,f,ears[2];
            const bool active = reference.ProcessSample(a,b,c);
            assert(active == binaural.ProcessSample(d,e,f,ears));
            assert(a==d && b==e && c==f);
            assert(std::isfinite(ears[0]) && std::isfinite(ears[1]));
            energyL += ears[0]*ears[0]; energyR += ears[1]*ears[1];
            dot += ears[0]*ears[1]; delta += (ears[0]-ears[1])*(ears[0]-ears[1]);
            ++checks;
        }
        assert(energyL > 0 && energyR > 0 && delta > 1e-12);
        // Before any HRTF, modes already have receiver-dependent phases and
        // cancellation: L/R cannot be copies differing only by a pan gain.
        const double correlation = dot/std::sqrt(energyL*energyR);
        assert(correlation < 1.0-1e-8);
        std::printf("model=%d ear-field correlation=%.9f\n",model,correlation);
    }
    assert(CloseEar(0).x < CloseEar(1).x);
    std::printf("CaptureGeometryTest: %d geometry and physical-state checks passed\n", checks);
}
