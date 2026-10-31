#pragma once

#include "BarModes.h"
#include "KalimbaModes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maremba {

enum class EMarimbaModel : uint8_t {
    ImperialRosewood = 0,   // concert marimba, rosewood bars over brass tubes
    MayanPadauk = 1,        // Guatemalan/Chiapas marimba, hormigo bars over wooden boxes
    BalafonAncestral = 2,   // West African balafon, vene keys over gourds with mirlitons
    KalimbaArtisan = 3,     // 17-key box kalimba, steel tines on a yielding bridge
    Count = 4
};

// Bending modes per voice (bars). A tine uses the first kNumTineModes of them.
static constexpr int kNumPitchedModes = kBarModes;
static constexpr int kNumTineModes = 8;

// ---------------------------------------------------------------------------
// Material loss: eta(f) = eta0 + eta2 (f / 1 kHz)^2 + etaLow (fLow / f)^2.
// A nearly constant loss factor with a rising high-frequency term is what wood
// shows (Aramaki 2007; Hill 2011: rosewood 0.0036..0.0068, mean 0.0045 up to
// 2 kHz; Chaigne & Doutaut 1997: padauk a0 + a2 f^2); the low term is support
// (cord, strap) friction below the lowest bars. T60 = 2.2 / (eta f).
//
// The rising term levels off above about 2 kHz. Hill's fit ends at 2.1 kHz
// (C5's f4); carried on as f^2 it gave 0.015 at 4.5 kHz and 0.038 at 8 kHz,
// so the bars' upper modes died in 7-30 ms and an attack had nothing above
// 4 kHz after 30 ms. The Iowa MIS 2012 concert marimba (yarn, cord and rubber
// at ff, C3-C5, anechoic) rings its 3-6.5 kHz band for a median T60 of 91 ms
// at 3.75 kHz and 75 ms at 5.5 kHz from 6 to 50 ms after contact: loss
// factors 0.0064 and 0.0053, about flat, as wood's is. The excess over eta0
// is held at their mean less rosewood's eta0. Measured on rosewood only;
// hormigo and vene take the same excess, an assumption.
// ---------------------------------------------------------------------------
struct MaterialLoss {
    double eta0, eta2, etaLow, fLow;
};

static constexpr double kWoodHighLossLimit = 0.0022;

inline double LossFactor(const MaterialLoss& loss, double hz) {
    const double f = std::max(hz, 1.0);
    const double high = std::min(loss.eta2 * (f * 1.0e-3) * (f * 1.0e-3), kWoodHighLossLimit);
    return loss.eta0 + high + loss.etaLow * (loss.fLow / f) * (loss.fLow / f);
}

inline MaterialLoss GetMaterialLoss(EMarimbaModel model) {
    switch (model) {
        case EMarimbaModel::MayanPadauk:      // hormigo 4.75..4.94 permil (Bremaud), fibre cords
            return {0.0048, 0.0010, 0.0015, 70.0};
        case EMarimbaModel::BalafonAncestral: // vene 5.56 permil, leather straps and padding
            return {0.0056, 0.0012, 0.0030, 100.0};
        case EMarimbaModel::KalimbaArtisan:   // spring steel and air; the bridge loss is separate
            return {0.0006, 0.0, 0.0, 1.0};
        case EMarimbaModel::ImperialRosewood:
        default:
            // Least-squares fit to all 13 of Hill's per-partial loss factors of a
            // YM-5100A without tubes (f1 of C2..C7, f4 of C2..C5, f10 of C2..C3,
            // f20 of C2): 10 % RMS error. C2 6.7 s .. C7 0.18 s bare.
            return {0.0036, 0.000539, 0.00133, 65.0};
    }
}

