#include "PiezoReferenceCircuit.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>

namespace maremba {
struct ReferenceChargeAmpTestAccess {
    static double Node(const ReferenceChargeAmp& amp) { return amp.mNode; }
    static double Output(const ReferenceChargeAmp& amp) { return amp.mOutput; }
};
}

namespace {
using namespace maremba;
using Complex = std::complex<double>;
int checks = 0;
void require(bool okay, const char* text) {
    ++checks;
    if (!okay) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}

// Independent continuous-time nodal solution: sensor Norton current s*q,
// input admittance Ys, feedback Yf and opamp Vout=-A(s)*Vnode.
Complex nodal(double hz, const ReferencePVDFPatch& patch, const ReferenceChargeAmp::Components& p) {
    const Complex s(0, 2 * kPiD * hz);
    const Complex a = p.openLoopGain / (1. + s * p.openLoopGain / (2 * kPiD * p.gainBandwidthHz));
    const Complex ys = 1. / patch.LeakageResistance()
        + s * (patch.Capacitance() + p.cableCapacitance + p.inputCapacitance);
    const Complex yf = 1. / p.feedbackResistance + s * p.feedbackCapacitance;
    return -a * s / (ys + (1. + a) * yf);
}

Complex measure(double rate, double hz, const ReferencePVDFPatch& patch, const ReferenceChargeAmp::Components& parts) {
    ReferenceChargeAmp amp;
    require(amp.Configure(rate, patch, parts), "valid circuit rejected");
    constexpr double q = 1.e-9;
    const int settle = static_cast<int>(rate * .8);
    const int samples = static_cast<int>(std::ceil(rate * std::max(.03, 12 / hz)));
    double cc = 0, ss = 0, cs = 0, yc = 0, ys = 0;
    for (int i = 0; i < settle + samples; ++i) {
        const double phase = 2 * kPiD * hz * i / rate;
        const double c = std::cos(phase), s = std::sin(phase);
        const double y = amp.ProcessCharge(q * c) / q;
        if (i >= settle) { cc += c*c; ss += s*s; cs += c*s; yc += y*c; ys += y*s; }
    }
    const double determinant = cc * ss - cs * cs;
    return {(yc * ss - ys * cs) / determinant, -(ys * cc - yc * cs) / determinant};
}

void material() {
    for (int model = 0; model < 4; ++model) {
        const auto patch = ReferencePVDFPatch::ForModel(static_cast<EMarimbaModel>(model));
        const double capacitance = patch.Capacitance();
        // Published 380 pF/cm2 at28um; published E=2GPa reference gives
        // approximately12mV per microstrain open-circuit, independent of area.
        require(std::abs(capacitance / (patch.Area() * 3.8e-6) - 1.) < 1.e-14,
                "capacitance is not derived from electrode area and film thickness");
        const double volts = patch.ChargeFromStrain(1.e-6) / capacitance;
        require(std::abs(volts / .0121052631578947 - 1.) < 1.e-12,
                "strain/charge/capacitance units are inconsistent");
        require(patch.ChargeFromStrain(-1.e-6) == -patch.ChargeFromStrain(1.e-6), "sensor rectifies strain");
        require(patch.FilmMass() > 0 && patch.FilmMass() < 1.e-4, "film mass unit error");
        std::cout << "MATERIAL model=" << model << " capacitance_F=" << capacitance
                  << " charge_per_microstrain_C=" << patch.ChargeFromStrain(1.e-6)
                  << " film_mass_kg=" << patch.FilmMass() << '\n';
    }
}

void transfer() {
    double maximumError = 0;
    int comparisons = 0;
    for (int model = 0; model < 4; ++model) {
        const auto patch = ReferencePVDFPatch::ForModel(static_cast<EMarimbaModel>(model));
        for (double rate : {8000., 44100., 48000., 96000., 192000., 768000., 1536000.})
            for (double hz : {20., 100., 1000., 4000., 16000.}) {
            if (hz > rate * .4) continue;
            ReferenceChargeAmp::Components parts;
            const Complex actual = measure(rate, hz, patch, parts), expected = nodal(hz, patch, parts);
            const double error = std::abs(actual / expected - 1.);
            maximumError = std::max(maximumError, error);
            require(error < .004, "sampled circuit differs from independent nodal transfer");
            ++comparisons;
        }
    }
    const auto patch = ReferencePVDFPatch::ForModel(EMarimbaModel::ImperialRosewood);
    ReferenceChargeAmp::Components slowed;
    slowed.gainBandwidthHz = 50000;
    for (double hz : {1000., 10000., 50000., 100000.}) {
        const auto a = measure(1536000, hz, patch, slowed), e = nodal(hz, patch, slowed);
        require(std::abs(a / e - 1.) < .02, "finite opamp bandwidth is not simulated");
    }
    std::cout << "TRANSFER comparisons=" << comparisons << " max_continuous_time_relative_error=" << maximumError << '\n';
}

void cleanAndState() {
    double maximumLinearityError = 0, maximumPeak = 0, maximumNode = 0;
    // Twice the measured24-voice coherent modal envelope: extra6dB allowance.
    constexpr double chargeBudget = 2 * 24 * 9.99326839827e-8;
    for (int model = 0; model < 4; ++model) for (double rate : {44100., 48000., 192000., 768000., 1536000.}) {
        const auto patch = ReferencePVDFPatch::ForModel(static_cast<EMarimbaModel>(model));
        ReferenceChargeAmp::Components parts;
        ReferenceChargeAmp a, b;
        require(a.Configure(rate, patch, parts) && b.Configure(rate, patch, parts), "configure failed");
        // Physical charge is continuous and modal-bandlimited. Full-scale
        // independent random samples at1.536MHz correctly exceed the opamp's
        // 20V/us slew, so that is an overload fixture, not a clean-hit oracle.
        const double highHz = std::min(100000., rate * .35);
        for (int i = 0; i < static_cast<int>(rate * .15); ++i) {
            const double t = i / rate;
            const double envelope = t < .1 ? std::sin(kPiD * t / .1) : 0.;
            const double q = chargeBudget * envelope * (.6 * std::sin(2 * kPiD * 23.17 * t)
                + .2 * std::sin(2 * kPiD * 997.31 * t) + .2 * std::sin(2 * kPiD * highHz * t));
            const double small = a.ProcessCharge(q * .01), big = b.ProcessCharge(q);
            const double error = std::abs(big - 100 * small);
            maximumLinearityError = std::max(maximumLinearityError, error);
            maximumPeak = std::max(maximumPeak, std::abs(big));
            maximumNode = std::max(maximumNode, std::abs(ReferenceChargeAmpTestAccess::Node(b)));
            require(error < 2.e-9, "clean charge budget reaches an analog nonlinearity");
            require(std::isfinite(big) && std::abs(big) < parts.outputRailVolts, "clean sizing budget reaches a rail");
        }
        for (int i = 0; i < static_cast<int>(rate * 2); ++i) {
            b.ProcessCharge(0);
            if (i % 64 == 0) b.FlushTiny();
        }
        b.FlushTiny();
        require(b.StateEnergy() == 0 && b.HasNoSubnormalState(), "zero-input circuit state did not drain");
        b.ProcessCharge(chargeBudget);
        b.Reset();
        require(b.StateEnergy() == 0 && b.ProcessCharge(0) == 0, "reset retained capacitor state");
        require(!b.Configure(rate, patch, {std::numeric_limits<double>::infinity()}), "nonfinite components accepted");
    }
    std::cout << "CLEAN charge_budget_C=" << chargeBudget << " max_peak_V=" << maximumPeak
              << " max_input_node_V=" << maximumNode << " max_linearity_error_V=" << maximumLinearityError << '\n';
}

void overload() {
    ReferenceChargeAmp amp;
    ReferenceChargeAmp::Components parts;
    constexpr double rate = 1536000.;
    require(amp.Configure(rate, ReferencePVDFPatch::ForModel(EMarimbaModel::ImperialRosewood), parts), "configure failed");
    double previous = 0, maximum = 0;
    for (int i = 0; i < 10000; ++i) {
        const double q = i < 200 ? ((i % 20) < 10 ? 1.e-3 : -1.e-3) : 0.;
        const double voltage = amp.ProcessCharge(q);
        require(std::isfinite(voltage) && std::isfinite(ReferenceChargeAmpTestAccess::Node(amp)), "nonfinite overloaded circuit");
        require(std::abs(voltage) <= parts.outputRailVolts, "amplifier exceeded supply swing");
        require(std::abs(voltage - previous) <= parts.slewVoltsPerSecond / rate + 1.e-12, "amplifier exceeded slew rate");
        previous = voltage;
        maximum = std::max(maximum, std::abs(voltage));
    }
    require(maximum == parts.outputRailVolts, "overload fixture never exercised the analog rails");
    amp.Reset();
    require(amp.ProcessCharge(0) == 0, "overload reset did not restore silence");
    std::cout << "OVERLOAD bounded_peak_V=" << maximum << '\n';
}
}

int main() {
    std::cout << std::setprecision(12);
    material(); transfer(); cleanAndState(); overload();
    std::cout << "PASS PiezoReferenceCircuitTest checks=" << checks << '\n';
}
