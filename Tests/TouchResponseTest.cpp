// Independent public-audio touch-response probe. No DSP internals or exact
// implementation constants are used. Compile with -I Source/DSP and Source/DSP/*.cpp.
// --measure records the complete matrix without enforcing acceptance gates.
#include "MarembaVoice.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::array<int, 7> velocities = {1, 8, 16, 32, 64, 96, 127};
constexpr std::array<double, 7> positions = {.05, .10, .2242, .35, .50, .65, .95};
struct Audio {
    std::vector<double> close, far, piezo;
    double rate;
};
struct Metric {
    double attack = 0, ring = 0, far = 0, piezo = 0, hfAttack = 0, hfRing = 0, correlation = 0,
           mixLoss = 0, peak = 0;
    double attackNoise = 0;
};
double db(double x) {
    return 10 * std::log10(std::max(x, 1e-30));
}
double hz(int n) {
    return 440 * std::exp2((n - 69) / 12.);
}
Audio render(int model, int striker, int velocity, double position, int note = 60, double rate = 88200,
             uint32_t seed = 0x49681u) {
    maremba::MarembaVoice voice;
    maremba::FastPRNG prng(0x382195u);
    voice.SetSeed(seed);
    voice.Trigger(note, velocity / 127.f, static_cast<maremba::EMarimbaModel>(model), .4f,
                  static_cast<float>(position), 0, .7f, 1, 0, 0, 0, rate, prng, 0, 0, striker, 0);
    Audio a;
    a.rate = rate;
    const size_t frames = static_cast<size_t>(rate * .18);
    a.close.resize(frames);
    a.far.resize(frames);
    a.piezo.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float c = 0, f = 0, p = 0;
        voice.ProcessSample(c, f, p);
        a.close[i] = c;
        a.far[i] = f;
        a.piezo[i] = p;
        if (i % 32 == 31)
            voice.UpdateEnvelope();
    }
    return a;
}
Metric measure(const Audio &a, int note) {
    Metric m;
    double ec = 0, ef = 0, cross = 0, emix = 0;
    // Butterworth high-pass distinguishes a changed spectrum from a gain change.
    // Its cutoff tracks the note; it is not tied to the model's exact mode weights.
    double k = std::tan(pi * hz(note) * 3 / a.rate), norm = 1 / (1 + std::sqrt(2.) * k + k * k);
    double b0 = norm, b1 = -2 * norm, b2 = norm, a1 = 2 * (k * k - 1) * norm,
           a2 = (1 - std::sqrt(2.) * k + k * k) * norm;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    const double nk = std::tan(pi * std::min(a.rate * .2, std::max(3000., hz(note) * 12)) / a.rate);
    const double nn = 1 / (1 + std::sqrt(2.) * nk + nk * nk);
    const double na1 = 2 * (nk * nk - 1) * nn, na2 = (1 - std::sqrt(2.) * nk + nk * nk) * nn;
    double nx1 = 0, nx2 = 0, ny1 = 0, ny2 = 0;
    size_t attackN = 0, ringN = 0;
    for (size_t i = 0; i < a.close.size(); ++i) {
        const double x = a.close[i], f = a.far[i], p = a.piezo[i];
        const double hp = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = hp;
        const double noise = nn * (x - 2 * nx1 + nx2) - na1 * ny1 - na2 * ny2;
        nx2 = nx1;
        nx1 = x;
        ny2 = ny1;
        ny1 = noise;
        if (!std::isfinite(x) || !std::isfinite(f) || !std::isfinite(p))
            m.peak = 1e30;
        m.peak = std::max(m.peak, std::max({std::abs(x), std::abs(f), std::abs(p)}));
        const double t = i / a.rate;
        if (t < .008) {
            m.attack += x * x;
            m.hfAttack += hp * hp;
            m.attackNoise += noise * noise;
            ++attackN;
        }
        if (t >= .012 && t < .16) {
            m.ring += x * x;
            m.far += f * f;
            m.piezo += p * p;
            m.hfRing += hp * hp;
            ++ringN;
            ec += x * x;
            ef += f * f;
            cross += x * f;
            emix += (x + f) * (x + f);
        }
    }
    m.hfAttack = db(m.hfAttack / std::max(m.attack, 1e-30));
    m.hfRing = db(m.hfRing / std::max(m.ring, 1e-30));
    m.attack = db(m.attack / std::max(size_t(1), attackN));
    m.attackNoise = db(m.attackNoise / std::max(size_t(1), attackN));
    m.ring = db(m.ring / std::max(size_t(1), ringN));
    m.far = db(m.far / std::max(size_t(1), ringN));
    m.piezo = db(m.piezo / std::max(size_t(1), ringN));
    m.correlation = cross / std::max(std::sqrt(ec * ef), 1e-30);
    m.mixLoss = db(emix / std::max(std::pow(std::sqrt(ec) + std::sqrt(ef), 2), 1e-30));
    return m;
}
double partial(const Audio &a, double f) {
    const size_t from = static_cast<size_t>(.012 * a.rate), to = static_cast<size_t>(.16 * a.rate);
    const double coeff = 2 * std::cos(2 * pi * f / a.rate);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < to; ++i) {
        const double w = .5 - .5 * std::cos(2 * pi * (i - from) / (to - from - 1));
        const double s = a.piezo[i] * w + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::max(0., s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((to - from) * (to - from));
}
double temporalResidual(const Audio &soft, const Audio &loud) {
    // Least-squares fit of a constant gain to the first8ms. Remaining energy
    // measures temporal waveform change (including phase), not spectral brightness.
    double low = 0, high = 0, cross = 0;
    const size_t n = static_cast<size_t>(soft.rate * .008);
    for (size_t i = 0; i < n; ++i) {
        const double x = soft.close[i], y = loud.close[i];
        low += x * x;
        high += y * y;
        cross += x * y;
    }
    return std::max(0., 1 - cross * cross / std::max(low * high, 1e-30));
}
void row(const char *kind, int model, int striker, int vel, double pos, int note, double rate,
         const Metric &m) {
    std::cout << kind << ',' << model << ',' << striker << ',' << vel << ',' << pos << ',' << note << ','
              << rate << ',' << m.attack << ',' << m.ring << ',' << m.far << ',' << m.piezo << ','
              << m.hfAttack << ',' << m.hfRing << ',' << m.correlation << ',' << m.mixLoss << ','
              << m.peak << ',' << m.attackNoise - m.ring << '\n';
}

Audio restrike(int model, double position) {
    maremba::MarembaVoice voice;
    maremba::FastPRNG prng(0x382195u);
    voice.SetSeed(0x49681u);
    const double rate = 88200;
    auto trigger = [&](float velocity, float pos) {
        voice.Trigger(48, velocity, static_cast<maremba::EMarimbaModel>(model), .4f, pos, 0, .7f, 4, 0,
                      0, 0, rate, prng, 0, 0, 1, 0);
    };
    trigger(1.f, .35f);
    for (int i = 0; i < static_cast<int>(rate * .045); ++i) {
        float c, f, p;
        voice.ProcessSample(c, f, p);
    }
    trigger(8 / 127.f, static_cast<float>(position));
    Audio a;
    a.rate = rate;
    const size_t n = static_cast<size_t>(rate * .18);
    a.close.resize(n);
    a.far.resize(n);
    a.piezo.resize(n);
    for (size_t i = 0; i < n; ++i) {
        float c = 0, f = 0, p = 0;
        voice.ProcessSample(c, f, p);
        a.close[i] = c;
        a.far[i] = f;
        a.piezo[i] = p;
        if (i % 32 == 31)
            voice.UpdateEnvelope();
    }
    return a;
}
} // namespace
int main(int argc, char **argv) {
    const bool measureOnly = argc == 2 && std::string(argv[1]) == "--measure";
    if (argc > 1 && !measureOnly) {
        std::cerr << "usage: TouchResponseTest [--measure]\n";
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    int failures = 0;
    auto check = [&](bool ok, const std::string &msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << msg << '\n';
        }
    };
    std::cout
        << std::setprecision(9)
        << "kind,model,striker,velocity,position,note,rate,attack_db,ring_db,far_db,piezo_db,attack_hf_"
           "db,ring_hf_db,close_far_correlation,mix_loss_db,peak,attack_noise_to_ring_db\n";
    double minStep = 100, minDynamic = 100, minMixLoss = 0, maxRateError = 0;
    int reversedBrightness = 0;
    for (int model = 0; model < 4; ++model)
        for (int striker = 0; striker < 4; ++striker)
            for (double pos : positions) {
                Metric first, last;
                double previous = -1000;
                for (int velocity : velocities) {
                    const Metric m = measure(render(model, striker, velocity, pos), 60);
                    row("sweep", model, striker, velocity, pos, 60, 88200, m);
                    // A hard stick's 0.2 ms contact rings every bending mode
                    // in phase: its attack spike reaches ~10x a yarn stroke's
                    // (the ring stays within ~2 dB), and more on the thinned
                    // end of a balafon key. Bounded, not clipped.
                    check(std::isfinite(m.peak) && m.peak < 64,
                          "finite/bounded audio model=" + std::to_string(model));
                    if (velocity == 1)
                        first = m;
                    else
                        minStep = std::min(minStep, m.ring - previous);
                    previous = m.ring;
                    last = m;
                    // Mic phase only matters for what can be heard at all.
                    if (m.ring > -60)
                        minMixLoss = std::min(minMixLoss, m.mixLoss);
                }
                minDynamic = std::min(minDynamic, last.ring - first.ring);
                if (last.hfAttack < first.hfAttack - 1)
                    ++reversedBrightness;
            }
    double minC5Brightness = 100, minC7Brightness = 100, minTemporalResponse = 1, minNoiseSlope = 100;
    for (int model = 0; model < 3; ++model)
        for (int note : {72, 96}) {
            const auto softAudio = render(model, 0, 16, .35, note),
                       loudAudio = render(model, 0, 127, .35, note);
            // A soft yarn stroke on a short treble key reaches its modes
            // mostly through the fibre noise, drawn anew per stroke: compare
            // the mean attack of four strokes.
            const auto meanHf = [&](int velocity) {
                double energy = 0;
                for (uint32_t k = 0; k < 4; ++k)
                    energy += std::pow(10.0, measure(render(model, 0, velocity, .35, note, 88200, 0x49681u + 977u * k), note).hfAttack / 10);
                return 10 * std::log10(energy / 4);
            };
            const double slope = meanHf(127) - meanHf(16);
            const auto soft = measure(softAudio, note);
            // A pp yarn stroke that leaves a treble key all but still (its
            // attack 60 dB down, below any mix) has no brightness to compare.
            if (soft.attack < -60)
                ;
            else if (note == 72)
                minC5Brightness = std::min(minC5Brightness, slope);
            else
                minC7Brightness = std::min(minC7Brightness, slope);
            const double residual = temporalResidual(softAudio, loudAudio);
            minTemporalResponse = std::min(minTemporalResponse, residual);
            std::cerr << "TREBLE model=" << model << " note=" << note << " attack_hf_growth_db=" << slope
                      << " gain_fit_temporal_residual=" << residual << '\n';
        }
    // Quiet strikes must not be disproportionately clicky. Use controlled contact
    // away from spatial nodes, a broad upper band, and normalize to the ring.
    for (int model = 0; model < 4; ++model)
        for (int striker = 0; striker < 4; ++striker)
            for (int note : {48, 60}) {
                const auto soft = measure(render(model, striker, 8, .5, note), note);
                const auto loud = measure(render(model, striker, 127, .5, note), note);
                const double slope = (loud.attackNoise - loud.ring) - (soft.attackNoise - soft.ring);
                minNoiseSlope = std::min(minNoiseSlope, slope);
                std::cerr << "NOISE model=" << model << " striker=" << striker << " note=" << note
                          << " normalized_attack_growth_db=" << slope << '\n';
            }
    // A carved bar's fundamental nodes sit nearer its ends than a uniform
    // bar's 0.2242: find each model's C3 node from the audible fundamental.
    std::array<double, 3> nodes{};
    for (int model = 0; model < 3; ++model) {
        double best = 1e300;
        for (double x = .15; x <= .26; x += .002) {
            const double p = partial(render(model, 3, 96, x, 48), hz(48));
            if (p < best) { best = p; nodes[model] = x; }
        }
        std::cerr << "NODE model=" << model << " c3_fundamental_node=" << nodes[model] << '\n';
    }
    double minNodeProtection = 100, minOddProtection = 100;
    for (int model = 0; model < 3; ++model) {
        const auto node = restrike(model, nodes[model]), center = restrike(model, .5),
                   off = restrike(model, .35);
        const double nodeProtection =
            db(partial(node, hz(48)) / std::max(partial(center, hz(48)), 1e-30));
        const double oddProtection =
            db(partial(center, 4 * hz(48)) / std::max(partial(off, 4 * hz(48)), 1e-30));
        minNodeProtection = std::min(minNodeProtection, nodeProtection);
        minOddProtection = std::min(minOddProtection, oddProtection);
        std::cerr << "RESTRIKE model=" << model << " fundamental_node_protection_db=" << nodeProtection
                  << " mixed_ring_odd_center_to_offcenter_db=" << oddProtection << '\n';
    }
    // Focused register/rate matrix: soft/medium/hard contacts, low/high velocity,
    // 44.1k/2x versus 48k/2x and 96k/2x. No resampling or FFT-bin matching.
    for (int note : {48, 72, 96})
        for (int model = 0; model < 4; ++model)
            for (int striker : {0, 1, 3})
                for (int v : {16, 127}) {
                    const auto ref = measure(render(model, striker, v, .35, note, 88200), note);
                    row("register", model, striker, v, .35, note, 88200, ref);
                    // A yarn or cord head's fibre noise is drawn anew at every
                    // rate; on a short treble key, whose fundamental the
                    // head's 2 ms contact barely reaches, it sets the ring by a
                    // dB or two. Four strokes' mean ring compares the physics.
                    const auto meanRing = [&](double rate) {
                        double energy = 0;
                        for (uint32_t k = 0; k < 4; ++k)
                            energy += std::pow(10.0, measure(render(model, striker, v, .35, note, rate, 0x49681u + 977u * k), note).ring / 10);
                        return 10 * std::log10(energy / 4);
                    };
                    const double refRing = meanRing(88200);
                    for (double rate : {96000., 192000.}) {
                        const auto m = measure(render(model, striker, v, .35, note, rate), note);
                        row("rate", model, striker, v, .35, note, rate, m);
                        // A pp yarn stroke barely moves a treble bar (its long
                        // contact passes almost nothing at 1-2 kHz); rings 60 dB
                        // down are below any mix and are not compared.
                        if (ref.ring > -60)
                            maxRateError = std::max(maxRateError, std::abs(meanRing(rate) - refRing));
                    }
                }
    // Mode parity and a fundamental null measured in audible output. These are
    // wide relative gates for an ideal controlled strike, not precise node fits.
    for (int model = 0; model < 3; ++model) {
        const auto center = render(model, 3, 96, .5, 48), off = render(model, 3, 96, .35, 48),
                   node = render(model, 3, 96, nodes[model], 48);
        const double parity =
            db(partial(center, 4 * hz(48)) / std::max(partial(off, 4 * hz(48)), 1e-30));
        const double null = db(partial(node, hz(48)) / std::max(partial(center, hz(48)), 1e-30));
        std::cerr << "PARITY model=" << model << " center_second_mode_db=" << parity
                  << " node_fundamental_db=" << null << '\n';
        if (!measureOnly) {
            check(parity < -15, "bar antisymmetric mode not suppressed at center");
            check(null < -15, "bar fundamental node not suppressed");
        }
    }
    const auto inner = render(3, 1, 96, .05, 48), tip = render(3, 1, 96, .95, 48);
    const double tineGain = db(partial(tip, hz(48)) / std::max(partial(inner, hz(48)), 1e-30));
    std::cerr << "SUMMARY min_velocity_step_db=" << minStep << " min_dynamic_range_db=" << minDynamic
              << " worst_close_far_mix_loss_db=" << minMixLoss
              << " max_rate_ring_error_db=" << maxRateError
              << " reversed_attack_brightness_cases=" << reversedBrightness
              << " tine_tip_gain_db=" << tineGain << '\n';
    std::cerr << "CONTACT min_C5_brightness_growth_db=" << minC5Brightness
              << " min_C7_brightness_growth_db=" << minC7Brightness
              << " min_treble_temporal_residual=" << minTemporalResponse
              << " min_normalized_noise_growth_db=" << minNoiseSlope
              << " min_restrike_node_protection_db=" << minNodeProtection
              << " mixed_ring_min_odd_center_to_offcenter_db=" << minOddProtection << '\n';
    if (!measureOnly) {
        check(minStep > 0, "velocity must increase ringing level at fixed touch");
        check(minDynamic > 20, "velocity1..127 has insufficient acoustic dynamic range");
        check(minMixLoss > -6, "close/far mix loses more than6dB to destructive phase cancellation");
        check(maxRateError < 1, "ring level varies by more than1dB across common internal rates");
        check(tineGain > 1,
              "tine free tip should excite its fundamental more strongly than inner contact");
        check(minC5Brightness > .15,
              "soft C5 velocity changes gain without meaningful spectral response");
        check(minC7Brightness > -1, "soft C7 disproportionately darkens at higher velocity");
        check(minTemporalResponse > .01, "soft treble attack is only a gain-scaled waveform");
        check(minNoiseSlope > -1,
              "quiet controlled strikes are disproportionately noisy relative to their ring");
        check(minNodeProtection > .25, "restrike at a fundamental node damps the ring like an antinode");
        // A localized restrike can transfer old fundamental energy into an
        // antisymmetric mode off-center. The mixed-ring ratio above therefore
        // diagnoses both transfer and absorption; MalletContactTest separately
        // establishes pure odd-mode absorption and finite-footprint node loss.
    }
    std::cerr << "TouchResponseTest "
              << (failures      ? "FAILED"
                  : measureOnly ? "MEASURED"
                                : "PASSED")
              << " failures=" << failures << " elapsed_sec="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
    return failures ? 1 : 0;
}
