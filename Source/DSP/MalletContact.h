#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// A mallet head (a mass) meeting the bar through a nonlinear felt, yarn,
// rubber or wood contact:
//     F = K d^p (1 + lambda d') + Kc (d - dc)^pc (1 + lambda d'),  F >= 0,
// d = compression (mallet position minus bar surface), the second term being
// a harder core that yarn and cord mallets reach on firm strokes. The bar
// enters through its modal driving-point response: the voice predicts the
// surface displacement without this sample's force, and the force moves it by
// compliance * F within the same sample. The implicit equation for the
// compression is monotone and is solved by safeguarded Newton each sample
// while the mallet is near the bar. Contact duration, spectrum, core "click", repeated
// contacts on a moving bar and the energy a restrike takes from a ringing bar
// all follow from masses and stiffnesses instead of prescribed pulses.
class MalletContact {
public:
    struct Setup {
        double mass = 0.028;
        double stiffness = 2.0e10, exponent = 2.5;
        double coreDepth = 0.0, coreStiffness = 0.0, coreExponent = 1.5;
        double hysteresis = 0.4;
        double velocity = 1.0;      // approach speed (m/s)
        double compliance = 0.0;    // bar displacement per newton within one step (m/N)
        double dt = 1.0 / 88200.0;
    };

    void Reset() {
        mActive = false;
        mInContact = false;
        mForce = mPrevious = mPosition = mVelocity = 0.0;
        mSamples = 0;
        mSeparated = false;
        mHold = mGrip = 0.0;
        mLifting = false;
    }

    // The mallet arrives at the bar surface with the given speed.
    void Start(const Setup& setup, double barDisplacement) {
        mSetup = setup;
        mSetup.mass = std::max(setup.mass, 1.0e-4);
        mSetup.dt = std::max(setup.dt, 1.0e-7);
        // Trapezoidal rule: over each step the head and the bar both feel the
        // mean of the previous and the new force, so a lossless contact keeps
        // its energy and delivers the same impulse at any rate.
        SetCompliance(setup.compliance);
        mPosition = barDisplacement;
        mVelocity = std::max(setup.velocity, 0.0);
        mPrevious = 0.0;
        mForce = 0.0;
        mSamples = 0;
        mSeparated = false;
        // Not touching until the head meets the surface: a ringing bar may be
        // swinging away when the stroke arrives and meet the head a moment later.
        mInContact = false;
        mActive = mVelocity > 0.0;
        mLimit = static_cast<int>(kTrackingSeconds / mSetup.dt);
        mHold = mGrip = 0.0;
        mLifting = false;
    }

    // Retuning changes the bar's instantaneous driving-point response while
    // the head can still be touching it. Keep that implicit response current
    // without restarting the head, its hysteresis history, or a held stroke.
    void SetCompliance(double compliance) {
        mSetup.compliance = std::max(compliance, 0.0);
        mStepCompliance = .25 * mSetup.dt * mSetup.dt / mSetup.mass + .5 * mSetup.compliance;
    }

    // A dead stroke: the hand keeps pressing the head into the bar with
    // `force` (N) and grips the stick, a damper of `grip` (N s/m) on the
    // head's motion, until Lift(). The pressed head's own hysteresis then
    // takes the bar's energy (c = lambda F), with the grip behind it.
    void Hold(double force, double grip) {
        mHold = std::max(force, 0.0);
        mGrip = std::max(grip, 0.0);
        mLifting = false;
    }
    // The hand eases off over a few milliseconds and lifts the stick away:
    // the head leaves the bar.
    void Lift() {
        if (mHold <= 0.0 || mLifting) return;
        mLifting = true;
        mLiftPole = std::exp(-mSetup.dt / kLiftSeconds);
    }
    bool Holding() const { return mActive && mHold > 0.0; }
    double HoldForce() const { return mActive ? mHold : 0.0; }   // the hand's push (N)

    bool Active() const { return mActive; }
    bool InContact() const { return mInContact; }
    double Force() const { return mForce; }
    double Velocity() const { return mVelocity; }
    bool IsFinite() const { return std::isfinite(mPosition) && std::isfinite(mVelocity) && std::isfinite(mForce); }
    void Stop() { mActive = false; mInContact = false; mForce = 0.0; }