// Kalimba: the fundamental's loss is dominated by the bridge, which carries
// the vibration into the board. Effective eta1(f) = 0.0012 + 0.0022 (f/1k)^2
// + 0.0008 (220/f)^2 reproduces about 4-5 s on low tines, 2.5-4.5 s in the
// middle register and "a few tenths of a second" on the top tines (Holdaway's
// spectra); the part above the steel's own loss leaves through the bridge,
// each mode in proportion to its bridge motion.
inline double TineFundamentalLossFactor(double hz) {
    const double k = std::max(hz, 20.0) * 1.0e-3;
    const double low = 0.220 / k;
    return 0.0012 + 0.0022 * k * k + 0.0008 * low * low;
}

// ---------------------------------------------------------------------------
// Bars: per-key modal data interpolated from the generated anchors.
// ---------------------------------------------------------------------------
struct BarKey {
    double ratio[kBarModes];
    double massFraction[kBarModes];
    double shape[kBarShapedModes][kBarShapePoints];
    double pickupStrain[kBarModes]; // fixed electrode's surface strain per metre of modal displacement
    double torsion[2];
    double lateral;
    double lengthM, widthM, thicknessM, massKg;
};

// The keys as built, or (carved = false) the same keys without an undercut.
inline const BarModeAnchor* BarAnchors(EMarimbaModel model, bool carved = true) {
    switch (model) {
        case EMarimbaModel::MayanPadauk: return carved ? kHormigoBars : kHormigoBlanks;
        case EMarimbaModel::BalafonAncestral: return carved ? kBalafonBars : kBalafonBlanks;
        default: return carved ? kRosewoodBars : kRosewoodBlanks;
    }
}

// Between anchors, in MIDI note (log frequency): mode and torsion ratios
// geometrically (so they move evenly and stay in order), everything else
// linearly; held beyond the ends.
inline BarKey InterpolateBarKey(const BarModeAnchor* anchors, double midi) {
    const double first = anchors[0].midi, last = anchors[kBarAnchors - 1].midi;
    const double position = (std::clamp(midi, first, last) - first)
        / (anchors[1].midi - anchors[0].midi);
    const int lower = std::clamp(static_cast<int>(std::floor(position)), 0, kBarAnchors - 2);
    const double t = std::clamp(position - lower, 0.0, 1.0);
    const BarModeAnchor& a = anchors[lower];
    const BarModeAnchor& b = anchors[lower + 1];
    const auto mix = [t](double x, double y) { return x + t * (y - x); };
    const auto blend = [t](double x, double y) { return x * std::pow(y / x, t); };
    BarKey key{};
    for (int m = 0; m < kBarModes; ++m) {
        key.ratio[m] = blend(a.ratio[m], b.ratio[m]);
        key.massFraction[m] = mix(a.massFraction[m], b.massFraction[m]);
        key.pickupStrain[m] = mix(a.pickupStrain[m], b.pickupStrain[m]);
    }
    for (int m = 0; m < kBarShapedModes; ++m)
        for (int i = 0; i < kBarShapePoints; ++i)
            key.shape[m][i] = mix(a.shape[m][i], b.shape[m][i]);
    key.torsion[0] = blend(a.torsion[0], b.torsion[0]);
    key.torsion[1] = blend(a.torsion[1], b.torsion[1]);
    key.lateral = blend(a.lateral, b.lateral);
    key.lengthM = mix(a.lengthM, b.lengthM);
    key.widthM = mix(a.widthM, b.widthM);
    key.thicknessM = mix(a.thicknessM, b.thicknessM);
    key.massKg = mix(a.massKg, b.massKg);
    return key;
}

