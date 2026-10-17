// The front-panel control table every front end shares: unique names, stored
// defaults inside each control's range, and a mapping that cannot hand the
// engine an out-of-range or nonfinite parameter, whatever a song or preset holds.
#include "MarembaControls.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

int gFailures = 0;

void Check(bool passed, const char* what) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

bool Finite(const maremba::EngineParameters& p) {
    for (float value : { p.malletHardness, p.strikePosition, p.resonatorTune, p.resonatorCoupling, p.decay,
                         p.buzzAmount, p.artifacts, p.sympathetic, p.bodyBloom, p.closeLevel, p.farLevel,
                         p.piezoLevel, p.stereoWidth, p.preampDrive, p.warmth, p.compAmount, p.compAttack,
                         p.compRelease, p.volume, p.tuneCents, p.detune, p.strikeJitter, p.modWheel, p.carving,
                         p.membraneTension })
        if (!std::isfinite(value)) return false;
    return true;
}

} // namespace

int main() {
    using namespace maremba;
    for (int i = 0; i < kControlCount; ++i)
        for (int j = i + 1; j < kControlCount; ++j)
            Check(std::strcmp(kControlNames[i], kControlNames[j]) != 0, "control names are unique");
    Check(std::strcmp(kControlNames[kModel], "model") == 0 && std::strcmp(kControlNames[kPitchBend], "pitchBend") == 0,
          "the table starts at model and ends at pitchBend");

    const ControlValues defaults = kControlDefaults;
    const EngineParameters p = EngineParametersFromControls(defaults);
    Check(Finite(p), "defaults map to finite parameters");
    Check(p.model == 0 && p.malletType == 1 && p.velocityCurve == 3 && p.polyphony == 1, "default switches");
    Check(p.volume == 0.85f && p.tuneCents == 0.0f && PitchBendCentsFromControls(defaults) == 0.0f,
          "nominal Volume, centred Master Tune and Pitch Bend");
    Check(p.pitchGlide == 0.0f && p.gentleWiden == 0, "legacy controls never reach the engine");

    // Nonfinite stored values read as defaults; out-of-range ones clamp.
    for (double value : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                          -1.0e9, 1.0e9 }) {
        ControlValues values;
        values.fill(value);
        const EngineParameters q = EngineParametersFromControls(values, ControlModulation{ value, value, value, value, value });
        Check(Finite(q), "every stored value maps to finite parameters");
        Check(q.model >= 0 && q.model <= 3 && q.malletType >= 0 && q.malletType < kStrikerCount
              && q.oversampling >= 0 && q.oversampling <= 2 && q.noteOffMode >= 0 && q.noteOffMode <= 2
              && q.tuningScale >= 0 && q.tuningScale < static_cast<int>(ETuningScale::Count),
              "switches stay on a step");
        Check(q.volume >= 0.0f && q.volume <= 1.0f && q.stereoWidth >= 0.0f && q.stereoWidth <= 2.0f,
              "knobs stay inside their range");
    }
    ControlValues nan;
    nan.fill(std::numeric_limits<double>::quiet_NaN());
    const EngineParameters fromNan = EngineParametersFromControls(nan);
    Check(fromNan.volume == p.volume && fromNan.malletType == p.malletType && fromNan.decay == p.decay,
          "a nonfinite control reads as its default");

    // Modulation adds in the control's own units and clamps.
    ControlModulation full;
    full.volume = 1.0;
    Check(EngineParametersFromControls(defaults, full).volume == 1.0f, "modulation clamps at the top");
    full.volume = -0.35;
    Check(std::abs(EngineParametersFromControls(defaults, full).volume - 0.5f) < 1e-6f, "modulation adds");

    if (gFailures) return EXIT_FAILURE;
    std::printf("ControlsTest OK (%d controls)\n", static_cast<int>(kControlCount));
    return EXIT_SUCCESS;
}
