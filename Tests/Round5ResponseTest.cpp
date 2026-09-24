// Whole-voice contracts for the common mechanical geometry and material law.
// Dense surface integration and finite-difference curvature are independent of
// the production quadrature/curvature helpers. Material A/Bs change only its
// two coefficients; voice geometry, RNG and every other state are identical.
#include "MarembaVoice.h"
#include "BarModes.h"
#include "KalimbaMountShapes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    static int Count(const MarembaVoice& v) { return v.mNumModes; }
    static double Hz(const MarembaVoice& v, int mode) {
        return v.mModes[mode].w * v.mSampleRate / (2 * 3.14159265358979323846);
    }
    static double Drive(const MarembaVoice& v, int mode) { return v.mModes[mode].b0; }
    static double Strain(const MarembaVoice& v, int mode) { return v.mStrainWeights[mode]; }
    static ModalRadiationReadout Radiation(const MarembaVoice& v, int mode) { return v.mRadiation[mode]; }
    static void LegacyMaterial(MarembaVoice& v) { v.mMembrane.SetMaterial(0, 0); }
};
}

namespace {
using namespace maremba;
using Access = MarembaVoiceTestAccess;
using Complex = std::complex<double>;
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 96000;
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition && failures++ < 20) std::cerr << "FAIL " << message << '\n';
}
double hz(int note) { return 440 * std::exp2((note - 69) / 12.0); }
MarembaVoice trigger(EMarimbaModel model, int note, float buzz, float position = .35f,
                     float velocity = 1) {
    MarembaVoice voice;
    voice.SetSeed(0x63495218);
    FastPRNG random(0x42537618);
    voice.Trigger(note, velocity, model, .7f, position, 0, .7f, 1, buzz,
                  0, 0, sampleRate, random, 0, 0, 2, 0);
    return voice;
}

double mountedRaw(const KalimbaMountedMode& mode, double x) {
    return mode.contactScale * (mode.ep * std::exp(mode.beta * (x - 1))
        + mode.em * std::exp(-mode.beta * x) + mode.cosine * std::cos(mode.beta * x)
        + mode.sine * std::sin(mode.beta * x));
}
double shape(EMarimbaModel model, int note, int mode, double x) {
    if (model != EMarimbaModel::KalimbaArtisan)
        return BarContactWeight(GetBarModeGeometry(model, note), mode, x, 0);
    const auto geometry = GetKalimbaMountShape(hz(note));
    const double a = mountedRaw(kKalimbaMountShapes[geometry.lower].modes[mode], x);
    const double b = mountedRaw(kKalimbaMountShapes[geometry.upper].modes[mode], x);
    return a + geometry.fraction * (b - a);
}

// Midpoint quadrature with 2048 longitudinal samples. Only the two transverse
// sample positions and documented radiation law are shared with production.
Complex denseRadiation(EMarimbaModel model, int note, int mode, double frequency,
        const ModalRadiationGeometry& geometry, const ModalRadiationPoint& mic) {
    constexpr int points = 2048;
    const double centre = std::sqrt(std::pow(mic.x - .5 * geometry.length, 2)
        + mic.y * mic.y + mic.z * mic.z);
    Complex sum = 0;
    double norm = 0;
    for (int i = 0; i < points; ++i) {
        const double x = (i + .5) / points;
        const double displacement = shape(model, note, mode, x);
        for (double side : {-1., 1.}) {
            const double distance = std::sqrt(std::pow(mic.x - x * geometry.length, 2)
                + std::pow(mic.y - side * geometry.width / std::sqrt(12.), 2) + mic.z * mic.z);
            const double weight = displacement / distance;
            sum += weight * std::polar(1., -2 * pi * frequency * (distance - centre) / 343.0);
            norm += std::abs(weight);
        }
    }
    return sum / norm;
}

