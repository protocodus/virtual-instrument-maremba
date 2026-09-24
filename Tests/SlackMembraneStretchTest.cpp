// Convex material stretching: independent work balance, passive release,
// rate convergence and legacy-zero regression. No engine/preset compensation.
#include "SlackMembrane.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void Seed(SlackMembrane& m, double x, double v) { m.mX = x; m.mV = v; }
    static double X(const SlackMembrane& m) { return m.mX; }
    static double V(const SlackMembrane& m) { return m.mV; }
};
}

namespace {
constexpr double pi = 3.14159265358979323846;
using maremba::SlackMembrane;
using Access = maremba::MarembaVoiceTestAccess;
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok && failures++ < 12) std::cerr << "FAIL " << message << '\n';
}
double random(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return (double(state >> 8) / 8388608.0) - 1.0;
}
bool same(double x, double y) { return std::memcmp(&x, &y, sizeof(x)) == 0; }

// Independent physical energy, evaluated with extra precision. The test does
// not call the DSP's energy or gradient when checking its conservation law.
long double energy(double x, double v, double outGap, double inGap, double spring, double stretch) {
    const long double out = std::max(static_cast<long double>(x) - outGap, 0.0L);
    const long double in = std::max(-static_cast<long double>(x) - inGap, 0.0L);
    return (static_cast<long double>(v) * v + static_cast<long double>(x) * x) / 2
        + spring * (out * out + in * in) / 2
        + spring * stretch * (out * out * out * out / (outGap * outGap)
                            + in * in * in * in / (inGap * inGap)) / 4;
}

void workAndRelease() {
    double worstBalance = 0, worstGrowth = 0;
    size_t samples = 0;
    for (double rate : {22050., 44100., 48000., 96000., 192000., 768000.})
    for (double stretch : {.12, .30, 1.5})
    for (double amplitude : {.01, 1., 4., 50.}) {
        constexpr double outGap = .075, inGap = .125, spring = 8, slapLoss = 20, strainLoss = .15;
        const double step = 2 * pi * (1150.0 / 3) / rate, baseLoss = step / 5;
        SlackMembrane m;
        m.Configure(rate, 1150.0 / 3, 2.5);
        m.SetMaterial(stretch, strainLoss);
        m.SetLimits(outGap, inGap, spring, slapLoss);
        Access::Seed(m, outGap * amplitude, -.1 * amplitude);
        uint32_t state = 0x81249812;
        for (int n = 0; n < 12000; ++n) {
            const double force = n < 2000 ? 12 * amplitude * random(state) : 0;
            const double x0 = Access::X(m), v0 = Access::V(m);
            const long double before = energy(x0, v0, outGap, inGap, spring, stretch);
            const double out = std::max(x0 - outGap, 0.0), in = std::max(-x0 - inGap, 0.0);
            const double extraLoss = std::min(11.0, slapLoss * (out + in) / std::min(outGap, inGap)
                + strainLoss * (out * out / (outGap * outGap) + in * in / (inGap * inGap)));
            const long double loss = baseLoss * (1 + extraLoss);
            const double dissipated = m.Step(force, 1);
            const double x1 = Access::X(m), v1 = Access::V(m);
            const long double after = energy(x1, v1, outGap, inGap, spring, stretch);
            const long double work = force * static_cast<long double>(x1 - x0);
            const long double dissipation = loss * (v1 + v0) * (v1 + v0) / 2;
            const double error = static_cast<double>(std::abs(after - before - work + dissipation)
                / std::max(1.e-24L, before + after + std::abs(work)));
            worstBalance = std::max(worstBalance, error);
            check(m.IsFinite() && std::isfinite(dissipated) && dissipated >= 0, "non-finite state/negative loss");
            check(error < 2.e-10, "pressure work minus damping does not equal energy change");
            if (force == 0 && before > 1.e-24L) {
                worstGrowth = std::max(worstGrowth, static_cast<double>((after - before) / before));
                check(after <= before * (1 + 2.e-11), "unforced material injected energy");
            }
            ++samples;
        }
        // Convex strain energy must also shrink under the existing hand pole.
        Access::Seed(m, outGap * 20, 4);
        double previous = m.Energy();
        for (int n = 0; n < 2048; ++n) {
            m.Step(0, .97);
            check(m.Energy() <= previous * (1 + 2.e-11), "hand-damped membrane injected energy");
            previous = m.Energy();
        }
        m.FlushTiny();
        check(m.HasNoSubnormalState(), "released material left subnormal state");
    }
    std::cout << "WORK samples=" << samples << " worst_relative_balance=" << worstBalance
              << " worst_relative_passive_growth=" << worstGrowth << '\n';
}

