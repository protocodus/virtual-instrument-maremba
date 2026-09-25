// Seven more per-strike velocity routes (1.4.0): Carving, Coupling, Bloom,
// Sympathetic, Buzz, Tension and Artifacts follow the law of Hardness,
// Position and Decay. A strike at mapped velocity v with depth d plays as the
// same strike with no route and the knob at value * (1 - d (1 - v)); full
// velocity reaches the knob at every depth. Bloom and Sympathetic scale the
// strike's drive into the shared body (the kalimba's box) and the ringing-
// along keys; Buzz and Tension keep the strike's factor when the knobs move
// while it rings. The removed Bow's stored value plays as the Shaft.
#include "MarembaEngine.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static float MapVelocity(const MarembaEngine& engine, float velocity) { return engine.MapVelocity(velocity); }
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

// The dry direct outputs (close, far, piezo) bypass the preamp and limiter,
// so the linear shared resonators are compared without their nonlinearity.
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
    bool exact;   // the knob only reaches the strike itself: bit-identical
};

} // namespace

int main() {
    const Route routes[] = {
        {"Carving", &EngineParameters::velocityToCarving, &EngineParameters::carving, .8f, true},
        {"Coupling", &EngineParameters::velocityToCoupling, &EngineParameters::resonatorCoupling, .8f, false},
        {"Bloom", &EngineParameters::velocityToBloom, &EngineParameters::bodyBloom, .7f, false},
        {"Sympathetic", &EngineParameters::velocityToSympathetic, &EngineParameters::sympathetic, .7f, false},
        {"Buzz", &EngineParameters::velocityToBuzz, &EngineParameters::buzzAmount, .6f, true},
        {"Tension", &EngineParameters::velocityToTension, &EngineParameters::membraneTension, .7f, true},
        {"Artifacts", &EngineParameters::velocityToArtifacts, &EngineParameters::artifacts, .8f, true},
    };
    const char* models[] = {"Rosewood", "Padauk", "Balafon", "Kalimba"};
    const float velocity = .35f;
    EngineParameters linear;
    linear.velocityCurve = 1;
    const auto probe = std::make_unique<maremba::MarembaEngine>(48000.0);
    probe->SetParameters(linear);
    // The engine's law at depth 1 on the strike's mapped velocity.
    const float scale = 1.0f - 1.0f * (1.0f - maremba::MarembaEngineTestAccess::MapVelocity(*probe, velocity));

    for (const Route& route : routes) {
        bool audibleSomewhere = false;
        for (int model = 0; model < 4; ++model) {
            const std::string where = std::string(route.name) + " on " + models[model];
            EngineParameters p;
            p.model = model;
            p.velocityCurve = 1;
            p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0.0f;
            p.buzzAmount = .3f;
            p.*(route.knob) = route.value;

            EngineParameters routed = p;
            routed.*(route.depth) = 1.0f;
            EngineParameters scaled = p;
            scaled.*(route.knob) = route.value * scale;

            const auto soft = Render(routed, velocity, 1.2);
            const auto equivalent = Render(scaled, velocity, 1.2);
            const auto fixed = Render(p, velocity, 1.2);
            const double error = Difference(soft, equivalent);
            const double effect = Difference(fixed, soft);
            std::cout << "ROUTE " << route.name << ' ' << models[model] << " equivalence=" << error
                      << " effect=" << effect << '\n';
            check(Rms(soft) > 0, where + ": silent");
            check(route.exact ? error == 0 : error < 1e-4,
                  where + ": a soft strike does not play as the knob scaled by its velocity");
            audibleSomewhere = audibleSomewhere || effect > 1e-3;

            // Full velocity reaches the knob at every depth.
            check(Difference(Render(p, 1.0f, .5), Render(routed, 1.0f, .5)) == 0,
                  where + ": full velocity does not reach the knob");
        }
        check(audibleSomewhere, std::string(route.name) + ": the route changes no instrument's sound");
    }

    // Buzz and Tension follow their knobs while a strike rings, each with the
    // factor its strike's velocity gave it.
    for (int model : {1, 2, 3}) {
        for (bool tension : {false, true}) {
            const std::string where = std::string(tension ? "live Tension" : "live Buzz") + " on " + models[model];
            EngineParameters p;
            p.model = model;
            p.velocityCurve = 1;
            p.buzzAmount = .4f;
            p.membraneTension = .6f;
            float EngineParameters::*knob = tension ? &EngineParameters::membraneTension : &EngineParameters::buzzAmount;
            EngineParameters routed = p;
            (tension ? routed.velocityToTension : routed.velocityToBuzz) = 1.0f;
            EngineParameters routedLater = routed;
            routedLater.*knob = .9f;
            EngineParameters scaled = p;
            scaled.*knob = p.*knob * scale;
            EngineParameters scaledLater = p;
            scaledLater.*knob = .9f * scale;
            check(Difference(Render(routed, velocity, 1.0, &routedLater, .3),
                             Render(scaled, velocity, 1.0, &scaledLater, .3)) == 0,
                  where + ": the ringing strike loses its velocity factor when the knob moves");
        }
    }

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
