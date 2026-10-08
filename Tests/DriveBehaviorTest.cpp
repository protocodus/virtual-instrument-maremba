// Independent audible contracts for the complete Main preamp. Harmonic tests
// distinguish saturation from a gain knob; the naive target is an antialiasing
// negative control, never an alternative production processor.
#include "AnalogPreamp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using maremba::AnalogPreamp;
constexpr double pi = 3.14159265358979323846;
constexpr int nfft = 8192;
using Signal = std::vector<float>;
int checks = 0;
std::string context;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << context << ": " << message << '\n'; std::exit(1); }
}
double db(double amplitude) { return 20 * std::log10(std::max(amplitude, 1e-20)); }
double power(const Signal& x) { double p = 0; for (float v : x) p += double(v)*v; return p; }
double binPower(const Signal& x, int bin) {
    double real = 0, imaginary = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double angle = 2*pi*bin*i/x.size();
        real += x[i]*std::cos(angle); imaginary += x[i]*std::sin(angle);
    }
    return 4*(real*real+imaginary*imaginary)/(x.size()*x.size());
}
struct Fit { double gain, residual; };
Fit fit(const Signal& reference, const Signal& actual) {
    double dot = 0;
    for (size_t i = 0; i < reference.size(); ++i) dot += double(reference[i])*actual[i];
    const double gain = dot/std::max(power(reference), 1e-30);
    double error = 0;
    for (size_t i = 0; i < reference.size(); ++i) {
        const double difference = actual[i]-gain*reference[i]; error += difference*difference;
    }
    return {gain, std::sqrt(error/std::max(power(actual), 1e-30))};
}
enum class Path { Production, Naive, GainOnly };
Signal tone(double rate, int bin, float amplitude, float drive, Path path = Path::Production) {
    AnalogPreamp preamp; preamp.SetSampleRate(rate); preamp.Reset();
    Signal result;
    // Four complete coherent periods settle the DC blocker at every rate.
    for (int i = 0; i < 5*nfft; ++i) {
        float input = amplitude*std::sin(2*pi*bin*i/nfft), left, right;
        const float control = path == Path::Production ? drive : 0;
        if (path == Path::Naive) input = AnalogPreamp::ApplyDrive(input, drive);
        if (path == Path::GainOnly) input *= 4;
        preamp.Process(input, 0, control, 0, left, right);
        require(std::isfinite(left) && right == 0, "nonfinite output or stereo crosstalk");
        if (i >= 4*nfft) result.push_back(left);
    }
    return result;
}
double thd(const Signal& x, int fundamental) {
    double harmonics = 0;
    for (int h = 2; h <= 12 && h*fundamental < nfft/2; ++h) harmonics += binPower(x, h*fundamental);
    return std::sqrt(harmonics/std::max(binPower(x, fundamental), 1e-30));
}
double foldedAlias(const Signal& x, int fundamental) {
    // An odd coherent fundamental makes the first 31 folded harmonics
    // distinct from each other and from all legitimate sub-Nyquist harmonics.
    double folded = 0;
    for (int h = nfft/(2*fundamental)+1; h <= 31; ++h) {
        int bin = (h*fundamental)%nfft;
        bin = std::min(bin, nfft-bin);
        folded += binPower(x, bin);
    }
    return std::sqrt(folded/std::max(binPower(x, fundamental), 1e-30));
}

void transfer() {
    context = "static target";
    for (float drive : {0.f,.1f,.3f,.6f,1.f}) {
        float previous = AnalogPreamp::ApplyDrive(-4, drive);
        const double linearGain = AnalogPreamp::ApplyDrive(.25f,drive)/.25;
        double maximumSlope = 0;
        for (int i = 1; i <= 16000; ++i) {
            const float x = -4.f+i*.0005f;
            const float value = AnalogPreamp::ApplyDrive(x, drive);
            require(std::isfinite(value) && value >= previous-1e-6f, "target is nonmonotonic or invalid");
            maximumSlope = std::max(maximumSlope, double(value-previous)/.0005);
            previous = value;
            if (drive == 0) require(value == x, "zero Drive is not exact unity");
            if (drive == .1f) require(std::abs(value-x*linearGain) < 2e-6,
                                     "default target is not linear across the full input range");
        }
        const double originSlope = (AnalogPreamp::ApplyDrive(.0001f,drive)
                                   - AnalogPreamp::ApplyDrive(-.0001f,drive))/.0002;
        require(maximumSlope < 1.025*originSlope,
                "soft-knee slope jumps upward into expansion");
        require(AnalogPreamp::ApplyDrive(0,drive) == 0, "static target produces silence DC");
    }
}

