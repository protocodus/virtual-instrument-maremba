#pragma once

#include <cmath>

namespace maremba {

// Every per-sample constant in the engine was voiced at 44.1 kHz with 2x
// oversampling. Rate-dependent coefficients are derived from that reference so
// the factory patches keep their sound at 44.1k/2x and stay the same at every
// other host rate and oversampling factor.
constexpr double kReferenceRate = 88200.0;

constexpr double kPiD = 3.14159265358979323846;
constexpr double kTwoPiD = 2.0 * kPiD;

// Per-sample decay multiplier (y *= pole) voiced at the reference rate,
// converted to fs with the same time constant.
inline double PoleAtRate(double poleAtRef, double fs) {
    return std::pow(poleAtRef, kReferenceRate / fs);
}

// One-pole smoother coefficient (y += alpha * (x - y)) voiced at the reference
// rate, converted to fs with the same time constant.
inline double SmootherAtRate(double alphaAtRef, double fs) {
    return 1.0 - std::pow(1.0 - alphaAtRef, kReferenceRate / fs);
}

// Magnitude of the two-pole denominator 1 - 2r cos(w) z^-1 + r^2 z^-2 at z = e^jw.
// A resonator y = c*y1 - s*y2 + b*x has gain b / ResonatorDenominatorAt(r, w) at
// its centre frequency, so scaling b by this value makes that gain independent
// of the sample rate.
inline double ResonatorDenominatorAt(double r, double w) {
    return (1.0 - r) * std::sqrt(1.0 - 2.0 * r * std::cos(2.0 * w) + r * r);
}

// Input gain b for a two-pole resonator at (r, w) that reproduces the centre
// gain that b = bAtRef had at the reference rate (same frequency and Q).
inline double RateInvariantResonatorGain(double bAtRef, double rRef, double wRef, double r, double w) {
    const double denRef = ResonatorDenominatorAt(rRef, wRef);
    if (!(denRef > 0.0)) return bAtRef;
    return bAtRef * ResonatorDenominatorAt(r, w) / denRef;
}

} // namespace maremba
