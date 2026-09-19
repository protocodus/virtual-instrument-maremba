#include "MarembaEngine.h"
#include <algorithm>
#include <cmath>
#include "DspMath.h"

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
constexpr double kTailHoldSeconds = 0.1;         // > longest room echo gap
constexpr float kSilenceLevel = 1.0e-9f;
constexpr float kMaxPitchBendCents = 1200.0f;

// Frame body contribution at bodyBloom = 1 (about -26 dB below the bar peak)
constexpr float kBodyBloomGain = 0.040f;

// Fixed -6 dB headroom on the pre-fader direct taps (close/far/piezo are summed
// before the compressor and saturator, so they run hotter than main).
constexpr float kDirectOutTrim = 0.5f;

// Steal score of a voice: its energy now (not the control-rate cache, which is
// stale or 0 right after a strike); a non-finite voice is the first victim.
inline double StealEnergy(const MarembaVoice& voice) {
    const double e = voice.ComputeEnergy();
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
    // The only allocation: room delay lines for the highest internal rate this
    // instance can run at (8x the host rate; a host-rate change re-creates the engine).
    mMicMixer.Allocate(mBaseSampleRate * kMaxOversample);
    mVolumeAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (kVolumeSmoothingSeconds * mBaseSampleRate)));
    mQuietFramesNeeded = static_cast<int>(kTailHoldSeconds * mBaseSampleRate);
    Reset();
}

EngineParameters MarembaEngine::Sanitize(const EngineParameters& in) {
    const EngineParameters d; // defaults for non-finite values
    EngineParameters p;
    p.model = std::clamp(in.model, 0, 3);
    p.malletType = std::clamp(in.malletType, 0, 3);
    p.malletHardness = FiniteClamp(in.malletHardness, 0.0f, 1.0f, d.malletHardness);
    p.strikePosition = FiniteClamp(in.strikePosition, 0.0f, 1.0f, d.strikePosition);
    p.resonatorTune = FiniteClamp(in.resonatorTune, -50.0f, 50.0f, d.resonatorTune);
    p.resonatorCoupling = FiniteClamp(in.resonatorCoupling, 0.0f, 1.0f, d.resonatorCoupling);
    p.decay = FiniteClamp(in.decay, 0.10f, 8.0f, d.decay);
    p.buzzAmount = FiniteClamp(in.buzzAmount, 0.0f, 1.0f, d.buzzAmount);
    p.artifacts = FiniteClamp(in.artifacts, 0.0f, 1.0f, d.artifacts);
    p.sympathetic = FiniteClamp(in.sympathetic, 0.0f, 1.0f, d.sympathetic);
    p.pitchGlide = FiniteClamp(in.pitchGlide, 0.0f, 1.0f, d.pitchGlide);
    p.bodyBloom = FiniteClamp(in.bodyBloom, 0.0f, 1.0f, d.bodyBloom);
    p.closeLevel = FiniteClamp(in.closeLevel, 0.0f, 1.0f, d.closeLevel);
    p.farLevel = FiniteClamp(in.farLevel, 0.0f, 1.0f, d.farLevel);
    p.piezoLevel = FiniteClamp(in.piezoLevel, 0.0f, 1.0f, d.piezoLevel);
    p.stereoWidth = FiniteClamp(in.stereoWidth, 0.0f, 2.0f, d.stereoWidth);
    p.preampDrive = FiniteClamp(in.preampDrive, 0.0f, 1.0f, d.preampDrive);
    p.warmth = FiniteClamp(in.warmth, -1.0f, 1.0f, d.warmth);
    p.compAmount = FiniteClamp(in.compAmount, 0.0f, 1.0f, d.compAmount);
    p.compAttack = FiniteClamp(in.compAttack, 1.0f, 50.0f, d.compAttack);
    p.compRelease = FiniteClamp(in.compRelease, 20.0f, 500.0f, d.compRelease);
    p.volume = FiniteClamp(in.volume, 0.0f, 1.0f, d.volume);
    p.oversampling = std::clamp(in.oversampling, 0, 2);
    p.velocityCurve = std::clamp(in.velocityCurve, 0, 3);
    p.polyphony = std::clamp(in.polyphony, 0, 2);
    p.tuneCents = FiniteClamp(in.tuneCents, -200.0f, 200.0f, d.tuneCents);
    p.detune = FiniteClamp(in.detune, 0.0f, 100.0f, d.detune);
    p.strikeJitter = FiniteClamp(in.strikeJitter, 0.0f, 100.0f, d.strikeJitter);
    return p;
}

