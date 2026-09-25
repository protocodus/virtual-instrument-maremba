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
    static int CavityModes(const MarembaVoice& voice) { return voice.mNumCavityModes; }
    static int CavityBar(const MarembaVoice& voice, int cavity) { return voice.mCavityBarIndex[cavity][0]; }
    // Seed a bar mode and an air mode with arbitrary phases and run the
    // pressure/velocity junction alone: the rotation must keep their summed
    // squared amplitudes (energies) exactly.
    static double JunctionError(MarembaVoice& voice, int cavity) {
        if (cavity >= voice.mNumCavityModes) return 0.0;
        auto& air = cavity == 0 ? voice.mTube : voice.mUpperCavity[cavity - 1];
        auto& bar = voice.mModes[voice.mCavityBarIndex[cavity][0]];
        const auto seed = [](MarembaVoice::ModeState& mode, double q, double v) {
            mode.y1 = q;
            mode.y2 = (q * mode.cosW + v / mode.invSin) / mode.r;
        };
        seed(bar, .37, -.24);
        seed(air, .21, .31);
        const double initial = MarembaVoice::ModeEnergy(bar) + MarembaVoice::ModeEnergy(air);
        for (int n = 0; n < 4096; ++n) voice.CoupleCavity(air, voice.mCavityBarIndex[cavity][0], std::cos(.013), std::sin(.013));
        const double after = MarembaVoice::ModeEnergy(bar) + MarembaVoice::ModeEnergy(air);
        return std::abs(after / initial - 1.0);
    }
    // Far past a taut limit, so the release exercises the stiff one-sided
    // spring and the overshoot-dependent slap loss, not only the slack range.
    static void SeedFreeMembrane(MarembaVoice& voice) {
        voice.mContact.Stop();
        voice.mArtifactSample = voice.mArtifactEnd;
        voice.mClickGain = 0.0;
        voice.mHasMirliton = false;
        for (int i = 0; i < voice.mNumMembranes; ++i) voice.mMembranes[i].Reset();
        voice.mMembranes[0].mX = .5;
        voice.mMembranes[0].mV = -.3;
    }
    static double MembraneEnergy(const MarembaVoice& voice) {
        double e = 0.0;
        for (int i = 0; i < voice.mNumMembranes; ++i) e += voice.mMembranes[i].Energy();
        return e;
    }
};
}
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void trigger(maremba::MarembaVoice& voice, int model, int note, double rate, float buzz = 0, float position = .25f) {
    maremba::FastPRNG random(123u);
    voice.Trigger(note, .85f, static_cast<maremba::EMarimbaModel>(model), .65f, position,
                  0, 1, 1, buzz, 0, 0, rate, random, 0, 0, 2, 0);
}
std::vector<double> render(int model, int note, double rate, float buzz, bool upper = true, float position = .25f) {
    maremba::MarembaVoice voice;
    trigger(voice, model, note, rate, buzz, position);
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
    double worstJunctionError = 0;
    for (int model = 0; model < 3; ++model) {
        for (int note : {36, 60, 96}) for (double rate : {88200., 96000., 192000.}) {
            maremba::MarembaVoice voice;
            trigger(voice, model, note, rate);
            for (int cavity = 0; cavity < maremba::MarembaVoiceTestAccess::CavityModes(voice); ++cavity) {
                // Only symmetric bar modes move air through the mouth.
                require(maremba::MarembaVoiceTestAccess::CavityBar(voice, cavity) % 2 == 0,
                        "an air mode is coupled to an antisymmetric bar mode");
                maremba::MarembaVoice copy = voice;
                worstJunctionError = std::max(worstJunctionError, maremba::MarembaVoiceTestAccess::JunctionError(copy, cavity));
            }
        }
    }
    require(worstJunctionError < 1.e-8, "pressure/velocity junction changed stored modal energy");
    std::cout << "JUNCTION worst_relative_energy_error=" << worstJunctionError << '\n';

    // The kalimba's box is one body for the whole instrument (the engine's
    // shared body): no tine carries an air mode of its own that could follow
    // the key or a pitch bend.
    for (int note : {48, 60, 84}) {
        maremba::MarembaVoice voice;
        trigger(voice, 3, note, 96000);
        voice.Retune(200);
        require(maremba::MarembaVoiceTestAccess::CavityModes(voice) == 0,
                "kalimba tine carries a per-key air cavity");
    }

    // The air mode must shape the rendered sound: an otherwise identical
    // voice without any resonator coupling differs audibly on every model.
    // (Struck off the fundamental's node: a balafon key carved to its own
    // overtone may carry that node anywhere from 0.20 to 0.25.)
    for (int model = 0; model < 3; ++model) {
        double weakestDifference = 1.0e30;
        for (int note : {48, 60, 72, 84}) {
            const auto coupled = render(model, note, 96000, 0, true, .35f);
            maremba::MarembaVoice voice;
            maremba::FastPRNG random(123u);
            voice.Trigger(note, .85f, static_cast<maremba::EMarimbaModel>(model), .65f, .35f,
                          0, 0, 1, 0, 0, 0, 96000, random, 0, 0, 2, 0);
            std::vector<double> bare(coupled.size());
            for (size_t n = 0; n < bare.size(); ++n) {
                float close, far, piezo;
                voice.ProcessSample(close, far, piezo);
                bare[n] = close;
                if ((n & 31u) == 31u) voice.UpdateEnvelope();
            }
            const double difference = relativeDifference(coupled, bare);
            weakestDifference = std::min(weakestDifference, difference);
            std::cout << "AIR_ABLATION model=" << model << " note=" << note
                      << " air_to_coupled_db=" << 10 * std::log10(std::max(difference, 1.e-30)) << '\n';
        }
        require(weakestDifference > 2.e-3, "the resonator's air mode has no meaningful acoustic contribution");
    }

    // With pressure removed the nonlinear membrane's positive elastic and
    // kinetic energy must decrease on EVERY step, including large excursion.
    for (double rate : {88200., 96000., 192000.}) {
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
