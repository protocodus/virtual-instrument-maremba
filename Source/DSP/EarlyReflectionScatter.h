#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace maremba {
// Object/geometry scattering, independently implemented from the
// allpass-cascade principle in Ewert et al. (2023), section3:
// https://arxiv.org/pdf/2306.16696 . No source code from the paper is used.
// A fixed causal allpass cascade spreads each image-source arrival without
// changing that path's long-term magnitude or total impulse energy. This is
// not a frequency-dependent rough-surface scattering/decomposition model.
class EarlyReflectionScatter {
public:
    static constexpr int kStages=4;
    size_t Layout(double meanDelaySeconds,double maximumRate){
        const auto seconds=Delays(meanDelaySeconds);size_t size=0;
        for(int i=0;i<kStages;++i){mOffset[i]=size;mSpan[i]=std::max(2,int(std::ceil(seconds[i]*maximumRate))+1);size+=mSpan[i];}
        mSamples=size;return size;
    }
    void Configure(double meanDelaySeconds,double rate){
        if(mSamples==0)return;
        const auto seconds=Delays(meanDelaySeconds);
        for(int i=0;i<kStages;++i)mDelay[i]=std::clamp(int(std::lround(seconds[i]*rate)),1,mSpan[i]);
        mWrite.fill(0);
    }
    void Reset(float* memory){if(mSamples>0)std::fill(memory,memory+mSamples,0.f);mWrite.fill(0);}
    float Process(float x,float* memory){
        if(mSamples==0)return x;
        for(int i=0;i<kStages;++i){
            float* line=memory+mOffset[i];
            const float y=kGain[i]*x+line[mWrite[i]];
            const float state=x-kGain[i]*y;
            line[mWrite[i]]=(state<1.e-20f&&state>-1.e-20f)?0.f:state;
            if(++mWrite[i]==mDelay[i])mWrite[i]=0;
            x=y;
        }
        return x;
    }
    int MeanGroupDelaySamples()const{int n=0;for(int d:mDelay)n+=d;return n;}
    static std::array<double,kStages> Delays(double budget){
        // Declared adaptation: four UNEQUAL pi-ratio delays, normalized to the
        // desired mean group-delay budget. The paper's printed Eq4 repeats a
        // pi^-2 term despite a geometric-series normalization; this code does
        // not claim to reproduce that ambiguous literal parameterization.
        const double pi=3.14159265358979323846;
        std::array<double,kStages> ratios{1./(pi*pi*pi),1./(pi*pi),1./pi,1.};
        double sum=0;for(double r:ratios)sum+=r;
        for(double& r:ratios)r*=budget/sum;
        return ratios;
    }
private:
    static constexpr std::array<float,kStages> kGain{.7071067811865475f,.7071067811865475f,.5f,.3535533905932738f};
    std::array<int,kStages> mSpan{},mDelay{},mWrite{};
    std::array<size_t,kStages> mOffset{};
    size_t mSamples=0;
};
}
