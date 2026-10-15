// Physical motion -> acoustic pressure, before captures, preamp or protection.
// The emitting face uses a fixed C4 reference; opposite modal lobes contribute
// signed pressure rather than an extra coherent point source. Compile with
// -I DSP and DSP/*.cpp. This is a relative voicing calibration, not absolute SPL.
#include "MarembaVoice.h"
#include "Oversampler.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    struct Pole { double angle, radius, forcePerSample; };
    static Pole IsolateComplexObserver(MarembaVoice& v) {
        // Keep the real contact solver and mechanical modes, but observe only
        // the second bending mode through a known complex pressure field.
        // No output limiter, noisy impact or cavity sound can hide its phase.
        for (auto& gain : v.mRadiationGain) gain = 0;
        v.mRadiationGain[1] = 1;
        v.mRadiation[1].close = {.35, .65};
        for (auto& gain : v.mTorsionClose) gain = 0;
        v.mLateralRadiation = 0;
        for (auto& gain : v.mCavityReadout) gain = 0;
        v.mClackBpB = v.mSlapB = 0;
        const auto& mode = v.mModes[1];
        return {mode.w, mode.r, mode.force / v.mSampleRate};
    }
    static void IsolateUpperSurfaceReadout(MarembaVoice& v) {
        // Observe the signed upper bending surface alone. The fundamental,
        // cavity and transverse fields are unchanged by RestorePointSourceShare
        // and can dominate the full-source peak after the deliberate voicing.
        v.mRadiationGain[0] = 0;
        for (auto& gain : v.mTorsionClose) gain = 0;
        v.mLateralRadiation = 0;
        // ProcessSample reopens mResReadoutGain over 5 ms. Mute the acoustic
        // coefficients themselves so the cavity stays absent from this
        // observer while its mechanical loading remains intact.
        for (auto& gain : v.mCavityReadout) gain = 0;
        v.mClackBpB = v.mSlapB = 0;
    }
    static double Area(const MarembaVoice& v) {
        const auto key = GetBarKey(v.mModel, 69 + 12 * std::log2(v.mF0 / 440), v.mRadiationCarving);
        const auto reference = GetBarKey(v.mModel, 60, .5);
        return key.lengthM * key.widthM / (reference.lengthM * reference.widthM);
    }
    static double WorstAreaError(const MarembaVoice& v) {
        constexpr double pi = 3.14159265358979323846, pressureCalibration = 3.32e-4;
        // Independent declared voicing weights. The geometric pressure and
        // finite-area conversion must still hold to the original precision.
        constexpr double upperBendingWeight = .08, transverseWeight = .15;
        const auto key = GetBarKey(v.mModel, 69 + 12 * std::log2(v.mF0 / 440), v.mRadiationCarving);
        const double calibration = GetModelParams(v.mModel).radiationGain * pressureCalibration;
        const double area = Area(v);
        double error = 0;
        const auto compare = [&](double actual, double unitArea) {
            error = std::max(error, std::abs(actual / unitArea - area));
        };
        for (int m = 0; m < v.mNumModes; ++m) {
            const double hz = v.mRadiationHz[m], omega = 2 * pi * hz;
            compare(v.mRadiationGain[m], calibration * omega * omega * v.mModes[m].toDisplacement
                    * BarDirectRadiationScale(hz, key.widthM) * (m >= 2 ? upperBendingWeight : 1.));
        }
        const double inertia = key.massKg * (key.widthM * key.widthM + key.thicknessM * key.thicknessM) / 24;
        for (int m = 0; m < v.mNumTorsionModes; ++m) {
            const double omega = v.mTorsion[m].omega * v.mSampleRate;
            const double unitArea = transverseWeight * calibration * omega * omega
                * BarDirectRadiationScale(omega / (2 * pi), key.widthM)
                * std::min(1., .5 * omega / 343 * key.widthM) * .5 * key.widthM
                / (MarembaVoice::kStateScale * std::sqrt(std::max(inertia, 1.e-9)));
            compare(v.mTorsionClose[m], unitArea);
            compare(v.mTorsionFar[m], .6 * unitArea);
        }
        if (v.mHasLateral) {
            const double omega = v.mLateral.omega * v.mSampleRate;
            compare(v.mLateralRadiation, transverseWeight * .15 * calibration * omega * omega
                    / (MarembaVoice::kStateScale * std::sqrt(.25 * key.massKg)));
        }
        return error;
    }
    static void RestorePointSourceShare(MarembaVoice& v) {
        const bool padauk = v.mModel == EMarimbaModel::MayanPadauk;
        const double close = padauk ? .18 : .16, far = padauk ? .36 : .34;
        for (int m = 1; m < v.mNumModes; ++m) {
            auto& c = v.mRadiation[m].close;
            auto& f = v.mRadiation[m].far;
            c = {1 - close + close * c.real, close * c.imag};
            f = {1 - far + far * f.real, far * f.imag};
        }
    }
};
}

