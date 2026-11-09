#pragma once

// Maremba's front-panel controls, shared by every front end so a Reason patch
// and a plug-in preset with the same values sound the same. Each control is
// stored as the Rack property value: 0..1 for a knob, the step index for a
// switch. The order, names and defaults are the Rack Extension's
// document_owner properties (motherboard_def.lua); never reorder or rename
// one, since saved songs and presets recall controls by name.

#include "DecayControl.h"
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

enum EControl {
    kModel,
    kMalletType,
    kMalletHardness,
    kStrikePosition,
    kStrikeJitter,
    kResonatorTune,
    kResonatorCoupling,
    kDecay,
    kBuzzAmount,
    kArtifacts,
    kSympathetic,
    kPitchGlide,
    kBodyBloom,
    kCloseLevel,
    kFarLevel,
    kPiezoLevel,
    kInvertPiezo,
    kStereoWidth,
    kGentleWiden,
    kPreampDrive,
    kWarmth,
    kCompAmount,
    kCompAttack,
    kCompRelease,
    kVolume,
    kOversampling,
    kVelocityCurve,
    kPolyphony,
    kMasterTune,
    kDetune,
    kRoomType,
    kVelocityToHardness,
    kVelocityToPosition,
    kVelocityToDecay,
    kTuningScale,
    kNoteOffMode,
    kCarving,
    kMembraneTension,
    kVelocityToCarving,
    kVelocityToCoupling,
    kVelocityToBloom,
    kVelocityToSympathetic,
    kVelocityToBuzz,
    kVelocityToTension,
    kVelocityToArtifacts,
    kVelocityToVariance,
    kModWheel,
    kPitchBend,
    kControlCount
};

inline constexpr std::array<const char*, kControlCount> kControlNames {
    "model",
    "malletType",
    "malletHardness",
    "strikePosition",
    "strikeJitter",
    "resonatorTune",
    "resonatorCoupling",
    "decay",
    "buzzAmount",
    "artifacts",
    "sympathetic",
    "pitchGlide",
    "bodyBloom",
    "closeLevel",
    "farLevel",
    "piezoLevel",
    "invertPiezo",
    "stereoWidth",
    "gentleWiden",
    "preampDrive",
    "warmth",
    "compAmount",
    "compAttack",
    "compRelease",
    "volume",
    "oversampling",
    "velocityCurve",
    "polyphony",
    "masterTune",
    "detune",
    "roomType",
    "velocityToHardness",
    "velocityToPosition",
    "velocityToDecay",
    "tuningScale",
    "noteOffMode",
    "carving",
    "membraneTension",
    "velocityToCarving",
    "velocityToCoupling",
    "velocityToBloom",
    "velocityToSympathetic",
    "velocityToBuzz",
    "velocityToTension",
    "velocityToArtifacts",
    "velocityToVariance",
    "modWheel",
    "pitchBend"
};

inline constexpr std::array<double, kControlCount> kControlDefaults {
    0.0,    // model
    1.0,    // malletType (Medium Cord / Natural Thumb)
    0.40,   // malletHardness
    0.50,   // strikePosition
    0.15,   // strikeJitter
    0.50,   // resonatorTune
    0.70,   // resonatorCoupling
    0.50,   // decay: natural ring at the midpoint
    0.0,    // buzzAmount: start without membrane buzz; saved values remain active
    0.35,   // artifacts
    0.40,   // sympathetic
    0.30,   // pitchGlide: retained legacy default, ignored by audio
    0.50,   // bodyBloom
    0.85,   // closeLevel
    0.45,   // farLevel
    0.30,   // piezoLevel
    0.0,    // invertPiezo: old songs retain their original polarity
    0.50,   // stereoWidth
    0.0,    // gentleWiden: retained legacy default, ignored by audio
    0.10,   // preampDrive
    0.55,   // warmth
    0.05,   // compAmount
    0.1837, // compAttack
    0.2083, // compRelease
    0.85,   // volume: nominal level reference
    3.0,    // oversampling (1x; saved 2x/4x/8x indices remain 0/1/2)
    1.0,    // velocityCurve (Linear)
    1.0,    // polyphony (16)
    0.50,   // masterTune
    0.0,    // detune
    0.0,    // roomType (Wood; existing patch default)
    0.20,   // velocityToHardness
    0.0,    // velocityToPosition
    0.0,    // velocityToDecay
    0.0,    // tuningScale (Equal)
    0.0,    // noteOffMode (Ring)
    1.0,    // carving
    0.50,   // membraneTension
    0.0,    // velocityToCarving
    0.0,    // velocityToCoupling
    0.0,    // velocityToBloom
    0.0,    // velocityToSympathetic
    0.0,    // velocityToBuzz
    0.0,    // velocityToTension
    0.0,    // velocityToArtifacts
    0.0,    // velocityToVariance: older patches retain fixed strike variance
    0.0,    // modWheel
    0.50    // pitchBend
};

using ControlValues = std::array<double, kControlCount>;

// What a front end adds to a control, in the control's 0..1 units: Rack CV
// inputs today. Zero leaves the control as stored.
struct ControlModulation {
    double malletHardness = 0.0;
    double strikePosition = 0.0;
    double resonatorCoupling = 0.0;
    double volume = 0.0;
    double sympathetic = 0.0;
};

