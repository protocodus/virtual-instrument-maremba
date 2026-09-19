#pragma once

#include <algorithm>

namespace maremba {

// Integral of a positive, finite contact/release pulse. Its derivative is
// 30 t² (1-t)² [1 + skew (1-2t)]. For |skew| <= 1 it cannot pull on a bar;
// both force and its slope vanish at separation. Integrating sample intervals
// instead of sampling the peak preserves the impulse at every internal rate.
inline double ContactCumulative(double time, double skew) {
    const double t = std::clamp(time, 0.0, 1.0);
    const double tail = 1.0 - t;
    return t * t * t * (10.0 + t * (-15.0 + 6.0 * t)
                       + 10.0 * std::clamp(skew, -1.0, 1.0) * tail * tail * tail);
}

} // namespace maremba
