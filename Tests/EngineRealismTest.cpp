// Public-audio regression tests for mechanical artifacts and contact pickup.
// Uses no private DSP state, coefficient values, or implementation event flags.
// Compile: clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP \
//   Tests/EngineRealismTest.cpp DSP/MarembaVoice.cpp -o /tmp/maremba-realism
#include "MarembaVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Settings {
    int model = 0, note = 48, striker = 1;
    float velocity = .85f, hardness = .4f, position = .35f;
    float coupling = .7f, decay = 1.f, buzz = 0.f, artifacts = 0.f;
    double rate = 96000., seconds = .24;
    uint32_t seed = 0x438721u;
};
struct Audio {
    std::vector<double> close, piezo;
    double rate;
};
Audio render(const Settings& s) {
    maremba::MarembaVoice voice;
    voice.SetSeed(s.seed ^ 0x593817u);
    maremba::FastPRNG random(s.seed);
    voice.Trigger(s.note, s.velocity, static_cast<maremba::EMarimbaModel>(s.model),
                  s.hardness, s.position, 0, s.coupling, s.decay, s.buzz,
                  s.artifacts, 0, s.rate, random, 0, 0, s.striker, 0);
    Audio a;
    a.rate = s.rate;
    a.close.resize(static_cast<size_t>(s.rate * s.seconds));
    a.piezo.resize(a.close.size());
    for (size_t i = 0; i < a.close.size(); ++i) {
        float close = 0, far = 0, piezo = 0;
        voice.ProcessSample(close, far, piezo);
        a.close[i] = close;
        a.piezo[i] = piezo;
        if ((i & 31u) == 31u) voice.UpdateEnvelope();
    }
    return a;
}
double energy(const std::vector<double>& v, size_t begin, size_t end) {
    double e = 0;
    for (size_t i = begin; i < end; ++i) e += v[i] * v[i];
    return e;
}
double db(double value) { return 10 * std::log10(std::max(value, 1e-30)); }
double residual(const std::vector<double>& x, const std::vector<double>& y,
                size_t begin, size_t end) {
    double ex = 0, ey = 0, xy = 0;
    for (size_t i = begin; i < end; ++i) {
        ex += x[i] * x[i]; ey += y[i] * y[i]; xy += x[i] * y[i];
    }
    return std::max(0., 1 - xy * xy / std::max(ex * ey, 1e-30));
}
double highFraction(const Audio& a) {
    // A single broad audio band distinguishes timbre movement from volume.
    const double alpha = 1 - std::exp(-2 * pi * 1000 / a.rate);
    double low = 0, highEnergy = 0, allEnergy = 0;
    const size_t end = static_cast<size_t>(.018 * a.rate);
    for (size_t i = 0; i < end; ++i) {
        low += alpha * (a.close[i] - low);
        highEnergy += (a.close[i] - low) * (a.close[i] - low);
        allEnergy += a.close[i] * a.close[i];
    }
    return db(highEnergy / std::max(allEnergy, 1e-30));
}
bool finite(const Audio& a) {
    for (const auto* v : {&a.close, &a.piezo})
        for (double x : *v) if (!std::isfinite(x) || std::abs(x) > 4) return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const bool measure = argc == 2 && std::string(argv[1]) == "--measure";
    if (argc > 1 && !measure) return 2;
    int failures = 0;
    auto check = [&](bool condition, const std::string& message) {
        if (!condition) {
            if (!measure) ++failures;
            std::cerr << (measure ? "OBSERVATION " : "FAIL ") << message << '\n';
        }
    };
    std::cout << std::setprecision(8);

    // A contact pickup remains distinguishable after gain matching when the
    // air resonator and membrane are absent. Otherwise those layers alone
    // could conceal a pickup that is just a copy of the acoustic bar signal.
    double minPickupResidual = 1;
    for (int model = 0; model < 4; ++model)
        for (int note : {48, 60, 72})
            for (double rate : {88200., 96000., 192000.}) {
                Settings s; s.model = model; s.note = note; s.rate = rate;
                s.coupling = 0;
                const auto a = render(s);
                const double difference = residual(a.close, a.piezo, 0,
                                                   static_cast<size_t>(.080 * rate));
                minPickupResidual = std::min(minPickupResidual, difference);
                check(finite(a), "non-finite/unbounded pickup model=" + std::to_string(model));
                check(energy(a.piezo, 0, a.piezo.size()) > 1e-9, "inaudible pickup");
                check(difference > .01, "pickup duplicates close after gain matching model="
                      + std::to_string(model) + " note=" + std::to_string(note));
                std::cout << "PICKUP model=" << model << " note=" << note << " rate=" << rate
                          << " gain_fit_residual=" << difference << '\n';
            }

    // A fixed ensemble avoids a probability test that can randomly miss a rare
    // imperfect hit. Compare wet-minus-clean output, with equal input and seed.
    // The added layer must carry substantial low-frequency mechanical energy,
    // and continue beyond the immediate noise burst of the main impact.
    for (int model = 0; model < 4; ++model) {
        double differenceEnergy = 0, lowEnergy = 0, delayedEnergy = 0, cleanEnergy = 0;
        double earlyEnergy = 0;
        for (uint32_t hit = 0; hit < 64; ++hit) {
            Settings s; s.model = model; s.seed += 0x9e3779b9u * hit;
            s.seconds = .18;
            const auto clean = render(s);
            s.artifacts = 1;
            const auto wet = render(s);
            check(finite(wet), "non-finite/unbounded artifacts model=" + std::to_string(model));
            if (hit == 0) {
                const auto replay = render(s);
                check(wet.close == replay.close && wet.piezo == replay.piezo,
                      "artifact rendering is not deterministic");
            }
            const double alpha = 1 - std::exp(-2 * pi * 1500 / s.rate);
            double low = 0;
            for (size_t i = 0; i < clean.close.size(); ++i) {
                const double d = wet.close[i] - clean.close[i];
                low += alpha * (d - low);
                differenceEnergy += d * d;
                lowEnergy += low * low;
                cleanEnergy += clean.close[i] * clean.close[i];
                if (i < .012 * s.rate) earlyEnergy += d * d;
                if (i >= .012 * s.rate && i < .15 * s.rate) delayedEnergy += d * d;
            }
        }
        const double lowFraction = lowEnergy / std::max(differenceEnergy, 1e-30);
        const double delayedFraction = delayedEnergy / std::max(differenceEnergy, 1e-30);
        const double audible = differenceEnergy / std::max(cleanEnergy, 1e-30);
        check(audible > .01, "artifacts have no substantial audible effect");
        check(lowFraction > .45, "artifacts predominantly add broadband noise model=" + std::to_string(model));
        check(delayedFraction > .20, "artifacts contain only the initial impact model=" + std::to_string(model));
        std::cout << "ARTIFACT model=" << model << " delta_to_clean_db=" << db(audible)
                  << " low_band_fraction=" << lowFraction << " delayed_fraction=" << delayedFraction
                  << " delayed_to_impact_db=" << db(delayedEnergy / std::max(earlyEnergy, 1e-30)) << '\n';
    }

    // Matched deterministic imperfect strokes must retain their level when the
    // host rate/quality changes; this catches impulses whose area depends on dt.
    double maximumArtifactRateError = 0, maximumArtifactDeltaRateError = 0;
    constexpr std::array<double, 3> rates = {96000., 88200., 192000.};
    for (int model = 0; model < 4; ++model)
        for (int note : {48, 72, 96}) {
            std::array<std::array<double, 2>, 3> deltaEnergy{};
            for (uint32_t hit = 0; hit < 8; ++hit) {
                Settings s; s.model = model; s.note = note;
                s.seed += 0x9e3779b9u * hit;
                std::array<double, 2> referenceEnergy{};
                for (size_t rateIndex = 0; rateIndex < rates.size(); ++rateIndex) {
                    s.rate = rates[rateIndex];
                    s.artifacts = 0;
                    const auto clean = render(s);
                    s.artifacts = 1;
                    const auto wet = render(s);
                    check(finite(wet), "artifact sample-rate render is non-finite/unbounded");
                    for (int channel = 0; channel < 2; ++channel) {
                        const auto& a = channel ? clean.piezo : clean.close;
                        const auto& b = channel ? wet.piezo : wet.close;
                        double differenceEnergy = 0;
                        for (size_t i = 0; i < a.size(); ++i)
                            differenceEnergy += (b[i] - a[i]) * (b[i] - a[i]);
                        deltaEnergy[rateIndex][channel] += differenceEnergy / a.size();
                        const double wetEnergy = energy(b, 0, b.size()) / b.size();
                        if (rateIndex == 0) referenceEnergy[channel] = wetEnergy;
                        else {
                            const double error = std::abs(db(wetEnergy / referenceEnergy[channel]));
                            maximumArtifactRateError = std::max(maximumArtifactRateError, error);
                            check(error < 1, "artifact level varies by more than 1dB across rates");
                        }
                    }
                }
            }
            // Check the added layer separately so a strong main note cannot
            // mask rate-dependent collisions. Matched ensembles stabilize the
            // small contact-noise contribution without inspecting event flags.
            for (size_t rateIndex = 1; rateIndex < rates.size(); ++rateIndex)
                for (int channel = 0; channel < 2; ++channel) {
                    const double error = std::abs(db(deltaEnergy[rateIndex][channel] /
                                                    deltaEnergy[0][channel]));
                    maximumArtifactDeltaRateError = std::max(maximumArtifactDeltaRateError, error);
                    check(error < 1, "wet-minus-clean artifact layer varies by more than 1dB across rates model="
                          + std::to_string(model) + " note=" + std::to_string(note));
                    std::cout << "ARTIFACT_RATE model=" << model << " note=" << note
                              << " rate=" << rates[rateIndex] << " channel=" << channel
                              << " delta_error_db=" << error << '\n';
                }
        }

    // Parameters must alter the relevant acoustic behavior on every model.
    // The response fingerprint is independent of the models' nominal gain.
    std::array<std::array<double, 4>, 4> fingerprints{};
    for (int model = 0; model < 4; ++model) {
        Settings s; s.model = model; s.seconds = .8;
        // Tuned bar cavities follow C3; the fixed kalimba box responds most
        // strongly near C4. Its extended bass register must not be forced into
        // the same loading merely to emulate the former note-following tube.
        s.note = model == 3 ? 60 : 48;
        s.hardness = .1f; const auto soft = render(s);
        s.hardness = .9f; const auto hard = render(s);
        s.hardness = .4f;
        s.position = 0.f; const auto positionStart = render(s);
        s.position = 1.f; const auto positionEnd = render(s);
        s.position = .35f;
        s.coupling = 0; const auto uncoupled = render(s);
        s.coupling = 1; const auto coupled = render(s);
        s.coupling = .7f;
        s.decay = .5f; const auto shortRing = render(s);
        s.decay = 2; const auto longRing = render(s);
        const size_t from = static_cast<size_t>(.02 * s.rate), to = coupled.close.size();
        const size_t late = static_cast<size_t>(.4 * s.rate);
        const size_t attack = static_cast<size_t>(.008 * s.rate);
        const double hardness = highFraction(hard) - highFraction(soft);
        const double hardnessResidual = residual(soft.close, hard.close, 0, attack);
        const double position = residual(positionStart.close, positionEnd.close, 0, attack);
        const double coupling = db(energy(coupled.close, from, to) / energy(uncoupled.close, from, to));
        const double couplingResidual = residual(uncoupled.close, coupled.close, from, to);
        const double decay = db(energy(longRing.close, late, longRing.close.size()) /
                                energy(shortRing.close, late, shortRing.close.size()));
        fingerprints[model] = {hardness / 10, position, coupling / 10, decay / 50};
        check(hardnessResidual > .01, "hardness has no contact response model=" + std::to_string(model));
        check(position > .002, "position has no timbral response model=" + std::to_string(model));
        check(std::abs(coupling) > .1 || couplingResidual > .002,
              "coupling has no resonator response model=" + std::to_string(model));
        check(decay > 3, "decay has no sustain response model=" + std::to_string(model));
        std::cout << "RESPONSE model=" << model << " hardness_brightness_db=" << hardness
                  << " hardness_attack_residual=" << hardnessResidual
                  << " position_residual=" << position << " coupling_ring_db=" << coupling
                  << " coupling_residual=" << couplingResidual
                  << " decay_tail_db=" << decay << '\n';
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) {
            double distance = 0;
            for (size_t i = 0; i < fingerprints[a].size(); ++i)
                distance += std::pow(fingerprints[a][i] - fingerprints[b][i], 2);
            check(distance > .0025, "models share indistinguishable parameter response");
        }
    std::cout << "SUMMARY minimum_pickup_residual=" << minPickupResidual
              << " maximum_artifact_rate_error_db=" << maximumArtifactRateError
              << " maximum_artifact_delta_rate_error_db=" << maximumArtifactDeltaRateError
              << " failures=" << failures << (measure ? " measured\n" : "\n");
    return failures ? 1 : 0;
}
