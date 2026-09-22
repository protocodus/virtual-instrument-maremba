// Geometrical radiation contracts independent of the voice's excitation and mix.
#include "ModalRadiation.h"
#include <algorithm>
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
}

int main() {
    using namespace maremba;
    for (const auto& node : kModalRadiationNodes) for (int mode = 0; mode < 8; ++mode) {
        require(std::abs(node.bar[mode] - ModalContactWeight(mode, node.x, 0, false)) < 1.e-12
             && std::abs(node.tine[mode] - ModalContactWeight(mode, node.x, 0, true)) < 1.e-12,
                "frozen spatial shapes disagree with the excitation geometry");
    }
    double worstQuadratureError = 0.0, smallestBalanceChange = 1.0;
    for (int instrument = 0; instrument < 4; ++instrument) {
        const auto model = static_cast<EMarimbaModel>(instrument);
        const bool tine = instrument == 3;
        const auto params = GetModelParams(model);
        double balanceChange = 0.0;
        for (double fundamental : {65.406391, 261.625565, 1046.50226, 4186.00904}) {
            const auto geometry = GetModalRadiationGeometry(model, fundamental);
            const auto root = ComputeModalRadiation(0, fundamental, geometry, tine);
            require(root.close.real == 1.0 && root.close.imag == 0.0
                 && root.far.real == 1.0 && root.far.imag == 0.0,
                    "fundamental radiation reference changed existing pitch level/phase");
            for (int mode = 1; mode < 8; ++mode) {
                const double hz = fundamental * (tine ? KalimbaModeRatio(mode) : params.overtoneRatio[mode]);
                const auto readout = ComputeModalRadiation(mode, hz, geometry, tine);
                for (const auto coefficient : {readout.close, readout.far}) {
                    require(std::isfinite(coefficient.real) && std::isfinite(coefficient.imag),
                            "radiation coefficient is not finite");
                    require(magnitude(coefficient) <= 1.0 + 1.e-12 && magnitude(coefficient) >= .2 - 1.e-12,
                            "spatial correction amplified or excessively suppressed an upper mode");
                }
                if (hz < 20000.0) {
                    balanceChange = std::max(balanceChange, std::abs(magnitude(readout.close) - magnitude(readout.far)));
                    const int points = 32;
                    for (const auto microphone : {geometry.close, geometry.far}) {
                        const auto tested = ProjectModalSurface(mode, hz, geometry, microphone, tine, points);
                        const auto reference = ProjectModalSurface(mode, hz, geometry, microphone, tine, 2048);
                        worstQuadratureError = std::max(worstQuadratureError, difference(tested, reference));
                    }
                }
            }
        }
        require(balanceChange > .04, "close/far differ only by a negligible scalar across modes");
        smallestBalanceChange = std::min(smallestBalanceChange, balanceChange);
    }
    require(worstQuadratureError < .015, "surface quadrature disagrees with dense spatial integration");

    auto geometry = GetModalRadiationGeometry(EMarimbaModel::ImperialRosewood, 261.625565);
    const ModalRadiationPoint centre{geometry.length * .5, .0, .15};
    const ModalRadiationPoint left{geometry.length * .21, .03, .15};
    const ModalRadiationPoint right{geometry.length * .79, .03, .15};
    const auto centreOdd = ProjectModalSurface(1, 1046.50226, geometry, centre, false);
    const auto leftOdd = ProjectModalSurface(1, 1046.50226, geometry, left, false);
    const auto rightOdd = ProjectModalSurface(1, 1046.50226, geometry, right, false);
    require(magnitude(centreOdd) < 1.e-12, "symmetric observation failed to cancel an antisymmetric bending mode");
    require(magnitude(leftOdd) > .03, "off-axis observation did not reveal the antisymmetric mode");
    require(magnitude({leftOdd.real + rightOdd.real, leftOdd.imag + rightOdd.imag}) < 1.e-12,
            "mirrored microphone did not reverse antisymmetric spatial radiation");

    // Isolate distance from the differently voiced Close/Far correction strengths.
    const ModalRadiationPoint near{geometry.length * .33, .02, .06};
    const ModalRadiationPoint distant{geometry.length * .33, .02, 1.2};
    const auto nearOdd = ProjectModalSurface(1, 1046.50226, geometry, near, false);
    const auto distantOdd = ProjectModalSurface(1, 1046.50226, geometry, distant, false);
    const auto nearEven = ProjectModalSurface(2, 2616.25565, geometry, near, false);
    const auto distantEven = ProjectModalSurface(2, 2616.25565, geometry, distant, false);
    require(difference(nearOdd, distantOdd) > .03 && difference(nearEven, distantEven) > .03,
            "distance failed to change modal surface cancellation");
    require(std::abs(magnitude(nearOdd) / magnitude(nearEven)
                   - magnitude(distantOdd) / magnitude(distantEven)) > .05,
            "distance changed all modes by only a common gain");

    // This transfer has no sample-rate argument: rendering the same physical
    // phase at different rates must give the same pressure projection. Include
    // the recurrence's quadrature reconstruction used by MarembaVoice.
    const auto coefficient = ComputeModalRadiation(2, 2616.25565, geometry, false).close;
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
