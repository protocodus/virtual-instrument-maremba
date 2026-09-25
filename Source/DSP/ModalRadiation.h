#pragma once

#include "MarimbaModel.h"
#include "ModalExcitation.h"
#include <algorithm>
#include <cmath>

namespace maremba {

struct ModalRadiationCoefficient {
    double real = 1.0;
    double imag = 0.0;

    // For y=A*cos(phase), quadrature=A*sin(phase). The spatial transfer
    // uses exp(-i*k*distance), hence Re{H*(y+i*quadrature)} below.
    double Read(double y, double quadrature) const {
        return real * y - imag * quadrature;
    }
};

struct ModalRadiationPoint {
    double x, y, z; // metres; x=0 at bar's left end or tine's bridge
};

struct ModalRadiationGeometry {
    double length, width;
    ModalRadiationPoint close, far;
    double closeStrength, farStrength;
};

struct ModalRadiationReadout {
    ModalRadiationCoefficient close, far;
};

// Dimensions and microphone positions are deliberately voiced approximations,
// not measurements of four named specimens. L~1/sqrt(f0) is the uniform-beam
// scaling at fixed thickness; actual undercuts/tine supports modify that law.
// Rosewood's mid-register scale is consistent with the 0.373--0.423 m F#3 bars
// measured by Beaton & Scavone (ISMA 2019). Other families are not calibrated.
// Bars take their length and width from the generated carved-bar geometry.
inline ModalRadiationGeometry GetModalRadiationGeometry(EMarimbaModel model, double fundamentalHz,
                                                        double barLength = 0.0, double barWidth = 0.0) {
    const double scale = std::sqrt(261.625565 / std::clamp(fundamentalHz, 16.0, 20000.0));
    double closeStrength = .16, farStrength = .34;
    if (model == EMarimbaModel::MayanPadauk) {
        closeStrength = .18; farStrength = .36;
    } else if (model == EMarimbaModel::KalimbaArtisan) {
        const double length = std::clamp(.061 * std::pow(261.625565 / std::clamp(fundamentalHz, 16.0, 20000.0), .55),
                                         .018, .120);
        return {length, .0045, {.88 * length, .008, .028},
                {.55 * length, .12, .60}, .12, .28};
    }
    const double length = barLength > 0.0 ? barLength : std::clamp(.350 * scale, .115, .720);
    const double width = barWidth > 0.0 ? barWidth : std::clamp(.055 * std::sqrt(scale), .030, .080);
    return {length, width, {.33 * length, .020, .080},
            {.55 * length, .25, .90}, closeStrength, farStrength};
}

// 32-point Gauss-Legendre rule on [0, 1]; the voice samples each mode's shape
// at these abscissae once per strike.
static constexpr int kRadiationNodes = 32;
struct ModalRadiationNode { double x, weight; };
static constexpr ModalRadiationNode kModalRadiationNodes[kRadiationNodes] = {
    {0.0013680690752592151, 0.0035093050047345703}, {0.0071942442273658647, 0.0081371973654531422},
    {0.017618872206246805, 0.012696032654630965}, {0.032546962031130167, 0.017136931456510782},
    {0.051839422116973954, 0.021417949011113449}, {0.075316193133715015, 0.025499029631188132},
    {0.10275810201602881, 0.02934204673926781}, {0.13390894062985514, 0.032911111388180848},
    {0.16847786653489238, 0.036172897054424218}, {0.20614212137961885, 0.039096947893535135},
    {0.24655004553388532, 0.04165596211347343}, {0.28932436193468236, 0.043826046502201947},
    {0.33406569885893617, 0.045586939347881959}, {0.38035631887393145, 0.046922199540402242},
    {0.42776401920860174, 0.047819360039637424}, {0.47584616715613082, 0.048270044257363927},
    {0.52415383284386918, 0.048270044257363927}, {0.57223598079139826, 0.047819360039637424},
    {0.61964368112606849, 0.046922199540402242}, {0.66593430114106389, 0.045586939347881959},
    {0.71067563806531764, 0.043826046502201947}, {0.75344995446611462, 0.04165596211347343},
    {0.79385787862038115, 0.039096947893535135}, {0.83152213346510762, 0.036172897054424218},
    {0.86609105937014486, 0.032911111388180848}, {0.89724189798397114, 0.02934204673926781},
    {0.92468380686628504, 0.025499029631188132}, {0.94816057788302599, 0.021417949011113449},
    {0.96745303796886983, 0.017136931456510782}, {0.98238112779375319, 0.012696032654630965},
    {0.99280575577263419, 0.0081371973654531422}, {0.99863193092474078, 0.0035093050047345703},
};

// Bounded directional coherence of signed surface motion. This is the spatial
// part of a top-face Rayleigh/point-source approximation, NOT an absolute SPL
// predictor, a full unbaffled radiation model, or an added damping term:
//     H = sum(phi_j * exp(-i*k*(R_j-R_c)) / R_j)
//         / sum(abs(phi_j) / R_j).
// The denominator removes level/area calibration and guarantees |H|<=1.
// Opposite lobes cancel; propagation phase and distance weighting retain a
// different response for each mode and microphone. Common flight time is
// removed, so this readout introduces no device latency. Finite arrival fronts,
// edge diffraction, the underside, and frame scattering are not simulated.
//
// 32-point Gauss-Legendre quadrature along the length (shape sampled at the
// nodes by the caller) and two-point Gaussian quadrature across the width. All
// of this runs at note-on, never in ProcessSample.
// The path from every quadrature point to one microphone: the distance
// weighting and the path difference to the bar's centre. It depends on the
// geometry only, so a strike computes it once for all of its modes.
struct ModalRadiationPaths {
    double weight[kRadiationNodes][2];   // quadrature weight / distance, per side
    double delta[kRadiationNodes][2];    // distance - centre distance (m)
};

inline ModalRadiationPaths PrepareModalRadiationPaths(const ModalRadiationGeometry& geometry,
                                                      const ModalRadiationPoint& microphone) {
    ModalRadiationPaths paths{};
    const double length = std::max(geometry.length, .001);
    const double width = std::max(geometry.width, .0001);
    const double z = std::max(std::abs(microphone.z), .002);
    const double dxCentre = microphone.x - .5 * length;
    const double centreDistance = std::sqrt(dxCentre * dxCentre + microphone.y * microphone.y + z * z);
    for (int point = 0; point < kRadiationNodes; ++point) {
        const double dx = microphone.x - kModalRadiationNodes[point].x * length;
        for (int side = 0; side < 2; ++side) {
            const double dy = microphone.y - (side ? 1.0 : -1.0) * width * .288675134594813;
            const double distance = std::sqrt(dx * dx + dy * dy + z * z);
            paths.weight[point][side] = kModalRadiationNodes[point].weight / distance;
            paths.delta[point][side] = distance - centreDistance;
        }
    }
    return paths;
}

inline ModalRadiationCoefficient ProjectModalSurface(const double* shapeAtNodes, double frequencyHz,
                                                     const ModalRadiationPaths& paths) {
    const double k = 2.0 * 3.14159265358979323846 * std::max(frequencyHz, 0.0) / 343.0;
    // The phases first (independent, so they vectorize), then the sums, one
    // chain per side of the bar.
    double sine[kRadiationNodes][2], cosine[kRadiationNodes][2];
    for (int point = 0; point < kRadiationNodes; ++point)
        for (int side = 0; side < 2; ++side)
            FastSinCos(k * paths.delta[point][side], sine[point][side], cosine[point][side]);
    double sumReal[2] = {}, sumImag[2] = {}, sumWeight[2] = {};
    for (int point = 0; point < kRadiationNodes; ++point) {
        const double shape = shapeAtNodes[point];
        for (int side = 0; side < 2; ++side) {
            const double weight = shape * paths.weight[point][side];
            sumReal[side] += weight * cosine[point][side];
            sumImag[side] -= weight * sine[point][side];
            sumWeight[side] += std::abs(weight);
        }
    }
    const double normalization = sumWeight[0] + sumWeight[1];
    if (normalization < 1.e-20) return {0.0, 0.0};
    double real = (sumReal[0] + sumReal[1]) / normalization;
    double imag = (sumImag[0] + sumImag[1]) / normalization;
    // Only guards floating-point roundoff; the triangle inequality sets the bound.
    const double magnitude = std::sqrt(real * real + imag * imag);
    if (magnitude > 1.0) { real /= magnitude; imag /= magnitude; }
    return {real, imag};
}

inline ModalRadiationCoefficient ProjectModalSurface(const double* shapeAtNodes, double frequencyHz,
        const ModalRadiationGeometry& geometry, const ModalRadiationPoint& microphone) {
    return ProjectModalSurface(shapeAtNodes, frequencyHz, PrepareModalRadiationPaths(geometry, microphone));
}

// A bare bar is an acoustically short-circuited source: air pushed by one face
// flows around the edges to the other. Below k*d~1 the pair radiates like a
// dipole, ~k*d weaker than a monopole of the same volume velocity; well above
// it the faces radiate almost independently. d is the effective edge path, a
// voiced multiple of the bar width. Normalized to unity for C4's tuned 4:1
// partial, so the voiced upper-mode balance near 1 kHz is retained while bass
// fundamentals lose the direct radiation that the resonator must supply.
// Relative amplitude only; not an absolute SPL or radiation-damping model.
inline double BarDirectRadiationScale(double frequencyHz, double width) {
    constexpr double kEdgePath = 1.6;
    const auto efficiency = [](double frequency, double barWidth) {
        const double kd = 2.0 * 3.14159265358979323846 * std::max(frequency, 0.0) / 343.0
                        * kEdgePath * barWidth;
        return kd / std::sqrt(1.0 + kd * kd);
    };
    return efficiency(frequencyHz, width) / efficiency(1046.502261, .055);
}

// Both microphones' paths for one strike.
struct ModalRadiationSetup {
    ModalRadiationPaths close, far;
    double closeStrength, farStrength;
};

inline ModalRadiationSetup PrepareModalRadiation(const ModalRadiationGeometry& geometry) {
    return {PrepareModalRadiationPaths(geometry, geometry.close), PrepareModalRadiationPaths(geometry, geometry.far),
            geometry.closeStrength, geometry.farStrength};
}

inline ModalRadiationReadout ComputeModalRadiation(int mode, double frequencyHz,
        const ModalRadiationSetup& setup, const double* shapeAtNodes) {
    // The fundamental's direct field is carried by the dipole efficiency;
    // upper modes blend a bounded spatial correction (lobe cancellation and
    // path differences to each microphone). Fixed quadrature keeps trigger
    // work bounded.
    if (mode == 0) return {};
    const auto blend = [](ModalRadiationCoefficient field, double strength) {
        strength = std::clamp(strength, 0.0, .4);
        return ModalRadiationCoefficient{1.0 - strength + strength * field.real,
                                         strength * field.imag};
    };
    return {blend(ProjectModalSurface(shapeAtNodes, frequencyHz, setup.close), setup.closeStrength),
            blend(ProjectModalSurface(shapeAtNodes, frequencyHz, setup.far), setup.farStrength)};
}

inline ModalRadiationReadout ComputeModalRadiation(int mode, double frequencyHz,
        const ModalRadiationGeometry& geometry, const double* shapeAtNodes) {
    if (mode == 0) return {};
    return ComputeModalRadiation(mode, frequencyHz, PrepareModalRadiation(geometry), shapeAtNodes);
}

} // namespace maremba
