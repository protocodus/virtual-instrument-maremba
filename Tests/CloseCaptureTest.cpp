#include "../DSP/CloseCapture.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <memory>
#include <string>

namespace {
using namespace maremba;
constexpr double pi = 3.14159265358979323846;
using Stereo = std::array<std::vector<float>, 2>;
void Require(bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
std::unique_ptr<CloseCapture> Create(double rate, bool all = false) {
    auto capture = std::make_unique<CloseCapture>();
    capture->Allocate(192000.0, kCaptureSourceCount);
    capture->SetSampleRate(rate);
    if (all) for (int id = 0; id < kCaptureSourceCount; ++id)
        Require(capture->AddSource(id, CaptureSourcePosition(id), kCloseHrtfSourceProfile[id]),
                "all declared source positions fit the physical propagation budget");
    return capture;
}
Stereo Render(CloseCapture& capture, int frames, const std::vector<int>& ids,
              bool left = true, bool right = true, bool impulses = true) {
    std::array<float, kCaptureSourceCount> inputL{}, inputR{};
    Stereo result;
    for (auto& ear : result) ear.resize(static_cast<std::size_t>(frames));
    for (int n = 0; n < frames; ++n) {
        inputL.fill(0.0f); inputR.fill(0.0f);
        for (int id : ids) {
            const float signal = impulses ? (n == 0 ? 1.0f : 0.0f)
                : (n < 271 ? static_cast<float>(0.1*std::sin(0.13*n + 0.47*id)
                                               + 0.05*std::cos(0.019*n + 0.1*id)) : 0.0f);
            if (left) inputL[static_cast<std::size_t>(id)] = signal;
            if (right) inputR[static_cast<std::size_t>(id)] = signal;
        }
        capture.Process(inputL.data(), inputR.data(), kCaptureSourceCount,
                        result[0][static_cast<std::size_t>(n)], result[1][static_cast<std::size_t>(n)]);
    }
    return result;
}
std::complex<double> Transform(const std::vector<float>& samples, double frequency, double rate) {
    std::complex<double> result{};
    for (std::size_t n = 0; n < samples.size(); ++n)
        result += static_cast<double>(samples[n])*std::polar(1.0, -2*pi*frequency*n/rate);
    return result;
}
void CheckPropagationAndMeasuredResponse() {
    // Check transfer against the frozen measured response and geometric flight
    // directly, independently of the partition/overlap-save implementation.
    double worstDb = 0.0, worstPhase = 0.0;
    for (double rate : {8000., 16000., 32000., 44100., 48000., 88200., 96000., 192000.}) {
        auto capture = Create(rate, true);
        std::printf("rate %.0f partition %d kernel %d\n", rate, capture->PartitionFrames(), capture->KernelFrames());
        for (int id : {36, 60, 61, 62, 63, 96, 3*128+60, kCaptureFrameSource,
                       kCaptureBoardSource, kCaptureHoleSource}) {
            capture->Reset();
            const auto output = Render(*capture, static_cast<int>(0.04*rate), {id});
            const auto& profile = kCloseHrtfProfiles[kCloseHrtfSourceProfile[id]];
            const double range = CaptureDistance(CaptureSourcePosition(id), CloseHeadCentre());
            for (int ear = 0; ear < 2; ++ear) {
                const std::vector<float> kernel(std::begin(profile.impulse[ear]), std::end(profile.impulse[ear]));
                for (double frequency : {100., 500., 1000., 2000., 5000., 10000., 16000.}) {
                    if (frequency > 0.35*rate) continue;
                    const double delay = range/kCaptureSpeedOfSound
                                       + (ear ? 0.5 : -0.5)*profile.residualItdSeconds;
                    const auto expected = Transform(kernel, frequency, kCloseHrtfReferenceRate)
                                        * (CloseCapture::kReferenceDistance/range)
                                        * std::polar(1.0, -2*pi*frequency*delay);
                    const auto actual = Transform(output[ear], frequency, rate);
                    const auto ratio = actual/expected;
                    const double db = std::abs(20*std::log10(std::abs(ratio)));
                    const double phase = std::abs(std::arg(ratio));
                    worstDb = std::max(worstDb, db); worstPhase = std::max(worstPhase, phase);
                    if (db > 1.0 || phase > 0.10)
                        std::printf("response rate %.0f source %d ear %d freq %.0f error %.6fdB phase %.6f\n",
                                    rate, id, ear, frequency, db, phase);
                    Require(db < 1.0, "measured directional spectral response survives convolution/resampling");
                    Require(phase < 0.10, "arrival includes flight and residual ITD once, with partition latency compensated");
                }
            }
            Require(capture->HasNoSubnormalState(), "finite normal persistent state after impulse");
            Require(!capture->HasTail(), "finite measured response and delay tail drain completely");
        }
    }
    std::printf("transfer worst magnitude %.6f dB, phase %.6f rad\n", worstDb, worstPhase);
}
void CheckLinearityAndLifecycle() {
    auto capture = Create(48000., true);
    const std::vector<int> ids{36, 60, 96, kCaptureFrameSource, kCaptureBoardSource, kCaptureHoleSource};
    const auto combined = Render(*capture, 2048, ids, true, true, false);
    Stereo sum{std::vector<float>(2048), std::vector<float>(2048)};
    for (int id : ids) {
        capture->Reset();
        const auto one = Render(*capture, 2048, {id}, true, true, false);
        for (int ear=0; ear<2; ++ear) for (int n=0; n<2048; ++n) sum[ear][n] += one[ear][n];
    }
    double energy=0.0, error=0.0;
    for (int ear=0; ear<2; ++ear) for (int n=0; n<2048; ++n) {
        energy += combined[ear][n]*combined[ear][n];
        error += std::pow(combined[ear][n]-sum[ear][n], 2);
    }
    Require(energy>1e-5 && error/energy<1e-11, "source-specific filtering and summing remain linear");
    capture->Reset();
    Require(Render(*capture, 2048, ids, true, true, false)==combined, "Reset restores bit-exact deterministic state");
    for (bool left : {false,true}) {
        capture->Reset();
        const auto isolated = Render(*capture, 2048, {60}, left, !left);
        double audible=0;
        for (int n=0; n<2048; ++n) {
            Require(isolated[left ? 1 : 0][n]==0.0f, "independent ear fields have no artificial crossfeed");
            audible += std::abs(isolated[left ? 0 : 1][n]);
        }
        Require(audible>0.01, "receiver's measured microphone response is audible");
    }
    capture->Reset();
    const auto reference = Render(*capture, 2048, {60});
    capture->Reset();
    std::array<float,kCaptureSourceCount> input{}; input[60]=1.0f;
    float l=0,r=0;
    capture->Process(input.data(), input.data(), kCaptureSourceCount,l,r);
    Require(l==reference[0][0] && r==reference[1][0], "first input frame consistent");
    for (int n=1;n<2048;++n) {
        capture->Process(nullptr,nullptr,0,l,r);
        Require(l==reference[0][n] && r==reference[1][n], "source tail survives null inputs/shrinking source count");
    }
    // An inactive source can start at any FFT/group phase. Its physical
    // arrival must not depend on a callback or partition boundary.
    for (int offset : {1, 17, 63, 128, 753}) {
        capture->Reset();
        for (int n=0;n<2048+offset;++n) {
            input[60] = n==offset ? 1.0f : 0.0f;
            capture->Process(input.data(),input.data(),kCaptureSourceCount,l,r);
            const float expectedL=n>=offset ? reference[0][n-offset] : 0.0f;
            const float expectedR=n>=offset ? reference[1][n-offset] : 0.0f;
            Require(std::abs(l-expectedL)<2e-6f && std::abs(r-expectedR)<2e-6f,
                    "first arrival is invariant to arbitrary source activation time");
        }
    }
    capture->Reset();
    input[60]=std::numeric_limits<float>::quiet_NaN();
    capture->Process(input.data(),input.data(),kCaptureSourceCount,l,r);
    Require(l==0 && r==0 && !capture->HasTail(), "nonfinite input resets capture state to silence");
    input[60]=std::numeric_limits<float>::denorm_min();
    capture->Process(input.data(),input.data(),kCaptureSourceCount,l,r);
    Require(l==0 && r==0 && capture->HasNoSubnormalState(), "subnormal inputs do not become persistent state");
    capture->Reset();
    input[60]=1;
    capture->Process(input.data(),input.data(),kCaptureSourceCount,l,r);
    capture->SetSampleRate(96000.);
    Require(!capture->HasTail(), "rate change clears old filter and propagation history");
    Require(!capture->AddSource(-1,{},0) && !capture->AddSource(0,{},kCloseHrtfProfileCount)
         && !capture->AddSource(0,CloseHeadCentre(),0), "invalid identities and noncausal geometry are rejected");
    std::printf("source sum relative squared error %.9g; lifecycle passed\n",error/energy);
}
void CheckSceneBinauralCues() {
    auto capture=Create(48000.,true);
    for(int id : {36,96}) {
        capture->Reset();
        const auto output=Render(*capture,2048,{id});
        const auto left=Transform(output[0],500.,48000.);
        const auto right=Transform(output[1],500.,48000.);
        const double interaural=-std::arg(right/left)/(2*pi*500.);
        double energy[2]{};
        for(int ear=0;ear<2;++ear) for(double f=6000.;f<=12000.;f+=500.)
            energy[ear]+=std::norm(Transform(output[ear],f,48000.));
        const double level=10*std::log10(energy[0]/energy[1]);
        std::printf("scene source%d 500Hz right-minus-left arrival %.3fus high-band left/right %.3fdB\n",
                    id,1e6*interaural,level);
        Require(id==36 ? interaural>50e-6 : interaural < -50e-6,
                "bass-left/treble-right scene preserves measured interaural timing handedness");
        Require(id==36 ? level>1.0 : level < -1.0,
                "head shadows high frequencies at the physically farther ear");
    }
}
void Benchmark() {
    // All 515 sources configured, 80 simultaneously active: one typical full
    // sympathetic rail plus played voices. Timing excludes allocation/setup.
    for (double rate : {44100.,48000.,96000.,192000.}) {
        auto capture=Create(rate,true);
        std::array<float,kCaptureSourceCount> left{},right{};
        for(int i=0;i<80;++i) left[36+i]=right[36+i]=static_cast<float>(0.001*(1+i%7));
        float l=0,r=0; double checksum=0;
        std::vector<double> times, cpuTimes;
        int wallMisses=0,cpuMisses=0;
        for(int block=0;block<2200;++block) {
            const auto start=std::chrono::steady_clock::now();
            const auto cpuStart=std::clock();
            for(int n=0;n<64;++n) { capture->Process(left.data(),right.data(),kCaptureSourceCount,l,r); checksum+=l+r; }
            const double cpu=1e6*(std::clock()-cpuStart)/CLOCKS_PER_SEC;
            const double wall=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            if(block>=200) {
                times.push_back(wall); cpuTimes.push_back(cpu);
                wallMisses+=wall>64e6/rate; cpuMisses+=cpu>64e6/rate;
            }
        }
        std::sort(times.begin(),times.end());
        std::sort(cpuTimes.begin(),cpuTimes.end());
        std::printf("Close benchmark %.0fHz 80 sources block64 wall median/p99/max %.3f/%.3f/%.3fus CPU %.3f/%.3f/%.3fus wall/CPU misses %d/%d of2000 deadline %.3fus checksum %.6f\n",
                    rate,times[times.size()/2],times[99*times.size()/100],times.back(),
                    cpuTimes[cpuTimes.size()/2],cpuTimes[99*cpuTimes.size()/100],cpuTimes.back(),
                    wallMisses,cpuMisses,64e6/rate,checksum);
    }
}
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="--benchmark") { Benchmark(); return 0; }
    CheckPropagationAndMeasuredResponse(); CheckLinearityAndLifecycle(); CheckSceneBinauralCues();
    std::puts("CloseCaptureTest passed");
}
