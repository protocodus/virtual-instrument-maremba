#pragma once

#include "DspMath.h"
#include "MarimbaModel.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace maremba {

// Declared virtual bonded film, not a replica of an unidentified pickup.
// Measurement Specialties, Piezo Film Sensors Technical Manual, table 1:
// 28 um PVDF; d31=23 pC/N; E=2..4 GPa (we choose 2); C=380 pF/cm2.
// These are typical material values, not toleranced measurements of our mount.
struct ReferencePVDFPatch {
    double lengthM, widthM;
    static constexpr double kThicknessM = 28.e-6;
    static constexpr double kYoungPa = 2.e9;
    static constexpr double kD31 = 23.e-12;
    static constexpr double kPermittivity = 106.4e-12;
    static constexpr double kDensity = 1780.;
    static constexpr double kBulkResistivity = 1.e13; // published lower bound, assumed here
    static constexpr double kLossTangentAt1kHz = .02; // material datum, not a broadband loss fit

    static ReferencePVDFPatch ForModel(EMarimbaModel model) {
        if (model == EMarimbaModel::KalimbaArtisan) return {.0252, .024};
        const auto patch = kBarPickupPatches[std::clamp(static_cast<int>(model), 0, 2)];
        return {patch.lengthM, patch.widthM};
    }
    double Area() const { return lengthM * widthM; }
    double Capacitance() const { return kPermittivity * Area() / kThicknessM; }
    double LeakageResistance() const { return kBulkResistivity * kThicknessM / Area(); }
    double FilmMass() const { return kDensity * Area() * kThicknessM; }
    double ChargeFromStrain(double tensileStrain) const {
        // Stretch/poling axis follows the bar. Assumes perfect strain transfer
        // and negligible transverse stress; adhesive/mount transfer is separate.
        return kD31 * kYoungPa * Area() * tensileStrain;
    }
};

// Reference sensor electronics for the physical surface-strain path.
// TI SLOA033A charge-amplifier topology, OPA1642-derived dominant-pole dynamics
// (SBOS484D: A0=134 dB, GBW=11 MHz, SR=20 V/us), operated at +/-15 V.
// Cp+cable+input capacitance and leakage meet Rf||Cf at the inverting node.
// KCL is integrated in coulombs; the output is volts. No audio normalization,
// AGC, output protection or invented conversion of nominal body/force feeds.
// The reduced model omits transistor-level poles, noise, bias, protection
// diodes and temperature dependence. Material dielectric loss needs a measured
// spectrum; the single tan(delta) datum is not silently treated as DC leakage.
class ReferenceChargeAmp {
public:
    struct Components {
        // 15,360 hard-hit bar fixtures gave <=99.94 nC per-voice modal
        // envelope. 24 coherent copies need <=5.11 V with this capacitor;
        // this is measured sizing evidence, not a universal physical bound.
        double feedbackCapacitance = 470.e-9;
        double feedbackResistance = 68.e3; // 4.98 Hz charge recovery
        double cableCapacitance = 470.e-12; // declared cable, not sensor material
        double inputCapacitance = 14.e-12; // differential 8 pF + common-mode 6 pF
        double openLoopGain = 5011872.336272725; // 134 dB
        double gainBandwidthHz = 11.e6;
        double slewVoltsPerSecond = 20.e6;
        double outputRailVolts = 14.0; // conservative 1 V margin to +/-15 V supplies
    };

    ReferenceChargeAmp() { Configure(kReferenceRate, ReferencePVDFPatch::ForModel(EMarimbaModel::ImperialRosewood), Components{}); }

