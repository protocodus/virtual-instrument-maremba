#pragma once

#include <array>

namespace maremba {

// Fixed recording gains, not automatic RMS control. Calibrated with the shared
// five-second velocity-80 phrase, Linear response, stereo RMS including tails.
// Main peaks reserve -7 dBFS through the existing smooth lookahead protector;
// the direct captures keep their independent 0.94 ceiling.
inline constexpr float kMainPeakCeiling = 0.446683592f; // 10^(-7 / 20)
inline constexpr float kMainReferenceRmsDb = -27.0f;
inline constexpr std::array<float, 4> kMainModelCalibration {1.225115902f, 1.174502045f, 1.406057112f, 1.034682015f};

} // namespace maremba
