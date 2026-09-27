#pragma once

#include "DspMath.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

// Declared generic pressure-condenser reference, not a measured branded mic.
// The acoustic equivalent circuit follows B&K Microphone Handbook Vol.1,
// sections2.3.10–11, tables2.2–3: https://www.bksv.com/doc/be1447.pdf .
// q is diaphragm volume displacement; p is pressure at both diaphragm/vent:
//   L*q'' + R*q' + q/Cd = p-pc
//   Cc*pc' = q' + (p-pc)/Rv.
// Thus q/p = Rv*Cc*s / [(L*s²+R*s+1/Cd)*(1+Rv*Cc*s)+Rv*s].
// It includes diaphragm/slit inertia, compliance, viscous slit damping and
// cavity/vent pressure equalization. The reference8mm effective diaphragm is
// compact/omnidirectional: angular diffraction, membrane higher modes,
// self-noise, electromechanical loading feedback and overload are not modeled.
// Electrical components below are declared generic buffer/coupling choices.
// All stages are linear and causal. Unity magnitude at1kHz is the fixed output
// sensitivity reference, common to all rooms; it is not automatic gain control.
class RoomPressureMicrophone {
public:
    static constexpr double kDiaphragmDiameter = .008; // m, compact receiver
    static constexpr double kDiaphragmCompliance = .314e-12; // m^5/N
    static constexpr double kCavityCompliance = 2.83e-12; // m^5/N
    static constexpr double kDiaphragmAcousticMass = 600.; // kg/m^4
    static constexpr double kSlitAcousticMass = 296.; // kg/m^4
    static constexpr double kSlitResistance = 56.2e6; // N*s/m^5
    static constexpr double kVentResistance = 36.e9; // N*s/m^5
    static constexpr double kCapsuleCapacitance = 17.e-12; // F
    static constexpr double kBufferInputCapacitance = 3.e-12; // F
    static constexpr double kBufferInputResistance = 1.e9; // ohm
    static constexpr double kOutputCapacitance = 1.e-6; // F
    static constexpr double kBufferOutputResistance = 150.; // ohm
    static constexpr double kLoadResistance = 20000.; // ohm
    static constexpr double kOpenCircuitSensitivity = .050; // V/Pa reference

    RoomPressureMicrophone() { SetSampleRate(48000.); }
    void SetSampleRate(double rate) {
        mRate = std::isfinite(rate) ? std::max(8000.,rate) : 48000.;
        // Factor the physical cubic into its vent pole and mechanical pair.
        const double L=kDiaphragmAcousticMass+kSlitAcousticMass;
        const double rc=kVentResistance*kCavityCompliance;
        const double a3=L*rc,a2=L+kSlitResistance*rc;
        const double a1=kSlitResistance+rc/kDiaphragmCompliance+kVentResistance;
        const double a0=1./kDiaphragmCompliance;
        double lo=0.,hi=1000.;
        for(int i=0;i<64;++i){const double v=.5*(lo+hi);const double d=((-a3*v+a2)*v-a1)*v+a0;if(d>0)lo=v;else hi=v;}
        const double vent=.5*(lo+hi),B=a2/a3-vent,C=a0/(a3*vent);
        // Bilinear mechanics, prewarped at its physical resonance (~10kHz).
        // At unsupported low rates the ordinary Tustin mapping remains stable.
        // This discretization preserves the resonance, with the usual bounded
        // bilinear phase/frequency warping elsewhere; it adds no pure delay.
        const double omega=std::sqrt(C);
        const double k=omega<.9*kPiD*mRate ? omega/std::tan(omega/(2.*mRate)) : 2.*mRate;
        const double den=k*k+B*k+C;
        mMechanical={C/den,2.*C/den,C/den,2.*(C-k*k)/den,(k*k-B*k+C)/den};
        mVent=HighPass(vent,2.*mRate,1.);
        const double cap=kCapsuleCapacitance+kBufferInputCapacitance;
        mInput=HighPass(1./(kBufferInputResistance*cap),2.*mRate,kCapsuleCapacitance/cap);
        const double outputR=kBufferOutputResistance+kLoadResistance;
        mOutput=HighPass(1./(outputR*kOutputCapacitance),2.*mRate,kLoadResistance/outputR);
        const double w=kTwoPiD*1000./mRate;
        const double gain=std::sqrt(mMechanical.Power(w)*mVent.Power(w)*mInput.Power(w)*mOutput.Power(w));
        mSensitivity=1./gain; // fixed capsule/buffer calibration at1kHz
        Reset();
    }
    float Process(float pressure) {
        // Emitted source pressure is voiced, not calibrated absolute Pascals;
        // the50mV/Pa capsule sensitivity cancels in this reference conversion.
        double y=mVent.Process(pressure);
        y=mMechanical.Process(y);y=mInput.Process(y);y=mOutput.Process(y);
        return static_cast<float>(y*mSensitivity);
    }
    void Reset(){mMechanical.Reset();mVent.Reset();mInput.Reset();mOutput.Reset();}
    void FlushTiny(){mMechanical.Flush();mVent.Flush();mInput.Flush();mOutput.Flush();}
    bool HasNoSubnormalState()const{return mMechanical.Normal()&&mVent.Normal()&&mInput.Normal()&&mOutput.Normal();}
private:
    struct Section {
        double b0=1.,b1=0.,b2=0.,a1=0.,a2=0.,s1=0.,s2=0.;
        double Process(double x){const double y=b0*x+s1;s1=b1*x-a1*y+s2;s2=b2*x-a2*y;Flush();return y;}
        void Reset(){s1=s2=0.;}
        void Flush(){if(s1<1.e-24&&s1> -1.e-24)s1=0.;if(s2<1.e-24&&s2> -1.e-24)s2=0.;}
        bool Normal()const{return std::fpclassify(s1)!=FP_SUBNORMAL&&std::fpclassify(s2)!=FP_SUBNORMAL;}
        double Power(double w)const{
            const double c=std::cos(w),s=std::sin(w),c2=std::cos(2*w),s2w=std::sin(2*w);
            const double nr=b0+b1*c+b2*c2,ni=b1*s+b2*s2w,dr=1.+a1*c+a2*c2,di=a1*s+a2*s2w;
            return (nr*nr+ni*ni)/(dr*dr+di*di);
        }
    };
    static Section HighPass(double pole,double k,double gain){const double b=gain*k/(k+pole);return {b,-b,0.,(pole-k)/(pole+k),0.};}
    Section mMechanical{},mVent{},mInput{},mOutput{};
    double mRate=48000.,mSensitivity=1.;
};
} // namespace maremba
