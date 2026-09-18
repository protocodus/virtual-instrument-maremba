#pragma once

#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>

namespace maremba {

struct ResonatorCouplingEntry {
    uint8_t resonatorIdx;
    float weight;
};

struct VoiceCouplingList {
    uint8_t count;
    ResonatorCouplingEntry entries[8];
};

class SympatheticMesh {
public:
    static constexpr int kNumResonators = 24; // C3 (MIDI 48) to B5 (MIDI 71)

    SympatheticMesh() {
        Reset();
    }

    void Reset() {
        for (int i = 0; i < kNumResonators; ++i) {
            mY1[i] = 0.0f;
            mY2[i] = 0.0f;
            mDrive[i] = 0.0f;
            mDriveLpState[i] = 0.0f;
        }
        mInputLpState = 0.0f;
    }

    void Configure(float sampleRate, float baseTuneOffsetCents = 0.0f) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 44100.0f;
        mLpAlpha = 1.0f - std::exp(-6.283185307f * 600.0f / mSampleRate);

        // 24 chromatic sympathetic resonators spanning C3 (MIDI 48) to B5 (MIDI 71)
        for (int i = 0; i < kNumResonators; ++i) {
            float note = 48.0f + static_cast<float>(i); // C3 to B5
            float f = 440.0f * std::pow(2.0f, (note - 69.0f + baseTuneOffsetCents / 100.0f) / 12.0f);

            // Realistic wooden bar & air column sympathetic Q (20 to 28)
            float q = 20.0f + 8.0f * (static_cast<float>(i) / static_cast<float>(kNumResonators - 1));
            float r = std::clamp(std::exp(-3.1415926535f * f / (q * mSampleRate)), 0.0f, 0.9999f);
            float w = std::clamp(6.283185307f * f / mSampleRate, 0.001f, 3.14f);
            mC[i] = 2.0f * r * std::cos(w);
            mS[i] = r * r;

            // Coupling strength diminishes slightly toward outer edges
            float octavePos = static_cast<float>(i) / 12.0f;
            float distFromCenter = std::abs(octavePos - 1.0f);
            float couplingFalloff = 1.0f - 0.25f * distFromCenter;
            mB[i] = (1.0f - r) * 0.05f * couplingFalloff;
        }

        // Build Harmonic Coupling Matrix: each MIDI note (0-127) couples ONLY
        // to consonant harmonic intervals (unison, octave, 5th, 4th, major 3rd).
        // Semitones, whole-tones, and tritones have STRICTLY 0.0 coupling.
        for (int n = 0; n < 128; ++n) {
            auto& list = mNoteCoupling[n];
            list.count = 0;

            for (int i = 0; i < kNumResonators; ++i) {
                int resNote = 48 + i;
                int delta = std::abs(n - resNote);
                int intervalClass = delta % 12;
                int octDist = delta / 12;
                float octFalloff = 1.0f / (1.0f + 0.5f * static_cast<float>(octDist));

                float w = 0.0f;
                if (delta == 0) {
                    w = 1.0f; // Unison
                } else if (intervalClass == 0) {
                    w = 0.70f * octFalloff; // Octaves
                } else if (intervalClass == 7) {
                    w = 0.45f * octFalloff; // Perfect Fifth
                } else if (intervalClass == 5) {
                    w = 0.25f * octFalloff; // Perfect Fourth
                } else if (intervalClass == 4) {
                    w = 0.15f * octFalloff; // Major Third
                }

                if (w > 0.001f && list.count < 8) {
                    list.entries[list.count++] = { static_cast<uint8_t>(i), w };
                }
            }
        }
    }

    // Begin a sub-sample: clear accumulated voice drives
    inline void BeginSample() {
        for (int i = 0; i < kNumResonators; ++i) {
            mDrive[i] = 0.0f;
        }
    }

    // Accumulate acoustic energy from an active voice into harmonically coupled resonators
    inline void AccumulateVoice(int noteNumber, float voiceAcoustic) {
        if (noteNumber < 0 || noteNumber >= 128) return;
        const auto& list = mNoteCoupling[noteNumber];
        for (uint8_t k = 0; k < list.count; ++k) {
            mDrive[list.entries[k].resonatorIdx] += list.entries[k].weight * voiceAcoustic;
        }
    }

    // Process all 24 resonators and produce spatialized stereo halo
    inline void Process(float sympatheticAmount, float& outHaloL, float& outHaloR) {
        if (sympatheticAmount <= 0.001f) {
            outHaloL = 0.0f;
            outHaloR = 0.0f;
            return;
        }

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int i = 0; i < kNumResonators; ++i) {
            // Lowpass input to smooth out abrupt transients
            mDriveLpState[i] += mLpAlpha * (mDrive[i] - mDriveLpState[i]);

            float y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * mDriveLpState[i];
            mY2[i] = mY1[i];
            mY1[i] = y;

            // Spatial distribution across stereo field: C3 left (0.12) to B5 right (0.88)
            float pan = 0.12f + 0.76f * (static_cast<float>(i) / static_cast<float>(kNumResonators - 1));
            sumL += y * (1.0f - pan);
            sumR += y * pan;
        }

        outHaloL = sumL * sympatheticAmount * 0.035f;
        outHaloR = sumR * sympatheticAmount * 0.035f;
    }

    // Legacy/fallback mono injection overload
    inline void Process(float inEnergy, float sympatheticAmount, float& outHaloL, float& outHaloR) {
        if (sympatheticAmount <= 0.001f) {
            outHaloL = 0.0f;
            outHaloR = 0.0f;
            return;
        }

        mInputLpState += mLpAlpha * (inEnergy - mInputLpState);
        float filtered = mInputLpState;

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int i = 0; i < kNumResonators; ++i) {
            float y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * filtered;
            mY2[i] = mY1[i];
            mY1[i] = y;

            float pan = 0.12f + 0.76f * (static_cast<float>(i) / static_cast<float>(kNumResonators - 1));
            sumL += y * (1.0f - pan);
            sumR += y * pan;
        }

        outHaloL = sumL * sympatheticAmount * 0.035f;
        outHaloR = sumR * sympatheticAmount * 0.035f;
    }

    // Direct access to resonator state for unit testing
    float GetResonatorOutput(int index) const {
        if (index >= 0 && index < kNumResonators) {
            return mY1[index];
        }
        return 0.0f;
    }

private:
    float mSampleRate = 44100.0f;
    float mInputLpState = 0.0f;
    float mLpAlpha = 0.08f;
    std::array<float, kNumResonators> mY1{};
    std::array<float, kNumResonators> mY2{};
    std::array<float, kNumResonators> mC{};
    std::array<float, kNumResonators> mS{};
    std::array<float, kNumResonators> mB{};
    std::array<float, kNumResonators> mDrive{};
    std::array<float, kNumResonators> mDriveLpState{};
    std::array<VoiceCouplingList, 128> mNoteCoupling{};
};

} // namespace maremba
