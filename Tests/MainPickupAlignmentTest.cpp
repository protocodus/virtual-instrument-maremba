// Main compensates acoustic flight; the raw electrical stem remains clean.
// Frequency-domain oracles use geometric phase, not the delay implementation.
#include "MarembaEngine.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static float MainModelGain(const MarembaEngine& engine) { return engine.mMainModelGain; }
    static void Fade(MarembaEngine& engine) {
        for (auto& voice : engine.mVoices) if (voice.IsActive()) voice.StartFade(.003);
    }
    static int SlotFor(const MarembaEngine& engine, int note) {
        for (int i = 0; i < MarembaEngine::kVoicePoolSize; ++i)
            if (engine.mVoices[i].IsActive() && engine.mVoices[i].GetNoteNumber() == note) return i;
        return -1;
    }
    static int NoteInSlot(const MarembaEngine& engine, int slot) { return engine.mVoices[slot].GetNoteNumber(); }
};
}

namespace {
using namespace maremba;
int checks = 0;
void Require(bool condition, const char* text) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}
std::complex<double> Transform(const std::vector<float>& x, double hz, double rate) {
    const auto step = std::polar(1.0, -2 * kPiD * hz / rate);
    std::complex<double> phase{1, 0}, sum{};
    for (float sample : x) { sum += double(sample) * phase; phase *= step; }
    return sum;
}

void PhysicalFlight(double rate, int factor, int source) {
    auto alignment = std::make_unique<MainPickupAlignment>();
    alignment->Allocate(rate);
    MonoOversampler direct;
    std::vector<float> raw(2048), aligned(2048), replay(2048);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) { alignment->Reset(); direct.Reset(); }
        for (int frame = 0; frame < 2048; ++frame) {
            std::array<float, 8> sub{};
            if (frame == 0) sub[0] = .25f;
            alignment->BeginFrame(factor);
            for (int s = 0; s < factor; ++s) alignment->Add(source, s, sub[s]);
            const float out = alignment->Process();
            if (pass) replay[frame] = out;
            else { raw[frame] = direct.DownsampleFrame(sub.data(), factor); aligned[frame] = out; }
        }
    }
    Require(aligned == replay, "pickup alignment reset is not exact replay");
    Require(aligned.back() == 0 && alignment->HasNoSubnormalState(), "pickup travelling tail did not retire cleanly");
    const double seconds = CaptureDistance(CaptureSourcePosition(source), CloseHeadCentre()) / kCaptureSpeedOfSound;
    for (double hz : {100., 500., 1000., 4000.}) {
        const auto observed = Transform(aligned, hz, rate) / Transform(raw, hz, rate);
        const auto expected = std::polar(1.0, -2 * kPiD * hz * seconds);
        if (std::abs(std::abs(observed) - 1) >= .003 || std::abs(std::arg(observed / expected)) >= .003)
            std::cerr << "flight rate=" << rate << " factor=" << factor << " source=" << source
                      << " hz=" << hz << " magnitude=" << std::abs(observed)
                      << " phase_error=" << std::arg(observed / expected) << '\n';
        Require(std::abs(std::abs(observed) - 1) < .003, "pickup alignment changes the pickup spectrum");
        Require(std::abs(std::arg(observed / expected)) < .003, "pickup alignment flight differs from source geometry");
    }
}

void TravellingKeys() {
    auto first = std::make_unique<MainPickupAlignment>();
    auto second = std::make_unique<MainPickupAlignment>();
    auto together = std::make_unique<MainPickupAlignment>();
    for (auto* path : {first.get(), second.get(), together.get()}) path->Allocate(48000);
    double energy = 0;
    for (int frame = 0; frame < 2048; ++frame) {
        for (auto* path : {first.get(), second.get(), together.get()}) path->BeginFrame(4);
        // The second key starts before the first key's wave arrives. Add in
        // reverse key order to exercise discovery without clearing that wave.
        if (frame == 2) { second->Add(96, 0, -.2f); together->Add(96, 0, -.2f); }
        if (frame == 0 || frame == 2) { first->Add(36, 0, .1f); together->Add(36, 0, .1f); }
        const float expected = first->Process() + second->Process();
        const float actual = together->Process();
        Require(std::abs(actual - expected) < 1.e-7f, "new or repeated key erases another travelling pickup wave");
        energy += double(actual) * actual;
    }
    Require(energy > 1.e-5, "travelling-key fixture is silent");
}

