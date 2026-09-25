// The mallet head: a mass on a nonlinear felt/rubber/wood spring meeting the
// bar through its driving-point response (MalletContact). Contracts: rigid-wall
// contact times and their velocity law, passivity against a ringing mode,
// finite restitution, rate invariance, and the voice's use of it (a restrike
// takes energy from a ringing bar where the bar moves, not at its node).
#include "MalletContact.h"
#include "MarembaVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static bool HeadActive(const MarembaVoice& voice) { return voice.mContact.Active(); }
};
}
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
constexpr double pi = 3.14159265358979323846;

maremba::MalletContact::Setup setupFor(int type, double velocity, double rate, double compliance = 0.0) {
    const auto spec = maremba::GetMalletSpec(type);
    maremba::MalletContact::Setup s;
    s.mass = spec.massKg;
    s.stiffness = spec.stiffness;
    s.exponent = spec.exponent;
    s.coreDepth = spec.coreDepthM;
    s.coreStiffness = spec.coreStiffness;
    s.coreExponent = spec.coreExponent;
    s.hysteresis = 0.0;
    s.velocity = velocity;
    s.compliance = compliance;
    s.dt = 1.0 / rate;
    return s;
}

struct Wall { double contactSeconds, impulse, rebound; };
// A rigid, immovable surface: the head's own dynamics only.
Wall rigidWall(int type, double velocity, double rate, double hysteresis = 0.0) {
    auto s = setupFor(type, velocity, rate);
    s.hysteresis = hysteresis;
    maremba::MalletContact contact;
    contact.Start(s, 0.0);
    Wall w{0.0, 0.0, 0.0};
    for (int n = 0; n < static_cast<int>(.03 * rate) && contact.Active(); ++n) {
        const double f = contact.Step(0.0);
        require(std::isfinite(f) && f >= 0.0, "contact force must be finite and push only");
        if (f > 0.0) w.contactSeconds += 1.0 / rate;
        w.impulse += f / rate;
        if (f == 0.0 && w.impulse > 0.0 && w.rebound == 0.0) w.rebound = -contact.Velocity();
    }
    return w;
}
}

