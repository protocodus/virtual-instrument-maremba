// Balafon/kalimba physical response and complete Close capture contracts.
// Fixed seeds; the source probe observes real engine-configured mechanics.
//  1. A pluck starts from the tine's continuous motion: no step at the
//     thumb's touch in the board's sound (every technique), re-plucks click-free.
//  2. Buzzers answer the ring: the default Buzz is clean for every technique;
//     at full Buzz a forte pick rattles a treble tine, a flesh pluck rattles
//     no more than a pick.
//  3. The usual thumb pluck tinks as a measured box alto does (Holdaway:
//     17-26 dB under the fundamental 0.1 s later; F#4, allowed 17-30 dB).
//  4. Stereo: the three physical emitters reach the measured ears with their
//     own geometry, phase, spectrum and calibration; left/right ITD is correct.
//  5. A balafon in a traditional tuning has only its degree keys to ring along.
#include "MarembaEngine.h"
#include "SympatheticMesh.h"
#include "Tunings.h"
#include <array>
#include <cmath>
#include <stdexcept>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


namespace maremba {
struct MarembaEngineTestAccess {
    using Emission = std::array<std::array<float, 8>, 4>; // tine L/R, board, hole
    static int Factor(const MarembaEngine& e) { return e.mActiveFactor; }
    static void Fade(MarembaEngine& e) {
        for (auto& voice : e.mVoices) if (voice.IsActive()) voice.StartFade(.003);
    }
    // Advance the engine-configured physical state without any acoustic
    // propagation, output filter or safety delay. This is not a production
    // bypass. Fixtures exclude rolls, shared halo and control automation.
    static Emission PhysicalFrame(MarembaEngine& e) {
        if (e.mParams.model != 3 || e.mParams.sympathetic != 0 || e.mParams.modWheel != 0
            || e.mParams.velocityToBloom != 0 || e.mParams.velocityToSympathetic != 0)
            throw std::runtime_error("Kalimba source probe requires fixed controls and no halo");
        Emission out{};
        e.mClock += 1;
        const float remaining = 1 - e.mBodyGain;
        for (int sub = 0; sub < e.mActiveFactor; ++sub) {
            KalimbaBoxDrive drive;
            for (int v = 0; v < MarembaEngine::kVoicePoolSize; ++v) {
                auto& voice = e.mVoices[v];
                if (!voice.IsActive()) continue;
                const int note = voice.GetNoteNumber();
                float close = 0, room = 0, piezo = 0, ears[2]{};
                if (!voice.ProcessSample(close, room, piezo, ears)) continue;
                out[0][sub] += ears[0]; out[1][sub] += ears[1];
                const auto& route = e.mRoutes[v];
                const float body = remaining > 0 ? std::min(1.f, route.body / remaining) : 0;
                drive.Add(voice.GetBridgeForce(), route.boxAir, body, KalimbaTineSide(note));
            }
            if (drive.board != 0 || e.mKalimbaBodyRunning) {
                double close = 0, room = 0, plate = 0, side = 0, emitted[2]{};
                e.mKalimbaBody.Process(drive, .6 + .8 * e.mBodyGain, .8 * remaining,
                                      close, room, plate, side, emitted);
                // Production's board-pressure calibration .072 and old
                // equal-ear coefficient .5, before the .6/r capture scale.
                out[2][sub] = .5f * static_cast<float>(emitted[0] * .072);
                out[3][sub] = .5f * static_cast<float>(emitted[1] * .072);
                e.mKalimbaBodyRunning = drive.board != 0 || e.mKalimbaBody.Energy() > 1e-20;
            }
        }
        if (++e.mControlCounter >= MarembaEngine::kControlInterval) {
            e.mControlCounter = 0;
            e.ControlTick();
        }
        return out;
    }
};
}

