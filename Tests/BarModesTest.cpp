#include "BarModes.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Require(bool value, const char* text) {
    if (!value) { std::cerr << text << '\n'; std::exit(1); }
}
double UniformShape(int mode, double x) {
    constexpr double root[8] = {4.7300407448627,7.85320462409584,10.9956078380017,
        14.137165491257,17.278759657399,20.420352245626,23.56194490204,26.7035375555};
    constexpr double sigma[8] = {.982502214576238,1.00077731190727,.999966450125409,
        1.00000144989766,.999999937344383,1.0000000027076,.999999999882994,1.00000000000506};
    double sign=1.;
    if (x>.5) { x=1-x; sign=mode%2?-1.:1.; }
    const double z=root[mode]*x;
    const double raw=.5*((1-sigma[mode])*std::exp(z)+(1+sigma[mode])*std::exp(-z))
                     +std::cos(z)-sigma[mode]*std::sin(z);
    return sign*((mode/2)%2?-1.:1.)*-raw*(mode==0?.82260894024917:.70710678118655);
}
}

int main() {
    using namespace maremba;
    double maxUniform=0.,maxIntegralError=0.,maxParticipationError=0.,maxStrain=0.;
    for (int family=0;family<3;++family) {
        const auto model=static_cast<EMarimbaModel>(family);
        const auto cut=GetBarModeGeometry(model,60.);
        Require(std::abs(BarModeRatio(cut,1)-4.)<1e-7,"undercut second mode target");
        Require(std::abs(BarModeRatio(cut,2)-(10.-.2*family))<1e-7,"undercut third mode target");
        const auto uniform=GetBarModeGeometry(model,96.);
        Require(BarModeFundamentalNode(cut)<.21,"undercut must move fundamental nodes");
        Require(std::abs(BarModeFundamentalNode(uniform)-.2241575)<1e-6,"uniform node");
        for (int note=12;note<=120;++note) {
            const auto g=GetBarModeGeometry(model,note);
            Require(BarModeRatio(g,0)==1.,"fundamental stays exact");
            Require(std::abs(BarContactWeight(g,0,BarModeFundamentalNode(g),0.))<1e-10,"geometry node null");
            for (int mode=0;mode<8;++mode) {
                if (mode) Require(BarModeRatio(g,mode)>BarModeRatio(g,mode-1),"mode ordering");
                const double strain=BarModeStrainWeight(g,mode);
                Require(std::isfinite(strain),"finite strain");
                maxStrain=std::max(maxStrain,std::abs(strain));
                Require(mode!=0 || strain==1.,"pickup fundamental normalization");
                for (int j=0;j<=50;++j) {
                    const double x=j/50.;
                    const double a=BarContactWeight(g,mode,x,0.);
                    const double b=BarContactWeight(g,mode,1.-x,0.);
                    Require(std::isfinite(a)&&std::abs(a)<2.5,"finite bounded mass-normalized shape");
                    Require(std::abs(a-(mode%2?-b:b))<1e-12,"mode parity");
                    if (note==96) maxUniform=std::max(maxUniform,std::abs(a-UniformShape(mode,x)));
                }
            }
        }
        for (double note:{60.,76.125,80.25,86.75,92.875,96.}) {
            const auto g=GetBarModeGeometry(model,note);
            for (int mode=0;mode<8;++mode) {
                int zeroCount=0;
                double previous=BarContactWeight(g,mode,0.,0.);
                for (int i=1;i<=8192;++i) {
                    const double value=BarContactWeight(g,mode,(i-.13)/8192.,0.);
                    if (value*previous<0.) ++zeroCount;
                    previous=value;
                }
                Require(zeroCount==mode+2,"flexural mode node count prevents mode swaps");
                if (mode%2) Require(BarContactWeight(g,mode,.5,.15)==0.,"odd footprint centre null");
                for (double x:{0.,.02,.175,.2241575,.35,.5,.73,.99,1.}) {
                    for (double width:{.0001,.025,.10,.25}) {
                        const double lo=std::max(0.,x-width/2),hi=std::min(1.,x+width/2);
                        double sum=0.,square=0.;
                        constexpr int count=1024;
                        for (int i=0;i<count;++i) {
                            const double v=BarContactWeight(g,mode,lo+(hi-lo)*(i+.5)/count,0.);
                            sum+=v; square+=v*v;
                        }
                        const double average=BarContactWeight(g,mode,x,width);
                        const double participation=BarContactParticipation(g,mode,x,width);
                        maxIntegralError=std::max(maxIntegralError,std::abs(average-sum/count));
                        maxParticipationError=std::max(maxParticipationError,std::abs(participation-square/count));
                        Require(participation+1e-9>=average*average,"finite footprint obeys Jensen bound");
                    }
                }
            }
        }
    }
    Require(maxUniform<3e-5,"uniform limit matches analytical beam");
    Require(maxIntegralError<3e-6,"footprint integral agrees with dense integration");
    Require(maxParticipationError<3e-6,"contact loss integral agrees with dense integration");
    Require(maxStrain<150.,"pickup weights stay bounded across register");
    std::cout << "Bar modes PASS; uniform max error=" << maxUniform
              << ", footprint max error=" << maxIntegralError
              << ", participation max error=" << maxParticipationError
              << ", max strain=" << maxStrain << '\n';
}
