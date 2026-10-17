#pragma once

#include <array>

namespace maremba {

// Fixed recording gains, not automatic level control. Measured through the Rack
// wrapper at nominal Volume, using the louder of the velocity-80 and varied
// velocity reference phrases. See the Rack device's Docs/validation/peak-calibration-20261016.
inline constexpr std::array<float, 4> kMainModelCalibration {1.726411287f, 1.638942302f, 1.816753899f, 1.414657906f};

} // namespace maremba
