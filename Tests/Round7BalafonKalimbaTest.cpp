// Proposed contracts for the balafon/kalimba realism audit (scratch; not in
// the repository). Engine API only, dry close output, fixed seeds.
//  1. A pluck starts from the tine's continuous motion: no step at the
//     thumb's touch in the board's sound (every technique), re-plucks click-free.
//  2. Buzzers answer the ring: the default Buzz is clean for every technique;
//     at full Buzz a forte pick rattles a treble tine, a flesh pluck rattles
//     no more than a pick.
//  3. The usual thumb pluck tinks as a measured box alto does (Holdaway:
//     17-26 dB under the fundamental 0.1 s later; F#4, allowed 17-30 dB).
//  4. Stereo: a tine sounds from its side of the bridge (D6 left, E6 right,
//     C4 centred, D4/E4 mirrored); the mono sum stays the same.
//  5. A balafon in a traditional tuning has only its degree keys to ring along.
#include "MarembaEngine.h"
#include "SympatheticMesh.h"
#include "Tunings.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr double kRate = 48000;
struct Out { std::vector<double> l, r; };
Out Render(maremba::EngineParameters p, const std::vector<std::pair<double, std::pair<int, float>>>& notes, double seconds) {
    auto e = std::make_unique<maremba::MarembaEngine>(kRate);
    e->SetParameters(p);
    e->Reset();
    Out o;
    const int n = static_cast<int>(seconds * kRate);
    o.l.resize(n); o.r.resize(n);
    float ml[64], mr[64], cl[64], cr[64];
    size_t next = 0;
    for (int i = 0; i < n; i += 64) {
        while (next < notes.size() && notes[next].first * kRate <= i) {
            e->NoteOn(notes[next].second.first, notes[next].second.second);
            ++next;
        }
        const int k = std::min(64, n - i);
        e->RenderBatch(ml, mr, cl, cr, nullptr, nullptr, nullptr, k);
        for (int j = 0; j < k; ++j) { o.l[i + j] = cl[j]; o.r[i + j] = cr[j]; }
    }
    return o;
}
maremba::EngineParameters Kalimba(int technique) {
    maremba::EngineParameters p;
    p.model = 3; p.malletType = technique; p.malletHardness = 0.5f; p.strikePosition = 0.5f;
    p.artifacts = p.strikeJitter = p.detune = p.sympathetic = p.farLevel = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToDecay = 0;
    return p;
}
double Db(double x) { return 10 * std::log10(std::max(x, 1e-30)); }
double Energy(const std::vector<double>& x, double a, double b) {
    double e = 0; const size_t i0 = static_cast<size_t>(a * kRate), i1 = std::min(x.size(), static_cast<size_t>(b * kRate));
    for (size_t i = i0; i < i1; ++i) e += x[i] * x[i];
    return e / std::max<size_t>(1, i1 - i0);
}
// Amplitude of a sinusoid near hz in [a, b) (Hann-windowed projection).
double Line(const std::vector<double>& x, double hz, double a, double b) {
    std::complex<double> s = 0; double w = 0;
    const size_t i0 = static_cast<size_t>(a * kRate), i1 = static_cast<size_t>(b * kRate);
    for (size_t i = i0; i < i1; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2 * M_PI * (i - i0) / (i1 - i0));
        s += x[i] * h * std::polar(1.0, -2 * M_PI * hz * i / kRate); w += h;
    }
    return std::abs(s) / w;
}
double PeakLine(const std::vector<double>& x, double hz, double a, double b) {
    double best = 0;
    for (int k = -40; k <= 40; ++k) best = std::max(best, Line(x, hz * (1 + 0.001 * k), a, b));
    return best;
}
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& what) { if (!ok) { ++failures; std::cerr << "FAIL " << what << '\n'; } };

    // 1. Continuous pluck: the first 0.1 ms carry no step (re the note's peak),
    //    and a re-pluck's high band stays under the ring.
    for (int technique = 0; technique < 4; ++technique) {
        for (int note : {60, 72, 84}) {
            const auto o = Render(Kalimba(technique), {{0.0, {note, 1.0f}}}, 0.3);
            double peak = 0, onset = 0;
            for (size_t i = 0; i < o.l.size(); ++i) peak = std::max(peak, std::abs(o.l[i] + o.r[i]));
            for (size_t i = 0; i < static_cast<size_t>(0.0001 * kRate) + 1; ++i) onset = std::max(onset, std::abs(o.l[i] + o.r[i]));
            std::cout << "ONSET technique=" << technique << " note=" << note << " step_re_peak=" << onset / peak << '\n';
            check(onset < 0.05 * peak, "a pluck starts with a step (technique " + std::to_string(technique) + ", note " + std::to_string(note) + ")");
        }
    }
    {
        // Natural thumb re-plucks of a ringing F#4: high band (>4 kHz, one-pole
        // differences) in the 2 ms after each touch vs the ring before it.
        std::vector<std::pair<double, std::pair<int, float>>> notes;
        for (int k = 0; k < 5; ++k) notes.push_back({0.2 * k, {66, 0.7f}});
        const auto o = Render(Kalimba(1), notes, 1.0);
        std::vector<double> d(o.l.size());
        for (size_t i = 2; i < d.size(); ++i) { const double m0 = o.l[i] + o.r[i], m1 = o.l[i - 1] + o.r[i - 1], m2 = o.l[i - 2] + o.r[i - 2]; d[i] = m0 - 2 * m1 + m2; }
        double worst = -300;
        for (int k = 1; k < 5; ++k) {
            const double t = std::ceil(0.2 * k * kRate / 64) * 64 / kRate;
            std::vector<double> sum(o.l.size());
            for (size_t i = 0; i < sum.size(); ++i) sum[i] = o.l[i] + o.r[i];
            worst = std::max(worst, Db(Energy(d, t, t + 0.002) / Energy(d, t - 0.03, t - 0.005)));
        }
        std::cout << "REPLUCK hf_burst_re_ring_db=" << worst << '\n';
        check(worst < 20, "a re-pluck clicks");
    }

    // 2. Buzzers: default Buzz clean for every technique; full Buzz rattles a
    //    forte picked C6; flesh no louder than a pick.
    {
        const auto rattle = [&](int technique, int note, float velocity, float buzz) {
            auto p = Kalimba(technique);
            const auto dry = Render(p, {{0.0, {note, velocity}}}, 0.6);
            p.buzzAmount = buzz;
            const auto wet = Render(p, {{0.0, {note, velocity}}}, 0.6);
            double e = 0, d = 0;
            for (size_t i = 0; i < dry.l.size(); ++i) { const double a = dry.l[i] + dry.r[i], b = wet.l[i] + wet.r[i]; e += a * a; d += (b - a) * (b - a); }
            return Db(d / e);
        };
        for (int technique = 0; technique < 4; ++technique)
            for (int note : {60, 72, 84, 96})
                check(rattle(technique, note, 1.0f, 0.15f) < -60, "default Buzz rattles technique " + std::to_string(technique) + " note " + std::to_string(note));
        const double pick = rattle(3, 84, 1.0f, 1.0f), flesh = rattle(0, 84, 1.0f, 1.0f);
        std::cout << "BUZZERS C6 full Buzz pick_db=" << pick << " flesh_db=" << flesh << '\n';
        check(pick > -30, "loose buzzers do not rattle under a forte pick");
        check(flesh <= pick, "a flesh pluck rattles the buzzers more than a pick");
    }

    // 3. Tink of the usual pluck (natural thumb, default position), F#4.
    {
        const auto o = Render(Kalimba(1), {{0.0, {66, 0.8f}}}, 0.4);
        std::vector<double> m(o.l.size());
        for (size_t i = 0; i < m.size(); ++i) m[i] = o.l[i] + o.r[i];
        const double f1 = 440 * std::exp2((66 - 69) / 12.0);
        const double ratio = maremba::GetKalimbaMount(f1).ratio[1];
        const double tink = 20 * std::log10(PeakLine(m, ratio * f1, 0.08, 0.12) / PeakLine(m, f1, 0.08, 0.12));
        std::cout << "TINK natural F#4 at 0.1 s re f1 db=" << tink << '\n';
        check(tink > -30 && tink < -17, "natural thumb tink outside the measured box alto's range");
    }

    // 4. Stereo from the bridge layout.
    {
        const auto balance = [&](int note) {
            const auto o = Render(Kalimba(1), {{0.0, {note, 0.8f}}}, 0.8);
            return Db(Energy(o.l, 0.0, 0.8) / Energy(o.r, 0.0, 0.8));
        };
        const double c4 = balance(60), d4 = balance(62), e4 = balance(64), d6 = balance(86), e6 = balance(88);
        std::cout << "STEREO L/R db C4=" << c4 << " D4=" << d4 << " E4=" << e4 << " D6=" << d6 << " E6=" << e6 << '\n';
        check(std::abs(c4) < 0.3, "C4 (the centre tine) is off centre");
        check(d6 > 2 && e6 < -2, "the outermost tines do not sound from their sides");
        check(d4 > 0.1 && e4 < -0.1 && std::abs(d4 + e4) < 0.3, "D4 and E4 are not mirrored about the centre");
    }

    // 5. Balafon halo in the Gyil tuning: only degree keys ring along.
    {
        for (int scale : {0, 5}) {
            maremba::SympatheticMesh mesh;
            mesh.Configure(kRate, 0.0, maremba::EMarimbaModel::BalafonAncestral, scale);
            double on = 0, off = 0;
            for (int i = 0; i < 4800; ++i) {
                mesh.BeginSample();
                mesh.AccumulateVoice(40, i < 48 ? 1.0 : 0.0);   // a struck E2 outside the scan, an impulse
                float l = 0, r = 0;
                mesh.Process(1.0f, l, r);
                for (int k = 0; k < mesh.ReceiverCount(); ++k) {
                    const double y = mesh.GetResonatorOutput(k);
                    (maremba::detail::IsDegreeKey(scale, mesh.ReceiverNote(k)) ? on : off) += y * y;
                }
            }
            std::cout << "HALO balafon scale=" << scale << " degree_keys_energy=" << on << " other_keys_energy=" << off << '\n';
            if (scale == 0) check(on > 0, "the chromatic balafon halo is silent");
            else check(on > 0 && off == 0, "keys a gyil does not have ring along");
        }
    }
    std::cout << "Round7BalafonKalimbaTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
