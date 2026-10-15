// A freely ringing tine must not acquire an empirical even harmonic before
// the shared board sensor. Opposite modal motions isolate generated even
// content without confusing the tine's inharmonic overtones with distortion.
// The actual board and charge amplifier remain in the measured signal path.
#include "MarembaVoice.h"
#include "KalimbaBody.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static double IsolateFundamental(MarembaVoice& v, double polarity) {
        // Keep the configured pole and physically held fundamental amplitude;
        // only remove excitation, other modes and the static preload transient.
        v.mThumbPending = false;
        v.mReleaseSample = v.mReleaseSamples;
        v.mPreloadOffset = 0;
        for (int m = 0; m < v.mNumModes; ++m) {
            auto& mode = v.mModes[m];
            mode.y1 = m == 0 ? polarity * v.mThumbHold[0] : 0;
            mode.y2 = mode.y1 * mode.cosW / mode.r;
            mode.idle = m != 0;
        }
        return v.mF0;
    }
    static double Preload(const MarembaVoice& v) { return v.mPreloadOffset; }
    static bool ThumbPending(const MarembaVoice& v) { return v.mThumbPending; }
    static double TouchReactionChange(const MarembaVoice& v) {
        double before = 0, after = 0;
        for (int m = 0; m < v.mNumModes; ++m) {
            before += v.mModes[m].y1 * v.mBridgeGain[m];
            after += (v.mThumbResidual * v.mModes[m].y1 + v.mThumbHold[m]) * v.mBridgeGain[m];
        }
        return after - before;
    }
};
}