void spectra(double rate) {
    const int fundamental = static_cast<int>(std::round(1000*nfft/rate));
    context = "spectra rate="+std::to_string(rate);
    for (float amplitude : {.03f,.1f,.3f}) {
        const auto clean = tone(rate,fundamental,amplitude,0);
        const auto subtle = tone(rate,fundamental,amplitude,.1f);
        const auto soft = tone(rate,fundamental,amplitude,.3f);
        const auto medium = tone(rate,fundamental,amplitude,.6f);
        const auto strong = tone(rate,fundamental,amplitude,1);
        const Fit defaultFit = fit(clean,subtle), strongFit = fit(clean,strong);
        const double defaultThd = thd(subtle,fundamental), softThd = thd(soft,fundamental);
        const double mediumThd = thd(medium,fundamental), strongThd = thd(strong,fundamental);
        require(db(defaultFit.gain) > .05 && db(defaultFit.gain) < .2,
                "10% Drive is missing its subtle boost or boosts too much");
        require(defaultFit.residual < 3e-6 && defaultThd < 3e-6,
                "10% Drive adds nonlinear distortion");
        require(strongThd > .01 && strongFit.residual > .01,
                "full Drive is inaudible after matching gain");
        require(!(strongFit.residual < 3e-6 && strongThd < 3e-6),
                "clean-default oracle accepts the always-saturated negative control");
        require(mediumThd > softThd && strongThd > mediumThd,
                "turning up Drive does not progressively increase harmonic color");
        std::cout << "DRIVE rate=" << rate << " peak=" << amplitude
                  << " default_gain_db=" << db(defaultFit.gain)
                  << " default_residual_db=" << db(defaultFit.residual)
                  << " thd_db[.1,.3,.6,1]=" << db(defaultThd) << ',' << db(softThd)
                  << ',' << db(mediumThd) << ',' << db(strongThd)
                  << " full_gainfit_residual_db=" << db(strongFit.residual) << '\n';
    }
    // A gain-only or bypass implementation passes a simple output-difference
    // test. It must fail the independent harmonic and gain-fit requirements.
    const auto dry = tone(rate,fundamental,.1f,0);
    const auto gain = tone(rate,fundamental,.1f,1,Path::GainOnly);
    require(thd(gain,fundamental) < 3e-6 && fit(dry,gain).residual < 3e-6,
            "gain-only negative control unexpectedly looks nonlinear");
    require(!(thd(gain,fundamental) > .01 && fit(dry,gain).residual > .01),
            "audibility oracle accepts the gain-only negative control");

    int highBin = static_cast<int>(std::round(8000*nfft/rate));
    if (highBin%2 == 0) ++highBin; // Coprime with the power-of-two FFT length.
    for (float amplitude : {.1f,.3f}) {
        const auto antialiased = tone(rate,highBin,amplitude,1);
        const auto naive = tone(rate,highBin,amplitude,1,Path::Naive);
        const double actualAlias = foldedAlias(antialiased,highBin), naiveAlias = foldedAlias(naive,highBin);
        std::cout << "ALIAS rate=" << rate << " frequency=" << highBin*rate/nfft << " peak=" << amplitude
                  << " production_dbc=" << db(actualAlias) << " naive_dbc=" << db(naiveAlias)
                  << " reduction_db=" << db(naiveAlias/actualAlias) << '\n';
        require(naiveAlias > .01 && actualAlias < naiveAlias*.75,
                "production does not reduce folded harmonics versus the instantaneous target");
    }
}

