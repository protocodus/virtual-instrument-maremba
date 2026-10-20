// A muted bar-frame tail must age while the selected instrument is Kalimba.
// Otherwise changing back can revive a resonance from arbitrarily far in the past.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace maremba {
struct MarembaEngineTestAccess {
    static FrameBody Frame(const MarembaEngine& engine, bool velocity) {
        return velocity ? engine.mVelocityFrameBody : engine.mFrameBody;
    }
};
}

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition && failures++ < 12) std::cerr << "FAIL " << message << '\n';
}
void render(maremba::MarembaEngine& engine, int frames) {
    std::array<float, 64> left{}, right{};
    while (frames > 0) {
        const int count = std::min(frames, 64);
        engine.RenderBatch(left.data(), right.data(), nullptr, nullptr,
                           nullptr, nullptr, nullptr, count);
        for (int n = 0; n < count; ++n)
            check(std::isfinite(left[n]) && std::isfinite(right[n]), "nonfinite output across model switch");
        frames -= count;
    }
}
double responseEnergy(maremba::FrameBody frame) {
    double energy = 0.0;
    for (int n = 0; n < 512; ++n) {
        const double sample = frame.Process(0.0f);
        energy += sample * sample;
    }
    return energy;
}
}

int main() {
    int fixtures = 0;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int quality = 0; quality < 4; ++quality) {
            const int factor = std::array<int, 4>{{2, 4, 8, 1}}[quality];
            for (bool velocity : {false, true}) {
                auto engine = std::make_unique<maremba::MarembaEngine>(rate);
                maremba::EngineParameters params;
                params.model = static_cast<int>(maremba::EMarimbaModel::BalafonAncestral);
                params.oversampling = quality;
                params.bodyBloom = velocity ? 0.0f : 1.0f;
                params.velocityToBloom = velocity ? 1.0f : 0.0f;
                params.sympathetic = params.strikeJitter = params.artifacts = 0.0f;
                params.farLevel = params.piezoLevel = params.preampDrive = 0.0f;
                params.warmth = params.compAmount = 0.0f;
                params.volume = params.closeLevel = 1.0f;
                engine->SetParameters(params);
                engine->NoteOn(48, .85f);
                render(*engine, static_cast<int>(rate * .05));

                params.model = static_cast<int>(maremba::EMarimbaModel::KalimbaArtisan);
                engine->SetParameters(params);
                auto expected = maremba::MarembaEngineTestAccess::Frame(*engine, velocity);
                const double initialEnergy = responseEnergy(expected);
                check(initialEnergy > 1.e-8, "fixture did not excite the frame");

                // The independent frame has no new drive on the Kalimba path.
                // A short interval also catches replacing decay with a reset.
                constexpr int shortFrames = 16;
                for (int n = 0; n < shortFrames * factor; ++n) expected.Process(0.0f);
                render(*engine, shortFrames);
                auto actual = maremba::MarembaEngineTestAccess::Frame(*engine, velocity);
                double expectedEnergy = 0.0, errorEnergy = 0.0;
                for (int n = 0; n < 512; ++n) {
                    const double reference = expected.Process(0.0f);
                    const double error = actual.Process(0.0f) - reference;
                    expectedEnergy += reference * reference;
                    errorEnergy += error * error;
                }
                check(errorEnergy < expectedEnergy * 1.e-12 + 1.e-24,
                      "muted frame does not follow its zero-input decay");
                render(*engine, static_cast<int>(rate * .5));
                const double tailEnergy = responseEnergy(maremba::MarembaEngineTestAccess::Frame(*engine, velocity));
                check(tailEnergy < initialEnergy * 1.e-10,
                      "inactive frame retains a stale tail");

                params.model = static_cast<int>(maremba::EMarimbaModel::BalafonAncestral);
                engine->SetParameters(params);
                const double returnedEnergy = responseEnergy(maremba::MarembaEngineTestAccess::Frame(*engine, velocity));
                check(returnedEnergy < initialEnergy * 1.e-10,
                      "returning to a bar revives the stale frame");
                render(*engine, 64);
                std::cout << "TRANSITION rate=" << rate << " factor=" << factor
                          << " velocity=" << velocity << " initial_energy=" << initialEnergy
                          << " short_relative_error=" << errorEnergy / expectedEnergy
                          << " final_energy=" << returnedEnergy << '\n';
                ++fixtures;
            }
        }
    }
    std::cout << "FrameBodyModelTransitionTest " << (failures ? "FAILED" : "PASSED")
              << " fixtures=" << fixtures << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
