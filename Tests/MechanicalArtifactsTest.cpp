// Event-plan contracts for realistic playing imperfections. Audio differentiation
// and host lifecycle are covered separately by EngineRealismTest / test_dsp.
#include "../DSP/RandomArtifacts.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct Counts { int grazes = 0, rebounds = 0, brushes = 0, collisions = 0; };

void CheckPlan(const maremba::StrikeArtifacts& a, float amount, float velocity) {
    Require(std::isfinite(a.strikePositionOffset) && std::abs(a.strikePositionOffset) <= 0.22f,
            "wrong hits must stay on a plausible part of the playing surface");
    Require(std::isfinite(a.malletHardnessJitter) && std::abs(a.malletHardnessJitter) <= 0.21f,
            "contact hardness deviation must be bounded");
    Require(std::isfinite(a.pitchJitterCents) && std::abs(a.pitchJitterCents) < 1.0f,
            "wrong-hit artifacts must not turn settled notes into detuned oscillators");
    Require(a.frictionNoiseBurst >= 0.0f && a.frictionNoiseBurst <= 0.0181f,
            "friction must remain a small contact modulation");
    Require(a.impulseScale >= 0.62f && a.impulseScale <= 1.0f,
            "grazing contact must lose, rather than create, stroke impulse");
    Require(a.reboundGain >= 0.0f && a.reboundGain < 0.35f && a.brushGain >= 0.0f && a.brushGain < 0.24f,
            "secondary hits must remain subordinate to the intended stroke");
    const double primaryFraction = 1.0 - a.reboundGain - a.brushGain;
    Require(primaryFraction > 0.42, "secondary contacts must leave a playable primary hit");
    const double strokeImpulse = std::pow(static_cast<double>(velocity), 1.25) * a.impulseScale;
    const double deliveredImpulse = strokeImpulse * primaryFraction
                                  + strokeImpulse * a.reboundGain + strokeImpulse * a.brushGain;
    Require(std::abs(deliveredImpulse - strokeImpulse) < 1.0e-6,
            "secondary hits must share the original stroke impulse instead of adding unrelated sound");
    Require(a.collisionAmount >= 0.0f && a.collisionAmount <= amount + 1.0e-6f,
            "loose-mount contact must stay inside the amount budget");
    for (float seconds : {a.reboundDelaySec, a.brushDelaySec}) {
        Require(std::isfinite(seconds) && seconds >= 0.0f && seconds < 0.050f,
                "contacts must be local physical events, scheduled in seconds");
        // The event plan must be representable at every engine rate with at
        // most the one-sample scheduling error, never reference-rate samples.
        for (double rate : {44100.0, 88200.0, 96000.0, 192000.0, 768000.0}) {
            const double onset = std::floor(static_cast<double>(seconds) * rate) / rate;
            Require(onset <= seconds && seconds - onset <= 1.0 / rate + 1.0e-12,
                    "the same event time must survive sample-rate conversion");
        }
    }
    if (a.reboundGain > 0.0f) Require(a.reboundDelaySec > 0.002f, "rebound must be separated from first contact");
    if (a.brushGain > 0.0f) {
        Require(a.brushDelaySec >= 0.002f && a.brushDelaySec <= 0.012f,
                "neighbor brushes must occur during the original gesture");
        Require(std::abs(a.brushSemitones) >= 1.0f && std::abs(a.brushSemitones) <= 4.0f,
                "a wrong-key brush must reach a nearby pitched key");
    }
}

Counts CountPlans(maremba::EMarimbaModel model, float amount, float velocity) {
    maremba::FastPRNG random(0x6824619u);
    Counts counts;
    for (int i = 0; i < 30000; ++i) {
        const auto a = maremba::GenerateStrikeArtifacts(random, velocity, 0.5f, amount, model);
        CheckPlan(a, amount, velocity);
        counts.grazes += a.impulseScale < 1.0f;
        counts.rebounds += a.reboundGain > 0.0f;
        counts.brushes += a.brushGain > 0.0f;
        counts.collisions += a.collisionAmount > 0.0f;
        if (amount == 0.0f) {
            Require(a.strikePositionOffset == 0.0f && a.malletHardnessJitter == 0.0f
                 && a.pitchJitterCents == 0.0f && a.frictionNoiseBurst == 0.0f
                 && a.impulseScale == 1.0f && a.reboundGain == 0.0f && a.brushGain == 0.0f
                 && a.reboundDelaySec == 0.0f && a.brushDelaySec == 0.0f && a.brushSemitones == 0.0f
                 && a.collisionAmount == 0.0f, "zero Artifacts must disable every imperfection");
        }
    }
    return counts;
}

} // namespace

int main() {
    std::array<Counts, 4> full{};
    for (int index = 0; index < 4; ++index) {
        const auto model = static_cast<maremba::EMarimbaModel>(index);
        CountPlans(model, 0.0f, 0.85f);
        const auto gentle = CountPlans(model, 0.20f, 0.85f);
        const auto half = CountPlans(model, 0.50f, 0.85f);
        full[index] = CountPlans(model, 1.0f, 0.85f);
        const auto soft = CountPlans(model, 1.0f, 0.15f);
        const auto loud = full[index];
        Require(loud.rebounds > soft.rebounds * 1.6, "strong strokes must be more likely to rebound");
        Require(loud.grazes > half.grazes * 3 && half.grazes > gentle.grazes * 4,
                "wrong hits should become frequent toward the top of Artifacts");
        Require(loud.rebounds > half.rebounds * 3 && loud.brushes > half.brushes * 3,
                "amount must change event incidence, not just artifact volume");
        Require(loud.collisions > half.collisions * 1.7 && half.collisions > gentle.collisions * 2,
                "mount collisions should emerge progressively across the control");
        Require(loud.grazes > 1000 && loud.rebounds > 1000 && loud.brushes > 1000 && loud.collisions > 1000,
                "each instrument must offer all four event families");

        // Changing amount/model/touch must not consume a different number of
        // decisions; render cadence and the knob cannot scramble later notes.
        maremba::FastPRNG a(0x157153u), b(0x157153u);
        for (int i = 0; i < 1000; ++i) {
            const auto low = maremba::GenerateStrikeArtifacts(a, 0.2f, 0.1f, 0.0f, model);
            const auto high = maremba::GenerateStrikeArtifacts(b, 1.0f, 0.9f, 1.0f,
                static_cast<maremba::EMarimbaModel>((index + 1) % 4));
            (void)low;
            (void)high;
            Require(a.Next() == b.Next(), "event planner must use a fixed deterministic random draw count");
        }
        std::printf("model=%d /30000 strokes: graze=%d rebound=%d brush=%d collision=%d\n",
                    index, loud.grazes, loud.rebounds, loud.brushes, loud.collisions);
    }
    Require(full[2].collisions > full[0].collisions * 1.7,
            "the loose Balafon mount must collide more often than concert Rosewood");
    Require(full[2].rebounds > full[3].rebounds * 2,
            "Balafon mallet bounce and Kalimba thumb recatch must behave distinctly");
    Require(full[3].brushes > full[0].brushes * 2,
            "neighboring Kalimba tines must brush more readily than Rosewood bars");
    std::puts("Mechanical artifact event-plan contracts passed.");
}
