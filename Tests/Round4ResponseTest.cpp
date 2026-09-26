// Fourth physical-model pass: resonator radiation and bloom, wood damping,
// torsional modes, per-key scatter, slack buzzing membranes, the mounted
// kalimba tine and its board buzzers. Rendered checks use the engine's dry
// close output; exact configuration invariants use friend access.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static double ModeT60(const MarembaVoice& v, int m) {
        return 6.907755 / (-std::log(v.mModes[m].r) * v.mSampleRate);
    }
    static double ModeHz(const MarembaVoice& v, int m) {
        return v.mModes[m].w * v.mSampleRate / (2.0 * 3.14159265358979323846);
    }
    static int TorsionCount(const MarembaVoice& v) { return v.mNumTorsionModes; }
    static double TorsionHz(const MarembaVoice& v, int n) {
        return v.mTorsion[n].w * v.mSampleRate / (2.0 * 3.14159265358979323846);
    }
    static double TorsionDrive(const MarembaVoice& v, int n) { return v.mTorsionDrive[n]; }
    static double SampleRate(const MarembaVoice& v) { return v.mSampleRate; }
    static int Membranes(const MarembaVoice& v) { return v.mNumMembranes; }
    static bool AnyTaut(const MarembaVoice& v) {
        for (int i = 0; i < v.mNumMembranes; ++i) if (v.mMembranes[i].Taut()) return true;
        return false;
    }
    static bool MatchesLinearShadow(const MarembaVoice& v) {
        for (int i = 0; i < v.mNumMembranes; ++i) {
            const auto& membrane = v.mMembranes[i];
            if (membrane.mX != membrane.mLinearX || membrane.mV != membrane.mLinearV) return false;
        }
        return true;
    }
    static double MembraneEnergy(const MarembaVoice& v) { return v.MembraneEnergy(); }
    static double SlapEnvelope(const MarembaVoice& v) { return v.mSlapNoise; }
    static bool MembranesFinite(const MarembaVoice& v) {
        for (int i = 0; i < v.mNumMembranes; ++i) if (!v.mMembranes[i].IsFinite()) return false;
        return std::isfinite(v.mSlapNoise);
    }
};
struct MarembaEngineTestAccess {
    static MarembaVoice TriggeredVoice(const MarembaEngine& engine, bool& found) {
        for (const auto& voice : engine.mVoices) if (voice.IsActive()) { found = true; return voice; }
        found = false;
        return {};
    }
};
}