namespace {
using namespace maremba;
using Access = MarembaVoiceTestAccess;
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
MarembaVoice strike(int model, int note, float carving, int factor) {
    MarembaVoice voice;
    voice.SetSeed(0x12345);
    FastPRNG random(0x87654321);
    StrikeOptions options;
    options.carving = carving;
    voice.Trigger(note, 1, static_cast<EMarimbaModel>(model), 1, .45f, 0, .7f,
                  .7f, 0, 0, 0, 48000 * factor, random, 0, 0, 3, 0, 0, options);
    return voice;
}
void areaCalibration() {
    double worst = 0;
    for (int model = 0; model < 3; ++model) {
        const auto instrument = static_cast<EMarimbaModel>(model);
        const auto reference = GetBarKey(instrument, 60, .5);
        check(BarAcousticAreaScale(instrument, reference.lengthM, reference.widthM) == 1,
              "C4 pressure calibration is not unity");
        for (float carving : {0.f, .5f, 1.f}) for (int note : {21, 36, 48, 60, 84, 108, 127}) {
            const auto voice = strike(model, note, carving, 4);
            const double area = Access::Area(voice);
            if (note < 60) check(area > 1, "bass bar lost its larger radiating face");
            if (note == 60) check(area == 1, "Carving changed the C4 face area");
            if (note > 60) check(area < 1, "treble bar retains C4's emitting area");
            worst = std::max(worst, Access::WorstAreaError(voice));
        }
    }
    check(worst < 1.e-11, "a bending, torsion or lateral readout missed finite-area conversion");
    std::cout << "AREA worst_relative_calibration_error=" << worst << '\n';
}
void hardStrikeCrest() {
    // Identical physical strikes with only the old upper-mode point-source
    // share restored in the comparison. Keep full-source peak/sustain bounds,
    // then measure cancellation on the affected upper surface itself. Neither
    // pass includes output limiting, and physical states must remain identical.
    for (bool upperSurface : {false, true})
    for (int model = 0; model < 3; ++model) for (int factor : {2, 4, 8}) {
        auto actual = strike(model, 60, 1, factor);
        if (upperSurface) Access::IsolateUpperSurfaceReadout(actual);
        auto pointShare = actual;
        Access::RestorePointSourceShare(pointShare);
        MonoOversampler actualDecimator, pointDecimator;
        double peak = 0, oldPeak = 0, ring = 0;
        bool samePhysicalState = true;
        for (int frame = 0; frame < 9600; ++frame) {
            float a[8]{}, b[8]{};
            for (int sub = 0; sub < factor; ++sub) {
                float far, piezoA, piezoB;
                actual.ProcessSample(a[sub], far, piezoA);
                pointShare.ProcessSample(b[sub], far, piezoB);
                samePhysicalState = samePhysicalState && piezoA == piezoB
                    && actual.GetContactForce() == pointShare.GetContactForce();
            }
            const double x = actualDecimator.DownsampleFrame(a, factor);
            const double y = pointDecimator.DownsampleFrame(b, factor);
            if (frame < 1920) { peak = std::max(peak, std::abs(x)); oldPeak = std::max(oldPeak, std::abs(y)); }
            else ring += x * x;
            if ((frame + 1) % 16 == 0) {
                samePhysicalState = samePhysicalState && actual.ComputeEnergy() == pointShare.ComputeEnergy();
                actual.UpdateEnvelope(); pointShare.UpdateEnvelope();
            }
        }
        const double ringRms = std::sqrt(ring / 7680);
        check(samePhysicalState, "acoustic projection changed contact, stored energy or pickup motion");
        check(peak > .1 && peak < 10, "hard C4 source retains excessive acceleration crest or went silent");
        if (upperSurface)
            check(peak < .75 * oldPeak, "signed surface readout failed to remove coherent upper-mode crest");
        // The isolated upper surface measures at least .0245 RMS across
        // these models/rates. Keep a shared .02 floor (under 2 dB margin),
        // independently of the complete source's existing sustain bound.
        check(ringRms > (upperSurface ? .02 : .05), upperSurface
            ? "isolated upper surface lost its physical C4 sustain"
            : "crest correction removed the physical C4 sustain");
        std::cout << "SOURCE upper_surface=" << upperSurface << " model=" << model << " factor=" << factor << " peak=" << peak
                  << " point_share_peak=" << oldPeak << " ring_rms=" << ringRms << '\n';
    }
}

void forcedComplexPhase() {
    // Independently convolve the measured contact force with the analytic
    // damped sinusoidal pressure impulse response. This does not reconstruct
    // quadrature from the implementation's adjacent oscillator states, and
    // catches mixing a post-force displacement with a pre-force quadrature.
    double worstResidual = 0;
    for (double rate : {88200., 96000., 192000.}) {
        MarembaVoice voice;
        voice.SetSeed(0x12345);
        FastPRNG random(0x87654321);
        voice.Trigger(96, .85f, EMarimbaModel::MayanPadauk, .4f, .35f, 0, 0,
                      1, 0, 0, 0, rate, random, 0, 0, 1, 0);
        const auto pole = Access::IsolateComplexObserver(voice);
        const int frames = static_cast<int>(std::ceil(.004 * rate));
        std::vector<double> pressureImpulse(frames), force(frames);
        for (int age = 0; age < frames; ++age) {
            const double phase = (age + 1) * pole.angle;
            pressureImpulse[age] = pole.forcePerSample * std::pow(pole.radius, age)
                / std::sin(pole.angle) * (.35 * std::sin(phase) + .65 * std::cos(phase));
        }
        double signal = 0, error = 0;
        int forcedSamples = 0, freeTail = 0;
        for (int frame = 0; frame < frames; ++frame) {
            float close, far, piezo;
            voice.ProcessSample(close, far, piezo);
            force[frame] = voice.GetContactForce();
            if (force[frame] != 0) { ++forcedSamples; freeTail = 0; } else ++freeTail;
            double expected = 0;
            for (int sample = 0; sample <= frame; ++sample)
                expected += force[sample] * pressureImpulse[frame - sample];
            signal += expected * expected;
            error += (close - expected) * (close - expected);
        }
        check(forcedSamples > 3 && freeTail > 20 && signal > 1.e-15,
              "complex observer fixture did not include both forced contact and free ringing");
        worstResidual = std::max(worstResidual, error / std::max(signal, 1.e-30));
    }
    check(worstResidual < 1.e-12, "forced complex pressure differs from analytic oscillator response");
    std::cout << "FORCED_PHASE worst_normalized_error_energy=" << worstResidual << '\n';
}
}

int main() {
    std::cout << std::setprecision(10);
    areaCalibration();
    hardStrikeCrest();
    forcedComplexPhase();
    std::cout << "AcousticReadoutTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