using Audio = std::array<std::vector<float>, 7>;

EngineParameters TransitionParameters() {
    EngineParameters p;
    p.closeLevel = p.farLevel = 0; p.piezoLevel = 1; p.volume = .02f;
    p.preampDrive = p.compAmount = p.warmth = 0;
    p.strikeJitter = p.detune = p.bodyBloom = p.sympathetic = p.artifacts = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.polyphony = 0;
    return p;
}

void SlotReusePreservesFlight() {
    auto changed = std::make_unique<MarembaEngine>(48000);
    auto reference = std::make_unique<MarembaEngine>(48000);
    auto p = TransitionParameters();
    for (auto* engine : {changed.get(), reference.get()}) {
        engine->SetParameters(p); engine->Reset();
        // Fill both live and fading slots. At 2 ms none of their 3 ms steal
        // fades has ended, so the next note must physically reuse a slot.
        for (int note = 36; note < 36 + MarembaEngine::kVoicePoolSize; ++note) engine->NoteOn(note, .12f);
        for (int i = 0; i < 96; ++i) {
            float left, right; engine->RenderBatch(&left, &right, nullptr, nullptr, nullptr, nullptr, nullptr, 1);
        }
        Require(engine->GetActiveVoiceCount() == MarembaEngine::kVoicePoolSize, "slot-reuse fixture did not fill the physical pool");
    }
    p.model = 1; changed->SetParameters(p); changed->NoteOn(110, .12f);
    const int replaced = MarembaEngineTestAccess::SlotFor(*changed, 110);
    Require(replaced >= 0 && MarembaEngineTestAccess::NoteInSlot(*reference, replaced) != 110,
            "slot-reuse fixture did not replace an old key with a new model");
    // The earliest differing source sample still needs >1.5 ms of physical
    // flight. Inspect the old wave after the common 2 ms output lookahead,
    // when clearing a per-key queue would already have become audible.
    double oldWaveEnergy = 0;
    std::array<float, 160> recordingGains{};
    const float oldRecordingGain = MarembaEngineTestAccess::MainModelGain(*reference);
    const int outputDelay = OutputHeadroom::LatencyFrames(48000);
    for (int i = 0; i < 160; ++i) {
        float actualL, actualR, expectedL, expectedR;
        changed->RenderBatch(&actualL, &actualR, nullptr, nullptr, nullptr, nullptr, nullptr, 1);
        reference->RenderBatch(&expectedL, &expectedR, nullptr, nullptr, nullptr, nullptr, nullptr, 1);
        // Model selection now changes a smooth recording gain. Remove only
        // that scalar at the output's known delay: the old travelling waveform
        // must still survive the model change and reuse of its physical slot.
        recordingGains[i] = MarembaEngineTestAccess::MainModelGain(*changed);
        const double ratio = i < outputDelay ? 1.0 : recordingGains[i-outputDelay] / oldRecordingGain;
        const auto matches = [ratio](double actual, double expected) {
            return std::abs(actual - expected*ratio) <= 1.e-12 + std::abs(expected*ratio)*1.e-6;
        };
        Require(matches(actualL, expectedL) && matches(actualR, expectedR),
                "model change or slot reuse erases a travelling pickup wave");
        if (i >= 96) oldWaveEnergy += double(expectedL)*expectedL + double(expectedR)*expectedR;
    }
    Require(oldWaveEnergy > 1.e-12, "slot-reuse arrival window contains no old pickup sound");
}

Audio SplitReplay(int batch) {
    auto engine = std::make_unique<MarembaEngine>(48000);
    auto p = TransitionParameters(); engine->SetParameters(p); engine->Reset();
    constexpr int frames = 1024;
    Audio audio; for (auto& channel : audio) channel.resize(frames);
    for (int frame = 0; frame < frames;) {
        if (frame == 0) for (int note = 36; note < 44; ++note) engine->NoteOn(note, .12f);
        if (frame == 128) for (int note = 44; note < 68; ++note) engine->NoteOn(note, .12f);
        if (frame == 256) { p.model = 1; engine->SetParameters(p); engine->NoteOn(36, .12f); }
        if (frame == 384) for (int note = 68; note < 116; ++note) engine->NoteOn(note, .12f);
        if (frame == 512) { p.model = 2; engine->SetParameters(p); engine->NoteOn(72, .12f); }
        if (frame == 640) engine->DampAll();
        const int boundary = ((frame / 128) + 1) * 128;
        const int count = std::min({batch, frames-frame, boundary-frame});
        engine->RenderBatch(audio[0].data()+frame, audio[1].data()+frame, audio[2].data()+frame,
                            audio[3].data()+frame, audio[4].data()+frame, audio[5].data()+frame,
                            audio[6].data()+frame, count);
        frame += count;
    }
    return audio;
}

