#pragma once

#include "DspMath.h"
#include "MarimbaModel.h"
#include "Tunings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace maremba {

// Unstruck keys that ring along. Every receiver is a real key of the
// instrument with the key's own loss (Q = 1/eta), so only energy that lands
// within a few tenths of a hertz of its pitch builds up: coincident partials,
// the kalimba's 2 f1 bridge line, and the brief broadband attack. Wooden bars
// share cords and frame, so they are driven by the summed support force; a
// kalimba's tines share one bridge, so each tine hears the others' bridge
// reaction weighted by how close they sit on it (physical neighbours are a
// third apart on the 17-key layout, and they ring most; Holdaway).
class SympatheticMesh {
public:
    static constexpr int kFirstNote = 36; // C2
    static constexpr int kLastNote = 96;  // C7
    static constexpr int kNumResonators = kLastNote - kFirstNote + 1;
    static constexpr int kTines = 17;

    SympatheticMesh() {
        Configure(44100.0);
        Reset();
    }

    void Reset() {
        mY1.fill(0.0);
        mY2.fill(0.0);
        mDrive.fill(0.0);
        mInput1.fill(0.0);
        mInput2.fill(0.0);
        mSharedDrive = 0.0;
        mSounding.fill(0);
    }

    // Coefficients only (cheap, realtime safe): rate, engine-wide tuning,
    // model and traditional scale. A ringing receiver keeps its amplitude and
    // phase across the change.
    void Configure(double sampleRate, double tuneOffsetCents = 0.0,
                   EMarimbaModel model = EMarimbaModel::ImperialRosewood, int tuningScale = 0) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
        mTine = model == EMarimbaModel::KalimbaArtisan;
        const MaterialLoss loss = GetMaterialLoss(model);
        mCount = mTine ? kTines : kNumResonators;
        for (int i = 0; i < mCount; ++i) {
            const int note = mTine ? kTineNotes[i] : kFirstNote + i;
            mNote[i] = note;
            const double cents = (note - 69) * 100.0 + tuneOffsetCents + TuningOffsetCents(tuningScale, note);
            const double f = 440.0 * std::exp2(cents / 1200.0);
            // Q = 1 / eta; a bar's tube halves its fundamental's Q.
            const double eta = mTine ? TineFundamentalLossFactor(f) : 2.0 * LossFactor(loss, f);
            const double q = 1.0 / std::max(eta, 1.0e-4);
            const double r = std::exp(-kPiD * f / (q * mSampleRate));
            const double w = std::min(kTwoPiD * f / mSampleRate, 0.98 * kPiD);
            const double sinOld = std::sin(mW[i]);
            if (mS[i] > 0.0 && sinOld > 1.0e-12) {
                const double rOld = std::sqrt(mS[i]);
                const double aSinPhi = (rOld * mY2[i] - mY1[i] * std::cos(mW[i])) / sinOld;
                mY2[i] = (mY1[i] * std::cos(w) + aSinPhi * std::sin(w)) / r;
            }
            mW[i] = w;
            mC[i] = 2.0 * r * std::cos(w);
            mS[i] = r * r;
            // Force-to-velocity band-pass with unity centre gain at every rate.
            mB[i] = ResonatorDenominatorAt(r, w) / (2.0 * std::sin(w));
            const double pan = mTine ? 0.5 + 0.40 * kTineSide[i] : 0.12 + 0.76 * (static_cast<double>(i) / (mCount - 1));
            mPanL[i] = 1.0 - pan;
            mPanR[i] = pan;
        }
        for (int i = mCount; i < kNumResonators; ++i) mY1[i] = mY2[i] = 0.0;
    }

    // Begin a sub-sample: clear accumulated drives and the list of keys that
    // are sounding as voices themselves (they are not receivers now).
    inline void BeginSample() {
        mSharedDrive = 0.0;
        if (mTine) for (int i = 0; i < mCount; ++i) mDrive[i] = 0.0;
    }

    inline void AccumulateVoice(int noteNumber, double drive) {
        if (!mTine) {
            mSharedDrive += drive;
            MarkSounding(noteNumber);
            return;
        }
        // Tines: weighted by distance along the bridge.
        const double slot = TineSlot(noteNumber);
        for (int i = 0; i < kTines; ++i) {
            const double d = std::abs(kTineSlot[i] - slot);
            if (d < 0.5) { mSounding[i] = 2; continue; }
            mDrive[i] += drive * (0.15 + 1.0 / (1.0 + d * d));
        }
    }

    // Process the receiver bank and produce a spatialized stereo halo.
    inline void Process(float amount, float& outL, float& outR) {
        if (amount <= 0.001f) { outL = outR = 0.0f; return; }
        double sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < mCount; ++i) {
            const double input = mTine ? mDrive[i] : mSharedDrive;
            const double drive = mSounding[i] ? 0.0 : input - mInput2[i];
            mInput2[i] = mInput1[i];
            mInput1[i] = mSounding[i] ? 0.0 : input;
            const double y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * drive;
            mY2[i] = mY1[i];
            mY1[i] = y;
            sumL += y * mPanL[i];
            sumR += y * mPanR[i];
            if (mSounding[i]) --mSounding[i];
        }
        const double gain = amount * (mTine ? kTineGain : kBarGain);
        outL = static_cast<float>(sumL * gain);
        outR = static_cast<float>(sumR * gain);
    }

    // Hands on the instrument: every receiver loses amplitude by `pole` per sample.
    inline void Damp(double pole) {
        for (int i = 0; i < mCount; ++i) {
            mY1[i] *= pole;
            mY2[i] *= pole;
        }
    }

    // Control rate: zero decayed states; clear any unstable one.
    void FlushTiny() {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::abs(mInput1[i]) < 1.0e-15) mInput1[i] = 0.0;
            if (std::abs(mInput2[i]) < 1.0e-15) mInput2[i] = 0.0;
            if (std::abs(mDrive[i]) < 1.0e-15) mDrive[i] = 0.0;
            const double mag = std::abs(mY1[i]) + std::abs(mY2[i]);
            if (mag < 1.0e-15 || !(mag <= kMaxState)) mY1[i] = mY2[i] = 0.0;
        }
    }

    bool HasNoSubnormalState() const {
        for (int i = 0; i < kNumResonators; ++i) {
            if (std::fpclassify(mY1[i]) == FP_SUBNORMAL || std::fpclassify(mY2[i]) == FP_SUBNORMAL
                || std::fpclassify(mInput1[i]) == FP_SUBNORMAL || std::fpclassify(mInput2[i]) == FP_SUBNORMAL
                || std::fpclassify(mDrive[i]) == FP_SUBNORMAL) return false;
        }
        return true;
    }

    float GetResonatorOutput(int index) const {
        return index >= 0 && index < kNumResonators ? static_cast<float>(mY1[index]) : 0.0f;
    }
    int ReceiverCount() const { return mCount; }
    int ReceiverNote(int index) const { return index >= 0 && index < mCount ? mNote[index] : -1; }

