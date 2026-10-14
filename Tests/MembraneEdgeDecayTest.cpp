// The membrane-edge envelope has one physical-time decay at every internal
// rate. Seed an actual voice's isolated edge state; derive the oracle directly
// from the declared 88.2-kHz pole, without reading cached DSP coefficients.
#include "MarembaVoice.h"
#include <cmath>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void Seed(MarembaVoice& v) { v.mSlapNoise=1;v.mSlapY1=v.mSlapY2=0; }
    static double Envelope(const MarembaVoice& v) { return v.mSlapNoise; }
};
}

int main() {
    int failures=0;
    for(int model:{0,1,2})for(double rate:{44100.,48000.,88200.,96000.,176400.,192000.,384000.,768000.}) {
        maremba::MarembaVoice voice;maremba::FastPRNG random(123u);
        voice.Trigger(60,.85f,static_cast<maremba::EMarimbaModel>(model),.65f,.25f,
                      0,1,1,0,0,0,rate,random,0,0,2,0);
        maremba::MarembaVoiceTestAccess::Seed(voice);
        const int frames=int(.005*rate);
        for(int n=0;n<frames;++n) {
            float c,f,p;voice.ProcessSample(c,f,p);
            if(!std::isfinite(c)||!std::isfinite(f)||!std::isfinite(p))++failures;
        }
        const double actual=maremba::MarembaVoiceTestAccess::Envelope(voice);
        const double expected=std::exp(std::log(.995)*88200*frames/rate);
        const double error=std::abs(actual-expected);
        if(error>1e-12) {++failures;std::cerr<<"FAIL edge decay follows sample count model="<<model<<" rate="<<rate<<'\n';}
        std::cout<<"EDGE model="<<model<<" rate="<<rate<<" actual="<<actual
                 <<" physical_time_oracle="<<expected<<" error="<<error<<'\n';
    }
    std::cout<<"MembraneEdgeDecayTest "<<(failures?"FAILED":"PASSED")<<" failures="<<failures<<'\n';
    return failures?1:0;
}
