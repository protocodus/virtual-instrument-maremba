// Public-audio continuity, shared-response and causal radiation regressions. These complement
// PhysicalResponseTest's level/decay/idle controls and CavityPhysicsTest's
// junction invariants. Build with -I Source/DSP and Source/DSP/*.cpp.
#include "MarembaVoice.h"
#include "FrameBody.h"
#include "SympatheticMesh.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    // Identity readout bypasses only surface radiation; every oscillator,
    // contact, cavity, pickup and random sequence remains otherwise untouched.
    static void BypassRadiation(MarembaVoice& voice) {
        for (auto& coefficient : voice.mRadiation) coefficient = {};
    }
    // Stored energy of the Buzz layer itself: membrane (and its shadow) or
    // the kalimba buzzers' rings.
    static double BuzzEnergy(const MarembaVoice& voice) { return voice.MembraneEnergy(); }
};
}

namespace {
constexpr double pi = 3.14159265358979323846;
struct Settings {
    int model = 0, note = 60;
    double rate = 96000, seconds = .12;
    float cavityTune = 0, buzz = 0, tension = .5f;
    bool disableBuzz = false;
    std::vector<double>* buzzEnergy = nullptr;   // per-sample Buzz layer energy
};
std::vector<double> render(const Settings& s) {
    maremba::MarembaVoice voice;
    maremba::FastPRNG random(0x812u);
    voice.SetSeed(0x812u);
    maremba::StrikeOptions options;
    options.membraneTension = s.tension;
    voice.Trigger(s.note, 1, static_cast<maremba::EMarimbaModel>(s.model), .9f, .35f,
                  s.cavityTune, 1, 1, s.buzz, 0, 0, s.rate, random, 0, 0, 3, 0, 0, options);
    std::vector<double> output(static_cast<size_t>(s.seconds * s.rate));
    for (size_t i = 0; i < output.size(); ++i) {
        if (s.disableBuzz && i == static_cast<size_t>(.04 * s.rate)) voice.SetBuzz(0, s.tension);
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
        output[i] = close;
        if (s.buzzEnergy) s.buzzEnergy->push_back(maremba::MarembaVoiceTestAccess::BuzzEnergy(voice));
        if ((i & 31u) == 31u) voice.UpdateEnvelope();
    }
    return output;
}
double db(double x) { return 10 * std::log10(std::max(x, 1.e-30)); }
double energy(const std::vector<double>& x) {
    double sum = 0;
    for (double sample : x) sum += sample * sample;
    return sum;
}
double difference(const std::vector<double>& a, const std::vector<double>& b,
                  size_t first = 0, size_t last = 0) {
    if (!last) last = a.size();
    double sum = 0;
    for (size_t i = first; i < last; ++i) {
        const double sample = a[i] - b[i];
        sum += sample * sample;
    }
    return sum;
}
struct SharedMetric { double level = 0, brightness = 0; };
SharedMetric sharedResponse(int model, bool sympathetic, double rate) {
    maremba::FrameBody body;
    maremba::SympatheticMesh mesh;
    const auto instrument = static_cast<maremba::EMarimbaModel>(model);
    const auto p = maremba::GetModelParams(instrument);
    body.Configure(rate, p.frameBodyFreq, p.frameBodyQ, instrument);
    mesh.Configure(rate, 0, instrument);
    double total = 0, upper = 0, filtered = 0;
    const double alpha = 1 - std::exp(-2*pi*500/rate);
    // A fixed, band-limited mechanical gesture: no discrete impulse whose
    // energy/area changes with sample rate, and no random sample-rate sequence.
    for (size_t i = 0; i < static_cast<size_t>(.4 * rate); ++i) {
        const double t = i / rate;
        const double envelope = t < .12 ? std::pow(std::sin(pi*t/.12), 2) : 0;
        const float input = static_cast<float>(.025 * envelope *
            (std::sin(2*pi*85*t)+std::sin(2*pi*261.625565*t)
             +std::sin(2*pi*523.251131*t)+std::sin(2*pi*1300*t)));
        double sample;
        if (sympathetic) {
            mesh.BeginSample();
            mesh.AccumulateVoice(60, input);
            float left, right;
            mesh.Process(1, left, right);
            sample = left + right;
        } else sample = body.Process(input);
        filtered += alpha * (sample-filtered);
        total += sample*sample;
        upper += (sample-filtered)*(sample-filtered);
    }
    return {db(total/rate), db(upper/std::max(total, 1.e-30))};
}
} // namespace

