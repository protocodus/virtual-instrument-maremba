// The yielding spring's force and dissipated energy must describe the same
// modes. Check the production voice against the independently generated
// frequency/loss table, then exercise signed interpolation and Carving.
#include "MarembaVoice.h"
#include "KalimbaMountShapes.h"
#include "KalimbaModes.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static int Count(const MarembaVoice& v) { return v.mNumModes; }
    static double Hz(const MarembaVoice& v) { return v.mF0; }
    static double Bridge(const MarembaVoice& v, int m) { return v.mBridgeGain[m]; }
    static double Displacement(const MarembaVoice& v) { return v.mModes[0].toDisplacement; }
};
}
namespace {
using namespace maremba;
using Access = MarembaVoiceTestAccess;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
MarembaVoice strike(int note, float carving, double rate) {
    MarembaVoice voice;
    FastPRNG random(0x28194);
    StrikeOptions options; options.carving = carving;
    voice.Trigger(note, .7f, EMarimbaModel::KalimbaArtisan, .5f, .5f,
        0, 0, 1, 0, 0, 0, rate, random, 0, 0, 1, 0, 0, options);
    return voice;
}
}
int main() {
    using namespace maremba;
    double worstLoss = 0, worstVoice = 0, worstContinuity = 0;
    double smallestFundamental = 1;
    int modesChecked = 0;
    // Gamma_m/Gamma_0 = (phi_m(0)/phi_0(0))^2 / (omega_m/omega_0)
    // for the same spring loss factor. These independently generated loss
    // values also detect a hidden return to the old rigid-support forces.
    for (const auto& point : kKalimbaMount) {
        const auto shape = GetKalimbaMountShape(point.hz);
        for (int m = 0; m < 8; ++m) {
            const double ratio = KalimbaMountedBridgeRatio(shape, m);
            const double energyRatio = point.bridgeLoss[m] * point.ratio[m];
            worstLoss = std::max(worstLoss, std::abs(ratio*ratio-energyRatio)/std::max(1.,energyRatio));
        }
    }
    require(worstLoss < 2.e-5, "bridge force contradicts the fitted mount's energy loss");
    for (int note = 12; note <= 108; ++note) for (double rate : {88200., 192000.}) {
        const auto reference = strike(note, 1, rate);
        const double hz = Access::Hz(reference);
        const auto shape = GetKalimbaMountShape(hz);
        const double displacement = KalimbaMountedShape(shape, 0, 0);
        smallestFundamental = std::min(smallestFundamental, displacement);
        const auto geometry = GetTineGeometry(hz);
        // Coupling=0 leaves the documented 0.6 fixed bridge share.
        const double expectedFundamental = .6 * geometry.bendingStiffness * KalimbaBridgeShearJump(0)
            / std::pow(geometry.lengthM,3) * Access::Displacement(reference);
        require(std::abs(Access::Bridge(reference,0)/expectedFundamental-1)<1.e-12,
            "fundamental output calibration changed");
        for (float carving : {0.f, .25f, .5f, .75f, 1.f}) {
            const auto voice = strike(note, carving, rate);
            require(Access::Bridge(voice,0)==Access::Bridge(reference,0),
                "Carving changed the fundamental calibration");
            for (int m = 0; m < Access::Count(voice); ++m) {
                const double mounted = KalimbaMountedShape(shape,m,0)/displacement;
                const double rigid = kKalimbaBeamModes[m].contactScale * KalimbaBridgeShearJump(m)
                    / (kKalimbaBeamModes[0].contactScale * KalimbaBridgeShearJump(0));
                const double expected = mounted + .65*(1-carving)*(rigid-mounted);
                const double actual = Access::Bridge(voice,m)/Access::Bridge(voice,0);
                worstVoice = std::max(worstVoice,std::abs(actual-expected));
                ++modesChecked;
            }
        }
    }
    require(worstVoice<1.e-10, "voice body drive bypasses the mounted reaction or its Carving morph");
    require(smallestFundamental>.03, "bridge normalization approaches a fundamental node");
    for (const auto& point : kKalimbaMount) for (int m=0; m<8; ++m) {
        const double a=KalimbaMountedBridgeRatio(GetKalimbaMountShape(point.hz*(1-1.e-8)),m);
        const double b=KalimbaMountedBridgeRatio(GetKalimbaMountShape(point.hz*(1+1.e-8)),m);
        worstContinuity=std::max(worstContinuity,std::abs(a-b));
    }
    require(worstContinuity<1.e-4,"bridge force jumps at a mount-table boundary");
    const auto c4=GetKalimbaMountShape(261.6255653005986);
    const auto a5=GetKalimbaMountShape(880);
    require(KalimbaMountedBridgeRatio(c4,4)>0 && KalimbaMountedBridgeRatio(a5,7)>0,
        "mounted force lost its modal polarity changes");
    require(std::abs(KalimbaMountedBridgeRatio(c4,5))<1,
        "near-nodal mounted mode drives the box as strongly as a rigid bridge");
    std::cout << "BRIDGE loss_error=" << worstLoss << " voice_error=" << worstVoice
        << " continuity=" << worstContinuity << " minimum_fundamental=" << smallestFundamental
        << " modes_checked=" << modesChecked << '\n';
    std::cout << "Kalimba bridge-reaction contracts passed.\n";
}
