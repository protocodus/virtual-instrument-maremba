#define main UnusedOriginalStrikeVarianceMain
#include "StrikeVarianceTest.cpp"
#undef main
#include <iomanip>

double WindowResidual(const std::vector<float>& a,const std::vector<float>& b,int channels,double scale) {
    double aa=0,ab=0,bb=0;
    for(size_t i=static_cast<size_t>(48000*.04*scale*channels);i<static_cast<size_t>(48000*.16*scale*channels);++i) {
        aa+=double(a[i])*a[i];ab+=double(a[i])*b[i];bb+=double(b[i])*b[i];
    }
    return aa>0&&bb>0?std::sqrt(std::max(0.,1-ab*ab/(aa*bb))):0;
}
double Median(const Hits& hits,int channels,double scale) {
    std::vector<double> values;
    for(size_t h=1;h<hits.size();++h)values.push_back(WindowResidual(hits[h-1],hits[h],channels,scale));
    std::sort(values.begin(),values.end());return values[values.size()/2];
}
int main() {
    std::cout<<std::setprecision(17);
    maremba::EngineParameters p;
    p.model=3;p.malletHardness=.5f;p.strikePosition=.45f;p.decay=.4f;
    p.artifacts=p.detune=p.buzzAmount=p.sympathetic=p.bodyBloom=0;
    p.farLevel=p.piezoLevel=p.preampDrive=p.compAmount=p.warmth=0;
    p.velocityToHardness=p.velocityToPosition=p.velocityToDecay=0;
    maremba::MarembaEngine engine(48000);
    for(float amount:{0.f,5.f}) {
        p.strikeJitter=amount;engine.SetParameters(p);engine.Reset();
        const auto hits=render(engine);
        check(hits.physical.size()==48&&hits.closeStereo.size()==48,"probe lost48-hit sequence");
        for(double scale:{1.,.7}) {
            Hits physicalGain,stereoGain;
            for(size_t h=0;h<48;++h) {
                physicalGain.push_back(hits.physical.front());stereoGain.push_back(hits.closeStereo.front());
                const float gain=.8f+.4f*static_cast<float>(h)/47;
                for(float& v:physicalGain.back())v*=gain;
                for(float& v:stereoGain.back())v*=gain;
            }
            const double physical=Median(hits.physical,1,scale),stereo=Median(hits.closeStereo,2,scale);
            const double physicalNegative=Median(physicalGain,1,scale),stereoNegative=Median(stereoGain,2,scale);
            check(physicalNegative<1.e-6&&stereoNegative<1.e-6,"gain-only control was mistaken for timbre");
            std::cout<<"amount="<<amount<<",scale="<<scale<<",physical_median="<<physical<<",stereo_median="<<stereo
                     <<",gain_only_physical="<<physicalNegative<<",gain_only_stereo="<<stereoNegative
                     <<",begin_frame="<<static_cast<size_t>(48000*.04*scale)<<",end_frame="<<static_cast<size_t>(48000*.16*scale)<<'\n'<<std::flush;
        }
    }
    std::cout<<"failures="<<failures<<",drained_hits="<<drainedHits<<",longest_voice_damp_seconds="<<longestVoiceDampFrames/48000.
             <<",longest_engine_drain_seconds="<<longestEngineDrainFrames/48000.<<'\n';
    return failures?1:0;
}
