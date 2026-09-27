#include "MarembaEngine.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
namespace maremba {
struct CaptureSceneTestAccess {
    static float Input(const CaptureScene& scene,int id,int lane,int sub) { return scene.mSource[id].buffer[lane][sub]; }
};
struct MarembaEngineTestAccess {
    static int Factor(const MarembaEngine& e) { return e.mActiveFactor; }
    static const CaptureScene& Scene(const MarembaEngine& e) { return e.mMicMixer; }
    static const SympatheticMesh& Mesh(const MarembaEngine& e) { return e.mSympathetic; }
    static std::array<float,3> PredictCompactLast(const MarembaEngine& e) {
        auto voices=e.mVoices;
        auto frame=e.mFrameBody;
        auto board=e.mKalimbaBody;
        std::array<float,3> expected{};
        for(int sub=0;sub<e.mActiveFactor;++sub) {
            float mechanical=0; KalimbaBoxDrive box;
            for(size_t slot=0;slot<voices.size();++slot) if(voices[slot].IsActive()) {
                auto& v=voices[slot];float c=0,f=0,p=0;
                if(v.ProcessSample(c,f,p)) {
                    mechanical+=v.GetMechanicalSample();
                    if(v.GetModel()==EMarimbaModel::KalimbaArtisan)
                        box.Add(v.GetBridgeForce(),e.mRoutes[slot].boxAir,0,KalimbaTineSide(v.GetNoteNumber()));
                }
            }
            if(e.mParams.model!=3) expected[0]=.5f*(frame.Process(mechanical)*e.mBodyGain*.375f);
            double c=0,f=0,p=0,s=0,emitted[2]{};
            board.Process(box,.6+.8*e.mBodyGain,.8*(1-e.mBodyGain),c,f,p,s,emitted);
            expected[1]=.5f*static_cast<float>(emitted[0]*.072);
            expected[2]=.5f*static_cast<float>(emitted[1]*.072);
        }
        return expected;
    }
};
}
namespace {
using namespace maremba;
int checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
void referenceUnits() {
    auto compact=std::make_unique<CaptureScene>(),manual=std::make_unique<CaptureScene>(),half=std::make_unique<CaptureScene>();
    compact->Allocate(48000);manual->Allocate(48000);half->Allocate(48000);
    double error=0,linearity=0,energy=0;
    for(int frame=0;frame<12000;++frame){compact->BeginFrame(2);manual->BeginFrame(2);half->BeginFrame(2);
        for(int sub=0;sub<2;++sub){const float pressure=frame<1500?.1f*std::sin(.073f*(2*frame+sub)):0;
            compact->AddCompactPressure(kCaptureFrameSource,sub,pressure);
            // Independent inverse-distance law: p(1m)=p(.6m)*.6/1.
            manual->Add(kCaptureFrameSource,sub,pressure,pressure,pressure*.6f);
            half->AddCompactPressure(kCaptureFrameSource,sub,pressure*.5f);}
        std::array<float,4>a{},b{},h{};compact->Process(a[0],a[1],a[2],a[3]);manual->Process(b[0],b[1],b[2],b[3]);half->Process(h[0],h[1],h[2],h[3]);
        for(int ch=0;ch<4;++ch){error=std::max(error,std::abs(double(a[ch])-b[ch]));linearity=std::max(linearity,std::abs(double(a[ch])-2*h[ch]));energy+=a[ch]*a[ch];}}
    require(error==0,"compact source reference distance conversion differs from inverse-distance oracle");
    // Capture histories intentionally flush values near1e-30; permit only
    // that inaudible numerical floor, not signal-dependent gain.
    require(linearity<1e-24,"compact source conversion is nonlinear");require(energy>1e-6,"source-unit test is silent");
    std::cout<<"REFERENCE inverse_distance_error="<<error<<" linearity_error="<<linearity<<'\n';
}
void boardDecomposition() {
    KalimbaBody board;board.Configure(96000);double error=0;
    for(int i=0;i<48000;++i){KalimbaBoxDrive d;d.Add(std::sin(i*.051),.7,.3,.5);double c=0,f=0,p=0,s=0,emitted[2]{};board.Process(d,1.1,.4,c,f,p,s,emitted);
        // Former per-ear terms .5*(close-side), .5*(close+side) sum
        // to close. The new centre board/hole sum must preserve that total.
        error=std::max(error,std::abs(c-emitted[0]-emitted[1]));}
    require(error<1e-12,"board/hole decomposition changes former close source calibration");
    std::cout<<"BOARD decomposition_error="<<error<<'\n';
}
void productionCalibration() {
    double maxError=0,signal=0;
    for(int model=0;model<4;++model)for(int quality:{0,2}){
        auto engine=std::make_unique<MarembaEngine>(48000);EngineParameters p;p.model=model;p.oversampling=quality;
        p.bodyBloom=p.sympathetic=1;p.artifacts=p.strikeJitter=p.buzzAmount=0;p.velocityToHardness=p.velocityToDecay=0;
        engine->SetParameters(p);engine->Reset();engine->NoteOn(60,.85f);
        for(int frame=0;frame<256;++frame){const auto expected=MarembaEngineTestAccess::PredictCompactLast(*engine);float l=0,r=0;engine->RenderBatch(&l,&r,nullptr,nullptr,nullptr,nullptr,nullptr,1);
            const auto& scene=MarembaEngineTestAccess::Scene(*engine);const auto& mesh=MarembaEngineTestAccess::Mesh(*engine);const int sub=MarembaEngineTestAccess::Factor(*engine)-1;
            auto check=[&](int id,float value){for(int lane=0;lane<3;++lane){const float want=lane==2?value*.6f:value;const float actual=CaptureSceneTestAccess::Input(scene,id,lane,sub);maxError=std::max(maxError,std::abs(double(actual)-want));require(std::abs(actual-want)<2e-6f*std::max(1.f,std::abs(want)),"production compact-source calibration or reference units differ");}signal+=value*value;};
            check(kCaptureFrameSource,expected[0]);
            check(kCaptureBoardSource,expected[1]);check(kCaptureHoleSource,expected[2]);
            for(int key=0;key<mesh.ReceiverCount();++key)if(mesh.ReceiverNote(key)!=60)
                check(CaptureSourceIndex(static_cast<EMarimbaModel>(model),mesh.ReceiverNote(key)),.35f*mesh.GetReceiverPressure(key,1));
        }
    }
    require(signal>1e-5,"production compact-source oracle is silent");
    std::cout<<"PRODUCTION cases=8 max_input_error="<<maxError<<" reference_energy="<<signal<<'\n';
}
}
int main(){std::cout<<std::setprecision(12);referenceUnits();boardDecomposition();productionCalibration();std::cout<<"PASS CompactCaptureSourceTest checks="<<checks<<'\n';}
