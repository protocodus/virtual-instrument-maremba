#pragma once

#include <array>

namespace maremba {

// Fixed recording gains, not automatic RMS control. Established with the shared
// five-second velocity-80 phrase, Linear response, stereo RMS including tails.
// Factory VCAs now balance a separate velocity-100 single/pair RMS comparison;
// natural phrase peaks differ. The independent smooth lookahead protects Main
// at -1 dBFS. Direct captures keep their independent 0.94 ceiling. The original
// model-gain reference below predates the shorter held-body Kalimba lifetime.
inline constexpr float kMainPeakCeiling = 0.891250938f; // 10^(-1 / 20)
inline constexpr float kMainReferenceRmsDb = -27.0f;
inline constexpr std::array<float, 4> kMainModelCalibration {1.225115902f, 1.174502045f, 1.406057112f, 1.034682015f};

} // namespace maremba
