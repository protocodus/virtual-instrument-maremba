// Sixth physical-model pass: research-based fixes. Tube Q and tuning from the
// tube bank's losses and makers' practice, Hill's C2 decay ratio and the
// reference blooms; carving; membrane tension; the shaft and the dead stroke;
// the hand and thumb that stop a key; measured traditional keyboards; the
// kalimba's hole cover; the balafon's overtone at its playing position and
// its membrane-free top keys. Rendered checks use the engine's dry close output.
#include "MarembaEngine.h"
#include "KalimbaBody.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using maremba::EMarimbaModel;
using maremba::MarembaEngine;
constexpr double kPi = 3.14159265358979323846;
constexpr double rate = 48000;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
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

struct Event {
    double time;
    std::function<void(MarembaEngine&)> action;
};
std::vector<double> play(const maremba::EngineParameters& p, std::vector<Event> events, double seconds) {
    MarembaEngine engine(rate);
    engine.SetParameters(p);
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    std::vector<double> out;
    std::array<float, 64> l{}, r{}, cl{}, cr{};
    const size_t frames = static_cast<size_t>(seconds * rate);
    size_t next = 0;
    for (size_t start = 0; start < frames;) {
        while (next < events.size() && static_cast<size_t>(events[next].time * rate) <= start)
            events[next++].action(engine);
        size_t n = std::min<size_t>(16, frames - start);
        if (next < events.size()) n = std::min(n, static_cast<size_t>(events[next].time * rate) - start);
        engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr, nullptr, static_cast<int>(n));
        for (size_t i = 0; i < n; ++i) out.push_back(cl[i] + cr[i]);
        start += n;
    }
    return out;
}
Event on(double t, int note, float velocity) { return {t, [=](MarembaEngine& e) { e.NoteOn(note, velocity); }}; }
Event off(double t, int note) { return {t, [=](MarembaEngine& e) { e.NoteOff(note); }}; }
std::vector<double> strike(const maremba::EngineParameters& p, int note, float velocity, double seconds) {
    return play(p, {on(0, note, velocity)}, seconds);
}

// Hann-windowed line amplitude at a frequency over [from, to) seconds.
double line(const std::vector<double>& x, double frequency, double from, double to) {
    const size_t a = static_cast<size_t>(from * rate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    double re = 0, im = 0, weight = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = .5 - .5 * std::cos(2 * kPi * (i - a) / std::max<double>(1, b - a - 1));
        const double phase = 2 * kPi * frequency * i / rate;
        re += x[i] * w * std::cos(phase);
        im += x[i] * w * std::sin(phase);
        weight += w;
    }
    return std::sqrt(re * re + im * im) / std::max(weight, 1.e-30);
}
// The strongest line within +/-span of a frequency (cents), and its level.
double peakHz(const std::vector<double>& x, double guess, double from, double to, int span = 60) {
    double best = guess, bestLevel = -1;
    for (int c = -span; c <= span; ++c) {
        const double f = guess * std::exp2(c / 1200.0);
        const double level = line(x, f, from, to);
        if (level > bestLevel) { bestLevel = level; best = f; }
    }
    return best;
}
double energy(const std::vector<double>& x, double from = 0, double to = 1e9) {
    double e = 0;
    const size_t a = static_cast<size_t>(from * rate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return e;
}
std::vector<double> minus(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> d(std::min(a.size(), b.size()));
    for (size_t i = 0; i < d.size(); ++i) d[i] = a[i] - b[i];
    return d;
}
// Decay of the line at a frequency: dB per second between two windows.
double lineSlope(const std::vector<double>& x, double frequency, double t0, double t1, double width) {
    const double a = 20 * std::log10(std::max(line(x, frequency, t0, t0 + width), 1e-30));
    const double b = 20 * std::log10(std::max(line(x, frequency, t1, t1 + width), 1e-30));
    return (b - a) / (t1 - t0);
}
// The fundamental's bloom: its peak level in 20 ms frames over the first
// 0.3 s against the frame at 20 ms.
double bloomRise(const std::vector<double>& x, double frequency) {
    double peak = 0;
    for (double t = .02; t + .02 <= .3; t += .005) peak = std::max(peak, line(x, frequency, t, t + .02));
    return 20 * std::log10(peak / std::max(line(x, frequency, .02, .04), 1e-30));
}
} // namespace

