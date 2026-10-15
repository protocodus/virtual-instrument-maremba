// A tube under its bar, against anechoic recordings of a concert marimba
// (University of Iowa MIS, 2013: cord mallets, 1.5 m, mf and ff): the
// fundamental decays without a beating twin and rings 1.4-2.9 s through the
// lower keyboard. The current accepted Rosewood radiation is bar-led rather
// than reproducing those recordings' 70-170 ms tube-dominant bloom. Preserve
// their decay/ripple constraints while testing the new prompt attack.
// Render the engine-configured voice's
// far microphone before Room (about 1 m), with the Concert Grand striker.
#include "DryCaptureProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double rate = 48000;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
double hz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

maremba::EngineParameters grand() {
    maremba::EngineParameters p;
    p.velocityCurve = 1; // keep the recorded-reference physical strike calibration
    p.model = 0; p.malletType = 1; p.malletHardness = .55f; p.strikePosition = .47f;
    p.resonatorCoupling = .75f; p.decay = 1;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.buzzAmount = p.bodyBloom = p.sympathetic = 0;
    p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    // Measure the fixed physical voicing; VOL range calibration is tested separately.
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.volume = 1;
    return p;
}
// Mono sum of one direct output pair after a single strike.
std::vector<double> strike(const maremba::EngineParameters& p, int note, float velocity, double seconds, bool far) {
    return maremba::MarembaEngineTestAccess::DryCapture(p, note, velocity, rate, seconds, far);
}
double line(const std::vector<double>& x, double f, double from, double to) {
    const size_t a = static_cast<size_t>(from * rate), b = std::min(x.size(), static_cast<size_t>(to * rate));
    double re = 0, im = 0, w = 0;
    for (size_t i = a; i < b; ++i) {
        const double h = .5 - .5 * std::cos(2 * kPi * (i - a) / std::max<double>(1, b - a - 1));
        re += x[i] * h * std::cos(2 * kPi * f * i / rate);
        im += x[i] * h * std::sin(2 * kPi * f * i / rate);
        w += h;
    }
    return std::sqrt(re * re + im * im) / std::max(w, 1e-30);
}
struct Envelope { double peakMs, t60, ripple; };
// The fundamental in 40 ms frames every 10 ms (wide enough to keep a twin's
// beating in the envelope): peak time, T60 of a line fitted from 50 ms after
// the peak down to -40 dB, and the peak-to-peak residual about that line.
Envelope envelope(const std::vector<double>& x, double f) {
    std::vector<double> t, d;
    for (double s = .0; s + .04 < x.size() / rate; s += .01) { t.push_back(s + .02); d.push_back(20 * std::log10(line(x, f, s, s + .04) + 1e-30)); }
    const size_t peak = std::max_element(d.begin(), d.end()) - d.begin();
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    for (size_t i = peak + 5; i < d.size() && d[i] > d[peak] - 40; ++i) { sx += t[i]; sy += d[i]; sxx += t[i] * t[i]; sxy += t[i] * d[i]; ++n; }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), cut = (sy - slope * sx) / n;
    double lo = 1e9, hi = -1e9;
    for (size_t i = peak + 5; i < d.size() && d[i] > d[peak] - 40; ++i) {
        const double r = d[i] - (cut + slope * t[i]);
        lo = std::min(lo, r); hi = std::max(hi, r);
    }
    return {t[peak] * 1e3, -60 / slope, hi - lo};
}
double bloomRise(const std::vector<double>& x, double f) {
    double peak = 0;
    for (double t = .02; t + .02 <= .3; t += .005) peak = std::max(peak, line(x, f, t, t + .02));
    return 20 * std::log10(peak / std::max(line(x, f, .02, .04), 1e-30));
}
} // namespace

int main() {
    // MIS cord mf/ff means: bloom peak (ms) and fundamental T60 (s).
    struct Key { int note; double peakMs, t60; };
    const Key keys[] = {{45, 170, 2.27}, {48, 150, 2.86}, {54, 110, 2.12}, {57, 90, 1.92}, {60, 70, 1.38}};
    for (const Key& k : keys) {
        auto reference = grand();
        // Preserve the physical input used to establish the MIS bounds. This
        // fixture originally inherited 20% attenuation routes at velocity .63:
        // mapped velocity .6337, effective Hardness .509707 and Decay .92674.
        // Freeze those values here rather than depend on the public VOL law.
        constexpr float velocity = .63f;
        constexpr float physicalVelocity = .01f + .99f * velocity;
        constexpr float referenceScale = 1.f - .20f * (1.f - physicalVelocity);
        reference.malletHardness *= referenceScale;
        reference.decay *= referenceScale;
        const auto x = strike(reference, k.note, velocity, 3.0, true);
        const Envelope e = envelope(x, hz(k.note));
        std::cout << "INSITU note=" << k.note << " peak_ms=" << e.peakMs << " (MIS " << k.peakMs << ") t60_s=" << e.t60
                  << " (MIS " << k.t60 << ") ripple_db=" << e.ripple << '\n';
        check(e.peakMs >= 20 && e.peakMs <= 40,
              "Rosewood's quiet tube masks the prompt bar attack");
        check(e.t60 > .75 * k.t60 && e.t60 < 1.35 * k.t60, "the fundamental's sustain is far from the recorded marimba's");
        check(e.ripple < 3.0, "bar and tube beat as a twin the recorded marimba does not show");
    }
    // The close microphone likewise keeps the bar ahead of the quiet tube.
    {
        auto p = grand();
        p.strikePosition = .4f;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
        const double g3 = bloomRise(strike(p, 55, .8f, .4, false), hz(55)), c4 = bloomRise(strike(p, 60, .8f, .4, false), hz(60));
        std::cout << "INSITU close bloom G3=" << g3 << " C4=" << c4 << '\n';
        check(std::isfinite(g3) && std::isfinite(c4) && g3 < 1 && c4 < 1,
              "Rosewood's close tube readout overwhelms the bar-led attack");
    }
    std::cout << (failures ? "TubeInSituTest FAILED" : "TubeInSituTest PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
