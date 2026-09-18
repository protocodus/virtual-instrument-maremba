#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace maremba {

class SympatheticMesh {
public:
    static constexpr int kNumResonators = 24; // C3 to B5 (2 octaves)

    SympatheticMesh() {
        Reset();
    }

    void Reset() {
        for (int i = 0; i < kNumResonators; ++i) {
            mY1[i] = 0.0f;
            mY2[i] = 0.0f;
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
            
            // Q varies by register: lower bars resonate more broadly
            float q = 18.0f + 8.0f * (static_cast<float>(i) / static_cast<float>(kNumResonators - 1));
            float r = std::clamp(std::exp(-3.1415926535f * f / (q * mSampleRate)), 0.0f, 0.9999f);
            float w = std::clamp(6.283185307f * f / mSampleRate, 0.001f, 3.14f);
            mC[i] = 2.0f * r * std::cos(w);
            mS[i] = r * r;
            // Coupling strength diminishes for distant resonators
            float octavePos = static_cast<float>(i) / 12.0f; // 0.0 to ~2.0
            float distFromCenter = std::abs(octavePos - 1.0f); // 0 at middle, 1 at edges
            float couplingFalloff = 1.0f - 0.35f * distFromCenter;
            mB[i] = (1.0f - r) * 0.035f * couplingFalloff;
        }
    }

    // Process a sample of acoustic bar energy through sympathetic mesh
    inline void Process(float inEnergy, float sympatheticAmount, float& outHaloL, float& outHaloR) {
        if (sympatheticAmount <= 0.001f) {
            outHaloL = 0.0f;
            outHaloR = 0.0f;
            return;
        }

        // Lowpass filter excitation so sharp attack clicks don't shock-excite metallic ringing
        mInputLpState += mLpAlpha * (inEnergy - mInputLpState);
        float filteredEnergy = mInputLpState;

        float sumL = 0.0f;
        float sumR = 0.0f;

        // Spread the 24 resonators across the stereo field
        for (int i = 0; i < kNumResonators; ++i) {
            float y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * filteredEnergy;
            mY2[i] = mY1[i];
            mY1[i] = y;

            // Natural spatial distribution: C3 is far left (0.12), B5 is far right (0.88)
            float pan = 0.12f + 0.76f * (static_cast<float>(i) / static_cast<float>(kNumResonators - 1));
            sumL += y * (1.0f - pan);
            sumR += y * pan;
        }

        outHaloL = sumL * sympatheticAmount * 0.10f;
        outHaloR = sumR * sympatheticAmount * 0.10f;
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
};

} // namespace maremba
