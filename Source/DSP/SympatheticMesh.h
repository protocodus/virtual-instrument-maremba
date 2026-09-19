#pragma once

#include "DspMath.h"
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
    // With 24 resonators a note has at most 9 consonant partners (unison,
    // octaves, 5ths, 4ths, major 3rds), so 12 never truncates the list.
    static constexpr int kCapacity = 12;
    uint8_t count;
    ResonatorCouplingEntry entries[kCapacity];
};

class SympatheticMesh {
public:
    static constexpr int kNumResonators = 24; // C3 (MIDI 48) to B5 (MIDI 71)

    SympatheticMesh() {
        for (int i = 0; i < kNumResonators; ++i) {
            double pan = 0.12 + 0.76 * (static_cast<double>(i) / static_cast<double>(kNumResonators - 1));
            mPanL[i] = 1.0 - pan;
            mPanR[i] = pan;
        }
        BuildCouplingTable();
        Reset();
    }

    void Reset() {
        for (int i = 0; i < kNumResonators; ++i) {
            mY1[i] = 0.0;
            mY2[i] = 0.0;
            mDrive[i] = 0.0f;
            mDriveLpState[i] = 0.0;
        }
    }

    // Coefficients only (cheap, realtime safe): call when the rate or the
    // engine-wide tuning (tune + pitch bend, in cents) changes. The coupling
    // table is built once in the constructor.
    void Configure(double sampleRate, double tuneOffsetCents = 0.0) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
        mLpAlpha = 1.0 - std::exp(-kTwoPiD * 600.0 / mSampleRate);

        // 24 chromatic sympathetic resonators spanning C3 (MIDI 48) to B5 (MIDI 71)
        for (int i = 0; i < kNumResonators; ++i) {
            double note = 48.0 + static_cast<double>(i); // C3 to B5
            double f = 440.0 * std::exp2((note - 69.0 + tuneOffsetCents / 100.0) / 12.0);

            // Realistic wooden bar & air column sympathetic Q (20 to 28)
            double q = 20.0 + 8.0 * (static_cast<double>(i) / static_cast<double>(kNumResonators - 1));
            double r = std::exp(-kPiD * f / (q * mSampleRate));
            double w = std::min(kTwoPiD * f / mSampleRate, 0.98 * kPiD);
            mC[i] = 2.0 * r * std::cos(w);
            mS[i] = r * r;

            // Coupling strength diminishes slightly toward outer edges
            double octavePos = static_cast<double>(i) / 12.0;
            double distFromCenter = std::abs(octavePos - 1.0);
            double couplingFalloff = 1.0 - 0.25 * distFromCenter;

            // Input gain (1 - r) * 0.05 as voiced at the reference rate, kept at the
            // same centre gain at every rate (the plain (1 - r) form grows with fs).
            double rRef = std::exp(-kPiD * f / (q * kReferenceRate));
            double wRef = std::min(kTwoPiD * f / kReferenceRate, 0.98 * kPiD);
            mB[i] = RateInvariantResonatorGain((1.0 - rRef) * 0.05 * couplingFalloff, rRef, wRef, r, w);
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

        double sumL = 0.0;
        double sumR = 0.0;

        for (int i = 0; i < kNumResonators; ++i) {
            // Lowpass input to smooth out abrupt transients
            mDriveLpState[i] += mLpAlpha * (mDrive[i] - mDriveLpState[i]);

            double y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * mDriveLpState[i];
            mY2[i] = mY1[i];
            mY1[i] = y;

            // Spatial distribution across stereo field: C3 left (0.12) to B5 right (0.88)
            sumL += y * mPanL[i];
            sumR += y * mPanR[i];
        }

        outHaloL = static_cast<float>(sumL * sympatheticAmount * 0.035);
        outHaloR = static_cast<float>(sumR * sympatheticAmount * 0.035);
    }

    // Control rate: zero states that have decayed to nothing (below 1e-15,
    // about -300 dB), so they never cycle on subnormal values. The input
    // low-pass decays much faster than its resonator, so it is flushed alone.
    void FlushTiny() {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::abs(mDriveLpState[i]) < 1.0e-15) {
                mDriveLpState[i] = 0.0;
            }
            if (std::abs(mY1[i]) + std::abs(mY2[i]) < 1.0e-15) {
                mY1[i] = 0.0;
                mY2[i] = 0.0;
            }
        }
    }

    // True when no resonator state is subnormal (unit testing)
    bool HasNoSubnormalState() const {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::fpclassify(mY1[i]) == FP_SUBNORMAL || std::fpclassify(mY2[i]) == FP_SUBNORMAL
                || std::fpclassify(mDriveLpState[i]) == FP_SUBNORMAL) return false;
        }
        return true;
    }

    // Direct access to resonator state for unit testing
    float GetResonatorOutput(int index) const {
        if (index >= 0 && index < kNumResonators) {
            return static_cast<float>(mY1[index]);
        }
        return 0.0f;
    }

    const VoiceCouplingList& GetCouplingList(int noteNumber) const {
        return mNoteCoupling[std::clamp(noteNumber, 0, 127)];
    }

private:
    // Build Harmonic Coupling Matrix: each MIDI note (0-127) couples ONLY
    // to consonant harmonic intervals (unison, octave, 5th, 4th, major 3rd).
    // Semitones, whole-tones, and tritones have STRICTLY 0.0 coupling.
    void BuildCouplingTable() {
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

                if (w > 0.001f && list.count < VoiceCouplingList::kCapacity) {
                    list.entries[list.count++] = { static_cast<uint8_t>(i), w };
                }
            }
        }
    }

    double mSampleRate = 44100.0;
    double mLpAlpha = 0.08;
    std::array<double, kNumResonators> mY1{};
    std::array<double, kNumResonators> mY2{};
    std::array<double, kNumResonators> mC{};
    std::array<double, kNumResonators> mS{};
    std::array<double, kNumResonators> mB{};
    std::array<float, kNumResonators> mDrive{};
    std::array<double, kNumResonators> mDriveLpState{};
    std::array<double, kNumResonators> mPanL{};
    std::array<double, kNumResonators> mPanR{};
    std::array<VoiceCouplingList, 128> mNoteCoupling{};
};

} // namespace maremba
