#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// Buzzers on an mbira-family board: loose metal rings, bottle caps or shells
// (machachara) that sit on the vibrating board. A buzzer rests while the board
// pushes it; when the board accelerates away faster than the buzzer's preload
// (gravity, a light retaining wire) it lifts off, flies, and lands with a
// clattering impact that rings its own thin-metal modes. So the sizzle needs
// a loud enough note, is densest in the attack and stops as the note decays.
// Buzz sets how loose the buzzer sits: more Buzz, less preload.
//
// The board acceleration arrives as a normalized signal (the voice's bridge
// drive); flight is integrated in physical time, and each landing delivers a
// velocity impulse (1 + e) * v to the ring modes, so the sound does not depend
// on the sample rate. The buzzer's reaction on the board and tine is not
// modelled: its mass is small against theirs. Frequencies, preload and
// restitution are voiced; no measured buzzer spectra were found.
class BuzzerRattle {
public:
    // driveScale divides the incoming board acceleration (register referral).
    void Configure(double sampleRate, double driveScale = 1.0) {
        const double fs = sampleRate > 8000.0 ? sampleRate : 88200.0;
        mStep = 1.0 / fs;
        mDriveScale = 1.0 / std::max(driveScale, 1.0e-3);
        static constexpr double kHz[kModes] = {3100.0, 5600.0};
        static constexpr double kQ[kModes] = {25.0, 20.0};
        static constexpr double kLevel[kModes] = {1.0, 0.6};
        for (int i = 0; i < kModes; ++i) {
            const double w = 2.0 * 3.14159265358979323846 * std::min(kHz[i], 0.42 * fs) / fs;
            const double r = std::exp(-0.5 * w / kQ[i]);
            mRing[i].c = 2.0 * r * std::cos(w);
            mRing[i].s = r * r;
            mRing[i].b = kLevel[i] * std::sin(w);
        }
    }

    // amount 0..1 (Buzz): 0 lifts the buzzers off the board entirely.
    void SetLooseness(double amount, double tension = 0.5) {
        amount = std::clamp(amount, 0.0, 1.0);
        mActive = amount > 0.001;
        if (!mActive) mHeight = mVelocity = 0.0;   // taken off: no late landing
        // Tightens quickly toward low settings, so the default Buzz only
        // rattles on the hardest treble strokes; a pluck's release jolts the
        // board far harder than its ring does.
        // A tighter retaining wire increases the load needed for lift-off.
        // The centre keeps the existing buzzer voicing exactly.
        mPreload = 3000.0 * std::exp(2.79 * (1.0 - amount))
                 * std::exp2(2.0 * (std::clamp(tension, 0.0, 1.0) - 0.5));
        // A looser ring also rings more freely once it has landed.
        mLevel = 0.3 + 0.7 * amount;
    }

    void Reset() {
        mHeight = mVelocity = 0.0;
        mPreviousDrive = 0.0;
        for (auto& ring : mRing) ring.y1 = ring.y2 = 0.0;
    }
    bool IsResting() const {
        return mHeight == 0.0 && mVelocity == 0.0 && mRing[0].y1 == 0.0 && mRing[0].y2 == 0.0
            && mRing[1].y1 == 0.0 && mRing[1].y2 == 0.0;
    }
    bool IsFinite() const {
        return std::isfinite(mHeight) && std::isfinite(mVelocity)
            && std::isfinite(mRing[0].y1) && std::isfinite(mRing[1].y1);
    }
    bool HasNoSubnormalState() const {
        for (double value : {mHeight, mVelocity, mRing[0].y1, mRing[0].y2, mRing[1].y1, mRing[1].y2})
            if (std::fpclassify(value) == FP_SUBNORMAL) return false;
        return true;
    }
    double Energy() const {
        double energy = 0.0;
        for (const auto& ring : mRing)
            energy += ring.y1 * ring.y1 + ring.y2 * ring.y2;
        return energy;
    }
    void FlushTiny() {
        for (auto& ring : mRing)
            if (std::abs(ring.y1) + std::abs(ring.y2) < 1.0e-15) ring.y1 = ring.y2 = 0.0;
    }