// Carving 1 is the key as built; 0 is the same key left uncarved: a bar of the
// same length and width thinned evenly to the note, whose overtones fall where
// a uniform bar's do (about 2.76 and 5.40 x f1). In between, the undercut
// deepens: the mode ratios move geometrically (so they stay in order), the
// mass, mode shapes and thickness linearly. The fundamental stays the note.
inline BarKey GetBarKey(EMarimbaModel model, double midi, double carving = 1.0) {
    BarKey key = InterpolateBarKey(BarAnchors(model, true), midi);
    const double c = std::clamp(carving, 0.0, 1.0);
    if (!(c < 1.0)) return key;
    const BarKey blank = InterpolateBarKey(BarAnchors(model, false), midi);
    const auto mix = [c](double carved, double plain) { return plain + c * (carved - plain); };
    const auto blend = [c](double carved, double plain) { return plain * std::pow(carved / plain, c); };
    for (int m = 0; m < kBarModes; ++m) {
        key.ratio[m] = blend(key.ratio[m], blank.ratio[m]);
        key.massFraction[m] = mix(key.massFraction[m], blank.massFraction[m]);
        key.pickupStrain[m] = mix(key.pickupStrain[m], blank.pickupStrain[m]);
    }
    for (int m = 0; m < kBarShapedModes; ++m)
        for (int i = 0; i < kBarShapePoints; ++i)
            key.shape[m][i] = mix(key.shape[m][i], blank.shape[m][i]);
    key.torsion[0] = blend(key.torsion[0], blank.torsion[0]);
    key.torsion[1] = blend(key.torsion[1], blank.torsion[1]);
    key.lateral = blend(key.lateral, blank.lateral);
    key.thicknessM = mix(key.thicknessM, blank.thicknessM);
    key.massKg = mix(key.massKg, blank.massKg);
    return key;
}

// ---------------------------------------------------------------------------
// Resonators: tube, tapered wooden box, gourd. Q and tuning follow the
// measured practice; readouts are the mouth's monopole radiation (calibrated).
// ---------------------------------------------------------------------------
struct ResonatorProfile {
    double ratio[3];          // air modes relative to the lowest
    double qAtC4;             // Q of the lowest mode at C4 ...
    double qSlope;            // ... times (C4 / f)^qSlope ...
    double qCornerHz;         // ... divided by 1 + (f / corner)^2 (mouth radiation)
    double tubeRadiusM;       // > 0: a round tube of this inner radius at C4, whose Q follows its losses instead
    double upperQ[2];         // upper modes' Q relative to the lowest
    double aperture[3];       // relative coupling of each air mode
    double readout[3];        // radiated pressure per unit air state
    double tuneBassCents;     // tuning of the lowest air mode re its bar at C2 ...
    double tuneLowCents;      // ... moving to this at C4, held to C5 ...
    double tuneHighCents;     // ... and moving to this in the top octave (C7)
    double coupling;          // kappa: bar-air coupling rate / bar angular frequency at Coupling 0.7
    double mouthFraction;     // mouth diameter / bar length at C4 (overlap window)
};

inline ResonatorProfile GetResonatorProfile(EMarimbaModel model) {
    switch (model) {
        case EMarimbaModel::MayanPadauk:
            // Tapered cedar/cypress box closed at the bottom; "tuned with much
            // greater precision than gourds" (Garfias 1983), lossier than brass.
            return {{1.0, 2.63, 4.37}, 26.0, 0.25, 1800.0, 0.0, {0.6, 0.45}, {1.0, 0.55, 0.35},
                    {1.0, 0.35, 0.22}, -5.0, -10.0, -40.0, 0.0125, 0.16};
        case EMarimbaModel::BalafonAncestral:
            // Calabash: a lossy Helmholtz-like cavity (Godlovitch: Q up to ~10;
            // a berimbau gourd measures 12), chosen by ear, so each gourd sits
            // up to +/-30 cents off its key.
            return {{1.0, 2.81, 4.62}, 9.0, 0.10, 2500.0, 0.0, {0.45, 0.35}, {1.0, 0.30, 0.18},
                    {1.0, 0.30, 0.18}, 0.0, 0.0, 0.0, 0.0150, 0.18};
        case EMarimbaModel::KalimbaArtisan:
            return {{1.0, 1.0, 1.0}, 1.0, 0.0, 1.0e9, 0.0, {1.0, 1.0}, {0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0}, 0.0, 0.0, 0.0, 0.0, 0.0};
        case EMarimbaModel::ImperialRosewood:
        default:
            // Round tube closed at the bottom: odd modes, end correction lowers
            // the upper ones slightly. Its Q follows the wall and mouth losses
            // of La Favre's tube bank (TubeRadius): 87 at C2, 63 at C4, 32 at
            // C5 and about 3 at C7, where the short caps act as reflectors more
            // than resonators (Stevens). With the bar over the mouth, a tube
            // sits slightly above its bar - "slightly above", Stevens tunes
            // them, growing to more than a half step in the top octave; Yamaha
            // and La Favre set them for the loudest resonance under the bar.
            // Coupling 0.0060 (was 0.0071): against the Iowa MIS 2012 keys
            // C2-C5 (cord ff) the fundamental rang a median 0.87x the
            // recordings' at 0.0071 and 0.96x at 0.0060, where Hill's C2 keeps
            // 0.75 of its bare decay (Round6ResponseTest's upper bound).
            return {{1.0, 2.97, 4.93}, 45.0, 0.18, 2000.0, 0.027, {0.7, 0.5}, {1.0, 0.45, 0.25},
                    {1.0, 0.40, 0.22}, 3.0, 8.0, 100.0, 0.0060, 0.14};
    }
}