namespace {
constexpr double kRate = 48000;
struct Out { std::vector<double> l, r; double rate = kRate; };
using Notes = std::vector<std::pair<double, std::pair<int, float>>>;
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
// Mechanical continuity must be observed before the acoustic and output
// delays; testing time zero on the complete recording merely tests silence.
Out RenderRaw(const maremba::EngineParameters& p, const Notes& notes, double seconds) {
    auto engine = std::make_unique<maremba::MarembaEngine>(kRate);
    engine->SetParameters(p); engine->Reset();
    const int factor = maremba::MarembaEngineTestAccess::Factor(*engine);
    Out out; out.rate = kRate * factor;
    const int frames = static_cast<int>(seconds * kRate);
    out.l.reserve(frames * factor); out.r.reserve(frames * factor);
    size_t next = 0;
    for (int frame = 0; frame < frames; ++frame) {
        // Match the original fixture's 64-host-frame event placement.
        if (frame % 64 == 0) while (next < notes.size() && notes[next].first * kRate <= frame) {
            engine->NoteOn(notes[next].second.first, notes[next].second.second); ++next;
        }
        const auto source = maremba::MarembaEngineTestAccess::PhysicalFrame(*engine);
        for (int sub = 0; sub < factor; ++sub) {
            out.l.push_back(source[0][sub] + source[2][sub] + source[3][sub]);
            out.r.push_back(source[1][sub] + source[2][sub] + source[3][sub]);
        }
    }
    return out;
}
double Db(double x) { return 10 * std::log10(std::max(x, 1e-30)); }
double Energy(const std::vector<double>& x, double a, double b, double rate = kRate) {
    double e = 0; const size_t i0 = static_cast<size_t>(a * rate), i1 = std::min(x.size(), static_cast<size_t>(b * rate));
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
std::complex<double> Transform(const double* data, size_t count, double hz, double rate) {
    const auto step = std::polar(1.0, -2 * M_PI * hz / rate);
    std::complex<double> phase{1, 0}, sum{};
    for (size_t i = 0; i < count; ++i) { sum += data[i] * phase; phase *= step; }
    return sum;
}
std::complex<double> Hrtf(const maremba::CloseHrtfProfile& profile, int ear, double hz) {
    std::complex<double> value{};
    for (int k = 0; k < maremba::kCloseHrtfTaps; ++k)
        value += static_cast<double>(profile.impulse[ear][k])
            * std::polar(1.0, -2 * M_PI * hz * k / maremba::kCloseHrtfReferenceRate);
    return value;
}
// Measured ears are not mirror copies. Compare low-band ITD to the centre
// reference, not total narrowband L/R energy after coherent board/hole mixing.
double Itd(int note) {
    const auto& h = maremba::kCloseHrtfProfiles[maremba::kCloseHrtfSourceProfile[
        maremba::CaptureSourceIndex(maremba::EMarimbaModel::KalimbaArtisan, note)]];
    constexpr double frequency = 500;
    return h.residualItdSeconds - std::arg(Hrtf(h, 1, frequency) / Hrtf(h, 0, frequency))
        / (2 * M_PI * frequency);
}
struct ArrivalResult { double magnitude = 0, phase = 0, peak = 0, error = 0; int bins = 0; bool audible = true, complete = true; };
ArrivalResult CheckArrival(int note, double rate, int quality) {
    using namespace maremba;
    auto p = Kalimba(1); p.volume = .08f; p.preampDrive = p.compAmount = p.warmth = 0;
    p.oversampling = quality; p.closeLevel = 1; p.farLevel = p.piezoLevel = 0;
    auto actual = std::make_unique<MarembaEngine>(rate);
    auto physical = std::make_unique<MarembaEngine>(rate);
    actual->SetParameters(p); actual->Reset(); actual->NoteOn(note, .8f);
    physical->SetParameters(p); physical->Reset(); physical->NoteOn(note, .8f);
    const int factor = MarembaEngineTestAccess::Factor(*physical);
    const int frames = static_cast<int>(std::ceil(.30 * rate));
    std::array<std::vector<double>, 4> source;
    std::array<std::vector<double>, 2> output;
    std::array<MonoOversampler, 4> decimator;
    for (auto& lane : source) lane.resize(frames);
    for (auto& lane : output) lane.resize(frames);
    ArrivalResult result;
    for (int frame = 0; frame < frames; ++frame) {
        if (frame == static_cast<int>(std::ceil(.04 * rate))) {
            MarembaEngineTestAccess::Fade(*actual); MarembaEngineTestAccess::Fade(*physical);
        }
        const auto raw = MarembaEngineTestAccess::PhysicalFrame(*physical);
        for (int lane = 0; lane < 4; ++lane)
            source[lane][frame] = decimator[lane].DownsampleFrame(raw[lane].data(), factor);
        float mainL = 0, mainR = 0, closeL = 0, closeR = 0;
        actual->RenderBatch(&mainL, &mainR, &closeL, &closeR, nullptr, nullptr, nullptr, 1);
        output[0][frame] = closeL; output[1][frame] = closeR;
        result.peak = std::max({result.peak, std::abs(double(mainL)), std::abs(double(mainR)),
                               std::abs(double(closeL)), std::abs(double(closeR))});
        if ((frame + 1) % 16 == 0) for (auto& filter : decimator) filter.FlushTiny();
    }
    // Each independent emitter must really contribute. Containing the whole
    // finite strike plus tails permits an unwindowed complex transfer oracle.
    for (const auto& lane : source) {
        const double energy = Energy(lane, 0, .30, rate);
        result.audible = result.audible && energy > 1e-12;
        result.complete = result.complete && Energy(lane, .29, .30, rate) < 1e-18 * energy;
    }
    for (const auto& lane : output) {
        const double energy = Energy(lane, 0, .30, rate);
        result.audible = result.audible && energy > 1e-16;
        result.complete = result.complete && Energy(lane, .29, .30, rate) < 1e-18 * energy;
    }
    const std::array<int, 3> ids{CaptureSourceIndex(EMarimbaModel::KalimbaArtisan, note),
                                kCaptureBoardSource, kCaptureHoleSource};
    const float gain = CaptureScene::kMasterMixScale * .06f * p.volume * p.volume;
    const std::array<double, 9> frequencies{100, 300, 500, 1000, 2000, 4000, 8000, 12000,
                                          440 * std::exp2((note - 69) / 12.0)};
    for (int ear = 0; ear < 2; ++ear) for (double hz : frequencies) {
        if (hz > .35 * rate) continue;
        std::complex<double> expected{};
        double contributions = 0;
        for (int emitter = 0; emitter < 3; ++emitter) {
            const int lane = emitter == 0 ? ear : emitter + 1;
            const auto& h = kCloseHrtfProfiles[kCloseHrtfSourceProfile[ids[emitter]]];
            const double distance = CaptureDistance(CaptureSourcePosition(ids[emitter]), CloseHeadCentre());
            const double delay = distance / kCaptureSpeedOfSound + (ear ? .5 : -.5) * h.residualItdSeconds
                + OutputHeadroom::LatencyFrames(rate) / rate;
            const auto wave = Transform(source[lane].data(), source[lane].size(), hz, rate)
                * Hrtf(h, ear, hz) * (CloseCapture::kReferenceDistance / distance)
                * double(gain) * std::polar(1.0, -2 * M_PI * hz * delay);
            expected += wave; contributions += std::abs(wave);
        }
        const auto measured = Transform(output[ear].data(), output[ear].size(), hz, rate);
        // Normalized complex error remains meaningful at destructive sums.
        // The magnitude/phase ratio additionally requires a sum retaining at
        // least one quarter of component magnitude (condition number <=4).
        // Strongly cancelling bins still face the same complex-error gate.
        result.error = std::max(result.error, std::abs(measured - expected) / std::max(contributions, 1e-30));
        if (std::abs(expected) < .25 * contributions || contributions < 1e-10) continue;
        const auto ratio = measured / expected;
        result.magnitude = std::max(result.magnitude, std::abs(20 * std::log10(std::abs(ratio))));
        result.phase = std::max(result.phase, std::abs(std::arg(ratio)));
        ++result.bins;
    }
    std::cout << "ARRIVAL note=" << note << " rate=" << rate << " factor=" << factor
              << " bins=" << result.bins << " magnitude_db=" << result.magnitude
              << " phase_rad=" << result.phase << " complex_error=" << result.error
              << " peak=" << result.peak << " audible=" << result.audible
              << " complete=" << result.complete << '\n';
    return result;
}
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& what) { if (!ok) { ++failures; std::cerr << "FAIL " << what << '\n'; } };

