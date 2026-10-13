// Public-engine contract: ROOM maps knob travel to a squared capture gain.
// The underlying room keeps rendering identically through mute and automation.
#include "MarembaEngine.h"
#include "OutputHeadroom.h"
#include "AnalogPreamp.h"
#include "MainStereo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 4>; // Main L/R, direct Room L/R
int checks = 0;
void require(bool pass, const char* message) {
    ++checks;
    if (!pass) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

Audio render(double rate, float room, bool automate = false) {
    EngineParameters p;
    p.closeLevel = p.piezoLevel = 0; p.farLevel = room;
    p.volume = .05f; p.compAmount = p.preampDrive = p.warmth = 0;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.bodyBloom = p.sympathetic = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.stereoWidth = 1; p.decay = 1;
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(p); engine->NoteOn(60, .25f);
    const int frames = static_cast<int>(rate * .26);
    const int up = static_cast<int>(rate * .06), down = static_cast<int>(rate * .16);
    Audio audio;
    for (auto& channel : audio) channel.resize(static_cast<size_t>(frames));
    for (int frame = 0; frame < frames;) {
        if (automate && (frame == up || frame == down)) {
            p.farLevel = frame == up ? .75f : .25f;
            engine->SetParameters(p);
        }
        int end = frames;
        if (automate) end = frame < up ? up : frame < down ? down : frames;
        const int count = std::min(64, end-frame);
        engine->RenderBatch(audio[0].data()+frame, audio[1].data()+frame,
                            nullptr, nullptr, audio[2].data()+frame, audio[3].data()+frame,
                            nullptr, count);
        frame += count;
    }
    for (const auto& channel : audio) for (float sample : channel)
        require(std::isfinite(sample) && std::abs(sample) < .05f,
                "room-taper fixture is nonfinite or too hot for a clean gain comparison");
    return audio;
}

void checkDirect(const Audio& reference, const Audio& actual) {
    require(reference[2] == actual[2] && reference[3] == actual[3],
            "ROOM knob modifies or interrupts the direct room capture");
}

void checkStatic(double rate) {
    const auto reference = render(rate, 1);
    double referenceEnergy = 0;
    for (int c = 0; c < 2; ++c) for (float sample : reference[c]) referenceEnergy += double(sample)*sample;
    require(referenceEnergy > 1.e-12, "room-only reference is silent");
    for (float knob : {0.f, .25f, .5f, .75f}) {
        const auto actual = render(rate, knob);
        checkDirect(reference, actual);
        double error = 0, actualEnergy = 0;
        for (int c = 0; c < 2; ++c) for (size_t i = 0; i < actual[c].size(); ++i) {
            const double expected = double(knob)*knob*reference[c][i];
            const double difference = actual[c][i]-expected;
            error += difference*difference;
            actualEnergy += double(actual[c][i])*actual[c][i];
        }
        require(error < 1.e-12*referenceEnergy,
                "ROOM main gain is not the square of normalized knob position");
        if (knob == 0) require(actualEnergy == 0, "ROOM zero is not exact mute on Main");
    }
    const auto moving = render(rate, 0, true);
    checkDirect(reference, moving);
    const int latency = OutputHeadroom::LatencyFrames(rate);
    const int up = static_cast<int>(rate*.06)+latency;
    const int down = static_cast<int>(rate*.16)+latency;
    // The common Main image and DC blocker follow the coefficient, so a
    // changing gain does not commute through either filter history. Rebuild
    // that verified public signal chain from the unchanged direct Room, which
    // already carries the same lookahead delay. MainStereo has a separate
    // independent analog-network oracle in MainStereoTest.
    MainStereo fullStereo, movingStereo, steppedStereo;
    for (auto* stereo : {&fullStereo, &movingStereo, &steppedStereo}) {
        stereo->SetSampleRate(rate); stereo->Reset();
    }
    AnalogPreamp fullFilter, movingFilter, steppedFilter;
    for (auto* filter : {&fullFilter, &movingFilter, &steppedFilter}) {
        filter->SetSampleRate(rate); filter->Reset();
    }
    Audio expected;
    for (auto& channel : expected) channel.resize(reference[0].size());
    double referenceDot = 0, fullEnergy = 0;
    for (size_t i = 0; i < reference[0].size(); ++i) {
        float fullL = reference[2][i], fullR = reference[3][i];
        fullStereo.Process(fullL, fullR, 1);
        fullFilter.Process(fullL, fullR, 0, 0, fullL, fullR);
        referenceDot += double(reference[0][i])*fullL+double(reference[1][i])*fullR;
        fullEnergy += double(fullL)*fullL+double(fullR)*fullR;
        const int frame = static_cast<int>(i);
        // Independent continuous-time 5ms settling target, with exact zero
        // before the first event and the squared endpoints above.
        const double target = frame < up ? 0 : frame < down ? .75*.75 : .25*.25;
        const double coefficient = frame < up ? 0 : frame < down
            ? .75*.75*(1-std::exp(-(frame-up+1)/(rate*.005)))
            : .25*.25+(.75*.75-.25*.25)*std::exp(-(frame-down+1)/(rate*.005));
        float movingL = reference[2][i]*coefficient, movingR = reference[3][i]*coefficient;
        movingStereo.Process(movingL, movingR, 1);
        movingFilter.Process(movingL, movingR, 0, 0, expected[0][i], expected[1][i]);
        float steppedL = reference[2][i]*target, steppedR = reference[3][i]*target;
        steppedStereo.Process(steppedL, steppedR, 1);
        steppedFilter.Process(steppedL, steppedR, 0, 0, expected[2][i], expected[3][i]);
    }
    require(fullEnergy > 1.e-9, "room automation reference is silent");
    const double mainScale = referenceDot/fullEnergy; // fixed calibration cancels
    double error = 0, steppedError = 0, energy = 0;
    for (int c = 0; c < 2; ++c) for (size_t i = 0; i < reference[c].size(); ++i) {
        const double sample = expected[c][i]*mainScale;
        const double difference = moving[c][i]-sample;
        const double wrong = moving[c][i]-expected[c+2][i]*mainScale;
        error += difference*difference; steppedError += wrong*wrong; energy += sample*sample;
    }
    require(energy > 1.e-12 && error < 1.e-8*energy,
            "ROOM automation steps, uses linear travel, or changes its smoothing time");
    require(steppedError > 1.e-3*energy, "automation fixture cannot distinguish an abrupt coefficient change");
    for (int c = 0; c < 2; ++c) for (int i = 0; i < up; ++i)
        require(moving[c][static_cast<size_t>(i)] == 0, "ROOM unmutes before its control event and declared latency");
    std::cout << "ROOM_TAPER rate=" << rate << " coefficients=0,.0625,.25,.5625,1 direct=exact automation=5ms\n";
}
}

int main() {
    for (double rate : {44100., 48000., 96000.}) checkStatic(rate);
    std::cout << "RoomLevelTaperTest PASSED checks=" << checks << '\n';
}
