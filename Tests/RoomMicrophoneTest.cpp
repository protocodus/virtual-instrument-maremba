// Independent physical transfer, impulse and lifecycle contract for the
// declared pressure condenser. No instrument/limiter/room can hide its result.
#include "RoomMicrophone.h"
#include <complex>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
namespace { bool counting=false;size_t allocations=0; }
void* operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace {
using namespace maremba;using Complex=std::complex<double>;int checks=0;
void require(bool b,const char*m){++checks;if(!b){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
// Direct evaluation of the continuous diaphragm+cavity ODE and circuit. It
// does not use the implementation's section coefficients or pole factorization.
Complex analog(double hz){
    const Complex s(0,kTwoPiD*hz);
    constexpr double Cd=.314e-12,Cc=2.83e-12,L=896.,R=56.2e6,Rv=36.e9;
    const Complex diaphragm=Rv*Cc*s/((L*s*s+R*s+1./Cd)*(1.+Rv*Cc*s)+Rv*s);
    const Complex input=(17./20.)*s/(s+1./(1.e9*20.e-12));
    const Complex output=(20000./20150.)*s/(s+1./(20150.*1.e-6));
    return diaphragm*input*output;
}
Complex dft(const std::vector<float>&h,double rate,double hz){Complex sum=0;const Complex z=std::exp(Complex(0,-kTwoPiD*hz/rate));Complex phase=1;for(float x:h){sum+=static_cast<double>(x)*phase;phase*=z;}return sum;}
}
int main(){
    double worstMagnitude=0,worstPhase=0;
    std::array<Complex,11> rateReference{};
    for(double rate:{44100.,48000.,88200.,96000.,192000.}){
        RoomPressureMicrophone mic;mic.SetSampleRate(rate);
        for(int i=0;i<64;++i)require(mic.Process(0)==0,"microphone generates a precursor or noise");
        std::vector<float> h(static_cast<size_t>(rate*2));
        for(size_t i=0;i<h.size();++i){h[i]=mic.Process(i==0?1.f:0.f);require(std::isfinite(h[i]),"microphone impulse is nonfinite");}
        require(h[0]!=0,"unreported pure capsule delay");
        require(std::abs(std::abs(dft(h,rate,1000.))-1)<2.e-6,"fixed1kHz sensitivity differs from unity");
        int frequencyIndex=0;
        for(double hz:{1.,4.,10.,50.,125.,500.,1000.,2000.,4000.,6000.,10000.}){
            const Complex actual=dft(h,rate,hz),expected=analog(hz)/std::abs(analog(1000.));
            const double db=20*std::log10(std::abs(actual/expected));
            const double phase=std::arg(actual/expected);
            if(rate==44100.)rateReference[static_cast<size_t>(frequencyIndex)]=actual;
            else {const auto delta=actual/rateReference[static_cast<size_t>(frequencyIndex)];require(std::abs(20*std::log10(std::abs(delta)))<.2,"microphone sample-rate magnitude consistency");require(std::abs(std::arg(delta))<.13,"microphone sample-rate phase consistency");}
            ++frequencyIndex;
            worstMagnitude=std::max(worstMagnitude,std::abs(db));worstPhase=std::max(worstPhase,std::abs(phase));
            // Prewarped Tustin mechanics preserve the10kHz resonance; the
            // native-rate approximation has bounded warping elsewhere.
            if(std::abs(db)>=.65||std::abs(phase)>=.13)std::cerr<<"MIC_RESPONSE rate="<<rate<<" hz="<<hz<<" db="<<db<<" phase="<<phase<<'\n';
            require(std::abs(db)<.65,"capsule magnitude disagrees with physical ODE/circuit");
            require(std::abs(phase)<.13,"capsule phase disagrees with physical ODE/circuit");
        }
        require(std::abs(dft(h,rate,.1))<.0001,"capsule/electronic charge leakage does not rejectDC");
        const auto bass=dft(h,rate,20.);require(std::abs(bass)>.5,"coupling removes excessive musical bass");
        mic.Reset();for(size_t i=0;i<h.size();++i)require(mic.Process(i==0?1.f:0.f)==h[i],"microphone reset is not deterministic");
        mic.Process(.2f);mic.SetSampleRate(rate);for(int i=0;i<64;++i)require(mic.Process(0)==0,"sample-rate change retains capsule charge");
        mic.Process(1.f);for(int i=0;i<static_cast<int>(rate*8);++i)mic.Process(0);
        mic.FlushTiny();require(mic.HasNoSubnormalState()&&mic.Process(0)==0,"capsule tail does not drain or has subnormal states");
        allocations=0;counting=true;mic.SetSampleRate(rate);mic.Reset();for(int i=0;i<4096;++i)mic.Process(i==0?1.f:0.f);mic.FlushTiny();counting=false;
        require(allocations==0,"microphone allocates on realtime paths");
    }
    // Pressure receiver is linear, including complete mechanical/electrical tails.
    RoomPressureMicrophone a,b,c;double error=0,energy=0;
    for(int i=0;i<96000;++i){const float x=i<4800?static_cast<float>(.1*std::sin(i*.13)):0;const float y=i<2400?static_cast<float>(.2*std::cos(i*.37)):0;const double expected=.3*a.Process(x)-.7*b.Process(y),actual=c.Process(.3f*x-.7f*y);error+=(actual-expected)*(actual-expected);energy+=expected*expected;}
    require(error<1.e-12*energy,"microphone does not preserve pressure superposition");
    std::cout<<"ROOM_MICROPHONE max_analog_magnitude_error_db="<<worstMagnitude<<" max_analog_phase_error_radians="<<worstPhase<<'\n';
    std::cout<<"RoomMicrophoneTest PASSED checks="<<checks<<'\n';
}
