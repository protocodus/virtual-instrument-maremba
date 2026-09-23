#pragma once

#include "DspMath.h"
#include "MarimbaModel.h"
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
    // Mechanical transmission reaches nearby modeled neighbors. Frequency
    // selectivity belongs to the resonators, not a musical-interval whitelist.
    static constexpr int kCapacity = 24;
    uint8_t count;
    ResonatorCouplingEntry entries[kCapacity];
};

class SympatheticMesh {
public:
    static constexpr int kFirstNote = 36; // C2
    static constexpr int kLastNote = 96;  // C7
    static constexpr int kNumResonators = kLastNote - kFirstNote + 1;

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
            mInput1[i] = mInput2[i] = 0.0;
        }
    }

    // Coefficients only (cheap, realtime safe): call when the rate or the
    // engine-wide tuning (tune + pitch bend, in cents) changes. The coupling
    // table is built once in the constructor. A ringing resonator keeps its
    // amplitude and phase across the change (its delayed state is re-expressed
    // for the new pole), so bend or tune modulation cannot pump it.
    void Configure(double sampleRate, double tuneOffsetCents = 0.0,
                   EMarimbaModel model = EMarimbaModel::ImperialRosewood) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
        const int index = std::clamp(static_cast<int>(model), 0, 3);
        static constexpr double transmissionCutoff[4] = {1800.0, 1400.0, 1200.0, 3200.0};
        static constexpr double supportQ[4] = {24.0, 14.0, 18.0, 38.0};
        mLpAlpha = 1.0 - std::exp(-kTwoPiD * transmissionCutoff[index] / mSampleRate);

        // Reduced chromatic bank covering the core five-octave playing register.
        for (int i = 0; i < kNumResonators; ++i) {
            double note = kFirstNote + static_cast<double>(i);
            double f = 440.0 * std::exp2((note - 69.0 + tuneOffsetCents / 100.0) / 12.0);

            // Steel bridge/tines ring longer than a lossy wooden soundbox.
            // These effective support losses are voiced, not species data.
            double q = supportQ[index] * (1.0 + 0.4 * i / (kNumResonators - 1.0));
            double r = std::exp(-kPiD * f / (q * mSampleRate));
            double w = std::min(kTwoPiD * f / mSampleRate, 0.98 * kPiD);
            // With y1 = A cos(phi) and rOld*y2 = A cos(phi - wOld):
            // y2' = (y1 cos(w) + A sin(phi) sin(w)) / r. Skipped on the first call.
            const double sinOld = std::sin(mW[i]);
            if (mS[i] > 0.0 && sinOld > 1.0e-12) {
                const double rOld = std::sqrt(mS[i]);
                const double aSinPhi = (rOld * mY2[i] - mY1[i] * std::cos(mW[i])) / sinOld;
                mY2[i] = (mY1[i] * std::cos(w) + aSinPhi * std::sin(w)) / r;
            }
            mW[i] = w;
            mC[i] = 2.0 * r * std::cos(w);
            mS[i] = r * r;

            // Coupling strength diminishes slightly toward outer edges
            double distFromCenter = std::abs(2.0 * i / (kNumResonators - 1.0) - 1.0);
            double couplingFalloff = 1.0 - 0.25 * distFromCenter;

            // Input gain (1 - r) * 0.05 as voiced at the reference rate, kept at the
            // same centre gain at every rate (the plain (1 - r) form grows with fs).
            double rRef = std::exp(-kPiD * f / (q * kReferenceRate));
            double wRef = std::min(kTwoPiD * f / kReferenceRate, 0.98 * kPiD);
            // Force-to-velocity bandpass: suppress off-resonant low-frequency
            // accumulation as well as DC, while retaining centre sensitivity.
            mB[i] = RateInvariantResonatorGain((1.0 - rRef) * 0.05 * couplingFalloff, rRef, wRef, r, w)
                / (2.0 * std::sin(w));
        }
    }

    // Begin a sub-sample: clear accumulated voice drives
    inline void BeginSample() {
        for (int i = 0; i < kNumResonators; ++i) {
            mDrive[i] = 0.0f;
        }
    }

    // Accumulate transmitted mechanical motion; the bank selects the spectral
    // overlap of neighboring bars/tines, including adjacent semitones.
    inline void AccumulateVoice(int noteNumber, float voiceMechanical) {
        if (noteNumber < 0 || noteNumber >= 128) return;
        const auto& runs = mNoteCouplingRuns[noteNumber];
        for (int k = 0; k < runs.firstCount; ++k) {
            mDrive[runs.firstStart + k] += runs.weights[k] * voiceMechanical;
        }
        for (int k = 0; k < runs.secondCount; ++k) {
            mDrive[runs.secondStart + k] += runs.weights[runs.firstCount + k] * voiceMechanical;
        }
    }

    // Process the receiver bank and produce a spatialized stereo halo
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

            const double drive = mDriveLpState[i] - mInput2[i];
            mInput2[i] = mInput1[i];
            mInput1[i] = mDriveLpState[i];
            double y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * drive;
            mY2[i] = mY1[i];
            mY1[i] = y;

            // Spatial distribution across stereo field: C2 left to C7 right
            sumL += y * mPanL[i];
            sumR += y * mPanR[i];
        }

        // Neighbor-only readout needs a modest level restoration after the
        // parallel driven-key resonance is removed. This is an acoustic gain,
        // not feedback energy; low-frequency rejection remains in each receiver.
        outHaloL = static_cast<float>(sumL * sympatheticAmount * 0.20);
        outHaloR = static_cast<float>(sumR * sympatheticAmount * 0.20);
    }

    // Control rate: zero states that have decayed to nothing (below 1e-15,
    // about -300 dB), so they never cycle on subnormal values. The input
    // low-pass decays much faster than its resonator, so it is flushed alone.
    // A resonator far above any real drive (|y| > kMaxState) is unstable and
    // is cleared as well (defence in depth).
    void FlushTiny() {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::abs(mDriveLpState[i]) < 1.0e-15) {
                mDriveLpState[i] = 0.0;
            }
            if (std::abs(mInput1[i]) < 1.0e-15) mInput1[i] = 0.0;
            if (std::abs(mInput2[i]) < 1.0e-15) mInput2[i] = 0.0;
            const double mag = std::abs(mY1[i]) + std::abs(mY2[i]);
            if (mag < 1.0e-15 || !(mag <= kMaxState)) {
                mY1[i] = 0.0;
                mY2[i] = 0.0;
            }
        }
    }

    // True when no resonator state is subnormal (unit testing)
    bool HasNoSubnormalState() const {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::fpclassify(mY1[i]) == FP_SUBNORMAL || std::fpclassify(mY2[i]) == FP_SUBNORMAL
                || std::fpclassify(mDriveLpState[i]) == FP_SUBNORMAL
                || std::fpclassify(mInput1[i]) == FP_SUBNORMAL || std::fpclassify(mInput2[i]) == FP_SUBNORMAL) return false;
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
    static constexpr double kMaxState = 1.0e3;

    // The sorted coupling list contains at most two contiguous runs: keys
    // below and above the driven note. Each independent receiver still gets
    // one identical multiply-add, in the same voice and receiver order.
    struct CouplingRuns {
        uint8_t firstStart = 0, firstCount = 0;
        uint8_t secondStart = 0, secondCount = 0;
        float weights[VoiceCouplingList::kCapacity] = {};
    };

    // A local support transmits most strongly to nearby keys. The spectral
    // filters decide what rings: no fifth/third whitelist and no arbitrary
    // prohibition on adjacent keys. The finite bank is a reduced approximation
    // to mounting transmission, not a reciprocal simulation of all 128 bars.
    void BuildCouplingTable() {
        for (int n = 0; n < 128; ++n) {
            auto& list = mNoteCoupling[n];
            list.count = 0;
            // The driven key already exists in the voice bank. Duplicating it
            // here masks neighboring resonances with a parallel fundamental.
            // A one-octave support neighborhood bounds work to 24 paths per
            // voice while the receiver bank spans the full core register.
            for (int key = std::max(kFirstNote, n - 12);
                 key <= std::min(kLastNote, n + 12); ++key) {
                if (key == n) continue;
                const double distance = std::abs(n - key);
                const float weight = static_cast<float>(1.0 / (1.0 + 0.12 * distance * distance));
                list.entries[list.count++] = {static_cast<uint8_t>(key - kFirstNote), weight};
            }
            auto& runs = mNoteCouplingRuns[n];
            runs = CouplingRuns{};
            if (list.count == 0) continue;
            runs.firstStart = list.entries[0].resonatorIdx;
            for (int k = 0; k < list.count; ++k) {
                const auto& entry = list.entries[k];
                runs.weights[k] = entry.weight;
                if (entry.resonatorIdx == runs.firstStart + k) {
                    ++runs.firstCount;
                } else {
                    if (runs.secondCount == 0) runs.secondStart = entry.resonatorIdx;
                    ++runs.secondCount;
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
    std::array<double, kNumResonators> mW{};   // pole angle currently in mC
    std::array<double, kNumResonators> mB{};
    std::array<float, kNumResonators> mDrive{};
    std::array<double, kNumResonators> mDriveLpState{};
    std::array<double, kNumResonators> mInput1{}, mInput2{};
    std::array<double, kNumResonators> mPanL{};
    std::array<double, kNumResonators> mPanR{};
    std::array<VoiceCouplingList, 128> mNoteCoupling{};
    std::array<CouplingRuns, 128> mNoteCouplingRuns{};
};

} // namespace maremba
