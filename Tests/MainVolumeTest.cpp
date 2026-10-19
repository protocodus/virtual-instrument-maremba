// Nominal Main level at 85%, useful lower travel, and smoothed boost reserve.
#include "MarembaEngine.h"
#include "MainVolume.h"
#include "OutputHeadroom.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 7>;
void Require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void Curve() {
    Require(MainVolumeGain(0) == 0 && MainVolumeGain(.85f) == 1 && MainVolumeGain(1) == 2.25f,
            "mute, nominal or maximum gain changed");
    for (const auto& anchor : std::array<std::array<double, 2>, 3>{{
             {{.5, .34602076124567477}}, {{.75, .7785467128027681}}, {{.95, 1.550230189074722}}}})
        Require(std::abs(MainVolumeGain(anchor[0]) - anchor[1]) < 2e-6,
                "Main volume amplitude anchor changed");
    Require(MainVolumeGain(-1) == 0 && MainVolumeGain(2) == 2.25f
            && MainVolumeGain(std::numeric_limits<float>::quiet_NaN()) == 0
            && MainVolumeGain(std::numeric_limits<float>::infinity()) == 0,
            "invalid volume must remain bounded");
    float previous = 0;
    for (int i = 1; i <= 10000; ++i) {
        const float gain = MainVolumeGain(i / 10000.f);
        Require(gain > previous && gain <= 2.25f, "Volume must increase monotonically");
        previous = gain;
    }
    constexpr float h = .0001f;
    const double left = (MainVolumeGain(.85f) - MainVolumeGain(.85f - h)) / h;
    const double right = (MainVolumeGain(.85f + h) - MainVolumeGain(.85f)) / h;
    Require(std::abs(left - 2 / .85) < .007 && std::abs(right - left) < .01,
            "nominal-to-boost join must preserve slope and level");
}

Audio Render(double rate, float volume, bool moving = false) {
    EngineParameters p;
    p.volume = volume; p.closeLevel = 1; p.farLevel = p.piezoLevel = 0;
    p.preampDrive = p.compAmount = p.warmth = 0;
    p.artifacts = p.strikeJitter = p.detune = p.bodyBloom = p.sympathetic = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(p); engine->NoteOn(60, .12f);
    const int frames = static_cast<int>(rate * .3);
    const int up = static_cast<int>(rate * .06), down = static_cast<int>(rate * .16);
    Audio audio;
    for (auto& channel : audio) channel.resize(frames);
    for (int frame = 0; frame < frames;) {
        if (moving && (frame == up || frame == down)) {
            p.volume = frame == up ? .85f : .5f;
            engine->SetParameters(p);
        }
        const int end = !moving ? frames : frame < up ? up : frame < down ? down : frames;
        const int count = std::min(64, end - frame);
        engine->RenderBatch(audio[0].data()+frame, audio[1].data()+frame,
                           audio[2].data()+frame, audio[3].data()+frame,
                           audio[4].data()+frame, audio[5].data()+frame,
                           audio[6].data()+frame, count);
        frame += count;
    }
    for (const auto& channel : audio) for (float sample : channel)
        Require(std::isfinite(sample) && std::abs(sample) < .2f,
                "Volume fixture must remain below protection");
    return audio;
}

void Engine(double rate) {
    const auto nominal = Render(rate, .85f);
    for (float knob : {0.f, .425f, .5f, .95f, 1.f}) {
        const auto actual = Render(rate, knob);
        for (int channel = 2; channel < 7; ++channel)
            Require(nominal[channel] == actual[channel], "Volume modifies a direct capture");
        // Independent amplitude anchors, not the helper used by the engine.
        const double gain = knob == 0 ? 0 : knob == .425f ? .25 : knob == .5f
            ? .34602076124567477 : knob == .95f ? 1.550230189074722 : 2.25;
        double error = 0, energy = 0;
        for (int c = 0; c < 2; ++c) for (size_t i = 0; i < actual[c].size(); ++i) {
            const double difference = actual[c][i] - nominal[c][i] * gain;
            error += difference * difference;
            energy += double(nominal[c][i]) * nominal[c][i];
        }
        Require(energy > 1e-10 && error < energy * 1e-10, "engine does not use the declared Volume curve");
    }
    const auto moving = Render(rate, 0, true);
    for (int channel = 2; channel < 7; ++channel)
        Require(nominal[channel] == moving[channel], "Volume automation changes a direct capture");
    const int delay = OutputHeadroom::LatencyFrames(rate);
    const int up = static_cast<int>(rate * .06) + delay;
    const int down = static_cast<int>(rate * .16) + delay;
    const double alpha = 1 - std::exp(-1 / (.005 * rate));
    double gain = 0, error = 0, stepped = 0, energy = 0;
    for (size_t i = 0; i < nominal[0].size(); ++i) {
        const double target = int(i) < up ? 0 : int(i) < down ? 1 : .34602076124567477;
        gain += alpha * (target - gain);
        for (int c = 0; c < 2; ++c) {
            const double expected = nominal[c][i] * gain;
            const double difference = moving[c][i] - expected;
            const double wrong = moving[c][i] - nominal[c][i] * target;
            error += difference * difference; stepped += wrong * wrong; energy += expected * expected;
        }
    }
    Require(energy > 1e-10 && error < 1e-8 * energy, "Volume automation differs from 5ms amplitude smoothing");
    Require(stepped > 1e-3 * energy, "fixture cannot distinguish abrupt Volume automation");
    std::printf("Main Volume %.0f Hz: nominal, boost, direct isolation and automation passed\n", rate);
}
}

int main() {
    Curve();
    for (double rate : {44100., 48000., 96000.}) Engine(rate);
}