    // boardDrive: the board's normalized velocity-like motion. Returns the
    // radiated sizzle; impact receives the landing impulse (for the pickup).
    double Process(double boardDrive, double pole, double& impact) {
        impact = 0.0;
        // Board acceleration from its velocity, in physical time.
        const double acceleration = (boardDrive - mPreviousDrive) / mStep * mDriveScale;
        mPreviousDrive = boardDrive;
        if (mActive) {
            // Height above the board: preload pulls down, the board's own
            // acceleration drives the relative motion the other way.
            const double relative = -mPreload - acceleration;
            if (mHeight <= 0.0 && mVelocity <= 0.0 && relative <= 0.0) {
                mHeight = mVelocity = 0.0;      // pressed onto the board
            } else {
                AdvanceFlight(relative, impact);
            }
        }
        // Resting buzzers with silent rings (flushed to exact zero at the
        // control rate) cost nothing more.
        if (impact == 0.0 && mRing[0].y1 == 0.0 && mRing[0].y2 == 0.0
            && mRing[1].y1 == 0.0 && mRing[1].y2 == 0.0) return 0.0;
        double sound = 0.0;
        for (auto& ring : mRing) {
            const double y = ring.c * ring.y1 - ring.s * ring.y2 + ring.b * impact;
            ring.y2 = ring.y1 * pole;
            ring.y1 = y * pole;
            sound += y;
        }
        return sound * mLevel;
    }

private:
    // Acceleration is constant over this sample. Locate a landing within the
    // interval, then keep its upward restitution velocity for the remaining
    // flight. Simply projecting height to zero after Euler integration loses
    // the rebound; retaining that velocity without locating contact instead
    // produces a perpetual one-sample bounce on a stationary board.
    void AdvanceFlight(double acceleration, double& impact) {
        double remaining = mStep;
        for (int event = 0; event < 8; ++event) {
            const double endHeight = mHeight + mVelocity * remaining
                                   + 0.5 * acceleration * remaining * remaining;
            bool lands = endHeight < 0.0
                      || (endHeight == 0.0 && mVelocity + acceleration * remaining < 0.0);
            // An upward-accelerating board can be crossed on the downward
            // part of a flight even if the endpoint has risen above it again.
            if (!lands && acceleration > 0.0 && mVelocity < 0.0) {
                const double turn = -mVelocity / acceleration;
                lands = turn < remaining && mHeight - mVelocity * mVelocity / (2.0 * acceleration) < 0.0;
            }
            if (!lands) {
                mHeight = std::max(endHeight, 0.0);
                mVelocity += acceleration * remaining;
                return;
            }
            double contactTime = 0.0;
            if (mHeight > 0.0) {
                const double speed = std::sqrt(std::max(0.0, mVelocity * mVelocity - 2.0 * acceleration * mHeight));
                // Rationalized first downward root; no cancellation when a
                // descending buzzer starts close to the board.
                contactTime = 2.0 * mHeight / (speed - mVelocity);
            } else if (mVelocity > 0.0 && acceleration < 0.0) {
                contactTime = -2.0 * mVelocity / acceleration;
            }
            contactTime = std::clamp(contactTime, 0.0, remaining);
            mVelocity += acceleration * contactTime;
            impact += -(1.0 + kRestitution) * std::min(mVelocity, 0.0);
            mVelocity = -kRestitution * std::min(mVelocity, 0.0);
            mHeight = 0.0;
            remaining -= contactTime;
            if (remaining <= 0.0) return;
        }
        // Inelastic collapse: bound realtime work and discard the remaining
        // tiny rebound passively. After eight landings its kinetic energy is
        // at most restitution^16 (5.1e-8) of the first incoming energy.
        mHeight = mVelocity = 0.0;
    }

    static constexpr int kModes = 2;
    static constexpr double kRestitution = 0.35;
    struct Ring { double y1 = 0.0, y2 = 0.0, c = 0.0, s = 0.0, b = 0.0; };
    Ring mRing[kModes];
    bool mActive = false;
    double mStep = 1.0 / 88200.0, mPreload = 1.0e9, mDriveScale = 1.0, mLevel = 1.0;
    double mHeight = 0.0, mVelocity = 0.0, mPreviousDrive = 0.0;
};

} // namespace maremba