// Inner radius of a concert marimba's tube by register: La Favre's 5-octave
// bank (98 mm bore at C2, 54 mm through C4-C#5, 48 mm from D5 to C7), a
// power law in frequency either side of C4.
inline double TubeRadius(const ResonatorProfile& r, double hz) {
    const double ratio = 261.625565 / std::max(hz, 1.0);
    return r.tubeRadiusM * std::pow(ratio, ratio > 1.0 ? 0.43 : 0.06);
}

inline double ResonatorQ(const ResonatorProfile& r, double hz) {
    const double f = std::max(hz, 1.0);
    if (r.tubeRadiusM > 0.0) {
        // 1/Q = 1/Q_wall + 1/Q_rad: the viscothermal boundary layer,
        // Q_wall = a / (delta_v (1 + (gamma - 1) / sqrt(Pr))) = 310 a sqrt(f),
        // and an unflanged open end, Q_rad = pi / (ka)^2. Soares, Debut &
        // Antunes (2023) measured 0.8 of this on their tube (Q 68 at 174 Hz).
        const double a = TubeRadius(r, f);
        const double wall = 310.0 * a * std::sqrt(f);
        const double ka = 2.0 * 3.141592653589793 * f / 343.0 * a;
        return 0.8 / (1.0 / wall + ka * ka / 3.141592653589793);
    }
    return r.qAtC4 * std::pow(261.625565 / f, r.qSlope) / (1.0 + (f / r.qCornerHz) * (f / r.qCornerHz));
}

// A tube under its bar. ResonatorQ is the free tube (Soares et al. measured
// 0.8 of the wall and mouth theory on a tube alone). Under the bar the air
// that fills and empties the tube is pumped through the narrow gap between
// the bar and the rim, and flow separation at the rim (the loss behind the
// odd harmonics an open end makes at high amplitude: Ingard & Ising 1967,
// Disselhorst & van Wijngaarden 1980) adds to its loss. Fitted to anechoic
// recordings of a concert marimba (University of Iowa MIS, 2013: cord
// mallets, mf and ff, A2-C5): the bloom peaks at 70-170 ms, rises 5-10 dB,
// decays smoothly and the fundamental rings 1.4-2.9 s, which the free-tube Q
// and coupling (past critical, twin lines beating 5-11 dB) could not give.
// Boxes and gourds keep their voiced Q (it is already their in-situ value).
// The mouth readout (airGain) was raised with it so the bank's loudness stays
// where it was: the gap takes energy that no longer radiates as a bloom.
constexpr double kTubeGapQ = 0.45;
inline double InSituResonatorQ(const ResonatorProfile& r, double hz) {
    return ResonatorQ(r, hz) * (r.tubeRadiusM > 0.0 ? kTubeGapQ : 1.0);
}