private:
    static constexpr double kMaxState = 1.0e3;
    static constexpr double kBarGain = 0.8;
    static constexpr double kTineGain = 40.0;
    // 17-key C layout, left to right: D6 B5 G5 E5 C5 A4 F4 D4 C4 E4 G4 B4 D5 F5 A5 C6 E6.
    static constexpr int kTineNotes[kTines] = {86, 83, 79, 76, 72, 69, 65, 62, 60, 64, 67, 71, 74, 77, 81, 84, 88};
    static constexpr double kTineSlot[kTines] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static constexpr double kTineSide[kTines] = {-1.0, -0.875, -0.75, -0.625, -0.5, -0.375, -0.25, -0.125, 0.0,
                                                 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0};

    // Position along the bridge of any played note: the slot of the nearest
    // tine in pitch (chromatic notes fall between their neighbours).
    static double TineSlot(int noteNumber) {
        int best = 0;
        int distance = 1000;
        for (int i = 0; i < kTines; ++i) {
            const int d = std::abs(kTineNotes[i] - noteNumber);
            if (d < distance) { distance = d; best = i; }
        }
        return kTineSlot[best];
    }

    void MarkSounding(int noteNumber) {
        const int index = noteNumber - kFirstNote;
        if (index >= 0 && index < mCount) mSounding[index] = 2;
    }

    double mSampleRate = 44100.0;
    bool mTine = false;
    int mCount = kNumResonators;
    double mSharedDrive = 0.0;
    std::array<int, kNumResonators> mNote{};
    std::array<double, kNumResonators> mY1{}, mY2{}, mC{}, mS{}, mW{}, mB{};
    std::array<double, kNumResonators> mDrive{}, mInput1{}, mInput2{};
    std::array<double, kNumResonators> mPanL{}, mPanR{};
    std::array<uint8_t, kNumResonators> mSounding{};
};

} // namespace maremba
