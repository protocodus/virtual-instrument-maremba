#pragma once

#include "DspMath.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

// Spring-steel tine geometry: 4.5 x 1.1 mm section, free length about 61 mm at
// C4 falling to 27 mm at E6 on 17-key sets. Only the mass and bending
// stiffness enter the model: the pitch comes from the note.
struct TineGeometry {
    double lengthM;
    double massPerLength;     // kg/m
    double bendingStiffness;  // EI, N m^2
};

inline TineGeometry GetTineGeometry(double fundamentalHz) {
    const double f = std::clamp(fundamentalHz, 20.0, 20000.0);
    const double length = std::clamp(0.061 * std::pow(261.625565 / f, 0.55), 0.015, 0.14);
    constexpr double width = 0.0045, thickness = 0.0011;
    return {length, 7850.0 * width * thickness, 200.0e9 * width * thickness * thickness * thickness / 12.0};
}

// The box: a Helmholtz air mode through the sound holes and the top/back plate
// modes the bridge drives. A 17-key box (~0.8 L) sits near 400 Hz, a little
// below the smaller Hugh Tracey boxes' 440-520 Hz (Holdaway). Voiced Q/levels.
struct KalimbaBodyMode {
    double hz, q, gain;
};
static constexpr int kKalimbaBodyModes = 4;
static constexpr KalimbaBodyMode kKalimbaBody[kKalimbaBodyModes] = {
    {400.0, 8.0, 1.10},     // air through the holes
    {640.0, 12.0, 0.45},    // top plate (1,1)
    {1120.0, 14.0, 0.30},   // top/back plate
    {1900.0, 16.0, 0.22},   // higher plate mode
};

// A hand cupped over the front sound hole (the mod wheel). The box's air
// flows through a 3 cm front hole (two thirds of its conductance) and small
// back holes (a third; a 0.8 L box: 299 Hz front hole alone, 366 Hz with two
// 10 mm back holes). A palm (radius four times the hole's) at height h adds
// end correction b dE with E_d = 0.61 (r/b)^0.18 (b/h)^0.39 once that
// exceeds 0.65 (Benade & Murday 1967), and closer still the radial flow
// through the gap, (b^2 / 2h) ln(r/b). The wheel lowers the palm from 40 mm
// to 1 mm on a log scale: about -40 cents at 20 mm, -230 at 5 mm and more
// than -600 as it closes. The covered hole radiates less (its flow and its
// shielded opening) and the box loses less through it.
struct HoleCover {
    double frequency, radiation, loss;   // factors on the air mode's Hz, gain and 1/Q
};

inline HoleCover KalimbaHoleCover(double cover) {
    constexpr double kHoleRadius = 0.015, kBoard = 0.005, kPalmRatio = 4.0, kFrontShare = 2.0 / 3.0;
    const double c = std::clamp(cover, 0.0, 1.0);
    if (c <= 0.0) return {1.0, 1.0, 1.0};
    const double height = 0.040 * std::pow(0.001 / 0.040, c);
    const double open = kBoard + 2.0 * 0.61 * kHoleRadius;
    const double ed = 0.61 * std::pow(kPalmRatio, 0.18) * std::pow(kHoleRadius / height, 0.39);
    double added = ed >= 0.65 ? (ed - 0.61) * kHoleRadius : 0.0;
    const double gap = kHoleRadius * kHoleRadius / (2.0 * height) * std::log(kPalmRatio);
    added = std::max(added, gap * std::clamp((0.005 - height) / 0.004, 0.0, 1.0));
    const double front = open / (open + added);            // front hole's conductance re open
    const double total = kFrontShare * front + (1.0 - kFrontShare);
    return {std::sqrt(total), kFrontShare * front * front + (1.0 - kFrontShare), 0.4 + 0.6 * total};
}

// Relative bridge loss of a tine mode near the body's resonances: a note on
// a box resonance is the loudest and dies sooner (Holdaway). Averages ~1. The
// air mode may be retuned (cents) and its coupling to the board scaled.
inline double KalimbaBodyLoading(double hz, double airCents = 0.0, double airCoupling = 1.0) {
    double sum = 0.0;
    for (int k = 0; k < kKalimbaBodyModes; ++k) {
        const auto& mode = kKalimbaBody[k];
        const double modeHz = k == 0 ? mode.hz * std::exp2(airCents / 1200.0) : mode.hz;
        const double detune = hz / modeHz - modeHz / hz;
        sum += (k == 0 ? airCoupling : 1.0) * mode.gain / (1.0 + mode.q * mode.q * detune * detune);
    }
    return 0.8 + 0.8 * sum;
}

