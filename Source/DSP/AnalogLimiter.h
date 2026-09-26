#pragma once

#include "DspMath.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// Original circuit-inspired limiter: a stereo rectifier charges a control
// capacitor, a second reservoir prolongs recovery after sustained overload,
// and the control voltage drives a shunt-like attenuator. A smooth diode-like
// rail catches transients while the musical attack capacitor is still charging.
// These are voiced block models, not measured components or a hardware clone.
// The modeled analog stages run at the engine's oversampled rate; a final
// sample-peak guard handles any overshoot created by decimation.
class AnalogLimiter {
public:
    static constexpr float kOutputCeiling = 0.98f;

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
    // main control capacitor, independently of the instantaneous soft rail.
    void SetTimes(float attackMs, float releaseMs) {
        attackMs = FiniteRange(attackMs, 1.0f, 50.0f, 10.0f);
        releaseMs = FiniteRange(releaseMs, 20.0f, 500.0f, 120.0f);
        if (attackMs == mAttackMs && releaseMs == mReleaseMs) return;
        mAttackMs = attackMs;
        mReleaseMs = releaseMs;
        UpdateBallistics();
    }

    // Amount lowers the control threshold from -3 to -18 dBFS. The default
    // 0.25 is deliberately gentle; there is no automatic makeup gain.
    // Expensive coefficient/threshold math happens only on parameter changes.
    void SetAmount(float amount) {
        amount = FiniteRange(amount, 0.0f, 1.0f, 0.25f);
        if (amount == mAmount) return;
        mAmount = amount;
        const double thresholdDb = -3.0 - 15.0 * std::pow(static_cast<double>(amount), 1.4);
        mThresholdTarget = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
        mEngagementTarget = amount > 0.0f ? 1.0f : 0.0f;
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
        float wetGain = 1.0f / (1.0f + mControl);
        const float wetPeak = peak * wetGain;
        if (wetPeak > kRailKnee) {
            const float excess = wetPeak - kRailKnee;
            const float span = kRailCeiling - kRailKnee;
            const float roundedPeak = kRailKnee + span * excess / (span + excess);
            wetGain *= roundedPeak / wetPeak;
        }
        // A common gain preserves stereo balance, polarity and antiphase.
        const float gain = (1.0f - mEngagement) + mEngagement * wetGain;
        outL = inL * gain;
        outR = inR * gain;
    }

    // The decimator can overshoot a bounded internal waveform. This emergency
    // linked sample-peak guard belongs AFTER decimation and master attenuation.
    // It adds no latency and is not an intersample/true-peak limiter. Enabling
    // the limiter protects immediately, including its wet-path crossfade;
    // only an intentional transition back to bypass eases the cap away.
    void ProtectOutput(float& left, float& right) const {
        if (mEngagement == 0.0f && mEngagementTarget == 0.0f) return;
        const float peak = std::max(std::abs(left), std::abs(right));
        if (peak > kOutputCeiling) {
            const float protection = mEngagementTarget > 0.0f ? 1.0f : mEngagement;
            const float gain = (1.0f - protection) + protection * (kOutputCeiling / peak);
            left *= gain;
            right *= gain;
        }
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
    static constexpr float kRailKnee = 0.88f;
    static constexpr float kRailCeiling = 0.94f;

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
