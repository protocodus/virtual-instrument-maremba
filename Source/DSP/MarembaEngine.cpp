#include "MarembaEngine.h"
#include <algorithm>
#include <cmath>
#include "DspMath.h"
#include "DecayControl.h"
#include "MainVolume.h"
#include "MainCalibration.h"

namespace maremba {

namespace {
// 128 pseudo-random static detuning offsets with zero DC mean
static const float kNoteDetuneTable[128] = {
    +0.0540f, -0.4863f, +0.4395f, -0.4014f, -0.2634f, +0.7395f, +0.7874f, +0.4396f,
    -0.2516f, -0.1425f, -0.0450f, -0.6242f, +0.4875f, +0.3633f, -0.0978f, -0.5690f,
    +0.2448f, -0.8768f, +0.1653f, -0.1134f, +0.3672f, +0.9552f, +0.1900f, +0.6198f,
    -0.6043f, +0.0424f, +0.8326f, -0.0614f, -0.1825f, +0.4814f, +0.0856f, +0.7843f,
    +0.6525f, +0.2983f, -0.6607f, +0.1730f, +0.9034f, -0.0474f, -0.7458f, -0.2010f,
    -0.7812f, +0.5343f, +0.6455f, -0.7004f, -0.1230f, +0.0744f, +0.6243f, -0.7440f,
    -0.8834f, +0.5480f, -0.1917f, +0.3354f, +0.8191f, -0.7050f, +0.4238f, +0.8344f,
    +0.5716f, -0.5663f, +0.5182f, -0.2365f, +0.6228f, +0.5611f, +0.6353f, -0.6516f,
    -0.4725f, -0.6864f, -0.3072f, -0.3661f, -0.8506f, +0.0266f, +0.8827f, -0.0083f,
    -0.4116f, +0.6660f, -0.6318f, -0.7930f, +0.0326f, -0.4042f, +0.3739f, -0.0961f,
    -0.8976f, +0.2549f, +0.2626f, -0.2720f, -0.5457f, +0.0707f, +0.0258f, +0.6480f,
    +0.0399f, +1.0000f, -0.3402f, +0.1319f, +0.3105f, -0.7834f, -0.8128f, +0.3995f,
    +0.8804f, +0.3696f, +0.6763f, +0.7605f, +0.4467f, -0.8270f, -0.9022f, -0.5655f,
    -0.2851f, +0.4152f, -0.7693f, -0.3939f, +0.0109f, -0.8956f, +0.1508f, +0.2784f,
    -0.3297f, -0.2897f, -0.4885f, -0.0774f, +0.5285f, -0.2647f, -0.2147f, -0.4585f,
    -0.2109f, +0.7463f, +0.3959f, -0.6043f, -0.6603f, +0.3764f, +0.6896f, -0.8317f,
};

inline float GetKeyDetuneOffset(int noteNumber, float detuneAmount) {
    if (detuneAmount <= 0.001f) return 0.0f;
    int idx = std::clamp(noteNumber, 0, 127);
    return detuneAmount * kNoteDetuneTable[idx];
}

constexpr uint32_t kEngineSeed = 0x87654321u;
constexpr uint32_t kVoiceSeedBase = 0x56789ABCu;

constexpr double kStealFadeSeconds = 0.003;      // stolen voice fades in its own slot
constexpr double kPolyphonyFadeSeconds = 0.005;  // excess voices when Polyphony is lowered
constexpr double kSwitchFadeSeconds = 0.005;     // all outputs, before an oversampling change
constexpr double kVolumeSmoothingSeconds = 0.005;
constexpr double kTailHoldSeconds = 0.2;         // > longest hall echo gap
constexpr float kSilenceLevel = 1.0e-9f;
constexpr float kMaxPitchBendCents = 1200.0f;
// Last-resort output guard: a sample above this (+60 dBFS) or a non-finite one
// means something went unstable; the engine resets (a dropout, never garbage).
constexpr float kMaxOutputLevel = 1.0e3f;
// Mapped velocity floor: every curve maps 1/127 to just above it, so the
// response stays strictly monotonic down to the softest key.
constexpr float kVelocityFloor = 0.01f;

// Audible frame resonance at full Bloom, with a restrained lower half.
constexpr float kBodyBloomGain = 0.375f;
// Kalimba box radiation per unit of bridge force rate; Body scales it from a
// board that barely rings (0) to a resonant box (1), never to silence.
constexpr double kKalimbaBodyGain = 0.072;
// Calibrate the complete Main signal before its musical limiter. Volume and
// the fixed recording trim must reduce detector drive as well as output level.
constexpr float kMainCalibrationGain = 1.202853f;
constexpr float kMainOutputCalibrationGain = 0.7f;
// Mod-wheel rolls: stroke rate at the wheel's bottom and top, two alternating
// mallets landing a little either side of the set position.
constexpr double kRollMinRate = 6.0, kRollMaxRate = 20.0;
constexpr float kRollHandOffset = 0.035f;
constexpr float kRollThreshold = 0.02f;
// Hole cover smoothing (a hand moves over the holes in ~10 ms).
constexpr double kCoverSmoothingSeconds = 0.012;
constexpr uint32_t kRollSeed = 0x2468ACE1u;

// The gain of a direct tap is its capture's trim (MarembaEngine.h) times the
// shared mix scale; a stem carries this calibration and nothing else. Bar and
// Kalimba families retain their original balance at 70% nominal amplitude.
// Each stereo pair or mono Piezo keeps its own independent protection, which
// may act on louder material (the Rack device's Docs/research/output-level.md).

// Steal score of a voice: how loud it sounds now (the last ~30 ms), so bass
// and treble voices compare by what is heard; a non-finite voice is the first
// victim.
inline double StealEnergy(const MarembaVoice& voice) {
    const double e = voice.GetLoudness();
    return std::isfinite(e) ? e : 0.0;
}

inline float FiniteClamp(float value, float lo, float hi, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, lo, hi);
}
} // namespace

uint32_t MarembaEngine::VoiceSeed(int slot) {
    return kVoiceSeedBase ^ (static_cast<uint32_t>(slot + 1) * 0x9E3779B9u);
}

MarembaEngine::MarembaEngine(double sampleRate)
    : mBaseSampleRate((std::isfinite(sampleRate) && sampleRate > 8000.0) ? sampleRate : 44100.0)
    , mPrng(kEngineSeed)
{
    for (int note = 0; note < 128; ++note) mTineSide[static_cast<std::size_t>(note)] = KalimbaTineSide(note);
    // Linear acoustic capture runs at host rate after per-source decimation.
    // A host-rate change re-creates the engine and its preallocated histories.
    mMicMixer.Allocate(mBaseSampleRate);
    mMainPickup.Allocate(mBaseSampleRate);
    mOutputHeadroom.SetSampleRate(mBaseSampleRate);
    mVolumeAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (kVolumeSmoothingSeconds * mBaseSampleRate)));
    mDriveAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (0.010 * mBaseSampleRate)));
    mQuietFramesNeeded = static_cast<int>(kTailHoldSeconds * mBaseSampleRate);
    Reset();
}