// What the sounding tines push into the box in one sample: the summed bridge
// forces (N), and the same sum with each tine weighted by its own strike's air
// coupling (the air mode's drive), by its Bloom (the resonances' weight
// against the board), and by both. Without velocity routes every weight is 1.
struct KalimbaBoxDrive {
    double board = 0.0, air = 0.0, bloom = 0.0, bloomAir = 0.0;
    void Add(double force, double airWeight, double bloomWeight) {
        board += force;
        air += force * airWeight;
        bloom += force * bloomWeight;
        bloomAir += force * airWeight * bloomWeight;
    }
};

// One shared body for every sounding tine, driven by the summed bridge
// reaction (N). The board radiates the force broadly above its first modes
// (mass-like), less below them (stiffness-like); the air and plate modes add
// resonant peaks on top (their weight is the Body control). The holes
// radiate the air the board pushes through them, opposite to the board's own
// push, as in a bass-reflex box: below the Helmholtz resonance they take a
// little from the board's sound (1-2 dB), at it they dominate (+5.5 dB at
// 400 Hz with Body at 0.5), above it they add to the board. Cupping the front hole (mod wheel,
// KalimbaHoleCover) lowers the air resonance and its radiation: notes near the
// open resonance drop, notes near the lowered one rise - level and timbre, not pitch. The air mode's
// tuning and its coupling to the board follow the resonator Tune and
// Coupling controls.
class KalimbaBody {
public:
    void Configure(double sampleRate) {
        mRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
        mDcAlpha = std::exp(-kTwoPiD * 18.0 / mRate);
        mDirectAlpha = std::exp(-kTwoPiD * 260.0 / mRate);
        mAppliedCover = -1.0;
        SetCover(mCover);
    }

    void Reset() {
        for (auto& mode : mModes) mode.y1 = mode.y2 = mode.z1 = mode.z2 = 0.0;
        for (auto& dc : mDc) dc = DcBlocker{};
        mDirect = 0.0;
    }

    // Air mode tuning (cents) and coupling to the board (1 = as built).
    void SetAir(double cents, double coupling) {
        cents = std::clamp(cents, -100.0, 100.0);
        coupling = std::clamp(coupling, 0.0, 2.0);
        if (cents == mAirCents && coupling == mAirCoupling) return;
        mAirCents = cents;
        mAirCoupling = coupling;
        mAppliedCover = -1.0;
        SetCover(mCover);
    }

    // 0 = holes open, 1 = covered. Coefficients re-express ringing states.
    void SetCover(double cover) {
        mCover = std::clamp(cover, 0.0, 1.0);
        if (std::abs(mCover - mAppliedCover) < 1.0e-4) return;
        mAppliedCover = mCover;
        for (int k = 0; k < kKalimbaBodyModes; ++k) {
            const auto& spec = kKalimbaBody[k];
            double hz = spec.hz, q = spec.q, gain = spec.gain;
            if (k == 0) {
                const HoleCover hand = KalimbaHoleCover(mCover);
                hz *= std::exp2(mAirCents / 1200.0) * hand.frequency;
                gain *= mAirCoupling * hand.radiation;
                q /= hand.loss;
            }
            auto& mode = mModes[k];
            const double w = kTwoPiD * std::min(hz, 0.45 * mRate) / mRate;
            const double r = std::exp(-kPiD * hz / (q * mRate));
            if (mode.r > 0.0 && mode.w > 1.0e-12) {
                const auto reexpress = [&](double y1, double& y2) {
                    const double quadrature = (mode.r * y2 - y1 * std::cos(mode.w)) / std::sin(mode.w);
                    y2 = (y1 * std::cos(w) + quadrature * std::sin(w)) / r;
                };
                reexpress(mode.y1, mode.y2);
                reexpress(mode.z1, mode.z2);
            }
            mode.r = r;
            mode.w = w;
            mode.c = 2.0 * r * std::cos(w);
            mode.s = r * r;
            // Unity centre gain at every rate, times the mode's level.
            mode.b = gain * ResonatorDenominatorAt(r, w);
        }
    }