int main() {
    // 1. Rigid-wall contact times at 1 m/s (MarimbaModel.h: 2.4 / 1.3 / 0.6 /
    //    0.18 ms for yarn, cord, rubber, wood), elastic impulse 2 m v.
    constexpr double expected[4] = {2.4e-3, 1.3e-3, 0.6e-3, 0.18e-3};
    for (int type = 0; type < 4; ++type) {
        const Wall w = rigidWall(type, 1.0, 768000.0);
        const double mass = maremba::GetMalletSpec(type).massKg;
        std::cout << "WALL type=" << type << " contact_ms=" << w.contactSeconds * 1e3
                  << " impulse_re_2mv=" << w.impulse / (2 * mass) << '\n';
        require(std::abs(w.contactSeconds / expected[type] - 1.0) < .2, "rigid-wall contact time off its specification");
        require(std::abs(w.impulse / (2 * mass) - 1.0) < .02, "a lossless head must rebound elastically");

        // Hertz: T ~ v^-(p-1)/(p+1) on the soft layer (before any core).
        const double p = maremba::GetMalletSpec(type).exponent;
        const double slow = rigidWall(type, .25, 768000.0).contactSeconds;
        const double fast = rigidWall(type, .5, 768000.0).contactSeconds;
        const double law = std::pow(2.0, (p - 1) / (p + 1));
        require(std::abs(slow / fast / law - 1.0) < .08, "contact time does not follow the Hertz velocity law");

        // Felt/rubber loss: less comes back than went in, never more.
        const Wall lossy = rigidWall(type, 1.0, 768000.0, maremba::GetMalletSpec(type).hysteresis);
        require(lossy.rebound > 0.0 && lossy.rebound < 1.0 && lossy.impulse < w.impulse,
                "hysteresis must make the head rebound slower than it arrived");

        // Rate invariance of what the head delivers.
        for (double rate : {88200.0, 192000.0}) {
            const Wall r = rigidWall(type, 1.0, rate);
            require(std::abs(r.impulse / w.impulse - 1.0) < .01, "contact impulse changes with the sample rate");
        }
    }

    // 2. Passivity against one ringing bar mode: head energy plus mode energy
    //    after the contact never exceeds what was there before it.
    for (int type = 0; type < 4; ++type) for (double phase : {0.0, .7, 1.9, 3.3}) {
        const double rate = 192000.0, f = 262.0, mass = .08;
        const double w = 2 * pi * f / rate, r = 1.0, c = 2 * r * std::cos(w), sq = r * r;
        const double amplitude = 4.0e-4;   // metres at the contact
        double y1 = amplitude * std::cos(phase), y2 = amplitude * std::cos(phase - w);
        const double gain = std::sin(w) / (2 * pi * f) / mass / rate;   // displacement per newton-sample
        auto s = setupFor(type, .5, rate, gain);
        s.hysteresis = maremba::GetMalletSpec(type).hysteresis;
        maremba::MalletContact contact;
        contact.Start(s, y1);
        const auto modeEnergy = [&](double a, double b) {
            const double q = a * a - c * a * b + sq * b * b;
            return .5 * mass * (2 * pi * f) * (2 * pi * f) * q / (std::sin(w) * std::sin(w));
        };
        const double before = modeEnergy(y1, y2) + .5 * s.mass * s.velocity * s.velocity;
        // Until the head first leaves: afterwards the wrist lifts it away,
        // which is the player's work, not the contact's.
        double worst = 0.0;
        bool touched = false;
        for (int n = 0; n < static_cast<int>(.03 * rate) && contact.Active(); ++n) {
            const double predicted = c * y1 - sq * y2;
            const double force = contact.Step(predicted);
            const double y = predicted + gain * force;
            y2 = y1; y1 = y;
            touched = touched || contact.InContact();
            if (touched && !contact.InContact()) break;
            worst = std::max(worst, modeEnergy(y1, y2) + .5 * s.mass * contact.Velocity() * contact.Velocity());
        }
        std::cout << "PASSIVITY type=" << type << " phase=" << phase << " worst_energy_ratio=" << worst / before << '\n';
        require(worst <= before * 1.02, "the contact created energy");
    }

    // 3. The voice: a head that meets a resting bar hits it; one that meets a
    //    ringing bar swinging away still catches it; the head never lingers.
    for (int model = 0; model < 3; ++model) {
        maremba::MarembaVoice voice;
        maremba::FastPRNG random(123u);
        voice.Trigger(48, 1.0f, static_cast<maremba::EMarimbaModel>(model), .4f, .5f, 0, .7f, 4, 0, 0, 0,
                      96000, random, 0, 0, 1, 0);
        for (int n = 0; n < 96000 / 20; ++n) { float a, b, p; voice.ProcessSample(a, b, p); }
        require(!maremba::MarembaVoiceTestAccess::HeadActive(voice), "mallet state survived separation");
        // Soft restrike at the antinode of a strongly ringing bar.
        voice.Trigger(48, 8 / 127.0f, static_cast<maremba::EMarimbaModel>(model), .4f, .5f, 0, .7f, 4, 0, 0, 0,
                      96000, random, 0, 0, 1, 0);
        double peak = 0.0;
        for (int n = 0; n < 96000 / 50; ++n) {
            float a, b, p;
            voice.ProcessSample(a, b, p);
            peak = std::max(peak, voice.GetContactForce());
        }
        require(peak > 0.0, "a soft restrike never met the ringing bar");
        voice.FastKill();
        require(!maremba::MarembaVoiceTestAccess::HeadActive(voice), "mallet state survived voice reset");
    }
    std::cout << "Mallet contact contracts passed.\n";
}