EngineParameters MarembaEngine::Sanitize(const EngineParameters& in) {
    const EngineParameters d; // defaults for non-finite values
    EngineParameters p;
    p.model = std::clamp(in.model, 0, 3);
    p.malletType = std::clamp(in.malletType, 0, kStrikerCount - 1);
    p.malletHardness = FiniteClamp(in.malletHardness, 0.0f, 1.0f, d.malletHardness);
    p.strikePosition = FiniteClamp(in.strikePosition, 0.0f, 1.0f, d.strikePosition);
    p.resonatorTune = FiniteClamp(in.resonatorTune, -50.0f, 50.0f, d.resonatorTune);
    p.resonatorCoupling = FiniteClamp(in.resonatorCoupling, 0.0f, 1.0f, d.resonatorCoupling);
    p.decay = FiniteClamp(in.decay, 0.10f, 8.0f, d.decay);
    p.buzzAmount = FiniteClamp(in.buzzAmount, 0.0f, 1.0f, d.buzzAmount);
    p.artifacts = FiniteClamp(in.artifacts, 0.0f, 1.0f, d.artifacts);
    p.sympathetic = FiniteClamp(in.sympathetic, 0.0f, 1.0f, d.sympathetic);
    p.pitchGlide = 0.0f; // old patches may retain this value; pitch sweep was removed
    p.bodyBloom = FiniteClamp(in.bodyBloom, 0.0f, 1.0f, d.bodyBloom);
    p.closeLevel = FiniteClamp(in.closeLevel, 0.0f, 1.0f, d.closeLevel);
    p.farLevel = FiniteClamp(in.farLevel, 0.0f, 1.0f, d.farLevel);
    p.roomType = std::clamp(in.roomType, 0, 2);
    p.piezoLevel = FiniteClamp(in.piezoLevel, 0.0f, 1.0f, d.piezoLevel);
    p.invertPiezo = std::clamp(in.invertPiezo, 0, 1);
    p.stereoWidth = FiniteClamp(in.stereoWidth, 0.0f, 2.0f, d.stereoWidth);
    p.gentleWiden = 0; // retained API/patch input; Stereo Spread was removed
    p.preampDrive = FiniteClamp(in.preampDrive, 0.0f, 1.0f, d.preampDrive);
    p.warmth = FiniteClamp(in.warmth, -1.0f, 1.0f, d.warmth);
    p.compAmount = FiniteClamp(in.compAmount, 0.0f, 1.0f, d.compAmount);
    p.compAttack = FiniteClamp(in.compAttack, 1.0f, 50.0f, d.compAttack);
    p.compRelease = FiniteClamp(in.compRelease, 20.0f, 500.0f, d.compRelease);
    p.volume = FiniteClamp(in.volume, 0.0f, 1.0f, d.volume);
    p.oversampling = std::clamp(in.oversampling, 0, 3);
    p.velocityCurve = std::clamp(in.velocityCurve, 0, 3);
    p.polyphony = std::clamp(in.polyphony, 0, 2);
    p.tuneCents = FiniteClamp(in.tuneCents, -200.0f, 200.0f, d.tuneCents);
    p.detune = FiniteClamp(in.detune, 0.0f, 100.0f, d.detune);
    p.strikeJitter = FiniteClamp(in.strikeJitter, 0.0f, 100.0f, d.strikeJitter);
    p.velocityToHardness = FiniteClamp(in.velocityToHardness, 0.0f, 1.0f, d.velocityToHardness);
    p.velocityToPosition = FiniteClamp(in.velocityToPosition, 0.0f, 1.0f, d.velocityToPosition);
    p.velocityToDecay = FiniteClamp(in.velocityToDecay, 0.0f, 1.0f, d.velocityToDecay);
    p.velocityToCarving = FiniteClamp(in.velocityToCarving, 0.0f, 1.0f, d.velocityToCarving);
    p.velocityToCoupling = FiniteClamp(in.velocityToCoupling, 0.0f, 1.0f, d.velocityToCoupling);
    p.velocityToBloom = FiniteClamp(in.velocityToBloom, 0.0f, 1.0f, d.velocityToBloom);
    p.velocityToSympathetic = FiniteClamp(in.velocityToSympathetic, 0.0f, 1.0f, d.velocityToSympathetic);
    p.velocityToBuzz = FiniteClamp(in.velocityToBuzz, 0.0f, 1.0f, d.velocityToBuzz);
    p.velocityToTension = FiniteClamp(in.velocityToTension, 0.0f, 1.0f, d.velocityToTension);
    p.velocityToArtifacts = FiniteClamp(in.velocityToArtifacts, 0.0f, 1.0f, d.velocityToArtifacts);
    p.velocityToVariance = FiniteClamp(in.velocityToVariance, 0.0f, 1.0f, d.velocityToVariance);
    p.tuningScale = std::clamp(in.tuningScale, 0, static_cast<int>(ETuningScale::Count) - 1);
    p.noteOffMode = std::clamp(in.noteOffMode, 0, 2);
    p.modWheel = FiniteClamp(in.modWheel, 0.0f, 1.0f, d.modWheel);
    p.carving = FiniteClamp(in.carving, 0.0f, 1.0f, d.carving);
    p.membraneTension = FiniteClamp(in.membraneTension, 0.0f, 1.0f, d.membraneTension);
    return p;
}

void MarembaEngine::Reset() {
    for (int i = 0; i < kVoicePoolSize; ++i) {
        mVoices[i].FastKill();
        mVoices[i].SetSeed(VoiceSeed(i));
    }
    mRoutes.fill(StrikeRoute{});
    // A fresh engine projects every key's radiation anew; a readout cached
    // before the reset would make the replay differ.
    for (auto& entry : mRadiationCache) entry.model = -1;
    mRollStrokes = 0;
    mPrng = FastPRNG(kEngineSeed);
    mRollPrng = FastPRNG(kRollSeed);
    for (auto& key : mKeys) key = KeyState{};
    mHeldKeys = 0;
    mMeshDamping = false;
    mClock = 0.0;
    mCover = mParams.model == static_cast<int>(EMarimbaModel::KalimbaArtisan) ? mParams.modWheel : 0.0f;

    mActiveFactor = FactorFor(mParams.oversampling);
    mSwitchPending = false;
    mSwitchGain = 1.0f;
    mSwitchStep = 0.0f;
    mPendingCount = 0;
    mDampedPendingCount = 0;

    ConfigureForRate();
    FlushTails();

    mVolumeGain = MainVolumeGain(mParams.volume);
    SnapLiveControls();
    mControlCounter = 0;
    mTailsSilent = true;
    mMeshRunning = mSympatheticGain > 0.0f;
    mBodyRunning = mBodyGain > 0.0f;
}