void MarembaEngine::Reset() {
    for (int i = 0; i < kVoicePoolSize; ++i) {
        mVoices[i].FastKill();
        mVoices[i].SetSeed(VoiceSeed(i));
    }
    mPrng = FastPRNG(kEngineSeed);

    mActiveFactor = FactorFor(mParams.oversampling);
    mSwitchPending = false;
    mSwitchGain = 1.0f;
    mSwitchStep = 0.0f;
    mPendingCount = 0;
    mDampedPendingCount = 0;

    ConfigureForRate();
    FlushTails();

    mVolumeGain = mParams.volume * mParams.volume;
    mControlCounter = 0;
    mTailsSilent = true;
    mMeshRunning = mParams.sympathetic > 0.001f;
    mBodyRunning = mParams.bodyBloom > 0.001f;
    mRoomRunning = mParams.farLevel > 0.001f;
}

void MarembaEngine::ConfigureForRate() {
    const double rate = InternalRate();
    mMicMixer.SetSampleRate(rate);   // changes the room's active lengths, no allocation
    mCompressor.SetSampleRate(rate);
    mCompressor.SetTimes(mParams.compAttack, mParams.compRelease);
    mPreamp.SetSampleRate(rate);
    mAppliedGlobalCents = GlobalCents();
    mSympathetic.Configure(rate, mAppliedGlobalCents);
    ConfigureModel();
}

void MarembaEngine::ConfigureModel() {
    mConfiguredModel = mParams.model;
    ModelParams mp = GetModelParams(static_cast<EMarimbaModel>(mParams.model));
    mFrameBody.Configure(InternalRate(), mp.frameBodyFreq, mp.frameBodyQ);
}

void MarembaEngine::FlushTails() {
    mMicMixer.Reset();
    mPreamp.Reset();
    mCompressor.Reset();
    mOversampler.Reset();
    for (int k = 0; k < kNumDirectOuts; ++k) {
        mDirectDecimators[k].Reset();
        mDirectRunning[k] = false;
    }
    mSympathetic.Reset();
    mFrameBody.Reset();
    mQuietFrames = 0;
}

void MarembaEngine::SetParameters(const EngineParameters& params) {
    const EngineParameters p = Sanitize(params);
    const int previousPolyphony = mParams.polyphony;
    mParams = p;

    mCompressor.SetTimes(p.compAttack, p.compRelease);

    // Oversampling change: switch at once when nothing sounds, otherwise fade
    // everything out at the old rate first (see RenderBatch / ApplyFactorSwitch).
    if (!mSwitchPending && FactorFor(p.oversampling) != mActiveFactor) {
        if (IsSilent()) {
            ApplyFactorSwitch();
        } else {
            mSwitchPending = true;
            mSwitchStep = static_cast<float>(1.0 / (kSwitchFadeSeconds * mBaseSampleRate));
        }
    }

    if (p.model != mConfiguredModel) {
        ConfigureModel();
    }
    ApplyGlobalTuning();

    if (p.polyphony != previousPolyphony) {
        EnforcePolyphony();
    }

    // A bypassed mesh, body or room is cleared once, so it restarts cleanly later
    // (no stale tail bursts back when the knob comes up again).
    const bool meshOn = p.sympathetic > 0.001f;
    if (!meshOn && mMeshRunning) mSympathetic.Reset();
    mMeshRunning = meshOn;
    const bool bodyOn = p.bodyBloom > 0.001f;
    if (!bodyOn && mBodyRunning) mFrameBody.Reset();
    mBodyRunning = bodyOn;
    const bool roomOn = p.farLevel > 0.001f;   // AcousticRoom's bypass threshold (roomLevel = farLevel)
    if (!roomOn && mRoomRunning) mMicMixer.Reset();
    mRoomRunning = roomOn;

    if (IsSilent()) {
        mVolumeGain = p.volume * p.volume; // no ramp from a stale gain when sound resumes
    }
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
    mSympathetic.Configure(InternalRate(), total);
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
    mVolumeGain = mParams.volume * mParams.volume;

    // Notes that arrived during the fade start now, at the new rate. Those that
    // were already queued when DampAll (transport stop) came are damped as well,
    // exactly like the voices that were sounding then; later notes ring normally.
    const int count = mPendingCount;
    const int damped = mDampedPendingCount;
    mPendingCount = 0;
    mDampedPendingCount = 0;
    for (int i = 0; i < count; ++i) {
        const int slot = TriggerNote(mPendingNotes[i].note, mPendingNotes[i].velocity);
        if (i < damped) mVoices[slot].StartDamping();
    }
}

