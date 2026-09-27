// Independent mechanical pickup contracts: a fixed physical electrode reads
// FEM section-rotation strain, not the old uniform-bar displacement proxy.
#include "MarembaVoice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static int Count(const MarembaVoice& voice) { return voice.mNumModes; }
    static double F0(const MarembaVoice& voice) { return voice.mF0; }
    static double Strain(const MarembaVoice& voice, int mode) { return voice.mStrainGain[mode]; }
    static double SurfaceStrain(const MarembaVoice& voice, int mode) { return voice.mSurfaceStrainGain[mode]; }
    static double Displacement(const MarembaVoice& voice, int mode) { return voice.mModes[mode].toDisplacement; }
    static double Position(const MarembaVoice& voice, int mode) { return voice.mModes[mode].y1; }
};
}

namespace {
using namespace maremba;
using Access = MarembaVoiceTestAccess;
int checks = 0;
void require(bool okay, const char* message) {
    ++checks;
    if (!okay) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

// Analytical slender free-free Euler-Bernoulli bar, normalized to displacement
// +1 at its left end. This is independent of both generated FEM coefficients
// and the production legacy weighting helper. The blank bass bars are slender
// enough that their Timoshenko correction is small (tested below).
double uniformAverageStrain(const BarKey& key, const BarPickupPatch& patch, int mode) {
    constexpr double roots[] = {4.730040744862704, 7.853204624095838, 10.995607838001671};
    const double beta = roots[mode];
    const double sigma = (std::cosh(beta) - std::cos(beta)) / (std::sinh(beta) - std::sin(beta));
    const auto slope = [beta, sigma](double x) {
        const double z = beta * x;
        return 0.5 * beta * (std::sinh(z) - sigma * std::cosh(z)
                            - std::sin(z) - sigma * std::cos(z));
    };
    const double centre = std::clamp(patch.centreFraction * key.lengthM,
                                    patch.lengthM * .5, key.lengthM - patch.lengthM * .5);
    const double a = (centre - patch.lengthM * .5) / key.lengthM;
    const double b = (centre + patch.lengthM * .5) / key.lengthM;
    return -.5 * key.thicknessM * (slope(b) - slope(a)) / (key.lengthM * patch.lengthM);
}

void physicalGeometry() {
    double maxUniformError = 0, maxStrain = 0, smallestFundamental = 1.e30;
    int oppositePolarity = 0;
    for (int family = 0; family < 3; ++family) {
        const auto model = static_cast<EMarimbaModel>(family);
        const auto patch = PiezoPickup::BarElectrode(model);
        require(patch.lengthM > 0 && patch.widthM > 0, "electrode has no physical area");
        for (int note = 0; note < 128; ++note) for (double carving : {0., .5, 1.}) {
            const auto key = GetBarKey(model, note, carving);
            require(patch.lengthM < key.lengthM && patch.widthM < key.widthM,
                    "fixed electrode no longer fits the physical bar");
            for (int mode = 0; mode < kBarModes; ++mode) {
                const double strain = PiezoPickup::BarSurfaceStrain(key, mode);
                require(std::isfinite(strain), "nonfinite physical pickup strain");
                maxStrain = std::max(maxStrain, std::abs(strain));
                if (mode == 0) smallestFundamental = std::min(smallestFundamental, std::abs(strain));
                if (strain * key.pickupStrain[0] < 0) ++oppositePolarity;
            }
        }
        for (int note : {24, 27, 30, 33, 36}) {
            const auto blank = GetBarKey(model, note, 0);
            for (int mode = 0; mode < 3; ++mode) {
                const double expected = uniformAverageStrain(blank, patch, mode);
                const double actual = blank.pickupStrain[mode];
                const double error = std::abs(actual / expected - 1.0);
                maxUniformError = std::max(maxUniformError, error);
                require(actual * expected > 0, "FEM surface strain disagrees with physical electrode polarity");
                require(error < .05, "slender blank strain differs from analytical beam by over 5 percent");
            }
        }
        const auto reference = GetBarKey(model, 60);
        const double legacy = PiezoPickup::ModalStrainWeight(model, 0)
                            * std::pow(.38 / reference.lengthM, 2);
        require(std::abs(PiezoPickup::BarStrainWeight(model, reference, 0) - legacy) < 1.e-13,
                "C4 fundamental sensitivity calibration changed");
        require(std::abs(reference.pickupStrain[0]) > 1.e-4,
                "C4 strain reference is at a node and cannot calibrate the family");
        std::cout << "REFERENCE model=" << family << " electrode_mm=" << patch.lengthM * 1000
                  << 'x' << patch.widthM * 1000 << " strain_per_m=" << reference.pickupStrain[0]
                  << " audio_calibration=" << PiezoPickup::BarStrainCalibration(model) << '\n';
    }
    require(oppositePolarity > 0, "pickup rectified signed strain instead of preserving cancellation");
    std::cout << "GEOMETRY max_blank_analytical_relative_error=" << maxUniformError
              << " max_abs_strain_per_m=" << maxStrain
              << " min_abs_fundamental_strain_per_m=" << smallestFundamental << '\n';
}

void interpolation() {
    double maximumError = 0, maximumCarvingDifference = 0;
    for (int family = 0; family < 3; ++family) {
        const auto model = static_cast<EMarimbaModel>(family);
        for (int anchor = 0; anchor < kBarAnchors - 1; ++anchor) {
            const auto& a = BarAnchors(model)[anchor];
            const auto& b = BarAnchors(model)[anchor + 1];
            const auto& u = BarAnchors(model, false)[anchor];
            const auto& v = BarAnchors(model, false)[anchor + 1];
            for (double fraction : {.125, .5, .875}) for (double carving : {0., .25, .5, 1.}) {
                const auto key = GetBarKey(model, a.midi + fraction * (b.midi - a.midi), carving);
                for (int mode = 0; mode < kBarModes; ++mode) {
                    const double plain = (1 - fraction) * u.pickupStrain[mode] + fraction * v.pickupStrain[mode];
                    const double carved = (1 - fraction) * a.pickupStrain[mode] + fraction * b.pickupStrain[mode];
                    const double expected = (1 - carving) * plain + carving * carved;
                    const double error = std::abs(key.pickupStrain[mode] - expected);
                    maximumError = std::max(maximumError, error);
                    maximumCarvingDifference = std::max(maximumCarvingDifference, std::abs(carved - plain));
                    require(error < 1.e-10, "pickup does not follow the key's note/Carving interpolation");
                }
            }
        }
    }
    require(maximumCarvingDifference > 1., "Carving does not alter the mechanical pickup strain");
    std::cout << "INTERPOLATION max_error=" << maximumError
              << " max_carved_blank_strain_difference=" << maximumCarvingDifference << '\n';
}

void actualVoice() {
    int coefficients = 0;
    double maximumError = 0;
    for (int family = 0; family < 4; ++family) for (int note : {24, 36, 60, 84, 108})
        for (float carving : {0.f, .5f, 1.f}) for (double rate : {44100., 192000.}) {
        const auto model = static_cast<EMarimbaModel>(family);
        MarembaVoice voice;
        FastPRNG random(0x81562);
        StrikeOptions options;
        options.carving = carving;
        voice.Trigger(note, .7f, model, .5f, .35f, 0, .7f, 1, 0, 0, 0, rate,
                      random, 0, 0, 0, 0, 0, options);
        const auto key = GetBarKey(model, 69 + 12 * std::log2(Access::F0(voice) / 440), carving);
        const auto& reference = BarAnchors(model)[12];
        for (int mode = 0; mode < Access::Count(voice); ++mode) {
            // SI displacement -> surface strain, then one documented C4
            // audio calibration. No note-length factor or per-mode voicing.
            const double referenceWeight = PiezoPickup::ModalStrainWeight(model, 0)
                * std::pow(.38 / reference.lengthM, 2);
            const double expected = family == 3 ? 0 : GetModelParams(model).pickupGain
                * 640 * 2.5 * referenceWeight / reference.pickupStrain[0]
                * key.pickupStrain[mode] * Access::Displacement(voice, mode);
            const double actual = Access::Strain(voice, mode);
            const double error = std::abs(actual - expected) / std::max(1., std::abs(expected));
            maximumError = std::max(maximumError, error);
            require(error < 1.e-11, "voice bypasses the generated fixed-electrode strain");
            const double expectedSI = family == 3 ? 0 : key.pickupStrain[mode] * Access::Displacement(voice, mode);
            require(std::abs(Access::SurfaceStrain(voice, mode) - expectedSI) < 1.e-13,
                    "physical circuit receives nominal audio units instead of tensile strain");
            ++coefficients;
        }
    }
    std::cout << "VOICE coefficients=" << coefficients << " max_scaled_error=" << maximumError << '\n';
}

void productionCircuitRoute() {
    int samples = 0;
    double peakVolts = 0, maxError = 0;
    for (int family = 0; family < 3; ++family) for (int note : {36, 60, 96})
        for (int striker : {0, 3, 4}) for (double rate : {44100., 192000.}) {
        const auto model = static_cast<EMarimbaModel>(family);
        MarembaVoice voice;
        FastPRNG random(0x872);
        voice.Trigger(note, 1, model, 1, .35f, 0, .7f, 1, 1, 1, 0,
                      rate, random, 0, 0, striker, 0);
        const auto key = GetBarKey(model, 69 + 12 * std::log2(Access::F0(voice) / 440));
        const auto film = ReferencePVDFPatch::ForModel(model);
        ReferenceChargeAmp reference;
        require(reference.Configure(rate, film, {}), "reference circuit configuration failed");
        for (int i = 0; i < static_cast<int>(rate * .08); ++i) {
            float close = 0, far = 0, piezo = 0;
            voice.ProcessSample(close, far, piezo);
            double strain = 0;
            for (int m = 0; m < Access::Count(voice); ++m)
                strain += Access::Position(voice, m) * (key.pickupStrain[m] * Access::Displacement(voice, m));
            const double volts = reference.ProcessCharge(film.ChargeFromStrain(strain));
            const float expected = static_cast<float>(volts * (PiezoPickup::PostAdcGain(model) / 12));
            const double error = std::abs(static_cast<double>(piezo) - expected);
            maxError = std::max(maxError, error);
            peakVolts = std::max(peakVolts, std::abs(volts));
            require(error < 1.e-6 * std::max(1., std::abs(static_cast<double>(expected))),
                    "production pickup bypasses charge electronics or mixes nominal force/audio into charge");
            require(std::abs(volts) < 12, "hard physical strike exceeds declared converter headroom");
            ++samples;
        }
        voice.FastKill();
        float close = 1, far = 1, piezo = 1;
        voice.ProcessSample(close, far, piezo);
        require(piezo == 0, "killed physical pickup retains output");
    }
    std::cout << "PRODUCTION_ROUTE samples=" << samples << " max_audio_error=" << maxError
              << " peak_charge_amp_volts=" << peakVolts << '\n';
}
}

int main() {
    std::cout << std::setprecision(12);
    physicalGeometry();
    interpolation();
    actualVoice();
    productionCircuitRoute();
    std::cout << "PASS PiezoGeometryTest checks=" << checks << '\n';
}
