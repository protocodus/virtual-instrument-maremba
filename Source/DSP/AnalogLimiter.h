#pragma once

#include "DspMath.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// Original circuit-inspired limiter: a stereo rectifier charges a control
// capacitor, a second reservoir prolongs recovery after sustained overload,
// and the control voltage drives a shunt-like attenuator. Transient headroom
// is handled by the engine's smooth lookahead gain after decimation; rounding
// individual peaks here would add unwanted distortion even at tiny amounts.
// These are voiced block models, not measured components or a hardware clone.
// The modeled control stage follows the complete captures at the host rate;
// the nonlinear Piezo Drive stage is processed separately while oversampled.
class AnalogLimiter {
public:
    AnalogLimiter() {
        SetSampleRate(kReferenceRate);
        SetAmount(0.25f);
        Reset();
    }

    void Reset() {
        mControl = mReservoir = 0.0f;
        mThreshold = mThresholdTarget;
        mEngagement = mEngagementTarget;
        mHasProcessed = false;
    }

    void SetSampleRate(double sampleRate) {
        mSampleRate = std::isfinite(sampleRate) && sampleRate > 8000.0
            ? sampleRate : kReferenceRate;
        mEngagementStep = static_cast<float>(1.0 / (0.002 * mSampleRate));
        mParameterAlpha = Alpha(5.0);
        mReservoirChargeAlpha = Alpha(80.0);
        UpdateBallistics();
    }

    // Existing patch/automation units remain milliseconds; they describe the
    // main control capacitor, independently of final clean headroom protection.
    void SetTimes(float attackMs, float releaseMs) {
        attackMs = FiniteRange(attackMs, 1.0f, 50.0f, 10.0f);
        releaseMs = FiniteRange(releaseMs, 20.0f, 500.0f, 120.0f);
        if (attackMs == mAttackMs && releaseMs == mReleaseMs) return;
        mAttackMs = attackMs;
        mReleaseMs = releaseMs;
        UpdateBallistics();
    }

    // The first 30% of Limit keeps the musical stage clean. Above that,
    // remap the remaining travel to the full -3 to -18 dBFS threshold range.
    // Automatic makeup gain compensates for the lower threshold. Once fully
    // engaged, sustained overload settles at -3 dBFS while quieter material
    // comes up. The onset blends this response with the clean signal.
    // The factory 0.05 setting is therefore unity, with no makeup or reduction.
    // Expensive coefficient/threshold math happens only on parameter changes.
    void SetAmount(float amount) {
        amount = FiniteRange(amount, 0.0f, 1.0f, 0.25f);
        if (amount == mAmount) return;
        mAmount = amount;
        const double activeAmount = std::max(0.0,
            (static_cast<double>(amount) - static_cast<double>(0.30f)) / (1.0 - static_cast<double>(0.30f)));
        const double thresholdDb = -3.0 - 15.0 * std::pow(activeAmount, 1.4);
        mThresholdTarget = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
        // Fade the control's contribution in over 30--37%, avoiding a static
        // transfer jump just above the clean range as well as live clicks.
        const double engagement = std::min(1.0, activeAmount * 10.0);
        mEngagementTarget = static_cast<float>(engagement * engagement * (3.0 - 2.0 * engagement));
        // Configuration before a first strike, and an explicit Reset, are
        // immediately settled. Live bypass changes crossfade over 2 ms.
        if (!mHasProcessed) {
            mThreshold = mThresholdTarget;
            mEngagement = mEngagementTarget;
        }
    }

