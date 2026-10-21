#pragma once

#include <array>

namespace maremba {

// Fixed recording gains retain the installed 1.5.5 instrument level and knob
// response. Factory VCAs balance the velocity-100 single/pair RMS comparison;
// normal attacks retain their character. The independent smooth lookahead
// protects Main at -1 dBFS; direct captures keep their 0.94 ceiling.
// kMainReferenceRmsDb is the historical offline solver default, not this bank's
// current target. No runtime RMS level matching is applied.
inline constexpr float kMainPeakCeiling = 0.891250938f; // 10^(-1 / 20)
inline constexpr float kMainReferenceRmsDb = -27.0f;
inline constexpr std::array<float, 4> kMainModelCalibration {2.156637999f, 2.113276235f, 2.805121587f, 2.111566469f};

} // namespace maremba
