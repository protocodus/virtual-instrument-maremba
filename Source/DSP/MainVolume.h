#pragma once

#include <cmath>

// Normalized Main amplitude: 85% is unity; full travel adds 7.04 dB.
// The existing Main calibration and 5 ms amplitude smoothing remain external.
// Evaluate this target once per block/control update, never per audio sample.
// Direct outputs do not use this curve.
namespace maremba {

inline float MainVolumeGain(float knob) noexcept {
    // The engine already sanitizes parameters; make this standalone helper safe.
    if (!std::isfinite(knob) || knob <= 0.0f) return 0.0f;
    if (knob >= 1.0f) return 2.25f;
    constexpr float reference = 0.85f;
    constexpr float upperWidth = 0.15f;
    // ln(2.25 * reference^2): compensate the continuing square law at the top.
    constexpr float upperExponent = 0.485892357220779f;
    const float normalized = knob / reference;
    const float square = normalized * normalized;
    if (knob <= reference) return square;
    const float upper = (knob - reference) / upperWidth;
    return square * std::exp(upperExponent * upper * upper);
}

} // namespace maremba
