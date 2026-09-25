// Fifth physical-model pass: traditional tunings, note-off damping, mod-wheel
// rolls and the kalimba's hole cover; pitch that stays on the key across the
// resonator controls; buzz that leaves the note's level alone; quiet restrikes,
// the kalimba re-pluck and the bars' and tines' decay against measurements.
// Rendered checks use the engine's dry close output.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static int Hand(const MarembaEngine& e, int note) { return e.mKeys[static_cast<std::size_t>(note)].hand; }
};
struct MarembaVoiceTestAccess {
    static double ModeT60(const MarembaVoice& v, int m) {
        return 6.907755 / (-std::log(v.mModes[m].r) * v.mSampleRate);
    }
};
}

namespace {
using maremba::EMarimbaModel;
using maremba::MarembaEngine;
constexpr double kPi = 3.14159265358979323846;
constexpr double rate = 48000;
double db(double ratio) { return 10 * std::log10(std::max(ratio, 1.e-30)); }
double hz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }
double cents(double a, double b) { return 1200 * std::log2(a / b); }

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

// A performance: actions at times (seconds), an observer after every block.
struct Event {
    double time;
    std::function<void(MarembaEngine&)> action;
};
std::vector<double> play(const maremba::EngineParameters& p, std::vector<Event> events, double seconds,
                         const std::function<void(const MarembaEngine&, double)>& observe = {},
                         double sampleRate = rate) {
    MarembaEngine engine(sampleRate);
    engine.SetParameters(p);
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    std::vector<double> out;
    std::array<float, 64> l{}, r{}, cl{}, cr{};
    const size_t frames = static_cast<size_t>(seconds * sampleRate);
    size_t next = 0;
    for (size_t start = 0; start < frames;) {
        while (next < events.size() && static_cast<size_t>(events[next].time * sampleRate) <= start)
            events[next++].action(engine);
        size_t n = std::min<size_t>(16, frames - start);
        if (next < events.size()) n = std::min(n, static_cast<size_t>(events[next].time * sampleRate) - start);
        engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr, nullptr, static_cast<int>(n));
        for (size_t i = 0; i < n; ++i) out.push_back(cl[i] + cr[i]);
        start += n;
        if (observe) observe(engine, start / sampleRate);
    }
    return out;
}
Event on(double t, int note, float velocity) { return {t, [=](MarembaEngine& e) { e.NoteOn(note, velocity); }}; }
Event off(double t, int note) { return {t, [=](MarembaEngine& e) { e.NoteOff(note); }}; }
Event set(double t, const maremba::EngineParameters& p) { return {t, [=](MarembaEngine& e) { e.SetParameters(p); }}; }
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

// The heard pitch: the strongest line within 60 cents of a guess over a
// window, found on a 1-cent grid and refined by golden section.
double peakHz(const std::vector<double>& x, double guess, double from, double to) {
    double best = guess, bestLevel = -1;
    for (int c = -60; c <= 60; ++c) {
        const double f = guess * std::exp2(c / 1200.0);
        const double level = line(x, f, from, to);
        if (level > bestLevel) { bestLevel = level; best = f; }
    }
    double lo = best * std::exp2(-1 / 1200.0), hi = best * std::exp2(1 / 1200.0);
    constexpr double g = .6180339887498949;
    double c = hi - g * (hi - lo), d = lo + g * (hi - lo);
    double lc = line(x, c, from, to), ld = line(x, d, from, to);
    for (int i = 0; i < 30; ++i) {
        if (lc > ld) { hi = d; d = c; ld = lc; c = hi - g * (hi - lo); lc = line(x, c, from, to); }
        else         { lo = c; c = d; lc = ld; d = lo + g * (hi - lo); ld = line(x, d, from, to); }
    }
    return .5 * (lo + hi);
}
// The window the resonator's pitch compensation targets (MarembaVoice::TuneOnResonator).
double heardPitch(const std::vector<double>& x, double guess) {
    const double length = guess < 130.8 ? 1.0 : .3;
    return peakHz(x, guess, .15, .15 + length);
}