    // One sample. freeDisplacement: the bar surface at the contact point this
    // sample without this step's force. texture: small relative force
    // modulation. Returns the force to apply to the bar over this step (the
    // mean of the previous and the new contact force).
    double Step(double freeDisplacement, double texture = 0.0) {
        if (!mActive) return 0.0;
        const double dt = mSetup.dt;
        // The hand's push moves the head by half its acceleration over the step.
        const double push = .5 * mHold / mSetup.mass * dt * dt;
        const double free = mPosition + mVelocity * dt + push - freeDisplacement - mStepCompliance * mForce;
        double force = 0.0;
        if (free > 0.0) {
            // Fibre slip changes the contact law itself. Include it in the
            // implicit balance: scaling the force afterwards would leave a
            // different compression from the one used to compute that force.
            const double textureScale = std::max(0.0, 1.0 + texture);
            // h(d) = d + A Phi(d) - free is increasing in the compression d,
            // which lies in [0, free]: safeguarded Newton on d (a bracket that
            // stays small at any rate, unlike one on the force).
            double lo = 0.0, hi = free, hLo = -free, hHi = 0.0;
            bool hiKnown = false;
            // Start from the compression that last sample's force would leave
            // (the force changes little between samples).
            double d = std::clamp(free - mStepCompliance * mForce, lo, hi);
            for (int iteration = 0; iteration < 60; ++iteration) {
                double slope = 0.0;
                const double h = d + mStepCompliance * (textureScale * Law(d, slope)) - free;
                if (h == 0.0) break;   // on the root
                if (h > 0.0) { hi = d; hHi = h; hiKnown = true; } else { lo = d; hLo = h; }
                double next = d - h / (1.0 + mStepCompliance * (textureScale * slope));
                // A step that leaves the bracket (at a kink of the law) goes
                // to its upper end if that is still unevaluated (a rebound
                // whose damping cancels the force puts the root there), else
                // along the secant through its ends.
                if (!(next > lo && next < hi)) {
                    if (!hiKnown && next >= hi) {
                        next = hi;
                    } else {
                        next = hiKnown ? lo - hLo * (hi - lo) / (hHi - hLo) : 0.5 * (lo + hi);
                        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
                    }
                }
                const bool done = std::abs(next - d) <= 1.0e-12 * free;
                d = next;
                if (done || hi - lo <= 1.0e-14 * free) break;
            }
            double slope = 0.0;
            force = Law(d, slope) * textureScale;
        }
        const double compression = free - mStepCompliance * force;
        const double applied = .5 * (mForce + force);
        const double oldVelocity = mVelocity;
        mVelocity -= (applied - mHold) / mSetup.mass * dt;
        // The grip, implicitly (stable at any rate): v' = v / (1 + c dt / m).
        if (mGrip > 0.0) mVelocity /= 1.0 + mGrip * dt / mSetup.mass;
        if (mSeparated && mHold <= 0.0) mVelocity -= kRetraction * dt;   // the wrist lifts the stick away
        mPosition += .5 * (oldVelocity + mVelocity) * dt;
        if (mLifting) {
            // The grip holds the head steady while the push eases off, so it
            // leaves the bar without a slap.
            mHold *= mLiftPole;
            if (mHold < kLiftDone) {
                mHold = mGrip = 0.0;
                mLifting = false;
                mSeparated = true;
                mSamples = 0;
            }
        }
        mPrevious = compression;
        mForce = force;
        const bool touching = force > 0.0;
        if (!touching && mInContact) mSeparated = true;
        mInContact = touching;
        // Follow the head after it leaves: a moving bar may catch it again.
        // A held head stays until the hand lifts it.
        if (mHold <= 0.0 && (++mSamples > mLimit || (mSeparated && compression < -kClearance))) {
            mActive = false;
            mInContact = false;
            mForce = 0.0;
        }
        return applied;
    }

private:
    static constexpr double kTrackingSeconds = 0.030;
    static constexpr double kRetraction = 60.0;   // m/s^2 after the first separation
    static constexpr double kClearance = 2.5e-3;  // head this far above the bar: stroke over
    static constexpr double kLiftSeconds = 0.015; // a held head's push eases off with this time constant ...
    static constexpr double kLiftDone = 1.0e-3;   // ... until below this (N): then the wrist lifts it

    // d^p: the rubber, wood and core laws (1.5) and cord (2.5) by square roots.
    static double Power(double d, double p) {
        if (p == 1.5) return d * std::sqrt(d);
        if (p == 2.5) return d * d * std::sqrt(d);
        return std::pow(d, p);
    }

    // Contact force and dF/dd at compression d (rate from the previous sample).
    double Law(double d, double& slope) const {
        slope = 0.0;
        if (d <= 0.0) return 0.0;
        const double rate = (d - mPrevious) / mSetup.dt;
        const double damping = std::max(0.0, 1.0 + mSetup.hysteresis * rate);
        const double base = mSetup.stiffness * Power(d, mSetup.exponent);
        double force = base * damping;
        slope = mSetup.exponent * base / d * damping
              + base * mSetup.hysteresis / mSetup.dt * (damping > 0.0 ? 1.0 : 0.0);
        if (mSetup.coreStiffness > 0.0 && d > mSetup.coreDepth) {
            const double e = d - mSetup.coreDepth;
            const double core = mSetup.coreStiffness * Power(e, mSetup.coreExponent);
            force += core * damping;
            slope += mSetup.coreExponent * core / std::max(e, 1.0e-12) * damping
                   + core * mSetup.hysteresis / mSetup.dt * (damping > 0.0 ? 1.0 : 0.0);
        }
        return std::max(force, 0.0);
    }

    Setup mSetup;
    double mHold = 0.0, mGrip = 0.0, mLiftPole = 1.0;
    bool mLifting = false;
    double mStepCompliance = 1.0;
    double mPosition = 0.0, mVelocity = 0.0, mForce = 0.0, mPrevious = 0.0;
    int mSamples = 0, mLimit = 0;
    bool mActive = false, mInContact = false, mSeparated = false;
};

} // namespace maremba
