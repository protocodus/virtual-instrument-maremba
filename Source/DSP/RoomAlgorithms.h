#pragma once

#include "AcousticRoom.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace maremba {

// Original implementations of standard reverb structures. See
// docs/CHARACTER_AND_ROOMS.md for design sources and measured responses.
// Memory is reserved once; control/rate changes never resize a vector.
class RoomDelay {
public:
    void Allocate(double maxRate, double seconds) {
        mData.assign(static_cast<size_t>(std::ceil(maxRate * seconds)) + 4, 0.0f);
    }
    void Configure(double rate, double seconds) {
        mLength = std::clamp(static_cast<int>(rate * seconds), 1, static_cast<int>(mData.size()));
        Reset();
    }
    void Reset() { std::fill(mData.begin(), mData.begin() + mLength, 0.0f); mIndex = 0; }
    float Read() const { return mData[mIndex]; }
    void Write(float x) {
        // Silence subnormal feedback states before they become expensive.
        mData[mIndex] = std::abs(x) < 1.0e-20f ? 0.0f : x;
        if (++mIndex == mLength) mIndex = 0;
    }
    float Process(float x) { const float y = Read(); Write(x); return y; }
    float Allpass(float x, float g) {
        const float y = Read() - g*x;
        Write(x + g*y);
        return y;
    }
    float Tap(float fraction) const {
        int index = mIndex - std::clamp(static_cast<int>(mLength*fraction), 1, mLength);
        if (index < 0) index += mLength;
        return mData[index];
    }
private:
    std::vector<float> mData;
    int mLength = 0, mIndex = 0;
};

// Compact chamber: eight mutually coupled delays with a Householder matrix.
// The matrix is lossless, each loop gain is below one, and the lowpass filters
// have unity DC gain. This gives a stable, dense, short tail (0.85 s LF T60).
class ChamberRoom {
public:
    void Allocate(double maxRate) {
        for (size_t i=0;i<mLines.size();++i) mLines[i].Allocate(maxRate,kTimes[i]);
        mDiffuse.Allocate(maxRate,.0047);
    }
    void SetSampleRate(double rate) {
        for (size_t i=0;i<mLines.size();++i) {
            mLines[i].Configure(rate,kTimes[i]);
            mGain[i] = static_cast<float>(std::pow(10.0,-3.0*kTimes[i]/.85));
        }
        mDiffuse.Configure(rate,.0047);
        mAlpha = static_cast<float>(1.0-std::exp(-kTwoPiD*4500.0/rate));
        Reset();
    }
    void Reset() { for (auto& d:mLines) d.Reset(); mDiffuse.Reset(); mDamp.fill(0); }
    void Process(float in, float& left, float& right) {
        const float x = mDiffuse.Allpass(in,.6f);
        float sum=0;
        for (size_t i=0;i<mLines.size();++i) {
            mDamp[i] += mAlpha*(mLines[i].Read()-mDamp[i]);
            if (std::abs(mDamp[i])<1.0e-20f) mDamp[i]=0;
            sum += mDamp[i];
        }
        left = .45f*(mDamp[0]+mDamp[2]-mDamp[5]-mDamp[7]);
        right = .45f*(mDamp[1]-mDamp[3]+mDamp[4]-mDamp[6]);
        for (size_t i=0;i<mLines.size();++i)
            mLines[i].Write(.45f*kInput[i]*x+mGain[i]*(.25f*sum-mDamp[i]));
    }
private:
    static constexpr double kTimes[8]={.0137,.0179,.0221,.0269,.0313,.0371,.0437,.0511};
    static constexpr float kInput[8]={1,-1,1,1,-1,1,-1,-1};
    std::array<RoomDelay,8> mLines;
    RoomDelay mDiffuse;
    std::array<float,8> mDamp{},mGain{};
    float mAlpha=0;
};