namespace {
using maremba::EMarimbaModel;
constexpr double kPi = 3.14159265358979323846;
constexpr double rate = 48000;
double db(double ratio) { return 10 * std::log10(std::max(ratio, 1.e-30)); }
double hz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

maremba::EngineParameters clean(int model) {
    maremba::EngineParameters p;
    p.model = model;
    p.strikePosition = .35f;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.buzzAmount = p.bodyBloom = p.sympathetic = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    p.closeLevel = p.volume = 1;
    return p;
}

struct MembraneObservation {
    bool found = false, linear = true, finite = true;
    int count = 0, tautSamples = 0;
    double peakEnergy = 0, peakSlapEnvelope = 0;
};
MembraneObservation membraneThreshold(int model, float buzz, float velocity) {
    auto p = clean(model);
    p.buzzAmount = buzz;
    maremba::MarembaEngine engine(rate);
    engine.SetParameters(p);
    engine.NoteOn(60, velocity);
    MembraneObservation result;
    // Copy the exact engine-created physical voice, including velocity curve,
    // per-slot seed and routed controls. Inspect every internal sample: host
    // block boundaries could miss a short crossing of the membrane limit.
    auto voice = maremba::MarembaEngineTestAccess::TriggeredVoice(engine, result.found);
    using Access = maremba::MarembaVoiceTestAccess;
    result.count = Access::Membranes(voice);
    const int samples = static_cast<int>(.4 * Access::SampleRate(voice));
    for (int i = 0; i < samples; ++i) {
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
        result.tautSamples += Access::AnyTaut(voice);
        result.linear = result.linear && Access::MatchesLinearShadow(voice);
        result.finite = result.finite && Access::MembranesFinite(voice);
        result.peakEnergy = std::max(result.peakEnergy, Access::MembraneEnergy(voice));
        result.peakSlapEnvelope = std::max(result.peakSlapEnvelope, Access::SlapEnvelope(voice));
        if ((i & 31) == 31) voice.UpdateEnvelope();
    }
    return result;
}

// Close L+R of one strike, dry; optional restrikes (seconds) of the same key.
std::vector<double> render(const maremba::EngineParameters& p, int note, float velocity,
                           double seconds, double sampleRate = rate,
                           const std::vector<double>& restrikes = {}) {
    maremba::MarembaEngine engine(sampleRate);
    engine.SetParameters(p);
    engine.NoteOn(note, velocity);
    std::vector<double> out;
    std::array<float, 64> l{}, r{}, cl{}, cr{};
    const size_t frames = static_cast<size_t>(seconds * sampleRate);
    size_t next = 0;
    for (size_t start = 0; start < frames;) {
        size_t n = std::min<size_t>(64, frames - start);
        if (next < restrikes.size()) {
            const size_t at = static_cast<size_t>(restrikes[next] * sampleRate);
            if (at == start) { engine.NoteOn(note, velocity); ++next; continue; }
            n = std::min(n, at - start);
        }
        engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr, nullptr,
                           static_cast<int>(n));
        for (size_t i = 0; i < n; ++i) out.push_back(cl[i] + cr[i]);
        start += n;
    }
    return out;
}

// Hann-windowed line amplitude at a frequency over [from, to) seconds.
double line(const std::vector<double>& x, double frequency, double from, double to,
            double sampleRate = rate) {
    const size_t a = static_cast<size_t>(from * sampleRate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * sampleRate));
    double re = 0, im = 0, weight = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = .5 - .5 * std::cos(2 * kPi * (i - a) / std::max<double>(1, b - a - 1));
        const double phase = 2 * kPi * frequency * i / sampleRate;
        re += x[i] * w * std::cos(phase);
        im += x[i] * w * std::sin(phase);
        weight += w;
    }
    return std::sqrt(re * re + im * im) / std::max(weight, 1.e-30);
}

// Time of a line's largest 20 ms-window amplitude within the first second.
double peakTime(const std::vector<double>& x, double frequency, double& firstToPeakDb) {
    double best = 0, bestTime = 0, first = -1;
    for (double t = 0; t + .02 < std::min(1.0, x.size() / rate); t += .002) {
        const double a = line(x, frequency, t, t + .02);
        if (first < 0) first = a;
        if (a > best) { best = a; bestTime = t + .01; }
    }
    firstToPeakDb = 2 * db(first / std::max(best, 1.e-30));
    return bestTime;
}

double energy(const std::vector<double>& x, double from = 0, double to = 1e9) {
    double e = 0;
    const size_t a = static_cast<size_t>(from * rate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return e;
}

// Spectral centroid of a signal via a direct DFT over 0..8 kHz (short segments).
double centroid(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * rate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    double weighted = 0, total = 0;
    for (double f = 100; f < 8000; f += 50) {
        const double level = line(x, f, a / rate, b / rate);
        weighted += f * level * level;
        total += level * level;
    }
    return weighted / std::max(total, 1.e-30);
}

std::vector<double> minus(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> d(std::min(a.size(), b.size()));
    for (size_t i = 0; i < d.size(); ++i) d[i] = a[i] - b[i];
    return d;
}

// Crude one-pole high-pass (fc ~ 800 Hz at 48 kHz): buzz lives above the note.
std::vector<double> highPass(const std::vector<double>& x) {
    std::vector<double> y(x.size());
    const double a = std::exp(-2 * kPi * 800 / rate);
    double previousIn = 0, previousOut = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        previousOut = a * (previousOut + x[i] - previousIn);
        previousIn = x[i];
        y[i] = previousOut;
    }
    return y;
}