Audio Render(double rate, int model, float pickup) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    EngineParameters p;
    p.oversampling = 0; // Keep the existing declared-gain routing fixture at 2x.
    p.model = model; p.closeLevel = p.farLevel = 0; p.piezoLevel = pickup;
    p.volume = .08f; p.preampDrive = p.compAmount = p.warmth = 0;
    p.strikeJitter = p.detune = p.bodyBloom = p.sympathetic = p.artifacts = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    engine->SetParameters(p); engine->Reset(); engine->NoteOn(60, .12f);
    const int frames = static_cast<int>(std::ceil(rate * .4));
    const int fade = static_cast<int>(std::ceil(rate * .04));
    Audio audio;
    for (auto& channel : audio) channel.resize(frames);
    for (int i = 0; i < frames; ++i) {
        if (i == fade) MarembaEngineTestAccess::Fade(*engine);
        engine->RenderBatch(audio[0].data()+i, audio[1].data()+i, audio[2].data()+i,
                            audio[3].data()+i, audio[4].data()+i, audio[5].data()+i,
                            audio[6].data()+i, 1);
    }
    for (const auto& channel : audio) for (float sample : channel)
        Require(std::isfinite(sample) && std::abs(sample) < .1, "pickup fixture reaches output protection");
    return audio;
}

void EngineRouting(double rate, int model) {
    const auto audible = Render(rate, model, 1), muted = Render(rate, model, 0);
    for (int channel = 2; channel < 7; ++channel)
        Require(audible[channel] == muted[channel], "Main pickup mix changes a raw direct capture");
    for (float sample : muted[0]) Require(sample == 0, "zero pickup mix is not exact mute");
    AnalogPreamp filter; filter.SetSampleRate(rate); filter.Reset();
    std::vector<float> reference(audible[6].size());
    for (std::size_t i = 0; i < reference.size(); ++i) {
        float right;
        filter.Process(audible[6][i], audible[6][i], 0, 0, reference[i], right);
    }
    const auto position = model == 3 ? KalimbaBoardSourcePosition()
                                   : CaptureSourcePosition(static_cast<EMarimbaModel>(model), 60);
    const double seconds = CaptureDistance(position, CloseHeadCentre()) / kCaptureSpeedOfSound;
    const double trim = model == 3 ? MarembaEngine::kKalimbaPiezoStemTrim : MarembaEngine::kPiezoStemTrim;
    // Independent installed 1.5.5 Main calibration and default-off Volume
    // transfer. Keep declared fixture values separate from production constants.
    constexpr double modelGain[] = {2.156637999, 2.113276235, 2.805121587, 2.111566469};
    const double scale = .707 * (.08 / .85) * (.08 / .85) * .7 * 1.202853 * modelGain[model] / trim;
    for (double hz : {100., 261.625565, 500., 1000.}) {
        const auto r = Transform(reference, hz, rate);
        Require(std::abs(r) > 1.e-6, "pickup routing fixture is silent at a reference frequency");
        const auto observed = Transform(audible[0], hz, rate) / (r * scale);
        const auto expected = std::polar(1.0, -2 * kPiD * hz * seconds);
        Require(std::abs(std::abs(observed) - 1) < .01, "Main pickup gain differs from the independent electrical capture");
        Require(std::abs(std::arg(observed / expected)) < .015, "engine bypasses or misroutes Main pickup flight");
    }
}
}

int main() {
    TravellingKeys();
    SlotReusePreservesFlight();
    const auto split = SplitReplay(1);
    Require(split == SplitReplay(7) && split == SplitReplay(64),
            "capture alignment depends on host batch splitting during note/model/steal transitions");
    for (double rate : {44100., 48000., 96000.}) {
        for (int factor : {1, 2, 4, 8}) for (int source : {36, 60, 96, 188, 316, kCaptureBoardSource})
            PhysicalFlight(rate, factor, source);
        for (int model = 0; model < 4; ++model) EngineRouting(rate, model);
    }
    std::cout << "PASS Main pickup physical-flight routing, raw direct isolation, rate/quality/reset: " << checks << " checks\n";
}