    // 1. Continuous pluck: the first 0.1 ms carry no step (re the note's peak),
    //    and a re-pluck's high band stays under the ring.
    for (int technique = 0; technique < 4; ++technique) {
        for (int note : {60, 72, 84}) {
            const auto o = RenderRaw(Kalimba(technique), {{0.0, {note, 1.0f}}}, 0.3);
            double peak = 0, onset = 0;
            for (size_t i = 0; i < o.l.size(); ++i) peak = std::max(peak, std::abs(o.l[i] + o.r[i]));
            for (size_t i = 0; i < static_cast<size_t>(0.0001 * o.rate) + 1; ++i) onset = std::max(onset, std::abs(o.l[i] + o.r[i]));
            std::cout << "ONSET technique=" << technique << " note=" << note << " step_re_peak=" << onset / peak << '\n';
            check(peak > 1e-8 && onset > 0, "physical onset probe is audible and not hidden behind capture delay");
            check(onset < 0.05 * peak, "a pluck starts with a step (technique " + std::to_string(technique) + ", note " + std::to_string(note) + ")");
        }
    }
    {
        // Natural thumb re-plucks of a ringing F#4: second differences of
        // raw physical pressure weight the high band, in the 2 ms after
        // each touch versus the preceding ring. No capture delay can hide it.
        std::vector<std::pair<double, std::pair<int, float>>> notes;
        for (int k = 0; k < 5; ++k) notes.push_back({0.2 * k, {66, 0.7f}});
        const auto o = RenderRaw(Kalimba(1), notes, 1.0);
        std::vector<double> d(o.l.size());
        for (size_t i = 2; i < d.size(); ++i) { const double m0 = o.l[i] + o.r[i], m1 = o.l[i - 1] + o.r[i - 1], m2 = o.l[i - 2] + o.r[i - 2]; d[i] = m0 - 2 * m1 + m2; }
        double worst = -300;
        for (int k = 1; k < 5; ++k) {
            const double t = std::ceil(0.2 * k * kRate / 64) * 64 / kRate;
            const double before = Energy(d, t - 0.03, t - 0.005, o.rate);
            const double after = Energy(d, t, t + 0.002, o.rate);
            check(before > 1e-20 && after > 1e-20, "raw repluck and preceding ring are nonzero");
            worst = std::max(worst, Db(after / before));
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

    // 4. Complete three-emitter Close capture: independent measured FIR,
    // geometric flight/range, residual ITD and common output latency oracle.
    // A static pan/whole-signal energy ordering cannot describe this coherent
    // narrowband tine + board + oppositely signed hole recording.
    {
        const double centre = Itd(60), leftNear = Itd(62), rightNear = Itd(64);
        const double left = Itd(86), right = Itd(88);
        std::cout << "ITD microseconds C4=" << centre * 1e6 << " D4=" << leftNear * 1e6
                  << " E4=" << rightNear * 1e6 << " D6=" << left * 1e6 << " E6=" << right * 1e6 << '\n';
        check(leftNear > centre + 5e-6 && rightNear < centre - 5e-6,
              "inner tine ITDs retain their sides relative to the measured centre");
        check(left > 40e-6 && right < -40e-6, "outer tine ITDs retain anatomical left/right");
        const auto arrival = [&](int note, double rate, int quality) {
            const auto result = CheckArrival(note, rate, quality);
            check(result.audible && result.complete, "three-source strike and tails are fully observed");
            check(result.peak > 1e-6 && result.peak < .1, "arrival fixture is audible below output protection");
            check(result.bins >= 12, "enough energized bins exercise both complete ear paths");
            check(result.error < .02, "complete Close retains the three-emitter complex wave sum");
            check(result.magnitude < .2, "complete Close retains measured three-emitter spectral balance");
            check(result.phase < .02, "complete Close preserves physical flight, ITD and common2ms once");
        };
        for (int note : {60,62,64,86,88}) for (double rate : {44100.,48000.,96000.}) arrival(note, rate, 0);
        for (int quality : {1,2}) for (int note : {60,86,88}) arrival(note, 48000, quality);
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
