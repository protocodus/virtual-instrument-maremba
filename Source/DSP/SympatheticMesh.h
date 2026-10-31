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
// within a few tenths of a hertz of its pitch builds up: coincident partials
// and the brief broadband attack. Wooden bars
// share cords and frame, so they are driven by the summed support force; a
// kalimba's tines share one bridge, so each tine hears the others' bridge
// reaction weighted by how close they sit on it (physical neighbours are a
// third apart on the 17-key layout, and they ring most; Holdaway). A balafon
// in a traditional tuning is built with only the keys that carry its degrees
// (a gyil's 14 pentatonic keys, a bala's heptatonic ones): the keyboard's
// keys between them are not on the instrument and do not ring along.
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
    // model, traditional scale and each key's Detune offset (cents, by MIDI
    // note; null for none). A ringing receiver keeps its amplitude and phase
    // across the change, except between the bar and tine banks, which are
    // different keys: a switch between them starts from rest.
    void Configure(double sampleRate, double tuneOffsetCents = 0.0,
                   EMarimbaModel model = EMarimbaModel::ImperialRosewood, int tuningScale = 0,
                   const float* keyDetuneCents = nullptr) {
        mSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
        const bool tine = model == EMarimbaModel::KalimbaArtisan;
        if (tine != mTine) Reset();
        mTine = tine;
        const MaterialLoss loss = GetMaterialLoss(model);
        const double radiationScale = GetModelParams(model).sympatheticRadiationGain;
        mCount = mTine ? kTines : kNumResonators;
        const bool degreesOnly = model == EMarimbaModel::BalafonAncestral && tuningScale != 0;
        mAllPresent = true;
        for (int i = 0; i < mCount; ++i) {
            const int note = mTine ? kTineNotes[i] : kFirstNote + i;
            mNote[i] = note;
            const bool present = !degreesOnly || detail::IsDegreeKey(tuningScale, note);
            mAllPresent = mAllPresent && present;
            if (!present) { mY1[i] = mY2[i] = mInput1[i] = mInput2[i] = 0.0; mSounding[i] = 0; }
            mPresent[i] = present;
            const double cents = (note - 69) * 100.0 + tuneOffsetCents + TuningOffsetCents(tuningScale, note)
                               + (keyDetuneCents ? keyDetuneCents[note] : 0.0f);
            const double f = 440.0 * std::exp2(cents / 1200.0);
            // Q = 1 / eta; a bar's resonator takes about half its
            // fundamental's Q (Hill's C2 keeps 0.54 of its bare decay). The
            // concert marimba's tubes take less: the Iowa MIS 2012 keys ring
            // 0.71-0.85 of the bare bars' decay through C2-C4, so 1 / 0.74.
            const double tube = model == EMarimbaModel::ImperialRosewood ? 1.35 : 2.0;
            const double eta = mTine ? TineFundamentalLossFactor(f) : tube * LossFactor(loss, f);
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
            // Rate-invariant band-pass with each model's radiation calibration.
            // Weight new excitation, preserving stored tail amplitude/phase
            // across live Model changes, as FrameBody does.
            mB[i] = radiationScale * ResonatorDenominatorAt(r, w) / (2.0 * std::sin(w));
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
        const int slot = noteNumber >= 0 && noteNumber < 128
            ? kTineNoteSlots[static_cast<std::size_t>(noteNumber)] : static_cast<int>(TineSlot(noteNumber));
        for (int i = 0; i < kTines; ++i) {
            if (i == slot) { mSounding[i] = 2; continue; }
            mDrive[i] += drive * kTineCoupling[static_cast<std::size_t>(slot)][static_cast<std::size_t>(i)];
        }
    }

    // Advance physical receivers without inventing a microphone pan. The
    // recording scene reads each receiver at its own source position.
    inline void Advance(float amount) {
        if (amount <= 0.0f) return;
        DispatchAdvance<false>(amount, nullptr);
    }

    // Engine readout: update and read each receiver while its state is hot.
    // Writes ReceiverCount() entries in unchanged physical-key order. The
    // caller owns the fixed output buffer; no storage or allocation is added.
    // Keep the original y * amount * gain operation order and float rounding.
    inline void AdvanceAndRead(float amount, float* receiverPressure) {
        if (amount <= 0.0f) {
            // Advance historically does nothing here, while its separate
            // readout still multiplies the retained state by the amount.
            for (int i = 0; i < mCount; ++i)
                receiverPressure[i] = GetReceiverPressure(i, amount);
            return;
        }
        DispatchAdvance<true>(amount, receiverPressure);
    }

    // Legacy standalone readout used by the mechanical-response contracts.
    inline void Process(float amount, float& outL, float& outR) {
        if (amount <= 0.0f) { outL = outR = 0.0f; return; }
        Advance(amount);
        double sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < mCount; ++i) {
            if (!mPresent[i]) continue;
            sumL += mY1[i] * mPanL[i];
            sumR += mY1[i] * mPanR[i];
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
    // Legacy-calibrated nominal receiver signal, before microphone placement;
    // this is not an SI velocity or pressure measurement. The engine converts
    // it to its declared reference pressure before feeding both microphones.
    float GetReceiverPressure(int index, float amount) const {
        return index >= 0 && index < mCount && mPresent[index]
            ? static_cast<float>(mY1[index] * amount * (mTine ? kTineGain : kBarGain)) : 0.0f;
    }
    int ReceiverCount() const { return mCount; }
    bool IsTine() const { return mTine; }
    int ReceiverNote(int index) const { return index >= 0 && index < mCount ? mNote[index] : -1; }

private:
    template<bool Read>
    inline void DispatchAdvance(float amount, float* receiverPressure) {
        if (mTine) AdvanceReceivers<true, true, Read>(amount, receiverPressure);
        else if (mAllPresent) AdvanceReceivers<false, true, Read>(amount, receiverPressure);
        else AdvanceReceivers<false, false, Read>(amount, receiverPressure);
    }

    template<bool Tine, bool AllPresent, bool Read>
    inline void AdvanceReceivers(float amount, float* receiverPressure) {
        constexpr int count = Tine ? kTines : kNumResonators;
        for (int i = 0; i < count; ++i) {
            if constexpr (!AllPresent) {
                if (!mPresent[i]) {
                    if constexpr (Read) receiverPressure[i] = 0.0f;
                    continue;
                }
            }
            const double input = Tine ? mDrive[i] : mSharedDrive;
            const bool sounding = mSounding[i] != 0;
            const double drive = sounding ? 0.0 : input - mInput2[i];
            mInput2[i] = mInput1[i];
            mInput1[i] = sounding ? 0.0 : input;
            const double y = mC[i] * mY1[i] - mS[i] * mY2[i] + mB[i] * drive;
            mY2[i] = mY1[i];
            mY1[i] = y;
            if (sounding) --mSounding[i];
            if constexpr (Read)
                receiverPressure[i] = static_cast<float>(y * amount * (Tine ? kTineGain : kBarGain));
        }
    }

    static constexpr double kMaxState = 1.0e3;
    static constexpr double kBarGain = 0.8;
    static constexpr double kTineGain = 40.0;
    // 17-key C layout, left to right: D6 B5 G5 E5 C5 A4 F4 D4 C4 E4 G4 B4 D5 F5 A5 C6 E6.
    static constexpr int kTineNotes[kTines] = {86, 83, 79, 76, 72, 69, 65, 62, 60, 64, 67, 71, 74, 77, 81, 84, 88};
    static constexpr double kTineSlot[kTines] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static constexpr double kTineSide[kTines] = {-1.0, -0.875, -0.75, -0.625, -0.5, -0.375, -0.25, -0.125, 0.0,
                                                 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0};
    // The bridge layout never changes with sample rate, tuning or model
    // controls. Cache the exact existing nearest-tine rule (including ties)
    // and distance law; accumulation order and receiver states stay intact.
    inline static constexpr auto kTineNoteSlots = [] {
        std::array<int, 128> slots{};
        for (int note = 0; note < 128; ++note) {
            int best = 0, distance = 1000;
            for (int i = 0; i < kTines; ++i) {
                const int difference = kTineNotes[i] - note;
                const int d = difference < 0 ? -difference : difference;
                if (d < distance) { distance = d; best = i; }
            }
            slots[static_cast<std::size_t>(note)] = best;
        }
        return slots;
    }();
    inline static constexpr auto kTineCoupling = [] {
        std::array<std::array<double, kTines>, kTines> weights{};
        for (int slot = 0; slot < kTines; ++slot) for (int i = 0; i < kTines; ++i) {
            const double difference = kTineSlot[i] - kTineSlot[slot];
            const double d = difference < 0.0 ? -difference : difference;
            weights[static_cast<std::size_t>(slot)][static_cast<std::size_t>(i)] = 0.15 + 1.0 / (1.0 + d * d);
        }
        return weights;
    }();

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
    bool mAllPresent = true;
    int mCount = kNumResonators;
    double mSharedDrive = 0.0;
    std::array<int, kNumResonators> mNote{};
    std::array<double, kNumResonators> mY1{}, mY2{}, mC{}, mS{}, mW{}, mB{};
    std::array<double, kNumResonators> mDrive{}, mInput1{}, mInput2{};
    std::array<double, kNumResonators> mPanL{}, mPanR{};
    std::array<uint8_t, kNumResonators> mSounding{};
    std::array<bool, kNumResonators> mPresent{};
};

} // namespace maremba