// Spacious hall: input diffusers feeding a two-branch cross-coupled allpass
// tank, inspired by the published Dattorro/Griesinger topology. Our lengths,
// fixed taps, damping and gain are voiced for mallets, not a hardware clone.
class HallRoom {
public:
    void Allocate(double maxRate) {
        for (size_t i=0;i<mInput.size();++i) mInput[i].Allocate(maxRate,kInputTimes[i]);
        for (size_t i=0;i<mTank.size();++i) mTank[i].Allocate(maxRate,kTankTimes[i]);
    }
    void SetSampleRate(double rate) {
        for (size_t i=0;i<mInput.size();++i) mInput[i].Configure(rate,kInputTimes[i]);
        for (size_t i=0;i<mTank.size();++i) mTank[i].Configure(rate,kTankTimes[i]);
        mAlpha = static_cast<float>(1.0-std::exp(-kTwoPiD*3600.0/rate));
        Reset();
    }
    void Reset() {
        for (auto& d:mInput) d.Reset();
        for (auto& d:mTank) d.Reset();
        mLastL=mLastR=mDampL=mDampR=0;
    }
    void Process(float in, float& left, float& right) {
        float x=in;
        for (size_t i=0;i<mInput.size();++i) x=mInput[i].Allpass(x,i<2?.65f:.55f);
        float l=mTank[0].Allpass(x+.55f*mLastR,-.62f);
        float r=mTank[4].Allpass(x+.55f*mLastL,-.62f);
        l=mTank[1].Process(l); r=mTank[5].Process(r);
        mDampL+=mAlpha*(l-mDampL); mDampR+=mAlpha*(r-mDampR);
        if (std::abs(mDampL)<1e-20f) mDampL=0;
        if (std::abs(mDampR)<1e-20f) mDampR=0;
        mLastL=mTank[3].Process(mTank[2].Allpass(mDampL,.5f));
        mLastR=mTank[7].Process(mTank[6].Allpass(mDampR,.5f));
        left=.42f*(mTank[1].Tap(.27f)+mTank[3].Tap(.63f)-mTank[5].Tap(.41f)+mLastR);
        right=.42f*(mTank[5].Tap(.31f)+mTank[7].Tap(.71f)-mTank[1].Tap(.53f)+mLastL);
    }
private:
    static constexpr double kInputTimes[4]={.00477,.00360,.01273,.00930};
    static constexpr double kTankTimes[8]={.0271,.0727,.0433,.0893,.0331,.0877,.0667,.1061};
    std::array<RoomDelay,4> mInput;
    std::array<RoomDelay,8> mTank;
    float mAlpha=0,mLastL=0,mLastR=0,mDampL=0,mDampR=0;
};

class RoomBank {
public:
    void Allocate(double maxRate) {
        mWood.Allocate(maxRate); mChamber.Allocate(maxRate); mHall.Allocate(maxRate);
    }
    void SetSampleRate(double rate) {
        mWood.SetSampleRate(rate); mChamber.SetSampleRate(rate); mHall.SetSampleRate(rate);
        mFadeStep=static_cast<float>(1.0/(.05*rate)); Reset();
    }
    void SetType(int type) {
        mWanted=std::clamp(type,0,2);
        // A choice made before the first strike starts in that space directly.
        if (mPristine) { mActive=mNext=mWanted; mFade=1; }
    }
    void Reset() {
        mWood.Reset(); mChamber.Reset(); mHall.Reset();
        mActive=mWanted; mNext=mActive; mFade=1;
        mPristine=true;
    }
    void Process(float inL,float inR,float level,float& dryL,float& dryR,float& wetL,float& wetR) {
        dryL=.7f*inL; dryR=.7f*inR;
        if (level<=.001f) { wetL=wetR=0; mActive=mWanted; mFade=1; return; }
        if (inL!=0 || inR!=0) mPristine=false;
        if (mFade>=1 && mWanted!=mActive) {
            mNext=mWanted; ResetType(mNext); mFade=0;
        }
        Wet(mActive,inL,inR,level,wetL,wetR);
        if (mFade<1) {
            float l=0,r=0; Wet(mNext,inL,inR,level,l,r);
            wetL += mFade*(l-wetL); wetR += mFade*(r-wetR);
            mFade=std::min(1.0f,mFade+mFadeStep);
            if (mFade>=1) mActive=mNext;
        }
    }
private:
    void ResetType(int type) {
        if (type==0) mWood.Reset(); else if (type==1) mChamber.Reset(); else mHall.Reset();
    }
    void Wet(int type,float l,float r,float level,float& outL,float& outR) {
        if (type==0) { float dl,dr; mWood.Process(l,r,level,dl,dr,outL,outR); }
        else {
            if (type==1) mChamber.Process((l+r)*.5f,outL,outR);
            else mHall.Process((l+r)*.5f,outL,outR);
            outL*=level; outR*=level;
        }
    }
    AcousticRoom mWood;
    ChamberRoom mChamber;
    HallRoom mHall;
    int mWanted=0,mActive=0,mNext=0;
    float mFade=1,mFadeStep=1;
    bool mPristine=true;
};
} // namespace maremba
