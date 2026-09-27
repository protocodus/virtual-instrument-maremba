// Ten per-strike VOL routes add a calibrated fraction of the full knob range.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace maremba {
struct CaptureSceneTestAccess {
    static float Emission(const CaptureScene& scene, int id, int lane, int sub) {
        const auto& source = scene.mSource[id];
        // Read the logical emission even while equal ears share storage.
        return source.buffer[lane == 1 && source.equalEars ? 0 : lane][sub];
    }
    static std::array<double, 2> FadeEnergy(const CaptureScene& actual,
                                           const CaptureScene& reference,
                                           const CaptureScene& floor, int factor) {
        std::array<double, 2> energy{};
        // Read the emissions actually supplied by the engine, before either
        // microphone path. Keep sources separate so spatial cancellation
        // cannot turn a correctly fading source into an apparent gain change.
        for (int id = 0; id < kCaptureSourceCount; ++id) {
            for (int lane = 0; lane < 3; ++lane) {
                for (int sub = 0; sub < factor; ++sub) {
                    const double fixed = Emission(floor, id, lane, sub);
                    const double a = Emission(actual, id, lane, sub) - fixed;
                    const double r = Emission(reference, id, lane, sub) - fixed;
                    energy[0] += a * a;
                    energy[1] += r * r;
                }
            }
        }
        return energy;
    }
};
struct MarembaEngineTestAccess {
    static float ControlVelocity(const MarembaEngine& engine, float velocity) { return engine.MapControlVelocity(velocity); }
    static float BaseGain(const MarembaEngine& engine, bool mesh) {
        return mesh ? engine.mSympatheticGain : engine.mBodyGain;
    }
    static bool Running(const MarembaEngine& engine, bool mesh) {
        return mesh ? engine.mMeshRunning : engine.mBodyRunning;
    }
    static KalimbaBody KalimbaSnapshot(const MarembaEngine& engine) { return engine.mKalimbaBody; }
    static bool ExtraBloomRunning(const MarembaEngine& engine) { return engine.mBodyVelocityRunning; }
    static std::array<double, 2> SourceFadeEnergy(const MarembaEngine& actual,
                                                 const MarembaEngine& reference,
                                                 const MarembaEngine& floor) {
        return CaptureSceneTestAccess::FadeEnergy(actual.mMicMixer, reference.mMicMixer,
                                                  floor.mMicMixer, actual.mActiveFactor);
    }
    static void StopVoices(MarembaEngine& engine) {
        for (auto& voice : engine.mVoices) voice.FastKill();
    }
    // Excite only a shared resonance, then leave it to ring without voices.
    // This isolates the tail a live base-knob fade must actually attenuate.
    static void SeedSharedTail(MarembaEngine& engine, bool mesh) {
        engine.mTailsSilent = false;
        const bool tine = engine.mParams.model == static_cast<int>(EMarimbaModel::KalimbaArtisan);
        const double rate = engine.InternalRate();
        const double hz = mesh ? (tine ? 329.6275569 : 523.2511306)
                               : (tine ? 400.0 : GetModelParams(static_cast<EMarimbaModel>(engine.mParams.model)).frameBodyFreq);
        for (int frame = 0; frame < static_cast<int>(rate * .20); ++frame) {
            const double drive = std::sin(6.283185307179586 * hz * frame / rate);
            if (mesh) {
                engine.mSympathetic.BeginSample();
                engine.mSympathetic.AccumulateVoice(60, drive);
                float left, right;
                engine.mSympathetic.Process(1, left, right);
            } else if (tine) {
                KalimbaBoxDrive box;
                box.board = box.air = drive;
                double close, far, plate, side;
                engine.mKalimbaBody.Process(box, .6, .8, close, far, plate, side);
                engine.mKalimbaBodyRunning = true;
            } else {
                engine.mFrameBody.Process(static_cast<float>(drive));
            }
        }
    }
};
} // namespace maremba