int main(int argc, char** argv) {
    const bool measure = argc == 2 && std::string(argv[1]) == "--measure";
    if (argc > 1 && !measure) return 2;
    int failures = 0;
    auto check = [&](bool pass, const std::string& description) {
        if (pass) return;
        if (!measure) ++failures;
        std::cerr << (measure ? "OBSERVATION " : "FAIL ") << description << '\n';
    };
    std::cout << std::setprecision(9);

    // Locations found by continuous cavity-tuning sweeps of the old single-
    // nearest-mode selection. Bar pitch/contact are identical in each pair,
    // so phase drift of the main note cannot conceal an acoustic jump. Shrink
    // the parameter interval tenfold: a continuous response must converge,
    // whereas a source-selection step remains the same size. Test all four
    // instruments, both fixed-box transitions and two host-rate families.
    struct Boundary { int model, note; double cents; };
    const std::array<Boundary, 5> boundaries = {{{0, 84, 34.76555}, {1, 89, -4.200615},
        {2, 87, -5.503624}, {3, 57, -47.14030}, {3, 68, 30.27426}}};
    for (const auto& b : boundaries) for (double rate : {88200., 96000.}) {
        Settings s;
        s.model = b.model; s.note = b.note; s.rate = rate;
        // This low tine's .005-cent response is below float output rounding:
        // the double readout contracts by 20 dB, but subtracting the public
        // floats measures their quantization instead. A .5/.05-cent bracket
        // resolves the same historical boundary while keeping the tenfold
        // interval contraction and the original acceptance criterion.
        const double halfWidth = b.model == 3 && b.note == 57 ? .5 : .05;
        s.cavityTune = static_cast<float>(b.cents - halfWidth);
        const auto wideLow = render(s);
        s.cavityTune = static_cast<float>(b.cents + halfWidth);
        const auto wideHigh = render(s);
        s.cavityTune = static_cast<float>(b.cents - halfWidth / 10);
        const auto fineLow = render(s);
        s.cavityTune = static_cast<float>(b.cents + halfWidth / 10);
        const auto fineHigh = render(s);
        const double coarse = difference(wideLow, wideHigh);
        const double fine = difference(fineLow, fineHigh);
        const double contraction = db(fine/std::max(coarse, 1.e-30));
        check(std::isfinite(contraction) && contraction < -13,
              "cavity tuning contains a finite step model=" + std::to_string(b.model)
              + " note=" + std::to_string(b.note));
        std::cout << "CONTINUITY model=" << b.model << " note=" << b.note << " rate=" << rate
                  << " coarse_half_width_cents=" << halfWidth
                  << " step_contraction_db=" << contraction
                  << " fine_delta_to_note_db=" << db(fine/std::max(energy(fineLow), 1.e-30)) << '\n';
    }

    // The three new body modes and model-specific sympathetic losses must
    // retain both level and broad spectral balance across internal rates.
    for (int model = 0; model < 4; ++model) for (bool mesh : {false, true}) {
        const auto reference = sharedResponse(model, mesh, 96000);
        for (double rate : {88200., 192000.}) {
            const auto response = sharedResponse(model, mesh, rate);
            const double levelError = std::abs(response.level - reference.level);
            const double brightnessError = std::abs(response.brightness - reference.brightness);
            check(std::isfinite(response.level) && levelError < .5 && brightnessError < .5,
                  std::string(mesh ? "sympathetic" : "body") + " changes with sample rate model="
                  + std::to_string(model));
            std::cout << "SHARED_RATE model=" << model << " layer=" << (mesh ? "sympathetic" : "body")
                      << " rate=" << rate << " level_error_db=" << levelError
                      << " brightness_error_db=" << brightnessError << '\n';
        }
    }

    // Buzz-off removes pressure drive while preserving a short inertial decay.
    // The layer's own stored energy (membrane, or kalimba buzzers) must then
    // die away, on every model/register, while the bar still rings: no frozen
    // or autonomous oscillator. The buzzing already drained energy from the
    // resonator, so the released note is not compared with a never-buzzed one.
    for (int model = 0; model < 4; ++model) for (int note : {48, 72}) {
        Settings s;
        s.model = model; s.note = note;
        s.buzz = 1; s.disableBuzz = true;
        // The yielding Kalimba bridge leaves a low C3 below the buzzer's
        // default lift-off preload. Slack tension makes this release probe
        // excite the layer without changing the excitation or decay gates.
        if (model == 3 && note == 48) s.tension = 0;
        std::vector<double> layer;
        s.buzzEnergy = &layer;
        const auto released = render(s);
        const auto peak = [&](double from, double to) {
            return *std::max_element(layer.begin() + static_cast<size_t>(from * s.rate),
                                     layer.begin() + static_cast<size_t>(to * s.rate));
        };
        const double before = peak(.02, .04), after = peak(.06, .08);
        const double decay = db(after/std::max(before, 1.e-30));
        check(before > 1.e-12, "Buzz-off probe never excited its membrane model="
              + std::to_string(model) + " note=" + std::to_string(note));
        check(energy(released) > 0 && std::isfinite(decay) && decay < -60,
              "disabled membrane fails to release model="
              + std::to_string(model) + " note=" + std::to_string(note));
        std::cout << "BUZZ_OFF model=" << model << " note=" << note
                  << " tension=" << s.tension << " energy_before=" << before
                  << " residual_after_20ms_db=" << decay << '\n';
    }

    // Controlled ablation establishes that surface radiation actually changes
    // the acoustic attack, without changing bar dynamics or contact pickup.
    // Both voices have the complete current cavity and membrane implementation.
    for (int model = 0; model < 4; ++model) {
        double strongestDifference = 0, strongestTimbre = 0;
        for (int note : {48, 60, 72}) {
            maremba::MarembaVoice full, bypass;
            maremba::FastPRNG fullRandom(0x812u), bypassRandom(0x812u);
            full.SetSeed(0x812u); bypass.SetSeed(0x812u);
            const auto instrument = static_cast<maremba::EMarimbaModel>(model);
            full.Trigger(note, 1, instrument, .9f, .35f, 0, 1, 1, 0, 0, 0,
                         96000, fullRandom, 0, 0, 3, 0);
            bypass.Trigger(note, 1, instrument, .9f, .35f, 0, 1, 1, 0, 0, 0,
                           96000, bypassRandom, 0, 0, 3, 0);
            maremba::MarembaVoiceTestAccess::BypassRadiation(bypass);
            bool sameMechanics = true;
            std::array<double, 2> fullEnergy{}, bypassEnergy{}, cross{}, delta{};
            for (int sample = 0; sample < 7680; ++sample) {
                float close, far, piezo, oldClose, oldFar, oldPiezo;
                full.ProcessSample(close, far, piezo);
                bypass.ProcessSample(oldClose, oldFar, oldPiezo);
                sameMechanics = sameMechanics && piezo == oldPiezo
                    && full.GetMechanicalSample() == bypass.GetMechanicalSample()
                    && full.ComputeEnergy() == bypass.ComputeEnergy();
                const std::array<double, 2> a = {close, far}, b = {oldClose, oldFar};
                for (size_t channel = 0; channel < a.size(); ++channel) {
                    fullEnergy[channel] += a[channel]*a[channel];
                    bypassEnergy[channel] += b[channel]*b[channel];
                    cross[channel] += a[channel]*b[channel];
                    delta[channel] += (a[channel]-b[channel])*(a[channel]-b[channel]);
                }
                if ((sample & 31) == 31) { full.UpdateEnvelope(); bypass.UpdateEnvelope(); }
            }
            check(sameMechanics, "surface radiation changes mechanical energy or pickup");
            for (size_t channel = 0; channel < fullEnergy.size(); ++channel) {
                const double effect = delta[channel]/std::max(fullEnergy[channel], 1.e-30);
                const double timbre = std::max(0., 1-cross[channel]*cross[channel]
                    /std::max(fullEnergy[channel]*bypassEnergy[channel], 1.e-30));
                strongestDifference = std::max(strongestDifference, effect);
                strongestTimbre = std::max(strongestTimbre, timbre);
                std::cout << "RADIATION_ABLATION model=" << model << " note=" << note
                          << " mic=" << (channel ? "far" : "close")
                          << " delta_to_full_db=" << db(effect)
                          << " gain_fit_residual=" << timbre << '\n';
            }
        }
        // At least one representative hard/off-centre attack per instrument
        // must exceed 0.3% RMS difference and retain a change after gain fitting.
        check(strongestDifference > 9.e-6 && strongestTimbre > 9.e-6,
              "surface radiation has no meaningful acoustic effect model=" + std::to_string(model));
    }
    std::cout << "Round2ResponseTest " << (measure ? "MEASURED" : failures ? "FAILED" : "PASSED")
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
