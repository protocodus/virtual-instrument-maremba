// Ballistic buzzers: independent landing/restitution oracle, passive settling,
// rate convergence and board lift-off threshold. No presets or compensating gain.
#include "BuzzerRattle.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double restitution=.35, preload=3000;
int failures=0;
void check(bool ok,const char* message) {
    if(!ok && failures++<15) std::cerr<<"FAIL "<<message<<'\n';
}

void isolatedRelease(double rate) {
    maremba::BuzzerRattle buzzer;
    buzzer.Configure(rate);buzzer.SetLooseness(1);buzzer.Reset();
    const double dt=1/rate;
    // A one-sample negative board-velocity step launches the resting buzzer.
    // The board then moves at constant velocity: only preload drives flight.
    const double launchAcceleration=.5*rate-preload;
    const double initialHeight=.5*launchAcceleration*dt*dt;
    const double initialVelocity=launchAcceleration*dt;
    const double flight=(initialVelocity+std::sqrt(initialVelocity*initialVelocity+2*preload*initialHeight))/preload;
    const double incoming=std::sqrt(initialVelocity*initialVelocity+2*preload*initialHeight);
    const double firstTime=dt+flight;
    const double secondTime=firstTime+2*restitution*incoming/preload;
    const double firstImpulse=(1+restitution)*incoming;
    const double totalImpulse=firstImpulse/(1-restitution);
    double sum=0,first=0,second=0;int firstFrame=-1,secondFrame=-1;
    for(int n=0;n<int(.02*rate);++n) {
        double impulse=0;
        const double out=buzzer.Process(-.5,1,impulse);
        check(std::isfinite(out)&&std::isfinite(impulse)&&impulse>=0,"invalid sound/landing impulse");
        if(impulse>0) {
            if(firstFrame<0) {firstFrame=n;first=impulse;}
            else if(secondFrame<0) {secondFrame=n;second=impulse;}
            sum+=impulse;
        }
        if(n>int(.002*rate)) check(impulse==0,"stationary board sustains settling chatter");
    }
    check(firstFrame>=0 && std::abs((firstFrame+1)*dt-firstTime)<=dt*1.000001,
          "first landing does not follow ballistic flight");
    check(secondFrame>firstFrame && std::abs((secondFrame+1)*dt-secondTime)<=dt*1.000001,
          "positive restitution rebound is discarded");
    check(std::abs(first-firstImpulse)<1e-11,"first landing impulse disagrees with restitution");
    check(std::abs(second-restitution*firstImpulse)<1e-11,"second landing impulse disagrees with restitution");
    // Several tiny final landings can occupy one sample. Their impulse sum
    // must approach the independent infinite geometric series. The eight-event
    // collapse discards at most restitution^8=.000225 of its remaining sum.
    check(std::abs(sum-totalImpulse)<=std::pow(restitution,8)*totalImpulse+1e-11,
          "settling loses more momentum than the eight-event bound");
    std::cout<<"RELEASE rate="<<rate<<" first_time="<<(firstFrame+1)*dt
             <<" second_time="<<(secondFrame+1)*dt<<" impulse_sum="<<sum
             <<" oracle_sum="<<totalImpulse<<'\n';
}

double gesture(double rate,double amplitude,bool& landed) {
    maremba::BuzzerRattle buzzer;buzzer.Configure(rate);buzzer.SetLooseness(.7);buzzer.Reset();
    double energy=0;
    for(int n=0;n<int(.2*rate);++n) {
        const double t=n/rate;
        // Smooth, fixed physical-time velocity gesture, independent of rate.
        const double env=t<.04?std::sin(3.141592653589793*t/.04)*std::sin(3.141592653589793*t/.04):0;
        const double drive=amplitude*env*std::sin(2*3.141592653589793*1100*t);
        double impact=0;const double y=buzzer.Process(drive,1,impact);
        check(std::isfinite(y)&&std::isfinite(impact),"band-limited gesture became non-finite");
        landed=landed||impact>0;energy+=y*y/rate;
        if((n&31)==31) buzzer.FlushTiny();
    }
    return energy;
}
}

int main() {
    for(double rate:{44100.,48000.,88200.,96000.,176400.,192000.,384000.}) isolatedRelease(rate);
    bool referenceActive=false;const double reference=gesture(768000,1.2,referenceActive);
    check(referenceActive&&reference>1e-12,"rate-reference gesture never excites buzzers");
    for(double rate:{44100.,48000.,88200.,96000.,176400.,192000.}) {
        bool soft=false,loud=false;
        const double quiet=gesture(rate,.25,soft),active=gesture(rate,1.2,loud);
        check(!soft&&quiet==0,"below-preload board motion lifts a buzzer");
        check(loud&&active>1e-12,"above-preload gesture does not rattle");
        const double error=std::abs(10*std::log10(active/reference));
        check(error<1,"landing/ring energy changes substantially with rate");
        std::cout<<"RATE rate="<<rate<<" level_error_db="<<error<<'\n';
    }
    std::cout<<"BuzzerRattlePhysicsTest "<<(failures?"FAILED":"PASSED")<<" failures="<<failures<<'\n';
    return failures?1:0;
}