inline constexpr double kPitchBendRangeCents = 200.0; // full wheel = +/-2 semitones

namespace detail {
inline double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

// Round and clamp a stepped value in floating point before converting, so
// no value can reach an undefined float-to-int conversion.
inline int StepIndex(double value, int steps) {
    const double last = static_cast<double>(steps - 1);
    return static_cast<int>(std::floor(std::clamp(FiniteOr(value, 0.0), 0.0, last) + 0.5));
}
} // namespace detail

// A nonfinite stored value reads as the control's default.
inline double ControlNumber(const ControlValues& values, EControl control) {
    return detail::FiniteOr(values[control], kControlDefaults[control]);
}

// A normalised 0..1 control, clamped in double precision.
inline float ControlUnit(const ControlValues& values, EControl control) {
    return static_cast<float>(std::clamp(ControlNumber(values, control), 0.0, 1.0));
}

// The engine's physical parameters for a set of controls. extraTuneCents is a
// host-wide tuning offset (Reason's master tune) added to the Master Tune knob.
inline EngineParameters EngineParametersFromControls(const ControlValues& values,
                                                     const ControlModulation& modulation = {},
                                                     double extraTuneCents = 0.0) {
    const auto number = [&](EControl c) { return ControlNumber(values, c); };
    const auto unit = [&](EControl c) { return ControlUnit(values, c); };
    // Control + modulation, clamped to 0..1. A nonfinite modulation adds nothing.
    const auto modulated = [&](EControl c, double amount) {
        return static_cast<float>(std::clamp(unit(c) + detail::FiniteOr(amount, 0.0), 0.0, 1.0));
    };
    using detail::StepIndex;

    EngineParameters p;
    p.model = StepIndex(number(kModel), 4);
    p.malletType = StepIndex(number(kMalletType), kStrikerCount);
    p.malletHardness = modulated(kMalletHardness, modulation.malletHardness);
    p.strikePosition = modulated(kStrikePosition, modulation.strikePosition);
    p.strikeJitter = unit(kStrikeJitter) * 100.0f;
    p.resonatorTune = (unit(kResonatorTune) - 0.5f) * 100.0f;
    p.resonatorCoupling = modulated(kResonatorCoupling, modulation.resonatorCoupling);
    p.decay = DecayFromControl(unit(kDecay));
    p.buzzAmount = unit(kBuzzAmount);
    p.artifacts = unit(kArtifacts);

    p.sympathetic = modulated(kSympathetic, modulation.sympathetic);
    // Keep the stored property/binding for old songs, but never apply its sweep.
    p.pitchGlide = 0.0f;
    p.bodyBloom = unit(kBodyBloom);

    p.closeLevel = unit(kCloseLevel);
    p.farLevel = unit(kFarLevel);
    p.roomType = StepIndex(number(kRoomType), 3);
    p.piezoLevel = unit(kPiezoLevel);
    p.invertPiezo = StepIndex(number(kInvertPiezo), 2);
    p.stereoWidth = unit(kStereoWidth) * 2.0f;
    p.gentleWiden = 0; // retain the saved property, but remove Stereo Spread from audio

    p.preampDrive = unit(kPreampDrive);
    p.warmth = (unit(kWarmth) - 0.5f) * 2.0f;
    p.compAmount = unit(kCompAmount);
    p.compAttack = 1.0f + unit(kCompAttack) * 49.0f;
    p.compRelease = 20.0f + unit(kCompRelease) * 480.0f;

    p.volume = modulated(kVolume, modulation.volume);
    p.oversampling = StepIndex(number(kOversampling), 4);
    p.velocityCurve = StepIndex(number(kVelocityCurve), 4);
    p.polyphony = StepIndex(number(kPolyphony), 3);
    // Master Tune (+/-100 cents) plus the host's own tuning offset.
    p.tuneCents = static_cast<float>((unit(kMasterTune) - 0.5) * 200.0 + extraTuneCents);
    p.detune = unit(kDetune) * 100.0f;
    p.velocityToHardness = unit(kVelocityToHardness);
    p.velocityToPosition = unit(kVelocityToPosition);
    p.velocityToDecay = unit(kVelocityToDecay);
    p.velocityToCarving = unit(kVelocityToCarving);
    p.velocityToCoupling = unit(kVelocityToCoupling);
    p.velocityToBloom = unit(kVelocityToBloom);
    p.velocityToSympathetic = unit(kVelocityToSympathetic);
    p.velocityToBuzz = unit(kVelocityToBuzz);
    p.velocityToTension = unit(kVelocityToTension);
    p.velocityToArtifacts = unit(kVelocityToArtifacts);
    p.velocityToVariance = unit(kVelocityToVariance);
    p.tuningScale = StepIndex(number(kTuningScale), static_cast<int>(ETuningScale::Count));
    p.noteOffMode = StepIndex(number(kNoteOffMode), 3);
    p.carving = unit(kCarving);
    p.membraneTension = unit(kMembraneTension);
    // Mod wheel: roll speed on the mallet instruments, hole cover on Kalimba.
    p.modWheel = unit(kModWheel);
    return p;
}

inline float PitchBendCentsFromControls(const ControlValues& values) {
    return static_cast<float>((ControlUnit(values, kPitchBend) - 0.5) * 2.0 * kPitchBendRangeCents);
}

} // namespace maremba
