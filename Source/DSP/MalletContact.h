#pragma once

#include "ModalExcitation.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// Linearized contact correction around the nominal finite strike pulse. A
// moving bar sees a finite mallet inertia through a viscous contact footprint.
// This does not solve a Hertz trajectory or predict the separation time: the
// prescribed contact window gates both ports, smoothly releasing the mallet.
// Velocities use the voice's normalized modal-energy coordinates (unit modal
// masses); the relative head mass is a voiced effective contact inertia.
class MalletContact {
public:
    static constexpr int kCapacity = 8;

    void Configure(int count, double position, double width, double loss, const double* mobility = nullptr) {
        count = std::clamp(count, 0, kCapacity);
        const double halfWidth = 0.5 * std::clamp(width, 0.0, 0.25);
        const double lo = std::max(0.0, position - halfWidth);
        const double hi = std::min(1.0, position + halfWidth);
        const double centre = 0.5 * (lo + hi);
        const double offset = (hi - lo) * 0.288675134594813;
        double left[kCapacity]{}, right[kCapacity]{};
        for (int m = 0; m < count; ++m) {
            left[m] = ModalContactWeight(m, centre - offset, 0.0, false);
            right[m] = ModalContactWeight(m, centre + offset, 0.0, false);
        }
        ConfigureProjection(count, loss, left, right, mobility);
    }

    // Two footprint quadrature samples from the voice's actual bar geometry.
    // Keeping the projection shared with excitation makes the same physical
    // nodes protect a ringing bar and suppress a new strike.
    void ConfigureProjection(int count, double loss, const double* leftShape,
                             const double* rightShape, const double* mobility = nullptr) {
        mCount = std::clamp(count, 0, kCapacity);
        mLoss = std::max(loss, 0.0);
        mHeadVelocity = 0.0;
        mCoherentNorm = kHeadMobility * kHeadMobility;
        mShearNorm = 0.0;
        for (int m = 0; m < mCount; ++m) {
            const double left = leftShape[m];
            const double right = rightShape[m];
            // The voice's modal amplitudes already contain its calibrated
            // strike/readout mobility. Reuse that force map here: bare unit
            // mobilities would inject excessive audible upper-mode motion.
            const double scale = mobility ? std::max(0.0, mobility[m]) : 1.0;
            mCoherent[m] = 0.5 * (left + right) * scale;
            mShear[m] = 0.5 * (left - right) * scale;
            mCoherentNorm += mCoherent[m] * mCoherent[m];
            mShearNorm += mShear[m] * mShear[m];
        }
    }

    void Reset() { mHeadVelocity = 0.0; }
    void Damp(double pole) { mHeadVelocity *= pole; }
    double Energy() const { return mHeadVelocity * mHeadVelocity; }
    bool IsFinite() const { return std::isfinite(mHeadVelocity); }
    bool HasNoSubnormalState() const { return std::fpclassify(mHeadVelocity) != FP_SUBNORMAL; }
    void FlushTiny() { if (std::abs(mHeadVelocity) < 1.0e-15) mHeadVelocity = 0.0; }

    // fraction is the integral of the unit-area contact window over this
    // sample. Exact dissipative subflows keep their loss independent of rate:
    // v' = v - (1-exp(-a*||d||²)) d(d.v)/||d||².
    // The norm of the bar velocities AND head velocity can only decrease.
    // Symmetric splitting applies footprint shear between two coherent half
    // steps. The shear port retains absorption when a wide mallet spans a node.
    void Process(double* velocity, double fraction) {
        const double step = mLoss * std::max(fraction, 0.0);
        if (!(step > 0.0) || mCount == 0) return;
        CoherentStep(velocity, step);
        if (mShearNorm > 1.0e-20) {
            double projected = 0.0;
            for (int m = 0; m < mCount; ++m) projected += mShear[m] * velocity[m];
            const double reaction = (1.0 - std::exp(-2.0 * step * mShearNorm)) * projected / mShearNorm;
            for (int m = 0; m < mCount; ++m) velocity[m] -= mShear[m] * reaction;
        }
        CoherentStep(velocity, step);
    }

private:
    // Effective head mass = 2 modal mass units; no hard/soft mass guess is
    // needed because material compliance already controls loss and duration.
    static constexpr double kHeadMobility = 0.7071067811865475244;
    void CoherentStep(double* velocity, double step) {
        double projected = -kHeadMobility * mHeadVelocity;
        for (int m = 0; m < mCount; ++m) projected += mCoherent[m] * velocity[m];
        const double reaction = (1.0 - std::exp(-step * mCoherentNorm)) * projected / mCoherentNorm;
        for (int m = 0; m < mCount; ++m) velocity[m] -= mCoherent[m] * reaction;
        mHeadVelocity += kHeadMobility * reaction;
    }

    int mCount = 0;
    double mLoss = 0.0;
    double mCoherent[kCapacity]{};
    double mShear[kCapacity]{};
    double mCoherentNorm = 1.0;
    double mShearNorm = 0.0;
    double mHeadVelocity = 0.0;
};

} // namespace maremba
