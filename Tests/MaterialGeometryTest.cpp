// Whole-voice contracts carried over from the 1.3.0f5 geometry pass (its
// Round5ResponseTest), adapted to the combined 1.4.0b1 voice. Every mode's
// microphone readout is the voice's own mode shape (a carved bar's, or a
// mounted tine's from the yielding-bridge solution) integrated densely here,
// independently of the production quadrature; an antisymmetric mode is barely
// driven by a centred stroke and cancels on the bar's axis; and the
// membranes' stretching material reaches the buzzing tone while Buzz 0 stays
// bit-identical. Material A/Bs change only its two coefficients.
#include "MarembaVoice.h"
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
        return v.mModes[mode].omega * v.mSampleRate / (2 * 3.14159265358979323846);
    }
    static double F0(const MarembaVoice& v) { return v.mF0; }
    static double Contact(const MarembaVoice& v, int mode) { return v.mModes[mode].contact; }
    static ModalRadiationReadout Radiation(const MarembaVoice& v, int mode) { return v.mRadiation[mode]; }
    static void LegacyMaterial(MarembaVoice& v) {
        for (int i = 0; i < v.mNumMembranes; ++i) v.mMembranes[i].SetMaterial(0, 0);
    }
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
MarembaVoice trigger(EMarimbaModel model, int note, float buzz, float position = .35f,
                     float velocity = 1) {
    MarembaVoice voice;
    voice.SetSeed(0x63495218);
    FastPRNG random(0x42537618);
    voice.Trigger(note, velocity, model, .7f, position, 0, .7f, 1, buzz,
                  0, 0, sampleRate, random, 0, 0, 2, 0);
    return voice;
}

// The voice's own spatial shape: the carved bar's, or the mounted tine's.
double shape(EMarimbaModel model, int note, double f0, int mode, double x) {
    if (model == EMarimbaModel::KalimbaArtisan)
        return KalimbaMountedShape(GetKalimbaMountShape(f0), mode, x);
    return BarShape(GetBarKey(model, note), mode, x);
}
ModalRadiationGeometry geometryOf(EMarimbaModel model, int note, double f0) {
    if (model == EMarimbaModel::KalimbaArtisan) return GetModalRadiationGeometry(model, f0);
    const BarKey key = GetBarKey(model, note);
    return GetModalRadiationGeometry(model, f0, key.lengthM, key.widthM);
}

