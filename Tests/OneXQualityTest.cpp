// Native-rate quality is a true decimator bypass. Legacy quality IDs retain
// their factors, live changes drain safely, and all seven outputs stay aligned.
#include "MarembaEngine.h"
#include "MarembaPlayer.h"
#include "MainCalibration.h"
#include "MainVolume.h"
#include "Oversampler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace maremba {
struct MarembaEngineTestAccess {
    static int Factor(const MarembaEngine& engine) { return engine.mActiveFactor; }
    static bool Switching(const MarembaEngine& engine) { return engine.mSwitchPending; }
    static double Clock(const MarembaEngine& engine) { return engine.mClock; }
    static int LiveVoices(const MarembaEngine& engine) {
        int count = 0;
        for (const auto& voice : engine.mVoices) if (voice.IsActive() && !voice.IsFading()) ++count;
        return count;
    }
};
}
namespace {
using namespace maremba;
using Frame = std::array<float, 7>;
constexpr std::array<int, 4> kFactors {{2, 4, 8, 1}};
std::uint64_t checks = 0;
void Require(bool okay, const char* message) {
    ++checks;
    if (!okay) { std::fprintf(stderr, "FAIL OneXQualityTest: %s\n", message); std::exit(1); }
}
bool Same(float left, float right) { return std::memcmp(&left, &right, sizeof(float)) == 0; }
Frame RenderOne(MarembaEngine& engine) {
    Frame frame;
    engine.RenderBatch(&frame[0], &frame[1], &frame[2], &frame[3], &frame[4], &frame[5], &frame[6], 1);
    for (int channel = 0; channel < 7; ++channel)
        Require(std::isfinite(frame[channel]) && std::abs(frame[channel]) <=
                (channel < 2 ? kMainPeakCeiling + 2e-6f : .940002f), "nonfinite or unprotected output");
    return frame;
}

void ExactBypass() {
    MonoOversampler dynamic, fixed;
    StereoOversampler stereo;
    // Dirty every half-band stage first. Native rate must neither reveal its
    // pending samples nor update that state while it forwards an input.
    std::array<float, 8> left {{1, -.2f, .3f, -.4f, .5f, -.6f, .7f, -.8f}}, right {{-.5f}};
    for (int i = 0; i < 16; ++i) {
        dynamic.DownsampleFrame(left.data(), 8); fixed.DownsampleFrame<8>(left.data());
        float l, r; stereo.DownsampleFrame(left.data(), right.data(), 8, l, r);
    }
    auto continued = dynamic;
    auto continuedStereo = stereo;
    for (int i = 0; i < 512; ++i) {
        left[0] = i == 0 ? 1.f : i == 1 ? -0.f : static_cast<float>(.2 * std::sin(i * .731));
        right[0] = i == 0 ? -.25f : i == 1 ? 0.f : -left[0] * .5f;
        Require(Same(dynamic.DownsampleFrame(left.data(), 1), left[0]), "dynamic mono 1x adds filtering or delay");
        Require(Same(fixed.DownsampleFrame<1>(left.data()), left[0]), "template mono 1x adds filtering or delay");
        float l, r; stereo.DownsampleFrame(left.data(), right.data(), 1, l, r);
        Require(Same(l, left[0]) && Same(r, right[0]), "stereo 1x changes samples, signed zero or arrival");
    }
    for (int i = 0; i < 32; ++i) {
        Require(Same(dynamic.DownsampleFrame(left.data(), 8), continued.DownsampleFrame(left.data(), 8)),
                "1x mono modifies hidden half-band state");
        float l, r, expectedL, expectedR;
        stereo.DownsampleFrame(left.data(), right.data(), 8, l, r);
        continuedStereo.DownsampleFrame(left.data(), right.data(), 8, expectedL, expectedR);
        Require(Same(l, expectedL) && Same(r, expectedR), "1x stereo modifies hidden half-band state");
    }
}

void QualityPolicy(double rate) {
    Require(EngineParameters{}.oversampling == 3 && kControlDefaults[kOversampling] == 3,
            "native rate is not the engine/control default");
    for (int model = 0; model < 4; ++model) {
        for (int quality = 0; quality < 4; ++quality) {
            auto engine = std::make_unique<MarembaEngine>(rate);
            EngineParameters p; p.model = model; p.oversampling = quality;
            engine->SetParameters(p);
            Require(MarembaEngineTestAccess::Factor(*engine) == kFactors[quality], "legacy or native quality factor changed");
            engine->NoteOn(60, .8f);
            double energy = 0;
            for (int frame = 0; frame < static_cast<int>(rate * .03); ++frame) {
                const auto output = RenderOne(*engine);
                energy += double(output[0]) * output[0] + double(output[1]) * output[1];
            }
            Require(energy > 1e-10, "quality policy fixture rendered silence");
        }
        for (int from = 0; from < 4; ++from) for (int to = 0; to < 4; ++to) {
            if (from == to) continue;
            auto engine = std::make_unique<MarembaEngine>(rate);
            EngineParameters p; p.model = model; p.oversampling = from;
            engine->SetParameters(p); engine->NoteOn(48, .8f);
            for (int frame = 0; frame < 256; ++frame) RenderOne(*engine);
            p.oversampling = to; engine->SetParameters(p);
            Require(MarembaEngineTestAccess::Switching(*engine), "active quality change failed to fade");
            engine->NoteOn(60, .8f); // A new key belongs to the new quality.
            double energy = 0;
            for (int frame = 0; frame < static_cast<int>(rate * .03); ++frame) {
                const auto output = RenderOne(*engine);
                if (frame > rate * .01) energy += double(output[0]) * output[0] + double(output[1]) * output[1];
            }
            Require(!MarembaEngineTestAccess::Switching(*engine)
                    && MarembaEngineTestAccess::Factor(*engine) == kFactors[to], "quality fade failed to settle at the target factor");
            Require(engine->GetActiveVoiceCount() > 0 && energy > 1e-10, "quality switch lost its queued strike");
        }
    }
}

void MainDirectAlignment(double rate) {
    for (int model = 0; model < 4; ++model) for (int quality = 0; quality < 4; ++quality) {
        auto engine = std::make_unique<MarembaEngine>(rate);
        EngineParameters p; p.model = model; p.oversampling = quality;
        p.closeLevel = 1; p.farLevel = p.piezoLevel = 0; p.stereoWidth = 1;
        p.preampDrive = p.warmth = p.compAmount = 0; p.volume = .1f;
        p.strikeJitter = p.artifacts = p.detune = p.sympathetic = p.bodyBloom = 0;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
        engine->SetParameters(p); engine->NoteOn(60, .12f);
        const double trim = model == 3 ? MarembaEngine::kKalimbaCloseStemTrim : MarembaEngine::kCloseStemTrim;
        const double gain = 1.202853 * .7 * kMainModelCalibration[model] * MainVolumeGain(p.volume);
        const double pole = static_cast<float>(std::exp(-2 * 3.14159265358979323846 * 20 / rate));
        double previousInput[2]{}, previousOutput[2]{}, energy = 0, error = 0;
        for (int frame = 0; frame < static_cast<int>(rate * .08); ++frame) {
            const auto output = RenderOne(*engine);
            for (int channel = 0; channel < 2; ++channel) {
                Require(std::abs(output[channel]) < .1f, "alignment fixture reaches Main protection");
                const double input = output[channel + 2] / trim;
                const double filtered = input - previousInput[channel] + pole * previousOutput[channel];
                previousInput[channel] = input; previousOutput[channel] = filtered;
                const double expected = filtered * gain, difference = output[channel] - expected;
                energy += expected * expected; error += difference * difference;
            }
        }
        Require(energy > 1e-12 && error < 1e-8 * energy, "Main and direct Close differ in arrival or the declared transfer");
    }
}

void NativeDefaultSafety(double rate) {
    for (int model = 0; model < 4; ++model) {
        auto engine = std::make_unique<MarembaEngine>(rate);
        EngineParameters p; p.model = model; p.volume = 1;
        p.closeLevel = p.farLevel = p.piezoLevel = 1; p.polyphony = 2;
        p.malletType = 3; p.malletHardness = 1;
        engine->SetParameters(p);
        Require(MarembaEngineTestAccess::Factor(*engine) == 1, "default strike secretly oversamples");
        for (int note : {0, 12, 24, 36, 48, 60, 72, 96, 127}) engine->NoteOn(note, 1);
        double energy = 0;
        for (int frame = 0; frame < static_cast<int>(rate * .20); ++frame) {
            const double before = MarembaEngineTestAccess::Clock(*engine);
            const bool active = !engine->IsSilent();
            const auto output = RenderOne(*engine);
            Require(!active || MarembaEngineTestAccess::Clock(*engine) > before,
                    "native hardest-hit audio was concealed by an emergency reset");
            for (float value : output) energy += double(value) * value;
        }
        Require(energy > 1e-8, "native hardest-hit fixture is silent");
    }
}

struct Timing { double cpu, wall, energy; };
double ThreadCpuSeconds() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    Require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) != 0,
            "cannot read benchmark thread CPU time");
    const auto ticks = [](FILETIME value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) * 1e-7;