std::vector<double> driven(double rate) {
    SlackMembrane m;
    m.Configure(rate, 1150.0 / 3, 2.5);
    m.SetMaterial(.3, .15);
    m.SetLimits(.075, .125, 8, 20);
    std::vector<double> out;
    for (int n = 0; n < static_cast<int>(rate * .1); ++n) {
        const double t = (n + .5) / rate;
        const double force = 8 * std::sin(2 * pi * 173 * t) * std::sin(pi * t / .1);
        m.Step(force, 1);
        if ((n + 1) % static_cast<int>(rate / 44100) == 0) out.push_back(m.Displacement());
    }
    return out;
}

void convergence() {
    const auto reference = driven(705600);
    double previous = std::numeric_limits<double>::infinity();
    for (double rate : {44100., 88200., 176400.}) {
        const auto result = driven(rate);
        long double error = 0, level = 0;
        check(result.size() == reference.size(), "rate comparison extent mismatch");
        for (size_t i = 0; i < std::min(result.size(), reference.size()); ++i) {
            error += (result[i] - reference[i]) * (result[i] - reference[i]);
            level += reference[i] * reference[i];
        }
        const double relative = std::sqrt(static_cast<double>(error / level));
        check(relative < previous * .8, "material update does not converge as dt shrinks");
        previous = relative;
        std::cout << "RATE hz=" << rate << " relative_rms_error=" << relative << '\n';
    }
}

double freeFrequency(double stretch, double amplitude) {
    constexpr double rate = 192000, gap = .1;
    SlackMembrane m;
    m.Configure(rate, 1150.0 / 3, 1.e10);
    m.SetLimits(gap, gap, 8, 0);
    m.SetMaterial(stretch, 0);
    Access::Seed(m, amplitude, 0);
    double previousX = amplitude, first = 0, last = 0;
    int crossings = 0;
    for (int n = 0; n < 19200 && crossings < 21; ++n) {
        m.Step(0, 1);
        const double x = m.Displacement();
        if (previousX < 0 && x >= 0) {
            const double time = (n + (-previousX / (x - previousX))) / rate;
            if (crossings == 0) first = time;
            last = time;
            ++crossings;
        }
        previousX = x;
    }
    check(crossings == 21, "no oscillation in material frequency probe");
    return (crossings - 1) / (last - first);
}

void hardening() {
    const double quietLegacy = freeFrequency(0, .05), quietMaterial = freeFrequency(.3, .05);
    const double loudLegacy = freeFrequency(0, .4), loudMaterial = freeFrequency(.3, .4);
    check(quietLegacy == quietMaterial, "material changed the below-slack response");
    check(loudMaterial > loudLegacy * 1.15, "larger stretch did not harden membrane resonance");
    std::cout << "FORMANT quiet_hz=" << quietMaterial << " loud_legacy_hz=" << loudLegacy
              << " loud_stretch_hz=" << loudMaterial << '\n';
}
} // namespace