    // Control-rate only. A changed sensor/circuit requires Reset; changing only
    // the sample rate preserves physical capacitor/node voltage state.
    bool Configure(double fs, const ReferencePVDFPatch& patch, const Components& parts) {
        for (double value : {patch.lengthM, patch.widthM, parts.feedbackCapacitance,
            parts.feedbackResistance, parts.cableCapacitance, parts.inputCapacitance,
            parts.openLoopGain, parts.gainBandwidthHz, parts.slewVoltsPerSecond, parts.outputRailVolts})
            if (!std::isfinite(value)) return false;
        if (!std::isfinite(fs) || fs < 8000. || fs > 1536000.
            || !(patch.lengthM > 0) || !(patch.widthM > 0)
            || !(parts.feedbackCapacitance > 0) || !(parts.feedbackResistance > 0)
            || !(parts.cableCapacitance >= 0) || !(parts.inputCapacitance >= 0)
            || !(parts.openLoopGain > 1) || !(parts.gainBandwidthHz > 0)
            || !(parts.slewVoltsPerSecond > 0) || !(parts.outputRailVolts > 0)) return false;
        const double cf = parts.feedbackCapacitance;
        const double c = patch.Capacitance() + parts.cableCapacitance + parts.inputCapacitance + cf;
        const double gf = 1. / parts.feedbackResistance;
        const double g = gf + 1. / patch.LeakageResistance();
        const double wu = 2. * kPiD * parts.gainBandwidthHz;
        const double wp = wu / parts.openLoopGain;
        // x=[input node voltage, output voltage]. x'=A*x+B*dq/dt.
        const double a00 = -(cf * wu + g) / c, a01 = (gf - cf * wp) / c;
        const double a10 = -wu, a11 = -wp;
        const double trace = -(a00 + a11);
        const double det = (g * wp + gf * wu) / c;
        const double discriminant = trace * trace - 4. * det;
        if (!(discriminant > 0) || !std::isfinite(discriminant)) return false;
        const double fast = .5 * (trace + std::sqrt(discriminant));
        const double slow = det / fast; // avoids cancellation in the slow pole
        const double es = std::exp(-slow / fs), ef = std::exp(-fast / fs);
        const double difference = fast - slow;
        const double common = (es - ef) / difference;
        mE00 = common * a00 + (es * fast - ef * slow) / difference;
        mE01 = common * a01;
        mE10 = common * a10;
        mE11 = common * a11 + (es * fast - ef * slow) / difference;
        // Exact state transition for linearly changing input charge during the
        // sample. Resolves the MHz pole without trapezoidal Nyquist ringing.
        const double t0 = (mE00 - 1.) / c, t1 = mE10 / c;
        mB0 = (a11 * t0 - a01 * t1) / det * fs;
        mB1 = (-a10 * t0 + a00 * t1) / det * fs;
        mC = c; mCf = cf; mDtG = g / fs; mDtGf = gf / fs;
        mSlewStep = parts.slewVoltsPerSecond / fs;
        mRail = parts.outputRailVolts;
        return std::isfinite(mB0) && std::isfinite(mB1);
    }

    double ProcessCharge(double chargeCoulombs) {
        const double dq = chargeCoulombs - mCharge;
        double node = mE00 * mNode + mE01 * mOutput + mB0 * dq;
        double output = mE10 * mNode + mE11 * mOutput + mB1 * dq;
        const double limited = std::clamp(std::clamp(output, mOutput - mSlewStep, mOutput + mSlewStep), -mRail, mRail);
        if (limited != output) {
            // Physical amplifier overload only: the feedback capacitor cannot
            // keep a virtual ground when the amplifier reaches a rail/slew
            // limit. Re-solve input charge balance at the attainable voltage;
            // stored input charge then determines recovery (backward Euler).
            output = limited;
            node = (mC * mNode - mCf * mOutput + dq + (mCf + mDtGf) * output) / (mC + mDtG);
        }
        mNode = node; mOutput = output; mCharge = chargeCoulombs;
        return output;
    }
    void Reset() { mNode = mOutput = mCharge = 0.; }
    double StateEnergy() const { return mNode * mNode + mOutput * mOutput; }
    void FlushTiny() {
        if (std::abs(mNode) < 1.e-18 && std::abs(mOutput) < 1.e-18 && std::abs(mCharge) < 1.e-27) Reset();
    }
    bool HasNoSubnormalState() const {
        return std::fpclassify(mNode) != FP_SUBNORMAL && std::fpclassify(mOutput) != FP_SUBNORMAL
            && std::fpclassify(mCharge) != FP_SUBNORMAL;
    }

private:
    friend struct ReferenceChargeAmpTestAccess;
    double mE00 = 0, mE01 = 0, mE10 = 0, mE11 = 0, mB0 = 0, mB1 = 0;
    double mC = 0, mCf = 0, mDtG = 0, mDtGf = 0, mSlewStep = 0, mRail = 0;
    double mNode = 0, mOutput = 0, mCharge = 0;
};

} // namespace maremba