namespace {

using maremba::EngineParameters;

int failures = 0;

void check(bool ok, const std::string& message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}

// Direct captures bypass the capture mix coefficients, Main warmth and its
// musical limiter. They retain acoustic propagation, Piezo Drive and their
// independent output headroom protection, held identical in each comparison.
std::vector<float> Render(const EngineParameters& p, float velocity, double seconds,
                          const EngineParameters* changeTo = nullptr, double changeAt = 0.0) {
    auto engine = std::make_unique<maremba::MarembaEngine>(48000.0);
    engine->SetParameters(p);
    engine->NoteOn(60, velocity);
    std::vector<float> out;
    float mainL[64], mainR[64], closeL[64], closeR[64], farL[64], farR[64], piezo[64];
    const int batches = static_cast<int>(seconds * 48000.0 / 64);
    const int changeBatch = static_cast<int>(changeAt * 48000.0 / 64);
    for (int b = 0; b < batches; ++b) {
        if (changeTo && b == changeBatch) engine->SetParameters(*changeTo);
        engine->RenderBatch(mainL, mainR, closeL, closeR, farL, farR, piezo, 64);
        for (int i = 0; i < 64; ++i) out.push_back(closeL[i] + closeR[i] + farL[i] + farR[i] + piezo[i]);
    }
    return out;
}

double Rms(const std::vector<float>& x) {
    double sum = 0;
    for (float v : x) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / std::max<std::size_t>(x.size(), 1));
}

double WindowRms(const std::vector<float>& x, int first, int last) {
    double sum = 0;
    for (int frame = first; frame < last; ++frame) sum += static_cast<double>(x[frame]) * x[frame];
    return std::sqrt(sum / (last - first));
}

float RenderFrame(maremba::MarembaEngine& engine) {
    float mainL, mainR, closeL, closeR;
    engine.RenderBatch(&mainL, &mainR, &closeL, &closeR, nullptr, nullptr, nullptr, 1);
    return closeL + closeR;
}

double KalimbaExtraTail(maremba::KalimbaBody body) {
    auto plain = body;
    double peak = 0;
    for (int frame = 0; frame < 256; ++frame) {
        double close, far, plate, side, plainClose, plainFar, plainPlate, plainSide;
        body.Process({}, 0, 1, close, far, plate, side);
        plain.Process({}, 0, 0, plainClose, plainFar, plainPlate, plainSide);
        peak = std::max(peak, std::abs(close - plainClose) + std::abs(far - plainFar));
    }
    return peak;
}

// RMS of the difference relative to the RMS of a.
double Difference(const std::vector<float>& a, const std::vector<float>& b) {
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        sum += d * d;
    }
    const double reference = Rms(a);
    return reference > 0 ? std::sqrt(sum / a.size()) / reference : std::sqrt(sum);
}

struct Route {
    const char* name;
    float EngineParameters::*depth;
    float EngineParameters::*knob;
    float value;
    float minimum, range;
    bool exact;   // the knob only reaches the strike itself: bit-identical
};

} // namespace

// Independent UI-range oracle: the selected curve keeps MIDI 1 at zero and
// MIDI 127 at one. The 1% physical strike floor is deliberately absent.
float VelocityTravel(int curve, float velocity) {
    const auto shape = [curve](double v) {
        if (curve == 0) return std::sqrt(v);
        if (curve == 2) return std::pow(v, 1.55);
        if (curve == 3) return v * v * (3.0 - 2.0 * v);
        return v;
    };
    const double travel = velocity <= 1.f / 127.f ? 0.0
        : std::clamp((static_cast<double>(velocity) * 127.0 - 1.0) / 126.0, 0.0, 1.0);
    return static_cast<float>(shape(travel));
}

