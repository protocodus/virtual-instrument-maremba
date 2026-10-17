#pragma once

#include "AcousticRoom.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace maremba {

// Original implementations of standard reverb structures. See
// the Rack device's Docs/research/character-and-rooms.md for design sources and measured responses.
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
        mData[mIndex] = FlushTiny(x);
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

// Compact chamber: a hard-walled echo chamber, denser and darker than the wood
// room. The band-limited (6.5 kHz) input is diffused by three short allpasses
// and fed to the 12-line feedback network: T60 0.85 s through the mids, 0.40 s
// at 4 kHz and 0.20 s at 10 kHz (plaster and air absorb the highs). Like the
// Wood room it runs on the room tick.
class ChamberRoom {
public:
    void Allocate(double maxRate) {
        mTail.memory.assign(mTail.Layout(kVoicing, RoomTick::MaxRate(std::max(maxRate, 8000.0))), 0.0f);
    }
    void SetSampleRate(double rate) {
        mBand.SetSampleRate(kBandwidthHz, rate);
        mTick.SetSampleRate(rate);
        mTail.SetSampleRate(kVoicing, mTick.Rate());
        Reset();
    }
    void Reset() { mTail.Reset(); mBand.Reset(); mTick.Reset(); mSum = 0.0f; mFlushTicks = 0; }
    void Process(float in, float& left, float& right) {
        if (!mTail.IsAllocated()) { left = right = 0.0f; return; }
        mSum += mBand.Process(RoomExcitation(in));
        if (mTick.Advance()) {
            float l = 0.0f, r = 0.0f;
            mTail.Process(mSum * mTick.Mean(), l, r);
            mSum = 0.0f;
            mTick.Emit(kGain * l, kGain * r);
            if (++mFlushTicks == FeedbackNetwork::kFlushPeriod) {
                mFlushTicks = 0;
                mTail.FlushStates();
                mBand.FlushStates();
            }
        }
        mTick.Output(left, right);
    }
private:
    static constexpr double kBandwidthHz = 6500.0;
    // Loops of 12-64 ms and diffusers of 1.9-4.8 ms (whole primes at kVoicingRate)
    static constexpr NetworkVoicing kVoicing = {
        { 523 / kVoicingRate, 691 / kVoicingRate, 877 / kVoicingRate, 1063 / kVoicingRate,
          1259 / kVoicingRate, 1471 / kVoicingRate, 1693 / kVoicingRate, 1901 / kVoicingRate,
          2113 / kVoicingRate, 2351 / kVoicingRate, 2591 / kVoicingRate, 2833 / kVoicingRate },
        { 83 / kVoicingRate, 137 / kVoicingRate, 211 / kVoicingRate }, 0.65,
        { 0.85, 0.85, 0.40, 0.20 } };
    static constexpr float kGain = 0.526f; // the 1.3.0f5 Chamber's loudness
    RoomBandLimit mBand;
    RoomTick mTick;
    FeedbackNetwork mTail;
    float mSum = 0.0f;
    int mFlushTicks = 0;
};

// Spacious hall: input diffusers feeding a two-branch cross-coupled allpass
// tank, inspired by the published Dattorro/Griesinger topology. Our lengths,
// fixed taps, damping and gain are voiced for mallets, not a hardware clone.
// The band-limited input (air absorption) keeps its early response the same
// at every internal rate.
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
        mBand.SetSampleRate(kBandwidthHz, rate);
        Reset();
    }
    void Reset() {
        for (auto& d:mInput) d.Reset();
        for (auto& d:mTank) d.Reset();
        mBand.Reset();
        mLastL=mLastR=mDampL=mDampR=0;
        mFlushCount=0;
    }
    void Process(float in, float& left, float& right) {
        float x=mBand.Process(RoomExcitation(in));
        if (++mFlushCount==FeedbackNetwork::kFlushPeriod) { mFlushCount=0; mBand.FlushStates(); }
        for (size_t i=0;i<mInput.size();++i) x=mInput[i].Allpass(x,i<2?.65f:.55f);
        float l=mTank[0].Allpass(x+.55f*mLastR,-.62f);
        float r=mTank[4].Allpass(x+.55f*mLastL,-.62f);
        l=mTank[1].Process(l); r=mTank[5].Process(r);
        mDampL=FlushTiny(mDampL+mAlpha*(l-mDampL));
        mDampR=FlushTiny(mDampR+mAlpha*(r-mDampR));
        mLastL=mTank[3].Process(mTank[2].Allpass(mDampL,.5f));
        mLastR=mTank[7].Process(mTank[6].Allpass(mDampR,.5f));
        left=.42f*(mTank[1].Tap(.27f)+mTank[3].Tap(.63f)-mTank[5].Tap(.41f)+mLastR);
        right=.42f*(mTank[5].Tap(.31f)+mTank[7].Tap(.71f)-mTank[1].Tap(.53f)+mLastL);
    }
private:
    static constexpr double kBandwidthHz=12000.0;
    static constexpr double kInputTimes[4]={.00477,.00360,.01273,.00930};
    static constexpr double kTankTimes[8]={.0271,.0727,.0433,.0893,.0331,.0877,.0667,.1061};
    std::array<RoomDelay,4> mInput;
    std::array<RoomDelay,8> mTank;
    RoomBandLimit mBand;
    float mAlpha=0,mLastL=0,mLastR=0,mDampL=0,mDampR=0;
    int mFlushCount=0;
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