// Tuning of the lowest air mode re its bar, by register.
inline double ResonatorTuningCents(const ResonatorProfile& r, int noteNumber) {
    const double note = static_cast<double>(noteNumber);
    if (note < 72.0) {
        const double t = std::clamp((note - 36.0) / 24.0, 0.0, 1.0);
        return r.tuneBassCents + (r.tuneLowCents - r.tuneBassCents) * t;
    }
    double t = std::clamp((note - 72.0) / 24.0, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    return r.tuneLowCents + (r.tuneHighCents - r.tuneLowCents) * std::pow(t, 1.5);
}

// ---------------------------------------------------------------------------
// Strikers. A mallet is a mass on a nonlinear felt/rubber/wood spring,
// F = K d^p (1 + lambda d'), plus a harder core that engages after coreDepth of
// compression (yarn and cord mallets). Stiffnesses give rigid-wall contact
// times at 1 m/s of 2.4 / 1.3 / 0.6 / 0.18 ms; rubber and boxwood follow
// Chaigne & Doutaut's xylophone mallets (Hertz exponent 1.5, 0.65-1.3 ms and
// 0.17-0.4 ms). Hardness scales the stiffness over about two decades.
//
// The core depths are measured: with 3.0 and 1.8 mm a forte yarn or cord head
// never reached its core (1.3 and 0.8 mm peak compression on C4), and ff
// attacks lay 20 and 10 dB under the Iowa MIS 2012 concert marimba in the
// 2-8 kHz bands of their first 15 ms (C3-C6, re the fundamental). 1.0 and
// 0.6 mm leave a mean error of -1.5 and -1.3 dB there, so a loud yarn or cord
// stroke gets its core's edge, as players hear it; soft strokes and low
// Hardness (which deepens the wrap) stay on the yarn.
// ---------------------------------------------------------------------------
struct MalletSpec {
    double massKg;
    double stiffness, exponent;
    double coreDepthM, coreStiffness, coreExponent;
    double hysteresis;        // s/m
    double footprint;         // contact patch / bar length
    double texture;           // yarn surface: small force modulation
    double click;             // unmodelled high-frequency impact noise
};

inline MalletSpec GetMalletSpec(int type) {
    switch (std::clamp(type, 0, 3)) {
        case 0:  return {0.030, 2.091e10, 2.8, 1.0e-3, 5.5e7, 1.5, 0.60, 0.050, 0.020, 0.02};
        case 2:  return {0.026, 5.477e7, 1.5, 0.0, 0.0, 1.5, 0.25, 0.030, 0.0, 0.25};
        case 3:  return {0.024, 1.027e9, 1.5, 0.0, 0.0, 1.5, 0.08, 0.012, 0.0, 0.70};
        case 1:
        default: return {0.028, 1.205e10, 2.5, 0.6e-3, 6.0e7, 1.5, 0.45, 0.040, 0.012, 0.06};
    }
}

// The mallet's shaft on the bar's edge (col legno): an 8-9 mm rattan or birch
// handle, Hertz contact of crossed cylinders K = 2e8 N/m^1.5 (5e7 across the
// grain .. 8e8 birch; Hardness spans that), 27 g effective at the shoulder,
// so 0.2-0.6 ms of contact. A fingernail tap on a tine: a few grams of
// fingertip behind a nail, about 0.25 ms.
inline MalletSpec GetShaftSpec(bool tine) {
    return tine ? MalletSpec{0.005, 1.0e8, 1.5, 0.0, 0.0, 1.5, 0.30, 0.02, 0.0, 0.30}
                : MalletSpec{0.027, 2.0e8, 1.5, 0.0, 0.0, 1.5, 0.10, 0.02, 0.0, 0.50};
}

// Kalimba thumb techniques: the tine is pushed down by the thumb, then
// released as the thumb slips off. A player presses with a force (about the
// same whatever touches the tine), so short stiff treble tines deflect less
// than long bass ones. Flesh lets go gradually, a nail or pick snaps free.
// The usual pluck, where the thumb's nail meets its flesh, snaps nearly as
// fast as a nail: 0.1 s after it a box alto's first overtone sits 17-26 dB
// under the fundamental (Holdaway's spectra of F#4 and C5); with 0.2 ms here
// 27 dB at F#4 and 36 at C5 (a 0.4 ms roll-off left 33 and 44).
// The thumb pad stops whatever the tine was still doing.
struct ThumbSpec {
    double releaseSeconds;   // slip time at Hardness 0.5
    double releaseShape;     // R(t) = 1 - (t/T)^shape: larger = more abrupt end
    double forceN;           // thumb force at full velocity
    double residual;         // fraction of the previous ring left by the thumb
    double click;            // nail/pick contact click
};

inline ThumbSpec GetThumbSpec(int type) {
    switch (std::clamp(type, 0, 3)) {
        case 0:  return {1.6e-3, 1.6, 3.1, 0.004, 0.0};
        case 2:  return {0.12e-3, 3.0, 3.0, 0.015, 0.45};
        case 3:  return {0.07e-3, 4.0, 3.0, 0.025, 0.80};
        case 1:
        default: return {0.2e-3, 2.2, 3.0, 0.008, 0.15};
    }
}

// ---------------------------------------------------------------------------
// Per-strike event probabilities (Artifacts) and restrike/hand damping.
// ---------------------------------------------------------------------------
struct InstrumentResponseProfile {
    double decayExponent;           // pow(Decay multiplier, exponent); unity stays unity
    // Per-strike event probabilities at maximum Artifacts. A zero Artifacts
    // amount disables all of these events.
    float reboundProbability;       // secondary mallet contact / thumb recatch
    float collisionProbability;     // rail, cord or bridge contact
    float grazingProbability;       // weak, off-centre main stroke / incomplete pluck
    float neighborBrushProbability; // unintended adjacent bar / tine excitation
    float reboundDelayMs;
    float collisionRatio;           // short mount-ring frequency / frameBodyFreq
    float collisionDecaySec;        // mount-ring T60, independent of pitched sustain
    float collisionLevel;           // mechanical impact level relative to strike drive
};

inline InstrumentResponseProfile GetInstrumentResponseProfile(EMarimbaModel model) {
    switch (model) {
        case EMarimbaModel::MayanPadauk:
            return {0.9, 0.40f, 0.62f, 0.28f, 0.22f, 10.0f, 9.7f, 0.024f, 0.110f};
        case EMarimbaModel::BalafonAncestral:
            return {0.9, 0.48f, 0.78f, 0.34f, 0.28f, 19.0f, 7.2f, 0.068f, 0.150f};
        case EMarimbaModel::KalimbaArtisan:
            return {1.0, 0.18f, 0.70f, 0.35f, 0.42f, 7.0f, 6.4f, 0.095f, 0.100f};
        case EMarimbaModel::ImperialRosewood:
        default:
            return {1.0, 0.32f, 0.36f, 0.20f, 0.16f, 14.0f, 8.3f, 0.038f, 0.080f};
    }
}

struct ModelParams {
    const char* name;
    const char* woodDescription;
    const char* resonatorDescription;
    double clackFrequency;    // centre of the unmodelled impact noise (Hz)
    double clackBandwidth;
    double frameBodyFreq;     // rail / frame / board support resonance (Hz)
    double frameBodyQ;
    double handDampRate;      // transport-stop hand damping (1/s)
    double releaseDampRate;   // note-off damping in Damp mode (1/s)
    double radiationGain;     // calibration of direct bar/tine radiation
    double airGain;           // calibration of the resonator mouth
    double pickupGain;        // calibration of the contact transducer strain
    // Shared-source radiation, separate from the mechanical bar/air junction.
    // Applied to new excitation so existing tails survive live model changes.
    double frameRadiationGain;
    double sympatheticRadiationGain;
};

inline ModelParams GetModelParams(EMarimbaModel model) {
    switch (model) {
        case EMarimbaModel::MayanPadauk:
            // Keep the box fundamental below its former dominant balance;
            // the coupled-pitch calibration uses this same mouth readout.
            // The neighbor return follows the quieter voice at full Sympathetic.
            return {"Mayan Padauk 4.3", "Hormigo (Platymiscium) bars",
                    "Tapered cedar boxes with charleo membranes",
                    2800.0, 1600.0, 110.0, 1.2, 14.0, 22.0, 1.0, 1.6, 1.0, 0.125, 0.625};
        case EMarimbaModel::BalafonAncestral:
            // Leave room for the key beneath the gourd; pitch calibration uses
            // this same radiation balance before tuning the coupled system.
            return {"Balafon Ancestral", "Fire-cured vene (Pterocarpus erinaceus) keys",
                    "Calabash gourds with spider-silk mirlitons",
                    4800.0, 3200.0, 145.0, 1.2, 12.0, 20.0, 1.0, 7.0, 2.0,
                    0.3535533905932738, 0.7071067811865476};
        case EMarimbaModel::KalimbaArtisan:
            return {"Kalimba Artisan 17-Key", "Spring-steel tines on a hardwood bridge",
                    "Hollow acacia soundbox with front and rear holes",
                    5400.0, 3500.0, 245.0, 1.6, 22.0, 30.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        case EMarimbaModel::ImperialRosewood:
        default:
            // Mechanical tube loading remains; pitch calibration follows this
            // restrained acoustic mouth balance.
            return {"Imperial Rosewood 5.0", "Honduran rosewood (Dalbergia stevensonii) bars",
                    "Drawn brass tubes, matched to their bars",
                    4200.0, 2800.0, 86.0, 1.2, 15.0, 24.0, 1.0, 0.1648288920159558, 1.0,
                    0.03162277660168379, 0.125};
    }
}

// Hand-made instruments are not uniform. A fixed per-key scatter (identical on
// every strike, reset and rate) perturbs what makers do not tune exactly:
// untuned partials beyond the carved profile's own, torsion, resonator
// alignment and loss. The fundamental keeps its exact tuning.
struct KeyVariation {
    double partial[kNumPitchedModes]; // multiplies ratio of mode m (m >= 1)
    double torsion;                   // multiplies both torsional ratios
    double resonatorCents;            // cavity alignment error
    double decayScale;                // wood/steel loss scatter
};

inline KeyVariation GetKeyVariation(EMarimbaModel model, int noteNumber) {
    uint32_t state = static_cast<uint32_t>(noteNumber & 127) * 0x9E3779B1u
                   ^ (static_cast<uint32_t>(model) + 1u) * 0x85EBCA6Bu;
    const auto next = [&state]() {
        // splitmix32: a fixed, well-mixed value per key, mapped to [-1, 1).
        state += 0x9E3779B9u;
        uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        z ^= z >> 15;
        return static_cast<double>(z >> 8) * (2.0 / 16777216.0) - 1.0;
    };
    double tuned = 0.003, untuned = 0.008, torsion = 0.04, cents = 1.0, decay = 0.04;
    int tunedModes = 3;                  // 1 : 4 : 10
    switch (model) {
        case EMarimbaModel::MayanPadauk:
            tuned = 0.006; untuned = 0.012; torsion = 0.05; cents = 8.0; decay = 0.08;
            break;
        case EMarimbaModel::BalafonAncestral:
            // The carved profile already differs per key; this is the rest.
            tuned = 0.012; untuned = 0.02; torsion = 0.06; cents = 30.0; decay = 0.12;
            tunedModes = 1;
            break;
        case EMarimbaModel::KalimbaArtisan:
            tuned = untuned = 0.015; torsion = 0.0; cents = 0.0; decay = 0.06;
            tunedModes = 1;
            break;
        default:
            break;
    }
    KeyVariation variation{};
    variation.partial[0] = 1.0;
    for (int m = 1; m < kNumPitchedModes; ++m)
        variation.partial[m] = 1.0 + next() * (m < tunedModes ? tuned : untuned);
    variation.torsion = 1.0 + next() * torsion;
    variation.resonatorCents = next() * cents;
    variation.decayScale = 1.0 + next() * decay;
    return variation;
}

} // namespace maremba