void MarembaEngine::ConfigureForRate() {
    mMicMixer.SetSampleRate(mBaseSampleRate);
    mMainPickup.SetSampleRate(mBaseSampleRate);
    mLimiter.SetSampleRate(mBaseSampleRate);
    mLimiter.SetTimes(mParams.compAttack, mParams.compRelease);
    mLimiter.SetAmount(mParams.compAmount);
    mPreamp.SetSampleRate(mBaseSampleRate);
    mAppliedGlobalCents = GlobalCents();
    ConfigureModel();
}

void MarembaEngine::ConfigureModel() {
    mConfiguredModel = mParams.model;
    mConfiguredScale = mParams.tuningScale;
    ModelParams mp = GetModelParams(static_cast<EMarimbaModel>(mParams.model));
    mFrameBody.Configure(InternalRate(), mp.frameBodyFreq, mp.frameBodyQ, static_cast<EMarimbaModel>(mParams.model));
    mVelocityFrameBody.Configure(InternalRate(), mp.frameBodyFreq, mp.frameBodyQ, static_cast<EMarimbaModel>(mParams.model));
    ConfigureSympathetic(GlobalCents());
    mKalimbaBody.Configure(InternalRate());
    mKalimbaPickup.Configure(InternalRate(), EMarimbaModel::KalimbaArtisan);
    mKalimbaBody.SetCover(mCover);
    ApplyKalimbaAir();
}

// Tune is shared; absolute coupling is carried by each strike's box drive.
// Keeping the shared coefficient at unity lets a VOL route open Coupling
// from a zero base without dividing by that base or multiplying by zero.
void MarembaEngine::ApplyKalimbaAir() {
    mKalimbaBody.SetAir(mParams.resonatorTune, 1.0);
}

void MarembaEngine::FlushTails(bool resetRoomMotion) {
    mMicMixer.Reset(resetRoomMotion);
    mMainPickup.Reset();
    mPreamp.Reset();
    mLimiter.Reset();
    mOutputHeadroom.Reset();
    mPiezoDecimator.Reset();
    for (int k = 0; k < kNumDirectOuts; ++k) {
        mDirectRunning[k] = false;
    }
    mSympathetic.Reset();
    mFrameBody.Reset();
    mVelocitySympathetic.Reset();
    mVelocityFrameBody.Reset();
    mKalimbaBody.Reset();
    mKalimbaPickup.Reset();
    mKalimbaBodyRunning = false;
    mKalimbaPickupRunning = false;
    mBodyVelocityActive = mMeshVelocityActive = false;
    mBodyVelocityRunning = mMeshVelocityRunning = false;
    mQuietFrames = 0;
}

void MarembaEngine::SetParameters(const EngineParameters& params) {
    const EngineParameters p = Sanitize(params);
    const int previousPolyphony = mParams.polyphony;
    const float previousBuzz = mParams.buzzAmount, previousTension = mParams.membraneTension;
    const float previousCoupling = mParams.resonatorCoupling;
    const float previousDetune = mParams.detune;
    mParams = p;
    if (p.detune != previousDetune)
        for (int note = 0; note < 128; ++note)
            mKeyDetune[static_cast<std::size_t>(note)] = GetKeyDetuneOffset(note, p.detune);
    mMicMixer.SetRoomType(p.roomType);

    if (p.buzzAmount != previousBuzz || p.membraneTension != previousTension) {
        for (int i = 0; i < kVoicePoolSize; ++i)
            if (mVoices[i].IsActive())
                mVoices[i].SetBuzz(std::min(1.0f, p.buzzAmount + mRoutes[i].buzz),
                                  std::min(1.0f, p.membraneTension + mRoutes[i].tension));
    }
    if (p.resonatorCoupling != previousCoupling) {
        for (int i = 0; i < kVoicePoolSize; ++i)
            if (mVoices[i].IsActive())
                mRoutes[i].boxAir = std::pow(std::min(1.0f, p.resonatorCoupling + mRoutes[i].coupling) / 0.7, 0.75);
    }

    mLimiter.SetTimes(p.compAttack, p.compRelease);
    mLimiter.SetAmount(p.compAmount);

    // Oversampling change: switch at once when nothing sounds, otherwise fade
    // everything out at the old rate first (see RenderBatch / ApplyFactorSwitch).
    // Set back to the active factor during the fade, the switch is cancelled.
    const int wantedFactor = FactorFor(p.oversampling);
    if (mSwitchPending) {
        if (wantedFactor == mActiveFactor) CancelFactorSwitch();
    } else if (wantedFactor != mActiveFactor) {
        if (IsSilent()) {
            ApplyFactorSwitch();
        } else {
            mSwitchPending = true;
            mSwitchStep = static_cast<float>(1.0 / (kSwitchFadeSeconds * mBaseSampleRate));
        }
    }

    if (p.model != mConfiguredModel || p.tuningScale != mConfiguredScale) {
        ConfigureModel();
    } else if (p.detune != previousDetune) {
        ConfigureSympathetic(mAppliedGlobalCents);
    }
    ApplyGlobalTuning();
    ApplyKalimbaAir();
    // Wheel down: rolls end; the notes they struck ring on.
    if (p.modWheel <= kRollThreshold) for (auto& key : mKeys) key.rolling = false;

    if (p.polyphony != previousPolyphony) {
        EnforcePolyphony();
    }

    if (IsSilent()) {
        mVolumeGain = MainVolumeGain(p.volume);
        SnapLiveControls();
    }
}

std::array<float, 3> MarembaEngine::StemTrimsFor(int model) {
    if (model == static_cast<int>(EMarimbaModel::KalimbaArtisan))
        return {kKalimbaCloseStemTrim, kKalimbaRoomStemTrim, kKalimbaPiezoStemTrim};
    return {kCloseStemTrim, kRoomStemTrim, kPiezoStemTrim};
}

void MarembaEngine::SnapLiveControls() {
    mCover = mParams.model == static_cast<int>(EMarimbaModel::KalimbaArtisan) ? mParams.modWheel : 0.0f;
    mKalimbaBody.SetCover(mCover);
    mPreamp.SnapTone(mParams.warmth);
    mBodyGain = mParams.bodyBloom;
    mSympatheticGain = mParams.sympathetic;
    mBodyRunning = mBodyGain > 0.0f;
    mMeshRunning = mSympatheticGain > 0.0f;
    mBodyVelocityRunning = mBodyVelocityActive && mBodyGain < 1.0f;
    mMeshVelocityRunning = mMeshVelocityActive && mSympatheticGain < 1.0f;
    mCloseGain = mParams.closeLevel;
    mFarGain = mParams.farLevel * mParams.farLevel;
    mPiezoGain = mParams.piezoLevel;
    mPiezoPolarity = mParams.invertPiezo ? -1.0f : 1.0f;
    mStereoWidth = mParams.stereoWidth;
    mDrive = mParams.preampDrive;
    mMainModelGain = kMainModelCalibration[static_cast<std::size_t>(mParams.model)];
    mStemTrim = StemTrimsFor(mParams.model);
}