#else
    timespec value{};
    Require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0, "cannot read benchmark thread CPU time");
    return static_cast<double>(value.tv_sec) + value.tv_nsec * 1e-9;
#endif
}
Timing TimeWorkload(double rate, int model, int quality) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    EngineParameters p; p.model = model; p.oversampling = quality; p.polyphony = 1;
    engine->SetParameters(p);
    Require(MarembaEngineTestAccess::Factor(*engine) == kFactors[quality], "benchmark uses the wrong physical quality factor");
    for (int i = 0; i < 16; ++i) engine->NoteOn(42 + i, .8f);
    std::array<std::array<float, 64>, 7> output;
    const auto block = [&](int index) {
        engine->SetParameters(p);
        if (index % 32 == 0) engine->NoteOn(42 + (index / 32) % 16, .8f);
        engine->RenderBatch(output[0].data(), output[1].data(), output[2].data(), output[3].data(),
                            output[4].data(), output[5].data(), output[6].data(), 64);
    };
    for (int i = 0; i < 128; ++i) block(i);
    Require(MarembaEngineTestAccess::LiveVoices(*engine) == 16, "benchmark did not retain sixteen nonfading voices after warmup");
    const auto wall = std::chrono::steady_clock::now();
    const double cpu = ThreadCpuSeconds();
    double energy = 0;
    for (int i = 0; i < 512; ++i) {
        block(i);
        for (float value : output[0]) {
            Require(std::isfinite(value), "benchmark output is nonfinite");
            energy += double(value) * value;
        }
    }
    const Timing result{ThreadCpuSeconds() - cpu,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - wall).count(), energy};
    Require(MarembaEngineTestAccess::LiveVoices(*engine) == 16, "benchmark lost its sixteen nonfading voices during measurement");
    Require(MarembaEngineTestAccess::Factor(*engine) == kFactors[quality], "benchmark changed physical quality factor during measurement");
    return result;
}
void Benchmark() {
    std::puts("rate,model,voices,block_frames,measured_blocks,pairs,one_x_cpu_seconds,two_x_cpu_seconds,cpu_ratio,one_x_wall_seconds,two_x_wall_seconds");
    for (double rate : {44100., 48000.}) for (int model = 0; model < 4; ++model) {
        std::array<double, 3> one{}, two{}, wallOne{}, wallTwo{};
        for (int pair = 0; pair < 3; ++pair) for (int pass = 0; pass < 2; ++pass) {
            const bool native = (pair + pass) % 2 == 0;
            const auto timing = TimeWorkload(rate, model, native ? 3 : 0);
            Require(timing.energy > 1e-8 && timing.cpu > 0, "benchmark workload is silent or unmeasured");
            (native ? one : two)[pair] = timing.cpu;
            (native ? wallOne : wallTwo)[pair] = timing.wall;
        }
        for (auto* values : {&one, &two, &wallOne, &wallTwo}) std::sort(values->begin(), values->end());
        std::printf("%.0f,%d,16,64,512,3,%.9f,%.9f,%.6f,%.9f,%.9f\n", rate, model,
                    one[1], two[1], one[1] / two[1], wallOne[1], wallTwo[1]);
    }
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") { Benchmark(); return 0; }
    if (argc != 1) return 2;
    ExactBypass();
    for (double rate : {22050., 44100., 48000.}) {
        QualityPolicy(rate); MainDirectAlignment(rate); NativeDefaultSafety(rate);
    }
    std::printf("PASS OneXQualityTest: exact bypass, four factors, active switches, aligned captures and native safety; checks=%llu\n",
                static_cast<unsigned long long>(checks));
}
