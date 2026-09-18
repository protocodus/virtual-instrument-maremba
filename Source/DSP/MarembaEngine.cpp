#include "MarembaEngine.h"
#include <algorithm>
#include <cmath>

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
} // namespace

MarembaEngine::MarembaEngine(double sampleRate)
    : mBaseSampleRate(sampleRate > 8000.0 ? sampleRate : 44100.0)
    , mPrng(0x87654321)
{
    Reset();
}

void MarembaEngine::SetSampleRate(double sampleRate) {
    mBaseSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
    Reset();
}

void MarembaEngine::Reset() {
    for (auto& voice : mVoices) {
        voice.FastKill();
    }
    for (auto& roll : mRollNotes) {
        roll.noteNumber = -1;
        roll.velocity = 0.0f;
        roll.timerSamples = 0.0f;
        roll.leftHand = true;
    }
    mMicMixer.Reset();
    mPreamp.Reset();
    mCompressor.Reset();
    mOversampler.Reset();
    mSympathetic.Reset();
    mFrameBody.Reset();

    int factor = (mParams.oversampling == 1) ? 4 : (mParams.oversampling == 2) ? 8 : 2;
    float internalRate = static_cast<float>(mBaseSampleRate * factor);
    mCompressor.SetSampleRate(internalRate);
    mPreamp.SetSampleRate(internalRate);

    ModelParams mp = GetModelParams(static_cast<EMarimbaModel>(std::clamp(mParams.model, 0, 3)));
    mFrameBody.Configure(internalRate, mp.frameBodyFreq, mp.frameBodyQ);
    mSympathetic.Configure(internalRate, (mParams.masterTune - 0.50f) * 200.0f);
}

void MarembaEngine::SetParameters(const EngineParameters& params) {
    // Detect sustain pedal release: was down, now up
    bool pedalLifted = mParams.sustainPedalDown && !params.sustainPedalDown;

    mParams = params;
    int factor = (mParams.oversampling == 1) ? 4 : (mParams.oversampling == 2) ? 8 : 2;
    float internalRate = static_cast<float>(mBaseSampleRate * factor);

    mCompressor.SetSampleRate(internalRate);
    mPreamp.SetSampleRate(internalRate);
    ModelParams mp = GetModelParams(static_cast<EMarimbaModel>(std::clamp(mParams.model, 0, 3)));
    mFrameBody.Configure(internalRate, mp.frameBodyFreq, mp.frameBodyQ);
    mSympathetic.Configure(internalRate, (mParams.masterTune - 0.50f) * 200.0f);

    // When sustain pedal lifts, begin hand-damping all sustained voices
    if (pedalLifted) {
        int maxVoices = (mParams.polyphony == 0) ? 8 : (mParams.polyphony == 1) ? 16 : 24;
        for (int i = 0; i < maxVoices; ++i) {
            if (mVoices[i].IsActive() && mVoices[i].IsSustained()) {
                mVoices[i].ReleaseSustainPedal();
            }
        }
    }
}

float MarembaEngine::MapVelocity(float vel) const {
    vel = std::clamp(vel, 0.001f, 1.0f);
    switch (mParams.velocityCurve) {
        case 0: // Soft
            return std::pow(vel, 1.55f);
        case 2: // Hard
            return std::sqrt(vel);
        case 3: // Expressive
            return vel * vel * (3.0f - 2.0f * vel);
        case 1: // Linear
        default:
            return vel;
    }
}

int MarembaEngine::FindVoiceToAllocate(int noteNumber) {
    int maxVoices = (mParams.polyphony == 0) ? 8 : (mParams.polyphony == 1) ? 16 : 24;

    // 1. If the exact same note is currently ringing, re-strike it!
    for (int i = 0; i < maxVoices; ++i) {
        if (mVoices[i].IsActive() && mVoices[i].GetNoteNumber() == noteNumber) {
            return i;
        }
    }

    // 2. Find an inactive voice
    for (int i = 0; i < maxVoices; ++i) {
        if (!mVoices[i].IsActive()) {
            return i;
        }
    }

    // 3. Voice stealing: steal voice with lowest amplitude
    int bestCandidate = 0;
    float lowestAmp = 1e9f;
    for (int i = 0; i < maxVoices; ++i) {
        float amp = mVoices[i].GetCurrentAmplitude();
        if (amp < lowestAmp) {
            lowestAmp = amp;
            bestCandidate = i;
        }
    }

    return bestCandidate;
}