void MarembaEngine::SmoothLiveControls() {
    const auto smooth = [this](float& value, float target) {
        value += mVolumeAlpha * (target - value);
        if (std::abs(target - value) < 1.0e-5f) value = target;
    };
    smooth(mBodyGain, mParams.bodyBloom);
    smooth(mSympatheticGain, mParams.sympathetic);
    smooth(mCloseGain, mParams.closeLevel);
    smooth(mFarGain, mParams.farLevel * mParams.farLevel);
    smooth(mPiezoGain, mParams.piezoLevel);
    smooth(mPiezoPolarity, mParams.invertPiezo ? -1.0f : 1.0f);
    smooth(mStereoWidth, mParams.stereoWidth);
    smooth(mMainModelGain, kMainModelCalibration[static_cast<std::size_t>(mParams.model)]);
    // The nonlinear input-gain taper needs a gentler sweep than a linear
    // capture fader. Ten milliseconds keeps live changes free of sharp edges.
    mDrive += mDriveAlpha * (mParams.preampDrive - mDrive);
    if (std::abs(mParams.preampDrive - mDrive) < 1.0e-5f) mDrive = mParams.preampDrive;
    const std::array<float, 3> stemTrim = StemTrimsFor(mParams.model);
    for (std::size_t stem = 0; stem < stemTrim.size(); ++stem) smooth(mStemTrim[stem], stemTrim[stem]);
    // Finish the audible fade before clearing a bypassed branch. Re-enabling
    // it starts from silence rather than reviving an old stored resonance.
    const bool meshOn = mSympatheticGain > 0.0f;
    if (!meshOn && mMeshRunning) mSympathetic.Reset();
    mMeshRunning = meshOn;
    const bool bodyOn = mBodyGain > 0.0f;
    if (!bodyOn && mBodyRunning) mFrameBody.Reset();
    mBodyRunning = bodyOn;
    // Base and velocity additions keep separate states: live knob movement
    // scales stored base resonance immediately, while each strike retains its
    // own added excitation. The addition cannot exceed the remaining range.
    const bool velocityMeshOn = mMeshVelocityActive && mSympatheticGain < 1.0f;
    if (!velocityMeshOn && mMeshVelocityRunning) mVelocitySympathetic.Reset();
    mMeshVelocityRunning = velocityMeshOn;
    const bool velocityBodyOn = mBodyVelocityActive && mBodyGain < 1.0f;
    if (!velocityBodyOn && mBodyVelocityRunning) {
        mVelocityFrameBody.Reset();
        mKalimbaBody.ResetBloom();
    }
    mBodyVelocityRunning = velocityBodyOn;
}

void MarembaEngine::SetPitchBendCents(float cents) {
    mBendCents = FiniteClamp(cents, -kMaxPitchBendCents, kMaxPitchBendCents, 0.0f);
    ApplyGlobalTuning();
}

void MarembaEngine::ApplyGlobalTuning() {
    const float total = GlobalCents();
    if (total == mAppliedGlobalCents) return;
    mAppliedGlobalCents = total;
    for (auto& voice : mVoices) {
        if (voice.IsActive()) voice.Retune(total);
    }
    ConfigureSympathetic(total);
}

// The ringing-along keys are the instrument's keys, Detune included.
void MarembaEngine::ConfigureSympathetic(float globalCents) {
    mSympathetic.Configure(InternalRate(), globalCents, static_cast<EMarimbaModel>(mParams.model),
                           mParams.tuningScale, mKeyDetune.data());
    mVelocitySympathetic.Configure(InternalRate(), globalCents, static_cast<EMarimbaModel>(mParams.model),
                                  mParams.tuningScale, mKeyDetune.data());
}

void MarembaEngine::ApplyFactorSwitch() {
    for (auto& voice : mVoices) {
        voice.FastKill();
    }
    mActiveFactor = FactorFor(mParams.oversampling);
    ConfigureForRate();
    FlushTails();
    mSwitchPending = false;
    mSwitchGain = 1.0f;
    mSwitchStep = 0.0f;
    mTailsSilent = true;
    mControlCounter = 0;
    mVolumeGain = MainVolumeGain(mParams.volume);
    SnapLiveControls();

    // Notes that arrived during the fade start now, at the new rate.
    StartPendingNotes();
}

// The factor went back to the active one before the fade ended: nothing has to
// switch. The output gain ramps back up in RenderBatch (the voices themselves
// were never faded, so they ring on without a click) and the queued notes start.
void MarembaEngine::CancelFactorSwitch() {
    mSwitchPending = false;   // mSwitchStep is kept for the ramp back up
    StartPendingNotes();
}

// Those that were already queued when DampAll (transport stop) came are damped
// as well, exactly like the voices that were sounding then; later notes ring
// normally. Keys released while queued start released.
void MarembaEngine::StartPendingNotes() {
    const int count = mPendingCount;
    const int damped = mDampedPendingCount;
    mPendingCount = 0;
    mDampedPendingCount = 0;
    for (int i = 0; i < count; ++i) {
        const PendingNote& pending = mPendingNotes[i];
        const int slot = TriggerNote(pending.note, pending.velocity);
        if (i < damped) mVoices[slot].StartDamping();
        if (pending.released) mVoices[slot].Release();
        if (pending.releaseDamped) mVoices[slot].StartReleaseDamping();
    }
}

float MarembaEngine::MapVelocity(float vel) const {
    vel = std::clamp(vel, 0.001f, 1.0f);
    float curve = vel;
    switch (mParams.velocityCurve) {
        case 0: // Soft: light touch plays loud
            curve = std::sqrt(vel);
            break;
        case 2: // Hard: needs force to play loud
            curve = std::pow(vel, 1.55f);
            break;
        case 3: // Sigmoid (formerly Expressive)
            curve = vel * vel * (3.0f - 2.0f * vel);
            break;
        case 1: // Linear
        default:
            break;
    }
    // Soft floor (not a clamp): no two key velocities give the same strike
    return kVelocityFloor + (1.0f - kVelocityFloor) * curve;
}

float MarembaEngine::MapControlVelocity(float vel) const {
    // MIDI 1 is the unmodulated control and 127 is exactly the requested
    // depth. The physical excitation mapper and its drive floor stay separate.
    const float v = std::clamp((vel - 1.0f / 127.0f) * (127.0f / 126.0f), 0.0f, 1.0f);
    switch (mParams.velocityCurve) {
        case 0: return std::sqrt(v);
        case 2: return std::pow(v, 1.55f);
        case 3: return v * v * (3.0f - 2.0f * v);
        default: return v;
    }
}

int MarembaEngine::CountLiveVoices() const {
    int count = 0;
    for (const auto& voice : mVoices) {
        if (voice.IsLive()) count++;
    }
    return count;
}

