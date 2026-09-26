#include "../DSP/PiezoPickup.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct Transfer { double real, imag; };

Transfer Measure(double rate, maremba::EMarimbaModel model, double frequency) {
    maremba::PiezoPickup pickup;
    pickup.Configure(rate, model);
    const int count = static_cast<int>(rate * 0.5);
    Transfer result{};
    for (int i = 0; i < count; ++i) {
        const double phase = maremba::kTwoPiD * frequency * i / rate;
        const double y = pickup.Process(0.05 * std::sin(phase), 0.0, 0.0);
        Require(std::isfinite(y), "frequency probe must stay finite");
        if (i >= count / 2) {
            result.real += y * std::sin(phase);
            result.imag += y * std::cos(phase);
        }
    }
    result.real *= 4.0 / (0.05 * count);
    result.imag *= 4.0 / (0.05 * count);
    return result;
}

using Components = maremba::PiezoInputCircuit::Components;

std::complex<double> CircuitResponse(double rate, const Components& parts, double frequency) {
    maremba::PiezoInputCircuit circuit;
    circuit.Configure(rate, parts);
    const int count = static_cast<int>(rate * 0.5);
    std::complex<double> result{};
    for (int i = 0; i < count; ++i) {
        const double phase = maremba::kTwoPiD * frequency * i / rate;
        const double sample = circuit.Process(std::sin(phase));
        Require(std::isfinite(sample), "electrical response must remain finite");
        if (i >= count / 2)
            result += sample * std::complex<double>(std::sin(phase), std::cos(phase));
    }
    return result * (4.0 / count);
}

// Independent nodal admittance: i = s * Cp * Vopen, divided by the complete
// parallel input admittance. Then the resistor-set voltage buffer and output
// RC network. Map the probe frequency through Tustin's continuous-frequency
// correspondence, without reproducing implementation coefficients/states.
std::complex<double> CircuitReference(double rate, const Components& parts, double frequency) {
    const std::complex<double> s(0.0, 2.0 * rate * std::tan(maremba::kPiD * frequency / rate));
    const auto admittance = 1.0 / parts.inputResistance + 1.0 / parts.leakageResistance
        + s * (parts.sensorCapacitance + parts.cableCapacitance + parts.inputCapacitance);
    return s * parts.sensorCapacitance / admittance
        * (1.0 + parts.feedbackResistance / parts.gainResistance)
        / (1.0 + s * parts.bandwidthResistance * parts.bandwidthCapacitance);
}

void TestCircuit() {
    constexpr double rates[] = {8000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000};
    constexpr double frequencies[] = {20, 100, 440, 3000, 12000};
    double maxError = 0.0;
    int comparisons = 0;
    for (int model = 0; model < 4; ++model) {
        const auto parts = maremba::PiezoInputCircuit::ForModel(static_cast<maremba::EMarimbaModel>(model));
        const double compensation = parts.sensorCapacitance
            / (parts.sensorCapacitance + parts.cableCapacitance + parts.inputCapacitance)
            * (1.0 + parts.feedbackResistance / parts.gainResistance);
        Require(std::abs(compensation - 1.0) < 0.01,
                "buffer compensation must preserve normal playing level");
        for (double rate : rates) {
            for (double hz : frequencies) {
                if (hz >= rate * 0.45) continue;
                const auto expected = CircuitReference(rate, parts, hz);
                const double error = std::abs(CircuitResponse(rate, parts, hz) - expected) / std::abs(expected);
                maxError = std::max(maxError, error);
                ++comparisons;
                Require(error < 2.0e-7, "electrical response must match independent nodal circuit solution");
            }
            maremba::PiezoInputCircuit small, large;
            small.Configure(rate, parts);
            large.Configure(rate, parts);
            for (int i = 0; i < static_cast<int>(rate * 0.3); ++i) {
                const double input = i < rate * 0.05 ? 0.2 : 0.0;
                const double a = small.Process(input), b = large.Process(input * 10.0);
                Require(std::abs(b - a * 10.0) < 1.0e-12,
                        "clean pickup electronics must not add level-dependent distortion");
                if (i % 64 == 0) { small.FlushTiny(); large.FlushTiny(); }
            }
            small.FlushTiny();
            Require(small.StateEnergy() == 0.0 && small.HasNoSubnormalState(),
                    "electrical charge tails must drain and flush at every supported rate");
            double dc = 0.0;
            for (int i = 0; i < static_cast<int>(rate * 0.25); ++i) dc = small.Process(0.1);
            Require(std::abs(dc) < 1.0e-10, "electrical circuit must reject sustained DC at every rate");
            small.Reset();
            Require(small.StateEnergy() == 0.0 && small.Process(0.0) == 0.0,
                    "electrical reset must clear every state");
        }
        auto loaded = parts;
        loaded.inputResistance *= 0.1;
        Require(std::abs(CircuitResponse(96000, loaded, 100))
                    < std::abs(CircuitResponse(96000, parts, 100)) * 0.3,
                "lower input impedance must remove low-frequency piezo voltage");
        auto longCable = parts;
        longCable.cableCapacitance *= 10.0;
        Require(std::abs(CircuitResponse(96000, longCable, 3000))
                    < std::abs(CircuitResponse(96000, parts, 3000)) * 0.65,
                "additional cable capacitance must load voltage-mode pickup sensitivity");
    }
    std::printf("circuit_analytic_comparisons=%d maximum_relative_error=%.9g\n", comparisons, maxError);
}

} // namespace

