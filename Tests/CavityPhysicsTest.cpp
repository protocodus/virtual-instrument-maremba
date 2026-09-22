// Isolated physical-junction and acoustic ablation checks. This friend access
// exercises invariants that an output-level stability test cannot establish.
#include "MarembaVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void DisableUpperAir(MarembaVoice& voice) { voice.mNumCavityModes = 1; }
    static double CavityHz(const MarembaVoice& voice, int mode) {
        const auto& air = mode == 0 ? voice.mTube : voice.mUpperCavity[mode - 1];
        return air.w * voice.mSampleRate / (2.0 * 3.14159265358979323846);
    }
    static double JunctionError(MarembaVoice& voice, bool residual) {
        auto& bar = voice.mModes[0];
        auto& air = voice.mTube;
        const auto seed = [](MarembaVoice::ModeState& mode, double q, double v, bool old) {
            double& first = old ? mode.o1 : mode.y1;
            double& second = old ? mode.o2 : mode.y2;
            first = q;
            second = (q * mode.cosW + v / mode.invSin) / mode.r;
        };
        seed(bar, .37, -.24, false);
        seed(bar, -.13, .19, true);
        seed(air, .21, .31, false);
        const double initial = MarembaVoice::ModeEnergy(bar) + MarembaVoice::ModeEnergy(air);
        for (int n = 0; n < 4096; ++n) {
            voice.CoupleCavity(air, 0, std::cos(.013), std::sin(.013));
            if (residual) voice.CoupleCavity(air, 0, std::cos(.017), std::sin(.017), true);
        }
        const double after = MarembaVoice::ModeEnergy(bar) + MarembaVoice::ModeEnergy(air);
        return std::abs(after / initial - 1.0);
    }
    static double TwoModeJunctionError(MarembaVoice& voice, bool residual) {
        if (voice.mNumModes < 2 || voice.mNumCavityModes < 2) return 0.0;
        auto& firstBar = voice.mModes[0];
        auto& secondBar = voice.mModes[1];
        auto& air = voice.mUpperCavity[0];
        const auto seed = [](MarembaVoice::ModeState& mode, double q, double v, bool old) {
            double& first = old ? mode.o1 : mode.y1;
            double& second = old ? mode.o2 : mode.y2;
            first = q;
            second = (q * mode.cosW + v / mode.invSin) / mode.r;
        };
        seed(firstBar, .37, -.24, false); seed(firstBar, -.13, .19, true);
        seed(secondBar, -.18, .32, false); seed(secondBar, .21, -.15, true);
        seed(air, .21, .31, false);
        voice.mCavityBarIndex[1][0] = 0; voice.mCavityBarIndex[1][1] = 1;
        voice.mCavityConnections[1] = 2;
        voice.mCavityDirection[1][0] = std::cos(.43);
        voice.mCavityDirection[1][1] = std::sin(.43);
        voice.mCavityCos[1] = std::cos(.017);
        voice.mCavitySin[1] = std::sin(.017);
        const double initial = MarembaVoice::ModeEnergy(firstBar)
            + MarembaVoice::ModeEnergy(secondBar) + MarembaVoice::ModeEnergy(air);
        for (int n = 0; n < 4096; ++n) {
            voice.CoupleCavityAperture(air, 1);
            if (residual) voice.CoupleCavityAperture(air, 1, true);
        }
        const double after = MarembaVoice::ModeEnergy(firstBar)
            + MarembaVoice::ModeEnergy(secondBar) + MarembaVoice::ModeEnergy(air);
        return std::abs(after / initial - 1.0);
    }
    // Far past a taut limit, so the release exercises the stiff one-sided
    // spring and the overshoot-dependent slap loss, not only the slack range.
    static void SeedFreeMembrane(MarembaVoice& voice) {
        voice.mContactSample = voice.mContactSamplesTotal;
        voice.mShockSample = voice.mShockSamplesTotal;
        voice.mArtifactSample = voice.mArtifactEnd;
        voice.mClackActive = false;
        voice.mHasMirliton = false;
        voice.mMembrane.mX = .5;
        voice.mMembrane.mV = -.3;
    }
    static double MembraneEnergy(const MarembaVoice& voice) { return voice.mMembrane.Energy(); }
};
}
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void trigger(maremba::MarembaVoice& voice, int model, int note, double rate, float buzz = 0) {
    maremba::FastPRNG random(123u);
    voice.Trigger(note, .85f, static_cast<maremba::EMarimbaModel>(model), .65f, .25f,
                  0, 1, 1, buzz, 0, 0, rate, random, 0, 0, 2, 0);
}
std::vector<double> render(int model, int note, double rate, float buzz, bool upper = true) {
    maremba::MarembaVoice voice;
    trigger(voice, model, note, rate, buzz);
    if (!upper) maremba::MarembaVoiceTestAccess::DisableUpperAir(voice);
    std::vector<double> result(static_cast<size_t>(.12 * rate));
    for (size_t n = 0; n < result.size(); ++n) {
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
        require(std::isfinite(close), "non-finite pressure/membrane output");
        result[n] = close;
        if ((n & 31u) == 31u) voice.UpdateEnvelope();
    }
    return result;
}
double relativeDifference(const std::vector<double>& x, const std::vector<double>& y) {
    double difference = 0, energy = 0;
    for (size_t n = 0; n < x.size(); ++n) {
        difference += (x[n] - y[n]) * (x[n] - y[n]);
        energy += x[n] * x[n];
    }
    return difference / std::max(energy, 1.e-30);
}
}
int main() {
    double worstJunctionError = 0, worstTwoModeJunctionError = 0;
    for (int model = 0; model < 4; ++model) {
        for (int note : {36, 60, 96}) for (double rate : {44100., 96000., 192000.}) {
            for (bool residual : {false, true}) {
                maremba::MarembaVoice voice;
                trigger(voice, model, note, rate);
                worstJunctionError = std::max(worstJunctionError,
                    maremba::MarembaVoiceTestAccess::JunctionError(voice, residual));
                worstTwoModeJunctionError = std::max(worstTwoModeJunctionError,
                    maremba::MarembaVoiceTestAccess::TwoModeJunctionError(voice, residual));
            }
        }
    }
    require(worstJunctionError < 1.e-8, "pressure/velocity junction changed stored modal energy");
    require(worstTwoModeJunctionError < 1.e-8, "two-mode aperture changed stored modal energy");
    std::cout << "JUNCTION worst_relative_energy_error=" << worstJunctionError
              << " two_mode_error=" << worstTwoModeJunctionError << '\n';

    // The box keeps its dimensions across keys and when tines pitch-bend.
    for (int note : {48, 60, 84}) {
        maremba::MarembaVoice reference, voice;
        trigger(reference, 3, 60, 96000);
        trigger(voice, 3, note, 96000);
        voice.Retune(200);
        for (int mode = 0; mode < 3; ++mode)
            require(std::abs(maremba::MarembaVoiceTestAccess::CavityHz(reference, mode)
                           - maremba::MarembaVoiceTestAccess::CavityHz(voice, mode)) < 1.e-9,
                    "kalimba air cavity incorrectly follows the played key or pitch bend");
    }

    // A pressure-mode layer must affect rendered sound. Compare all layers
    // against an otherwise identical voice with only the primary air mode;
    // register-dependent radiation need not be identical on every key.
    for (int model = 0; model < 4; ++model) {
        double strongestDifference = 0;
        for (int note : {48, 60, 72, 84}) {
            const auto full = render(model, note, 96000, 0);
            const auto oneMode = render(model, note, 96000, 0, false);
            const double difference = relativeDifference(full, oneMode);
            strongestDifference = std::max(strongestDifference, difference);
            std::cout << "AIR_ABLATION model=" << model << " note=" << note
                      << " upper_to_full_db=" << 10 * std::log10(std::max(difference, 1.e-30)) << '\n';
        }
        // A modest attack-colour contribution is intentional; a 0.5% amplitude
        // floor prevents adding computationally present but silent air modes.
        require(strongestDifference > 2.5e-5, "upper cavity modes have no meaningful acoustic contribution");
    }

    // With pressure removed the nonlinear membrane's positive elastic and
    // kinetic energy must decrease on EVERY step, including large excursion.
    for (double rate : {44100., 96000., 192000.}) {
        maremba::MarembaVoice voice;
        trigger(voice, 2, 84, rate, 1);
        maremba::MarembaVoiceTestAccess::SeedFreeMembrane(voice);
        double previous = maremba::MarembaVoiceTestAccess::MembraneEnergy(voice);
        for (int n = 0; n < static_cast<int>(rate * .03); ++n) {
            float close, far, piezo;
            voice.ProcessSample(close, far, piezo);
            const double current = maremba::MarembaVoiceTestAccess::MembraneEnergy(voice);
            require(current <= previous + 1.e-12, "unforced slack/taut membrane injected energy");
            previous = current;
        }
        require(previous < 1.e-12, "unforced membrane did not release naturally");
    }

    // Compare the isolated nonlinear contribution at physical times, allowing
    // contact integration and implicit dispersion to differ slightly by rate.
    double worstMembraneRateError = 0;
    for (int note : {48, 60, 84}) {
        const double reference = relativeDifference(render(2, note, 96000, 0), render(2, note, 96000, 1));
        for (double rate : {88200., 192000.}) {
            const double measured = relativeDifference(render(2, note, rate, 0), render(2, note, rate, 1));
            const double error = std::abs(10 * std::log10(measured / reference));
            worstMembraneRateError = std::max(worstMembraneRateError, error);
        }
    }
    require(worstMembraneRateError < 1.0, "nonlinear membrane level changes substantially with sample rate");
    std::cout << "MEMBRANE worst_rate_error_db=" << worstMembraneRateError << '\n';
    std::cout << "Cavity physics contracts passed.\n";
}
