#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// Equal knob travel produces a proportional change in ring time, with the
// natural instrument exactly halfway. The engine retains physical .1..8 units.
inline float DecayFromControl(float position) {
    position = std::isfinite(position) ? std::clamp(position, 0.0f, 1.0f) : 0.5f;
    return std::pow(position <= 0.5f ? 10.0f : 8.0f, 2.0f * position - 1.0f);
}

inline float DecayToControl(float multiplier) {
    multiplier = std::isfinite(multiplier) ? std::clamp(multiplier, 0.1f, 8.0f) : 1.0f;
    return 0.5f + 0.5f * std::log(multiplier) / std::log(multiplier <= 1.0f ? 10.0f : 8.0f);
}

} // namespace maremba
