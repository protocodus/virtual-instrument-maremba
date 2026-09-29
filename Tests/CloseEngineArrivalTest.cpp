// A real engine strike, independently observed before acoustic capture. The
// reference uses the frozen measured FIR and an analytic propagation phase;
// it does not call CloseCapture or duplicate its FFT/delay implementation.
#include "../DSP/MarembaEngine.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static MarembaVoice CloneStruckVoice(const MarembaEngine& engine) {
        for (const auto& voice : engine.mVoices) if (voice.IsActive()) return voice;
        std::fprintf(stderr, "FAIL: engine did not configure a physical strike\n");
        std::exit(1);
    }
    static void FadeVoices(MarembaEngine& engine) {
        for (auto& voice : engine.mVoices) if (voice.IsActive()) voice.StartFade(.003);
    }
    static int ControlInterval() { return MarembaEngine::kControlInterval; }
};
}

namespace {
using namespace maremba;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<double, 7> frequencies{100.,500.,1000.,2000.,4000.,8000.,12000.};
constexpr float kStrikeVelocity=.1f;
int checks=0,cases=0;
double worstMagnitude=0,worstPhase=0,worstElectrical=0,maximumOutput=0;
int contextModel=0,contextNote=0,contextQuality=0;
double contextRate=0;
void Require(bool condition,const char* description) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr,"FAIL model%d note%d rate%.0f quality%d: %s\n",
                     contextModel,contextNote,contextRate,contextQuality,description);
        std::exit(1);
    }
}
std::complex<double> Transform(const float* samples,std::size_t count,double f,double rate) {
    const auto step=std::polar(1.0,-2*pi*f/rate);
    std::complex<double> phase{1,0},sum{};
    for(std::size_t i=0;i<count;++i) { sum+=static_cast<double>(samples[i])*phase;phase*=step; }
    return sum;
}
std::complex<double> Transform(const std::vector<float>& samples,double f,double rate) {
    return Transform(samples.data(),samples.size(),f,rate);
}
void Run(int model,int note,double rate,int quality) {
    contextModel=model;contextNote=note;contextRate=rate;contextQuality=quality;
    EngineParameters p;
    p.model=model;p.oversampling=quality;p.malletType=3;p.malletHardness=.7f;
    p.volume=.08f;p.preampDrive=0;p.compAmount=0;p.warmth=0; // Volume quiets Main only
    p.closeLevel=1;p.farLevel=0;p.piezoLevel=0;p.stereoWidth=1;
    p.artifacts=p.buzzAmount=p.bodyBloom=p.sympathetic=p.strikeJitter=p.detune=0;
    p.velocityToHardness=p.velocityToPosition=p.velocityToDecay=0;
    p.velocityToCarving=p.velocityToCoupling=p.velocityToBloom=p.velocityToSympathetic=0;
    p.velocityToBuzz=p.velocityToTension=p.velocityToArtifacts=0;
    auto engine=std::make_unique<MarembaEngine>(rate);
    // A soft strike keeps every direct output far below its protection.
    engine->SetParameters(p);engine->NoteOn(note,kStrikeVelocity);
    auto physical=MarembaEngineTestAccess::CloneStruckVoice(*engine);
    const int factor=2<<quality;
    const int frames=static_cast<int>(std::ceil(.080*rate));
    const int fadeFrame=static_cast<int>(std::ceil(.040*rate));
    const int latency=OutputHeadroom::LatencyFrames(rate);
    // Direct outputs carry only their calibration (never Volume).
    const float closeGain=CaptureScene::kMasterMixScale*MarembaEngine::kCloseStemTrim;
    const float piezoGain=CaptureScene::kMasterMixScale*MarembaEngine::kPiezoStemTrim;
    std::array<MonoOversampler,3> decimator;
    std::array<std::vector<float>,3> raw;
    std::array<std::vector<float>,7> output;
    for(auto& channel:raw) channel.resize(static_cast<std::size_t>(frames));
    for(auto& channel:output) channel.resize(static_cast<std::size_t>(frames));
    double casePeak=0,closeEnergy=0,rawEnergy=0,piezoEnergy=0,piezoError=0;
    for(int frame=0;frame<frames;++frame) {
        if(frame==fadeFrame) {
            // A finite physical strike makes the spectral phase oracle exact
            // without a truncation window. This also exercises the final
            // fading sample retaining its original key's source identity.
            MarembaEngineTestAccess::FadeVoices(*engine);physical.StartFade(.003);
        }
        std::array<std::array<float,8>,3> sub{};
        for(int s=0;s<factor;++s) {
            float close=0,room=0,piezo=0,ears[2]{};
            physical.ProcessSample(close,room,piezo,ears);
            sub[0][s]=ears[0];sub[1][s]=ears[1];sub[2][s]=piezo;
        }
        for(int channel=0;channel<3;++channel)
            raw[channel][frame]=decimator[channel].DownsampleFrame(sub[channel].data(),factor);
        engine->RenderBatch(&output[0][frame],&output[1][frame],&output[2][frame],
                            &output[3][frame],&output[4][frame],&output[5][frame],&output[6][frame],1);
        if((frame+1)%MarembaEngineTestAccess::ControlInterval()==0) {
            if(physical.IsActive()) physical.UpdateEnvelope();
            for(auto& filter:decimator) filter.FlushTiny();
        }
        for(auto& channel:output) {
            Require(std::isfinite(channel[frame]),"engine strike emits only finite samples");
            casePeak=std::max(casePeak,std::abs(static_cast<double>(channel[frame])));
        }
        rawEnergy+=static_cast<double>(raw[0][frame])*raw[0][frame]
                  +static_cast<double>(raw[1][frame])*raw[1][frame];
        closeEnergy+=static_cast<double>(output[2][frame])*output[2][frame]
                    +static_cast<double>(output[3][frame])*output[3][frame];
        const double expected=frame>=latency ? raw[2][frame-latency]*piezoGain : 0.0;
        piezoEnergy+=expected*expected;
        piezoError+=std::pow(output[6][frame]-expected,2);
    }
    Require(casePeak<.25,"quiet fixture leaves every output protection group at unity");
    Require(rawEnergy>1e-8 && closeEnergy>1e-12 && piezoEnergy>1e-16,"physical/acoustic/electrical paths are audible");
    Require(!physical.IsActive(),"finite reference strike finishes before analysis window ends");
    for(int channel=0;channel<3;++channel)
        for(int frame=frames-static_cast<int>(.010*rate);frame<frames;++frame)
            Require(raw[channel][frame]==0,"analysis contains the full raw strike and decimator tail");
    for(int channel=2;channel<=3;++channel)
        for(int frame=frames-static_cast<int>(.010*rate);frame<frames;++frame)
            Require(output[channel][frame]==0,"analysis contains the full Close propagation/filter tail");
    const double electricalRelative=piezoError/piezoEnergy;
    Require(electricalRelative<1e-12,"electrical output has only the declared common2ms delay after matching decimation");
    worstElectrical=std::max(worstElectrical,electricalRelative);
    maximumOutput=std::max(maximumOutput,casePeak);
    const auto position=CaptureSourcePosition(static_cast<EMarimbaModel>(model),note);
    const double distance=CaptureDistance(position,CloseHeadCentre());
    const auto& profile=kCloseHrtfProfiles[kCloseHrtfSourceProfile[CaptureSourceIndex(static_cast<EMarimbaModel>(model),note)]];
    double caseMagnitude=0,casePhase=0;
    int accepted=0;
    for(int ear=0;ear<2;++ear) {
        std::array<std::complex<double>,frequencies.size()> source{};
        double strongest=0;
        for(std::size_t i=0;i<frequencies.size();++i) {
            source[i]=Transform(raw[ear],frequencies[i],rate);
            strongest=std::max(strongest,std::abs(source[i]));
        }
        int earChecks=0;
        for(std::size_t i=0;i<frequencies.size();++i) {
            const double f=frequencies[i];
            // A transfer ratio at a near-zero source bin is ill-conditioned.
            // Keep bins above-80dB of that strike's strongest sampled source
            // bin, and stay inside the validated resampling passband.
            if(f>.35*rate || std::abs(source[i])<1e-4*strongest) continue;
            const double delay=distance/kCaptureSpeedOfSound
                +(ear ? .5 : -.5)*profile.residualItdSeconds+latency/rate;
            const auto h=Transform(profile.impulse[ear],kCloseHrtfTaps,f,kCloseHrtfReferenceRate);
            const auto expected=source[i]*h*(CloseCapture::kReferenceDistance/distance)
                *static_cast<double>(closeGain)*std::polar(1.0,-2*pi*f*delay);
            const auto actual=Transform(output[2+ear],f,rate);
            const auto ratio=actual/expected;
            const double db=std::abs(20*std::log10(std::abs(ratio)));
            const double phase=std::abs(std::arg(ratio));
            if(db>=.2 || phase>=.02)
                std::fprintf(stderr,"transfer ear%d frequency%.0f magnitude%.9fdB phase%.9frad\n",ear,f,db,phase);
            Require(db<.2,"real strike retains measured relative directional response and common distance scale");
            Require(phase<.02,"real strike includes geometric flight, residual ITD and2ms once without partition latency");
            caseMagnitude=std::max(caseMagnitude,db);casePhase=std::max(casePhase,phase);
            ++earChecks;++accepted;
        }
        Require(earChecks>=4,"enough energized frequency bins test each physical ear path");
    }
    worstMagnitude=std::max(worstMagnitude,caseMagnitude);worstPhase=std::max(worstPhase,casePhase);
    ++cases;
    std::printf("model%d note%d %.0fHz %dx bins%d closeMagnitude%.6fdB phase%.6frad piezoRelativeEnergyError%.3g peak%.6f\n",
                model,note,rate,factor,accepted,caseMagnitude,casePhase,electricalRelative,casePeak);
}
}

int main() {
    for(int model=0;model<3;++model) for(int note : {36,66,96})
        for(double rate : {44100.,48000.,88200.,96000.,192000.}) Run(model,note,rate,0);
    for(int quality : {1,2}) for(int model=0;model<3;++model) for(int note : {36,66,96}) Run(model,note,48000.,quality);
    std::printf("CloseEngineArrivalTest passed: %d cases, %d checks; worst%.6fdB/%.6frad; electricalRelativeEnergyError%.3g; maximumOutput%.6f\n",
                cases,checks,worstMagnitude,worstPhase,worstElectrical,maximumOutput);
}