int main() {
    std::cout << std::setprecision(5);

    // 1. Tube Q from the tube bank's losses (La Favre's bores; wall and mouth
    //    loss x 0.8, Soares et al.'s measured tube), tuned slightly above the
    //    bar and more than a half step up in the top octave (Stevens).
    {
        const auto tube = maremba::GetResonatorProfile(EMarimbaModel::ImperialRosewood);
        const double q2 = maremba::ResonatorQ(tube, hz(36)), q4 = maremba::ResonatorQ(tube, hz(60)),
                     q5 = maremba::ResonatorQ(tube, hz(72)), q7 = maremba::ResonatorQ(tube, hz(96));
        std::cout << "TUBE_Q C2=" << q2 << " C4=" << q4 << " C5=" << q5 << " C7=" << q7 << '\n';
        check(q2 > 80 && q2 < 95 && q4 > 58 && q4 < 68 && q5 > 28 && q5 < 36 && q7 < 5,
              "tube Q does not follow the tube bank's wall and mouth losses");
        for (int note = 48; note < 96; ++note)
            check(maremba::ResonatorQ(tube, hz(note + 1)) < maremba::ResonatorQ(tube, hz(note)),
                  "tube Q does not fall up the keyboard");
        auto soares = tube;
        soares.tubeRadiusM = .025 / std::pow(261.625565 / 174.0, .43);   // their 25 mm tube at 174 Hz
        const double measured = maremba::ResonatorQ(soares, 174.0);
        check(std::abs(measured - 68) < 2, "tube law misses Soares et al.'s measured Q 68 at 174 Hz");
        check(maremba::ResonatorTuningCents(tube, 36) == 3 && maremba::ResonatorTuningCents(tube, 60) == 8
              && maremba::ResonatorTuningCents(tube, 72) == 8 && maremba::ResonatorTuningCents(tube, 96) == 100,
              "tubes are not tuned slightly above their bars and a half step up at the top");
    }

    // 2. Hill's C2: with its tube the fundamental keeps about half the bare
    //    bar's decay (3.7 against 6.8 s, 0.54); the anechoic Iowa marimba
    //    keeps 0.68 (4.7 s). The tube under its bar lies between them.
    {
        auto p = clean(0);
        const auto tubed = strike(p, 36, .8f, 2.6);
        p.resonatorCoupling = 0;
        const auto bare = strike(p, 36, .8f, 2.6);
        const double withTube = lineSlope(tubed, hz(36), .5, 2.1, .4), without = lineSlope(bare, hz(36), .5, 2.1, .4);
        const double ratio = without / withTube;
        std::cout << "HILL C2 tube_T60=" << -60 / withTube << " bare_T60=" << -60 / without << " ratio=" << ratio << '\n';
        check(ratio > .45 && ratio < .75, "the C2 tube does not take Hill's share of the decay");
    }

    // 3. The bass bloom rises on the close microphone. The anechoic Iowa
    //    marimba rises 4.5 dB at G3 and 2.6 at C4 at 1.5 m by this measure;
    //    8 cm above the bar the bar's own near field fills the first frame
    //    more (TubeInSituTest checks the far microphone's bloom).
    {
        auto p = clean(0);
        p.strikePosition = .4f;
        const double g3 = bloomRise(strike(p, 55, .8f, .4), hz(55)), c4 = bloomRise(strike(p, 60, .8f, .4), hz(60));
        std::cout << "BLOOM G3=" << g3 << " C4=" << c4 << '\n';
        check(g3 > 3 && c4 > 1, "the bass bloom stays shallow");
    }

    // 4. Carving: the uncarved key has a flat bar's overtones; the undercut
    //    moves them up to the key as built; the fundamental stays the note.
    {
        for (int model = 0; model < 3; ++model) {
            const auto type = static_cast<EMarimbaModel>(model);
            const auto built = maremba::GetBarKey(type, 48), blank = maremba::GetBarKey(type, 48, 0),
                       half = maremba::GetBarKey(type, 48, .5);
            check(blank.ratio[0] == 1 && blank.ratio[1] > 2.6 && blank.ratio[1] < 2.85
                  && blank.ratio[2] > 5.1 && blank.ratio[2] < 5.5,
                  "an uncarved key does not ring like a flat bar");
            for (int m = 1; m < maremba::kBarModes; ++m) {
                check(half.ratio[m] > std::min(blank.ratio[m], built.ratio[m]) - 1e-12
                      && half.ratio[m] < std::max(blank.ratio[m], built.ratio[m]) + 1e-12
                      && half.ratio[m] > half.ratio[m - 1], "carving does not move the modes in order");
            }
            check(maremba::GetBarKey(type, 48, 1).ratio[1] == built.ratio[1], "full carving is not the key as built");
        }
        auto p = clean(0);
        p.carving = 0;
        const auto flat = strike(p, 48, .8f, .5);
        const double flatOvertone = line(flat, peakHz(flat, 2.75 * hz(48), .02, .3, 80), .02, .3);
        std::cout << "CARVING uncarved C3 overtone_2.75x_vs_4x_db="
                  << 20 * std::log10(flatOvertone / std::max(line(flat, 4 * hz(48), .02, .3), 1e-30)) << '\n';
        check(flatOvertone > 3 * line(flat, 4 * hz(48), .02, .3), "an uncarved bar still sounds its two-octave partial");
    }

    // 5. Membrane tension: a tighter membrane needs more pressure (threshold
    //    ~ tension^1.5, Bellet et al.), so it buzzes less at a given stroke.
    {
        auto p = clean(2);
        p.strikePosition = .43f;
        const auto dry = strike(p, 57, .8f, .4);
        p.buzzAmount = .6f;
        double previous = 1e9;
        for (float tension : {0.f, .5f, 1.f}) {
            p.membraneTension = tension;
            const double layer = db(energy(minus(strike(p, 57, .8f, .4), dry)) / energy(dry));
            std::cout << "TENSION balafon A3 tension=" << tension << " buzz_layer_db=" << layer << '\n';
            check(layer < previous, "a tighter membrane does not buzz less");
            previous = layer;
        }
    }

    // 6. Dead stroke: the head stays pressed while the key is held and lifts
    //    without a click.
    {
        auto p = clean(0);
        const auto ring = play(p, {on(0, 60, .8f), off(1.0, 60)}, 1.2);
        p.noteOffMode = 2;
        const auto dead = play(p, {on(0, 60, .8f), off(1.0, 60)}, 1.2);
        const double drop = db(energy(dead, .4, .5) / energy(ring, .4, .5));
        // The head eases off and leaves a stopped bar almost silent (the
        // supports carried the hand's push): no knock as it lifts.
        const double held = db(energy(dead, .95, 1.0) / energy(dead, 0, .05));
        const double lift = db(energy(dead, 1.0, 1.05) / energy(dead, 0, .05));
        std::cout << "DEAD rosewood C4 drop_at_0.4s_db=" << drop << " held_db_re_stroke=" << held
                  << " lift_db_re_stroke=" << lift << '\n';
        check(drop < -20, "a dead stroke does not stop the bar");
        check(lift < -50, "the dead stroke's mallet knocks as it lifts");
    }

    // 7. A muffling hand makes a low E decay 4-5 times faster (Burritt, via
    //    Schutz & Lipscomb 2007).
    {
        auto p = clean(0);
        const auto ring = strike(p, 40, .8f, 1.2);
        p.noteOffMode = 1;
        const auto damped = play(p, {on(0, 40, .8f), off(.2, 40)}, 1.2);
        const double free = lineSlope(ring, hz(40), .35, .75, .2), muffled = lineSlope(damped, hz(40), .35, .75, .2);
        std::cout << "PALM rosewood E2 free_db_s=" << free << " muffled_db_s=" << muffled << " ratio=" << muffled / free << '\n';
        check(muffled / free > 3.5 && muffled / free < 6.5, "a muffling hand does not stop a low bar as Burritt's does");
    }

    // 8. A thumb laid on a tine stops it within a few periods (5-15 ms).
    {
        auto p = clean(3);
        const auto ring = strike(p, 64, .8f, .4);
        p.noteOffMode = 1;
        const auto muted = play(p, {on(0, 64, .8f), off(.3, 64)}, .4);
        const double drop = db(energy(muted, .33, .36) / energy(ring, .33, .36));
        std::cout << "THUMB kalimba E4 drop_30ms_db=" << drop << '\n';
        check(drop < -40, "a thumb on the tine does not stop it within 30 ms");
    }

    // 10. Shaft: a hard, light line contact of 0.2-0.6 ms (Hertz, K 5e7-8e8,
    //     27 g); the handle's own modes knock with it.
    {
        maremba::MarembaVoice voice;
        maremba::FastPRNG random(99u);
        voice.Trigger(60, .8f, EMarimbaModel::ImperialRosewood, .5f, .35f, 0, .7f, 1, 0, 0, 0, 96000, random, 0, 0,
                      maremba::kStrikerShaft, 0);
        int contact = 0;
        for (int i = 0; i < 4800; ++i) {
            float c, f, pz;
            voice.ProcessSample(c, f, pz);
            if (voice.GetContactForce() > 0) ++contact;
            if (i % 32 == 31) voice.UpdateEnvelope();
        }
        const double ms = contact / 96.0;
        std::cout << "SHAFT contact_ms=" << ms << '\n';
        check(ms > .15 && ms < .7, "the shaft's contact is not a hard wooden knock");
    }

    // 11. Measured keyboards: the valimba's keys where Tracey measured them;
    //     the gyil's steps and octaves as Godsey averaged them; each hand-tuned
    //     tradition with its own per-key scatter.
    {
        const double valimba[21] = {128, 146, 162, 178, 194, 220, 240, 267, 295, 328, 356, 388, 420, 480, 524, 584,
                                    640, 712, 776, 864, 912};
        const int white[7] = {0, 2, 4, 5, 7, 9, 11};
        double worst = 0;
        for (int k = 0; k < 21; ++k) {
            const int note = 48 + 12 * (k / 7) + white[k % 7];
            const double measured = 1200 * std::log2(valimba[k] / 128.0);
            worst = std::max(worst, std::abs(100.0 * (note - 48) + maremba::TuningOffsetCents(6, note) - measured));
        }
        std::cout << "VALIMBA worst_key_error_cents=" << worst << '\n';
        check(worst < .1, "the valimba keys miss Tracey's measured frequencies");
        const int penta[5] = {0, 2, 4, 7, 9};
        double steps = 0, lowOctave = 1e9, highOctave = 0;
        std::array<double, 14> key{};
        for (int k = 0; k < 14; ++k) {
            const int note = 48 + 12 * (k / 5) + penta[k % 5];
            key[k] = 100.0 * note + maremba::TuningOffsetCents(5, note);
            if (k) steps += key[k] - key[k - 1];
            if (k >= 5) { lowOctave = std::min(lowOctave, key[k] - key[k - 5]); highOctave = std::max(highOctave, key[k] - key[k - 5]); }
        }
        std::cout << "GYIL mean_step=" << steps / 13 << " octaves=[" << lowOctave << ',' << highOctave << "]\n";
        check(std::abs(steps / 13 - 238) < 15 && lowOctave > 1050 && highOctave < 1350,
              "the gyil keyboard strays from Godsey's averaged tuning");
        const std::array<std::pair<int, double>, 3> scatter = {{{1, 6.0}, {3, 25.0}, {5, 30.0}}};
        for (const auto& [scale, sd] : scatter) {
            double sum = 0, square = 0;
            int count = 0;
            for (int note = 24; note <= 108; ++note) {
                const int pc = note % 12;
                const bool degree = scale == 1 ? (pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11)
                                               : (pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9);
                if (!degree) continue;
                // Deviation from the next octave's same degree: the scatter of two keys.
                const double d = maremba::TuningOffsetCents(scale, note) - maremba::TuningOffsetCents(scale, note + 12);
                if (scale == 5 && (note < 48 || note + 12 > 79)) continue;   // inside Godsey's measured range
                sum += d; square += d * d; ++count;
            }
            const double measuredSd = std::sqrt(std::max(0.0, square / count - (sum / count) * (sum / count))) / std::sqrt(2.0);
            std::cout << "SCATTER scale=" << scale << " sd_cents=" << measuredSd << " target=" << sd << '\n';
            check(measuredSd > .5 * sd && measuredSd < 1.6 * sd, "a tradition's per-key scatter is off its measured spread");
        }
    }

    // 12. The kalimba's hole cover follows Benade & Murday's end correction:
    //     a palm 5 mm over the hole lowers the box ~230 cents, at 20 mm ~40.
    {
        double previous = 1;
        for (int i = 0; i <= 20; ++i) {
            const auto cover = maremba::KalimbaHoleCover(i / 20.0);
            check(cover.frequency <= previous + 1e-12, "covering the hole does not lower the box");
            previous = cover.frequency;
        }
        const double half = 1200 * std::log2(maremba::KalimbaHoleCover(.5).frequency);
        const double full = 1200 * std::log2(maremba::KalimbaHoleCover(1).frequency);
        std::cout << "HOLE cover_0.5_cents=" << half << " cover_1_cents=" << full << '\n';
        check(maremba::KalimbaHoleCover(0).frequency == 1 && half < -100 && half > -200 && full < -500,
              "the hole cover does not follow the palm's end correction");
    }

    // 13. Balafon at its playing position (0.43): the first overtone about
    //     14 dB under the fundamental (Godlovitch et al. 2012).
    {
        auto p = clean(2);
        p.strikePosition = .43f;
        p.malletType = 2;
        p.malletHardness = .6f;
        const auto note = strike(p, 60, .8f, .4);
        const double r2 = maremba::GetBarKey(EMarimbaModel::BalafonAncestral, 60).ratio[1];
        const double overtone = line(note, peakHz(note, r2 * hz(60), .03, .3, 40), .03, .3);
        const double level = 20 * std::log10(overtone / line(note, peakHz(note, hz(60), .03, .3, 20), .03, .3));
        std::cout << "BALAFON C4 first_overtone_db=" << level << " ratio=" << r2 << '\n';
        check(level > -22 && level < -8, "the balafon's first overtone is off Godlovitch's level");
    }

    // 14. A balafon's top keys carry no membrane (Voisin 1993).
    {
        auto p = clean(2);
        const auto dry = strike(p, 86, .8f, .2);
        p.buzzAmount = 1;
        check(energy(minus(strike(p, 86, .8f, .2), dry)) == 0, "a top balafon key buzzes");
    }

    // 15. Kalimba tink: a nail at the tip sounds the tine's second mode far
    //     more than flesh inside it (technique, not the tine).
    {
        const auto mount = maremba::GetKalimbaMount(hz(64));
        auto p = clean(3);
        p.malletType = 2;
        p.strikePosition = 1;
        const auto nail = strike(p, 64, .8f, .1);
        p.malletType = 0;
        p.strikePosition = 0;
        const auto flesh = strike(p, 64, .8f, .1);
        const auto tink = [&](const std::vector<double>& x) {
            return 20 * std::log10(line(x, peakHz(x, mount.ratio[1] * hz(64), .002, .05, 60), .002, .05)
                                   / line(x, peakHz(x, hz(64), .002, .05, 20), .002, .05));
        };
        std::cout << "TINK kalimba E4 nail_tip_db=" << tink(nail) << " flesh_inner_db=" << tink(flesh) << '\n';
        check(tink(nail) > tink(flesh) + 8, "a nail at the tip does not tink");
    }

    std::cout << "Round6ResponseTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
