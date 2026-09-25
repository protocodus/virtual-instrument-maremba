#pragma once

#include "MarimbaModel.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// sin and cos of one angle to 1 ulp for |x| < 1e5: reduction by quarter
// turns (Cody-Waite, pi/2 in three parts) and Cephes' minimax polynomials on
// [-pi/4, pi/4], branch-free so loops over it vectorize; about five times
// faster than libm. A fresh strike takes some 2000 (mode shapes, radiation).
inline void FastSinCos(double x, double& s, double& c) {
    constexpr double kTwoOverPi = 0.63661977236758134308;
    constexpr double kP1 = 1.5707963267341256e+00, kP2 = 6.0771005065061922e-11, kP3 = 2.0222662487959506e-21;
    const double n = std::nearbyint(x * kTwoOverPi);
    const double r = ((x - n * kP1) - n * kP2) - n * kP3;
    const double z = r * r;
    const double sr = r + r * z * (-1.66666666666666307295e-1 + z * (8.33333333332211858878e-3
                    + z * (-1.98412698295895385996e-4 + z * (2.75573136213857245213e-6
                    + z * (-2.50507477628578072866e-8 + z * 1.58962301576546568060e-10)))));
    const double cr = 1.0 - 0.5 * z + z * z * (4.16666666666665929218e-2 + z * (-1.38888888888730564116e-3
                    + z * (2.48015872888517045348e-5 + z * (-2.75573141792967388112e-7
                    + z * (2.08757008419747316778e-9 + z * -1.13585365213876817300e-11)))));
    const long q = std::abs(n) < 1.0e15 ? static_cast<long>(n) & 3 : 0;   // NaN stays NaN, no UB
    const double ss = (q & 1) ? cr : sr, cc = (q & 1) ? sr : cr;
    s = (q & 2) ? -ss : ss;
    c = ((q + 1) & 2) ? -cc : cc;
}

// Mode shapes along the bar (x = 0..1) or tine (x = 0 at the bridge, 1 at the
// free end). Bars use the generated carved-bar shapes for their first modes
// and uniform free-free shapes above; both are normalized to max |phi| = 1 and
// positive at x = 0, the convention of the generated modal masses. Everything
// here runs at note-on only.

// Free-free Euler-Bernoulli bar, mode m (0 = fundamental). Parity is enforced
// by mirroring, which also keeps the exponentials small.
inline double UniformBarShape(int mode, double x) {
    static constexpr double kRoot[8] = {4.7300407448627, 7.85320462409584, 10.9956078380017,
        14.137165491257, 17.278759657399, 20.420352245626, 23.56194490204, 26.7035375555};
    static constexpr double kSigma[4] = {0.982502214576238, 1.00077731190727,
        0.999966450125409, 1.00000144989766};
    mode = std::max(mode, 0);
    const double beta = mode < 8 ? kRoot[mode] : (mode + 1.5) * 3.14159265358979323846;
    const double sigma = mode < 4 ? kSigma[mode] : 1.0;
    x = std::clamp(x, 0.0, 1.0);
    double sign = 1.0;
    if (x > 0.5) {
        x = 1.0 - x;
        if (mode % 2) sign = -1.0;
    }
    const double z = beta * x;
    // From the fifth mode on sigma is 1 and the growing exponential drops out.
    const double growing = sigma == 1.0 ? 0.0 : (1.0 - sigma) * std::exp(z);
    const double hyperbolic = 0.5 * (growing + (1.0 + sigma) * std::exp(-z));
    double sine, cosine;
    FastSinCos(z, sine, cosine);
    return sign * 0.5 * (hyperbolic + cosine - sigma * sine);
}

// Carved bar shape: Catmull-Rom interpolation of the half-bar samples.
inline double BarShape(const BarKey& key, int mode, double x) {
    if (mode >= kBarShapedModes) return UniformBarShape(mode, x);
    x = std::clamp(x, 0.0, 1.0);
    double sign = 1.0;
    if (x > 0.5) {
        x = 1.0 - x;
        if (mode % 2) sign = -1.0;
    }
    const double* s = key.shape[mode];
    const double u = x / 0.5 * (kBarShapePoints - 1);
    const int i = std::clamp(static_cast<int>(u), 0, kBarShapePoints - 2);
    const double t = u - i;
    // Mirror samples across x = 0.5 by parity; extrapolate linearly at x = 0.
    const auto at = [&](int k) {
        if (k < 0) return 2.0 * s[0] - s[1];
        if (k >= kBarShapePoints) {
            const int mirrored = 2 * (kBarShapePoints - 1) - k;
            return (mode % 2 ? -1.0 : 1.0) * s[std::max(mirrored, 0)];
        }
        return s[k];
    };
    const double p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    const double value = p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3
                       + t * (3.0 * (p1 - p2) + p3 - p0)));
    return sign * value;
}

// Displacement shape of a mounted tine (rigid-bridge solution, integral of
// phi^2 over the whole two-span beam = 1 in free-length units).
inline double TineShape(int mode, double position) {
    return KalimbaFullModeShape(mode, std::clamp(position, 0.0, 1.0));
}

// Force projection averaged over a contact patch of the given width
// (fraction of the length), five-point Gauss-Legendre.
template <typename Shape>
inline double FootprintAverage(Shape&& shape, double x, double width) {
    static constexpr double kNode[5] = {-0.906179845938664, -0.538469310105683, 0.0,
                                        0.538469310105683, 0.906179845938664};
    static constexpr double kWeight[5] = {0.236926885056189, 0.478628670499366, 0.568888888888889,
                                          0.478628670499366, 0.236926885056189};
    const double half = 0.5 * std::clamp(width, 0.0, 0.3);
    if (half < 1.0e-9) return shape(x);
    double sum = 0.0;
    for (int i = 0; i < 5; ++i) sum += kWeight[i] * shape(std::clamp(x + half * kNode[i], 0.0, 1.0));
    return 0.5 * sum;
}

// Mean square over the patch: the part of a mode the contact can damp even
// where opposite lobes cancel its force projection.
template <typename Shape>
inline double FootprintMeanSquare(Shape&& shape, double x, double width) {
    return FootprintAverage([&shape](double y) { const double v = shape(y); return v * v; }, x, width);
}

} // namespace maremba
