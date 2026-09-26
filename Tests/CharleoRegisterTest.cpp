// Every box of a Guatemalan marimba carries its charleo, up to the treble
// the melody is played in. With the Mayan Dance Padauk settings (medium
// cord, Buzz 0.57) the buzz is heard at mezzo-forte from G4 to G5 and on
// forte strokes at C6, while soft strokes stay clean across the keyboard.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr double rate = 48000;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
maremba::EngineParameters mayanDance(float buzz) {
    maremba::EngineParameters p;   // Mayan Dance Padauk, as the wrapper maps it; no variance or artifacts
    p.model = 1; p.malletType = 1; p.malletHardness = .5f; p.strikePosition = .5f;
    p.resonatorTune = -8; p.resonatorCoupling = .7f; p.decay = 1; p.buzzAmount = buzz;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.bodyBloom = p.sympathetic = p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    // Measure the fixed physical voicing; VOL range calibration is tested separately.
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.volume = 1;
    return p;
}
std::vector<double> strike(const maremba::EngineParameters& p, int note, float velocity) {
    maremba::MarembaEngine engine(rate);
    auto reference = p;
    // Keep the physical excitation/decay of the original charleo reference:
    // its old 20% attenuation routes are baked in at each test velocity, with
    // the current public VOL routes disabled and tested separately.
    const float physicalVelocity = .01f + .99f * velocity;
    const float referenceScale = 1.f - .20f * (1.f - physicalVelocity);
    reference.malletHardness *= referenceScale;
    reference.decay *= referenceScale;
    engine.SetParameters(reference);
    engine.NoteOn(note, velocity);
    std::vector<double> out;
    std::array<float, 64> l{}, r{}, cl{}, cr{};
    for (size_t done = 0; done < static_cast<size_t>(.4 * rate); done += 64) {
        engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr, nullptr, 64);
        for (int i = 0; i < 64; ++i) out.push_back(cl[i] + cr[i]);
    }
    return out;
}
// The buzz layer: the render with Buzz minus the same stroke without, re the latter.
double layerDb(int note, float velocity) {
    const auto buzz = strike(mayanDance(.5714f), note, velocity), dry = strike(mayanDance(0), note, velocity);
    double e = 0, d = 0;
    for (size_t i = 0; i < dry.size(); ++i) { e += (buzz[i] - dry[i]) * (buzz[i] - dry[i]); d += dry[i] * dry[i]; }
    return 10 * std::log10(std::max(e, 1e-30) / std::max(d, 1e-30));
}
} // namespace

int main() {
    for (int note : {60, 67, 72, 79, 84}) {
        const double soft = layerDb(note, .3f), mf = layerDb(note, .6f), ff = layerDb(note, .9f);
        std::cout << "CHARLEO note=" << note << " soft_db=" << soft << " mf_db=" << mf << " ff_db=" << ff << '\n';
        check(soft < -60, "a soft stroke buzzes");
        if (note <= 79) check(mf > -40, "the charleo is silent at mezzo-forte in the melody register");
        check(ff > -45, "the charleo is silent on a forte stroke");
        if (note <= 72) check(mf < -10, "the charleo buzz swamps the note");
    }
    std::cout << (failures ? "CharleoRegisterTest FAILED" : "CharleoRegisterTest PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