int main() {
    const Route routes[] = {
        {"Hardness", &EngineParameters::velocityToHardness, &EngineParameters::malletHardness, .25f, 0, 1, true},
        {"Position", &EngineParameters::velocityToPosition, &EngineParameters::strikePosition, .20f, 0, 1, true},
        {"Decay", &EngineParameters::velocityToDecay, &EngineParameters::decay, 1.0f, .1f, 7.9f, true},
        {"Carving", &EngineParameters::velocityToCarving, &EngineParameters::carving, .25f, 0, 1, true},
        {"Coupling", &EngineParameters::velocityToCoupling, &EngineParameters::resonatorCoupling, .25f, 0, 1, false},
        {"Bloom", &EngineParameters::velocityToBloom, &EngineParameters::bodyBloom, .20f, 0, 1, false},
        {"Sympathetic", &EngineParameters::velocityToSympathetic, &EngineParameters::sympathetic, .20f, 0, 1, false},
        {"Buzz", &EngineParameters::velocityToBuzz, &EngineParameters::buzzAmount, .20f, 0, 1, true},
        {"Tension", &EngineParameters::velocityToTension, &EngineParameters::membraneTension, .25f, 0, 1, true},
        {"Artifacts", &EngineParameters::velocityToArtifacts, &EngineParameters::artifacts, .20f, 0, 1, true},
    };
    const char* models[] = {"Rosewood", "Padauk", "Balafon", "Kalimba"};
    struct Case { float base, depth, velocity; };
    // MIDI 1 must preserve the base even with maximum VOL; MIDI 127 adds
    // exactly half/full range. Include zero base and exhausted headroom.
    const Case cases[] = {
        {.20f, 1.0f, 1.0f / 127.0f},
        {0.0f, .50f, 1.0f},
        {0.0f, 1.0f, 1.0f},
        {.20f, .50f, 1.0f},
        {.80f, .50f, 1.0f},
        {.20f, 0.0f, .60f},
    };
    // Numeric calibration isolates all three labelled positions from the
    // physical strike's necessary nonzero energy at minimum MIDI velocity.
    auto probe = std::make_unique<maremba::MarembaEngine>(48000.0);
    for (int curve = 0; curve < 4; ++curve) {
        EngineParameters p; p.velocityCurve = curve; probe->SetParameters(p);
        float previous = -1.f;
        for (int midi = 1; midi <= 127; ++midi) {
            const float velocity = midi / 127.f;
            const float actual = maremba::MarembaEngineTestAccess::ControlVelocity(*probe, velocity);
            const float expected = VelocityTravel(curve, velocity);
            check(std::abs(actual - expected) < 2e-6f, "numeric VOL curve mismatch");
            check(actual > previous, "VOL curve is not strictly increasing over MIDI velocities");
            previous = actual;
            if (midi == 1) check(actual == 0, "MIDI 1 must produce exactly zero VOL travel");
            if (midi == 127) check(actual == 1, "MIDI 127 must produce exactly full VOL travel");
        }
    }
    int comparisons = 0;
    for (const Route& route : routes) {
        bool audibleSomewhere = false;
        for (int model = 0; model < 4; ++model) {
            const std::string where = std::string(route.name) + " on " + models[model];
            EngineParameters p;
            p.model = model;
            p.velocityCurve = 1;
            p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
            p.buzzAmount = .7f;
            p.*(route.knob) = route.value;
            for (const auto& item : cases) {
                auto routed = p, expected = p;
                routed.*(route.knob) = route.minimum + route.range * item.base;
                routed.*(route.depth) = item.depth;
                expected.*(route.knob) = std::min(route.minimum + route.range,
                    routed.*(route.knob) + route.range * item.depth * VelocityTravel(1, item.velocity));
                const auto actual = Render(routed, item.velocity, .30);
                const auto reference = Render(expected, item.velocity, .30);
                const double error = Difference(reference, actual);
                check(Rms(actual) > 0, where + ": silent");
                check(route.exact ? error == 0 : error < 1e-4,
                      where + ": endpoint/zero-base/cap calibration mismatch, base=" + std::to_string(item.base)
                      + " depth=" + std::to_string(item.depth) + " velocity=" + std::to_string(item.velocity)
                      + " error=" + std::to_string(error));
                ++comparisons;
            }
            // All four response curves at an interior velocity; use the same
            // physical velocity in actual/reference so timbral response remains.
            for (int curve = 0; curve < 4; ++curve) {
                auto routed = p, expected = p;
                routed.velocityCurve = expected.velocityCurve = curve;
                routed.*(route.depth) = .50f;
                expected.*(route.knob) = std::min(route.minimum + route.range,
                    p.*(route.knob) + route.range * .50f * VelocityTravel(curve, .60f));
                const auto actual = Render(routed, .60f, .30);
                const double error = Difference(Render(expected, .60f, .30), actual);
                // Float curve rounding can reach the physical modal parameters;
                // the tolerance is well below an audible difference.
                check(error < 1e-4, where + ": curved midpoint calibration mismatch curve="
                      + std::to_string(curve) + " error=" + std::to_string(error));
                if (curve == 1) audibleSomewhere = audibleSomewhere || Difference(Render(p, .60f, .30), actual) > 1e-3;
                ++comparisons;
            }
        }
        check(audibleSomewhere, std::string(route.name) + ": the route changes no instrument's sound");
    }

    // Buzz and Tension follow live knob movement with the offset captured at
    // the strike; later changes to VOL apply to the next strike only.
    constexpr float velocity = .6f;
    const float offset = .5f * VelocityTravel(1, velocity);
    for (int model = 0; model < 4; ++model) {
        for (bool tension : {false, true}) {
            const std::string where = std::string(tension ? "live Tension" : "live Buzz") + " on " + models[model];
            EngineParameters p;
            p.model = model;
            p.velocityCurve = 1;
            p.buzzAmount = .4f;
            p.membraneTension = .3f;
            float EngineParameters::*knob = tension ? &EngineParameters::membraneTension : &EngineParameters::buzzAmount;
            float EngineParameters::*depth = tension ? &EngineParameters::velocityToTension : &EngineParameters::velocityToBuzz;
            EngineParameters routed = p;
            routed.*depth = .5f;
            EngineParameters routedLater = routed;
            routedLater.*knob = .9f;
            routedLater.*depth = 0; // must not rewrite the already-ringing strike
            EngineParameters expected = p;
            expected.*knob = std::min(1.f, p.*knob + offset);
            EngineParameters expectedLater = p;
            expectedLater.*knob = 1.f;
            check(Difference(Render(routed, velocity, 1.0, &routedLater, .3),
                             Render(expected, velocity, 1.0, &expectedLater, .3)) < 1e-4,
                  where + ": ringing strike loses its offset or fails to cap when the knob moves");
        }
    }

    // A live base knob must attenuate stored resonance, including reductions
    // that stop above zero. Fresh velocity drive must not reopen the old tail.
    using Access = maremba::MarembaEngineTestAccess;
    int sharedFadeCases = 0;
    for (int model = 0; model < 4; ++model) {
        for (bool mesh : {false, true}) {
            const std::string where = std::string(mesh ? "Sympathetic" : "Bloom") + " tail on " + models[model];
            float EngineParameters::*knob = mesh ? &EngineParameters::sympathetic : &EngineParameters::bodyBloom;
            float EngineParameters::*depth = mesh ? &EngineParameters::velocityToSympathetic : &EngineParameters::velocityToBloom;
            EngineParameters p;
            p.model = model;
            p.bodyBloom = p.sympathetic = p.farLevel = p.piezoLevel = 0;
            p.closeLevel = 1;
            for (float base : {1.f, .05f, .0001f}) {
                p.*knob = base;
                auto fading = std::make_unique<maremba::MarembaEngine>(48000);
                auto reference = std::make_unique<maremba::MarembaEngine>(48000);
                auto floor = std::make_unique<maremba::MarembaEngine>(48000);
                for (auto* engine : {fading.get(), reference.get(), floor.get()}) {
                    auto start = p;
                    if (engine == reference.get()) start.*knob = 1;
                    if (engine == floor.get()) start.*knob = 0;
                    engine->SetParameters(start);
                    engine->Reset();
                    Access::SeedSharedTail(*engine, mesh);
                }
                auto off = p;
                off.*knob = 0;
                fading->SetParameters(off);
                std::vector<float> actual, held;
                std::array<double, 2> sourceEnergy{};
                int stopFrame = -1;
                float beforeStop = 1;
                for (int frame = 0; frame < 3360; ++frame) {
                    const float before = Access::BaseGain(*fading, mesh);
                    // Kalimba retains a fixed box resonance at Bloom zero;
                    // subtract that identical floor to isolate the knob.
                    const float fixed = RenderFrame(*floor);
                    actual.push_back(RenderFrame(*fading) - fixed);
                    held.push_back(RenderFrame(*reference) - fixed);
                    if (frame >= 480 && frame < 720) {
                        const auto energy = Access::SourceFadeEnergy(*fading, *reference, *floor);
                        sourceEnergy[0] += energy[0];
                        sourceEnergy[1] += energy[1];
                    }
                    if (stopFrame < 0 && !Access::Running(*fading, mesh)) {
                        stopFrame = frame;
                        beforeStop = before;
                    }
                }
                const auto ratio = [&](int first, int last) {
                    const double level = WindowRms(held, first, last);
                    check(level > 1e-15, where + ": seeded reference tail is inaudible");
                    return WindowRms(actual, first, last) / std::max(level, 1e-30);
                };
                // The 10–15 ms bound is a source-fade contract. Close now has
                // real flight time and a measured head/ear impulse response,
                // followed by the common 2 ms output delay: a listener at
                // this time still receives sound emitted earlier in the fade.
                // Inspect actual source audio rather than the gain variable;
                // retain the separate delivered-audio settling gate below.
                check(std::isfinite(sourceEnergy[0]) && std::isfinite(sourceEnergy[1]) && sourceEnergy[1] > 1e-30,
                      where + ": seeded source reference tail is invalid or inaudible");
                const double sourceRatio = std::sqrt(sourceEnergy[0] / std::max(sourceEnergy[1], 1e-30));
                check(sourceRatio < .20 * base, where + ": stored source tail does not fade with the base knob");
                const double lateRatio = ratio(1680, 1920);
                check(lateRatio < .002,
                      where + ": quiet tail is not sufficiently attenuated");
                check(stopFrame >= 0 && stopFrame < 3360 && beforeStop < .0011f,
                      where + ": bypass resets before the output fade completes, base=" + std::to_string(base));
                check(Access::BaseGain(*fading, mesh) == 0, where + ": fully disabled branch does not settle to zero");
                std::cout << std::setprecision(9) << "FADE model=" << model << " route=" << (mesh ? "Sympathetic" : "Bloom")
                          << " base=" << base << " source_10_15ms_over_base=" << sourceRatio / base
                          << " close_10_15ms_over_base=" << ratio(480, 720) / base
                          << " close_35_40ms=" << lateRatio << '\n';
                ++sharedFadeCases;
            }

            // Turning 100% down to 25% must also quiet energy already stored
            // in a high-Q mesh; input weighting alone leaves that tail loud.
            p.*knob = 1;
            auto reduced = std::make_unique<maremba::MarembaEngine>(48000);
            auto held = std::make_unique<maremba::MarembaEngine>(48000);
            auto floor = std::make_unique<maremba::MarembaEngine>(48000);
            for (auto* engine : {reduced.get(), held.get(), floor.get()}) {
                auto start = p;
                if (engine == floor.get()) start.*knob = 0;
                engine->SetParameters(start);
                engine->Reset();
                Access::SeedSharedTail(*engine, mesh);
            }
            p.*knob = .25f;
            reduced->SetParameters(p);
            std::vector<float> reducedTail, heldTail;
            for (int frame = 0; frame < 2880; ++frame) {
                const float fixed = RenderFrame(*floor);
                reducedTail.push_back(RenderFrame(*reduced) - fixed);
                heldTail.push_back(RenderFrame(*held) - fixed);
            }
            const double heldLevel = WindowRms(heldTail, 2400, 2880);
            check(heldLevel > 1e-15, where + ": partial-fade reference tail is inaudible");
            const double reducedRatio = WindowRms(reducedTail, 2400, 2880) / std::max(heldLevel, 1e-30);
            check(std::abs(reducedRatio - .25) < .02,
                  where + ": a nonzero live base reduction leaves the stored tail too loud");
            ++sharedFadeCases;

            // Reopening the base reverses its fade smoothly. A velocity note
            // excites a separate path and must leave the base fade alone.
            for (bool velocityStrike : {false, true}) {
                p.*knob = 1;
                p.*depth = 0;
                auto engine = std::make_unique<maremba::MarembaEngine>(48000);
                engine->SetParameters(p);
                engine->Reset();
                Access::SeedSharedTail(*engine, mesh);
                p.*knob = 0;
                engine->SetParameters(p);
                for (int frame = 0; frame < 480; ++frame) RenderFrame(*engine);
                const float before = Access::BaseGain(*engine, mesh);
                check(before > .1f && before < .2f && Access::Running(*engine, mesh),
                      where + ": interrupted-fade fixture has no partly faded tail");
                if (velocityStrike) {
                    p.*depth = .5f;
                    engine->SetParameters(p);
                    engine->NoteOn(60, 1);
                } else {
                    p.*knob = 1;
                    engine->SetParameters(p);
                }
                check(Access::BaseGain(*engine, mesh) == before, where + ": reopening snaps a stored tail to full volume");
                RenderFrame(*engine);
                const float after = Access::BaseGain(*engine, mesh);
                check(std::abs(after - before) < .01f && (velocityStrike ? after < before : after > before),
                      where + ": base fade jumps or follows the new velocity excitation incorrectly");
                p.*depth = 0;
                ++sharedFadeCases;
            }

            // Inspect the attack itself: a fresh zero-base route must have its
            // complete calibrated gain from the first note, without fading in.
            p.*knob = 0;
            p.*depth = .5f;
            auto expected = p;
            expected.*knob = .5f;
            expected.*depth = 0;
            check(Difference(Render(expected, 1, .02), Render(p, 1, .02)) < 1e-4,
                  where + ": first zero-base VOL strike fades in below its calibrated amount");
        }
    }
    std::cout << "Velocity calibration: " << comparisons << " all-model endpoint and curve comparisons\n";
    std::cout << "Shared resonance control: " << sharedFadeCases << " full/tiny/partial-base and interrupted fades; 8 fresh-attack comparisons\n";

    // Reaching full base Bloom closes the extra path. Its stored Kalimba
    // resonance must not return later, and clearing it must leave the plain
    // board/air sound untouched.
    {
        EngineParameters p;
        p.model = 3;
        p.bodyBloom = .2f;
        p.velocityToBloom = .5f;
        auto engine = std::make_unique<maremba::MarembaEngine>(48000);
        engine->SetParameters(p);
        engine->NoteOn(60, 1);
        for (int frame = 0; frame < 2400; ++frame) RenderFrame(*engine);
        Access::StopVoices(*engine);
        auto ringing = Access::KalimbaSnapshot(*engine);
        check(KalimbaExtraTail(ringing) > 1e-8, "Kalimba extra-tail fixture has no velocity resonance");
        auto cleared = ringing;
        cleared.ResetBloom();
        check(KalimbaExtraTail(cleared) == 0, "Kalimba ResetBloom leaves stored velocity resonance");
        double plainEnergy = 0;
        bool plainUnchanged = true;
        for (int frame = 0; frame < 256; ++frame) {
            double close, far, plate, side, clearClose, clearFar, clearPlate, clearSide;
            ringing.Process({}, 1, 0, close, far, plate, side);
            cleared.Process({}, 1, 0, clearClose, clearFar, clearPlate, clearSide);
            plainEnergy += close * close + far * far + plate * plate;
            plainUnchanged = plainUnchanged && close == clearClose && far == clearFar
                && plate == clearPlate && side == clearSide;
        }
        check(plainEnergy > 1e-12 && plainUnchanged, "Kalimba ResetBloom changes the plain board or air tail");
        p.bodyBloom = 1;
        engine->SetParameters(p);
        for (int frame = 0; frame < 3360; ++frame) RenderFrame(*engine);
        check(!Access::ExtraBloomRunning(*engine) && KalimbaExtraTail(Access::KalimbaSnapshot(*engine)) == 0,
              "Kalimba reaching full base Bloom does not clear the extra branch");
        p.bodyBloom = .2f;
        engine->SetParameters(p);
        for (int frame = 0; frame < 480; ++frame) RenderFrame(*engine);
        check(Access::ExtraBloomRunning(*engine) && KalimbaExtraTail(Access::KalimbaSnapshot(*engine)) == 0,
              "Kalimba lowering Bloom reopens an old velocity tail");
    }
    std::cout << "Kalimba extra resonance: full-base close/reopen and plain-body preservation checked\n";

    // A stored Bow (1.4.0b1 beta only) plays as the Shaft.
    for (int model = 0; model < 4; ++model) {
        EngineParameters shaft;
        shaft.model = model;
        shaft.malletType = maremba::kStrikerShaft;
        EngineParameters legacy = shaft;
        legacy.malletType = 5;
        check(Difference(Render(shaft, .7f, .5), Render(legacy, .7f, .5)) == 0,
              std::string("a stored Bow does not play as the Shaft on ") + models[model]);
    }

    if (failures) {
        std::cerr << "VelocityRoutesTest FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "VelocityRoutesTest PASSED\n";
    return 0;
}