template<class Function> double slope(Function value, double x) {
    constexpr double h = 1.e-5;
    return (value(x - 2*h) - 8*value(x - h) + 8*value(x + h) - value(x + 2*h)) / (12*h);
}
double denseStrain(EMarimbaModel model, int note, int mode) {
    if (model != EMarimbaModel::KalimbaArtisan) {
        constexpr double position[] = {.24, .29, .20}, width[] = {.10, .13, .09};
        const int index = static_cast<int>(model);
        const double lo = position[index] - .5 * width[index], hi = position[index] + .5 * width[index];
        const auto modal = [&](double x) { return shape(model, note, mode, x); };
        const auto fundamental = [&](double x) { return shape(model, note, 0, x); };
        // Same established electrical voltage reference, applied to an
        // independently finite-differenced new geometry.
        return PiezoPickup::ModalStrainWeight(model, 0) * (slope(modal, hi) - slope(modal, lo))
            / (slope(fundamental, hi) - slope(fundamental, lo));
    }
    const auto geometry = GetKalimbaMountShape(hz(note));
    const auto weighted = [&](double x) {
        const auto& lo = kKalimbaMountShapes[geometry.lower];
        const auto& hi = kKalimbaMountShapes[geometry.upper];
        const double a = lo.strainScale * mountedRaw(lo.modes[mode], x);
        const double b = hi.strainScale * mountedRaw(hi.modes[mode], x);
        return a + geometry.fraction * (b - a);
    };
    return (slope(weighted, .0975) - slope(weighted, .0225)) / .075;
}

void commonGeometry() {
    double worstTuning = 0, worstField = 0, worstReadout = 0, worstStrain = 0;
    double worstFieldHz = 0;
    int worstFieldFamily = -1, worstFieldNote = -1, worstFieldMode = -1;
    int checkedModes = 0, checkedFields = 0;
    for (int family = 0; family < 4; ++family) for (int note : {48, 60, 72, 84}) {
        const auto model = static_cast<EMarimbaModel>(family);
        const bool tine = family == 3;
        const auto voice = trigger(model, note, 0);
        const auto geometry = GetModalRadiationGeometry(model, hz(note));
        const auto variation = GetKeyVariation(model, note);
        for (int mode = 0; mode < Access::Count(voice); ++mode) {
            const double ratio = tine ? GetKalimbaMount(hz(note)).ratio[mode]
                : BarModeRatio(GetBarModeGeometry(model, note), mode);
            const double actualHz = Access::Hz(voice, mode);
            const double tuningError = std::abs(actualHz / (hz(note) * ratio * variation.partial[mode]) - 1);
            worstTuning = std::max(worstTuning, tuningError);
            check(tuningError < 3.e-12, "voice poles do not use the generated geometry's modal ratios");
            const double strain = denseStrain(model, note, mode);
            const double strainError = std::abs(Access::Strain(voice, mode) - strain) / std::max(1., std::abs(strain));
            worstStrain = std::max(worstStrain, strainError);
            check(strainError < 2.e-7, "voice pickup does not read the same geometry's signed curvature");
            ++checkedModes;
            const auto radiation = Access::Radiation(voice, mode);
            const double direct = tine ? 1 : BarDirectRadiationScale(actualHz, geometry.width);
            if (mode == 0) {
                check(radiation.close.real == direct && radiation.far.real == direct
                    && radiation.close.imag == 0 && radiation.far.imag == 0,
                    "fundamental radiation reference changed level/phase");
            } else if (actualHz < 20000) {
                for (bool far : {false, true}) {
                    const auto coefficient = far ? radiation.far : radiation.close;
                    const double strength = far ? geometry.farStrength : geometry.closeStrength;
                    const Complex actual((coefficient.real / direct - 1 + strength) / strength,
                                         coefficient.imag / (direct * strength));
                    const Complex reference = denseRadiation(model, note, mode, actualHz, geometry,
                        far ? geometry.far : geometry.close);
                    const double error = std::abs(actual - reference);
                    if (error > worstField) {
                        worstField = error; worstFieldHz = actualHz; worstFieldFamily = family;
                        worstFieldNote = note; worstFieldMode = mode;
                    }
                    const double readoutError = direct * strength * error;
                    worstReadout = std::max(worstReadout, readoutError);
                    // Gate the actual audible coefficient, including the
                    // disclosed blend and direct radiation. Retain the raw
                    // field discrepancy so weak-mode approximation is visible.
                    check(std::isfinite(error) && std::abs(actual) <= 1 + 1.e-12,
                          "non-finite/unbounded normalized radiation field");
                    check(readoutError < .01, "voice radiation differs from dense readout by more than .01");
                    ++checkedFields;
                }
            }
        }
        std::cout << "GEOMETRY model=" << family << " note=" << note
                  << " overtone_ratio=" << Access::Hz(voice, 1) / Access::Hz(voice, 0)
                  << " pickup_overtone=" << Access::Strain(voice, 1) << '\n';
    }
    std::cout << "COMMON modes=" << checkedModes << " fields=" << checkedFields
              << " max_relative_tuning_error=" << worstTuning
              << " max_normalized_field_error=" << worstField
              << " max_audible_coefficient_error=" << worstReadout
              << " max_relative_strain_error=" << worstStrain << '\n';
    std::cout << "RAW_FIELD_MAX model=" << worstFieldFamily << " note=" << worstFieldNote
              << " mode=" << worstFieldMode << " frequency_hz=" << worstFieldHz << '\n';
}