maremba::MarembaVoice trigger(EMarimbaModel model, int note, float position, uint32_t seed,
                             float buzz = 0) {
    maremba::MarembaVoice voice;
    voice.SetSeed(seed);
    maremba::FastPRNG random(seed);
    voice.Trigger(note, .9f, model, .7f, position, 0, .7f, 1, buzz, 0, 0, 88200, random, 0, 0, 2, 0);
    return voice;
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& message) {
        if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    };
    std::cout << std::setprecision(6);
    const char* names[] = {"rosewood", "padauk", "balafon", "kalimba"};

    // 1. The resonator, not the bar, radiates the low fundamental; it blooms.
    // Bars are short-circuited dipoles, the tube mouth a monopole: the far
    // field turns from dipole to monopole within tens of ms (Doutaut et al.
    // 1998), and recorded marimba fundamentals peak 50-160 ms after the hit.
    check(std::abs(maremba::BarDirectRadiationScale(1046.502261, .055) - 1) < 1e-12,
          "direct bar radiation is not unity at its C4 4:1 reference");
    check(maremba::BarDirectRadiationScale(65.4, .078) < .25,
          "a bass bar's fundamental still radiates like a monopole");
    for (double f = 50; f < 8000; f *= 1.1)
        check(maremba::BarDirectRadiationScale(f * 1.1, .055) > maremba::BarDirectRadiationScale(f, .055),
              "dipole radiation efficiency is not monotone in frequency");
    const double minimumPeak[3] = {.06, .025, .03};
    for (int model = 0; model < 3; ++model) {
        auto p = clean(model);
        p.malletType = 2; p.malletHardness = .7f;
        const auto note = render(p, 48, .9f, 1.0);
        double fundamentalRise = 0, partialRise = 0;
        const double fundamentalPeak = peakTime(note, hz(48), fundamentalRise);
        const double partialPeak = peakTime(note, 4 * hz(48), partialRise);
        check(fundamentalPeak >= minimumPeak[model] && fundamentalRise < -3,
              std::string("no resonator bloom on the fundamental: ") + names[model]);
        check(partialPeak < .03, std::string("bar partial does not lead the attack: ") + names[model]);
        p.resonatorCoupling = 0;
        const auto bare = render(p, 48, .9f, 1.0);
        const double gain = 2 * db(line(note, hz(48), .05, .45) / line(bare, hz(48), .05, .45));
        check(gain > 6, std::string("the resonator does not amplify the low fundamental: ") + names[model]);
        std::cout << "BLOOM model=" << names[model] << " fundamental_peak_s=" << fundamentalPeak
                  << " rise_db=" << -fundamentalRise << " partial_peak_s=" << partialPeak
                  << " resonator_gain_db=" << gain << '\n';
    }

    // 2. Wood loss: near-constant loss factor with a small f^2 term. Measured
    // bars ring their 4:1 and 10:1 partials for roughly 0.25-0.6 and 0.1-0.2 of
    // the fundamental's T60 (Chaigne & Doutaut 1997; Hill 2011).
    for (int model = 0; model < 3; ++model) for (int note : {48, 60}) {
        const auto voice = trigger(static_cast<EMarimbaModel>(model), note, .35f, 7);
        const double t0 = maremba::MarembaVoiceTestAccess::ModeT60(voice, 0);
        const double second = maremba::MarembaVoiceTestAccess::ModeT60(voice, 1) / t0;
        const double third = maremba::MarembaVoiceTestAccess::ModeT60(voice, 2) / t0;
        // Against the bar's own fundamental (the tube shortens the sounding
        // one about twofold); C4's 10:1 then rings 0.11 s, as in the
        // reference fit.
        check(second > .2 && second < .65 && third > .04 && third < .35,
              std::string("upper-partial decay outside the measured range: ") + names[model]);
        std::cout << "DECAY model=" << names[model] << " note=" << note
                  << " t60_4x_ratio=" << second << " t60_10x_ratio=" << third << '\n';
    }

    // 3. Torsional modes: measured ratios, lengthwise node, off-axis drive.
    for (int model = 0; model < 4; ++model) {
        const auto type = static_cast<EMarimbaModel>(model);
        const auto centre = trigger(type, 48, .5f, 11);
        if (model == 3) {
            check(maremba::MarembaVoiceTestAccess::TorsionCount(centre) == 0, "a tine has bar torsion");
            continue;
        }
        check(maremba::MarembaVoiceTestAccess::TorsionCount(centre) == 2, "torsional modes missing");
        const auto key = maremba::GetBarKey(type, 48);
        const auto variation = maremba::GetKeyVariation(type, 48);
        for (int n = 0; n < 2; ++n) {
            const double expected = hz(48) * key.torsion[n] * variation.torsion;
            check(std::abs(maremba::MarembaVoiceTestAccess::TorsionHz(centre, n) / expected - 1) < 1e-9,
                  "torsional frequency does not follow its profile");
        }
        int driven = 0;
        for (uint32_t seed = 1; seed <= 16; ++seed) {
            const auto voice = trigger(type, 48, .5f, seed);
            const double first = maremba::MarembaVoiceTestAccess::TorsionDrive(voice, 0);
            const double second = maremba::MarembaVoiceTestAccess::TorsionDrive(voice, 1);
            // Even at zero Variance a stroke lands within about half a
            // millimetre of the centre: T1 stays 20 dB under T2.
            check(std::abs(first) <= .1 * std::abs(second) + 1e-300,
                  "a centred stroke drives the first torsional mode at its node");
            driven += std::abs(second) > 0;
        }
        check(driven >= 14, "off-axis strokes rarely twist the bar");
    }
    {
        // Audible and organically varying: successive hard off-centre strokes.
        auto p = clean(0);
        p.malletType = 2; p.malletHardness = .7f;
        const auto strikes = render(p, 48, .9f, 8.0, rate, {2, 4, 6});
        const auto key = maremba::GetBarKey(EMarimbaModel::ImperialRosewood, 48);
        const double t2 = hz(48) * key.torsion[1] * maremba::GetKeyVariation(EMarimbaModel::ImperialRosewood, 48).torsion;
        double loudest = -1e9, quietest = 1e9;
        for (int k = 0; k < 4; ++k) {
            const double level = 2 * db(line(strikes, t2, 2.0 * k + .003, 2.0 * k + .08)
                                        / line(strikes, hz(48), 2.0 * k + .003, 2.0 * k + .08));
            loudest = std::max(loudest, level);
            quietest = std::min(quietest, level);
        }
        check(loudest > -55 && loudest < -15, "torsional line level is implausible");
        check(loudest - quietest > 3, "every stroke twists the bar identically");
        std::cout << "TORSION rosewood C3 T2_line_db=[" << quietest << ',' << loudest << "]\n";
    }

    // 4. Per-key scatter: fixed, fundamental untouched, within its spread.
    const double tunedSpread[4] = {.003, .006, .025, .015};
    for (int model = 0; model < 4; ++model) {
        const auto type = static_cast<EMarimbaModel>(model);
        bool differs = false;
        for (int note = 24; note <= 108; ++note) {
            const auto a = maremba::GetKeyVariation(type, note);
            const auto b = maremba::GetKeyVariation(type, note);
            check(a.partial[0] == 1 && a.partial[1] == b.partial[1] && a.resonatorCents == b.resonatorCents,
                  "per-key scatter is not fixed or detunes the fundamental");
            check(std::abs(a.partial[1] - 1) <= tunedSpread[model] + 1e-12,
                  "tuned partial scatter exceeds its spread");
            differs |= a.partial[2] != maremba::GetKeyVariation(type, note + 1).partial[2];
            if (model == 3) check(a.resonatorCents == 0 && a.torsion == 1, "kalimba has bar scatter");
        }
        check(differs, "every key is identical");
    }

    // 5. Slack membranes: gated by stroke force, formant-coloured, releasable.
    // Below the taut limits the membrane must equal its linear shadow and
    // lose no energy through contact. The old soft-buzz/dry-note ratio was
    // not a snap oracle: velocity changes the dry note's modal balance, and
    // its acoustic projection changes that denominator independently of the
    // membrane. Inspect actual limit crossings; retain the audible forte
    // growth and formant checks below.
    {
        std::array<double, 2> buzzCentroid{};
        const auto layer = [](int model, float buzz, float velocity) {
            auto p = clean(model);
            const auto dry = render(p, 60, velocity, .4);
            p.buzzAmount = buzz;
            const auto wet = render(p, 60, velocity, .4);
            return std::make_pair(minus(wet, dry), dry);
        };
        for (int model : {2, 1}) {
            const auto relative = [&](float buzz, float velocity) {
                const auto result = layer(model, buzz, velocity);
                return db(energy(highPass(result.first)) / energy(result.second));
            };
            const double quiet = relative(.5f, .2f), soft = relative(.5f, .3f), forte = relative(.5f, 1);
            const auto full = layer(model, 1, 1);
            buzzCentroid[model == 2 ? 0 : 1] = centroid(full.first, .005, .1);
            for (float velocity : {.2f, .3f, 1.f}) {
                const auto observed = membraneThreshold(model, .5f, velocity);
                check(observed.found && observed.finite && observed.count > 0 && observed.peakEnergy > 0,
                      std::string("membrane threshold fixture did not excite its membrane: ") + names[model]);
                if (velocity < 1) {
                    check(observed.tautSamples == 0 && observed.linear && observed.peakSlapEnvelope == 0,
                          std::string("membrane snaps on soft strokes at medium Buzz: ") + names[model]);
                } else {
                    check(observed.tautSamples > 0 && !observed.linear && observed.peakSlapEnvelope > 0,
                          std::string("forte stroke never reaches its membrane's taut limits: ") + names[model]);
                }
                std::cout << "MEMBRANE_STATE model=" << names[model] << " velocity=" << velocity
                          << " taut_samples=" << observed.tautSamples << " linear_shadow=" << observed.linear
                          << " peak_energy=" << observed.peakEnergy
                          << " peak_slap_envelope=" << observed.peakSlapEnvelope << '\n';
            }
            check(forte - soft > 10, std::string("forte strokes do not cross the buzz threshold: ") + names[model]);
            check(db(energy(highPass(full.first)) / energy(full.second)) > -30,
                  std::string("a forte stroke at full Buzz barely buzzes: ") + names[model]);
            std::cout << "MEMBRANE model=" << names[model] << " mid_buzz_hf_db[v.2,v.3,v1]=" << quiet << ','
                      << soft << ',' << forte << " centroid_hz=" << buzzCentroid[model == 2 ? 0 : 1] << '\n';
        }
        check(buzzCentroid[0] > 900 && buzzCentroid[0] < 4000, "balafon buzz is not in the mirliton band");
        check(buzzCentroid[1] < buzzCentroid[0], "the heavier charleo does not buzz lower than the mirliton");
        // Slap loss is the only energy taken from the air; it is never negative.
        maremba::SlackMembrane membrane;
        membrane.Configure(88200, 380, 2.5);
        membrane.SetLimits(.02, .03, 8, 20);
        maremba::FastPRNG random(5);
        bool nonNegative = true;
        for (int n = 0; n < 88200; ++n)
            nonNegative &= membrane.Step(.3 * random.NextBipolar() + .2 * std::sin(n * .01), 1) >= 0;
        check(nonNegative && membrane.IsFinite(), "membrane returns negative loss or diverges");
    }

    // 6. Mounted kalimba tine: measured ratios, faster overtone decay.
    {
        const std::array<std::array<double, 2>, 5> chapman = {{{304, 5.86}, {336, 5.63}, {415, 5.76},
                                                               {462, 5.61}, {581, 5.30}}};
        double squared = 0;
        for (const auto& point : chapman) {
            const double error = maremba::GetKalimbaMount(point[0]).ratio[1] - point[1];
            squared += error * error;
        }
        const double rms = std::sqrt(squared / chapman.size());
        check(rms < .15, "mounted tine misses Chapman's measured overtone ratios");
        for (double f = 130; f < 1000; f *= 1.05)
            check(maremba::GetKalimbaMount(f * 1.05).ratio[1] <= maremba::GetKalimbaMount(f).ratio[1] + 1e-12,
                  "overtone ratio does not fall toward the treble");
        check(maremba::GetKalimbaMount(3000).ratio[1] == maremba::GetKalimbaMount(880).ratio[1],
              "mount law extrapolates beyond its trusted range");
        check(maremba::GetKalimbaMount(262).bridgeLoss[1] > 2, "overtone loses no energy through the bridge");
        const auto voice = trigger(EMarimbaModel::KalimbaArtisan, 60, .5f, 3);
        const double overtone = maremba::MarembaVoiceTestAccess::ModeHz(voice, 1)
            / maremba::MarembaVoiceTestAccess::ModeHz(voice, 0);
        const double expected = maremba::GetKalimbaMount(hz(60)).ratio[1]
            * maremba::GetKeyVariation(EMarimbaModel::KalimbaArtisan, 60).partial[1];
        check(std::abs(overtone / expected - 1) < 1e-6, "tine does not sound at its mounted ratio");
        const double decay = maremba::MarembaVoiceTestAccess::ModeT60(voice, 1)
            / maremba::MarembaVoiceTestAccess::ModeT60(voice, 0);
        check(decay < .3, "tine overtone rings too long against its fundamental");
        std::cout << "MOUNT chapman_rms=" << rms << " C4_ratio=" << overtone << " C4_t60_ratio=" << decay << '\n';
    }

    // 7. Kalimba buzzers: default Buzz stays clean at mf; loose buzzers rattle.
    {
        auto p = clean(3);
        const auto dry = render(p, 72, .7f, .6);
        p.buzzAmount = .15f;
        check(energy(minus(render(p, 72, .7f, .6), dry)) == 0, "default Buzz rattles a mezzo-forte tine");
        p.buzzAmount = 0;
        const auto dryLoud = render(p, 72, 1, .6);
        p.buzzAmount = 1;
        const double rattle = db(energy(minus(render(p, 72, 1, .6), dryLoud)) / energy(dryLoud));
        // Full Buzz is 70 % of the former physical range (1.3.0f2): the
        // loosest buzzers still rattle a forte note, a little less.
        check(rattle > -45, "loose buzzers do not rattle on a forte note");
        // Physical-time flight and impulses: the same layer level at 48 and 96 kHz.
        const auto at = [&](double sampleRate) {
            auto q = p; q.buzzAmount = 0;
            const auto base = render(q, 72, 1, .6, sampleRate);
            q.buzzAmount = 1;
            const auto wet = render(q, 72, 1, .6, sampleRate);
            double e = 0, d = 0;
            for (size_t i = 0; i < base.size(); ++i) { e += (wet[i] - base[i]) * (wet[i] - base[i]); d += base[i] * base[i]; }
            return db(e / d);
        };
        const double rateError = std::abs(at(48000) - at(96000));
        check(rateError < 1.5, "buzzer level depends on the sample rate");
        std::cout << "BUZZER forte_full_db=" << rattle << " rate_error_db=" << rateError << '\n';
    }

    std::cout << "Round4ResponseTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
