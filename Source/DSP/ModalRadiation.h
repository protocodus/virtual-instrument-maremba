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
    if (model == EMarimbaModel::KalimbaArtisan) {
        const double length = std::clamp(.061 * std::pow(261.625565 / std::clamp(fundamentalHz, 16.0, 20000.0), .55),
                                         .018, .120);
        return {length, .0045, {.88 * length, .008, .028},
                {.55 * length, .12, .60}, .12, .28};
    }
    const double length = barLength > 0.0 ? barLength : std::clamp(.350 * scale, .115, .720);
    const double width = barWidth > 0.0 ? barWidth : std::clamp(.055 * std::sqrt(scale), .030, .080);
    return {length, width, {.33 * length, .020, .080},
            {.55 * length, .25, .90}, 1.0, 1.0};
}

// Pressure from the moving bar face scales with its emitting area. The
// existing model-specific pressure calibration is the C4 bar reference;
// carrying its area to every key would make small treble bars unrealistically
// strong acceleration sources. Carving changes thickness/shapes, not the
// face's length and width. This relative conversion is not absolute SPL:
// the geometry and microphone positions remain voiced approximations.
inline double BarAcousticAreaScale(EMarimbaModel model, double length, double width) {
    // C4 is an exact generated anchor. Read only its face dimensions rather
    // than interpolating every mode and carving shape again on each strike.
    constexpr int referenceIndex = 12;
    static_assert(kRosewoodBars[referenceIndex].midi == 60.0
               && kHormigoBars[referenceIndex].midi == 60.0
               && kBalafonBars[referenceIndex].midi == 60.0, "C4 bar anchor moved");
    const auto& reference = BarAnchors(model)[referenceIndex];
    return length * width / (reference.lengthM * reference.widthM);
}

// 64-point Gauss-Legendre rule on [0, 1]; the voice samples each mode's shape
// at these abscissae once per strike.
static constexpr int kRadiationNodes = 64;
struct ModalRadiationNode { double x, weight; };
static constexpr ModalRadiationNode kModalRadiationNodes[kRadiationNodes] = {
    {0.00034747913211391479, 0.00089164036084813389},
    {0.0018299416140223901, 0.0020735166302812604},
    {0.0044933142616278565, 0.003252228984489184},
    {0.0083318730576870115, 0.0044233799131819674},
    {0.013336586105044512, 0.0055840697300655381},
    {0.019495600173973116, 0.0067315239483593681},
    {0.026794312570798617, 0.0078630152380123938},
    {0.035215413934030215, 0.0089758578878486681},
    {0.044738931460748588, 0.01006741157676511},
    {0.05534227700244293, 0.011135086904191606},
    {0.06700030092295356, 0.012176351284355466},
    {0.079685351873709787, 0.013188734857527322},
    {0.093367342438601231, 0.01416983630712973},
    {0.10801382052832931, 0.015117328536201213},
    {0.12359004636973403, 0.016028964177425803},
    {0.14005907491419456, 0.016902580918570807},
    {0.15738184347288336, 0.017736106628441179},
    {0.17551726437267134, 0.018527564270120034},
    {0.19442232241380336, 0.019275076589307799},
    {0.214052176898683, 0.019976870566360182},
    {0.23436026799005272, 0.020631281621311774},
    {0.25529842714647355, 0.021236757561826813},
    {0.27681699137326798, 0.021791862264661736},
    {0.29886492101800421, 0.02229527908187829},
    {0.32138992083116591, 0.022745813963709061},
    {0.34433856400489449, 0.023142398290657198},
    {0.36765641889561629, 0.023484091408104996},
    {0.39128817812999644, 0.023770082857415158},
    {0.41517778978800357, 0.023999694298229166},
    {0.43926859035193971, 0.024172381117401449},
    {0.46350343910610048, 0.024287733720751697},
    {0.48782485366828776, 0.024345478504569865},
    {0.51217514633171224, 0.024345478504569865},
    {0.53649656089389952, 0.024287733720751697},
    {0.56073140964806023, 0.024172381117401449},
    {0.58482221021199643, 0.023999694298229166},
    {0.6087118218700035, 0.023770082857415158},
    {0.63234358110438371, 0.023484091408104996},
    {0.65566143599510551, 0.023142398290657198},
    {0.67861007916883409, 0.022745813963709061},
    {0.70113507898199579, 0.02229527908187829},
    {0.72318300862673202, 0.021791862264661736},
    {0.74470157285352645, 0.021236757561826813},
    {0.76563973200994728, 0.020631281621311774},
    {0.78594782310131706, 0.019976870566360182},
    {0.80557767758619669, 0.019275076589307799},
    {0.82448273562732866, 0.018527564270120034},
    {0.84261815652711669, 0.017736106628441179},
    {0.85994092508580544, 0.016902580918570807},
    {0.87640995363026597, 0.016028964177425803},
    {0.89198617947167069, 0.015117328536201213},
    {0.90663265756139877, 0.01416983630712973},
    {0.92031464812629027, 0.013188734857527322},
    {0.93299969907704638, 0.012176351284355466},
    {0.94465772299755701, 0.011135086904191606},
    {0.95526106853925141, 0.01006741157676511},
    {0.96478458606596984, 0.0089758578878486681},
    {0.97320568742920144, 0.0078630152380123938},
    {0.98050439982602688, 0.0067315239483593681},
    {0.98666341389495549, 0.0055840697300655381},
    {0.99166812694231299, 0.0044233799131819674},
    {0.99550668573837209, 0.003252228984489184},
    {0.99817005838597761, 0.0020735166302812604},
    {0.99965252086788614, 0.00089164036084813389},
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
// 64-point Gauss-Legendre quadrature along the length (shape sampled at the
// nodes by the caller), resolving the rapid propagation phase of bass bars'
// upper modes, and two-point Gaussian quadrature across the width. All
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
    // Upper bar modes use the complete signed surface field: retaining an
    // unprojected point-source share would bypass opposite-lobe cancellation
    // and add a coherent acceleration spike on hard strikes. Tines retain
    // their existing small correction; most of their sound leaves through
    // the bridge. Fixed quadrature keeps trigger work bounded.
    if (mode == 0) return {};
    const auto blend = [](ModalRadiationCoefficient field, double strength) {
        strength = std::clamp(strength, 0.0, 1.0);
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
