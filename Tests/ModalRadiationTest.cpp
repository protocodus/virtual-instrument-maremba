// Geometrical radiation contracts independent of the voice's excitation and mix.
#include "ModalRadiation.h"
#include "ModalExcitation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
double magnitude(maremba::ModalRadiationCoefficient c) {
    return std::sqrt(c.real * c.real + c.imag * c.imag);
}
double difference(maremba::ModalRadiationCoefficient a, maremba::ModalRadiationCoefficient b) {
    return magnitude({a.real - b.real, a.imag - b.imag});
}
template <typename Shape>
std::array<double, maremba::kRadiationNodes> sample(Shape&& shape) {
    std::array<double, maremba::kRadiationNodes> values{};
    for (int i = 0; i < maremba::kRadiationNodes; ++i) values[i] = shape(maremba::kModalRadiationNodes[i].x);
    return values;
}
// The same top-face projection integrated densely (midpoint rule), as the
// reference for the voice's 64-point quadrature.
template <typename Shape>
maremba::ModalRadiationCoefficient dense(Shape&& shape, double hz, const maremba::ModalRadiationGeometry& g,
                                         const maremba::ModalRadiationPoint& mic, int points = 4096) {
    const double length = std::max(g.length, .001), width = std::max(g.width, .0001);
    const double z = std::max(std::abs(mic.z), .002);
    const double dxc = mic.x - .5 * length;
    const double centre = std::sqrt(dxc * dxc + mic.y * mic.y + z * z);
    const double k = 2.0 * 3.14159265358979323846 * hz / 343.0;
    double re = 0.0, im = 0.0, norm = 0.0;
    for (int p = 0; p < points; ++p) {
        const double x = (p + .5) / points;
        const double s = shape(x);
        const double dx = mic.x - x * length;
        for (int side : {-1, 1}) {
            const double dy = mic.y - side * width * .288675134594813;
            const double d = std::sqrt(dx * dx + dy * dy + z * z);
            const double w = s / (points * d);
            re += w * std::cos(k * (d - centre));
            im -= w * std::sin(k * (d - centre));
            norm += std::abs(w);
        }
    }
    return {re / norm, im / norm};
}
}

