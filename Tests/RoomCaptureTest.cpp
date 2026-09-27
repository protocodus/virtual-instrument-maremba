// Independent arrival, propagation, room-energy and lifecycle contracts for the
// declared synthetic microphone scene. No limiter hides the capture response.
#include "RoomCapture.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

namespace maremba {
struct RoomCaptureTestAccess {
    static int HistorySpan(const RoomCapture& room){return room.mSpan;}
    static void Pressure(RoomCapture& room,const float* input,int count,float& left,float& right){room.ProcessPressure(input,count,left,right);}
};
}
namespace {
bool countAllocations=false;
size_t allocations=0;
}
void* operator new(std::size_t size) {
    if(countAllocations)++allocations;
    if(void* p=std::malloc(std::max(size,size_t{1})))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

namespace {
using namespace maremba;
using Stereo=std::array<std::vector<float>,2>;
int checks=0;
void require(bool condition,const char* message){++checks;if(!condition){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
std::unique_ptr<RoomCapture> makeRoom(int type,double rate,int capacity=2){
    auto room=std::make_unique<RoomCapture>();room->Allocate(rate,capacity);room->SetSampleRate(rate);room->SetType(type);return room;
}
Stereo impulse(RoomCapture& room,double seconds,const std::array<float,2>& excitation={1,0}){
    Stereo result;const int frames=static_cast<int>(std::ceil(room.SampleRate()*seconds));
    for(auto& c:result)c.resize(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i){float x[2]={i==0?excitation[0]:0,i==0?excitation[1]:0};RoomCaptureTestAccess::Pressure(room,x,2,result[0][static_cast<size_t>(i)],result[1][static_cast<size_t>(i)]);}
    return result;
}
void fractionalTiming(){
    // The isolated causal allpass must preserve energy and implement its
    // analytically specified phase, including endpoints and reset.
    for(double fraction:{0.,.001,.13,.5,.93,1.}){
        RoomFractionalDelay delay;delay.SetFraction(fraction);
        for(int i=0;i<32;++i)require(delay.Process(0)==0,"fractional delay produces a precursor");
        std::vector<float> h(65536);
        double energy=0,dc=0,moment=0;
        for(size_t i=0;i<h.size();++i){h[i]=delay.Process(i==0?1.f:0.f);energy+=h[i]*h[i];dc+=h[i];moment+=i*h[i];}
        require(std::abs(energy-1)<2.e-6,"fractional allpass changes impulse energy");
        require(std::abs(dc-1)<1.e-5&&std::abs(moment-fraction)<3.e-5,"fractional DC delay differs from requested duration");
        for(double w:{.01,.15,.5,1.2,2.5}){
            std::complex<double> measured=0;
            for(size_t i=0;i<h.size();++i)measured+=static_cast<double>(h[i])*std::exp(std::complex<double>(0,-w*i));
            const double a=(1-fraction)/(1+fraction);const auto z=std::exp(std::complex<double>(0,-w));
            require(std::abs(measured-(a+z)/(1.+a*z))<2.e-6,"fractional allpass differs from independent transfer equation");
        }
        delay.Reset();for(int i=0;i<64;++i)require(delay.Process(i==0?1.f:0.f)==h[static_cast<size_t>(i)],"fractional reset changes phase state");
        delay.SetFraction(0);require(delay.Process(.31f)==.31f&&delay.Process(0)==0,"rate/geometry change retains fractional state");
    }
    // This coherent note exposed a .69dB engine error with integer-rounded
    // FDN loops. All rooms now retain the declared continuous loop durations.
    for(int type=0;type<3;++type)for(double hz:{261.625565,1046.502261}){
        double reference[2]={};
        for(double rate:{44100.,48000.,88200.,96000.,192000.}){
            auto room=makeRoom(type,rate,1);require(room->AddSource(0,CaptureSourcePosition(static_cast<EMarimbaModel>(0),84)),"rate source setup");
            double energy[2]={};
            for(int i=0;i<static_cast<int>(rate*.5);++i){const double t=i/rate;const float x=static_cast<float>(.001*std::sin(kTwoPiD*hz*t)*std::exp(-t/.1));float l,r;RoomCaptureTestAccess::Pressure(*room,&x,1,l,r);energy[0]+=l*l/rate;energy[1]+=r*r/rate;}
            for(int c=0;c<2;++c){if(rate==44100)reference[c]=energy[c];else require(std::abs(10*std::log10(energy[c]/reference[c]))<.5,"coherent Room tone level changes beyond .5dB with sample rate");}
        }
    }
}
void geometry(){
    // Independently evaluated path lengths/c for source(.2,-.15,.04), both
    // microphones and the six signed walls, in direct,-X,+X,-Y,+Y,-Z,+Z order.
    constexpr double expected[3][2][7]={
      {{.00690666800389029,.0184785609817414,.0190225360917384,.0155784737902674,.0113110892038657,.0107184399237134,.0110002206077841},
       {.00675737349681121,.0201170617266328,.01739821414644,.015512861737716,.0112205513517497,.0106228521620936,.0109071025978196}},
      {{.00690666800389029,.0240011113309323,.0245612071431192,.0184509412438948,.0170128796584169,.0107184399237134,.0129637987105913},
       {.00675737349681121,.0256844693362074,.0228843660398437,.0183955773610154,.0169528200583625,.0106228521620936,.0128848791483729}},
      {{.00690666800389029,.0526214515308808,.0531997758588652,.0288632829738715,.0413554667543312,.0107184399237134,.0328808304493867},
       {.00675737349681121,.0543567300176103,.0514651255426825,.0288279228819773,.0413307955903106,.0106228521620936,.0328497952002864}}};
    for(int type=0;type<3;++type)for(int channel=0;channel<2;++channel){
        const auto paths=RoomCapture::Geometry(type,{.2,-.15,.04},channel);
        for(int p=0;p<7;++p){
            require(std::abs(paths[static_cast<size_t>(p)].seconds-expected[type][channel][p])<1.e-14,"image delay differs from independently evaluated geometry");
            require(std::abs(paths[static_cast<size_t>(p)].gain*paths[static_cast<size_t>(p)].distance-1)<1.e-14,"pressure attenuation is not1/r");
            if(p)require(paths[static_cast<size_t>(p)].seconds>paths[0].seconds,"reflection precedes direct path");
        }
    }
    auto room=makeRoom(0,192000,kCaptureSourceCount);
    for(int id=0;id<kCaptureSourceCount;++id)require(room->AddSource(id,CaptureSourcePosition(id)),"declared source exceeds allocated geometry envelope");
    require(room->AllocatedSamples()*sizeof(float)<40*1024*1024,"source history unexpectedly exceeds40MiB at192k");
    require(!room->AddSource(kCaptureSourceCount,{0,0,0}),"invalid source slot accepted");
    require(!room->AddSource(0,{100,100,100}),"unrepresentable position silently truncates delay");
    std::cout<<"ROOM_GEOMETRY catalog="<<kCaptureSourceCount<<" history_bytes="<<room->AllocatedSamples()*sizeof(float)<<" maximum_seconds="<<room->MaximumHistorySeconds()<<'\n';
}
void arrivals(){
    for(double rate:{44100.,48000.,88200.,96000.,192000.})for(int type=0;type<3;++type){
        auto room=makeRoom(type,rate);require(room->AddSource(0,{.2,-.15,.04}),"source setup failed");
        const auto h=impulse(*room,.03);
        for(int channel=0;channel<2;++channel){
            const auto p=RoomCapture::Geometry(type,{.2,-.15,.04},channel);
            const int direct=static_cast<int>(std::floor(p[0].seconds*rate));
            double firstReflection=1;
            for(int k=1;k<7;++k)firstReflection=std::min(firstReflection,p[static_cast<size_t>(k)].seconds);
            const int early=static_cast<int>(std::floor(firstReflection*rate));
            for(int i=0;i<direct;++i)require(h[static_cast<size_t>(channel)][static_cast<size_t>(i)]==0,"Room emits before direct wave arrival");
            double sum=0,centroid=0;
            for(int i=direct;i<early;++i){const double x=h[static_cast<size_t>(channel)][static_cast<size_t>(i)];sum+=x;centroid+=i*x;}
            require(std::abs(sum-p[0].gain)<2.e-7,"direct wave has an extra wet/dry scaling or wrong attenuation");
            require(std::abs(centroid/sum-p[0].seconds*rate)<2.e-6,"fractional direct delay is inaccurate");
            for(int i=direct+2;i<early;++i)require(h[static_cast<size_t>(channel)][static_cast<size_t>(i)]==0,"unmodelled early signal before first wall reflection");
            require(std::abs(h[static_cast<size_t>(channel)][static_cast<size_t>(early)])+std::abs(h[static_cast<size_t>(channel)][static_cast<size_t>(early+1)])>1.e-6,"first image did not arrive");
        }
        room->Reset();require(h==impulse(*room,.03),"Reset fails to replay room impulse exactly");
        require(room->HasNoSubnormalState(),"subnormal capture state");
    }
    // Double the radial distance from the left microphone inside the Hall.
    // Integrated isolated direct impulses avoid fractional-delay peak bias.
    double level[2]={},time[2]={};
    for(int n=0;n<2;++n){
        const CapturePoint p=n?CapturePoint{0,0,0}:CapturePoint{-.15,1.,.45};
        auto room=makeRoom(2,96000);require(room->AddSource(0,p),"distance source does not fit");
        auto h=impulse(*room,.025);const auto path=RoomCapture::Geometry(2,p,0)[0];
        const int i=static_cast<int>(std::floor(path.seconds*96000));
        level[n]=h[0][static_cast<size_t>(i)]+h[0][static_cast<size_t>(i+1)];time[n]=path.seconds;
    }
    require(std::abs(20*std::log10(level[1]/level[0])+6.02059991328)<1.e-5,"doubling distance is not minus6.02dB");
    require(std::abs(time[1]-2*time[0])<1.e-14,"doubling distance does not double acoustic flight time");
}
void linearAndSpatial(){
    constexpr double rate=48000;
    for(int type=0;type<3;++type){
        auto room=makeRoom(type,rate);require(room->AddSource(0,{-.55,0,0})&&room->AddSource(1,{.55,0,0}),"stereo source setup");
        const auto a=impulse(*room,.6,{1,0});room->Reset();const auto b=impulse(*room,.6,{0,1});room->Reset();const auto sum=impulse(*room,.6,{.3f,-.7f});
        double error=0,energy=0,side=0;
        for(int c=0;c<2;++c)for(size_t i=0;i<sum[0].size();++i){
            const double expected=.3*a[static_cast<size_t>(c)][i]-.7*b[static_cast<size_t>(c)][i];
            const double d=sum[static_cast<size_t>(c)][i]-expected;error+=d*d;energy+=expected*expected;
            require(std::isfinite(sum[static_cast<size_t>(c)][i]),"nonfinite linear room response");
            if(i<static_cast<size_t>(.020*rate))require(std::abs(a[static_cast<size_t>(c)][i]-b[static_cast<size_t>(1-c)][i])<1.e-7,"mirror sources fail early stereo reciprocity");
        }
        for(size_t i=static_cast<size_t>(.04*rate);i<sum[0].size();++i){const double d=a[0][i]-b[0][i];side+=d*d;}
        require(error<1.e-10*energy,"per-source response is not linear superposition");
        require(side>1.e-7,"separated source positions disappear from late response");
    }
}
// A source can go dormant while the room's reflected/microphone tail keeps
// ringing. Reusing that slot must equal two independent impulse responses;
// output silence alone cannot reveal a stale sample in the circular history.
void dormantSourceSuperposition(){
    double worst=0;
    int cases=0;
    for(double rate:{44100.,48000.,88200.,96000.,192000.})for(int type=0;type<3;++type){
        auto prototype=makeRoom(type,rate,1);
        require(prototype->AddSource(0,CaptureSourcePosition(60)),"dormant source setup");
        const int span=RoomCaptureTestAccess::HistorySpan(*prototype);
        for(int gap:{0,1,17,span/2,span-1}){
            RoomCapture combined=*prototype,first=*prototype,second=*prototype;
            const int onset=span+gap;
            double peakError=0;
            for(int frame=0;frame<onset+2*span;++frame){
                const float a=frame==0?1.f:0.f,b=frame==onset?.2f:0.f,x=a+b;
                float al,ar,bl,br,l,r;
                combined.Process(&x,1,l,r);
                first.Process(&a,1,al,ar);
                second.Process(&b,1,bl,br);
                // Independent linearity oracle includes all original tails,
                // wall filters and microphone state; none are reset at reuse.
                const double error=std::max(std::abs(static_cast<double>(l)-al-bl),
                                            std::abs(static_cast<double>(r)-ar-br));
                require(std::isfinite(error),"nonfinite reactivated room source");
                peakError=std::max(peakError,error);
            }
            require(peakError<2.e-6,"dormant room source replays stale history on reactivation");
            worst=std::max(worst,peakError);++cases;
        }
    }
    std::cout<<"ROOM_DORMANT_SUPERPOSITION cases="<<cases<<" maximum_error="<<worst<<'\n';
}
struct BandPass{
    double b0,b2,a1,a2,s1=0,s2=0;
    BandPass(double fs,double hz){const double w=kTwoPiD*hz/fs,alpha=std::sin(w)/(2*4.32),a0=1+alpha;b0=alpha/a0;b2=-b0;a1=-2*std::cos(w)/a0;a2=(1-alpha)/a0;}
    double process(double x){const double y=b0*x+s1;s1=-a1*y+s2;s2=b2*x-a2*y;return y;}
};
double decay(const Stereo& h,double rate,double frequency){
    std::vector<double> e(h[0].size(),0);
    for(const auto& c:h){BandPass a(rate,frequency),b(rate,frequency);for(size_t i=0;i<c.size();++i){const double y=b.process(a.process(c[i]));e[i]+=y*y;}}
    for(size_t i=e.size()-1;i>0;--i)e[i-1]+=e[i];
    const size_t start=static_cast<size_t>(.1*rate);const double reference=e[start];
    double sx=0,sy=0,sxx=0,sxy=0,n=0;
    for(size_t i=start;i<e.size();++i){const double y=10*std::log10(std::max(e[i]/reference,1.e-30));if(y>-5)continue;if(y< -30)break;const double x=i/rate;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;n+=1;}
    return n>10?-60/((n*sxy-sx*sy)/(n*sxx-sx*sx)):0;
}
void roomEnergy(){
    for(double rate:{44100.,96000.})for(int type=0;type<3;++type){
        auto room=makeRoom(type,rate);require(room->AddSource(0,{0,0,0}),"decay source setup");
        const auto h=impulse(*room,4.0);const auto target=RoomCapture::Decay(type,rate);
        const double measured=decay(h,rate,500.);
        require(std::abs(measured/target.t500-1)<.20,"diffuse decay disagrees with shared wall absorption");
        double direct=0,late=0;
        for(const auto& channel:h)for(size_t i=0;i<channel.size();++i){const double e=static_cast<double>(channel[i])*channel[i];if(i<static_cast<size_t>(.009*rate))direct+=e;else if(i>static_cast<size_t>(.04*rate))late+=e;}
        require(direct>0&&late>0&&late/direct<100,"invalid or excessive diffuse energy");
        require(target.t4k<std::min(target.t125,target.t500)&&target.t10k<target.t4k,"wall absorption does not shorten high-frequency decay");
        std::cout<<"ROOM_ENERGY type="<<type<<" rate="<<rate<<" expected_t500="<<target.t500<<" measured_t500="<<measured<<" late_over_direct_db="<<10*std::log10(late/direct)<<'\n';
        room->FlushTiny();require(room->HasNoSubnormalState(),"tail subnormal after flush");
    }
}
void microphoneIntegration(){
    for(double rate:{44100.,48000.,88200.,96000.,192000.})for(int type=0;type<3;++type){
        auto raw=makeRoom(type,rate,2),captured=makeRoom(type,rate,2);
        for(int id=0;id<2;++id){const CapturePoint p{id?.6:-.3,0,0};require(raw->AddSource(id,p)&&captured->AddSource(id,p),"capsule source setup");}
        RoomPressureMicrophone expectedL,expectedR;expectedL.SetSampleRate(rate);expectedR.SetSampleRate(rate);
        for(int i=0;i<static_cast<int>(rate*.3);++i){
            const float x[2]={i==0?.7f:0.f,i<static_cast<int>(.03*rate)?static_cast<float>(.04*std::sin(i*.12)):0.f};
            float pl,pr,l,r;RoomCaptureTestAccess::Pressure(*raw,x,2,pl,pr);captured->Process(x,2,l,r);
            require(l==expectedL.Process(pl)&&r==expectedR.Process(pr),"capsule not applied once after complete room pressure");
        }
        captured->Reset();for(int i=0;i<256;++i){float l,r;captured->Process(nullptr,0,l,r);require(l==0&&r==0,"room reset retains microphone tail");}
    }
}
void realtime(){
    auto room=makeRoom(0,192000,4);for(int id=0;id<4;++id)require(room->AddSource(id,{id*.15,0,0}),"realtime source setup");
    allocations=0;countAllocations=true;
    for(double rate:{44100.,48000.,96000.,192000.}){
        room->SetSampleRate(rate);
        for(int i=0;i<static_cast<int>(rate*.2);++i){if(i==static_cast<int>(rate*.05))room->SetType(1);if(i==static_cast<int>(rate*.07))room->SetType(2);
            const float x[4]={i==0?1.f:0.f,i<200?.01f:0.f,0,0};float l,r;room->Process(x,4,l,r);require(std::isfinite(l)&&std::isfinite(r),"room switch produced nonfinite output");}
        room->FlushTiny();room->Reset();
        for(int i=0;i<200;++i){float l,r;room->Process(nullptr,0,l,r);require(l==0&&r==0,"Reset retains room audio");}
    }
    countAllocations=false;require(allocations==0,"room allocates on realtime paths");
}
}
int main(){fractionalTiming();geometry();arrivals();linearAndSpatial();dormantSourceSuperposition();roomEnergy();microphoneIntegration();realtime();std::cout<<"RoomCaptureTest PASSED checks="<<checks<<'\n';}