void MarembaEngine::NoteOn(int noteNumber, float velocity) {
    if (velocity <= 0.0f) {
        NoteOff(noteNumber);
        return;
    }

    float mappedVel = MapVelocity(velocity);

    // Track for Mallet Roll if roll speed active
    if (mParams.rollSpeed >= 4.0f) {
        for (auto& roll : mRollNotes) {
            if (roll.noteNumber == noteNumber || roll.noteNumber == -1) {
                roll.noteNumber = noteNumber;
                roll.velocity = mappedVel;
                roll.timerSamples = 0.0f;
                roll.leftHand = true;
                break;
            }
        }
    }

    int voiceIdx = FindVoiceToAllocate(noteNumber);
    int factor = (mParams.oversampling == 1) ? 4 : (mParams.oversampling == 2) ? 8 : 2;
    float internalSampleRate = static_cast<float>(mBaseSampleRate * factor);

    EMarimbaModel model = static_cast<EMarimbaModel>(std::clamp(mParams.model, 0, 3));
    float masterTuneCents = (mParams.masterTune - 0.50f) * 200.0f;
    float keyDetuneCents = GetKeyDetuneOffset(noteNumber, mParams.detune);
    float totalPitchOffset = masterTuneCents + keyDetuneCents;

    mVoices[voiceIdx].Trigger(noteNumber, mappedVel, model,
                             mParams.malletHardness, mParams.strikePosition,
                             mParams.resonatorTune, mParams.resonatorCoupling,
                             mParams.decay, mParams.buzzAmount, mParams.artifacts,
                             mParams.pitchGlide,
                             internalSampleRate, mPrng,
                             totalPitchOffset,
                             mParams.malletType,
                             mParams.strikeJitter);
}

void MarembaEngine::NoteOff(int noteNumber) {
    int maxVoices = (mParams.polyphony == 0) ? 8 : (mParams.polyphony == 1) ? 16 : 24;
    for (int i = 0; i < maxVoices; ++i) {
        if (mVoices[i].IsActive() && mVoices[i].GetNoteNumber() == noteNumber) {
            mVoices[i].Release(mParams.sustainPedalDown);
        }
    }
    for (auto& roll : mRollNotes) {
        if (roll.noteNumber == noteNumber) {
            roll.noteNumber = -1;
        }
    }
}

void MarembaEngine::AllNotesOff() {
    for (auto& voice : mVoices) {
        if (voice.IsActive()) {
            voice.Release();
        }
    }
    for (auto& roll : mRollNotes) {
        roll.noteNumber = -1;
    }
}

int MarembaEngine::GetActiveVoiceCount() const {
    int count = 0;
    int maxVoices = (mParams.polyphony == 0) ? 8 : (mParams.polyphony == 1) ? 16 : 24;
    for (int i = 0; i < maxVoices; ++i) {
        if (mVoices[i].IsActive()) {
            count++;
        }
    }
    return count;
}

void MarembaEngine::ProcessMalletRolls(int frames) {
    if (mParams.rollSpeed < 4.0f) return;

    int factor = (mParams.oversampling == 1) ? 4 : (mParams.oversampling == 2) ? 8 : 2;
    float internalSampleRate = static_cast<float>(mBaseSampleRate * factor);
    float rollIntervalSamples = static_cast<float>(mBaseSampleRate) / mParams.rollSpeed;

    for (auto& roll : mRollNotes) {
        if (roll.noteNumber < 0) continue;

        roll.timerSamples += static_cast<float>(frames);
        if (roll.timerSamples >= rollIntervalSamples) {
            roll.timerSamples -= rollIntervalSamples;

            // Alternate hand: Left vs Right hand micro-variations
            roll.leftHand = !roll.leftHand;
            float handVel = roll.velocity * (roll.leftHand ? 0.94f : 1.04f); // Dominant right hand firmness
            float handPos = mParams.strikePosition + (roll.leftHand ? -0.04f : 0.04f); // Spatial strike wobble

            int voiceIdx = FindVoiceToAllocate(roll.noteNumber);
            EMarimbaModel model = static_cast<EMarimbaModel>(std::clamp(mParams.model, 0, 3));
            float masterTuneCents = (mParams.masterTune - 0.50f) * 200.0f;
            float keyDetuneCents = GetKeyDetuneOffset(roll.noteNumber, mParams.detune);
            float totalPitchOffset = masterTuneCents + keyDetuneCents;

            mVoices[voiceIdx].Trigger(roll.noteNumber, handVel, model,
                                     mParams.malletHardness, handPos,
                                     mParams.resonatorTune, mParams.resonatorCoupling,
                                     mParams.decay, mParams.buzzAmount, mParams.artifacts,
                                     mParams.pitchGlide * 0.5f, // rolls have smoother re-strikes
                                     internalSampleRate, mPrng,
                                     totalPitchOffset,
                                     mParams.malletType,
                                     mParams.strikeJitter);
        }
    }
}

