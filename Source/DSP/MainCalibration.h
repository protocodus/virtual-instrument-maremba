#pragma once

#include <array>

namespace maremba {

// Fixed gains: initialized models at 85% Volume and the authored factory VCAs
// put the shared five-second velocity-80 Linear phrase around -7 dBFS peak.
// No runtime RMS measurement or level matching is applied. The existing
// smooth lookahead allows accents up to -1 dBFS, retaining 6 dB above normal
// playing and 1 dB below full scale. Direct captures keep their 0.94 ceiling.
inline constexpr float kMainPeakCeiling = 0.891250938f; // 10^(-1 / 20)
inline constexpr float kMainReferencePeakDb = -7.0f;
// Historical offline equal-RMS solver default; no longer an init/bank target.
inline constexpr float kMainReferenceRmsDb = -27.0f;
inline constexpr std::array<float, 4> kMainModelCalibration {2.156637999f, 2.113276235f, 2.805121587f, 2.111566469f};

} // namespace maremba