// Frozen pre-round5 implementation, used only for the exact-zero regression.
namespace maremba {

// A slack buzzing membrane (balafon mirliton, Mesoamerican charleo) over a
// hole in a resonator wall. Inside its slack range it is a light, lossy
// oscillator with weak tension. Pressure that drives it past either limit
// pulls it taut: a much stiffer one-sided spring snaps it back. These snaps,
// not a smooth distortion of the note, make the buzz: short accelerations
// ringing at the taut membrane's resonance (a fixed formant, independent of
// the note and of how hard it is pushed), and only while the resonator
// pressure is large enough to reach a limit. A membrane is fixed bulging
// (wax ring, glue), so its two limits differ and the buzz carries even as
// well as odd products.
//
// Normalized units: time is scaled by the slack resonance, and the stored
// energy is E = (x^2 + v^2) / 2 + P(x). A discrete-gradient midpoint update
// changes E by exactly the pressure work F * dx minus the loss
// L/2 * (v[n+1] + v[n])^2 >= 0. Beyond a limit the loss grows with the
// overshoot (a slap loses energy, as in Hunt-Crossley contact damping); Step
// returns that contact loss, which the air has to supply.
//
// Within the slack range the membrane simply follows the air, mostly part of
// the resonator's own calibrated radiation. A shadow membrane that never goes
// taut is driven identically; the radiated buzz is the difference of their
// accelerations plus a tenth of the slack motion (the opening's own small,
// formant-coloured radiation), so Buzz colours even below the snap threshold
// and adds texture above it without re-leveling the note.
// Formants, gaps and stiffness are voiced, not measured membrane constants.
class LegacySlackMembrane {
    friend struct MarembaVoiceTestAccess; // seeds an unforced release in tests
public:
    void Configure(double sampleRate, double formantHz, double quality) {
        const double fs = sampleRate > 8000.0 ? sampleRate : 88200.0;
        const double h = 2.0 * 3.14159265358979323846 * std::clamp(formantHz, 50.0, 0.2 * fs) / fs;
        mStep = h;
        mLoss = h / (2.0 * std::max(quality, 0.3));
        mForceStep = h * h / (2.0 * (1.0 + mLoss));
        mVelocityStep = h / (1.0 + mLoss);
        mInverseHalfStep = 2.0 / h;
        mInverseStep = 1.0 / h;
    }

    // Limits in normalized displacement; tautStiffness is the extra spring
    // beyond a limit (taut resonance = sqrt(1 + tautStiffness) x slack
    // resonance) and slapLoss the extra loss per unit overshoot / gap.
    void SetLimits(double gapOut, double gapIn, double stiffness, double slapLoss) {
        mGapOut = std::max(gapOut, 1.0e-9);
        mGapIn = std::max(gapIn, 1.0e-9);
        mStiffness = std::max(stiffness, 0.0);
        mSlapLoss = std::max(slapLoss, 0.0) / std::min(mGapOut, mGapIn);
    }

    void Reset() { mX = mV = mLinearX = mLinearV = mAcceleration = 0.0; }
    bool IsResting() const { return mX == 0.0 && mV == 0.0 && mLinearX == 0.0 && mLinearV == 0.0; }
    bool IsFinite() const {
        return std::isfinite(mX) && std::isfinite(mV) && std::isfinite(mAcceleration)
            && std::isfinite(mLinearX) && std::isfinite(mLinearV);
    }
    bool HasNoSubnormalState() const {
        for (double value : {mX, mV, mLinearX, mLinearV})
            if (std::fpclassify(value) == FP_SUBNORMAL) return false;
        return true;
    }
    // Twice the stored energy of both states: the voice's squared-amplitude
    // convention, so lifetime and flushing see the shadow as well.
    double Energy() const {
        return mV * mV + 2.0 * Potential(mX) + mLinearV * mLinearV + mLinearX * mLinearX;
    }
    void FlushTiny() { if (Energy() < 1.0e-30) Reset(); }
    double Displacement() const { return mX; }
    // Radiated buzz: the acceleration added by the taut limits.
    double Acceleration() const { return mAcceleration; }

