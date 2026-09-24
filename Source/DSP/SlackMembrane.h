#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// A slack buzzing membrane (balafon mirliton, Mesoamerican charleo) over a
// hole in a resonator wall. Inside its slack range it is a light, lossy
// oscillator with weak tension. Pressure that drives it past either limit
// pulls it taut: a much stiffer one-sided spring snaps it back. These snaps,
// not a smooth distortion of the note, make the buzz: short accelerations
// ringing around the taut membrane's reference resonance, with positive
// stretching raising its frequency at large excursions, and only while the resonator
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
        UpdateMaterial();
    }

    // Geometric stretching once the wrinkles have pulled taut. At an
    // overshoot equal to one gap, stretch is the cubic/linear spring-force
    // ratio. Strain loss is additional positive Kelvin--Voigt-type damping
    // per squared overshoot/gap. Both are dimensionless voicing parameters,
    // not measured spider-silk or intestinal-membrane material constants.
    // Zero keeps the original slack/taut response bit for bit.
    void SetMaterial(double stretch, double strainLoss) {
        mStretch = std::max(stretch, 0.0);
        mStrainLoss = std::max(strainLoss, 0.0);
        UpdateMaterial();
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
        double extraLoss = mSlapLoss * Overshoot(oldX);
        if (mStrainLoss > 0.0) {
            const double out = std::max(oldX - mGapOut, 0.0);
            const double in = std::max(-oldX - mGapIn, 0.0);
            extraLoss += mStrainOut * out * out + mStrainIn * in * in;
        }
        const double loss = mLoss * (1.0 + std::min(extraLoss, 11.0));
        const double k = loss == mLoss ? mForceStep : mStep * mStep / (2.0 * (1.0 + loss));
        const double velocityStep = loss == mLoss ? mVelocityStep : mStep / (1.0 + loss);
        const double target = velocityStep * mV + k * force;
        // Inside the slack range the update is linear and solved directly.
        double x = (target + oldX - 0.5 * k * oldX) / (1.0 + 0.5 * k);
        if (Overshoot(x) > 0.0 || Overshoot(oldX) > 0.0) {
            if (mStretch > 0.0) {
                x = SolveStretched(x, oldX, k, target);
            } else {
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
        }
        const double velocity = (x - oldX) * mInverseHalfStep - mV;
        mAcceleration = (velocity - mV) * mInverseStep - 0.9 * linearAcceleration;
        const double sum = velocity + mV;
        mX = x * pole;
        mV = velocity * pole;
        return 0.5 * (loss - mLoss) * sum * sum;
    }

private:
    void UpdateMaterial() {
        mCubicOut = mStiffness * mStretch / (mGapOut * mGapOut);
        mCubicIn = mStiffness * mStretch / (mGapIn * mGapIn);
        mStrainOut = mStrainLoss / (mGapOut * mGapOut);
        mStrainIn = mStrainLoss / (mGapIn * mGapIn);
    }
    double Overshoot(double x) const {
        return std::max(x - mGapOut, 0.0) + std::max(-x - mGapIn, 0.0);
    }
    double Potential(double x) const {
        const double out = std::max(x - mGapOut, 0.0), in = std::max(-x - mGapIn, 0.0);
        const double linear = 0.5 * (x * x + mStiffness * (out * out + in * in));
        if (mStretch == 0.0) return linear;
        return linear + 0.25 * (mCubicOut * out * out * out * out
                              + mCubicIn * in * in * in * in);
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

    // Factor the divided differences of each unilateral quadratic/quartic
    // potential. Unlike subtracting two total energies, these formulas stay
    // accurate at almost-equal states and at either slack/taut transition.
    void StretchGradient(double x, double old, double& gradient, double& slope) const {
        gradient = 0.5 * (x + old);
        slope = 0.5;
        for (int side = 0; side < 2; ++side) {
            const double sign = side == 0 ? 1.0 : -1.0;
            const double gap = side == 0 ? mGapOut : mGapIn;
            const double cubic = side == 0 ? mCubicOut : mCubicIn;
            const double a = sign * x - gap, b = sign * old - gap;
            if (a <= 0.0 && b <= 0.0) continue;
            if (a >= 0.0 && b >= 0.0) {
                gradient += sign * (a + b) * (0.5 * mStiffness + 0.25 * cubic * (a * a + b * b));
                slope += 0.5 * mStiffness + 0.25 * cubic * (3.0 * a * a + 2.0 * a * b + b * b);
            } else {
                // A transition always has x != old. Its potential difference
                // is one nonzero endpoint, so there is no cancellation.
                const double q = std::max(a, b);
                const double potential = q * q * (0.5 * mStiffness + 0.25 * cubic * q * q);
                const double dx = x - old;
                const double g = (a > 0.0 ? potential : -potential) / dx;
                gradient += g;
                const double force = a > 0.0 ? sign * a * (mStiffness + cubic * a * a) : 0.0;
                slope += (force - g) / dx;
            }
        }
    }

    double SolveStretched(double x, double old, double k, double target) const {
        double gradient, slope;
        StretchGradient(x, old, gradient, slope);
        double residual = x - old + k * gradient - target;
        // The full residual derivative is >= 1 + k/2. A step made with that
        // lower bound brackets its unique root, even on a large limit crossing.
        const double bound = x - residual / (1.0 + 0.5 * k);
        double lo = std::min(x, bound), hi = std::max(x, bound);
        // Normally 3--5 Newton steps. Bracketing guarantees progress for
        // extreme impulses; a fixed cap keeps realtime work bounded.
        for (int iteration = 0; iteration < 48; ++iteration) {
            const double correction = residual / (1.0 + k * slope);
            if (std::abs(correction) <= 2.0e-15 * std::max({mGapOut, mGapIn, std::abs(x)})) break;
            if (residual > 0.0) hi = x; else lo = x;
            const double next = x - correction;
            x = next > lo && next < hi ? next : lo + 0.5 * (hi - lo);
            StretchGradient(x, old, gradient, slope);
            residual = x - old + k * gradient - target;
        }
        return x;
    }

    double mX = 0.0, mV = 0.0, mLinearX = 0.0, mLinearV = 0.0, mAcceleration = 0.0;
    double mStep = 0.0, mLoss = 0.0, mForceStep = 0.0, mVelocityStep = 0.0;
    double mInverseHalfStep = 0.0, mInverseStep = 0.0;
    double mGapOut = 1.0, mGapIn = 1.0, mStiffness = 0.0, mSlapLoss = 0.0;
    double mStretch = 0.0, mStrainLoss = 0.0, mCubicOut = 0.0, mCubicIn = 0.0;
    double mStrainOut = 0.0, mStrainIn = 0.0;
};

} // namespace maremba
