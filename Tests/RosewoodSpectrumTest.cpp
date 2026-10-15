// A ringing Rosewood tube must not acquire the former authored 3f/5f layer.
// Isolate its actual emitted sound by subtracting two otherwise identical
// physical strikes, with and without the cavity radiation readout. This keeps
// the quieter production cavity from hiding unwanted harmonics behind the bar.
#include "MarembaVoice.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void MuteCavityRadiation(MarembaVoice& voice) {
        // Trigger has already calibrated the heard pitch. Preserve every
        // mode, contact, coupling, pickup and collision coefficient.
        for (double& gain : voice.mCavityReadout) gain = 0.0;
    }
};
}

namespace {
constexpr double rate = 96000.0; // default 2x internal rate at a 48 kHz host
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
double hz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }
double line(const std::vector<double>& x, double frequency) {
    const std::size_t first = static_cast<std::size_t>(.15 * rate);
    const std::size_t last = static_cast<std::size_t>(.55 * rate);
    double re = 0.0, im = 0.0, weight = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double window = .5 - .5 * std::cos(2 * pi * (i - first) / (last - first - 1));
        re += x[i] * window * std::cos(2 * pi * frequency * i / rate);
        im += x[i] * window * std::sin(2 * pi * frequency * i / rate);
        weight += window;
    }
    return std::hypot(re, im) / weight;
}
void spectrum(int note, float velocity) {
    using namespace maremba;
    FastPRNG random(0x726f7365u);
    MarembaVoice radiating;
    radiating.SetSeed(0x74756265u);
    radiating.Trigger(note, velocity, EMarimbaModel::ImperialRosewood,
        .55f, .47f, 0, .75f, 1, 0, 0, 0, rate, random, 0, 0, 1, 0);
    MarembaVoice muted = radiating;
    MarembaVoiceTestAccess::MuteCavityRadiation(muted);
    std::vector<double> cavity(static_cast<std::size_t>(.60 * rate));
    bool finite = true, samePickup = true;
    double energy = 0.0;
    for (std::size_t i = 0; i < cavity.size(); ++i) {
        float close = 0, far = 0, piezo = 0, mutedClose = 0, mutedFar = 0, mutedPiezo = 0;
        radiating.ProcessSample(close, far, piezo);
        muted.ProcessSample(mutedClose, mutedFar, mutedPiezo);
        cavity[i] = double(close) - mutedClose;
        finite = finite && std::isfinite(cavity[i]);
        samePickup = samePickup && piezo == mutedPiezo;
        energy += cavity[i] * cavity[i];
        // The engine flushes/updates voices every 16 host frames (32 at 2x).
        if ((i + 1) % 32 == 0) {
            radiating.UpdateEnvelope();
            muted.UpdateEnvelope();
        }
    }
    check(finite && samePickup, "cavity isolation changed mechanics/pickup or became nonfinite");
    check(energy > 1e-10, "cavity isolation produced silence");
    double f0 = hz(note), amplitude = 0;
    for (double cents = -30; cents <= 30; cents += .5) {
        const double f = hz(note) * std::exp2(cents / 1200.0);
        const double value = line(cavity, f);
        if (value > amplitude) { amplitude = value; f0 = f; }
    }
    check(amplitude > 1e-8, "cavity fundamental was not measurable");
    const double third = 20 * std::log10(line(cavity, 3 * f0) / std::max(amplitude, 1e-30) + 1e-30);
    const double fifth = 20 * std::log10(line(cavity, 5 * f0) / std::max(amplitude, 1e-30) + 1e-30);
    std::cout << "CAVITY note=" << note << " velocity=" << velocity << " f0_hz=" << f0
              << " third_db=" << third << " fifth_db=" << fifth << '\n';
    // Finite-window leakage is allowed; the former authored lines were tens
    // of dB stronger relative to the isolated tube, even at today's low gain.
    check(third < -70 && fifth < -70,
          "Rosewood tube acquires an added third/fifth harmonic at note " + std::to_string(note));
}
}

int main() {
    std::cout << std::setprecision(8);
    for (int note : {48, 53, 57, 60})
        for (float velocity : {.30f, .63f, .95f}) spectrum(note, velocity);
    std::cout << (failures ? "RosewoodSpectrumTest FAILED" : "RosewoodSpectrumTest PASSED")
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