    // One sample. Returns the slap (contact) loss of this step in units of E;
    // the caller takes it from the driving air as a resistive load. pole < 1
    // applies hand damping to every state.
    double Step(double force, double pole) {
        // Shadow membrane: the same slack oscillator without limits.
        const double linearTarget = mVelocityStep * mLinearV + mForceStep * force;
        const double linearX = (linearTarget + mLinearX - 0.5 * mForceStep * mLinearX) / (1.0 + 0.5 * mForceStep);
        const double linearV = (linearX - mLinearX) * mInverseHalfStep - mLinearV;
        const double linearAcceleration = (linearV - mLinearV) * mInverseStep;
        mLinearX = linearX * pole;
        mLinearV = linearV * pole;

        const double oldX = mX;
        // The slap loss follows the overshoot at the start of the step; the
        // damping coefficient is then fixed, so the update stays conservative.
        // It is a damping ratio (a multiple of the slack loss, so the same at
        // every rate), capped at 12x where the midpoint rule still damps
        // every frequency instead of ringing at Nyquist.
        const double loss = mLoss * (1.0 + std::min(mSlapLoss * Overshoot(oldX), 11.0));
        const double k = loss == mLoss ? mForceStep : mStep * mStep / (2.0 * (1.0 + loss));
        const double velocityStep = loss == mLoss ? mVelocityStep : mStep / (1.0 + loss);
        const double target = velocityStep * mV + k * force;
        // Inside the slack range the update is linear and solved directly.
        double x = (target + oldX - 0.5 * k * oldX) / (1.0 + 0.5 * k);
        if (Overshoot(x) > 0.0 || Overshoot(oldX) > 0.0) {
            // Newton on x - oldX + k*G(x) - target, increasing in x because
            // the discrete gradient G of a convex potential is monotone.
            for (int iteration = 0; iteration < 8; ++iteration) {
                double gradient, slope;
                DiscreteGradient(x, oldX, gradient, slope);
                const double step = (x - oldX + k * gradient - target) / (1.0 + k * slope);
                x -= step;
                if (std::abs(step) <= 1.0e-15 * (1.0 + std::abs(x))) break;
            }
        }
        const double velocity = (x - oldX) * mInverseHalfStep - mV;
        mAcceleration = (velocity - mV) * mInverseStep - 0.9 * linearAcceleration;
        const double sum = velocity + mV;
        mX = x * pole;
        mV = velocity * pole;
        return 0.5 * (loss - mLoss) * sum * sum;
    }

private:
    double Overshoot(double x) const {
        return std::max(x - mGapOut, 0.0) + std::max(-x - mGapIn, 0.0);
    }
    double Potential(double x) const {
        const double out = std::max(x - mGapOut, 0.0), in = std::max(-x - mGapIn, 0.0);
        return 0.5 * (x * x + mStiffness * (out * out + in * in));
    }
    double Force(double x) const {
        return x + mStiffness * (std::max(x - mGapOut, 0.0) - std::max(-x - mGapIn, 0.0));
    }
    double Curvature(double x) const {
        return 1.0 + (Overshoot(x) > 0.0 ? mStiffness : 0.0);
    }
    // G(x, old) = (P(x) - P(old)) / (x - old) and dG/dx; the midpoint forms
    // are the limit when both points coincide.
    void DiscreteGradient(double x, double old, double& gradient, double& slope) const {
        const double dx = x - old;
        if (std::abs(dx) > 1.0e-9 * (1.0 + std::abs(x))) {
            gradient = (Potential(x) - Potential(old)) / dx;
            slope = (Force(x) - gradient) / dx;
        } else {
            gradient = Force(0.5 * (x + old));
            slope = 0.5 * Curvature(0.5 * (x + old));
        }
    }

    double mX = 0.0, mV = 0.0, mLinearX = 0.0, mLinearV = 0.0, mAcceleration = 0.0;
    double mStep = 0.0, mLoss = 0.0, mForceStep = 0.0, mVelocityStep = 0.0;
    double mInverseHalfStep = 0.0, mInverseStep = 0.0;
    double mGapOut = 1.0, mGapIn = 1.0, mStiffness = 0.0, mSlapLoss = 0.0;
};

} // namespace maremba


void legacyZero() {
    size_t compared = 0;
    for (double rate : {22050., 44100., 48000., 96000., 192000.}) {
        maremba::LegacySlackMembrane old;
        SlackMembrane current;
        old.Configure(rate, 383.3333333333333, 2.5);
        current.Configure(rate, 383.3333333333333, 2.5);
        old.SetLimits(.075, .125, 8, 20);
        current.SetLimits(.075, .125, 8, 20);
        current.SetMaterial(0, 0);
        uint32_t state = 0x59281642;
        for (int n = 0; n < 16000; ++n) {
            const double force = n < 6000 ? 25 * random(state) : 0;
            const double pole = n < 9000 ? 1 : .99;
            const double a = old.Step(force, pole), b = current.Step(force, pole);
            check(same(a, b) && same(old.Energy(), current.Energy())
                && same(old.Acceleration(), current.Acceleration())
                && same(old.Displacement(), current.Displacement()), "zero material changed legacy bits");
            ++compared;
        }
    }
    std::cout << "LEGACY bit_exact_samples=" << compared << '\n';
}

int main() {
    legacyZero();
    workAndRelease();
    convergence();
    hardening();
    std::cout << "SlackMembraneStretchTest " << (failures ? "FAILED" : "PASSED")
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
