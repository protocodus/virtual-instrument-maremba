#include "../DSP/PiezoPickup.h"
#include <array>
#include <cmath>
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

} // namespace

int main() {
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
