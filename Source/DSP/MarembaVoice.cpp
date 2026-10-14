#include "MarembaVoice.h"
#include <type_traits>
#include "BarModes.h"
#include "KalimbaMountShapes.h"
#include "DspMath.h"
#include "ModalExcitation.h"
#include "ContactExcitation.h"
#include "KalimbaBody.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <initializer_list>

namespace maremba {

namespace {

// A voice is freed once the summed squared amplitude of its resonators falls
// below this (-120 dB) and no excitation or noise burst is pending.
constexpr double kSilenceEnergy = 1.0e-12;
// A single resonator below this squared amplitude (-300 dB) is flushed to 0
// and skipped until the next strike.
constexpr double kFlushEnergy = 1.0e-30;
// Longest T60 in seconds (a time cap keeps decay independent of the rate).
constexpr double kMaxT60Seconds = 60.0;
// Modes at or above this fraction of the internal rate are dropped, and every
// mode above 19 kHz (inaudible, and the same set at every rate from 44.1 kHz
// up, so the contact and the outputs do not change with the host rate or
// quality).
constexpr double kModeDropFraction = 0.45;
constexpr double kHighestModeHz = 19000.0;
// Air modes per resonator that take part (see Trigger).
constexpr int kAirModes = 1;
inline double ModeLimit(double fs) { return std::min(fs * kModeDropFraction, kHighestModeHz); }
// Highest pole angle a retuned (pitch-bent) resonator may reach.
constexpr double kMaxOmega = 0.98 * kPiD;
// A voice whose summed squared resonator amplitude exceeds this (+40 dB, far
// above any strike) is unstable and is freed (defence in depth).
constexpr double kMaxVoiceEnergy = 1.0e4;

// Physical units of the readouts, chosen so a mezzo-forte C4 sits where the
// earlier engine did; per-model calibration lives in ModelParams.
constexpr double kRadiationUnit = 3.32e-4;  // close/far pressure per (m/s^2) of bar surface
constexpr double kAirUnit = 3.32e-4;        // pressure per unit air state x omega^2
// Voiced acoustic balance, not additional mechanical damping or a measured
// radiation model. The fundamental, tuned first upper partial and cavity
// retain their body; higher bending and transverse pressure no longer dwarf
// them on hard contacts. Apply the same pressure weight at every microphone.
// Contact motion, modal energy, decay, cavity exchange and pickup strain keep
// their physical paths; these fixed readout weights never follow signal level.
constexpr double kUpperBendingRadiationGain = 0.08;
constexpr double kTransverseRadiationGain = 0.15;
constexpr double kStrainUnit = 640.0;       // nominal pickup units at the C4 strain reference, not coulombs
constexpr double kSupportUnit = 4.0e-3;     // support force per (m/s) at the cords
constexpr double kFrameDriveUnit = 1100.0;  // frame/mesh drive per unit support force
constexpr double kBrushLevel = 0.18;
constexpr double kPickupForceUnit = 9.0e-4;  // pickup charge per newton of contact force
constexpr double kTineBridgeStrain = 0.028;  // board strain per newton of bridge reaction
constexpr double kPickupSupportUnit = 300.0; // rail motion reaching the transducer
// Mallet speed at full velocity (m/s); the curve keeps the engine's loudness law.
constexpr double kMaxMalletSpeed = 3.0;

// Buzz membranes per instrument. Balafon: two or three spider-silk mirlitons
// per gourd with staggered thresholds (Godlovitch et al. 2012: 2-5 per gourd,
// components near 1.2-1.8 kHz); Guatemalan charleo: one pig-gut membrane at
// the closed bottom of the box, where the pressure is highest, buzzing at mf
// and leaning to odd harmonics; Rosewood: a weak add-on. Voiced constants.
// `drain` is the air energy each unit of slap loss costs: enough that a loose
// membrane clamps the resonator near its threshold on loud strokes (up to
// about 3 dB of the fundamental at full Buzz), little enough that Buzz stays
// an added layer (within 1.5 dB up to the factory 0.75 at mezzo-forte).
struct MembraneVoicing {
    int count;
    double formantHz[3], gap[3], asymmetry[3], drive[3], radiation[3];
    double quality, stiffness, slapLoss, drain, slackShare, pressureScale, slapNoise;
    double slapJitter;   // cycle-to-cycle spread of where a membrane goes taut
};

inline MembraneVoicing GetMembraneVoicing(EMarimbaModel model, int noteNumber) {
    switch (model) {
        case EMarimbaModel::BalafonAncestral: {
            // Two or three membranes per gourd, fixed per key. The top of a
            // balafon (a three-octave instrument reaches about C6) is meant to
            // sound dull and carries no membrane (Voisin 1993).
            const int count = noteNumber >= 84 ? 0 : ((noteNumber * 7) % 5) < 2 ? 2 : 3;
            return {count, {1150.0, 1550.0, 2050.0}, {0.22, 0.30, 0.40}, {0.25, 0.12, 0.35},
                    {3.4, 2.9, 2.5}, {2.4, 2.0, 1.6}, 2.5, 8.0, 20.0, 0.0006, 0.015, 1.0, 0.35, 0.15};
        }
        case EMarimbaModel::MayanPadauk:
            return {1, {720.0, 0.0, 0.0}, {0.88, 0.0, 0.0}, {0.06, 0.0, 0.0},
                    {2.6, 0.0, 0.0}, {0.27, 0.0, 0.0}, 1.8, 8.0, 20.0, 0.0002, 0.015, 1.6, 0.18, 0.1};
        case EMarimbaModel::KalimbaArtisan:
            return {0, {}, {}, {}, {}, {}, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        case EMarimbaModel::ImperialRosewood:
        default:
            return {1, {900.0, 0.0, 0.0}, {0.45, 0.0, 0.0}, {0.30, 0.0, 0.0},
                    {1.4, 0.0, 0.0}, {1.0, 0.0, 0.0}, 2.0, 8.0, 20.0, 0.0004, 0.03, 1.0, 0.08, 0.1};
    }
}

// Odd harmonics from the flow through the gap between a bar and its tube's
// rim. An open end driven hard loses energy to flow separation, a pressure
// drop of 1/2 rho K u|u| whose odd harmonics radiate (Ingard & Ising 1967;
// Atig, Dalmont & Gilbert 2004); under a bar the gap concentrates it. In
// anechoic recordings of a concert marimba (University of Iowa MIS, 2013,
// cord mallets, 1.5 m) the keys from B2 to Eb4 carry lines at 3 f0 and 5 f0
// (median 2 cents off, at most 5, while the key a twelfth up sits up to 9
// cents away): the third 28-55 dB below the fundamental at mf and ff (median
// -33), 45-65 at pp, the fifth about 11 dB below the third, both dying 2-3
// times faster than the fundamental in dB (a quadratic law). Below B2 they
// fall to about -52 dB and from E4 up below -60. Level of the third harmonic
// re the fundamental at a mouth velocity of 1 m/s, by MIDI note (fitted to
// those recordings), and the fifth's level re the third.
struct RimHarmonicAnchor { double midi, db; };
constexpr RimHarmonicAnchor kRimHarmonic3[] = {{36, -48}, {45, -36}, {48, -28}, {57, -31}, {60, -31}, {62, -35}, {64, -55}, {66, -70}};
constexpr double kRimHarmonic5Db = -11.0;
inline double RimHarmonic3Db(double midi) {
    constexpr int n = static_cast<int>(sizeof(kRimHarmonic3) / sizeof(kRimHarmonic3[0]));
    if (midi <= kRimHarmonic3[0].midi) return kRimHarmonic3[0].db;
    for (int i = 1; i < n; ++i)
        if (midi <= kRimHarmonic3[i].midi) {
            const double t = (midi - kRimHarmonic3[i - 1].midi) / (kRimHarmonic3[i].midi - kRimHarmonic3[i - 1].midi);
            return kRimHarmonic3[i - 1].db + t * (kRimHarmonic3[i].db - kRimHarmonic3[i - 1].db);
        }
    return -200.0;
}

// Resonator pressure reaching the membranes, in the voice's air-state units.
constexpr double kMembraneDrive = 2.5;
// Charleo above C4: the extra fall of its gap with frequency (the box's
// taper; see Trigger) and the rise of its formant (a membrane's resonance
// goes as 1 / its radius; the treble holes are smaller).
constexpr double kCharleoTrebleGapExponent = 1.0;
constexpr double kCharleoTrebleFormantExponent = 0.35;
inline double MembraneFormantScale(EMarimbaModel model, double f0) {
    const double r = f0 / 261.625565;
    return std::pow(r, 0.15) * (model == EMarimbaModel::MayanPadauk ? std::pow(std::max(1.0, r), kCharleoTrebleFormantExponent) : 1.0);
}

// Radiated sizzle per unit buzzer ring amplitude (and its knock into the pickup).
constexpr double kBuzzerRadiation = 0.5;
// Board motion reaching the buzzers per newton of bridge force. The board
// moves continuously under a pluck (the thumb's press has settled before the
// release), so the buzzers answer the ring and its tink: the default Buzz
// stays clean, loose buzzers sizzle on firm plucks while the note is loud,
// more with a nail or pick and toward the treble.
constexpr double kBuzzerDrive = 0.19;
// A loose mounting's knock relative to the bar's own radiation per unit of
// fundamental swing, and a tine's knock into the bridge per unit of its
// fundamental's bridge force.
constexpr double kCollisionLevel = 1.0;
constexpr double kTineCollisionBridge = 1.0;
// A tine's bridge force as the box radiates it, for voice stealing.
constexpr double kTineLoudness = 0.07;
// The shaft's knock: its bending modes per unit contact impulse and their
// sound (a thin rod: a weak radiator, heard in the attack).
constexpr double kShaftDrive = 1.0;
constexpr double kShaftRadiation = 0.03;

// Dead stroke: after the stroke the hand keeps the head pressed into the bar
// ("hold it firmly into the bar", Otte). No hold force is published; 5 N is a
// firm share of a stroke. The pressed head's hysteresis takes c = lambda F
// (1-3 N s/m); the grip on the stick adds a damper on the head.
constexpr double kDeadHoldForce = 5.0;
constexpr double kDeadGrip = 4.0;
// A hand muffling a bar (Damp note-off): a damper of 24 N s/m on the bar
// under it (palm tissue: 17-73 N s/m, Dong et al. 2013). A tube under
// a stopped bar would ring on at its own Q; the hand over the bar and its
// gap takes it down to about Q 35, so a muffled low E decays 4-5 times
// faster, as Burritt's do (Schutz & Lipscomb 2007). Transport stop damps
// more gently. The palm lands softly: about 20 g of skin at 0.1 m/s,
// restitution 0.44, over 3 ms (Jindrich 2003) - a quiet thud.
constexpr double kPalmDamping = 24.0;
constexpr double kPalmAirQ = 35.0;
// The hand lies across the bar from about its quarter point toward the
// middle (palm and fingers, 18 cm), so every low mode has motion under it.
constexpr double kHandSpanM = 0.18;
constexpr double kHandCentre = 0.37;
constexpr double kPalmImpulse = 0.020 * 0.1 * 1.44;
constexpr double kPalmPulseSeconds = 0.003;
constexpr double kTransportDampShare = 0.7;
// A thumb laid flat on a tine to stop it ("lay the flat thumb flesh on the
// tine quickly and it will stop sounding"): a light fingertip touch is 1-2
// kN/m and 0.75-2.4 N s/m (Wiertlewski & Hayward 2012) and stops a tine in
// 5-15 ms; a thumb presses harder, 1 N, and only chatters while the tine
// still swings further than that. The touch lands over 6 ms (Jindrich et al.
// 2003: fingertip force transients take 5 ms or more). A fingertip
// left resting after a dead tap presses lightly (0.3 N).
constexpr double kFingerStiffness = 3000.0;
constexpr double kFingerDamping = 2.4;
constexpr double kFingerTouch = 0.3;
constexpr double kThumbRestTouch = 1.0;
constexpr double kFingerLandingSeconds = 0.006;
// After a dead pluck the thumb, which slipped off the tip, is back on the
// tine this long after it let go: a short pitched tick.
constexpr double kThumbReturnSeconds = 0.03;
// The board's quasi-static return from the thumb's press: slow against every
// tine period (C3: 7.6 ms), fast against a note.
constexpr double kPreloadBleedSeconds = 0.04;
// Voiced engineering travel limit, not a measured specimen dimension. The
// finite tine geometry must not turn a fixed thumb force into metre-scale
// bends when MIDI extrapolates its fundamental below the audible register.
constexpr double kMaxThumbTravel = 0.008;

// sin and cos of an angle, by their series below 0.01 rad (a bar-air
// rotation per sample; exact to double precision there), else by libm.
inline void SinCosSmall(double a, double& s, double& c) {
    if (std::abs(a) < 0.01) {
        const double a2 = a * a;
        s = a * (1.0 - a2 / 6.0 * (1.0 - a2 / 20.0 * (1.0 - a2 / 42.0)));
        c = 1.0 - a2 / 2.0 * (1.0 - a2 / 12.0 * (1.0 - a2 / 30.0));
    } else {
        s = std::sin(a);
        c = std::cos(a);
    }
}

// Two-pole resonant noise filter, with unity gain at its specified centre at
// every rate. The all-pole response, bandwidth and ringing poles are retained.
inline void BandPass(double hz, double bandwidth, double fs, double& c, double& s, double& b) {
    const double f = std::min(hz, fs * 0.45);
    const double q = std::max(0.3, f / std::max(bandwidth, 1.0));
    const double r = std::clamp(std::exp(-kPiD * f / (q * fs)), 0.0, 0.9999);
    const double w = kTwoPiD * f / fs;
    c = 2.0 * r * std::cos(w);
    s = r * r;
    b = ResonatorDenominatorAt(r, w);
}

} // namespace

MarembaVoice::MarembaVoice()
    : mLocalPrng(0x56789ABC)
{
    FastKill();
}

void MarembaVoice::FastKill() {
    mActive = false;
    mReleased = false;
    mDamping = false;
    mFading = false;
    mNoteNumber = -1;
    mVelocity = 0.0f;
    mEnergy = 0.0;
    mPitchRatio = 1.0;
    mContact.Reset();
    mContactForce = mPreviousForce = 0.0;
    mTextureState = 0.0;
    mReleaseSample = mReleaseSamples = 0;
    mThumbPending = false;
    mLoudness = 0.0;
    mReleaseForce = 0.0;
    mBridgeSample = 0.0;
    mPreloadOffset = 0.0;
    mFadeGain = 1.0f;
    mFadeStep = 0.0f;
    for (auto& membrane : mMembranes) membrane.Reset();
    for (auto& taut : mMembraneTaut) taut = false;
    mNumMembranes = 0;
    mHasMirliton = false;
    mSlapNoise = mSlapY1 = mSlapY2 = 0.0;
    mBuzzer.Reset();
    mArtifactSample = mArtifactEnd = 0;
    mReboundStart = -1;
    mReboundVelocity = mBrushImpulse = 0.0;
    mCollisionEnvelope = 0.0;
    mBrushActive = mCollisionActive = false;
    mCollisionPrevious = mCollisionImpulse = 0.0;
    mCollisionSide = mCollisionSample = mCollisionSamplesTotal = 0;
    mMechanicalSample = 0.0f;
    for (auto& mode : mBrushModes) mode = ModeState{};
    for (auto& mode : mCollisionModes) mode = ModeState{};
    for (double& sine : mBrushSin) sine = 0.0;
    for (double& sine : mCollisionSin) sine = 0.0;
    mPickup.Reset();
    mClackBpY1 = mClackBpY2 = 0.0;
    mClickGain = 0.0;
    for (int i = 0; i < kNumPitchedModes; ++i) {
        mModes[i] = ModeState{};
        mRadiation[i] = ModalRadiationReadout{};
        mRadiationHz[i] = 0.0;
        mRadiationGain[i] = mStrainGain[i] = mSupportGain[i] = mBridgeGain[i] = 0.0;
        mSurfaceStrainGain[i] = 0.0;
    }
    mNumModes = 0;
    for (auto& mode : mTorsion) mode = ModeState{};
    mNumTorsionModes = 0;
    mLateral = ModeState{};
    mHasLateral = false;
    mTube = ModeState{};
    for (auto& mode : mUpperCavity) mode = ModeState{};
    mNumCavityModes = 0;
    mCavityAngleRamp = false;
    mResReadoutGain = 0.0;
    mRimHarmonic3 = mRimHarmonic5 = mRimGain3 = mRimGain5 = mRimLevel3 = mRimLevel5 = 0.0;
    mDeadStroke = false;
    mSupportLoad = 0.0;
    mFingerOn = mFingerPending = false;
    mFingerPreload = mFingerCompression = 0.0;
    mHandModes = false;
    mHandPulseSample = mHandPulseSamples = 0;
    for (auto& mode : mShaftModes) mode = ModeState{};
    for (double& sine : mShaftSin) sine = 0.0;
    mShaftActive = false;
}

void MarembaVoice::Release() {
    mReleased = true;
    if (!mActive) return;
    // A dead stroke's head (or a resting thumb) lifts off with the key.
    if (mContact.Holding()) mContact.Lift();
    if (mDeadStroke && mTine && !mDamping) mFingerOn = mFingerPending = false;
}

void MarembaVoice::SetModeCoefficients(ModeState& mode, double omega) const {
    const double w = std::clamp(omega, 1.0e-9, kMaxOmega);
    const double sinW = std::sin(w);
    mode.w = w;
    mode.cosW = std::cos(w);
    mode.invSin = 1.0 / sinW;
    mode.cosOverR = mode.r > 0.0 ? mode.cosW / mode.r : 0.0;
    mode.sinOverR = mode.r > 0.0 ? 1.0 / (mode.invSin * mode.r) : 0.0;
    mode.c = 2.0 * mode.r * mode.cosW;
    mode.s = mode.r * mode.r;
    mode.invSin2 = 1.0 / (sinW * sinW);
}

// A direct-form state pair (y1, y2) only means an amplitude and a phase for one
// pole angle. Swapping c alone scales the ring by a phase-dependent factor, and
// a periodic bend then pumps it parametrically. So the delayed state is
// re-expressed for the new angle: with y1 = A cos(phi) and
// r*y2 = A cos(phi - w), A sin(phi) = (r*y2 - y1*cos(wOld)) / sin(wOld) and
// y2' = (y1*cos(wNew) + A sin(phi) * sin(wNew)) / r.
void MarembaVoice::RetuneMode(ModeState& mode, double omega) const {
    ReconfigureMode(mode, mode.r, omega);
}

void MarembaVoice::ReconfigureMode(ModeState& mode, double radius, double omega) const {
    const double wOld = mode.w;
    const double rOld = mode.r;
    mode.r = radius;
    SetModeCoefficients(mode, omega);
    const double r = mode.r;
    const double sinOld = std::sin(wOld);
    if (!(r > 0.0) || sinOld < 1.0e-12 || !(rOld > 0.0)) return;
    const double cosOld = std::cos(wOld);
    const double cosNew = std::cos(mode.w);
    const double k = std::sin(mode.w) / sinOld;
    mode.y2 = (mode.y1 * cosNew + (rOld * mode.y2 - mode.y1 * cosOld) * k) / r;
}

// Cord/felt loading on bars and retaining-bar loading on tines. This is a
// bounded voiced extension of the existing losses, not a measured support
// law: tighter supports absorb more energy, especially from upper modes.
// At the centre retain the old pole bit-for-bit. No pitch change or per-sample
// work is needed; live updates preserve each mode's displacement and phase.
double MarembaVoice::TensionRadius(int mode, float tension) const {
    if (tension == 0.5f) return mNeutralModeRadius[mode];
    const double span = mTine ? 1.15 : mModel == EMarimbaModel::BalafonAncestral ? 1.35
                                  : mModel == EMarimbaModel::MayanPadauk ? 1.1 : 0.8;
    const double upperWeight = 1.0 + 0.25 * std::min(mode, 4);
    const double loss = mNeutralModeLoss[mode] * std::exp2((tension - 0.5) * span * upperWeight);
    return std::exp(-std::max(loss, 6.907755 / (kMaxT60Seconds * mSampleRate)));
}

// For y[n] = A r^n cos(wn + phi): y1^2 - c*y1*y2 + s*y2^2 = (A r^n sin w)^2, so
// dividing by sin^2 w gives the squared amplitude independent of the phase.
double MarembaVoice::ModeEnergy(const ModeState& mode) {
    const double q = mode.y1 * mode.y1 - mode.c * mode.y1 * mode.y2 + mode.s * mode.y2 * mode.y2;
    return q * mode.invSin2;
}

double MarembaVoice::FlushedModeEnergy(ModeState& mode) {
    const double e = ModeEnergy(mode);
    if (e < kFlushEnergy) mode.y1 = mode.y2 = 0.0;
    return e;
}

double MarembaVoice::MembraneEnergy() const {
    double energy = mBuzzer.Energy();
    for (int i = 0; i < mNumMembranes; ++i) energy += mMembranes[i].Energy();
    return energy + mSlapY1 * mSlapY1 + mSlapY2 * mSlapY2;
}

bool MarembaVoice::HasNoSubnormalState() const {
    auto clean = [](const ModeState& mode) {
        for (double v : { mode.y1, mode.y2 }) {
            if (std::fpclassify(v) == FP_SUBNORMAL) return false;
        }
        return true;
    };
    for (int m = 0; m < kNumPitchedModes; ++m) if (!clean(mModes[m])) return false;
    for (const auto& mode : mBrushModes) if (!clean(mode)) return false;
    for (const auto& mode : mCollisionModes) if (!clean(mode)) return false;
    for (const auto& mode : mUpperCavity) if (!clean(mode)) return false;
    for (const auto& mode : mTorsion) if (!clean(mode)) return false;
    for (const auto& membrane : mMembranes) if (!membrane.HasNoSubnormalState()) return false;
    if (!mBuzzer.HasNoSubnormalState()) return false;
    return clean(mLateral) && clean(mTube) && mPickup.HasNoSubnormalState();
}

double MarembaVoice::ComputeEnergy() const {
    if (!mActive) return 0.0;
    double energy = 0.0;
    for (int m = 0; m < mNumModes; ++m) energy += ModeEnergy(mModes[m]);
    for (int n = 0; n < mNumTorsionModes; ++n) energy += ModeEnergy(mTorsion[n]);
    if (mHasLateral) energy += ModeEnergy(mLateral);
    energy += ModeEnergy(mTube);
    for (const auto& mode : mUpperCavity) energy += ModeEnergy(mode);
    for (const auto& mode : mBrushModes) energy += ModeEnergy(mode);
    for (const auto& mode : mCollisionModes) energy += ModeEnergy(mode);
    return energy + mPickup.StateEnergy() + MembraneEnergy();
}

void MarembaVoice::StartDamping() {
    if (!mActive) return;
    const double pole = std::exp(-mHandDampRate / mSampleRate);
    if (!mDamping || pole < mHandDampPole) mHandDampPole = pole;
    mDamping = true;
    StartHand(kTransportDampShare);
}

void MarembaVoice::StartReleaseDamping() {
    if (!mActive) return;
    const double pole = std::exp(-mReleaseDampRate / mSampleRate);
    if (!mDamping || pole < mHandDampPole) mHandDampPole = pole;
    mDamping = true;
    StartHand(1.0);
}

// A palm on a bar: each bending mode loses c <phi^2> / (2 m) per second
// (mean square under the palm, modal mass m), torsion and lateral bending
// like the fundamental, the air down to Q kPalmAirQ.
// A fingertip on a tine is a contact force (FingerForce). A stronger hand
// replaces a gentler one.
void MarembaVoice::StartHand(double strength) {
    if (mTine) {
        if (mFingerOn && mFingerTarget >= kThumbRestTouch / kFingerStiffness * strength) return;
        if (!mFingerOn) mFingerCompression = -ContactDisplacement();   // lands on the tine where it is
        mFingerOn = true;
        mFingerPending = false;
        mFingerTarget = kThumbRestTouch / kFingerStiffness * strength;
        mFingerStiffness = kFingerStiffness;
        mFingerDamping = kFingerDamping * strength;
        mFingerRamp = 1.0 - std::exp(-1.0 / (kFingerLandingSeconds * mSampleRate));
        return;
    }
    const double c = kPalmDamping * strength;
    bool stronger = !mHandModes;
    double pole[kNumPitchedModes];
    for (int m = 0; m < mNumModes; ++m) {
        const double scale = kStateScale * mModes[m].toDisplacement;   // 1 / sqrt(modal mass)
        pole[m] = std::exp(-0.5 * c * mHandShape[m] * scale * scale / mSampleRate);
        stronger = stronger || pole[m] < mHandPole[m];
    }
    if (!stronger) return;
    for (int m = 0; m < mNumModes; ++m) mHandPole[m] = pole[m];
    mHandTorsionPole = mNumModes > 0 ? pole[0] : 1.0;
    // The air under the hand: at most Q kPalmAirQ. Below hearing, a
    // constant-Q loss approaches zero and can keep a stopped MIDI-0 voice's
    // membranes alive for tens of seconds. Bound that hand-damped lifetime
    // at the 20 Hz loss; every audible-range pole keeps its existing value.
    const double airHz = std::max(20.0, mTube.omega * mSampleRate / kTwoPiD);
    const double airLoss = mTube.r > 0.0 ? -std::log(mTube.r) * mSampleRate : 0.0;
    mHandAirPole = std::exp(-std::max(0.0, kPiD * airHz / kPalmAirQ - airLoss) * strength / mSampleRate);
    if (!mHandModes) {
        // The palm's landing: a soft pulse (through the stroke's point).
        mHandPulseImpulse = kPalmImpulse * strength;
        mHandPulseSamples = std::max(2, static_cast<int>(std::lround(kPalmPulseSeconds * mSampleRate)));
        mHandPulseStep = 1.0 / mHandPulseSamples;
        mHandPulseSample = 0;
    }
    mHandModes = true;
}

void MarembaVoice::StartFade(double seconds) {
    if (!mActive) return;
    const float step = static_cast<float>(1.0 / std::max(1.0, seconds * mSampleRate));
    if (!mFading || step > mFadeStep) mFadeStep = step;
    mFading = true;
}

void MarembaVoice::Retune(float globalCents) {
    if (!mActive) return;
    mPitchRatio = std::exp2(static_cast<double>(globalCents - mTriggerGlobalCents) / 1200.0);
    for (int m = 0; m < mNumModes; ++m) RetuneMode(mModes[m], mModes[m].omega * mPitchRatio);
    for (int n = 0; n < mNumTorsionModes; ++n) RetuneMode(mTorsion[n], mTorsion[n].omega * mPitchRatio);
    if (mHasLateral) RetuneMode(mLateral, mLateral.omega * mPitchRatio);
    RetuneMode(mTube, mTube.omega * mPitchRatio);
    for (auto& mode : mUpperCavity) if (mode.r > 0.0) RetuneMode(mode, mode.omega * mPitchRatio);
    for (int i = 0; i < 2; ++i) {
        auto& mode = mBrushModes[i];
        if (mode.r > 0.0) RetuneMode(mode, mode.omega * mPitchRatio);
        mBrushSin[i] = std::sin(mode.w);
    }
    UpdateContactCompliance();
    UpdateCavityCoupling();
}

// Bar displacement at the contact per newton delivered within one sample, and
// the per-mode force gains, at the modes' current pole angles.
void MarembaVoice::UpdateContactCompliance() {
    double compliance = 0.0;
    for (int m = 0; m < mNumModes; ++m) {
        auto& mode = mModes[m];
        const double omegaSeconds = mode.w * mSampleRate;
        // Impulse J moves the state by S^2 * toDisplacement * phi * sin(w) / omega * J.
        mode.force = kStateScale * kStateScale * mode.toDisplacement * mode.contact * std::sin(mode.w) / omegaSeconds;
        compliance += mode.contact * mode.toDisplacement * mode.force * mDt;
    }
    mContactCompliance = compliance;
}

void MarembaVoice::UpdateCavityCoupling() {
    // The coupling scales with the pitch, like every frequency of the system.
    for (int i = 0; i < mNumCavityModes; ++i) {
        mCavityAngleTarget[i] = mCavityRate[i] * mPitchRatio * mDt;
        if (!mCavityAngleRamp) mCavityAngle[i] = mCavityAngleTarget[i];
        mCavityCos[i] = std::cos(mCavityAngle[i]);
        mCavitySin[i] = std::sin(mCavityAngle[i]);
    }
}

// A maker tunes each bar on its resonator. The tube and the bar's
// fundamental form two hybrid modes whose frequencies the air pulls apart, so
// the bar alone would sound a few cents off once the air has answered. Per
// pair, with lambda = -alpha + i omega and coupling g, the modes are
// lambda = (lb + lt) / 2 +- sqrt(((lb - lt) / 2)^2 - (g / 2)^2), each with
// air/bar amplitude w = -i (lambda - lb) / (g / 2). The bar is first set so
// that the bar-like mode (followed from g = 0) lies on the key's pitch; then,
// on the same branch, so that the line a tuner reads after the attack does:
// the close microphone through a Hann window from 0.15 s, 0.3 s long (1 s
// below C3, where the ear integrates longer). Runs at note-on only.
void MarembaVoice::TuneOnResonator() {
    ModeState& bar = mModes[0];
    if (mTine || mNumModes == 0 || mNumCavityModes == 0 || mCavityBarIndex[0][0] != 0
        || !(mCavityRate[0] > 0.0) || !(bar.r > 0.0 && bar.r < 1.0) || !(mTube.r > 0.0 && mTube.r < 1.0)) return;
    using Complex = std::complex<double>;
    const double fs = mSampleRate;
    const double target = bar.omega * fs;
    const Complex tube(std::log(mTube.r) * fs, mTube.omega * fs);
    const double barLoss = std::log(bar.r) * fs;
    const double kappa = 0.5 * mCavityRate[0];
    const auto modes = [&](double omegaBar, Complex lambda[2]) {
        const Complex lb(barLoss, omegaBar);
        const Complex half = 0.5 * (lb - tube);
        const Complex root = std::sqrt(half * half - kappa * kappa);
        lambda[0] = 0.5 * (lb + tube) + root;
        lambda[1] = 0.5 * (lb + tube) - root;
    };
    // 1. The bar-like mode on pitch.
    const auto barLike = [&](double omegaBar) {
        const Complex lb(barLoss, omegaBar);
        Complex mode = lb;
        constexpr int kSteps = 12;
        for (int step = 1; step <= kSteps; ++step) {
            const double k = kappa * step / kSteps;
            const Complex half = 0.5 * (lb - tube);
            const Complex root = std::sqrt(half * half - k * k);
            const Complex a = 0.5 * (lb + tube) + root, b = 0.5 * (lb + tube) - root;
            mode = std::abs(a - mode) <= std::abs(b - mode) ? a : b;
        }
        return mode.imag();
    };
    // Secant on the bar's own frequency (the plain fixed-point step first).
    double omegaBar = target, error = barLike(omegaBar) - target;
    double previous = omegaBar, previousError = error;
    for (int iteration = 0; iteration < 24 && std::abs(error) >= 1.0e-9 * target; ++iteration) {
        const double slope = iteration > 0 ? (error - previousError) / (omegaBar - previous) : 1.0;
        // Far steeper than the branch itself (0.3-2): the last two iterates
        // straddle the jump of a bass key's bar-like mode, so there is no root.
        if (slope > 5.0) break;
        previous = omegaBar;
        previousError = error;
        omegaBar -= std::isfinite(slope) && slope > 0.1 ? error / slope : error;
        error = barLike(omegaBar) - target;
    }
    // A tube within the coupling of its bar's pitch (bass keys) makes the
    // bar-like mode jump across the note: no root. The heard line is still
    // continuous there, so step 2 starts from the untuned bar.
    if (!std::isfinite(omegaBar) || std::abs(error) > 1.0e-4 * target) omegaBar = target;

    // 2. The merged line as heard, refined on the same branch.
    const Complex readBar = mRadiationGain[0] * Complex(mRadiation[0].close.real, mRadiation[0].close.imag);
    const double readAir = mCavityReadout[0];
    constexpr double kStart = 0.15;
    const double length = target < kTwoPiD * 130.8 ? 1.0 : 0.3;
    const double hann = kTwoPiD / length;
    const double span = target * (std::exp2(60.0 / 1200.0) - 1.0);
    const auto heard = [&](double bar) {
        Complex lambda[2];
        modes(bar, lambda);
        const Complex lb(barLoss, bar);
        const Complex w[2] = {Complex(0.0, -1.0) * (lambda[0] - lb) / kappa,
                              Complex(0.0, -1.0) * (lambda[1] - lb) / kappa};
        const Complex split = w[1] - w[0];
        if (!(std::abs(split) > 1.0e-12)) return bar;
        // Struck bar, air at rest: each mode's heard amplitude at the window
        // start and its growth over the window.
        const Complex amplitude[2] = {w[1] / split * (readBar + readAir * w[0]) * std::exp(lambda[0] * kStart),
                                      -w[0] / split * (readBar + readAir * w[1]) * std::exp(lambda[1] * kStart)};
        const Complex growth[2] = {std::exp(lambda[0] * length), std::exp(lambda[1] * length)};
        // The Hann window's transform, 0.5/b - 0.25/(b + ih) - 0.25/(b - ih),
        // in one reciprocal: 0.5 h^2 / (b (b^2 + h^2)).
        const double halfHann2 = 0.5 * hann * hann;
        const auto power = [&](double omega) {
            const Complex turn = std::polar(1.0, -omega * length);
            Complex sum = 0.0;
            for (int k = 0; k < 2; ++k) {
                const Complex base = lambda[k] - Complex(0.0, omega);
                const Complex q = base * (base * base + hann * hann);
                const double n = std::norm(q);
                sum += amplitude[k] * (growth[k] * turn - 1.0) * Complex(halfHann2 * q.real() / n, -halfHann2 * q.imag() / n);
            }
            return std::norm(sum);
        };
        double best = target, bestPower = -1.0;
        for (int i = -10; i <= 10; ++i) {
            const double omega = target + span * i / 10.0;
            const double p = power(omega);
            if (p > bestPower) { bestPower = p; best = omega; }
        }
        double lo = best - span / 10.0, hi = best + span / 10.0;
        constexpr double g = 0.6180339887498949;
        double c = hi - g * (hi - lo), d = lo + g * (hi - lo);
        double pc = power(c), pd = power(d);
        for (int i = 0; i < 16; ++i) {   // to 0.003 cent
            if (pc > pd) { hi = d; d = c; pd = pc; c = hi - g * (hi - lo); pc = power(c); }
            else         { lo = c; c = d; pc = pd; d = lo + g * (hi - lo); pd = power(d); }
        }
        return 0.5 * (lo + hi);
    };
    double x0 = omegaBar, e0 = heard(x0) - target;
    double x1 = x0 - e0, e1 = heard(x1) - target;
    for (int iteration = 0; iteration < 8 && std::abs(e1) > 1.0e-5 * target; ++iteration) {   // 0.02 cent
        const double slope = (e1 - e0) / (x1 - x0);
        if (!(slope > 0.2 && slope < 5.0)) break;   // left the branch
        const double x2 = x1 - e1 / slope;
        x0 = x1; e0 = e1;
        x1 = x2; e1 = heard(x1) - target;
    }
    if (std::isfinite(x1) && std::abs(e1) < std::abs(e0) && std::abs(x1 - omegaBar) < 0.02 * target) omegaBar = x1;

    const double omega = std::clamp(omegaBar / fs, 1.0e-6, kMaxOmega);
    bar.omega = omega;
    ReconfigureMode(bar, bar.r, omega * mPitchRatio);
    UpdateContactCompliance();
}

double MarembaVoice::ContactDisplacement() const {
    double u = 0.0;
    for (int m = 0; m < mNumModes; ++m) u += mModes[m].contact * mModes[m].toDisplacement * mModes[m].y1;
    return u;
}

// A fingertip resting where the thumb plucks, about the tine's pressed rest
// position (so a resting finger leaves no static deflection): a spring and
// damper on the compression d = -u. It pushes back as the tine comes up into
// it but cannot pull harder than its touch lets go: beyond that the tine
// leaves the finger and slaps back, the chatter of a light mute.
double MarembaVoice::FingerForce(const double* predicted, double otherForce) {
    mFingerPreload += mFingerRamp * (mFingerTarget - mFingerPreload);
    double free = mContactCompliance * otherForce;
    for (int m = 0; m < mNumModes; ++m) free += mModes[m].contact * mModes[m].toDisplacement * predicted[m];
    const double k = mFingerStiffness * mFingerPreload / std::max(mFingerTarget, 1.0e-12);
    const double cd = mFingerDamping * mSampleRate * mFingerPreload / std::max(mFingerTarget, 1.0e-12);
    // F = (k + c/dt) d_new - (c/dt) d_old with d_new = -(free + C F).
    double force = -((k + cd) * free + cd * mFingerCompression) / (1.0 + (k + cd) * mContactCompliance);
    // The pad lifts off gradually (soft tissue), not at a corner: a smooth
    // floor at the touch's release, 20 % of the touch wide, that leaves a
    // still tine alone (zero in, zero out).
    const double touch = mFingerStiffness * mFingerPreload;
    if (touch > 0.0) {
        const double width = 0.2 * touch, z = (force + touch) / width;
        const auto softplus = [](double v) { return v > 30.0 ? v : std::log1p(std::exp(v)); };
        force = width * (softplus(z) - softplus(touch / width));
    }
    mFingerCompression = -(free + mContactCompliance * force);
    return force;
}

void MarembaVoice::StartMallet(double velocity, double /*positionOffset*/) {
    mMalletSetup.velocity = velocity;
    mMalletSetup.compliance = mContactCompliance;
    mMalletSetup.dt = mDt;
    for (int m = 0; m < mNumModes; ++m) mModes[m].idle = false;
    mContact.Start(mMalletSetup, ContactDisplacement());
}

void MarembaVoice::Trigger(int noteNumber, float velocity, EMarimbaModel model,
                           float malletHardness, float strikePosition,
                           float resonatorTuneCents, float resonatorCoupling,
                           float decayMultiplier, float buzzAmount, float artifactsAmount,
                           float /*legacyPitchGlideIgnored*/,
                           double sampleRate, FastPRNG& prng,
                           float globalCents, float keyOffsetCents,
                           int malletType, float strikeJitter,
                           float positionOffset,
                           const StrikeOptions& options)
{
    const bool isRestrike = mActive && !mFading && mNoteNumber == noteNumber && mModel == model;
    if (!isRestrike && mActive) FastKill();
    mActive = true;
    mReleased = false;
    mDamping = false;
    mFading = false;
    mFadeGain = 1.0f;
    mFadeStep = 0.0f;
    mNoteNumber = noteNumber;
    mModel = model;
    mTine = model == EMarimbaModel::KalimbaArtisan;
    mStriker = std::clamp(malletType, 0, kStrikerCount - 1);
    const bool shaft = mStriker == kStrikerShaft;
    const bool pluck = mTine && mStriker < kStrikerShaft;   // a thumb on a tine
    mDeadStroke = options.deadStroke;
    mVelocity = std::clamp(velocity, 0.01f, 1.0f);
    mSampleRate = sampleRate > 8000.0 ? sampleRate : kReferenceRate;
    mDt = 1.0 / mSampleRate;
    mNoiseScale = std::sqrt(mSampleRate / kReferenceRate);
    mSlapNoisePole = PoleAtRate(.995, mSampleRate);
    mTriggerGlobalCents = globalCents;
    mPitchRatio = 1.0;

    const double fs = mSampleRate;
    const ModelParams params = GetModelParams(model);
    const auto response = GetInstrumentResponseProfile(model);
    const KeyVariation variation = GetKeyVariation(model, noteNumber);
    const MaterialLoss material = GetMaterialLoss(model);
    mPickup.Configure(fs, model);
    const StrikeCharacter character = GenerateStrikeCharacter(prng, strikeJitter);
    const StrikeArtifacts art = GenerateStrikeArtifacts(prng, mVelocity, malletHardness, artifactsAmount, model);
    mHandDampRate = params.handDampRate;
    mReleaseDampRate = params.releaseDampRate;
    mHandDampPole = std::exp(-mHandDampRate / fs);

    const double totalPitchCents = static_cast<double>(noteNumber - 69) * 100.0
                                 + globalCents + keyOffsetCents + art.pitchJitterCents;
    const double f0 = 440.0 * std::exp2(totalPitchCents / 1200.0);
    mF0 = f0;
    const double midi = 69.0 + totalPitchCents / 100.0;
    const double decayControl = std::clamp(static_cast<double>(decayMultiplier), 0.10, 8.0);
    // Tines already sustain for several seconds at the natural setting.
    // Preserve short/natural decays, then spread only 1x..2x sustain over the
    // remaining knob travel. This also bounds the already-added Decay VOL.
    const double voicedDecay = mTine && decayControl > 1.0
        ? 1.0 + (decayControl - 1.0) / 7.0 : decayControl;
    const double decayScale = std::pow(voicedDecay, response.decayExponent) * variation.decayScale;
    const double hardness = std::clamp(static_cast<double>(malletHardness + art.malletHardnessJitter), 0.0, 1.0);
    const double position = std::clamp(static_cast<double>(strikePosition + positionOffset + art.strikePositionOffset)
                                       + character.positionOffset, 0.03, 0.97);
    const int mt = std::min(mStriker, 3);   // head (or thumb technique) of strikers 0-3
    // Coupling 0.7 is the instrument as built.
    const double couplingShape = std::pow(std::clamp(static_cast<double>(resonatorCoupling), 0.0, 1.0) / 0.7, 0.75);
    // A tine's box: how firmly the bridge ties the tines to it scales what
    // leaves through the bridge (loss into the box, force on it).
    const double bridgeCoupling = 0.6 + 0.4 * couplingShape;
    const double touch = std::pow(static_cast<double>(mVelocity), 1.25)
                       * character.velocityScale * art.impulseScale;
    const double primaryShare = 1.0 - art.brushGain;

    // ----------------------------------------------------------------------
    // Modes, losses and readouts
    // ----------------------------------------------------------------------
    const int previousModes = isRestrike ? mNumModes : 0;
    const float carving = std::clamp(options.carving, 0.0f, 1.0f);
    const bool sameRadiationCarving = mRadiationCarving == carving;
    mRadiationCarving = carving;
    mMembraneTension = std::clamp(options.membraneTension, 0.0f, 1.0f);
    mNumModes = mTine ? kNumTineModes : kNumPitchedModes;
    BarKey key{};
    KalimbaMount mount{};
    double tineLength = 0.0, tineMass = 0.0, tineStiffness = 0.0, bridgeLoss0 = 0.0;
    // The tine's shapes come from the same pinned-bar / yielding-bridge
    // solution as its frequencies (KalimbaMountShapes.h), per key.
    KalimbaMountShape mountShape{};
    const TineGeometry tineGeometry = GetTineGeometry(f0);
    if (mTine) {
        mount = GetKalimbaMount(f0);
        mountShape = GetKalimbaMountShape(f0);
        tineLength = tineGeometry.lengthM;
        tineMass = tineGeometry.massPerLength * tineLength;
        tineStiffness = tineGeometry.bendingStiffness;
        // The fundamental's loss above the steel's own leaves through the bridge.
        bridgeLoss0 = kPiD * f0 * std::max(TineFundamentalLossFactor(f0) - material.eta0, 0.0);
        // Filing/relieving the tine at its support moves a stiff profile
        // toward the existing yielding-bridge instrument. A conservative
        // morph toward the rigid supported-beam spectrum gives Carving a
        // tine-specific meaning while leaving its fundamental at the key.
        // The interpolation is a voiced approximation, not a new FEM fit.
        const double stiffShare = 0.65 * (1.0 - carving);
        for (int m = 0; m < kNumTineModes; ++m) {
            mount.ratio[m] += stiffShare * (KalimbaModeRatio(m) - mount.ratio[m]);
            mount.bridgeLoss[m] *= 1.0 - 0.3 * stiffShare;
        }
    } else {
        key = GetBarKey(model, midi, options.carving);
    }
    const auto geometry = mTine ? GetModalRadiationGeometry(model, f0)
                                : GetModalRadiationGeometry(model, f0, key.lengthM, key.widthM);
    const ModalRadiationSetup radiation = PrepareModalRadiation(geometry);
    const CapturePoint source = CaptureSourcePosition(model, noteNumber);
    const auto earPoint = [&](int channel) {
        const auto ear = CloseEar(channel);
        // A key's long axis is scene Y; its local origin is its near end.
        return ModalRadiationPoint{.5 * geometry.length + ear.y - source.y,
                                   ear.x - source.x, ear.z - source.z};
    };
    const ModalRadiationSetup ears{PrepareModalRadiationPaths(geometry, earPoint(0)),
                                  PrepareModalRadiationPaths(geometry, earPoint(1)),
                                  geometry.closeStrength, geometry.closeStrength};
    // A bar's finite radiating face, relative to this model's calibrated C4
    // key. This changes acoustic readout only, not contact energy, mounting
    // forces, cavity exchange or pickup strain. Tines sound through the box.
    const double acousticArea = mTine ? 1.0 : BarAcousticAreaScale(model, key.lengthM, key.widthM);
    const double footprint = mTine ? 0.02 : shaft ? GetShaftSpec(false).footprint
        : GetMalletSpec(mt).footprint * (0.8 + 0.4 * std::pow(static_cast<double>(mVelocity), 0.4));
    // A thumb presses within the last fifth of a tine: Position runs from
    // there (0) to the very tip (1), where the overtones are fully excited.
    // (A thumb lands within a millimetre or two of the same spot on a tine,
    // several times the spread of a mallet on a bar in relative terms.)
    const double spatialPosition = mTine
        ? std::clamp(0.80 + 0.20 * std::clamp(static_cast<double>(strikePosition + positionOffset
                                                                   + art.strikePositionOffset), 0.0, 1.0)
                     + 10.0 * character.positionOffset, 0.76, 1.0)
        : position;
    mStrokePosition = spatialPosition;
    // Supports sit at the fundamental's nodes (wherever the carving put
    // them); the cords transmit what moves there.
    double supportPoint = 0.2242;
    if (!mTine) {
        double lo = 0.10, hi = 0.35;
        if (BarShape(key, 0, lo) > 0.0 && BarShape(key, 0, hi) < 0.0) {
            for (int i = 0; i < 30; ++i) {
                const double mid = 0.5 * (lo + hi);
                (BarShape(key, 0, mid) > 0.0 ? lo : hi) = mid;
            }
            supportPoint = 0.5 * (lo + hi);
        }
    }

    const double pickupStrainScale = mTine ? 0.0 : params.pickupGain * kStrainUnit
        * PiezoPickup::BarStrainCalibration(model) * kStateScale * 1.0e-3;
    for (int m = 0; m < kNumPitchedModes; ++m) {
        ModeState& mode = mModes[m];
        if (m >= mNumModes) { mode = ModeState{}; mRadiationGain[m] = 0.0; continue; }
        const double ratio = (mTine ? mount.ratio[std::min(m, 7)] : key.ratio[m]) * variation.partial[m];
        const double fm = f0 * ratio;
        if (fm >= ModeLimit(fs)) {
            for (int k = m; k < kNumPitchedModes; ++k) { mModes[k] = ModeState{}; mRadiationGain[k] = 0.0; }
            mNumModes = m;
            break;
        }
        double alpha;   // amplitude decay rate (1/s)
        double modalMass;
        double shapeAtContact;
        // A restrike of the same key keeps its spatial radiation unless the
        // mode moved by more than a stroke's pitch scatter (0.05 %).
        bool project = !(isRestrike && sameRadiationCarving && m < previousModes && mRadiationHz[m] > 0.0
                         && std::abs(fm / mRadiationHz[m] - 1.0) < 5.0e-4);
        // A fresh strike of a key played before takes the engine's copy.
        RadiationCacheEntry* const cache = options.radiationCache;
        const bool cacheValid = cache && cache->model == static_cast<int>(model) && cache->carving == options.carving;
        if (project && cacheValid && cache->hz[m] > 0.0 && std::abs(fm / cache->hz[m] - 1.0) < 5.0e-4) {
            mRadiation[m] = cache->readout[m];
            mEarRadiation[m] = cache->ears[m];
            mRadiationHz[m] = cache->hz[m];
            project = false;
        }
        double nodeShapes[kRadiationNodes];
        if (mTine) {
            alpha = kPiD * fm * material.eta0 + bridgeLoss0 * bridgeCoupling * mount.bridgeLoss[m]
                  * KalimbaBodyLoading(fm, resonatorTuneCents, couplingShape);
            modalMass = tineMass;
            mHandShape[m] = 0.0;
            const auto shape = [&mountShape, m, carving](double x) {
                const double mounted = KalimbaMountedShape(mountShape, m, x);
                if (carving == 1.0f) return mounted;
                const double rigid = kKalimbaBeamModes[m].contactScale * KalimbaFullModeShape(m, x);
                return mounted + 0.65 * (1.0 - carving) * (rigid - mounted);
            };
            shapeAtContact = FootprintAverage(shape,
                                              spatialPosition, footprint);
            if (project)
                for (int k = 0; k < kRadiationNodes; ++k) nodeShapes[k] = shape(kModalRadiationNodes[k].x);
        } else {
            alpha = kPiD * fm * LossFactor(material, fm);
            modalMass = std::max(key.massFraction[m] * key.massKg, 1.0e-4);
            shapeAtContact = FootprintAverage([&key, m](double x) { return BarShape(key, m, x); },
                                              spatialPosition, footprint);
            // Under a muffling hand.
            mHandShape[m] = FootprintMeanSquare([&key, m](double x) { return BarShape(key, m, x); },
                                                kHandCentre, kHandSpanM / std::max(key.lengthM, 0.05));
            if (project)
                for (int k = 0; k < kRadiationNodes; ++k) nodeShapes[k] = BarShape(key, m, kModalRadiationNodes[k].x);
        }
        const double t60 = std::min(6.907755 * decayScale / std::max(alpha, 1.0e-6), kMaxT60Seconds);
        const double omega = kTwoPiD * fm / fs;
        if (!(isRestrike && m < previousModes)) mode.y1 = mode.y2 = 0.0;
        mode.omega = omega;
        mode.idle = false;
        mNeutralModeLoss[m] = 6.907755 / (t60 * fs);
        mNeutralModeRadius[m] = std::exp(-mNeutralModeLoss[m]);
        ReconfigureMode(mode, TensionRadius(m, mMembraneTension), omega);
        mode.contact = shapeAtContact;
        mode.toDisplacement = 1.0 / (kStateScale * std::sqrt(modalMass));

        const double omegaSeconds = kTwoPiD * fm;
        if (project) {
            mRadiation[m] = ComputeModalRadiation(m, fm, radiation, nodeShapes);
            mEarRadiation[m] = ComputeModalRadiation(m, fm, ears, nodeShapes);
            mRadiationHz[m] = fm;
            if (cache) {
                if (!cacheValid) {
                    for (double& hz : cache->hz) hz = 0.0;
                    cache->model = static_cast<int>(model);
                    cache->carving = options.carving;
                }
                cache->hz[m] = fm;
                cache->readout[m] = mRadiation[m];
                cache->ears[m] = mEarRadiation[m];
            }
        }
        if (mTine) {
            // A narrow tine is a weak dipole; its sound mostly leaves through
            // the bridge into the shared body (engine).
            const double direct = BarDirectRadiationScale(fm, geometry.width);
            mRadiationGain[m] = params.radiationGain * kRadiationUnit * 0.007 * omegaSeconds * omegaSeconds
                              * mode.toDisplacement * direct;
            // The same yielding mount drives the box and the microphones.
            // Preserve the established fundamental output with one common
            // gain, while every overtone keeps its signed spring reaction.
            // Carving follows the existing approximate mounted/rigid morph;
            // normalize each endpoint by its fundamental before blending.
            const double mountedBridge = KalimbaMountedBridgeRatio(mountShape, m);
            const double rigidBridge = kKalimbaBeamModes[m].contactScale * KalimbaBridgeShearJump(m)
                / (kKalimbaBeamModes[0].contactScale * KalimbaBridgeShearJump(0));
            const double bridgeRatio = mountedBridge + 0.65 * (1.0 - carving) * (rigidBridge - mountedBridge);
            mBridgeGain[m] = bridgeCoupling * tineStiffness * KalimbaBridgeShearJump(0) * bridgeRatio / (tineLength * tineLength * tineLength)
                           * mode.toDisplacement;
            mSupportGain[m] = 0.0;
        } else {
            const double direct = BarDirectRadiationScale(fm, geometry.width);
            mRadiationGain[m] = params.radiationGain * kRadiationUnit * omegaSeconds * omegaSeconds
                              * mode.toDisplacement * direct * acousticArea
                              * (m >= 2 ? kUpperBendingRadiationGain : 1.0);
            mBridgeGain[m] = 0.0;
            // The cords pass the bar's motion over the felt (about 5% of the
            // length around the nodal point) to the frame, read from the
            // displacement state and scaled like velocity; the felt passes
            // little above a few kHz.
            const double cord = 1.0 / (1.0 + (fm / 3000.0) * (fm / 3000.0));
            const double felt = std::sqrt(FootprintMeanSquare([&key, m](double x) { return BarShape(key, m, x); },
                                                              supportPoint, 0.05));
            mSupportGain[m] = kSupportUnit * omegaSeconds * mode.toDisplacement * cord * felt;
        }
        // The fixed electrode reads the same carved Timoshenko mode, including
        // local thickness and section rotation. C4 sets only the family audio
        // calibration; no extra length scaling belongs here. A tine's pickup
        // sits on its board and reads the bridge reaction instead (below).
        mStrainGain[m] = mTine ? 0.0 : pickupStrainScale
                       * PiezoPickup::BarSurfaceStrain(key, m) * mode.toDisplacement;
        mSurfaceStrainGain[m] = mTine ? 0.0
            : PiezoPickup::BarSurfaceStrain(key, m) * mode.toDisplacement;
    }
    UpdateContactCompliance();

    // ----------------------------------------------------------------------
    // Torsion and lateral bending (wooden bars): driven by the off-axis and
    // glancing parts of the contact; they carry no net volume velocity.
    // ----------------------------------------------------------------------
    const int previousTorsion = isRestrike ? mNumTorsionModes : 0;
    mNumTorsionModes = 0;
    mHasLateral = false;
    if (!mTine && mNumModes > 0) {
        // A shaft strikes the bar's edge.
        const double lateral = shaft ? std::copysign(0.9, character.lateralOffset)
            : std::clamp(character.lateralOffset
                         + std::copysign(1.5 * (1.0 - std::min(1.0f, art.impulseScale)), character.lateralOffset), -0.9, 0.9);
        for (int n = 0; n < kNumTorsionModes; ++n) {
            const double ratio = key.torsion[n] * variation.torsion;
            const double frequency = f0 * ratio;
            if (frequency >= ModeLimit(fs)) break;
            ModeState& mode = mTorsion[n];
            if (n >= previousTorsion) mode.y1 = mode.y2 = 0.0;
            // Wood loses more in shear than in bending.
            const double alpha = 1.67 * kPiD * frequency * LossFactor(material, frequency);
            const double t60 = std::min(6.907755 * decayScale / alpha, kMaxT60Seconds);
            mode.omega = kTwoPiD * frequency / fs;
            ReconfigureMode(mode, std::exp(-6.907755 / (t60 * fs)), mode.omega);
            // cos(n pi x) twist shapes: T1's node, T2's antinode at the centre.
            const double shape = std::cos((n + 1) * kPiD * spatialPosition);
            // The head's patch straddles the bar's axis: only its off-centre
            // share (about a third) twists the bar. A shaft's line of contact
            // lies on the edge.
            const double lever = (shaft ? 1.0 : 0.3) * lateral * 0.5 * key.widthM;
            const double inertia = key.massKg * (key.widthM * key.widthM + key.thicknessM * key.thicknessM) / 24.0;
            mTorsionDrive[n] = kStateScale * lever * shape * std::sin(mode.omega)
                             / (std::sqrt(std::max(inertia, 1.0e-9)) * kTwoPiD * frequency);
            // The two edges move in antiphase, a width apart: their fields
            // cancel until the width nears a wavelength.
            const double direct = BarDirectRadiationScale(frequency, geometry.width);
            const double edges = std::min(1.0, 0.5 * kTwoPiD * frequency / 343.0 * key.widthM);
            const double gain = params.radiationGain * kRadiationUnit * (kTwoPiD * frequency) * (kTwoPiD * frequency)
                              * direct * edges * 0.5 * key.widthM * acousticArea
                              / (kStateScale * std::sqrt(std::max(inertia, 1.0e-9)));
            mTorsionClose[n] = gain * kTransverseRadiationGain;
            mTorsionFar[n] = 0.6 * gain * kTransverseRadiationGain;
            ++mNumTorsionModes;
        }
        const double lateralHz = f0 * key.lateral * variation.torsion;
        if (lateralHz < ModeLimit(fs)) {
            mHasLateral = true;
            if (!isRestrike) mLateral.y1 = mLateral.y2 = 0.0;
            const double alpha = 1.3 * kPiD * lateralHz * LossFactor(material, lateralHz);
            const double t60 = std::min(6.907755 * decayScale / alpha, kMaxT60Seconds);
            mLateral.omega = kTwoPiD * lateralHz / fs;
            ReconfigureMode(mLateral, std::exp(-6.907755 / (t60 * fs)), mLateral.omega);
            // Glancing strokes push along the width: a friction share of the
            // force.
            const double glance = 0.04 + 0.25 * (1.0 - std::min(1.0f, art.impulseScale));
            const double mass = 0.25 * key.massKg;
            mLateralDrive = kStateScale * glance * std::copysign(1.0, character.lateralOffset)
                          * std::sin(mLateral.omega) / (std::sqrt(mass) * kTwoPiD * lateralHz);
            // In-plane motion radiates little.
            mLateralRadiation = 0.15 * params.radiationGain * kRadiationUnit * (kTwoPiD * lateralHz) * (kTwoPiD * lateralHz)
                              * acousticArea / (kStateScale * std::sqrt(mass)) * kTransverseRadiationGain;
        }
    }
    for (int n = mNumTorsionModes; n < kNumTorsionModes; ++n) mTorsion[n] = ModeState{};
    if (!mHasLateral) mLateral = ModeState{};

    // ----------------------------------------------------------------------
    // Excitation: a mallet (bars) or a thumb release (tines)
    // ----------------------------------------------------------------------
    mArtifactSample = mArtifactEnd = 0;
    mReboundStart = -1;
    mTextureAlpha = SmootherAtRate(0.05, fs);
    mLoudnessAlpha = 1.0 - std::exp(-1.0 / (0.03 * fs));
    if (!isRestrike) mSupportLoad = 0.0;
    // Fibre slip noise grows with how fast the head is compressed.
    mTextureDepth = std::min(1.0, 1.5 * touch);
    mFingerOn = mFingerPending = false;
    mFingerPreload = mFingerCompression = 0.0;
    mHandModes = false;
    mHandPulseSample = mHandPulseSamples = 0;
    // Bridge-rod slip: the tine rocks on the rod twice per cycle, adding a
    // force at 2 f1 that grows with amplitude (Holdaway: -35..-41 dB at 0.1 s).
    mBridgeNonlinear = mTine ? 0.004 : 0.0;
    mPreloadBleed = std::exp(-1.0 / (kPreloadBleedSeconds * fs));
    if (!isRestrike) mPreloadOffset = 0.0;
    if (pluck) {
        mContact.Reset();
        const ThumbSpec thumb = GetThumbSpec(mt);
        mReleaseForce = thumb.forceN * touch * primaryShare;
        // The thumb stops what the tine was doing, then holds it deflected:
        // on contact, the first sample of the stroke (ProcessSample).
        mThumbResidual = isRestrike ? thumb.residual : 0.0;
        double compliance = 0.0;
        for (int m = 0; m < mNumModes; ++m) {
            const auto& mode = mModes[m];
            compliance += mode.force * mDt / std::max(1.0 - mode.c + mode.s, 1.0e-18)
                        * mode.contact * mode.toDisplacement;
        }
        const double retained = std::abs(mThumbResidual * ContactDisplacement());
        if (retained > kMaxThumbTravel) mThumbResidual *= kMaxThumbTravel / retained;
        // Retain the velocity law in the extended bass, then include the
        // strike variation and any residual motion in the absolute bound.
        const double travel = std::min(kMaxThumbTravel * std::min(1.0, touch * primaryShare),
                                       std::max(0.0, kMaxThumbTravel - retained));
        const double deflection = compliance * mReleaseForce;
        if (deflection > travel) mReleaseForce *= travel / deflection;
        for (int m = 0; m < mNumModes; ++m) {
            const auto& mode = mModes[m];
            mThumbHold[m] = mode.force * mReleaseForce * mDt / std::max(1.0 - mode.c + mode.s, 1.0e-18);
        }
        mThumbPending = true;
        // A firmer stroke (stiffer contact) snaps free a little faster.
        // A soft thumb lets the tine slide free more slowly; a firm stroke
        // snaps away. Velocity shapes the physical force-time history even
        // with every optional velocity route at zero. Full-force release
        // keeps the calibrated timing. Bound the extra drag smoothly at
        // 0.15 ms before hardness scaling, so an already slow flesh release
        // does not cancel the low note's ring and leave disproportionate noise.
        const double extraSlip = 0.00015 * (1.0 - std::exp(-0.75 * (1.0 - mVelocity)
                                                   * thumb.releaseSeconds / 0.00015));
        const double slip = (thumb.releaseSeconds + extraSlip) * std::exp2(1.0 - 2.0 * hardness)
                          / character.stiffnessScale;
        mReleaseSamples = std::max(2, static_cast<int>(std::ceil(slip * fs)));
        mReleaseSample = 0;
        mReleaseShape = thumb.releaseShape;
        mClickGain = thumb.click * 0.8;
        // A dead pluck: the thumb comes back to rest on the tine.
        mFingerPending = mDeadStroke;
        mFingerDelay = static_cast<int>(std::lround(kThumbReturnSeconds * fs));
    } else {
        mReleaseSamples = mReleaseSample = 0;
        mReleaseForce = 0.0;
        mMallet = shaft ? GetShaftSpec(mTine) : GetMalletSpec(mt);
        mMalletSetup = MalletContact::Setup{};
        mMalletSetup.mass = mMallet.massKg;
        mMalletSetup.stiffness = mMallet.stiffness * std::pow(10.0, 1.1 * (hardness - 0.5)) * character.stiffnessScale;
        mMalletSetup.exponent = mMallet.exponent;
        mMalletSetup.coreDepth = mMallet.coreDepthM * (1.6 - 1.2 * hardness);
        mMalletSetup.coreStiffness = mMallet.coreStiffness;
        mMalletSetup.coreExponent = mMallet.coreExponent;
        mMalletSetup.hysteresis = mMallet.hysteresis * character.hysteresisScale;
        // A shaft stroke uses the side of the handle, with a shorter swing
        // than a head strike; do not inject head-strike energy through its
        // much harder contact. A fingertip taps a tine more slowly again.
        const double strokeSpeed = kMaxMalletSpeed * (mTine ? 0.35 : shaft ? 0.50 : 1.0);
        StartMallet(strokeSpeed * touch * primaryShare, 0.0);
        mClickGain = mMallet.click * (shaft && !mTine ? 0.4 : 1.0);
        if (mDeadStroke) {
            if (mTine) mFingerPending = true;              // the fingertip stays on the tine
            else mContact.Hold(kDeadHoldForce, kDeadGrip); // the head stays pressed into the bar
        }
        mFingerDelay = 0;
        // A glancing or loose stroke bounces back onto the bar (a held one cannot).
        if (art.reboundGain > 0.0f && !mDeadStroke) {
            mReboundStart = static_cast<int>(art.reboundDelaySec * fs);
            mReboundVelocity = strokeSpeed * touch * (0.2 + art.reboundGain);
            mArtifactEnd = std::max(mArtifactEnd, mReboundStart + 1);
        }
    }
    // The shaft's own bending modes (rattan to birch with Hardness, lowered by
    // the head and the grip) knock briefly with the contact.
    mShaftActive = shaft && !mTine;
    for (int k = 0; k < 3; ++k) {
        auto& mode = mShaftModes[k];
        if (!mShaftActive) { mode = ModeState{}; continue; }
        static constexpr double kRattan[3] = {95.0, 260.0, 510.0}, kBirch[3] = {200.0, 550.0, 1080.0};
        const double frequency = kRattan[k] + hardness * (kBirch[k] - kRattan[k]);
        if (!isRestrike) mode.y1 = mode.y2 = 0.0;
        mode.omega = kTwoPiD * frequency / fs;
        ReconfigureMode(mode, std::exp(-6.907755 / (0.06 * fs)), mode.omega);
    }
    mShaftDrive = kShaftDrive;
    mShaftRadiation = kShaftRadiation;
    mPreviousForce = 0.0;
    mContactForce = 0.0;
    // Impact force reaches a bar's transducer directly; a tine's arrives
    // through the bridge reaction already read above.
    mPickupContactGain = mTine ? 0.0 : kPickupForceUnit * params.pickupGain;
    BandPass(std::clamp(params.clackFrequency + f0 * 0.35, 2500.0, 9000.0), params.clackBandwidth, fs,
             mClackBpC, mClackBpS, mClackBpB);
    if (!isRestrike) mClackBpY1 = mClackBpY2 = 0.0;

    // ----------------------------------------------------------------------
    // Resonator: tube, box or gourd, coupled to the symmetric bar modes
    // ----------------------------------------------------------------------
    const ResonatorProfile resonator = GetResonatorProfile(model);
    mNumCavityModes = 0;
    mRimHarmonic3 = mRimHarmonic5 = 0.0;
    if (!isRestrike) mRimGain3 = mRimGain5 = 0.0;
    mResCoupling = couplingShape;
    mResReadoutStep = 1.0 - std::exp(-1.0 / (0.005 * fs));
    if (!isRestrike) mResReadoutGain = 1.0;
    // The mouth's monopole reaches the far microphone about as strongly as
    // the close one; the bar's dipole falls off faster (0.6 above).
    mFarAirWeight = 0.9;
    if (!mTine && mNumModes > 0) {
        const double cents = ResonatorTuningCents(resonator, noteNumber) + variation.resonatorCents
                           + resonatorTuneCents;
        const double fRes = f0 * std::exp2(cents / 1200.0);
        // The mouth sits under the bar's centre: only modes with net motion
        // over it (the symmetric ones) exchange volume with the air.
        const double mouth = std::clamp(1.2 * key.widthM / std::max(key.lengthM, 1.0e-3), 0.08, 0.4);
        double overlap[kNumPitchedModes]{};
        for (int m = 0; m < mNumModes; ++m) {
            overlap[m] = m % 2 ? 0.0 : std::abs(FootprintAverage([&key, m](double x) { return BarShape(key, m, x); },
                                                                 0.5, mouth));
        }
        // Only the lowest air mode: the tube's or box's upper resonances sit
        // between the bar's symmetric modes, and measured through the
        // junction they added nothing above -50 dB.
        for (auto& upper : mUpperCavity) upper = ModeState{};
        for (int i = 0; i < kAirModes; ++i) {
            auto& mode = i == 0 ? mTube : mUpperCavity[i - 1];
            const double frequency = fRes * resonator.ratio[i];
            if (frequency >= ModeLimit(fs)) {
                mode = ModeState{};
                mCavityCos[i] = 1.0; mCavitySin[i] = 0.0;
                mCavityAngle[i] = mCavityAngleTarget[i] = 0.0;
                mCavityRate[i] = 0.0;
                continue;
            }
            ++mNumCavityModes;
            // Decay stretches the whole resonant system in time: the air's
            // loss and the bar-air exchange with the bar's own loss (below),
            // so a key whose tube carries its ring lengthens too, and the
            // bloom keeps its shape.
            const double q = std::max(1.5, InSituResonatorQ(resonator, frequency) * (i > 0 ? resonator.upperQ[i - 1] : 1.0)
                                           * decayScale);
            const double airLoss = kPiD * frequency / q;
            mode.omega = kTwoPiD * frequency / fs;
            ReconfigureMode(mode, std::exp(-airLoss / fs), mode.omega);
            if (!isRestrike) mode.y1 = mode.y2 = 0.0;
            // Nearest symmetric bar mode in log frequency.
            int bar = 0;
            double best = 1.0e30;
            for (int m = 0; m < mNumModes; m += 2) {
                const double distance = std::abs(std::log(frequency / (f0 * key.ratio[m])));
                if (distance < best) { best = distance; bar = m; }
            }
            mCavityBarIndex[i][0] = mCavityBarIndex[i][1] = bar;
            mCavityConnections[i] = 1;
            mCavityDirection[i][0] = 1.0;
            mCavityDirection[i][1] = 0.0;
            mCavityRate[i] = resonator.coupling * kTwoPiD * f0 * couplingShape * resonator.aperture[i] * overlap[bar]
                           / decayScale;
            mCavityReadout[i] = params.airGain * kAirUnit * resonator.readout[i]
                              * (kTwoPiD * frequency) * (kTwoPiD * frequency) / kStateScale
                              / std::sqrt(std::max(key.massFraction[0] * key.massKg, 1.0e-4));
            // The rim's harmonics (tubes): the mouth's velocity per unit air
            // state from the mode's energy, E = 1/2 w^2 (y / S)^2 = 1/4 rho A L U^2.
            if (i == 0 && resonator.tubeRadiusM > 0.0) {
                const double a = TubeRadius(resonator, frequency);
                const double area = kPiD * a * a, length = 343.0 / (4.0 * frequency);
                const double mouth = kTwoPiD * frequency / kStateScale * std::sqrt(2.0 / (1.2 * area * length));
                const double third = std::pow(10.0, RimHarmonic3Db(midi) / 20.0) * mouth;
                mRimHarmonic3 = third;
                mRimHarmonic5 = third * std::pow(10.0, kRimHarmonic5Db / 20.0);
            }
        }
    } else {
        mTube = ModeState{};
        for (auto& mode : mUpperCavity) mode = ModeState{};
    }
    mCavityAngleRamp = isRestrike;
    UpdateCavityCoupling();
    TuneOnResonator();

    // ----------------------------------------------------------------------
    // Membranes and buzzers
    // ----------------------------------------------------------------------
    // Smaller treble resonators carry smaller membranes that need less
    // pressure to go taut, so the buzz threshold follows the register.
    // A gourd's membrane shrinks faster than its pressure up the keyboard, so
    // treble balafon keys still buzz on moderate strokes. Every Guatemalan
    // box carries its charleo, up to the treble the melody is played in:
    // above C4 a box's volume falls faster than its length (it tapers too),
    // so the same air energy presses harder on its bottom (p^2 ~ E / V), and
    // its smaller hole takes a smaller membrane. Its gap follows 1/f^2.1
    // there, so the charleo keeps buzzing up to C6 as the bass does.
    const double trebleGap = model == EMarimbaModel::MayanPadauk
        ? std::pow(std::min(1.0, 261.625565 / f0), kCharleoTrebleGapExponent) : 1.0;
    mMembraneGapScale = std::clamp(std::pow(261.625565 / f0, model == EMarimbaModel::BalafonAncestral ? 1.8 : 1.1)
                                   * trebleGap, model == EMarimbaModel::MayanPadauk ? 0.02 : 0.08, 1.6);
    ConfigureMembranes(buzzAmount, true);
    mSlapPrng = FastPRNG(mLocalPrng.Next() | 1u);
    // A short treble tine shakes the board faster for the same bridge motion;
    // the buzzers' sensitivity is referred to the middle of the instrument.
    // Stop extending that referral once the geometry reaches its longest
    // tine: inverse-square-root gain must not amplify a returning thumb into
    // an unbounded buzzer collision as an extrapolated MIDI pitch tends to 0.
    mBuzzer.Configure(fs, std::sqrt(std::max(MinimumTineGeometryFrequency(), f0)
                                    / kTineReferenceFrequency));
    SetBuzz(buzzAmount, mMembraneTension);
    if (!isRestrike) {
        for (auto& membrane : mMembranes) membrane.Reset();
        for (auto& taut : mMembraneTaut) taut = false;
        mBuzzer.Reset();
        mSlapNoise = mSlapY1 = mSlapY2 = 0.0;
    }
    BandPass(2600.0, 3200.0, fs, mSlapC, mSlapS, mSlapB);
    {
        // Fourth-order Butterworth high-pass (bilinear, two sections) and a
        // notch (Q 1.5) on the fundamental for the buzz.
        const double corner = std::min(std::max(1.5 * f0, 350.0), 0.2 * fs);
        const double k = std::tan(kPiD * corner / fs);
        constexpr double kSectionQ[2] = {0.5411961001461970, 1.3065629648763764};
        for (int s = 0; s < 2; ++s) {
            auto& section = mBuzzFilter[s];
            const double norm = 1.0 / (1.0 + k / kSectionQ[s] + k * k);
            section.b0 = norm;
            section.b1 = -2.0 * norm;
            section.b2 = norm;
            section.a1 = 2.0 * (k * k - 1.0) * norm;
            section.a2 = (1.0 - k / kSectionQ[s] + k * k) * norm;
        }
        auto& notch = mBuzzFilter[2];
        const double w = kTwoPiD * std::min(f0, 0.2 * fs) / fs;
        const double alpha = std::sin(w) / (2.0 * 1.5);
        const double norm = 1.0 / (1.0 + alpha);
        notch.b0 = norm;
        notch.b1 = -2.0 * std::cos(w) * norm;
        notch.b2 = norm;
        notch.a1 = notch.b1;
        notch.a2 = (1.0 - alpha) * norm;
        if (!isRestrike)
            for (auto& section : mBuzzFilter) section.x1 = section.x2 = section.y1 = section.y2 = 0.0;
    }

    // ----------------------------------------------------------------------
    // Artifacts: a brush of a neighbouring key, a loose mounting knock
    // ----------------------------------------------------------------------
    mBrushImpulse = touch * art.brushGain;
    mBrushStart = static_cast<int>(art.brushDelaySec * fs);
    const double secondaryDuration = std::max(3.0, 0.0012 * art.brushDurationScale * fs);
    mSecondaryStep = 1.0 / secondaryDuration;
    if (mBrushImpulse > 0.0) mArtifactEnd = std::max(mArtifactEnd, mBrushStart + static_cast<int>(std::ceil(secondaryDuration)));
    const double brushF0 = f0 * std::exp2(art.brushSemitones / 12.0);
    for (auto& mode : mBrushModes) mode.omega = mode.w;
    for (int m = 0; m < 2 && mBrushImpulse > 0.0; ++m) {
        auto& mode = mBrushModes[m];
        const double frequency = brushF0 * (m == 0 ? 1.0
            : mTine ? GetKalimbaMount(brushF0).ratio[1] : GetBarKey(model, midi + art.brushSemitones).ratio[1]);
        if (frequency >= ModeLimit(fs)) { mode = ModeState{}; continue; }
        mode.omega = kTwoPiD * frequency / fs;
        const double brushT60 = mTine ? 2.2 / (TineFundamentalLossFactor(frequency) * frequency)
                                      : 2.2 / (LossFactor(material, frequency) * frequency);
        ReconfigureMode(mode, std::exp(-6.907755 / (std::max(0.025, brushT60 * art.brushDecayScale * (m == 0 ? 0.5 : 0.1)) * fs)), mode.omega);
    }
    mBrushPickupWeight = PiezoPickup::ModalStrainWeight(model, 0);
    mBrushActive = mBrushImpulse > 0.0 || ModeEnergy(mBrushModes[0]) + ModeEnergy(mBrushModes[1]) > kFlushEnergy;

    // A loose mounting is struck as bar/tine motion crosses its clearance. The
    // clearance is a fraction of the swing this stroke leaves in the
    // fundamental, measured once the mallet or thumb has let go.
    mCollisionEnvelope = art.collisionAmount * response.collisionLevel;
    mCollisionActive = mCollisionEnvelope > 0.0 || ModeEnergy(mCollisionModes[0]) + ModeEnergy(mCollisionModes[1]) > kFlushEnergy;
    mCollisionGapFactor = (0.08 + 0.22 * (1.0 - art.collisionAmount)) * art.collisionGapScale;
    mCollisionGap = 1.0e30;
    mCollisionCalibrated = false;
    mCollisionSawContact = false;
    mCollisionClock = 0;
    mCollisionCalibrationSample = static_cast<int>(std::lround(0.004 * fs));
    mCollisionDecay = std::exp(-1.0 / ((mTine ? 0.09 : 0.055) * art.collisionDecayScale * fs));
    mCollisionPrevious = mModes[0].y1;
    mCollisionSide = 0;
    mCollisionImpulse = 0.0;
    mCollisionImpulseLimit = 0.0;
    // A bar's knock radiates like the bar and its air, reaches the pickup and
    // shakes the frame like the bar's own swing; a tine's knock is a blow to
    // the bridge, which the box radiates and the pickup reads.
    mCollisionOutput = mTine ? kTineCollisionBridge * std::abs(mBridgeGain[0])
        : kCollisionLevel * (std::abs(mRadiationGain[0]) + (mNumCavityModes > 0 ? 0.5 * std::abs(mCavityReadout[0]) : 0.0));
    mCollisionPickup = mTine ? 0.0 : kCollisionLevel * std::abs(mStrainGain[0]);
    mCollisionFrame = mTine ? 0.0 : kCollisionLevel * kFrameDriveUnit * kSupportUnit * kTwoPiD * f0
                                    * mModes[0].toDisplacement;
    const double collisionDuration = std::max(1.0, std::min((mTine ? 0.00012 : 0.00025) * art.collisionPulseScale, 0.18 / f0) * fs);
    mCollisionAmplitudeRatio = 0.35 * art.collisionAmount;
    mCollisionPulseStep = 1.0 / collisionDuration;
    mCollisionSamplesTotal = static_cast<int>(std::ceil(collisionDuration));
    mCollisionSample = mCollisionSamplesTotal;
    // A clean restrike preserves an existing knock's tuning and decay. Only a
    // newly scheduled mounting event gets a fresh continuously varied shape.
    for (auto& mode : mCollisionModes) mode.omega = mode.w;
    for (int m = 0; m < 2 && mCollisionEnvelope > 0.0; ++m) {
        auto& mode = mCollisionModes[m];
        const double frequency = std::min(fs * 0.4, params.frameBodyFreq * response.collisionRatio * art.collisionPitchScale * (m == 0 ? 1.0 : 2.71));
        mode.omega = kTwoPiD * frequency / fs;
        ReconfigureMode(mode, std::exp(-6.907755 / (response.collisionDecaySec * art.collisionDecayScale * (m == 0 ? 1.0 : 0.43) * fs)), mode.omega);
    }
    // Frequencies stay fixed between Trigger/Retune calls. Keep these exact
    // libm sine values outside the oversampled oscillator loops.
    for (int i = 0; i < 3; ++i) mShaftSin[i] = std::sin(mShaftModes[i].w);
    for (int i = 0; i < 2; ++i) {
        mBrushSin[i] = std::sin(mBrushModes[i].w);
        mCollisionSin[i] = std::sin(mCollisionModes[i].w);
    }
}

// Membrane tension: a membrane's resonance goes as the square root of its
// tension, its buzz threshold as tension^1.5 (Bellet et al. 2014); Tension
// spans half an octave of formant either side of the voicing.
inline double TensionFormantRatio(double tension) {
    return std::exp2(std::clamp(tension, 0.0, 1.0) - 0.5);
}

void MarembaVoice::ConfigureMembranes(float buzzAmount, bool reshuffle) {
    const MembraneVoicing voicing = GetMembraneVoicing(mModel, mNoteNumber);
    mNumMembranes = voicing.count;
    const double tension = TensionFormantRatio(mMembraneTension);
    for (int i = 0; i < mNumMembranes; ++i) {
        // Time runs at the slack resonance; the taut spring lifts it to the formant.
        mMembranes[i].Configure(mSampleRate, voicing.formantHz[i] * tension * MembraneFormantScale(mModel, mF0)
            / std::sqrt(1.0 + voicing.stiffness), voicing.quality);
        mMembranes[i].SetSlackRadiation(voicing.slackShare);
        // Positive geometric stretching and strain loss once the membrane
        // pulls taut. Material spreads are voiced reduced-model parameters,
        // not measured constants for the silk/gut of a particular instrument.
        mMembranes[i].SetMaterial(mModel == EMarimbaModel::BalafonAncestral ? 0.30
                                  : mModel == EMarimbaModel::MayanPadauk ? 0.12 : 0.0,
                                  mModel == EMarimbaModel::BalafonAncestral ? 0.15
                                  : mModel == EMarimbaModel::MayanPadauk ? 0.08 : 0.0);
        if (reshuffle) {
            // Silk and gut are never twice alike: thresholds move stroke to stroke.
            mMembraneThreshold[i] = 1.0 + 0.15 * (mLocalPrng.NextGaussian() * 1.7);
        }
    }
    mMembraneDrain = voicing.drain;
    mSlapNoiseGain = voicing.slapNoise;
    mSlapJitter = voicing.slapJitter;
    mBuzzAmount = -1.0f;   // force SetBuzz to apply
    (void)buzzAmount;
}

void MarembaVoice::SetBuzz(float amount, float tension) {
    // Keep the persisted/automated knob at 0..1 while restricting the physical
    // response to the former 0..0.7 range (1.3.0f2). Trigger, restrike and
    // live changes all enter here; apply this once, before both membrane and
    // buzzer voicing. mBuzzAmount holds the physical amount.
    amount = kBuzzRange * std::clamp(amount, 0.0f, 1.0f);
    tension = std::clamp(tension, 0.0f, 1.0f);
    mBuzzAmount = amount;
    const MembraneVoicing voicing = GetMembraneVoicing(mModel, mNoteNumber);
    if (tension != mMembraneTension) {
        mMembraneTension = tension;
        for (int m = 0; m < mNumModes; ++m)
            ReconfigureMode(mModes[m], TensionRadius(m, tension), mModes[m].w);
        const double formant = TensionFormantRatio(tension);
        for (int i = 0; i < mNumMembranes; ++i)
            mMembranes[i].Configure(mSampleRate, voicing.formantHz[i] * formant * MembraneFormantScale(mModel, mF0)
                / std::sqrt(1.0 + voicing.stiffness), voicing.quality);
    }
    // A tighter membrane needs more pressure to reach its limits.
    const double tautness = std::pow(TensionFormantRatio(tension), 3.0);
    // A kalimba has no resonator membrane; its Buzz is the board buzzers.
    mBuzzer.SetLooseness(mTine ? amount : 0.0f, tension);
    mHasMirliton = !mTine && amount > 0.001f && mNumMembranes > 0;
    // More Buzz: looser, more exposed membranes that reach their taut limits
    // at lower pressure and are pushed harder by it. Mid settings buzz only on
    // firm strokes and stop once the resonator pressure falls below the gap.
    const double slack = 1.2 - amount;
    // The large bass boxes of a Guatemalan marimba build far more pressure
    // at their bottoms than the treble ones; their charleos sit a little
    // tighter so the buzz stays even across the keyboard.
    const double register_ = mModel == EMarimbaModel::MayanPadauk
        ? std::clamp(261.625565 / mF0, 0.25, 3.0) : 1.0;
    // Buzz loosens the membranes toward their free travel; a charleo (one
    // large gut membrane) less steeply than the small silk mirlitons. Both
    // keep growing up to the top of the range.
    const double looseness = mModel == EMarimbaModel::MayanPadauk ? std::pow(slack, 0.6) : std::pow(slack, 1.25);
    for (int i = 0; i < mNumMembranes; ++i) {
        const double gap = voicing.gap[i] * mMembraneGapScale * register_ * looseness * tautness
                         * std::clamp(mMembraneThreshold[i], 0.6, 1.5);
        mMembranes[i].SetLimits(gap * (1.0 - voicing.asymmetry[i]), gap * (1.0 + voicing.asymmetry[i]),
                                voicing.stiffness, voicing.slapLoss);
        mMembraneDrive[i] = amount * voicing.drive[i] * voicing.pressureScale;
        // A looser membrane also leaves more of its area free to radiate.
        mMembraneRadiation[i] = voicing.radiation[i] * (0.4 + 0.6 * amount);
    }
}

void MarembaVoice::CoupleCavity(ModeState& cavity, int barIndex, double cosine, double sine) {
    auto& bar = mModes[barIndex];
    if (sine == 0.0 || !(bar.r > 0.0)) return;
    const double pressure = cavity.y1;
    const double velocity = -(bar.r * bar.y2 - bar.y1 * bar.cosW) * bar.invSin;
    const double nextVelocity = cosine * velocity - sine * pressure;
    const double nextPressure = sine * velocity + cosine * pressure;
    cavity.y1 = nextPressure;
    cavity.y2 += (nextPressure - pressure) * cavity.cosOverR;
    bar.y2 -= (nextVelocity - velocity) * bar.sinOverR;
    bar.idle = false;
}

void MarembaVoice::CoupleCavityAperture(ModeState& cavity, int cavityIndex) {
    const double sine = mCavitySin[cavityIndex];
    if (sine == 0.0) return;
    CoupleCavity(cavity, mCavityBarIndex[cavityIndex][0], mCavityCos[cavityIndex], sine);
}

void MarembaVoice::Damp(double pole) {
    if (mTine && mFingerOn) return;   // the fingertip is the damping (FingerForce)
    if (mHandModes) {
        // A palm on the bar: each mode by its own rate.
        for (int m = 0; m < mNumModes; ++m) { mModes[m].y1 *= mHandPole[m]; mModes[m].y2 *= mHandPole[m]; }
        for (int n = 0; n < mNumTorsionModes; ++n) { mTorsion[n].y1 *= mHandTorsionPole; mTorsion[n].y2 *= mHandTorsionPole; }
        mLateral.y1 *= mHandTorsionPole; mLateral.y2 *= mHandTorsionPole;
        mTube.y1 *= mHandAirPole; mTube.y2 *= mHandAirPole;
        return;
    }
    for (int m = 0; m < mNumModes; ++m) { mModes[m].y1 *= pole; mModes[m].y2 *= pole; }
    for (int n = 0; n < mNumTorsionModes; ++n) { mTorsion[n].y1 *= pole; mTorsion[n].y2 *= pole; }
    mLateral.y1 *= pole; mLateral.y2 *= pole;
    mTube.y1 *= pole; mTube.y2 *= pole;
    for (auto& mode : mUpperCavity) { mode.y1 *= pole; mode.y2 *= pole; }
}

bool MarembaVoice::ProcessSample(float& outClose, float& outFar, float& outPiezo, float* outEars) {
    if (!mActive) {
        outClose = outFar = outPiezo = 0.0f;
        if (outEars) outEars[0] = outEars[1] = 0.0f;
        mMechanicalSample = 0.0f;
        mBridgeSample = 0.0;
        return false;
    }

    // 0. A thumb's touch: the tine is stopped and held deflected.
    if (mThumbPending) {
        mThumbPending = false;
        double before = 0.0, after = 0.0;
        for (int m = 0; m < mNumModes; ++m) {
            auto& mode = mModes[m];
            before += mode.y1 * mBridgeGain[m];
            mode.y1 = mode.y1 * mThumbResidual + mThumbHold[m];
            mode.y2 = mode.y2 * mThumbResidual + mThumbHold[m];
            after += mode.y1 * mBridgeGain[m];
            mode.idle = false;
        }
        mPreloadOffset += (after + mBridgeNonlinear * after * after) - (before + mBridgeNonlinear * before * before);
    }

    // Contact/finger solvers need all free predictions before solving their
    // force. A freely ringing key can instead predict each mode in its update
    // loop, avoiding a second pass and temporary-array traffic every sample.
    double predicted[kNumPitchedModes];
    bool predictedReady = false;
    const auto predictContact = [&] {
        if (predictedReady) return;
        for (int m = 0; m < mNumModes; ++m) {
            const ModeState& mode = mModes[m];
            predicted[m] = mode.idle ? 0.0 : mode.c * mode.y1 - mode.s * mode.y2;
        }
        predictedReady = true;
    };

    // 2. Excitation force at the contact: a thumb's release, or a mallet
    // head, shaft or fingertip.
    double force = 0.0;
    if (mTine && mStriker < kStrikerShaft) {
        if (mReleaseSample < mReleaseSamples) {
            // Thumb force held, then released as the thumb slips off the tip.
            const double t = static_cast<double>(mReleaseSample) / mReleaseSamples;
            force = mReleaseForce * (1.0 - std::pow(t, mReleaseShape));
            ++mReleaseSample;
        }
    } else {
        if (mArtifactSample < mArtifactEnd && mArtifactSample == mReboundStart && !mContact.Active() && !mDamping) {
            StartMallet(mReboundVelocity, 0.0);
            mReboundStart = -1;
        }
        if (mContact.Active()) {
            predictContact();
            double free = -mContactCompliance * mSupportLoad;
            for (int m = 0; m < mNumModes; ++m) free += mModes[m].contact * mModes[m].toDisplacement * predicted[m];
            // Fibre slip while the head is compressed; a head held still on
            // the bar no longer slips.
            double texture = 0.0;
            if (mMallet.texture > 0.0 && !mContact.Holding()) {
                mTextureState += mTextureAlpha * (mLocalPrng.NextBipolar() * mNoiseScale - mTextureState);
                texture = mTextureState * mMallet.texture * 8.0 * mTextureDepth;
            }
            force = mContact.Step(free, texture);
        }
    }
    // A thumb or fingertip coming back to rest on a tine (dead stroke) once
    // the pluck or tap has let go: a tapping fingertip stays on the tine.
    if (mFingerPending && mContact.Active() && !mContact.InContact() && mContact.Velocity() < 0.0) mContact.Stop();
    if (mFingerPending && mReleaseSample >= mReleaseSamples && !mContact.Active() && !mThumbPending
        && mFingerDelay-- <= 0) {
        mFingerPending = false;
        mFingerOn = true;
        mFingerCompression = -ContactDisplacement();
        mFingerTarget = (mStriker < kStrikerShaft ? kThumbRestTouch : kFingerTouch) / kFingerStiffness;
        mFingerStiffness = kFingerStiffness;
        mFingerDamping = kFingerDamping;
        mFingerRamp = 1.0 - std::exp(-1.0 / (kFingerLandingSeconds * mSampleRate));
    }
    if (mFingerOn) { predictContact(); force += FingerForce(predicted, force); }
    mContactForce = force;
    // A muffling palm's landing, where the stroke landed.
    double handImpulse = 0.0;
    if (mHandPulseSample < mHandPulseSamples) {
        const double t = mHandPulseSample * mHandPulseStep;
        handImpulse = mHandPulseImpulse * (ContactCumulative(t + mHandPulseStep, 0.0) - ContactCumulative(t, 0.0));
        ++mHandPulseSample;
    }
    // What the bar's supports carry: a held head's push (on a tine too, whose
    // readouts would take a static bend for sound).
    if (mContact.Holding() || mSupportLoad != 0.0) {
        mSupportLoad = mContact.HoldForce();
        force -= mSupportLoad;
    }
    const double impulse = force * mDt + handImpulse;

    // Brush of a neighbouring key: a short positive pulse in its own modes.
    double brushForce = 0.0;
    if (mArtifactSample < mArtifactEnd) {
        if (mBrushImpulse > 0.0 && mArtifactSample >= mBrushStart) {
            const double t = (mArtifactSample - mBrushStart) * mSecondaryStep;
            brushForce = (mDamping ? 0.0 : 1.0) * mBrushImpulse
                * std::max(0.0, ContactCumulative(t + mSecondaryStep, 0.0) - ContactCumulative(t, 0.0));
        }
        ++mArtifactSample;
    }

    // 3. Air: advance its lossy free motion, then exchange with the bar.
    if (mCavityAngleRamp) {
        bool settled = true;
        for (int i = 0; i < mNumCavityModes; ++i) {
            mCavityAngle[i] += mResReadoutStep * (mCavityAngleTarget[i] - mCavityAngle[i]);
            if (std::abs(mCavityAngleTarget[i] - mCavityAngle[i]) < 1.0e-12) mCavityAngle[i] = mCavityAngleTarget[i];
            else settled = false;
            SinCosSmall(mCavityAngle[i], mCavitySin[i], mCavityCos[i]);
        }
        mCavityAngleRamp = !settled;
    }

    // 4. Bending modes.
    double barClose = 0.0, barFar = 0.0, support = 0.0, strain = 0.0, bridge = 0.0;
    double surfaceStrain = 0.0;
    double earLeft = 0.0, earRight = 0.0;
    // These choices are constant for this sample. Dispatch once so the
    // modal pass does not repeat them for each independently ringing mode.
    const auto renderModes = [&](auto predictedTag, auto tineTag, auto earsTag) {
        for (int m = 0; m < mNumModes; ++m) {
            ModeState& mode = mModes[m];
            if (mode.idle && impulse == 0.0) continue;
            const double free = decltype(predictedTag)::value ? predicted[m]
                : mode.idle ? 0.0 : mode.c * mode.y1 - mode.s * mode.y2;
            const double y = free + mode.force * impulse;
            mode.y2 = mode.y1;
            mode.y1 = y;
            // Both coordinates describe the same post-force oscillator state.
            // Reading the new displacement with the old, free quadrature would
            // give the spatial field a rate-dependent phase error during contact.
            // The impulse's quadrature contribution is proportional to F*dt;
            // it vanishes with the timestep rather than creating a 1/dt spike.
            const double quadrature = (mode.r * mode.y2 - y * mode.cosW) * mode.invSin;
            barClose += mRadiation[m].close.Read(y, quadrature) * mRadiationGain[m];
            barFar += mRadiation[m].far.Read(y, quadrature) * mRadiationGain[m];
            if constexpr (decltype(earsTag)::value) {
                earLeft += mEarRadiation[m].close.Read(y, quadrature) * mRadiationGain[m];
                earRight += mEarRadiation[m].far.Read(y, quadrature) * mRadiationGain[m];
            }
            if constexpr (decltype(tineTag)::value) {
                bridge += y * mBridgeGain[m];
            } else {
                support += y * mSupportGain[m];
                surfaceStrain += y * mSurfaceStrainGain[m];
            }
        }
    };
    if (predictedReady) {
        if (mTine) {
            if (outEars) renderModes(std::true_type{}, std::true_type{}, std::true_type{});
            else renderModes(std::true_type{}, std::true_type{}, std::false_type{});
        }
        else {
            if (outEars) renderModes(std::true_type{}, std::false_type{}, std::true_type{});
            else renderModes(std::true_type{}, std::false_type{}, std::false_type{});
        }
    } else {
        if (mTine) {
            if (outEars) renderModes(std::false_type{}, std::true_type{}, std::true_type{});
            else renderModes(std::false_type{}, std::true_type{}, std::false_type{});
        }
        else {
            if (outEars) renderModes(std::false_type{}, std::false_type{}, std::true_type{});
            else renderModes(std::false_type{}, std::false_type{}, std::false_type{});
        }
    }
    const double bendingClose = barClose;

    double airSum = 0.0;
    double membranePressure = 0.0;
    for (int i = 0; i < mNumCavityModes; ++i) {
        auto& air = i == 0 ? mTube : mUpperCavity[i - 1];
        const double y = air.c * air.y1 - air.s * air.y2;
        air.y2 = air.y1;
        air.y1 = y;
        CoupleCavityAperture(air, i);
        airSum += air.y1 * mCavityReadout[i];
        membranePressure += air.y1 * mMembranePressure[i];
    }

    for (int n = 0; n < mNumTorsionModes; ++n) {
        ModeState& mode = mTorsion[n];
        const double y = mode.c * mode.y1 - mode.s * mode.y2 + mTorsionDrive[n] * impulse;
        mode.y2 = mode.y1;
        mode.y1 = y;
        barClose += y * mTorsionClose[n];
        barFar += y * mTorsionFar[n];
    }
    if (mHasLateral) {
        const double y = mLateral.c * mLateral.y1 - mLateral.s * mLateral.y2 + mLateralDrive * impulse;
        mLateral.y2 = mLateral.y1;
        mLateral.y1 = y;
        barClose += y * mLateralRadiation;
        barFar += y * mLateralRadiation * 0.5;
    }

    // The shaft's knock.
    if (mShaftActive) {
        double knock = 0.0;
        for (int i = 0; i < 3; ++i) {
            auto& mode = mShaftModes[i];
            const double y = mode.c * mode.y1 - mode.s * mode.y2 + mShaftSin[i] * mShaftDrive * impulse;
            mode.y2 = mode.y1;
            mode.y1 = y;
            knock += y;
        }
        barClose += knock * mShaftRadiation;
        barFar += 0.6 * knock * mShaftRadiation;
    }

    // 5. Unmodelled high-frequency impact noise, shaped by the force rate: a
    // hard stick or nail clicks, soft yarn barely does.
    double clickSound = 0.0;
    {
        const double rate = (force - mPreviousForce) * mSampleRate;
        mPreviousForce = force;
        const double drive = mClickGain * std::abs(rate) * 5.0e-7;
        const double input = drive > 0.0 ? mLocalPrng.NextBipolar() * mNoiseScale * drive : 0.0;
        const double y = mClackBpC * mClackBpY1 - mClackBpS * mClackBpY2 + mClackBpB * input;
        mClackBpY2 = mClackBpY1;
        mClackBpY1 = y;
        clickSound = y;
        if (drive == 0.0 && std::abs(mClackBpY1) + std::abs(mClackBpY2) < 1.0e-12) mClackBpY1 = mClackBpY2 = 0.0;
    }

    // 6. Buzzing membranes driven by the local resonator pressure. Their slap
    // loss is taken from the air, so a buzzing note spends its resonator
    // energy sooner (a nonlinear sink that acts only above the threshold).
    double buzz = 0.0;
    const double handPole = mDamping ? mHandDampPole : 1.0;
    bool membranesMoving = false;
    if (!mHasMirliton)
        for (int i = 0; i < mNumMembranes; ++i) membranesMoving = membranesMoving || !mMembranes[i].IsResting();
    if (mHasMirliton || membranesMoving) {
        double slapTotal = 0.0;
        for (int i = 0; i < mNumMembranes; ++i) {
            const double drive = mHasMirliton ? membranePressure * mMembraneDrive[i] * kMembraneDrive : 0.0;
            const double slap = mMembranes[i].Step(drive, handPole);
            slapTotal += slap;
            // Each slap ends where the next one's limit is drawn.
            const bool taut = mMembranes[i].Taut();
            if (mMembraneTaut[i] && !taut && mSlapJitter > 0.0)
                mMembranes[i].SetLimitScale(1.0 + mSlapJitter * mSlapPrng.NextGaussian());
            mMembraneTaut[i] = taut;
            buzz += mMembranes[i].Acceleration() * mMembraneRadiation[i];
            if (drive == 0.0 && mMembranes[i].Energy() < 1.0e-24) mMembranes[i].Reset();
        }
        if (slapTotal > 0.0) {
            const double air = ModeEnergy(mTube) + ModeEnergy(mUpperCavity[0]) + ModeEnergy(mUpperCavity[1]);
            if (air > 1.0e-30) {
                const double scale = std::sqrt(std::max(0.0, 1.0 - mMembraneDrain * slapTotal / air));
                for (auto* mode : {&mTube, &mUpperCavity[0], &mUpperCavity[1]}) {
                    mode->y1 *= scale;
                    mode->y2 *= scale;
                }
            }
        }
        for (auto& section : mBuzzFilter) {
            const double y = section.b0 * buzz + section.b1 * section.x1 + section.b2 * section.x2
                           - section.a1 * section.y1 - section.a2 * section.y2;
            section.x2 = section.x1; section.x1 = buzz;
            section.y2 = section.y1; section.y1 = y;
            buzz = y;
        }
        // Each slap also rattles the membrane edge: a short broadband burst.
        mSlapNoise = std::max(mSlapNoise * mSlapNoisePole, std::sqrt(std::max(slapTotal, 0.0)));
    } else {
        mSlapNoise *= mSlapNoisePole;
    }
    double slapSound = 0.0;
    if (mSlapNoise > 1.0e-9 || std::abs(mSlapY1) + std::abs(mSlapY2) > 1.0e-12) {
        const double input = mLocalPrng.NextBipolar() * mNoiseScale * mSlapNoise * mSlapNoiseGain;
        const double y = mSlapC * mSlapY1 - mSlapS * mSlapY2 + mSlapB * input;
        mSlapY2 = mSlapY1;
        mSlapY1 = y;
        slapSound = y;
    } else {
        mSlapNoise = mSlapY1 = mSlapY2 = 0.0;
    }

    // 7. Adjacent-key brush and vibration-driven mount/bridge collision.
    double brushSound = 0.0;
    if (mBrushActive) for (int i = 0; i < 2; ++i) {
        auto& mode = mBrushModes[i];
        const double y = mode.c * mode.y1 - mode.s * mode.y2 + mBrushSin[i] * kBrushLevel * brushForce;
        mode.y2 = mode.y1 * handPole;
        mode.y1 = y * handPole;
        brushSound += y;
    }
    double collisionDrive = 0.0;
    const ModeState& mode0 = mModes[0];
    if (mCollisionActive) {
        if (!mCollisionCalibrated) {
            const bool exciting = mTine ? mThumbPending || mReleaseSample < mReleaseSamples
                                        : force > 0.0 || !mCollisionSawContact;
            mCollisionSawContact = mCollisionSawContact || force > 0.0;
            // From 4 ms after the stroke (the same moment at every rate), or
            // once a slow stroke has let go, each swing out past the
            // clearance can knock.
            const bool settled = ++mCollisionClock >= mCollisionCalibrationSample;
            if (!exciting && settled) {
                const double swing = std::sqrt(std::max(0.0, ModeEnergy(mode0)));
                mCollisionGap = std::max(1.0e-12, swing * mCollisionGapFactor);
                mCollisionImpulseLimit = swing * 0.4;
                mCollisionCalibrated = true;
                // A swing already past the clearance is not a new approach.
                mCollisionSide = std::abs(mode0.y1) > mCollisionGap ? (mode0.y1 < 0.0 ? -1 : 1) : 0;
            }
        }
        const double motion = mode0.y1;
        const double magnitude = std::abs(motion);
        const int side = motion < 0.0 ? -1 : 1;
        if (magnitude < 0.7 * mCollisionGap) mCollisionSide = 0;
        if (mCollisionEnvelope > 1.0e-8 && magnitude > mCollisionGap && side != mCollisionSide
            && mCollisionSample >= mCollisionSamplesTotal) {
            const double approach = std::max(0.0, magnitude - std::abs(mCollisionPrevious)) * mode0.invSin;
            const double ringBound = std::sqrt(2.0 * (ModeEnergy(mCollisionModes[0]) + ModeEnergy(mCollisionModes[1])));
            const double barBound = std::sqrt(std::max(0.0, ModeEnergy(mode0))) * mCollisionAmplitudeRatio;
            const double available = std::max(0.0, barBound - ringBound) / 1.45;
            mCollisionImpulse = side * std::min(available,
                std::min(approach, mCollisionImpulseLimit) * mCollisionEnvelope * 4.0);
            mCollisionSample = 0;
            mCollisionSide = side;
        }
        if (mCollisionSample < mCollisionSamplesTotal) {
            const double t = mCollisionSample++ * mCollisionPulseStep;
            collisionDrive = mCollisionImpulse * (ContactCumulative(t + mCollisionPulseStep, 0.0) - ContactCumulative(t, 0.0));
        }
        mCollisionPrevious = motion;
        mCollisionEnvelope *= mCollisionDecay * handPole;
        if (mCollisionEnvelope < 1.0e-8) mCollisionEnvelope = 0.0;
    }
    double collisionSound = 0.0;
    if (mCollisionActive) for (int i = 0; i < 2; ++i) {
        auto& mode = mCollisionModes[i];
        const double y = mode.c * mode.y1 - mode.s * mode.y2 + mCollisionSin[i] * collisionDrive;
        mode.y2 = mode.y1 * handPole;
        mode.y1 = y * handPole;
        collisionSound += y;
    }
    // The knock, in each destination's units (see Trigger).
    const double knockAir = mTine ? 0.0 : collisionSound * mCollisionOutput;
    const double knockPickup = mTine ? 0.0 : collisionSound * mCollisionPickup;
    const double knockFrame = mTine ? 0.0 : collisionSound * mCollisionFrame;
    // A brushed neighbouring tine rings on the same bridge.
    const double knockBridge = mTine ? collisionSound * mCollisionOutput + brushSound * mCollisionOutput : 0.0;
    const double brushAir = mTine ? 0.0 : brushSound;
    mCollisionOut = mTine ? collisionSound * mCollisionOutput : knockAir;

    // 8. Mechanical outputs: support force into the frame (bars) or the
    // bridge reaction into the board (tines, plus the rod-slip 2 f1 term).
    if (mTine) {
        bridge += mBridgeNonlinear * bridge * bridge + knockBridge - mPreloadOffset;
        mPreloadOffset *= mPreloadBleed;
        mBridgeSample = bridge;
        mMechanicalSample = static_cast<float>(bridge * 0.02);
    } else {
        mBridgeSample = 0.0;
        mMechanicalSample = static_cast<float>(support * kFrameDriveUnit + brushAir * 0.5 + knockFrame);
    }

    // Kalimba buzzers ride on the board that the bridge drives.
    double buzzerImpact = 0.0;
    if (mTine) {
        buzz += kBuzzerRadiation * mBuzzer.Process(bridge * kBuzzerDrive, handPole, buzzerImpact);
        buzzerImpact *= kBuzzerRadiation;
    }

    // 9. Microphones and the attached contact transducer.
    if (mResReadoutGain != 1.0) {
        mResReadoutGain += mResReadoutStep * (1.0 - mResReadoutGain);
        if (std::abs(mResReadoutGain - 1.0) < 1.0e-12) mResReadoutGain = 1.0;
    }
    double rim = 0.0;
    if (mRimGain3 != 0.0) {
        // The air's swing as a phasor x + i q: Re (x + i q)^3 = A^3 cos 3 phi
        // and Re (x + i q)^5 = A^5 cos 5 phi, scaled at control rate by 1/A
        // and 1/A^3 so each harmonic grows as the square of the air's swing.
        // Between control ticks a tube ringing up from near silence outgrows
        // the A it was scaled by, so each harmonic is held to its own level at
        // the swing it has now, level * A^2 (A^2 = x^2 + q^2, no root needed).
        const double x = mTube.y1;
        const double q = (mTube.r * mTube.y2 - x * mTube.cosW) * mTube.invSin;
        const double x2 = x * x, q2 = q * q, swing = x2 + q2;
        const double third = x * (x2 - 3.0 * q2) * mRimGain3;
        const double fifth = x * ((x2 - 10.0 * q2) * x2 + 5.0 * q2 * q2) * mRimGain5;
        rim = std::clamp(third, -mRimLevel3 * swing, mRimLevel3 * swing)
            + std::clamp(fifth, -mRimLevel5 * swing, mRimLevel5 * swing);
    }
    const double air = (airSum + rim) * mResReadoutGain;
    double close = barClose + air + buzz * 0.35 + slapSound * 0.35
                 + brushAir * 0.6 + knockAir * 0.65 + clickSound * 0.5;
    double far = barFar * 0.60 + air * mFarAirWeight + buzz * 0.50 + slapSound * 0.45
               + brushAir * 0.45 + knockAir * 0.22 + clickSound * 0.2;
    // Cavity mouths, impacts and weak auxiliary modes are compact sources at
    // this key. The capture scene subsequently filters/delays every source.
    // Their point-source approximation does not override the bending field.
    earLeft += close - bendingClose;
    earRight += close - bendingClose;
    if (mTine) strain += bridge * kTineBridgeStrain;
    // The bonded bar film reads bending strain only. Contact, support and
    // cavity feedback already act on these mechanical modes; adding their
    // nominal audio layers as sensor charge would double-count excitation.
    double piezo = mTine ? mPickup.Process(strain + knockPickup * 1.6 + brushAir * mBrushPickupWeight * 0.2,
        (force + brushForce * 50.0) * mPickupContactGain + buzzerImpact,
        support * kPickupSupportUnit + brushAir * 0.2 + knockPickup)
        : mPickup.ProcessSurfaceStrain(surfaceStrain);

    if (mDamping) Damp(mHandDampPole);

    // How loud the voice is now (a tine's sound leaves through its bridge):
    // what voice stealing weighs.
    {
        const double sound = mTine ? mBridgeSample * kTineLoudness : close;
        mLoudness += mLoudnessAlpha * (sound * sound - mLoudness);
    }

    // 10. Fade-out (steal, polyphony reduction, oversampling change)
    if (mFading) {
        // Smoothstep of the linear progress: no slope corners at either end.
        const float fade = mFadeGain * mFadeGain * (3.0f - 2.0f * mFadeGain);
        close *= fade;
        far *= fade;
        piezo *= fade;
        earLeft *= fade;
        earRight *= fade;
        mMechanicalSample *= fade;
        mBridgeSample *= fade;
        mFadeGain -= mFadeStep;
        if (mFadeGain <= 0.0f) {
            outClose = static_cast<float>(close);
            outFar = static_cast<float>(far);
            outPiezo = static_cast<float>(piezo);
            if (outEars) { outEars[0] = static_cast<float>(earLeft); outEars[1] = static_cast<float>(earRight); }
            FastKill();
            return true;
        }
    }

    outClose = static_cast<float>(close);
    outFar = static_cast<float>(far);
    outPiezo = static_cast<float>(piezo);
    if (outEars) { outEars[0] = static_cast<float>(earLeft); outEars[1] = static_cast<float>(earRight); }
    return true;
}

void MarembaVoice::UpdateEnvelope() {
    if (!mActive) return;

    double energy = 0.0;
    const bool driven = mContact.Active() || mReleaseSample < mReleaseSamples || mFingerOn
                     || mHandPulseSample < mHandPulseSamples;
    for (int m = 0; m < mNumModes; ++m) {
        const double e = FlushedModeEnergy(mModes[m]);
        // A decayed mode is skipped until the next strike wakes it.
        mModes[m].idle = !driven && e < kFlushEnergy && mNumCavityModes == 0;
        energy += e;
    }
    for (int n = 0; n < mNumTorsionModes; ++n) energy += FlushedModeEnergy(mTorsion[n]);
    if (mHasLateral) energy += FlushedModeEnergy(mLateral);
    const double tubeEnergy = FlushedModeEnergy(mTube);
    energy += tubeEnergy;
    for (auto& mode : mUpperCavity) energy += FlushedModeEnergy(mode);
    // The rim's harmonics follow the air's swing A = sqrt(tube energy).
    if (mRimHarmonic3 != 0.0 && tubeEnergy > 1.0e-24) {
        const double inverse = 1.0 / std::sqrt(tubeEnergy);
        mRimLevel3 = mRimHarmonic3 * std::abs(mCavityReadout[0]);
        mRimLevel5 = mRimHarmonic5 * std::abs(mCavityReadout[0]);
        mRimGain3 = mRimHarmonic3 * mCavityReadout[0] * inverse;
        mRimGain5 = mRimHarmonic5 * mCavityReadout[0] * inverse * inverse * inverse;
    } else {
        mRimGain3 = mRimGain5 = 0.0;
    }
    if (mShaftActive) {
        double knock = 0.0;
        for (auto& mode : mShaftModes) knock += FlushedModeEnergy(mode);
        mShaftActive = mContact.Active() || knock >= kFlushEnergy;
        energy += knock;
    }
    double brushEnergy = 0.0, collisionEnergy = 0.0;
    for (auto& mode : mBrushModes) brushEnergy += FlushedModeEnergy(mode);
    for (auto& mode : mCollisionModes) collisionEnergy += FlushedModeEnergy(mode);
    if (mArtifactSample >= mArtifactEnd && brushEnergy < kFlushEnergy) mBrushActive = false;
    if (mCollisionEnvelope == 0.0 && mCollisionSample >= mCollisionSamplesTotal && collisionEnergy < kFlushEnergy) mCollisionActive = false;
    energy += brushEnergy + collisionEnergy;
    mPickup.FlushTiny();
    energy += mPickup.StateEnergy();
    for (int i = 0; i < mNumMembranes; ++i) mMembranes[i].FlushTiny();
    mBuzzer.FlushTiny();
    energy += MembraneEnergy();

    bool membranesFinite = mBuzzer.IsFinite();
    for (int i = 0; i < mNumMembranes; ++i) membranesFinite = membranesFinite && mMembranes[i].IsFinite();
    if (!std::isfinite(energy) || energy > kMaxVoiceEnergy || !std::isfinite(mClackBpY1)
        || !membranesFinite || !mContact.IsFinite()) {
        FastKill();
        return;
    }
    mEnergy = energy;

    if (IsStriking() || std::abs(mClackBpY1) + std::abs(mSlapY1) > 1.0e-10) return;
    // The board is still settling (below 1 uN the rest is some -150 dB).
    if (std::abs(mPreloadOffset) > 1.0e-6) return;

    // -120 dB and nothing left to excite the bar: free the voice (state flushed)
    if (energy < kSilenceEnergy) FastKill();
}

} // namespace maremba
