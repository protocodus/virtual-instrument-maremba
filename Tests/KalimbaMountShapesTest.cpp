// Independent quadrature/derivative checks of the mounted tine's excitation,
// pickup and radiation. Frequencies and mass/boundaries are FEM-checked by the
// generator; this tests the actual frozen coefficients and runtime lookup.
#include "KalimbaMountShapes.h"
#include "KalimbaModes.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace maremba;
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class Function> double integrate(Function f, double lo, double hi) {
    constexpr int n = 512;
    double sum = f(lo)+f(hi);
    for (int i=1;i<n;++i) sum += (i%2 ? 4 : 2)*f(lo+(hi-lo)*i/n);
    return sum*(hi-lo)/(3*n);
}
double raw(const KalimbaMountedMode& m, double x) {
    return m.contactScale*(m.ep*std::exp(m.beta*(x-1))+m.em*std::exp(-m.beta*x)
        +m.cosine*std::cos(m.beta*x)+m.sine*std::sin(m.beta*x));
}
// Obtain Gauss-Legendre roots independently with a recurrence/Newton solve.
double gaussNode(int point) {
    double z = std::cos(3.14159265358979323846*(point+.75)/(32+.5));
    for (int iteration=0;iteration<20;++iteration) {
        double p=1, previous=0;
        for (int n=1;n<=32;++n) {
            const double next=((2*n-1)*z*p-(n-1)*previous)/n;
            previous=p; p=next;
        }
        const double derivative=32*(z*p-previous)/(z*z-1);
        const double change=p/derivative;
        z-=change;
        if (std::abs(change)<1.e-15) break;
    }
    return .5*(1-z);
}
}