namespace {
using namespace maremba;
using Access = MarembaVoiceTestAccess;
using Complex = std::complex<double>;
int checks = 0, spectra = 0, plucks = 0;
double worstH2 = 0, worstEven = 0, worstPreload = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

void strike(MarembaVoice& v, double rate, int note, int velocity, int technique = 1,
            bool dead = false) {
    FastPRNG random(0x61ca11u);
    StrikeOptions options; options.deadStroke = dead;
    v.Trigger(note, velocity / 127.f, EMarimbaModel::KalimbaArtisan, .4f, .5f,
              0, .7f, 1, 0, 0, 0, rate, random, 0, 0, technique, 0, 0, options);
}

double advance(MarembaVoice& v) {
    float close = 0, room = 0, piezo = 0;
    v.ProcessSample(close, room, piezo);
    require(std::isfinite(close) && std::isfinite(room) && std::isfinite(piezo),
            "physical tine produced nonfinite output");
    return v.GetBridgeForce();
}

struct Sensor {
    KalimbaBody body;
    PiezoPickup pickup;
    explicit Sensor(double rate) {
        body.Configure(rate);
        pickup.Configure(rate, EMarimbaModel::KalimbaArtisan);
    }
    double process(double force) {
        KalimbaBoxDrive drive; drive.board = force;
        double close = 0, far = 0, plate = 0, side = 0, strain = 0;
        body.Process(drive, 1, .4, close, far, plate, side, nullptr, &strain);
        return pickup.ProcessSurfaceStrain(strain);
    }
};

struct Window {
    Complex fundamental{}, second{};
    double oddEnergy = 0, evenEnergy = 0;
    void add(double time, double begin, double end, double hz, double odd, double even) {
        if (time < begin || time >= end) return;
        const double weight = .5 - .5 * std::cos(kTwoPiD * (time - begin) / (end - begin));
        fundamental += weight * odd * std::polar(1., -kTwoPiD * hz * time);
        second += weight * even * std::polar(1., -2 * kTwoPiD * hz * time);
        oddEnergy += weight * odd * odd;
        evenEnergy += weight * even * even;
    }
};

void spectrum(double rate, int note, int velocity, bool overlap = false) {
    std::array<MarembaVoice, 2> positive, negative;
    const int count = overlap ? 2 : 1;
    double hz = 0;
    for (int i = 0; i < count; ++i) {
        strike(positive[i], rate, note + 4 * i, velocity);
        strike(negative[i], rate, note + 4 * i, velocity);
        const double frequency = Access::IsolateFundamental(positive[i], 1);
        Access::IsolateFundamental(negative[i], -1);
        if (i == 0) hz = frequency;
    }
    Sensor plus(rate), minus(rate);
    std::array<Window, 2> windows{};
    for (int sample = 0; sample < int(.24 * rate); ++sample) {
        double a = 0, b = 0;
        for (int i = 0; i < count; ++i) { a += advance(positive[i]); b += advance(negative[i]); }
        const double p = plus.process(a), n = minus.process(b);
        const double odd = .5 * (p - n), even = .5 * (p + n), time = sample / rate;
        windows[0].add(time, .04, .12, hz, odd, even);
        windows[1].add(time, .16, .24, hz, odd, even);
    }
    for (const auto& window : windows) {
        require(window.oddEnergy > 1.e-10 && std::abs(window.fundamental) > 1.e-5,
                "board-spectrum fixture is silent");
        const double h2 = std::abs(window.second / window.fundamental);
        const double even = std::sqrt(window.evenEnergy / window.oddEnergy);
        worstH2 = std::max(worstH2, h2);
        worstEven = std::max(worstEven, even);
        if (h2 >= 1.e-5 || even >= 1.e-5)
            std::cerr << "SPECTRUM rate=" << rate << " note=" << note << " velocity=" << velocity
                      << " overlap=" << overlap << " generated_h2_db=" << 20 * std::log10(h2)
                      << " even_rms_db=" << 20 * std::log10(even) << '\n';
        require(h2 < 1.e-5, "free tine acquired a second harmonic through the shared board pickup");
        require(even < 1.e-5, "signed bridge motion acquired rectified/even content");
    }
    ++spectra;
}

void preload(double rate, int note, int technique, bool dead) {
    MarembaVoice voice;
    for (int velocity : {32, 80, 127}) {
        strike(voice, rate, note, velocity, technique, dead);
        require(Access::ThumbPending(voice), "preload fixture did not produce a real thumb pluck");
        // A static touch changes the settled load by the signed modal reaction.
        // The one-sample bleed follows that change; retaining the old squared
        // load after removing the output square would reintroduce a click.
        const double expected = (Access::Preload(voice) + Access::TouchReactionChange(voice))
                              * std::exp(-1. / (.04 * rate));
        advance(voice);
        const double error = std::abs(Access::Preload(voice) - expected) / std::max(1., std::abs(expected));
        worstPreload = std::max(worstPreload, error);
        require(error < 1.e-12, "thumb preload does not preserve the signed bridge reaction");
        for (int sample = 1; sample < int(.04 * rate); ++sample) {
            advance(voice);
            if ((sample & 63) == 0) voice.UpdateEnvelope();
        }
        ++plucks;
    }
}
}

int main() {
    std::cout << std::setprecision(12);
    // Every supported host rate and literal quality factor. Rotate velocities
    // across the three notes, then add the complete 3x3 note/velocity fixture
    // at the default 48 kHz/2x. Shared C4+E4 checks overlapping reactions.
    const std::array<int, 3> notes{{60, 64, 72}}, velocities{{32, 80, 127}};
    int caseIndex = 0;
    for (double hostRate : {44100., 48000., 88200., 96000., 176400., 192000.})
        for (int factor : {2, 4, 8}) {
            const double rate = hostRate * factor;
            for (int i = 0; i < 3; ++i) spectrum(rate, notes[i], velocities[(caseIndex + i) % 3]);
            spectrum(rate, 60, 80, true);
            preload(rate, notes[caseIndex % 3], caseIndex % 4, caseIndex % 2 != 0);
            ++caseIndex;
        }
    for (int note : notes) for (int velocity : velocities) spectrum(96000, note, velocity);
    std::cout << "SPECTRA cases=" << spectra << " worst_generated_h2=" << worstH2
              << " worst_even_rms=" << worstEven << '\n';
    std::cout << "PRELOAD plucks=" << plucks << " worst_relative_error=" << worstPreload << '\n';
    std::cout << "PASS KalimbaBridgeSpectrumTest checks=" << checks << '\n';
}