double averageTarget(double a, double b, float drive) {
    // Numerical integration is independent of the production closed-form
    // antiderivative and tests crossings of both rounded rails.
    constexpr int intervals = 2048;
    double sum = 0;
    for (int i = 0; i <= intervals; ++i) {
        const float x = static_cast<float>(b+(a-b)*i/intervals);
        const double weight = i == 0 || i == intervals ? 1 : i%2 ? 4 : 2;
        sum += weight*AnalogPreamp::ApplyDrive(x,drive);
    }
    return sum/(3*intervals);
}

void stateAndAutomation(double rate) {
    context = "state rate="+std::to_string(rate);
    AnalogPreamp p; p.SetSampleRate(rate); p.Reset();
    float previousOut = 0; double previousSaturation = 0, previousInput = 0;
    const double dcPole = static_cast<float>(std::exp(-2*pi*20/rate));
    const std::array<float,9> input = {.17f,-.13f,.08f,.19f,-.23f,.04f,0,0,.1f};
    for (size_t i = 0; i < input.size(); ++i) {
        const float drive = i < 3 ? .1f : i < 6 ? 1.f : .6f;
        float left,right; p.Process(input[i],0,drive,0,left,right);
        const double recovered = left+previousSaturation-dcPole*previousOut;
        const double expected = i < 3 ? AnalogPreamp::ApplyDrive(input[i],drive)
                                     : averageTarget(input[i],previousInput,drive);
        require(std::abs(recovered-expected) < 3e-6,
                "live Drive resumes stale input history or disagrees with its averaged target");
        previousOut=left; previousSaturation=recovered; previousInput=input[i];
    }

    const auto sequence = [rate](AnalogPreamp& preamp) {
        Signal output; float left,right;
        for (int i=0;i<4096;++i) {
            const float drive = i < 1024 ? .1f : i < 2048 ? .1f+.9f*(i-1024)/1024 : i < 3072 ? 1.f : 0.f;
            preamp.Process(.1f*std::sin(2*pi*373*i/rate),0,drive,0,left,right);
            require(std::isfinite(left) && right == 0, "automation produced invalid audio or crosstalk");
            output.push_back(left);
        }
        return output;
    };
    p.Reset(); const auto first=sequence(p); p.Reset(); require(sequence(p)==first,"Reset is not repeatable");
    AnalogPreamp fresh; fresh.SetSampleRate(rate); fresh.Reset();
    require(sequence(fresh)==first,"Reset does not reproduce a fresh processor");
    p.SetSampleRate(rate==48000 ? 96000 : 48000); p.Reset(); sequence(p);
    p.SetSampleRate(rate); p.Reset(); require(sequence(p)==first,"rate/reset retained stale preamp state");

    for (float drive : {0.f,.1f,1.f}) {
        p.Reset(); float left,right; p.Process(.01f,0,drive,0,left,right);
        require(left>0 && right==0,"Drive added fixed onset latency");
    }
    // At a constant input, a gradual knob sweep cannot hide a waveform edge.
    p.Reset(); float prior=0,unused; double largestStep=0;
    for (int i=0;i<4000;++i) {
        const float drive = i<1000 ? .1f : i<2000 ? .1f+.9f*(i-1000)/1000 : i<3000 ? 1.f-.9f*(i-2000)/1000 : .1f;
        float left; p.Process(.03f,0,drive,0,left,unused);
        if(i>1000)largestStep=std::max(largestStep,double(std::abs(left-prior)));
        prior=left;
    }
    require(largestStep<.001,"smooth automation produces a discontinuity at the clean/active boundary");
    for (int i=0;i<static_cast<int>(rate*8);++i) {
        p.Process(0,0,1,0,prior,unused);
        if(i%64==0)p.FlushTiny();
    }
    require(prior==0 && unused==0 && p.HasNoSubnormalState(),"Drive leaves DC, noise or subnormal state after silence");
    std::cout << "STATE rate=" << rate << " largest_smooth_control_step=" << largestStep << '\n';
}
} // namespace

int main() {
    std::cout << std::setprecision(10);
    transfer();
    for (double rate : {44100.,48000.,96000.}) { spectra(rate); stateAndAutomation(rate); }
    std::cout << "DriveBehaviorTest PASSED checks=" << checks << '\n';
}
