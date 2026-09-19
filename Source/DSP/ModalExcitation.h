#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// Signed Euler-Bernoulli mode shapes, averaged over the contact footprint.
// Frequencies still come from the individually voiced instrument models: a
// uniform beam is a spatial approximation, not a measured undercut geometry.
// All work is at note-on; nothing here runs in the per-sample modal bank.
inline double ModalContactWeight(int mode, double position, double width, bool tine) {
    struct BeamMode { double root, sigma; };
    // Non-rigid roots of cosh(b)*cos(b)=1 (free/free) or -1 (clamped/free).
    static constexpr BeamMode bar[8] = {
        {4.7300407448627, 0.982502214576238}, {7.85320462409584, 1.00077731190727},
        {10.9956078380017, 0.999966450125409}, {14.137165491257, 1.00000144989766},
        {17.278759657399, 0.999999937344383}, {20.420352245626, 1.0000000027076},
        {23.56194490204, 0.999999999882994}, {26.7035375555, 1.00000000000506}
    };
    static constexpr BeamMode cantilever[8] = {
        {1.87510406871196, 0.734095513758913}, {4.69409113297417, 1.01846731875922},
        {7.85475743823761, 0.999224496517428}, {10.9955407348755, 1.00003355325171},
        {14.1371683910465, 0.99999855010865}, {17.2787595320882, 1.00000006265563},
        {20.4203522510413, 0.999999997292405}, {23.5619449018064, 1.00000000011701}
    };
    mode = std::clamp(mode, 0, 7);
    const BeamMode& b = tine ? cantilever[mode] : bar[mode];
    const double x = std::clamp(position, 0.0, 1.0);
    const double halfWidth = 0.5 * std::clamp(width, 0.0, 0.25);
    const double lo = std::max(0.0, x - halfWidth);
    const double hi = std::min(1.0, x + halfWidth);
    double centre = 0.5 * (lo + hi);
    double sign = 1.0;
    // Enforce parity exactly, and keep exponentials small at the right end.
    if (!tine && centre > 0.5) {
        centre = 1.0 - centre;
        if (mode % 2) sign = -1.0;
    }
    if (!tine && mode % 2 && centre == 0.5) return 0.0;
    const double z = b.root * centre;
    const double h = b.root * 0.5 * (hi - lo);
    // SDK 5's target math surface has exp, but no sinh. Small h uses the
    // even Taylor series to avoid cancellation in exp(h)-exp(-h).
    const double hyperbolicAverage = h > 1.0e-3 ? (std::exp(h) - std::exp(-h)) / (2.0 * h)
                                              : 1.0 + h * h / 6.0 + h * h * h * h / 120.0;
    const double circularAverage = h > 1.0e-8 ? std::sin(h) / h : 1.0;
    // This form avoids cancellation between cosh(z) and sigma*sinh(z).
    const double hyperbolic = 0.5 * ((1.0 - b.sigma) * std::exp(z)
                                  + (1.0 + b.sigma) * std::exp(-z));
    const double circular = std::cos(z) - b.sigma * std::sin(z);
    const double shape = hyperbolic * hyperbolicAverage
                       + (tine ? -1.0 : 1.0) * circular * circularAverage;
    // Fundamental gain stays close to the existing centre/default voicing.
    // Other modes use a fixed normalization, never a local nodal denominator.
    if (tine) return shape * (mode == 0 ? 0.65826598627927 : 0.5);
    const double orientation = (mode / 2) % 2 == 0 ? -1.0 : 1.0;
    return sign * orientation * shape * (mode == 0 ? 0.82260894024917 : 0.70710678118655);
}

} // namespace maremba
