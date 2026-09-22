#pragma once

#include <algorithm>
#include <cmath>

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
class SlackMembrane {
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