// Midpoint quadrature with 2048 longitudinal samples. Only the two transverse
// sample positions and documented radiation law are shared with production.
Complex denseRadiation(EMarimbaModel model, int note, double f0, int mode, double frequency,
        const ModalRadiationGeometry& geometry, const ModalRadiationPoint& mic) {
    constexpr int points = 2048;
    const double centre = std::sqrt(std::pow(mic.x - .5 * geometry.length, 2)
        + mic.y * mic.y + mic.z * mic.z);
    Complex sum = 0;
    double norm = 0;
    for (int i = 0; i < points; ++i) {
        const double x = (i + .5) / points;
        const double displacement = shape(model, note, f0, mode, x);
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

void commonGeometry() {
    double worstField = 0, worstReadout = 0;
    int checkedFields = 0;
    for (int family = 0; family < 4; ++family) for (int note : {48, 60, 72, 84}) {
        const auto model = static_cast<EMarimbaModel>(family);
        const auto voice = trigger(model, note, 0);
        const double f0 = Access::F0(voice);
        const auto geometry = geometryOf(model, note, f0);
        for (int mode = 0; mode < Access::Count(voice); ++mode) {
            const auto radiation = Access::Radiation(voice, mode);
            const double actualHz = Access::Hz(voice, mode);
            if (mode == 0) {
                check(radiation.close.real == 1 && radiation.far.real == 1
                    && radiation.close.imag == 0 && radiation.far.imag == 0,
                    "fundamental radiation reference changed level/phase");
                continue;
            }
            if (actualHz >= 20000) continue;
            for (bool far : {false, true}) {
                const auto coefficient = far ? radiation.far : radiation.close;
                const double strength = far ? geometry.farStrength : geometry.closeStrength;
                const Complex actual((coefficient.real - 1 + strength) / strength, coefficient.imag / strength);
                const Complex reference = denseRadiation(model, note, f0, mode, actualHz, geometry,
                                                         far ? geometry.far : geometry.close);
                const double error = std::abs(actual - reference);
                worstField = std::max(worstField, error);
                const double readoutError = strength * error;
                worstReadout = std::max(worstReadout, readoutError);
                // Gate the audible coefficient, including the disclosed blend.
                // 32 Gauss points resolve the close microphone's near-field
                // phase through 8 kHz; the bass bars' modes above it (10-15,
                // gone within tens of milliseconds) read up to 4 % off.
                check(std::isfinite(error) && std::abs(actual) <= 1 + 1.e-12,
                      "non-finite/unbounded normalized radiation field");
                check(readoutError < (actualHz < 8000 ? .01 : .04),
                      "voice radiation differs from dense readout by more than .01 (.04 above 8 kHz)");
                ++checkedFields;
            }
        }
    }
    std::cout << "COMMON fields=" << checkedFields << " max_normalized_field_error=" << worstField
              << " max_audible_coefficient_error=" << worstReadout << '\n';
}

void parity() {
    double maxCentreRatio = 0, maxCentreField = 0, maxMirrorError = 0, minOffAxis = 1;
    for (int family = 0; family < 3; ++family) for (int note : {48, 60, 72, 84}) {
        const auto model = static_cast<EMarimbaModel>(family);
        // A player at Variance 0 still lands within about half a millimetre,
        // so a centred stroke drives the antisymmetric mode only slightly.
        const auto centred = trigger(model, note, 0, .5f);
        const auto offCentre = trigger(model, note, 0, .35f);
        const double ratio = std::abs(Access::Contact(centred, 1)) / std::abs(Access::Contact(offCentre, 1));
        maxCentreRatio = std::max(maxCentreRatio, ratio);
        check(std::abs(Access::Contact(offCentre, 1)) > 1.e-3 && ratio < .05,
              "centred/off-centre voice strikes do not respect the antisymmetric mode node");
        const double f0 = Access::F0(centred);
        auto geometry = geometryOf(model, note, f0);
        const ModalRadiationPoint centre{.5 * geometry.length, 0, .15};
        const ModalRadiationPoint left{.21 * geometry.length, .03, .15};
        const ModalRadiationPoint right{.79 * geometry.length, .03, .15};
        double samples[kRadiationNodes];
        for (int i = 0; i < kRadiationNodes; ++i) samples[i] = shape(model, note, f0, 1, kModalRadiationNodes[i].x);
        const double frequency = Access::Hz(centred, 1);
        const auto c = ProjectModalSurface(samples, frequency, geometry, centre);
        const auto l = ProjectModalSurface(samples, frequency, geometry, left);
        const auto r = ProjectModalSurface(samples, frequency, geometry, right);
        maxCentreField = std::max(maxCentreField, std::hypot(c.real, c.imag));
        maxMirrorError = std::max(maxMirrorError, std::hypot(l.real + r.real, l.imag + r.imag));
        minOffAxis = std::min(minOffAxis, std::hypot(l.real, l.imag));
    }
    check(maxCentreField < 1.e-12 && maxMirrorError < 1.e-12,
          "same-geometry radiation violates odd-mode cancellation/polarity");
    check(minOffAxis > .01, "antisymmetric mode remains inaudible to an off-axis microphone");
    std::cout << "PARITY max_centred_drive_ratio=" << maxCentreRatio << " max_centre_field=" << maxCentreField
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
                if ((i & 31) == 31) { material.UpdateEnvelope(); legacy.UpdateEnvelope(); }
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
        // 0.1% rejects numerical-only changes; this gate does not assert
        // perceptual superiority.
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
    std::cout << "MaterialGeometryTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
