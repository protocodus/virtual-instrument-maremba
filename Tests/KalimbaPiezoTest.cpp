// Independent SI soundboard/pickup checks. A single board sensor follows
// mechanical bridge force; nominal per-tine and acoustic feeds cannot bypass it.
#include "MarembaEngine.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void ChargeLegacy(MarembaVoice& v) { v.mPickup.Process(100., 50., 20.); }
};
struct MarembaEngineTestAccess {
    static void PoisonLegacy(MarembaEngine& e) {
        for (auto& v : e.mVoices) if (v.IsActive()) MarembaVoiceTestAccess::ChargeLegacy(v);
    }
    static double SharedEnergy(const MarembaEngine& e) { return e.mKalimbaPickup.StateEnergy(); }
};
}
namespace {
using namespace maremba;
using C = std::complex<double>;
int checks = 0;
void require(bool ok, const char* text) {
    ++checks;
    if (!ok) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}
// Numerical electrode integral, independent of production's sinc expression.
double strainCoefficient(int m) {
    constexpr int n = 400;
    double sum = 0;
    for (int ix = 0; ix < n; ++ix) for (int iy = 0; iy < n; ++iy) {
        const double x = .38*.21 + ((ix+.5)/n-.5)*.0252;
        const double y = .36*.15 + ((iy+.5)/n-.5)*.024;
        sum += .0025 * std::pow(m*kPiD/.21, 2) * std::sin(m*kPiD*x/.21) * std::sin(kPiD*y/.15);
    }
    return sum/(n*n);
}
C mechanics(double hz) {
    const C s(0, kTwoPiD*hz);
    C result = 0;
    for (int m=1; m<=3; ++m) {
        const auto spec=kKalimbaBody[m];
        const double omega=kTwoPiD*spec.hz, gamma=omega/(2*spec.q);
        const double projection=std::sin(m*kPiD*.30)*std::sin(kPiD*.5);
        result += strainCoefficient(m)*projection/(.023625*(s*s+2*gamma*s+omega*omega+gamma*gamma));
    }
    return result;
}
double process(KalimbaBody& body, double force, double air = 0) {
    KalimbaBoxDrive drive; drive.board=force; drive.air=air;
    double close=0,far=0,plate=0,side=0,strain=0;
    body.Process(drive,.6,.8,close,far,plate,side,nullptr,&strain);
    return strain;
}
void geometryAndStatic() {
    require(std::abs(KalimbaPlatePickup::ModalMass()-.023625)<1.e-15,"soundboard modal mass unit error");
    const auto film=ReferencePVDFPatch::ForModel(EMarimbaModel::KalimbaArtisan);
    require(film.lengthM==KalimbaPlatePickup::kPatchLength && film.widthM==KalimbaPlatePickup::kPatchWidth,
            "film capacitance and mechanical footprint differ");
    for(int m=1;m<=3;++m)
        require(std::abs(strainCoefficient(m)/KalimbaPlatePickup::StrainPerMetre(m)-1)<5.e-7,
                "plate surface strain differs from independent area integral");
    for(double rate:{44100.,96000.,768000.}) {
        KalimbaBody body; body.Configure(rate);
        double strain=0;
        for(int i=0;i<int(rate);++i) strain=process(body,1);
        require(std::abs(strain/mechanics(0).real()-1)<1.e-6,"plate static strain does not equal force over stiffness");
        body.Reset();
        for(int i=0;i<1000;++i) require(process(body,0,std::sin(.1*i))==0,
                "uncalibrated cavity audio is incorrectly converted into physical charge");
        body.Reset();
        require(body.Energy()==0 && process(body,0)==0,"plate reset is not exact silence");
    }
    std::cout<<"STATIC strain_per_newton="<<mechanics(0).real()<<" modal_mass_kg="<<KalimbaPlatePickup::ModalMass()<<'\n';
}
void frequencyAndLifecycle() {
    double maxError=0;
    for(double rate:{44100.,48000.,96000.,192000.,768000.}) for(double hz:{20.,100.,261.625565,640.,1120.,1900.,4000.}) {
        KalimbaBody body;body.Configure(rate);
        double cc=0,ss=0,cs=0,yc=0,ys=0;
        const int settle=int(rate*.3), samples=int(std::ceil(rate*std::max(.05,8/hz)));
        for(int i=0;i<settle+samples;++i){const double p=kTwoPiD*hz*i/rate,c=std::cos(p),s=std::sin(p),y=process(body,c);
            if(i>=settle){cc+=c*c;ss+=s*s;cs+=c*s;yc+=y*c;ys+=y*s;}}
        const double det=cc*ss-cs*cs;
        const C actual((yc*ss-ys*cs)/det,-(ys*cc-yc*cs)/det);
        // Samples are the end of a held-force interval. Its continuous
        // fundamental gains half a sample of phase and the hold's sinc.
        const double half=kPiD*hz/rate;
        const C expected=mechanics(hz)*(std::sin(half)/half)*std::exp(C(0,half));
        const double error=std::abs(actual/expected-1.);
        maxError=std::max(maxError,error);
        require(error<.005,"plate differs from independent mass/stiffness transfer");
        for(int i=0;i<int(rate*2);++i){process(body,0);if(i%64==0)body.FlushTiny();}
        body.FlushTiny();
        require(body.Energy()==0 && body.HasNoSubnormalState(),"physical board did not drain cleanly");
    }
    std::cout<<"TRANSFER cases=35 max_complex_relative_error="<<maxError<<'\n';
}
using Block=std::array<std::array<float,64>,7>;
void render(MarembaEngine& e,Block& b){e.RenderBatch(b[0].data(),b[1].data(),b[2].data(),b[3].data(),b[4].data(),b[5].data(),b[6].data(),64);}
void singleSensorEngine() {
    double maxDifference=0,energy=0;
    for(double rate:{44100.,96000.})for(int quality:{0,2}) {
        auto a=std::make_unique<MarembaEngine>(rate),b=std::make_unique<MarembaEngine>(rate);
        EngineParameters p;p.model=3;p.oversampling=quality;p.artifacts=p.strikeJitter=p.detune=0;
        p.preampDrive=p.compAmount=p.warmth=0;p.piezoLevel=1;
        a->SetParameters(p);b->SetParameters(p);a->Reset();b->Reset();
        for(int note:{48,60,67,84}){a->NoteOn(note,.8f);b->NoteOn(note,.8f);}
        MarembaEngineTestAccess::PoisonLegacy(*b);
        Block x{},y{};
        for(int block=0;block<48;++block){render(*a,x);render(*b,y);
            for(int ch=0;ch<7;++ch)for(int i=0;i<64;++i){
                const double diff=std::abs(double(x[ch][i])-y[ch][i]);maxDifference=std::max(maxDifference,diff);
                require(diff==0,"per-tine legacy pickup bypasses the shared board sensor");
                if(ch==6)energy+=x[ch][i]*x[ch][i];}}
        require(MarembaEngineTestAccess::SharedEnergy(*a)>0,"real tines do not excite the shared charge amplifier");
        a->Reset();render(*a,x);for(float v:x[6])require(v==0,"engine reset retains piezo output");
    }
    require(energy>1.e-6,"single-sensor isolation test rendered silence");
    std::cout<<"ENGINE cases=4 legacy_feed_difference="<<maxDifference<<" piezo_energy="<<energy<<'\n';
}
}
int main(){std::cout<<std::setprecision(12);geometryAndStatic();frequencyAndLifecycle();singleSensorEngine();std::cout<<"PASS KalimbaPiezoTest checks="<<checks<<'\n';}