float MarembaEngine::MapVelocity(float vel) const {
    vel = std::clamp(vel, 0.001f, 1.0f);
    switch (mParams.velocityCurve) {
        case 0: // Soft: light touch plays loud
            return std::sqrt(vel);
        case 2: // Hard: needs force to play loud
            return std::pow(vel, 1.55f);
        case 3: // Expressive
            return vel * vel * (3.0f - 2.0f * vel);
        case 1: // Linear
        default:
            return vel;
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

    if (mSwitchPending) {
        // Rate change in progress: start the note right after the switch
        if (mPendingCount < kMaxPendingNotes) {
            mPendingNotes[mPendingCount++] = { noteNumber, velocity };
        }
        return;
    }
    TriggerNote(noteNumber, velocity);
}

int MarembaEngine::TriggerNote(int noteNumber, float velocity) {
    mTailsSilent = false;
    mQuietFrames = 0;

    const float mappedVel = MapVelocity(velocity);

    // 1. The same bar still ringing: re-strike it
    int slot = -1;
    for (int i = 0; i < kVoicePoolSize; ++i) {
        if (mVoices[i].IsLive() && mVoices[i].GetNoteNumber() == noteNumber) {
            slot = i;
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

        // 4. All slots busy (live + fading): hard-reuse the quietest fading voice
        if (slot < 0) {
            double bestEnergy = 0.0;
            for (int i = 0; i < kVoicePoolSize; ++i) {
                if (!mVoices[i].IsFading()) continue;
                const double energy = StealEnergy(mVoices[i]);
                if (slot < 0 || energy < bestEnergy) {
                    slot = i;
                    bestEnergy = energy;
                }
            }
            if (slot < 0) slot = 0;
            mVoices[slot].FastKill();
        }
    }

    const EMarimbaModel model = static_cast<EMarimbaModel>(mParams.model);
    const float keyDetuneCents = GetKeyDetuneOffset(noteNumber, mParams.detune);

    mVoices[slot].Trigger(noteNumber, mappedVel, model,
                          mParams.malletHardness, mParams.strikePosition,
                          mParams.resonatorTune, mParams.resonatorCoupling,
                          mParams.decay, mParams.buzzAmount, mParams.artifacts,
                          mParams.pitchGlide,
                          InternalRate(), mPrng,
                          mAppliedGlobalCents, keyDetuneCents,
                          mParams.malletType,
                          mParams.strikeJitter);
    return slot;
}

void MarembaEngine::NoteOff(int noteNumber) {
    // Sound-neutral by design (bars ring freely): only marks released keys,
    // which are preferred when a voice has to be stolen.
    for (auto& voice : mVoices) {
        if (voice.IsLive() && voice.GetNoteNumber() == noteNumber) {
            voice.Release();
        }
    }
}

void MarembaEngine::DampAll() {
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
    return mTailsSilent && !mSwitchPending && mPendingCount == 0;
}

void MarembaEngine::ControlTick() {
    for (auto& voice : mVoices) {
        if (voice.IsActive()) voice.UpdateEnvelope();
    }
    // Zero decayed shared resonators before they reach subnormal values
    mSympathetic.FlushTiny();
    mFrameBody.FlushTiny();
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

    const float volumeTarget = mParams.volume * mParams.volume;
    const bool meshOn = mParams.sympathetic > 0.001f;
    const bool bodyOn = mParams.bodyBloom > 0.001f;

    for (int f = 0; f < frameCount; ++f) {
        const int factor = mActiveFactor;
        bool anyVoice = false;

        for (int s = 0; s < factor; ++s) {
            float accCloseL = 0.0f;
            float accCloseR = 0.0f;
            float accFarL = 0.0f;
            float accFarR = 0.0f;
            float accPiezo = 0.0f;

            float totalMechanical = 0.0f;

            if (meshOn) {
                mSympathetic.BeginSample();
            }

            for (int v = 0; v < kVoicePoolSize; ++v) {
                if (mVoices[v].IsActive()) {
                    float voiceClose = 0.0f;
                    float voiceFar = 0.0f;
                    float voicePiezo = 0.0f;

                    if (mVoices[v].ProcessSample(voiceClose, voiceFar, voicePiezo)) {
                        anyVoice = true;
                        int noteNum = mVoices[v].GetNoteNumber();
                        mMicMixer.MixVoiceSample(noteNum,
                                                 voiceClose, voiceFar, voicePiezo,
                                                 accCloseL, accCloseR,
                                                 accFarL, accFarR,
                                                 accPiezo);

                        totalMechanical += voicePiezo;

                        if (meshOn) {
                            mSympathetic.AccumulateVoice(noteNum, voiceClose + voiceFar);
                        }
                    }
                }
            }

            // Frame Body Bloom (low-frequency frame & rail mass resonance)
            float bodySound = 0.0f;
            if (bodyOn) {
                bodySound = mFrameBody.Process(totalMechanical) * mParams.bodyBloom * kBodyBloomGain;
            }

            // Inter-Bar Sympathetic Resonance Halo ("Singing Rack")
            float haloL = 0.0f, haloR = 0.0f;
            if (meshOn) {
                mSympathetic.Process(mParams.sympathetic, haloL, haloR);
            }

            // Route acoustic emissions (frame body bloom & sympathetic halo) into acoustic mic accumulators
            // so they are properly controlled by the mixer and completely silent when mixer volumes are at 0
            accCloseL += (bodySound * 0.5f + haloL * 0.7f);
            accCloseR += (bodySound * 0.5f + haloR * 0.7f);
            accFarL += (bodySound * 0.8f + haloL * 1.0f);
            accFarR += (bodySound * 0.8f + haloR * 1.0f);

            float frameMainL = 0.0f;
            float frameMainR = 0.0f;

            mMicMixer.ProcessFrame(accCloseL, accCloseR,
                                   accFarL, accFarR,
                                   accPiezo,
                                   mParams.closeLevel, mParams.farLevel, mParams.piezoLevel,
                                   mParams.stereoWidth,
                                   frameMainL, frameMainR,
                                   mSubBufDirect[0][s], mSubBufDirect[1][s],
                                   mSubBufDirect[2][s], mSubBufDirect[3][s],
                                   mSubBufDirect[4][s]);

            // 1. Percussion Compressor (gentle glue on the main bus before saturation)
            float compL = 0.0f;
            float compR = 0.0f;
            mCompressor.Process(frameMainL, frameMainR, mParams.compAmount, compL, compR);

            // 2. Analog Preamp (operates smoothly and cleanly on controlled dynamics)
            float preL = 0.0f;
            float preR = 0.0f;
            mPreamp.Process(compL, compR, mParams.preampDrive, mParams.warmth, preL, preR);

            mSubBufMainL[s] = preL;
            mSubBufMainR[s] = preR;
        }

        // Decimate through polyphase half-band filters
        float mainL = 0.0f, mainR = 0.0f;
        mOversampler.DownsampleFrame(mSubBufMainL, mSubBufMainR, factor, mainL, mainR);

        // Master volume (smoothed per frame) and the oversampling-change fade
        mVolumeGain += mVolumeAlpha * (volumeTarget - mVolumeGain);
        const float gain = mVolumeGain * mSwitchGain;
        outMainL[f] = mainL * gain;
        outMainR[f] = mainR * gain;

        // Direct outs: same half-band chain as main, pre-fader mic taps scaled
        // by the main mix scale, a fixed -6 dB trim and the master volume
        // (unconnected outs are skipped)
        const float directGain = MicMixer::kMasterMixScale * kDirectOutTrim * gain;
        for (int k = 0; k < kNumDirectOuts; ++k) {
            if (direct[k]) {
                if (!mDirectRunning[k]) {
                    mDirectDecimators[k].Reset();
                    mDirectRunning[k] = true;
                }
                direct[k][f] = mDirectDecimators[k].DownsampleFrame(mSubBufDirect[k], factor) * directGain;
            } else {
                mDirectRunning[k] = false;
            }
        }

        // Silence detection: no voice and every tail below -180 dBFS for longer
        // than the longest room echo gap -> flush all tails and go idle.
        if (!anyVoice && !mSwitchPending) {
            float peak = std::max(std::abs(mainL), std::abs(mainR));
            for (int s = 0; s < factor; ++s) {
                peak = std::max(peak, std::max(std::abs(mSubBufMainL[s]), std::abs(mSubBufMainR[s])));
                for (int k = 0; k < kNumDirectOuts; ++k) {
                    peak = std::max(peak, std::abs(mSubBufDirect[k][s]));
                }
            }
            mQuietFrames = (peak < kSilenceLevel) ? mQuietFrames + 1 : 0;
            if (mQuietFrames >= mQuietFramesNeeded) {
                FlushTails();
                mTailsSilent = true;
                mControlCounter = 0;
                mVolumeGain = volumeTarget;
                zeroFrames(f + 1);
                break;
            }
        } else {
            mQuietFrames = 0;
        }

        // Oversampling change: after the fade, reset everything and switch
        if (mSwitchPending) {
            mSwitchGain -= mSwitchStep;
            if (mSwitchGain <= 0.0f) {
                ApplyFactorSwitch();
                if (IsSilent()) {
                    zeroFrames(f + 1);
                    break;
                }
            }
        }

        // Control rate (fixed grid of output frames, independent of batch splits)
        if (++mControlCounter >= kControlInterval) {
            mControlCounter = 0;
            ControlTick();
        }
    }

    // Output guard: never hand non-finite samples to the host
    bool finite = true;
    for (int f = 0; f < frameCount && finite; ++f) {
        finite = std::isfinite(outMainL[f]) && std::isfinite(outMainR[f]);
        for (int k = 0; k < kNumDirectOuts && finite; ++k) {
            if (direct[k]) finite = std::isfinite(direct[k][f]);
        }
    }
    if (!finite) {
        Reset();
        zeroFrames(0);
    }
}

} // namespace maremba
