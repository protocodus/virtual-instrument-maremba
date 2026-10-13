// Public-engine contract for polarity: clean capture inversion, isolated
// microphone paths and independently derived five-millisecond automation.
#include "MarembaEngine.h"
#include "OutputHeadroom.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 7>;
int checks = 0;
void require(bool pass, const char* text) {
    ++checks;
    if (!pass) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}
double energy(const std::vector<float>& audio) {
    double result = 0;
    for (float sample : audio) result += double(sample) * sample;
    return result;
}
EngineParameters parameters(int model, int quality) {
    EngineParameters p;
    p.model = model; p.oversampling = quality;
    p.closeLevel = .4f; p.farLevel = .3f; p.piezoLevel = .7f;
    p.volume = .02f; p.compAmount = p.preampDrive = p.warmth = 0;
    p.artifacts = p.strikeJitter = p.detune = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    return p;
}
Audio render(double rate, EngineParameters p, bool automate = false, bool reset = false) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(p);
    if (reset) engine->Reset();
    engine->NoteOn(60, .12f);
    const int frames = int(rate * .18), up = int(rate * .045), down = int(rate * .11);
    Audio audio;
    for (auto& channel : audio) channel.resize(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames;) {
        if (automate && (frame == up || frame == down)) {
            p.invertPiezo = frame == up ? 1 : 0;
            engine->SetParameters(p);
        }
        const int end = !automate ? frames : frame < up ? up : frame < down ? down : frames;
        const int count = std::min(64, end - frame);
        engine->RenderBatch(audio[0].data() + frame, audio[1].data() + frame,
                            audio[2].data() + frame, audio[3].data() + frame,
                            audio[4].data() + frame, audio[5].data() + frame,
                            audio[6].data() + frame, count);
        frame += count;
    }
    for (const auto& channel : audio) for (float sample : channel)
        require(std::isfinite(sample) && std::abs(sample) < .5f,
                "polarity fixture is nonfinite or too hot for a clean comparison");
    return audio;
}
void microphonesExact(const Audio& a, const Audio& b) {
    for (int c = 2; c < 6; ++c)
        require(a[c] == b[c], "Invert Piezo changes a direct Close/Room microphone");
}
void staticPolarity(double rate, int model, int quality) {
    auto p = parameters(model, quality);
    const auto off = render(rate, p);
    require(energy(off[6]) > 1.e-10, "polarity witness has no electrical signal");
    p.invertPiezo = 1;
    const auto on = render(rate, p);
    microphonesExact(off, on);
    for (std::size_t i = 0; i < off[6].size(); ++i)
        require(on[6][i] == -off[6][i], "direct Piezo is not an exact polarity inversion");
    require(on == render(rate, p, false, true), "reset does not reapply saved inverted polarity exactly");

    // The sum of both polarity choices must cancel only the pickup in a
    // clean Main mix. This also rejects inverting the whole Main output.
    p.piezoLevel = 0;
    const auto microphones = render(rate, p);
    p.invertPiezo = 0;
    const auto originalMicrophones = render(rate, p);
    require(microphones[0] == originalMicrophones[0] && microphones[1] == originalMicrophones[1],
            "polarity changes a microphone-only Main mix");
    double error = 0, signal = 0, difference = 0;
    for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < off[c].size(); ++i) {
        const double residual = .5 * (double(off[c][i]) + on[c][i]) - microphones[c][i];
        error += residual * residual;
        signal += double(microphones[c][i]) * microphones[c][i];
        const double delta = double(off[c][i]) - on[c][i]; difference += delta * delta;
    }
    require(signal > 1.e-15 && error < 1.e-9 * signal,
            "Main polarity does not invert only its aligned pickup before the preamp");
    require(difference > 1.e-12, "Invert Piezo has no Main-output witness");

    // Main coloration remains downstream; its asymmetric transfer must not
    // be inverted as a completed output. Direct channels stay clean.
    p.closeLevel = p.farLevel = 0; p.piezoLevel = 1; p.preampDrive = .9f; p.invertPiezo = 0;
    const auto drivenOff = render(rate, p);
    p.invertPiezo = 1;
    const auto drivenOn = render(rate, p);
    microphonesExact(drivenOff, drivenOn);
    require(drivenOff[6] == off[6], "Main Drive reaches direct Piezo");
    require(drivenOn[6] == on[6], "Main Drive reaches inverted direct Piezo");
    double asymmetricEnergy = 0, drivenEnergy = 0;
    for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < drivenOff[c].size(); ++i) {
        const double sum = double(drivenOff[c][i]) + drivenOn[c][i];
        asymmetricEnergy += sum * sum;
        drivenEnergy += double(drivenOff[c][i]) * drivenOff[c][i];
    }
    require(drivenEnergy > 1.e-12 && asymmetricEnergy > 1.e-10 * drivenEnergy,
            "asymmetric Main Drive does not follow polarity inversion");
    std::cout << "PIEZO_POLARITY rate=" << rate << " model=" << model
              << " quality=" << quality << " direct=negative microphones=exact main=pickup-only reset=exact\n";
}
void livePolarity(double rate, int model, int quality) {
    const auto p = parameters(model, quality);
    const auto reference = render(rate, p), moving = render(rate, p, true);
    microphonesExact(reference, moving);
    const int latency = OutputHeadroom::LatencyFrames(rate);
    const int up = int(rate * .045) + latency, down = int(rate * .11) + latency;
    double error = 0, abruptError = 0, signal = 0;
    const double atDown = -1 + 2 * std::exp(-(down - up) / (rate * .005));
    for (std::size_t i = 0; i < reference[6].size(); ++i) {
        const int frame = static_cast<int>(i);
        const double coefficient = frame < up ? 1 : frame < down
            ? -1 + 2 * std::exp(-(frame - up + 1) / (rate * .005))
            : 1 + (atDown - 1) * std::exp(-(frame - down + 1) / (rate * .005));
        const double expected = reference[6][i] * coefficient;
        const double delta = moving[6][i] - expected;
        const double abrupt = moving[6][i] - reference[6][i] * (frame < up || frame >= down ? 1 : -1);
        error += delta * delta; abruptError += abrupt * abrupt; signal += expected * expected;
        if (frame < up) require(reference[6][i] == moving[6][i], "polarity changes before its event and output latency");
    }
    require(signal > 1.e-10 && error < 1.e-8 * signal,
            "live polarity differs from independent five-millisecond smoothing oracle");
    require(abruptError > 1.e-3 * signal, "live fixture cannot distinguish an abrupt polarity flip");
    std::cout << "PIEZO_POLARITY_LIVE rate=" << rate << " model=" << model << " quality=" << quality
              << " oracle_error=" << error / signal << " abrupt_error=" << abruptError / signal << '\n';
}
}
int main() {
    require(EngineParameters{}.invertPiezo == 0, "original polarity is not the engine default");
    for (double rate : {44100., 192000.}) for (int model = 0; model < 4; ++model)
        for (int quality : {0, 1, 2}) staticPolarity(rate, model, quality);
    for (double rate : {44100., 96000.}) for (int model : {0, 3})
        for (int quality : {0, 2}) livePolarity(rate, model, quality);
    std::cout << "PiezoPolarityTest PASSED checks=" << checks << '\n';
}
