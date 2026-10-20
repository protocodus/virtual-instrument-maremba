#pragma once

#include <cstdint>
#include <cmath>

namespace maremba {

// The decimators run in float. Control-rate FlushTiny() zeroes states below
// this (about -400 dB) so a silent input never leaves them cycling on
// subnormal values.
constexpr float kDecimatorTinyState = 1.0e-20f;

// 2nd-order allpass section for half-band polyphase decomposition
struct HalfbandAllpass {
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    inline float Process(float x) {
        float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    inline void Reset() {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    inline void FlushTiny() {
        if (std::abs(x1) < kDecimatorTinyState) x1 = 0.0f;
        if (std::abs(y1) < kDecimatorTinyState) y1 = 0.0f;
    }

    inline bool HasNoSubnormalState() const {
        return std::fpclassify(x1) != FP_SUBNORMAL && std::fpclassify(y1) != FP_SUBNORMAL;
    }
};

// 2x Decimator using a 2-path polyphase allpass structure
class HalfbandDecimator2x {
public:
    HalfbandDecimator2x() {
        // High-attenuation coefficients for halfband polyphase
        mAp0.a = 0.165728f;
        mAp1.a = 0.590836f;
        Reset();
    }

    void Reset() {
        mAp0.Reset();
        mAp1.Reset();
        mZ1 = 0.0f;
    }

    void FlushTiny() {
        mAp0.FlushTiny();
        mAp1.FlushTiny();
        if (std::abs(mZ1) < kDecimatorTinyState) mZ1 = 0.0f;
    }

    bool HasNoSubnormalState() const {
        return mAp0.HasNoSubnormalState() && mAp1.HasNoSubnormalState() && std::fpclassify(mZ1) != FP_SUBNORMAL;
    }

    // Input: two consecutive samples at 2*Fs (s0, s1). Output: one sample at Fs.
    inline float Decimate(float s0, float s1) {
        float path0 = mAp0.Process(s0);
        float path1 = mAp1.Process(mZ1);
        mZ1 = s1;
        return (path0 + path1) * 0.5f;
    }

private:
    HalfbandAllpass mAp0;
    HalfbandAllpass mAp1;
    float mZ1 = 0.0f;
};

// Mono decimator for 1x, 2x, 4x, and 8x (same half-band cascade as
// the stereo main-bus decimator, so direct outs match it in response and phase)
class MonoOversampler {
public:
    MonoOversampler() {
        Reset();
    }

    void Reset() {
        for (int i = 0; i < 3; ++i) {
            mDec[i].Reset();
        }
    }

    void FlushTiny() {
        for (int i = 0; i < 3; ++i) {
            mDec[i].FlushTiny();
        }
    }

    bool HasNoSubnormalState() const {
        for (int i = 0; i < 3; ++i) {
            if (!mDec[i].HasNoSubnormalState()) return false;
        }
        return true;
    }

    inline float DownsampleFrame(const float* inBuf, int factor) {
        if (factor == 1) return DownsampleFrame<1>(inBuf);
        if (factor == 2) return DownsampleFrame<2>(inBuf);
        if (factor == 4) return DownsampleFrame<4>(inBuf);
        return DownsampleFrame<8>(inBuf);
    }

    // A scene has one common quality setting. Its source loop dispatches
    // once per frame, leaving this small, fixed cascade available to inline.
    template<int Factor>
    inline float DownsampleFrame(const float* inBuf) {
        static_assert(Factor == 1 || Factor == 2 || Factor == 4 || Factor == 8, "Unsupported oversampling factor");
        if constexpr (Factor == 1) {
            return inBuf[0];
        } else if constexpr (Factor == 2) {
            return mDec[0].Decimate(inBuf[0], inBuf[1]);
        } else if constexpr (Factor == 4) {
            float s0 = mDec[0].Decimate(inBuf[0], inBuf[1]);
            float s1 = mDec[0].Decimate(inBuf[2], inBuf[3]);
            return mDec[1].Decimate(s0, s1);
        } else { // 8x
            float s0 = mDec[0].Decimate(inBuf[0], inBuf[1]);
            float s1 = mDec[0].Decimate(inBuf[2], inBuf[3]);
            float s2 = mDec[0].Decimate(inBuf[4], inBuf[5]);
            float s3 = mDec[0].Decimate(inBuf[6], inBuf[7]);
            float mid0 = mDec[1].Decimate(s0, s1);
            float mid1 = mDec[1].Decimate(s2, s3);
            return mDec[2].Decimate(mid0, mid1);
        }
    }

private:
    HalfbandDecimator2x mDec[3];
};

// Stereo decimator for 1x, 2x, 4x, and 8x oversampling
class StereoOversampler {
public:
    StereoOversampler() {
        Reset();
    }

    void Reset() {
        for (int i = 0; i < 3; ++i) {
            mDecL[i].Reset();
            mDecR[i].Reset();
        }
    }

    void FlushTiny() {
        for (int i = 0; i < 3; ++i) {
            mDecL[i].FlushTiny();
            mDecR[i].FlushTiny();
        }
    }

    bool HasNoSubnormalState() const {
        for (int i = 0; i < 3; ++i) {
            if (!mDecL[i].HasNoSubnormalState() || !mDecR[i].HasNoSubnormalState()) return false;
        }
        return true;
    }

    // At 1x, pass through exactly without filtering, state changes or delay.
    // Downsample buffer of samples according to factor (1, 2, 4, or 8)
    // For 2x: takes 2 samples, produces 1
    // For 4x: takes 4 samples, produces 1
    // For 8x: takes 8 samples, produces 1
    inline void DownsampleFrame(const float* inBufL, const float* inBufR, int factor, float& outL, float& outR) {
        if (factor == 1) {
            outL = inBufL[0];
            outR = inBufR[0];
        } else if (factor == 2) {
            outL = mDecL[0].Decimate(inBufL[0], inBufL[1]);
            outR = mDecR[0].Decimate(inBufR[0], inBufR[1]);
        } else if (factor == 4) {
            float sL0 = mDecL[0].Decimate(inBufL[0], inBufL[1]);
            float sL1 = mDecL[0].Decimate(inBufL[2], inBufL[3]);
            outL = mDecL[1].Decimate(sL0, sL1);

            float sR0 = mDecR[0].Decimate(inBufR[0], inBufR[1]);
            float sR1 = mDecR[0].Decimate(inBufR[2], inBufR[3]);
            outR = mDecR[1].Decimate(sR0, sR1);
        } else { // 8x
            float sL0 = mDecL[0].Decimate(inBufL[0], inBufL[1]);
            float sL1 = mDecL[0].Decimate(inBufL[2], inBufL[3]);
            float sL2 = mDecL[0].Decimate(inBufL[4], inBufL[5]);
            float sL3 = mDecL[0].Decimate(inBufL[6], inBufL[7]);
            float sL_mid0 = mDecL[1].Decimate(sL0, sL1);
            float sL_mid1 = mDecL[1].Decimate(sL2, sL3);
            outL = mDecL[2].Decimate(sL_mid0, sL_mid1);

            float sR0 = mDecR[0].Decimate(inBufR[0], inBufR[1]);
            float sR1 = mDecR[0].Decimate(inBufR[2], inBufR[3]);
            float sR2 = mDecR[0].Decimate(inBufR[4], inBufR[5]);
            float sR3 = mDecR[0].Decimate(inBufR[6], inBufR[7]);
            float sR_mid0 = mDecR[1].Decimate(sR0, sR1);
            float sR_mid1 = mDecR[1].Decimate(sR2, sR3);
            outR = mDecR[2].Decimate(sR_mid0, sR_mid1);
        }
    }

private:
    HalfbandDecimator2x mDecL[3];
    HalfbandDecimator2x mDecR[3];
};

} // namespace maremba