int main() {
    TestCircuit();
    constexpr double rates[] = {44100, 48000, 88200, 96000, 192000, 384000, 768000, 1536000};
    constexpr double frequencies[] = {100, 440, 1400, 3000};
    std::array<Transfer, 4> profiles{};
    for (int index = 0; index < 4; ++index) {
        const auto model = static_cast<maremba::EMarimbaModel>(index);
        double maxRateError = 0.0;
        for (double hz : frequencies) {
            const Transfer reference = Measure(96000.0, model, hz);
            const double referenceMagnitude = std::hypot(reference.real, reference.imag);
            for (double rate : rates) {
                const Transfer value = Measure(rate, model, hz);
                const double error = std::hypot(value.real - reference.real, value.imag - reference.imag)
                                   / referenceMagnitude;
                maxRateError = std::max(maxRateError, error);
            }
        }
        Require(maxRateError < 0.065, "contact response must retain gain and phase across sample rates");
        profiles[index] = Measure(96000.0, model, 1400.0);

        maremba::PiezoPickup pickup;
        pickup.Configure(96000.0, model);
        double contactEnergy = 0.0;
        for (int i = 0; i < 96000; ++i) {
            // Same force density for every rate; no displacement/air input.
            const double force = i < 96 ? 0.2 * std::sin(maremba::kPiD * i / 96.0) : 0.0;
            const double sample = pickup.Process(0.0, force, 0.0);
            Require(std::isfinite(sample), "force-only contact path must remain finite");
            contactEnergy += sample * sample;
            if (i % 64 == 0) pickup.FlushTiny();
        }
        Require(contactEnergy > 0.005, "contact pickup must hear a physical knock independently of air sound");
        Require(pickup.StateEnergy() == 0.0 && pickup.HasNoSubnormalState(), "short mounting tails must clear without subnormal state");

        // A piezo cannot sustain voltage from a static force under finite load.
        double dc = 0.0;
        for (int i = 0; i < 96000; ++i) dc = pickup.Process(0.1, 0.2, 0.1);
        Require(std::abs(dc) < 1.0e-10, "finite electrical load must reject static strain");
        pickup.Reset();
        Require(pickup.StateEnergy() == 0.0 && pickup.Process(0.0, 0.0, 0.0) == 0.0, "reset must remove every electrical and mechanical state");

        // A shared modal motion must produce a different spectrum at an air
        // microphone and at a strain-sensitive mounting patch, even with no
        // noise, excitation click, distortion or resonator-cavity contribution.
        constexpr double motionAmplitude[4] = {0.05, 0.014, 0.0035, 0.0012};
        constexpr double modeRatio[4] = {1.0, 4.0, 10.0, 16.0};
        double closeEnergy = 0.0, piezoEnergy = 0.0, crossEnergy = 0.0;
        for (int i = 0; i < 19200; ++i) {
            const double t = static_cast<double>(i) / 96000.0;
            double close = 0.0, strain = 0.0;
            for (int mode = 0; mode < 4; ++mode) {
                const double motion = motionAmplitude[mode] * std::exp(-t * (3.0 + mode * 1.4))
                    * std::sin(maremba::kTwoPiD * 196.0 * modeRatio[mode] * t);
                close += motion;
                strain += motion * maremba::PiezoPickup::ModalStrainWeight(model, mode);
            }
            const double piezo = pickup.Process(strain, 0.0, 0.0);
            closeEnergy += close * close;
            piezoEnergy += piezo * piezo;
            crossEnergy += close * piezo;
        }
        const double correlation = crossEnergy / std::sqrt(closeEnergy * piezoEnergy);
        Require(std::abs(correlation) < 0.90, "noiseless modal contact output must be distinct from close microphone");
        Require(piezoEnergy > closeEnergy * 0.08 && piezoEnergy < closeEnergy * 12.0,
                "mounting model must retain playable level without excessive gain");
        std::printf("model=%d max_rate_error=%.5f contact_energy=%.6f close_correlation=%.5f relative_rms=%.5f\n",
                    index, maxRateError, contactEnergy, correlation, std::sqrt(piezoEnergy / closeEnergy));
    }
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            Require(std::hypot(profiles[a].real - profiles[b].real, profiles[a].imag - profiles[b].imag) > 0.08,
                    "instrument mounting profiles must have distinct transfer functions");
        }
    }
    std::puts("Piezo pickup physical response contracts passed.");
}