    // Returns radiated pressure (close mic); far receives more of the air
    // mode. The air and plate modes are weighted against the board by
    // `fixedResonance` plus `bloomResonance` (the Body knob's share), which
    // applies to each tine's drive in proportion to its own Bloom: the modes
    // run twice, on the plain and on the Bloom-weighted drive (states y and z).
    // `plate` is the top plate's own motion under the bridge, where a contact
    // pickup sits: the plate modes and the air pushing on the plate.
    void Process(const KalimbaBoxDrive& drive, double fixedResonance, double bloomResonance,
                 double& close, double& far, double& plate) {
        // The thumb's static hold is not sound: remove it.
        const double hp = mDc[0].Process(drive.board, mDcAlpha);
        const double hpAir = mDc[1].Process(drive.air, mDcAlpha);
        const double hpBloom = mDc[2].Process(drive.bloom, mDcAlpha);
        const double hpBloomAir = mDc[3].Process(drive.bloomAir, mDcAlpha);
        double airSum = 0.0, plateSum = 0.0, bloomAirSum = 0.0, bloomPlateSum = 0.0;
        for (int k = 0; k < kKalimbaBodyModes; ++k) {
            auto& mode = mModes[k];
            const double y = mode.c * mode.y1 - mode.s * mode.y2 + mode.b * (k == 0 ? hpAir : hp);
            mode.y2 = mode.y1;
            mode.y1 = y;
            const double z = mode.c * mode.z1 - mode.s * mode.z2 + mode.b * (k == 0 ? hpBloomAir : hpBloom);
            mode.z2 = mode.z1;
            mode.z1 = z;
            (k == 0 ? airSum : plateSum) += y;
            (k == 0 ? bloomAirSum : bloomPlateSum) += z;
        }
        // The board itself: stiffness-controlled below ~260 Hz, broad above.
        mDirect = mDirectAlpha * mDirect + (1.0 - mDirectAlpha) * hp;
        const double board = hp - mDirect;
        close = board + fixedResonance * (plateSum - kHoleClose * airSum)
                      + bloomResonance * (bloomPlateSum - kHoleClose * bloomAirSum);
        far = 0.8 * board + fixedResonance * (0.7 * plateSum - kHoleFar * airSum)
                          + bloomResonance * (0.7 * bloomPlateSum - kHoleFar * bloomAirSum);
        plate = plateSum + 0.35 * airSum;
    }

    void FlushTiny() {
        for (auto& mode : mModes) {
            if (std::abs(mode.y1) + std::abs(mode.y2) < 1.0e-15) mode.y1 = mode.y2 = 0.0;
            if (std::abs(mode.z1) + std::abs(mode.z2) < 1.0e-15) mode.z1 = mode.z2 = 0.0;
        }
        for (auto& dc : mDc)
            if (std::abs(dc.in) + std::abs(dc.out) < 1.0e-15) dc = DcBlocker{};
        if (std::abs(mDirect) < 1.0e-15) mDirect = 0.0;
    }

    bool HasNoSubnormalState() const {
        const auto subnormal = [](double x) { return std::fpclassify(x) == FP_SUBNORMAL; };
        for (const auto& mode : mModes)
            if (subnormal(mode.y1) || subnormal(mode.y2) || subnormal(mode.z1) || subnormal(mode.z2)) return false;
        for (const auto& dc : mDc)
            if (subnormal(dc.in) || subnormal(dc.out)) return false;
        return !subnormal(mDirect);
    }

    double Energy() const {
        double e = mDirect * mDirect;
        for (const auto& dc : mDc) e += dc.out * dc.out;
        for (const auto& mode : mModes)
            e += mode.y1 * mode.y1 + mode.y2 * mode.y2 + mode.z1 * mode.z1 + mode.z2 * mode.z2;
        return e;
    }

private:
    // Hole radiation per unit air-mode response: at the resonance the holes
    // radiate a little more than the board (voiced), more toward the far mic.
    static constexpr double kHoleClose = 1.0, kHoleFar = 1.36;
    struct Mode { double y1 = 0.0, y2 = 0.0, z1 = 0.0, z2 = 0.0, c = 0.0, s = 0.0, b = 0.0, r = 0.0, w = 0.0; };
    struct DcBlocker {
        double in = 0.0, out = 0.0;
        double Process(double x, double alpha) {
            out = alpha * (out + x - in);
            in = x;
            return out;
        }
    };
    std::array<Mode, kKalimbaBodyModes> mModes{};
    std::array<DcBlocker, 4> mDc{};   // board, air, bloom and bloom-air drives
    double mRate = 44100.0;
    double mCover = 0.0, mAppliedCover = -1.0;
    double mAirCents = 0.0, mAirCoupling = 1.0;
    double mDcAlpha = 0.99;
    double mDirectAlpha = 0.9, mDirect = 0.0;
};

} // namespace maremba