void parity() {
    double maxCentreDrive = 0, maxCentreField = 0, maxMirrorError = 0, minOffAxis = 1;
    for (int family = 0; family < 3; ++family) for (int note : {48, 60, 72, 84}) {
        const auto model = static_cast<EMarimbaModel>(family);
        const auto centred = trigger(model, note, 0, .5f);
        const auto offCentre = trigger(model, note, 0, .35f);
        auto geometry = GetModalRadiationGeometry(model, hz(note));
        const ModalRadiationPoint centre{.5 * geometry.length, 0, .15};
        const ModalRadiationPoint left{.21 * geometry.length, .03, .15};
        const ModalRadiationPoint right{.79 * geometry.length, .03, .15};
        std::array<double, 32> samples{};
        for (int i = 0; i < 32; ++i) samples[i] = shape(model, note, 1, kModalRadiationNodes[i].x);
        const double frequency = Access::Hz(centred, 1);
        const auto c = ProjectModalSurface(1, frequency, geometry, centre, false, 32, samples.data());
        const auto l = ProjectModalSurface(1, frequency, geometry, left, false, 32, samples.data());
        const auto r = ProjectModalSurface(1, frequency, geometry, right, false, 32, samples.data());
        maxCentreDrive = std::max(maxCentreDrive, std::abs(Access::Drive(centred, 1)));
        maxCentreField = std::max(maxCentreField, std::hypot(c.real, c.imag));
        maxMirrorError = std::max(maxMirrorError, std::hypot(l.real + r.real, l.imag + r.imag));
        minOffAxis = std::min(minOffAxis, std::hypot(l.real, l.imag));
        check(Access::Drive(centred, 1) == 0 && std::abs(Access::Drive(offCentre, 1)) > 1.e-8,
              "centred/off-centre voice strikes do not respect the antisymmetric mode node");
    }
    check(maxCentreField < 1.e-12 && maxMirrorError < 1.e-12,
          "same-geometry radiation violates odd-mode cancellation/polarity");
    check(minOffAxis > .01, "antisymmetric mode remains inaudible to an off-axis microphone");
    std::cout << "PARITY max_centre_drive=" << maxCentreDrive << " max_centre_field=" << maxCentreField
              << " max_mirror_error=" << maxMirrorError << " min_off_axis_field=" << minOffAxis << '\n';
}