int main() {
    double worstFootprint=0, worstRadiation=0, worstCurvature=0;
    double worstContinuity=0, largestWeight=0, largestStrain=0, geometryChange=0, worstFrequency=0;
    for (int point=0;point<12;++point) {
        const auto& row=kKalimbaMountShapes[point];
        const auto shape=GetKalimbaMountShape(row.hz);
        const auto mount=GetKalimbaMount(row.hz);
        require(row.strainScale>0, "pickup fundamental polarity changed");
        for (int m=0;m<8;++m) {
            const auto& mode=row.modes[m];
            const double ratio=mode.beta*mode.beta/(row.modes[0].beta*row.modes[0].beta);
            worstFrequency=std::max(worstFrequency,std::abs(ratio/mount.ratio[m]-1));
            require(std::abs(ratio/mount.ratio[m]-1)<2.e-7,
                "shape frequency differs from mounted modal bank");
            require(raw(mode,1)*(m%2 ? -1 : 1)>0, "mode polarity flipped between keys");
            for (double x : {0.,.06,.3,.65,.825,.97,1.}) {
                for (double width : {0.01,.05,.13,.25}) {
                    const double lo=std::max(0.,x-width/2), hi=std::min(1.,x+width/2);
                    const double expected=integrate([&](double p) { return raw(mode,p); },lo,hi)/(hi-lo);
                    worstFootprint=std::max(worstFootprint,std::abs(
                        KalimbaMountedContactWeight(shape,m,x,width)-expected));
                }
            }
            for (int q=0;q<32;++q) {
                const double expected=raw(mode,gaussNode(q));
                worstRadiation=std::max(worstRadiation,
                    std::abs(KalimbaMountedRadiationNode(shape,m,q)-expected));
            }
            // Average curvature is slope(hi)-slope(lo), independent of the
            // runtime's closed-form spatial average. Fourth-order derivative.
            const auto slope=[&](double x) {
                constexpr double h=1.e-4;
                return (raw(mode,x-2*h)-8*raw(mode,x-h)+8*raw(mode,x+h)-raw(mode,x+2*h))/(12*h);
            };
            const double expected=row.strainScale*(slope(.0975)-slope(.0225))/.075;
            worstCurvature=std::max(worstCurvature,std::abs(
                KalimbaMountedStrainWeight(shape,m)-expected)/std::max(1.,std::abs(expected)));
        }
    }
    for (int i=0;i<161;++i) {
        const double hz=60*std::exp2(i/32.);
        const auto shape=GetKalimbaMountShape(hz);
        require(std::abs(KalimbaMountedContactWeight(shape,0,.825,0)-1)<2.e-8,
            "fundamental release normalization changed across register");
        require(std::abs(KalimbaMountedStrainWeight(shape,0)-.9174214950230173)<2.e-8,
            "fundamental pickup normalization changed across register");
        for (int m=0;m<8;++m) {
            for (double x : {0.,.3,.65,.75,.85,.95,1.}) {
                const double weight=KalimbaMountedContactWeight(shape,m,x,.05);
                const double participation=KalimbaMountedContactParticipation(shape,m,x,.05);
                require(std::isfinite(weight) && std::isfinite(participation) && participation>=0,
                    "non-finite or active contact damping coefficient");
                largestWeight=std::max(largestWeight,std::abs(weight));
                if (m==1) geometryChange=std::max(geometryChange,
                    std::abs(weight-KalimbaContactWeight(m,x,.05)));
            }
            largestStrain=std::max(largestStrain,std::abs(KalimbaMountedStrainWeight(shape,m)));
        }
    }
    for (int i=1;i<11;++i) {
        const double hz=kKalimbaMountShapes[i].hz;
        const auto left=GetKalimbaMountShape(hz*(1-1.e-8));
        const auto right=GetKalimbaMountShape(hz*(1+1.e-8));
        for (int m=0;m<8;++m) {
            const double a=KalimbaMountedContactWeight(left,m,.82,.055);
            const double b=KalimbaMountedContactWeight(right,m,.82,.055);
            worstContinuity=std::max(worstContinuity,std::abs(a-b));
        }
    }
    // Position cancellation must remain physically distinct from contact
    // damping: find an overtone node and span it with a thumb footprint.
    const auto c4=GetKalimbaMountShape(261.6255653005986);
    double lo=.5, hi=1;
    require(KalimbaMountedContactWeight(c4,1,lo,0)*KalimbaMountedContactWeight(c4,1,hi,0)<0,
        "first overtone no longer has a playing-span node");
    for (int i=0;i<50;++i) {
        const double mid=(lo+hi)/2;
        if (KalimbaMountedContactWeight(c4,1,lo,0)*KalimbaMountedContactWeight(c4,1,mid,0)>0) lo=mid;
        else hi=mid;
    }
    require(std::abs(KalimbaMountedContactWeight(c4,1,(lo+hi)/2,0))<1.e-12,
        "nodal pluck did not cancel");
    require(KalimbaMountedContactParticipation(c4,1,(lo+hi)/2,.08)>1.e-4,
        "finite thumb contact incorrectly disappears at a mode node");
    require(worstFootprint<1.e-8, "finite footprint disagrees with independent quadrature");
    require(worstRadiation<2.e-7, "cached radiation differs from mounted geometry");
    require(worstCurvature<2.e-8, "pickup differs from mounted curvature");
    require(worstContinuity<1.e-6, "interpolated shape jumps across a lookup key");
    require(largestWeight<2 && largestStrain<100, "new geometry produces an unbounded readout");
    require(geometryChange>.15, "mounted shape silently retains the rigid-bridge approximation");
    const auto low=GetKalimbaMountShape(1), high=GetKalimbaMountShape(1.e8);
    require(low.lower==0 && low.fraction==0 && high.upper==11 && high.fraction==1,
        "outside-range mount is extrapolated");
    const auto invalid=GetKalimbaMountShape(std::numeric_limits<double>::quiet_NaN());
    require(invalid.lower==0 && invalid.fraction==0, "invalid frequency did not select safe endpoint");
    std::cout << "MOUNT_SHAPES footprint=" << worstFootprint << " radiation=" << worstRadiation
        << " curvature=" << worstCurvature << " continuity=" << worstContinuity
        << " max_weight=" << largestWeight << " max_strain=" << largestStrain
        << " geometry_change=" << geometryChange << " frequency=" << worstFrequency << '\n';
    std::cout << "Kalimba mounted-shape contracts passed.\n";
}
