#pragma once

#include <array>

namespace maremba {

// Fixed recording gains, not automatic RMS control. Calibrated with the shared
// five-second velocity-80 phrase, Linear response, stereo RMS including tails.
// Factory VCA settings put normal phrase peaks around -8 dBFS. The existing
// smooth lookahead allows accents up to -1 dBFS, retaining 7 dB of dynamic
// reserve above normal playing and 1 dB below full scale. Direct captures
// keep their independent 0.94 ceiling.
inline constexpr float kMainPeakCeiling = 0.891250938f; // 10^(-1 / 20)
inline constexpr float kMainReferenceRmsDb = -27.0f;
inline constexpr std::array<float, 4> kMainModelCalibration {1.225115902f, 1.174502045f, 1.406057112f, 1.034682015f};

} // namespace maremba
