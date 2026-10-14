// Numerical joins of the empirical palm/end-correction approximation must be
// continuous; this test does not claim a measured hand/box calibration.
#include "KalimbaBody.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
int failures=0;
void check(bool ok,const char* message) {
    if(!ok&&failures++<12)std::cerr<<"FAIL "<<message<<'\n';
}
double coverForEd(double ed) {
    const double height=.015*std::pow(.61*std::pow(4.,.18)/ed,1/.39);
    return std::log(height/.040)/std::log(.001/.040);
}
std::array<double,3> values(maremba::HoleCover h) {return{h.frequency,h.radiation,h.loss};}
maremba::HoleCover legacy(double cover) {
    const double c=std::clamp(cover,0.,1.);
    if(c<=0)return{1,1,1};
    const double height=.040*std::pow(.001/.040,c),open=.005+2*.61*.015;
    const double ed=.61*std::pow(4.,.18)*std::pow(.015/height,.39);
    double added=ed>=.65?(ed-.61)*.015:0;
    const double gap=.015*.015/(2*height)*std::log(4.);
    added=std::max(added,gap*std::clamp((.005-height)/.004,0.,1.));
    const double front=open/(open+added),total=2./3*front+(1-2./3);
    return{std::sqrt(total),2./3*front*front+(1-2./3),.4+.6*total};
}
}

int main() {
    const double lower=coverForEd(.61),upper=coverForEd(.65);
    for(double edge:{lower,upper}) {
        constexpr double eps=1e-8,step=1e-5;
        const auto below=values(maremba::KalimbaHoleCover(edge-eps));
        const auto above=values(maremba::KalimbaHoleCover(edge+eps));
        const auto a=values(maremba::KalimbaHoleCover(edge-step));
        const auto b=values(maremba::KalimbaHoleCover(edge));
        const auto c=values(maremba::KalimbaHoleCover(edge+step));
        for(int i=0;i<3;++i) {
            check(std::abs(above[i]-below[i])<1e-7,"hole-cover coefficient jumps at a transition edge");
            const double left=(b[i]-a[i])/step,right=(c[i]-b[i])/step;
            check(std::abs(right-left)<.001,"hole-cover slope jumps at a transition edge");
        }
        std::cout<<"JOIN cover="<<edge<<" frequency_delta="<<above[0]-below[0]
                 <<" radiation_delta="<<above[1]-below[1]<<" loss_delta="<<above[2]-below[2]<<'\n';
    }
    auto previous=values(maremba::KalimbaHoleCover(0));
    int unchanged=0;
    for(int n=-1000;n<=11000;++n) {
        const double cover=n/10000.;const auto current=values(maremba::KalimbaHoleCover(cover));
        for(double value:current)check(std::isfinite(value)&&value>0&&value<=1,"invalid hole-cover coefficient");
        if(n>=0) {
            for(int i=0;i<3;++i)check(current[i]<=previous[i]+1e-14,"cover sweep reverses its physical direction");
            previous=current;
        }
        if(cover<lower || cover>upper) {
            const auto old=values(legacy(cover));
            for(int i=0;i<3;++i)check(std::memcmp(&current[i],&old[i],sizeof(double))==0,
                                     "coefficient outside the transition changed");
            ++unchanged;
        }
    }
    std::cout<<"SWEEP points=12001 unchanged_region_points="<<unchanged<<'\n';
    std::cout<<"KalimbaHoleCoverContinuityTest "<<(failures?"FAILED":"PASSED")<<" failures="<<failures<<'\n';
    return failures?1:0;
}