// Quietest live voice by its phase-independent energy; released keys are
// preferred (4x weight on held keys) and a voice still being struck is only
// taken when nothing else is left.
int MarembaEngine::FindStealVictim() const {
    int best = -1;
    double bestScore = 0.0;
    for (int i = 0; i < kVoicePoolSize; ++i) {
        const auto& voice = mVoices[i];
        if (!voice.IsLive()) continue;
        double score = StealEnergy(voice) * (voice.IsReleased() ? 1.0 : 4.0);
        if (voice.IsStriking()) score += 1.0e30;
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

void MarembaEngine::EnforcePolyphony() {
    const int limit = VoiceLimitFor(mParams.polyphony);
    int live = CountLiveVoices();
    while (live > limit) {
        const int victim = FindStealVictim();
        if (victim < 0) break;
        mVoices[victim].StartFade(kPolyphonyFadeSeconds);
        --live;
    }
}

void MarembaEngine::NoteOn(int noteNumber, float velocity) {
    if (!(velocity > 0.0f)) {
        NoteOff(noteNumber);
        return;
    }
    if (noteNumber < 0 || noteNumber > 127) return;
    auto& key = mKeys[static_cast<std::size_t>(noteNumber)];
    if (!key.held) ++mHeldKeys;
    key.held = true;
    key.velocity = velocity;
    key.rolling = false;
    key.hand = 1;

    if (mSwitchPending) {
        // Rate change in progress: start the note right after the switch. A
        // full queue drops its oldest note, as stealing keeps the newest.
        if (mPendingCount == kMaxPendingNotes) {
            std::copy(mPendingNotes.begin() + 1, mPendingNotes.end(), mPendingNotes.begin());
            --mPendingCount;
            if (mDampedPendingCount > 0) --mDampedPendingCount;
        }
        mPendingNotes[mPendingCount++] = { noteNumber, velocity, false, false };
        return;
    }
    TriggerNote(noteNumber, velocity);
}

int MarembaEngine::TriggerNote(int noteNumber, float velocity, float positionOffset) {
    mTailsSilent = false;
    mQuietFrames = 0;
    mMeshDamping = false;

    const float mappedVel = MapVelocity(velocity);
    const EMarimbaModel model = static_cast<EMarimbaModel>(mParams.model);

    // 1. The same bar still ringing: re-strike it. A bar struck with another
    // model cannot be carried into the new strike (its twin mode, upper modes
    // and membrane may not exist in the new model): it fades out in its own slot
    // and the new strike takes a fresh one.
    int slot = -1;
    for (int i = 0; i < kVoicePoolSize; ++i) {
        if (mVoices[i].IsLive() && mVoices[i].GetNoteNumber() == noteNumber) {
            if (mVoices[i].GetModel() == model) {
                slot = i;
            } else {
                mVoices[i].StartFade(kStealFadeSeconds);
            }
            break;
        }
    }

    if (slot < 0) {
        // 2. At the polyphony limit: fade the quietest voice out in its own slot
        const int limit = VoiceLimitFor(mParams.polyphony);
        int live = CountLiveVoices();
        while (live >= limit) {
            const int victim = FindStealVictim();
            if (victim < 0) break;
            mVoices[victim].StartFade(kStealFadeSeconds);
            --live;
        }

        // 3. A free slot for the new note
        for (int i = 0; i < kVoicePoolSize; ++i) {
            if (!mVoices[i].IsActive()) {
                slot = i;
                break;
            }
        }

        // 4. All slots busy (live + fading, only possible with a chord of more
        // than kMaxPolyphony notes): hard-reuse the fading voice that is
        // quietest now, i.e. the one nearest the end of its fade.
        if (slot < 0) {
            double bestEnergy = 0.0;
            for (int i = 0; i < kVoicePoolSize; ++i) {
                if (!mVoices[i].IsFading()) continue;
                const double fade = mVoices[i].GetFadeGain();
                const double energy = fade * fade * StealEnergy(mVoices[i]);
                if (slot < 0 || energy < bestEnergy) {
                    slot = i;
                    bestEnergy = energy;
                }
            }
            if (slot < 0) slot = 0;
            mVoices[slot].FastKill();
        }
    }

    const float keyDetuneCents = GetKeyDetuneOffset(noteNumber, mParams.detune)
        + static_cast<float>(TuningOffsetCents(mParams.tuningScale, noteNumber));

    // VOL adds a fraction of the full knob range above its base setting.
    // Evaluate at the strike boundary, including restrikes and queued notes,
    // so separate notes retain independent settings without changing the knobs.
    const float controlVelocity = MapControlVelocity(velocity);
    const auto velocityOffset = [controlVelocity](float depth) {
        return depth * controlVelocity;
    };
    const auto modulate = [&velocityOffset](float base, float depth) {
        return std::min(1.0f, base + velocityOffset(depth));
    };
    const float hardness = modulate(mParams.malletHardness, mParams.velocityToHardness);
    const float position = modulate(mParams.strikePosition, mParams.velocityToPosition);
    const float decayOffset = velocityOffset(mParams.velocityToDecay);
    const float decay = decayOffset > 0.0f
        ? DecayFromControl(DecayToControl(mParams.decay) + decayOffset) : mParams.decay;
    const float carving = modulate(mParams.carving, mParams.velocityToCarving);
    const float coupling = modulate(mParams.resonatorCoupling, mParams.velocityToCoupling);
    const float artifacts = modulate(mParams.artifacts, mParams.velocityToArtifacts);
    const float varianceOffset = velocityOffset(mParams.velocityToVariance);
    const float variance = varianceOffset > 0.0f
        ? std::min(100.0f, mParams.strikeJitter + 100.0f * varianceOffset) : mParams.strikeJitter;
    // Bloom and Sympathetic are shared resonances: a strike's velocity sets
    // how hard it drives them, which is its own Bloom or Sympathetic level.
    // The kalimba box's air coupling follows Coupling as the voice's loading
    // by that box does (KalimbaBody's 0.75 law).
    StrikeRoute& route = mRoutes[static_cast<std::size_t>(slot)];
    route.body = velocityOffset(mParams.velocityToBloom);
    route.sympathetic = velocityOffset(mParams.velocityToSympathetic);
    route.coupling = velocityOffset(mParams.velocityToCoupling);
    route.boxAir = std::pow(coupling / 0.7, 0.75);
    route.buzz = velocityOffset(mParams.velocityToBuzz);
    route.tension = velocityOffset(mParams.velocityToTension);
    mBodyVelocityActive = mBodyVelocityActive || route.body > 0.0f;
    mMeshVelocityActive = mMeshVelocityActive || route.sympathetic > 0.0f;

    mVoices[slot].Trigger(noteNumber, mappedVel, model,
                          hardness, position,
                          mParams.resonatorTune, coupling,
                          decay, std::min(1.0f, mParams.buzzAmount + route.buzz), artifacts,
                          mParams.pitchGlide,
                          InternalRate(), mPrng,
                          mAppliedGlobalCents, keyDetuneCents,
                          mParams.malletType,
                          variance,
                          positionOffset,
                          StrikeOptions{carving, std::min(1.0f, mParams.membraneTension + route.tension), mParams.noteOffMode == 2,
                                        &mRadiationCache[static_cast<std::size_t>(std::clamp(noteNumber, 0, 127))]});
    return slot;
}

void MarembaEngine::NoteOff(int noteNumber) {
    // Ring mode: sound-neutral by design (bars ring freely); released keys are
    // only preferred when a voice has to be stolen (also a key still queued
    // during an oversampling fade). Damp mode: the hand stops the key. Dead
    // mode: the mallet (thumb) pressed on the key since the stroke lifts off.
    if (noteNumber >= 0 && noteNumber < 128) {
        auto& key = mKeys[static_cast<std::size_t>(noteNumber)];
        if (key.held) --mHeldKeys;
        key.held = false;
        key.rolling = false;
    }
    for (auto& voice : mVoices) {
        if (voice.IsLive() && voice.GetNoteNumber() == noteNumber) {
            voice.Release();
            if (mParams.noteOffMode == 1) voice.StartReleaseDamping();
        }
    }
    for (int i = 0; i < mPendingCount; ++i) {
        if (mPendingNotes[i].note == noteNumber) {
            mPendingNotes[i].released = true;
            if (mParams.noteOffMode == 1) mPendingNotes[i].releaseDamped = true;
        }
    }
}

void MarembaEngine::DampAll() {
    // Transport stop silences rolls too: their keys count as released.
    for (auto& key : mKeys) key.held = key.rolling = false;
    mHeldKeys = 0;
    // The hands that stop the struck keys also stop the ones ringing along.
    mMeshDampPole = std::exp(-GetModelParams(static_cast<EMarimbaModel>(mParams.model)).handDampRate / InternalRate());
    mMeshDamping = true;
    for (auto& voice : mVoices) {
        if (voice.IsActive()) voice.StartDamping();
    }
    // Notes queued during an oversampling fade are damped when they start
    mDampedPendingCount = mPendingCount;
}

int MarembaEngine::GetActiveVoiceCount() const {
    int count = 0;
    for (const auto& voice : mVoices) {
        if (voice.IsActive()) count++;
    }
    return count;
}

bool MarembaEngine::IsSilent() const {
    // mTailsSilent is only set while no voice is active and is cleared by every note.
    return mTailsSilent && !mSwitchPending && mPendingCount == 0 && !RollsPending();
}

// A held key with the wheel up rolls even after its bar has died away.
bool MarembaEngine::RollsPending() const {
    return mHeldKeys > 0 && mParams.modWheel > kRollThreshold
        && mParams.model != static_cast<int>(EMarimbaModel::KalimbaArtisan);
}

void MarembaEngine::ControlTick() {
    for (auto& voice : mVoices) {
        if (voice.IsActive()) voice.UpdateEnvelope();
    }
    // Zero decayed shared resonators and float filter states before they reach
    // subnormal values (the main bus sits at exact 0 when every mic level is 0)
    mSympathetic.FlushTiny();
    mFrameBody.FlushTiny();
    mVelocitySympathetic.FlushTiny();
    mVelocityFrameBody.FlushTiny();
    mKalimbaBody.FlushTiny();
    mKalimbaPickup.FlushTiny();
    mPreamp.FlushTiny();
    mLimiter.FlushTiny();
    mPiezoDecimator.FlushTiny();
    mMicMixer.FlushTiny();
    mMainPickup.FlushTiny();
}

// Held keys roll while the mod wheel is up (mallet models): two mallets
// alternate a little either side of the set position, the second slightly
// softer, with a player's timing and force scatter. Each stroke re-strikes the
// ringing bar through the physical contact, so the ring builds and breathes.
void MarembaEngine::AdvanceRolls() {
    const float wheel = mParams.modWheel;
    if (wheel <= kRollThreshold || mParams.model == static_cast<int>(EMarimbaModel::KalimbaArtisan)) return;
    const double rate = kRollMinRate + (kRollMaxRate - kRollMinRate) * wheel;
    const double interval = mBaseSampleRate / rate;
    for (int note = 0; note < 128; ++note) {
        auto& key = mKeys[static_cast<std::size_t>(note)];
        if (!key.held) continue;
        if (!key.rolling) {
            key.rolling = true;
            key.next = mClock + interval;
            continue;
        }
        if (mClock < key.next) continue;
        const float jitter = mRollPrng.NextGaussian();
        const float hand = key.hand ? 0.92f : 1.0f;
        const float velocity = std::clamp(key.velocity * hand * (0.78f + 0.22f * wheel)
                                          * (1.0f + 0.05f * mRollPrng.NextGaussian()), 0.01f, 1.0f);
        TriggerNote(note, velocity, key.hand ? kRollHandOffset : -kRollHandOffset);
        ++mRollStrokes;
        key.hand ^= 1;
        key.next += interval * (1.0 + 0.07 * jitter);
    }
}

void MarembaEngine::RenderBatch(float* outMainL, float* outMainR,
                                float* outCloseL, float* outCloseR,
                                float* outFarL, float* outFarR,
                                float* outPiezo,
                                int frameCount)
{
    if (frameCount <= 0 || !outMainL || !outMainR) return;
    frameCount = std::min(frameCount, kMaxFrames);
    float* const direct[kNumDirectOuts] = { outCloseL, outCloseR, outFarL, outFarR, outPiezo };

    auto zeroFrames = [&](int from) {
        for (int f = from; f < frameCount; ++f) {
            outMainL[f] = 0.0f;
            outMainR[f] = 0.0f;
            for (int k = 0; k < kNumDirectOuts; ++k) {
                if (direct[k]) direct[k][f] = 0.0f;
            }
        }
    };

    // Idle: nothing sounding and every tail flushed
    if (IsSilent()) {
        zeroFrames(0);
        return;
    }

    const float volumeTarget = MainVolumeGain(mParams.volume);
    const bool kalimba = mParams.model == static_cast<int>(EMarimbaModel::KalimbaArtisan);
    const float coverAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (kCoverSmoothingSeconds * mBaseSampleRate)));
    // Receiver identities do not change inside a batch, including a pending
    // rate switch. Resolve their physical source slots once, not at every
    // oversampled step of each receiver.
    std::array<int, 128> receiverSources{};
    std::array<float, SympatheticMesh::kNumResonators> receiverPressure;
    for (int key = 0; key < mSympathetic.ReceiverCount(); ++key)
        receiverSources[static_cast<std::size_t>(key)] = CaptureSourceIndex(
            static_cast<EMarimbaModel>(mParams.model), mSympathetic.ReceiverNote(key));
    struct ActiveRoute {
        int slot, note, source;
        bool tine, mesh;
        float body, sympathetic;
    };
    std::array<ActiveRoute, kVoicePoolSize> active;
    int activeCount = 0, cachedRollStrokes = mRollStrokes, cachedFactor = mActiveFactor;
    for (int f = 0; f < frameCount; ++f) {
        SmoothLiveControls();
        if (!mSwitchPending) AdvanceRolls();
        mClock += 1.0;
        const float coverTarget = kalimba ? mParams.modWheel : 0.0f;
        if (mCover != coverTarget) {
            mCover += coverAlpha * (coverTarget - mCover);
            if (std::abs(coverTarget - mCover) < 1.0e-4f) mCover = coverTarget;
            mKalimbaBody.SetCover(mCover);
        }
        const bool meshOn = mMeshRunning;
        const bool bodyOn = mBodyRunning;
        const float bodyHeadroom = 1.0f - mBodyGain;
        const float meshHeadroom = 1.0f - mSympatheticGain;
        const float bodyInverse = bodyHeadroom > 0.0f ? 1.0f / bodyHeadroom : 0.0f;
        const float meshInverse = meshHeadroom > 0.0f ? 1.0f / meshHeadroom : 0.0f;
        const int factor = mActiveFactor;
        // Host events occur between batches. Within one batch only a roll or
        // a quality switch's queued notes can add/reuse slots. Preserve their
        // ascending summation order, and still check retirement per substep.
        if (f == 0 || cachedRollStrokes != mRollStrokes || cachedFactor != factor) {
            activeCount = 0;
            for (int v = 0; v < kVoicePoolSize; ++v) {
                if (!mVoices[v].IsActive()) continue;
                const int note = mVoices[v].GetNoteNumber();
                const auto model = mVoices[v].GetModel();
                const bool tine = model == EMarimbaModel::KalimbaArtisan;
                active[static_cast<std::size_t>(activeCount++)] = {
                    v, note, CaptureSourceIndex(model, note), tine,
                    tine == mSympathetic.IsTine(), 0.0f, 0.0f};
            }
            cachedRollStrokes = mRollStrokes;
            cachedFactor = factor;
        }
        // Smoothed shared controls still change once per host frame.
        for (int index = 0; index < activeCount; ++index) {
            auto& cached = active[static_cast<std::size_t>(index)];
            const auto& route = mRoutes[static_cast<std::size_t>(cached.slot)];
            cached.body = std::min(1.0f, route.body * bodyInverse);
            cached.sympathetic = std::min(1.0f, route.sympathetic * meshInverse);
        }
        bool anyVoice = false;
        mMicMixer.BeginFrame(factor);
        mMainPickup.BeginFrame(factor);

        for (int s = 0; s < factor; ++s) {
            float accPiezo = 0.0f;

            float totalMechanical = 0.0f;
            float velocityMechanical = 0.0f;
            KalimbaBoxDrive box;

            if (meshOn) {
                mSympathetic.BeginSample();
            }
            if (mMeshVelocityRunning) mVelocitySympathetic.BeginSample();

            for (int index = 0; index < activeCount; ++index) {
                const auto& cached = active[static_cast<std::size_t>(index)];
                const int v = cached.slot;
                if (mVoices[v].IsActive()) {
                    // A final faded sample is valid even though ProcessSample
                    // releases the slot. Its travelling wave keeps its key.
                    const int noteNum = cached.note;
                    float voiceClose = 0.0f;
                    float voiceFar = 0.0f;
                    float voicePiezo = 0.0f;
                    float voiceEars[2]{};

                    if (mVoices[v].ProcessSample(voiceClose, voiceFar, voicePiezo, voiceEars)) {
                        anyVoice = true;
                        mMicMixer.Add(cached.source, s,
                                      voiceEars[0], voiceEars[1], voiceFar);
                        // One Kalimba board sensor reads summed tine force;
                        // per-voice nominal pickups must not bypass it.
                        if (!cached.tine) {
                            accPiezo += voicePiezo;
                            mMainPickup.Add(cached.source, s, voicePiezo);
                        }

                        const StrikeRoute& route = mRoutes[static_cast<std::size_t>(v)];
                        const float body = cached.body;
                        totalMechanical += mVoices[v].GetMechanicalSample();
                        velocityMechanical += mVoices[v].GetMechanicalSample() * body;
                        const bool tine = cached.tine;
                        const double bridge = tine ? mVoices[v].GetBridgeForce() : 0.0;
                        if (tine) box.Add(bridge, route.boxAir, body,
                                          mTineSide[static_cast<std::size_t>(std::clamp(noteNum, 0, 127))]);

                        if (cached.mesh) {
                            const double drive = tine ? bridge * 1.0e-2 : mVoices[v].GetMechanicalSample();
                            if (meshOn) mSympathetic.AccumulateVoice(noteNum, drive);
                            if (mMeshVelocityRunning)
                                mVelocitySympathetic.AccumulateVoice(noteNum, drive * cached.sympathetic);
                        }
                    }
                }
            }

            // Frame Body Bloom (low-frequency frame & rail mass resonance); a
            // kalimba's box is the physical body below.
            float bodySound = 0.0f;
            if (bodyOn) {
                // The selected Kalimba uses its own box, but an earlier bar's
                // frame must still decay rather than resume after switching back.
                const float frame = mFrameBody.Process(kalimba ? 0.0f : totalMechanical);
                if (!kalimba) bodySound = frame * mBodyGain * kBodyBloomGain;
            }
            if (mBodyVelocityRunning) {
                const float frame = mVelocityFrameBody.Process(kalimba ? 0.0f : velocityMechanical);
                if (!kalimba) bodySound += frame * bodyHeadroom * kBodyBloomGain;
            }
            // The kalimba's box radiates what every tine's bridge pushes into it.
            float boxPickup = 0.0f;
            double boardStrain = 0.0;
            if (box.board != 0.0 || mKalimbaBodyRunning) {
                double close = 0.0, far = 0.0, plate = 0.0, side = 0.0;
                double emitted[2]{};
                // Body weights the box's resonances against its board.
                mKalimbaBody.Process(box, 0.6 + 0.8 * mBodyGain, 0.8 * bodyHeadroom, close, far, plate, side, emitted, &boardStrain);
                const float boardSound = static_cast<float>(emitted[0] * kKalimbaBodyGain);
                const float holeSound = static_cast<float>(emitted[1] * kKalimbaBodyGain);
                // The former board/hole sum contributed half to each Close
                // channel. Preserve that source calibration before assigning
                // real propagation, instead of doubling a compact source.
                mMicMixer.AddCompactPressure(kCaptureBoardSource, s, .5f * boardSound);
                mMicMixer.AddCompactPressure(kCaptureHoleSource, s, .5f * holeSound);
                mKalimbaBodyRunning = box.board != 0.0 || mKalimbaBody.Energy() > 1.0e-20;
            }
            // SI board strain enters the PVDF/charge-amplifier chain. Retain
            // capacitor charge after the last physical board sample.
            if (boardStrain != 0.0 || mKalimbaPickupRunning) {
                boxPickup = static_cast<float>(mKalimbaPickup.ProcessSurfaceStrain(boardStrain));
                mKalimbaPickupRunning = mKalimbaPickup.StateEnergy() > 1.0e-28;
            }

            // Inter-Bar Sympathetic Resonance Halo ("Singing Rack")
            if (meshOn) {
                mSympathetic.AdvanceAndRead(mSympatheticGain, receiverPressure.data());
                for (int key = 0; key < mSympathetic.ReceiverCount(); ++key) {
                    const float sound = receiverPressure[static_cast<std::size_t>(key)];
                    // Legacy receiver pan gains summed to 1 and the Close
                    // halo scale was .7: its equal-ear reference pressure is
                    // .7/2. Geometry now determines each ear's actual level.
                    mMicMixer.AddCompactPressure(receiverSources[static_cast<std::size_t>(key)],
                                                s, .35f * sound);
                }
                if (mMeshDamping) mSympathetic.Damp(mMeshDampPole);
            }
            if (mMeshVelocityRunning) {
                mVelocitySympathetic.AdvanceAndRead(meshHeadroom, receiverPressure.data());
                for (int key = 0; key < mVelocitySympathetic.ReceiverCount(); ++key) {
                    const float sound = receiverPressure[static_cast<std::size_t>(key)];
                    mMicMixer.AddCompactPressure(receiverSources[static_cast<std::size_t>(key)],
                                                s, .35f * sound);
                }
                if (mMeshDamping) mVelocitySympathetic.Damp(mMeshDampPole);
            }

            // Frame sound likewise used half its nominal level per Close ear.
            mMicMixer.AddCompactPressure(kCaptureFrameSource, s, .5f * bodySound);
            accPiezo += boxPickup;
            mMainPickup.Add(kCaptureBoardSource, s, boxPickup);

            // The electrical sensor stays clean, including its direct output.
            // Main Drive is applied once, after the complete capture mix.
            mSubBufPiezo[s] = accPiezo;
        }

        if (mMicMixer.HasInvalidInput()) {
            Reset();
            zeroFrames(0);
            return;
        }
        float capture[kNumDirectOuts]{};
        mMicMixer.Process(capture[0], capture[1], capture[2], capture[3]);
        capture[4] = mPiezoDecimator.DownsampleFrame(mSubBufPiezo, factor) * mPiezoPolarity;
        // Main aligns the electrical arrival to each source's acoustic flight.
        // The independent raw Piezo capture above keeps its original timing.
        // Both electrical paths share the same smooth polarity reversal.
        // Apply it after their independent timing and before Main saturation.
        const float mainPickup = mMainPickup.Process() * mPiezoPolarity;
        float mixedL = 0.0f, mixedR = 0.0f, preL = 0.0f, preR = 0.0f;
        CaptureScene::Mix(capture[0], capture[1], capture[2], capture[3], mainPickup,
                          mCloseGain, mFarGain, mPiezoGain, mStereoWidth, mixedL, mixedR);
        // The Main preamp uses antiderivative antialiasing on the complete
        // host-rate capture mix. All five direct channels remain untouched.
        mPreamp.Process(mixedL, mixedR, mDrive, mParams.warmth, preL, preR);
        // Smooth Volume before both dynamics stages, so turning it down buys
        // actual headroom instead of attenuating an already limited signal.
        mVolumeGain += mVolumeAlpha * (volumeTarget - mVolumeGain);
        preL *= mVolumeGain * kMainOutputCalibrationGain * mMainModelGain;
        preR *= mVolumeGain * kMainOutputCalibrationGain * mMainModelGain;
        float mainL = 0.0f, mainR = 0.0f;
        mLimiter.Process(preL * kMainCalibrationGain, preR * kMainCalibrationGain, mainL, mainR);

        // The quality-switch fade remains after the shared lookahead delay.
        std::array<float, 7> frame = {mainL, mainR, 0, 0, 0, 0, 0};

        // Complete microphone/electrical captures, before the main mix and
        // dynamics. Each carries only its fixed calibration: Volume and the
        // other Main-mix controls never reach a stem.
        static_assert(kNumDirectOuts == 5, "a stereo Close pair, a stereo Room pair and one Piezo: three trims");
        for (int k = 0; k < kNumDirectOuts; ++k) {
            if (direct[k]) {
                if (!mDirectRunning[k]) {
                    mOutputHeadroom.ResetDirect(k);
                    mDirectRunning[k] = true;
                }
                frame[k + 2] = capture[k] * (CaptureScene::kMasterMixScale * mStemTrim[static_cast<std::size_t>(k / 2)]);
            } else {
                mDirectRunning[k] = false;
            }
        }

        // Do not let headroom protection conceal an unstable model or poison
        // its queued state. Preserve the existing +60 dB/non-finite fail-safe
        // on the unprotected frame as well as on the final output buffers.
        bool validFrame = true;
        for (float value : frame) validFrame = validFrame && std::abs(value) <= kMaxOutputLevel;
        if (!validFrame) {
            Reset();
            zeroFrames(0);
            return;
        }

        // Independent Main/Close/Room/Piezo gain controllers anticipate
        // overloads by 2 ms. Quiet audio retains its level; one hot capture
        // cannot turn down another. Every stereo pair shares its gain.
        mOutputHeadroom.Process(frame, kMainPeakCeiling);
        outMainL[f] = frame[0] * mSwitchGain;
        outMainR[f] = frame[1] * mSwitchGain;
        for (int k = 0; k < kNumDirectOuts; ++k)
            if (direct[k]) direct[k][f] = frame[k + 2] * mSwitchGain;

        // Silence detection: no voice and every tail below -180 dBFS for longer
        // than the longest room echo gap -> flush all tails and go idle.
        if (!anyVoice && !mSwitchPending) {
            float peak = std::max(std::abs(mainL), std::abs(mainR));
            for (float value : capture) peak = std::max(peak, std::abs(value));
            mQuietFrames = (peak < kSilenceLevel) ? mQuietFrames + 1 : 0;
            if (mQuietFrames >= mQuietFramesNeeded) {
                // Clear inaudible audio, retaining the room's slow scattering
                // phase across rests. Explicit Reset still replays exactly.
                FlushTails(false);
                mTailsSilent = true;
                mControlCounter = 0;
                mVolumeGain = volumeTarget;
                SnapLiveControls();
                mSwitchGain = 1.0f;   // a cancelled switch may still be ramping back up
                zeroFrames(f + 1);
                break;
            }
        } else {
            mQuietFrames = 0;
        }

        // Oversampling change: after the fade, reset everything and switch
        // (or, after a cancelled switch, ramp the output back up)
        if (mSwitchPending) {
            mSwitchGain -= mSwitchStep;
            if (mSwitchGain <= 0.0f) {
                ApplyFactorSwitch();
                if (IsSilent()) {
                    zeroFrames(f + 1);
                    break;
                }
            }
        } else if (mSwitchGain < 1.0f) {
            mSwitchGain = std::min(1.0f, mSwitchGain + mSwitchStep);
        }

        // Control rate (fixed grid of output frames, independent of batch splits)
        if (++mControlCounter >= kControlInterval) {
            mControlCounter = 0;
            ControlTick();
        }
    }

    // Output guard (last resort; voices and mesh have their own guards): never
    // hand non-finite or runaway samples to the host. NaN fails the comparison.
    auto sane = [](float x) { return std::abs(x) <= kMaxOutputLevel; };
    bool ok = true;
    for (int f = 0; f < frameCount && ok; ++f) {
        ok = sane(outMainL[f]) && sane(outMainR[f]);
        for (int k = 0; k < kNumDirectOuts && ok; ++k) {
            if (direct[k]) ok = sane(direct[k][f]);
        }
    }
    if (!ok) {
        Reset();
        zeroFrames(0);
    }
}

} // namespace maremba