    void Process(float inL, float inR, float& outL, float& outR) {
        mHasProcessed = true;
        if (mEngagement < mEngagementTarget)
            mEngagement = std::min(mEngagementTarget, mEngagement + mEngagementStep);
        else if (mEngagement > mEngagementTarget)
            mEngagement = std::max(mEngagementTarget, mEngagement - mEngagementStep);

        if (mEngagement == 0.0f) {
            // Exact zero bypass, without stale control charge on re-enabling.
            mControl = mReservoir = 0.0f;
            mThreshold = mThresholdTarget;
            outL = inL;
            outR = inR;
            return;
        }

        mThreshold += mParameterAlpha * (mThresholdTarget - mThreshold);
        // The makeup follows this value, and at high internal rates the float
        // smoother stalls up to ~3e-4 short of its target: land it exactly.
        if (std::abs(mThresholdTarget - mThreshold) < 1.0e-3f * mThresholdTarget)
            mThreshold = mThresholdTarget;
        const float peak = std::max(std::abs(inL), std::abs(inR));
        // Bound control voltage well beyond the engine's valid signal range;
        // even a finite input near FLT_MAX must not poison capacitor state.
        const float normalizedPeak = std::min(peak / mThreshold, 1.0e6f);
        const float demand = RectifiedOverdrive(normalizedPeak - 1.0f);

        // The extra reservoir fills slowly, so an isolated strike releases
        // promptly while sustained drive leaves a longer recovery component.
        const float reservoirAlpha = demand > mReservoir
            ? mReservoirChargeAlpha : mReservoirReleaseAlpha;
        mReservoir += reservoirAlpha * (demand - mReservoir);
        const float target = std::max(demand, 0.30f * mReservoir);
        const float controlAlpha = target > mControl ? mAttackAlpha : mReleaseAlpha;
        mControl += controlAlpha * (target - mControl);

        // The positive capacitor voltage increases attenuation conductance.
        // With constant overdrive, g=1/(1+c) tends to threshold/peak: limiting,
        // rather than the old low-ratio compressor's partial gain reduction.
        // The makeup follows the smoothed threshold, so automation stays
        // click-free, and fades in and out with the engagement crossfade.
        const float wetGain = (kMakeupReference / mThreshold) / (1.0f + mControl);
        // A common gain preserves stereo balance, polarity and antiphase.
        const float gain = (1.0f - mEngagement) + mEngagement * wetGain;
        outL = inL * gain;
        outR = inR * gain;
    }

    void FlushTiny() {
        if (mControl < 1.0e-20f) mControl = 0.0f;
        if (mReservoir < 1.0e-20f) mReservoir = 0.0f;
    }

    bool HasNoSubnormalState() const {
        for (float x : {mControl, mReservoir, mThreshold, mEngagement})
            if (!std::isfinite(x) || std::fpclassify(x) == FP_SUBNORMAL) return false;
        return true;
    }

private:
    static float FiniteRange(float x, float low, float high, float fallback) {
        return std::isfinite(x) ? std::max(low, std::min(high, x)) : fallback;
    }

    static float RectifiedOverdrive(float x) {
        // A narrow C1 knee avoids an abrupt control-current onset at threshold.
        constexpr float halfKnee = 0.10f;
        if (x <= -halfKnee) return 0.0f;
        if (x >= halfKnee) return x;
        const float knee = x + halfKnee;
        return knee * knee / (4.0f * halfKnee);
    }

    float Alpha(double milliseconds) const {
        return static_cast<float>(1.0 - std::exp(-1000.0 / (milliseconds * mSampleRate)));
    }

    void UpdateBallistics() {
        mAttackAlpha = Alpha(mAttackMs);
        mReleaseAlpha = Alpha(mReleaseMs);
        mReservoirReleaseAlpha = Alpha(4.0 * mReleaseMs);
    }

    // The threshold at the smallest nonzero Amount (-3 dBFS). Makeup is
    // measured from it, so the level is continuous with the exact bypass.
    static constexpr float kMakeupReference = 0.70794578f;

    double mSampleRate = kReferenceRate;
    float mAmount = -1.0f;
    float mAttackMs = 10.0f;
    float mReleaseMs = 120.0f;
    float mAttackAlpha = 0.0f;
    float mReleaseAlpha = 0.0f;
    float mReservoirChargeAlpha = 0.0f;
    float mReservoirReleaseAlpha = 0.0f;
    float mParameterAlpha = 0.0f;
    float mEngagementStep = 0.0f;
    float mThresholdTarget = 1.0f;
    float mThreshold = 1.0f;
    float mEngagementTarget = 1.0f;
    float mEngagement = 1.0f;
    float mControl = 0.0f;
    float mReservoir = 0.0f;
    bool mHasProcessed = false;
};

} // namespace maremba