// Magnitudes remove phase; normalization removes overall level. Differences
// therefore establish a change in spectral shape, not just gain or delay.
std::vector<double> spectrum(const std::vector<double>& samples) {
    size_t n = 1;
    while (n < samples.size()) n <<= 1;
    std::vector<Complex> bins(n);
    for (size_t i = 0; i < samples.size(); ++i)
        bins[i] = samples[i] * (.5 - .5 * std::cos(2 * pi * i / (samples.size() - 1)));
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(bins[i], bins[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1) {
        const Complex rotation = std::polar(1., -2 * pi / length);
        for (size_t i = 0; i < n; i += length) {
            Complex phase = 1;
            for (size_t j = 0; j < length / 2; ++j) {
                const Complex a = bins[i + j], b = bins[i + j + length / 2] * phase;
                bins[i + j] = a + b;
                bins[i + j + length / 2] = a - b;
                phase *= rotation;
            }
        }
    }
    std::vector<double> magnitudes;
    for (size_t i = 1; i < n / 2 && i * sampleRate / n <= 16000; ++i)
        magnitudes.push_back(std::abs(bins[i]));
    return magnitudes;
}

double differenceAfterGain(const std::vector<double>& a, const std::vector<double>& b) {
    double aa = 0, ab = 0, bb = 0;
    for (size_t i = 0; i < a.size(); ++i) { aa += a[i]*a[i]; ab += a[i]*b[i]; bb += b[i]*b[i]; }
    if (aa == 0 || bb == 0) return 0;
    const double gain = ab / aa;
    double error = 0;
    for (size_t i = 0; i < a.size(); ++i) error += std::pow(b[i] - gain * a[i], 2);
    return std::sqrt(error / bb);
}

void audibleMaterial() {
    for (int family : {1, 2}) {
        const auto model = static_cast<EMarimbaModel>(family);
        double bestToneChange = 0, bestSpectralChange = 0, largestLevelChange = 0;
        for (int note : {48, 60, 72}) for (float buzz : {0.f, 1.f}) {
            auto material = trigger(model, note, buzz);
            auto legacy = material;
            Access::LegacyMaterial(legacy);
            std::vector<double> a, b;
            double aa = 0, bb = 0, maximumDifference = 0;
            for (int i = 0; i < static_cast<int>(sampleRate * .4); ++i) {
                float cl = 0, far = 0, pickup = 0, oldCl = 0, oldFar = 0, oldPickup = 0;
                material.ProcessSample(cl, far, pickup);
                legacy.ProcessSample(oldCl, oldFar, oldPickup);
                check(std::isfinite(cl) && std::isfinite(far) && std::isfinite(pickup)
                    && std::isfinite(oldCl), "non-finite integrated membrane output");
                a.push_back(oldCl); b.push_back(cl);
                aa += double(oldCl) * oldCl; bb += double(cl) * cl;
                maximumDifference = std::max(maximumDifference, double(std::abs(cl - oldCl))
                    + std::abs(far - oldFar) + std::abs(pickup - oldPickup));
                if ((i & 15) == 15) { material.UpdateEnvelope(); legacy.UpdateEnvelope(); }
            }
            if (buzz == 0) {
                check(maximumDifference == 0, "disabled Buzz changed with material coefficients");
                continue;
            }
            check(aa > 1.e-8 && bb > 1.e-8, "material comparison rendered silence");
            const double tone = differenceAfterGain(a, b);
            const double spectral = differenceAfterGain(spectrum(a), spectrum(b));
            const double levelDb = 10 * std::log10(bb / aa);
            bestToneChange = std::max(bestToneChange, tone);
            bestSpectralChange = std::max(bestSpectralChange, spectral);
            largestLevelChange = std::max(largestLevelChange, std::abs(levelDb));
            std::cout << "MATERIAL model=" << family << " note=" << note << " gain_fit_residual=" << tone
                      << " phase_gain_invariant_spectral_residual=" << spectral << " rms_change_db=" << levelDb << '\n';
        }
        // One per-family threshold: individual registers need not snap equally.
        // 0.1% rejects numerical-only changes, while the output gives reviewers
        // the actual scale; this gate does not assert perceptual superiority.
        check(bestToneChange > .001 && bestSpectralChange > .001,
              "nonlinear material does not reach the instrument's actual tone");
        check(largestLevelChange < 6, "material update changes whole-note level excessively");
    }
}
} // namespace

int main() {
    commonGeometry();
    parity();
    audibleMaterial();
    std::cout << "Round5ResponseTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