int main() {
    using namespace maremba;
    // The quadrature itself: weights sum to one and integrate x^2 exactly.
    double weights = 0.0, second = 0.0;
    for (const auto& node : kModalRadiationNodes) { weights += node.weight; second += node.weight * node.x * node.x; }
    require(std::abs(weights - 1.0) < 1.e-12 && std::abs(second - 1.0 / 3.0) < 1.e-12,
            "radiation quadrature does not integrate low polynomials exactly");

    double worstQuadratureError = 0.0, smallestBalanceChange = 1.0;
    for (int instrument = 0; instrument < 4; ++instrument) {
        const auto model = static_cast<EMarimbaModel>(instrument);
        const bool tine = instrument == 3;
        double balanceChange = 0.0;
        for (double fundamental : {65.406391, 261.625565, 1046.50226, 4186.00904}) {
            const double midi = 69.0 + 12.0 * std::log2(fundamental / 440.0);
            const BarKey key = GetBarKey(model, midi);
            const auto geometry = tine ? GetModalRadiationGeometry(model, fundamental)
                                       : GetModalRadiationGeometry(model, fundamental, key.lengthM, key.widthM);
            const auto rootShapes = sample([&](double x) { return tine ? TineShape(0, x) : BarShape(key, 0, x); });
            const auto root = ComputeModalRadiation(0, fundamental, geometry, rootShapes.data());
            require(root.close.real == 1.0 && root.close.imag == 0.0
                 && root.far.real == 1.0 && root.far.imag == 0.0,
                    "fundamental radiation reference changed existing pitch level/phase");
            for (int mode = 1; mode < 8; ++mode) {
                const double hz = fundamental * (tine ? KalimbaModeRatio(mode) : key.ratio[mode]);
                const auto shape = [&](double x) { return tine ? TineShape(mode, x) : BarShape(key, mode, x); };
                const auto shapes = sample(shape);
                const auto readout = ComputeModalRadiation(mode, hz, geometry, shapes.data());
                for (const auto coefficient : {readout.close, readout.far}) {
                    require(std::isfinite(coefficient.real) && std::isfinite(coefficient.imag),
                            "radiation coefficient is not finite");
                    require(magnitude(coefficient) <= 1.0 + 1.e-12,
                            "spatial projection amplified an upper mode");
                    if (tine) require(magnitude(coefficient) >= .2 - 1.e-12,
                                      "the existing tine correction changed");
                }
                const auto closeField = ProjectModalSurface(shapes.data(), hz, geometry, geometry.close);
                const auto farField = ProjectModalSurface(shapes.data(), hz, geometry, geometry.far);
                if (!tine) {
                    require(difference(readout.close, closeField) < 1.e-12
                         && difference(readout.far, farField) < 1.e-12,
                            "bar readout retained an unprojected coherent pressure component");
                } else {
                    require(difference(readout.close, {.88 + .12 * closeField.real, .12 * closeField.imag}) < 1.e-12
                         && difference(readout.far, {.72 + .28 * farField.real, .28 * farField.imag}) < 1.e-12,
                            "bar radiation change altered the tine's existing readout");
                }
                if (hz < 20000.0) {
                    balanceChange = std::max(balanceChange, std::abs(magnitude(readout.close) - magnitude(readout.far)));
                    for (const auto microphone : {geometry.close, geometry.far}) {
                        const auto tested = ProjectModalSurface(shapes.data(), hz, geometry, microphone);
                        const double error = difference(tested, dense(shape, hz, geometry, microphone));
                        if (error > worstQuadratureError && std::getenv("RADIATION_VERBOSE"))
                            std::cout << "  worst so far " << error << " instrument " << instrument << " f0 " << fundamental
                                      << " mode " << mode << " hz " << hz << " z " << microphone.z << '\n';
                        worstQuadratureError = std::max(worstQuadratureError, error);
                    }
                }
            }
        }
        require(balanceChange > .04, "close/far differ only by a negligible scalar across modes");
        smallestBalanceChange = std::min(smallestBalanceChange, balanceChange);
    }
    // Worst case: the 8 cm close microphone over a C4 bar's 5 kHz mode, where
    // the phase turns ~3 times along the bar; 0.025 of |H| is < 0.25 dB.
    require(worstQuadratureError < .025, "surface quadrature disagrees with dense spatial integration");

    auto geometry = GetModalRadiationGeometry(EMarimbaModel::ImperialRosewood, 261.625565);
    const auto odd = sample([](double x) { return UniformBarShape(1, x); });
    const auto even = sample([](double x) { return UniformBarShape(2, x); });
    const ModalRadiationPoint centre{geometry.length * .5, .0, .15};
    const ModalRadiationPoint left{geometry.length * .21, .03, .15};
    const ModalRadiationPoint right{geometry.length * .79, .03, .15};
    const auto centreOdd = ProjectModalSurface(odd.data(), 1046.50226, geometry, centre);
    const auto leftOdd = ProjectModalSurface(odd.data(), 1046.50226, geometry, left);
    const auto rightOdd = ProjectModalSurface(odd.data(), 1046.50226, geometry, right);
    require(magnitude(centreOdd) < 1.e-12, "symmetric observation failed to cancel an antisymmetric bending mode");
    auto centredGeometry = geometry;
    centredGeometry.close = centredGeometry.far = centre;
    const auto centredReadout = ComputeModalRadiation(1, 1046.50226, centredGeometry, odd.data());
    require(magnitude(centredReadout.close) < 1.e-12 && magnitude(centredReadout.far) < 1.e-12,
            "opposite lobes cancel geometrically but the audible readout retains a point source");
    require(magnitude(leftOdd) > .03, "off-axis observation did not reveal the antisymmetric mode");
    require(magnitude({leftOdd.real + rightOdd.real, leftOdd.imag + rightOdd.imag}) < 1.e-12,
            "mirrored microphone did not reverse antisymmetric spatial radiation");

    // Isolate the microphone distance while retaining the same modal shape.
    const ModalRadiationPoint near{geometry.length * .33, .02, .06};
    const ModalRadiationPoint distant{geometry.length * .33, .02, 1.2};
    const auto nearOdd = ProjectModalSurface(odd.data(), 1046.50226, geometry, near);
    const auto distantOdd = ProjectModalSurface(odd.data(), 1046.50226, geometry, distant);
    const auto nearEven = ProjectModalSurface(even.data(), 2616.25565, geometry, near);
    const auto distantEven = ProjectModalSurface(even.data(), 2616.25565, geometry, distant);
    require(difference(nearOdd, distantOdd) > .03 && difference(nearEven, distantEven) > .03,
            "distance failed to change modal surface cancellation");
    require(std::abs(magnitude(nearOdd) / magnitude(nearEven)
                   - magnitude(distantOdd) / magnitude(distantEven)) > .05,
            "distance changed all modes by only a common gain");

    // This transfer has no sample-rate argument: rendering the same physical
    // phase at different rates must give the same pressure projection. Include
    // the recurrence's quadrature reconstruction used by MarembaVoice.
    const auto coefficient = ComputeModalRadiation(2, 2616.25565, geometry, even.data()).close;
    double worstRateError = 0.0;
    for (double sampleRate : {44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        const double omega = 2.0 * 3.14159265358979323846 * 2616.25565 / sampleRate;
        const double radius = std::exp(-5.0 / sampleRate);
        for (double phase : {.1, .7, 1.3, 2.9}) {
            const double y1 = std::cos(phase);
            const double y2 = std::cos(phase - omega) / radius;
            const double quadrature = (radius * y2 - y1 * std::cos(omega)) / std::sin(omega);
            worstRateError = std::max(worstRateError, std::abs(coefficient.Read(y1, quadrature)
                - coefficient.Read(std::cos(phase), std::sin(phase))));
        }
    }
    require(worstRateError < 1.e-12, "same physical modal state projects differently with sample rate");
    std::cout << "RADIATION worst_dense_projection_error=" << worstQuadratureError
              << " minimum_model_close_far_balance_change=" << smallestBalanceChange
              << " worst_rate_projection_error=" << worstRateError << '\n';
    std::cout << "Modal radiation geometry contracts passed.\n";
}