double energy(const std::vector<double>& x, double from = 0, double to = 1e9) {
    double e = 0;
    const size_t a = static_cast<size_t>(from * rate);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return e;
}
double largestStep(const std::vector<double>& x, double from, double to) {
    double step = 0;
    const size_t a = std::max<size_t>(1, static_cast<size_t>(from * rate));
    const size_t b = std::min(x.size(), static_cast<size_t>(to * rate));
    for (size_t i = a; i < b; ++i) step = std::max(step, std::abs(x[i] - x[i - 1]));
    return step;
}
std::vector<double> minus(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> d(std::min(a.size(), b.size()));
    for (size_t i = 0; i < d.size(); ++i) d[i] = a[i] - b[i];
    return d;
}
// Crude one-pole high-pass (fc ~ 800 Hz): buzz lives above the note.
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

// Strokes of a held, rolling key: the first one, plus each roll stroke (the
// hands alternate on every stroke).
struct StrokeCounter {
    int note, strokes = 0, lastHand = -1;
    double lastTime = -1;
    void operator()(const MarembaEngine& e, double t) {
        const int hand = maremba::MarembaEngineTestAccess::Hand(e, note);
        if (lastHand >= 0 && hand != lastHand) { ++strokes; lastTime = t; }
        lastHand = hand;
    }
};
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& message) {
        if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    };
    std::cout << std::setprecision(5);
    const char* names[] = {"rosewood", "padauk", "balafon", "kalimba"};
    const char* scales[] = {"equal", "bala", "timbila", "pentatonic", "sambla"};

    // 1. Traditional tunings: monotonic over the keyboard and the measured
    //    scale degrees on the keys a player reaches for. Equal and the
    //    Timbila (tuned by octave copying) repeat exactly at the octave; the
    //    other scales' hand-tuned keys each keep their own deviation (see
    //    Round6ResponseTest).
    for (int s = 0; s < 5; ++s) {
        const double* table = maremba::TuningPitchClassCents(s);
        for (int pc = 0; pc < 12; ++pc)
            check(table[pc] >= 0 && table[pc] < 1200 && (pc == 0 || table[pc] > table[pc - 1]),
                  std::string("pitch classes out of order: ") + scales[s]);
        for (int note = 1; note < 128; ++note) {
            const double offset = maremba::TuningOffsetCents(s, note);
            check(100.0 * note + offset > 100.0 * (note - 1) + maremba::TuningOffsetCents(s, note - 1)
                  && std::abs(offset) <= 150,
                  std::string("tuning does not rise key by key: ") + scales[s]);
            if (s == 0 || s == 2)
                check(std::abs(offset - maremba::TuningOffsetCents(s, 60 + note % 12)) < 1e-9,
                      std::string("tuning does not repeat at the octave: ") + scales[s]);
        }
    }
    for (int note = 0; note < 128; ++note)
        check(maremba::TuningOffsetCents(0, note) == 0, "Equal is not equal temperament");
    const int white[7] = {0, 2, 4, 5, 7, 9, 11};
    const double tracey[8] = {252, 276, 304, 336, 368, 408, 456, 504};
    for (int k = 0; k < 7; ++k) {
        check(std::abs(maremba::TuningPitchClassCents(1)[white[k]] - k * 1200.0 / 7) < .1,
              "Bala white keys are not 7-equal");
        check(std::abs(maremba::TuningPitchClassCents(2)[white[k]] - 1200 * std::log2(tracey[k] / tracey[0])) < .5,
              "Timbila white keys miss Tracey's measured frequencies");
    }
    const int penta[5] = {0, 2, 4, 7, 9};
    for (int k = 0; k < 5; ++k)
        check(std::abs(maremba::TuningPitchClassCents(3)[penta[k]] - 240.0 * k) < 1e-9,
              "pentatonic C D E G A are not 5-equal");
    const double* sambla = maremba::TuningPitchClassCents(4);
    check(sambla[3] < 300 && sambla[3] > 280 && sambla[4] - sambla[3] > 100 && sambla[4] - sambla[3] < 120
          && sambla[0] == 0 && sambla[7] == 700 && sambla[9] == 900,
          "Sambla degrees 1, flat 3, 3, 5, 6 misplaced");

    // 2. A key of a traditional tuning sounds exactly like the equal-tempered
    //    key retuned by its offset (strike, bar geometry, resonator and all).
    for (int model = 0; model < 4; ++model) for (int s = 1; s < 5; ++s) for (int note : {63, 65}) {
        auto p = clean(model);
        p.tuningScale = s;
        const auto scaled = strike(p, note, .8f, .4);
        p.tuningScale = 0;
        p.tuneCents = static_cast<float>(maremba::TuningOffsetCents(s, note));
        const auto shifted = strike(p, note, .8f, .4);
        check(energy(minus(scaled, shifted)) <= 1e-12 * energy(shifted),
              std::string("a tuning offset is not a plain retune: ") + names[model] + ' ' + scales[s]);
    }
    //    ...and it is heard: the interval C4 -> E4 on every instrument.
    double worstInterval = 0;
    for (int model = 0; model < 4; ++model) for (int s : {1, 3}) {
        auto p = clean(model);
        p.tuningScale = s;
        const double lowTarget = hz(60) * std::exp2(maremba::TuningOffsetCents(s, 60) / 1200),
                     highTarget = hz(64) * std::exp2(maremba::TuningOffsetCents(s, 64) / 1200);
        const double low = heardPitch(strike(p, 60, .8f, .5), lowTarget);
        const double high = heardPitch(strike(p, 64, .8f, .5), highTarget);
        const double error = cents(high / low, highTarget / lowTarget);
        worstInterval = std::max(worstInterval, std::abs(error));
        std::cout << "SCALE model=" << names[model] << " scale=" << scales[s]
                  << " C4_E4_interval_error_cents=" << error << '\n';
    }
    check(worstInterval < 3, "a traditional interval is not heard as tuned");

    // 3. The pitch stays on the key across the resonator controls: the tube,
    //    box or gourd pulls the bar's line, and the key is tuned against it.
    //    A tube Tuned 50 cents sharp of an already sharp tube rings on at its
    //    own pitch (Q 60-90 in the bass) and is heard as such where it
    //    outlasts a weakly coupled bar; the key stays within 25 cents there.
    double worstPitch = 0, worstDetuned = 0;
    for (int model = 0; model < 3; ++model) for (int note : {36, 43, 48, 50, 60, 72, 84})
        for (float coupling : {0.f, .35f, .7f, 1.f}) for (float tune : {-50.f, 0.f, 50.f}) {
            auto p = clean(model);
            p.resonatorCoupling = coupling;
            p.resonatorTune = tune;
            const double error = cents(heardPitch(strike(p, note, .8f, note < 48 ? 1.2 : .5), hz(note)), hz(note));
            double& worst = tune == 0 ? worstPitch : worstDetuned;
            if (std::abs(error) > worst)
                std::cout << "  pitch model=" << names[model] << " note=" << note << " coupling=" << coupling
                          << " tune=" << tune << " error_cents=" << error << '\n';
            worst = std::max(worst, std::abs(error));
        }
    std::cout << "PITCH worst_error_cents=" << worstPitch << " detuned_resonator=" << worstDetuned << '\n';
    check(worstPitch < 5, "the resonator controls pull the key off pitch");
    check(worstDetuned < 25, "a detuned resonator pulls the key off pitch");

    // 4. Note-off. Ring: the key rings on, bit for bit. Damp: the hand stops
    //    it (>= 30 dB within 0.25 s) without a click.
    for (int model = 0; model < 4; ++model) {
        auto p = clean(model);
        const auto held = play(p, {on(0, 60, .8f)}, .8);
        const auto released = play(p, {on(0, 60, .8f), off(.3, 60)}, .8);
        check(energy(minus(held, released)) == 0, std::string("Ring mode note-off changes the sound: ") + names[model]);
        p.noteOffMode = 1;
        const auto damped = play(p, {on(0, 60, .8f), off(.3, 60)}, .8);
        const double drop = db(energy(damped, .5, .6) / energy(held, .5, .6));
        const double click = largestStep(damped, .3, .32) / largestStep(held, .28, .3);
        std::cout << "DAMP model=" << names[model] << " drop_db_at_0.2s=" << drop << " step_ratio=" << click << '\n';
        check(drop < -30, std::string("Damp mode does not stop the key: ") + names[model]);
        check(click < 1.05, std::string("Damp mode clicks: ") + names[model]);
    }

    // 5. Mod-wheel rolls on the mallet instruments.
    for (int model = 0; model < 3; ++model) {
        auto p = clean(model);
        p.modWheel = .5f;   // 6 + 14 x 0.5 = 13 strokes/s
        StrokeCounter roll{60};
        const auto rolled = play(p, {on(0, 60, .7f), off(1.0, 60)}, 1.6, std::ref(roll));
        const int strokes = roll.strokes + 1;
        std::cout << "ROLL model=" << names[model] << " strokes_in_1s=" << strokes
                  << " last_stroke_s=" << roll.lastTime;
        check(strokes >= 12 && strokes <= 15, std::string("roll rate does not follow the wheel: ") + names[model]);
        check(roll.lastTime <= 1.0, std::string("a roll outlives its key: ") + names[model]);
        // A roll sustains the note above the first stroke's own level.
        const auto single = strike(clean(model), 60, .7f, 1.0);
        const double sustain = db(energy(rolled, .5, 1.0) / (energy(single, 0, 1.0 / 13) * 13 * .5));
        std::cout << " roll_vs_first_stroke_db=" << sustain << '\n';
        check(sustain > -1.5, std::string("a roll does not sustain the note: ") + names[model]);
    }
    {
        auto p = clean(0);
        // Rate law: below the wheel's threshold nothing rolls; full wheel is 20/s.
        for (float wheel : {.01f, 1.f}) {
            p.modWheel = wheel;
            StrokeCounter roll{60};
            play(p, {on(0, 60, .7f)}, 1.0, std::ref(roll));
            const int strokes = roll.strokes + 1;
            check(wheel < .02f ? strokes == 1 : strokes >= 18 && strokes <= 21, "roll rate law broken");
        }
        // Wheel down ends the roll and the key rings on; wheel up again rolls
        // again, even from silence.
        auto down = p, up = p;
        down.modWheel = 0;
        up.modWheel = .5f;
        StrokeCounter roll{60};
        bool silentBeforeWheel = false;
        const auto phrase = play(up, {on(0, 60, .7f), set(1, down), set(6, up)}, 7,
                                 [&](const MarembaEngine& e, double t) {
                                     roll(e, t);
                                     if (t > 5.5 && t < 6) silentBeforeWheel |= e.IsSilent();
                                 });
        check(roll.lastTime > 6 && energy(phrase, 6.2, 7) > 0, "the wheel does not restart a roll on a held key");
        check(energy(phrase, 1.05, 1.5) > 0, "wheel down cut the ringing note");
        // A held key with the wheel down lets the engine go idle.
        check(silentBeforeWheel, "a held, silent key keeps the engine busy");
        // Transport stop ends the roll and damps it.
        StrokeCounter stopped{60};
        const auto stop = play(up, {on(0, 60, .7f), {1.0, [](MarembaEngine& e) { e.DampAll(); }}}, 1.6,
                               std::ref(stopped));
        check(stopped.lastTime <= 1.0 && db(energy(stop, 1.4, 1.6) / energy(stop, .8, 1.0)) < -30,
              "transport stop does not end a roll");
        // A rolled chord re-strikes its own bars: no extra voices.
        int most = 0;
        up.modWheel = 1;
        play(up, {on(0, 48, .7f), on(0, 55, .7f), on(0, 60, .7f), on(0, 64, .7f)}, 1.0,
             [&](const MarembaEngine& e, double) { most = std::max(most, e.GetActiveVoiceCount()); });
        check(most == 4, "a rolled chord takes more voices than its keys");
    }

    // 6. Kalimba: the wheel is the hand over the sound holes, never a roll.
    //    The open box's air resonance (~400 Hz) makes its notes the loudest;
    //    covering the holes lowers it: those notes drop, the low ones near
    //    the lowered resonance rise. Level and colour change, the pitch not.
    {
        auto p = clean(3);
        p.modWheel = 1;
        StrokeCounter roll{60};
        play(p, {on(0, 60, .8f)}, 1.0, std::ref(roll));
        check(roll.strokes == 0, "the kalimba rolls");
        auto open = clean(3), covered = clean(3);
        open.bodyBloom = covered.bodyBloom = .5f;
        covered.modWheel = 1;
        int loudest = 0;
        double loudestLevel = -1e9, resonanceChange = 0, lowChange = 0;
        for (int note : {60, 64, 67, 69, 72, 76}) {
            const auto a = strike(open, note, .8f, .6), b = strike(covered, note, .8f, .6);
            const double shift = cents(heardPitch(b, hz(note)), heardPitch(a, hz(note)));
            const double level = db(energy(a, .02, .6)), change = db(energy(b, .02, .6)) - level;
            if (level > loudestLevel) { loudestLevel = level; loudest = note; }
            if (note == 67) resonanceChange = change;
            if (note == 60) lowChange = change;
            std::cout << "COVER note=" << note << " open_db=" << level << " covered_change_db=" << change
                      << " pitch_shift_cents=" << shift << '\n';
            check(std::abs(shift) < .5, "covering the holes moves the pitch");
        }
        check(loudest == 67 || loudest == 69, "the open box's air resonance does not favour its notes");
        check(resonanceChange < -3, "covering the holes does not take the resonance from its notes");
        check(lowChange > 2, "the lowered resonance does not support the low notes");
        // Moving the hand over a ringing note does not click.
        const auto moved = play(open, {on(0, 67, .8f), set(.3, covered)}, .5);
        check(largestStep(moved, .3, .35) < 1.05 * largestStep(moved, .25, .3), "moving the hand over the holes clicks");
    }

    // 7. Buzz is an added layer: up to the factory settings (0.75) a
    //    mezzo-forte note's fundamental keeps its level within 1.5 dB. Loose
    //    membranes clamp the resonator near their threshold on loud strokes,
    //    turning its excess into buzz: at most 3.5 dB at full Buzz.
    double worstFactory = 0, worstLoose = 0;
    for (int model : {0, 1, 2}) for (int note : {48, 60, 72}) for (float velocity : {.5f, .7f, 1.f}) {
        auto p = clean(model);
        p.malletType = 2;
        const auto dry = strike(p, note, velocity, .4);
        for (float buzz : {.35f, .75f, 1.f}) {
            p.buzzAmount = buzz;
            const auto wet = strike(p, note, velocity, .4);
            const double change = std::abs(2 * db(line(wet, hz(note), .05, .4) / line(dry, hz(note), .05, .4)));
            (buzz <= .75f && velocity <= .7f ? worstFactory : worstLoose)
                = std::max(buzz <= .75f && velocity <= .7f ? worstFactory : worstLoose, change);
        }
    }
    std::cout << "BUZZ worst_fundamental_change_db factory_mf=" << worstFactory << " loose_or_forte=" << worstLoose << '\n';
    check(worstFactory < 1.5, "the buzzing membrane re-levels a mezzo-forte note");
    check(worstLoose < 3.5, "a loose membrane swallows the note");
    //    The charleo is heard at mezzo-forte with the Guatemalan patches' Buzz
    //    (0.50 and 1.00 since the knob spans 70 % of the former physical
    //    range: 0.35 and 0.70 of it).
    for (float buzz : {.50f, 1.f}) {
        auto p = clean(1);
        p.malletType = 2;
        p.malletHardness = .5f;
        const auto dry = strike(p, 60, .6f, .4);
        p.buzzAmount = buzz;
        const double layer = db(energy(highPass(minus(strike(p, 60, .6f, .4), dry))) / energy(dry));
        std::cout << "CHARLEO buzz=" << buzz << " mf_layer_db=" << layer << '\n';
        check(layer > -40, "the charleo is inaudible at mezzo-forte");
    }

    // 8. Re-striking a ringing key: what is left of the old ring is the
    //    restruck sound minus the same stroke on a resting key. A light mallet
    //    (20-30 g against a bar of hundreds) barely takes energy from the bar:
    //    <= 3 dB. The thumb stops a tine before plucking it again: the old
    //    ring loses >= 25 dB.
    for (int note : {48, 60}) {
        const auto p = clean(0);
        const auto ringing = play(p, {on(0, note, .8f)}, .8);
        const auto restruck = play(p, {on(0, note, .8f), on(.3, note, 8 / 127.f)}, .8);
        const auto quiet = play(p, {on(.3, note, 8 / 127.f)}, .8);
        const auto left = minus(restruck, quiet);
        const double loss = 2 * db(line(left, hz(note), .35, .75) / line(ringing, hz(note), .35, .75));
        std::cout << "RESTRIKE rosewood note=" << note << " quiet_restrike_ring_change_db=" << loss << '\n';
        check(loss > -3, "a quiet restrike takes too much of the ringing bar");
    }
    {
        const auto p = clean(3);
        const auto ringing = play(p, {on(0, 60, .8f)}, 1.6);
        const auto replucked = play(p, {on(0, 60, .8f), on(1.0, 60, .8f)}, 1.6);
        const auto fresh = play(p, {on(1.0, 60, .8f)}, 1.6);
        const double residual = db(energy(minus(replucked, fresh), 1.05, 1.6) / energy(ringing, 1.05, 1.6));
        std::cout << "REPLUCK kalimba old_ring_left_db=" << residual << '\n';
        check(residual < -25, "a re-plucked tine keeps its old ring");
    }

    // 9. Decay against measurements. Rosewood bare-bar fundamentals (Hill
    //    2011) and the kalimba's mid-register ring (Holdaway).
    const std::array<std::pair<int, double>, 6> hill = {{{36, 6.7}, {48, 4.7}, {60, 2.2}, {72, 1.1}, {84, .54}, {96, .18}}};
    for (const auto& [note, t60] : hill) {
        maremba::MarembaVoice voice;
        maremba::FastPRNG random(7);
        voice.Trigger(note, .8f, EMarimbaModel::ImperialRosewood, .5f, .35f, 0, .7f, 1, 0, 0, 0, 96000, random,
                      0, 0, 1, 0, 0);
        const double measured = maremba::MarembaVoiceTestAccess::ModeT60(voice, 0);
        std::cout << "HILL note=" << note << " t60_s=" << measured << " measured_s=" << t60 << '\n';
        check(std::abs(measured / t60 - 1) < .25, "rosewood bar decay misses Hill's measurement");
    }
    //    Kalimba: 4-5 s on the low tines, 2.5-4.5 s in the middle register (a
    //    note on the box resonance dies a little sooner), a few tenths of a
    //    second on the top tines.
    const auto tineT60 = [](int note) {
        maremba::MarembaVoice voice;
        maremba::FastPRNG random(7);
        voice.Trigger(note, .8f, EMarimbaModel::KalimbaArtisan, .5f, .35f, 0, .7f, 1, 0, 0, 0, 96000, random,
                      0, 0, 1, 0, 0);
        return maremba::MarembaVoiceTestAccess::ModeT60(voice, 0);
    };
    std::vector<double> middle;
    for (int note : {64, 65, 67, 69, 71}) middle.push_back(tineT60(note));
    std::sort(middle.begin(), middle.end());
    const double low = tineT60(60), top = tineT60(88);
    std::cout << "HOLDAWAY C4_t60_s=" << low << " E4-B4_median_t60_s=" << middle[2]
              << " E4-B4_range_s=[" << middle.front() << ',' << middle.back() << "] E6_t60_s=" << top << '\n';
    check(low > 4 && low < 5.5, "kalimba low tine ring outside Holdaway's range");
    check(middle[2] > 2.5 && middle[2] < 4.5 && middle.front() > 2 && middle.back() < 4.8,
          "kalimba mid-register ring outside Holdaway's range");
    check(top > .2 && top < .9, "kalimba top tine does not ring a few tenths of a second");

    std::cout << "Round5ResponseTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