void MarembaEngine::RenderBatch(float* outMainL, float* outMainR,
                                float* outCloseL, float* outCloseR,
                                float* outFarL, float* outFarR,
                                float* outPiezo,
                                int frameCount)
{
    int factor = (mParams.oversampling == 1) ? 4 : (mParams.oversampling == 2) ? 8 : 2;
    int maxVoices = (mParams.polyphony == 0) ? 8 : (mParams.polyphony == 1) ? 16 : 24;

    ProcessMalletRolls(frameCount);

    float volGain = mParams.volume * mParams.volume;

    for (int f = 0; f < frameCount; ++f) {
        for (int s = 0; s < factor; ++s) {
            float accCloseL = 0.0f;
            float accCloseR = 0.0f;
            float accFarL = 0.0f;
            float accFarR = 0.0f;
            float accPiezo = 0.0f;

            float totalMechanical = 0.0f;
            float totalAcoustic = 0.0f;

            for (int v = 0; v < maxVoices; ++v) {
                if (mVoices[v].IsActive()) {
                    float voiceClose = 0.0f;
                    float voiceFar = 0.0f;
                    float voicePiezo = 0.0f;

                    if (mVoices[v].ProcessSample(voiceClose, voiceFar, voicePiezo)) {
                        mMicMixer.MixVoiceSample(mVoices[v].GetNoteNumber(),
                                                 voiceClose, voiceFar, voicePiezo,
                                                 accCloseL, accCloseR,
                                                 accFarL, accFarR,
                                                 accPiezo);

                        totalMechanical += voicePiezo;
                        totalAcoustic += voiceClose + voiceFar;
                    }
                }
            }

            // Frame Body Bloom (low-frequency frame & rail mass resonance)
            float bodySound = 0.0f;
            if (mParams.bodyBloom > 0.001f) {
                bodySound = mFrameBody.Process(totalMechanical) * mParams.bodyBloom * 0.02f;
            }

            // Inter-Bar Sympathetic Resonance Halo ("Singing Rack")
            float haloL = 0.0f, haloR = 0.0f;
            if (mParams.sympathetic > 0.001f) {
                mSympathetic.Process(totalAcoustic, mParams.sympathetic, haloL, haloR);
            }

            float frameMainL = 0.0f;
            float frameMainR = 0.0f;
            float frameCloseL = 0.0f;
            float frameCloseR = 0.0f;
            float frameFarL = 0.0f;
            float frameFarR = 0.0f;
            float framePiezo = 0.0f;

            mMicMixer.ProcessFrame(accCloseL, accCloseR,
                                   accFarL, accFarR,
                                   accPiezo,
                                   mParams.closeLevel, mParams.farLevel, mParams.piezoLevel,
                                   mParams.stereoWidth,
                                   frameMainL, frameMainR,
                                   frameCloseL, frameCloseR,
                                   frameFarL, frameFarR,
                                   framePiezo);

            // Inject Frame Body and Sympathetic Halo into Main Stereo field
            frameMainL += bodySound + haloL;
            frameMainR += bodySound + haloR;

            // 1. Percussion Compressor (tames dynamic peaks before saturation stage)
            float compL = 0.0f;
            float compR = 0.0f;
            mCompressor.Process(frameMainL, frameMainR,
                                mParams.compAmount, mParams.compAttack, mParams.compRelease,
                                compL, compR);

            // 2. Analog Preamp (operates smoothly and cleanly on controlled dynamics)
            float preL = 0.0f;
            float preR = 0.0f;
            mPreamp.Process(compL, compR, mParams.preampDrive, mParams.warmth, preL, preR);

            mSubBufMainL[s] = preL;
            mSubBufMainR[s] = preR;
            mSubBufCloseL[s] = frameCloseL;
            mSubBufCloseR[s] = frameCloseR;
            mSubBufFarL[s] = frameFarL;
            mSubBufFarR[s] = frameFarR;
            mSubBufPiezo[s] = framePiezo;
        }

        // Decimate through polyphase half-band filters
        float mainL = 0.0f, mainR = 0.0f;
        mOversampler.DownsampleFrame(mSubBufMainL, mSubBufMainR, factor, mainL, mainR);

        outMainL[f] = mainL * volGain;
        outMainR[f] = mainR * volGain;

        if (outCloseL && outCloseR) {
            float clL = 0.0f, clR = 0.0f;
            for (int s = 0; s < factor; ++s) { clL += mSubBufCloseL[s]; clR += mSubBufCloseR[s]; }
            outCloseL[f] = clL / static_cast<float>(factor);
            outCloseR[f] = clR / static_cast<float>(factor);
        }

        if (outFarL && outFarR) {
            float fL = 0.0f, fR = 0.0f;
            for (int s = 0; s < factor; ++s) { fL += mSubBufFarL[s]; fR += mSubBufFarR[s]; }
            outFarL[f] = fL / static_cast<float>(factor);
            outFarR[f] = fR / static_cast<float>(factor);
        }

        if (outPiezo) {
            float pz = 0.0f;
            for (int s = 0; s < factor; ++s) { pz += mSubBufPiezo[s]; }
            outPiezo[f] = pz / static_cast<float>(factor);
        }
    }
}

} // namespace maremba
