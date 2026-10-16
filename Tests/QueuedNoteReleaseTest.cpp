// Note Off must retain its sound behavior when a Quality fade queues the strike.
// Compare against the public NoteOn/NoteOff API at the exact queue-drain frame.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace maremba {
struct MarembaEngineTestAccess {
    static bool Switching(const MarembaEngine& engine) { return engine.mSwitchPending; }
    static bool Released(const MarembaEngine& engine, int note) {
        for (const auto& voice : engine.mVoices)
            if (voice.IsLive() && voice.GetNoteNumber() == note) return voice.IsReleased();
        return false;
    }
};
}

namespace {
int failures = 0, fixtures = 0;
void check(bool condition, const char* message) {
    if (!condition && failures++ < 16) std::cerr << "FAIL " << message << '\n';
}
using Frame = std::array<float, 7>;
Frame render(maremba::MarembaEngine& engine) {
    Frame frame{};
    engine.RenderBatch(&frame[0], &frame[1], &frame[2], &frame[3],
                       &frame[4], &frame[5], &frame[6], 1);
    return frame;
}

void run(int model, double rate, int quality, int releaseMode, int drainMode, bool cancel) {
    using namespace maremba;
    auto queued = std::make_unique<MarembaEngine>(rate);
    auto reference = std::make_unique<MarembaEngine>(rate);
    EngineParameters params;
    params.model = model;
    params.oversampling = quality;
    params.noteOffMode = releaseMode;
    params.artifacts = params.strikeJitter = 0.0f;
    params.sympathetic = params.bodyBloom = params.farLevel = 0.0f;
    params.preampDrive = params.compAmount = params.warmth = 0.0f;
    for (auto* engine : {queued.get(), reference.get()}) {
        engine->SetParameters(params);
        engine->NoteOn(48, .8f);
        for (int n = 0; n < 256; ++n) render(*engine);
    }
    params.oversampling = (quality + 1) % 3;
    for (auto* engine : {queued.get(), reference.get()}) {
        engine->SetParameters(params);
        check(MarembaEngineTestAccess::Switching(*engine), "fixture did not start a Quality fade");
        // Both queues start this held note, keeping the reference on the same
        // non-silent control grid when the fade completes.
        engine->NoteOn(64, .7f);
    }
    queued->NoteOn(62, .9f);
    queued->NoteOff(62);
    params.noteOffMode = drainMode;
    queued->SetParameters(params);
    reference->SetParameters(params);
    if (cancel) {
        // Exercise the separate cancellation path after the fade has advanced.
        for (int n = 0; n < 32; ++n) { render(*queued); render(*reference); }
        params.oversampling = quality;
        queued->SetParameters(params);
        reference->SetParameters(params);
    } else {
        int frames = 0;
        while (MarembaEngineTestAccess::Switching(*queued) && frames++ < static_cast<int>(rate * .02)) {
            render(*queued);
            render(*reference);
        }
    }
    check(!MarembaEngineTestAccess::Switching(*queued), "Quality queue did not drain");
    check(!MarembaEngineTestAccess::Switching(*reference), "reference Quality queue did not drain");
    reference->NoteOn(62, .9f);
    // The action belongs to the mode at NoteOff, even if the selector changes
    // before the queued strike starts. A held strike uses the drain-time mode.
    auto atRelease = params;
    atRelease.noteOffMode = releaseMode;
    reference->SetParameters(atRelease);
    reference->NoteOff(62);
    reference->SetParameters(params);
    check(MarembaEngineTestAccess::Released(*queued, 62), "queued note did not start released");
    check(!MarembaEngineTestAccess::Released(*queued, 64), "unreleased queued note was released");

    double signal = 0.0, difference = 0.0;
    for (int n = 0; n < 2048; ++n) {
        const Frame actual = render(*queued), expected = render(*reference);
        for (std::size_t channel = 0; channel < actual.size(); ++channel) {
            check(std::isfinite(actual[channel]) && std::isfinite(expected[channel]), "nonfinite queued-release audio");
            signal += expected[channel] * expected[channel];
            const double delta = actual[channel] - expected[channel];
            difference += delta * delta;
        }
    }
    check(signal > 1.e-12, "queued-release comparison was silent");
    if (difference != 0.0) {
        std::cerr << "MISMATCH model=" << model << " rate=" << rate << " quality=" << quality
                  << " release_mode=" << releaseMode << " drain_mode=" << drainMode
                  << " cancel=" << cancel << " relative_energy=" << difference / signal << '\n';
    }
    check(difference == 0.0, "queued release differs from the immediate public NoteOff behavior");
    ++fixtures;
}
}

int main() {
    for (int model = 0; model < 4; ++model)
        for (double rate : {44100.0, 48000.0, 96000.0})
            for (int quality = 0; quality < 3; ++quality)
                for (int mode = 0; mode < 3; ++mode)
                    for (bool cancel : {false, true}) run(model, rate, quality, mode, mode, cancel);
    for (int model = 0; model < 4; ++model)
        for (int mode : {0, 1})
            for (bool cancel : {false, true}) run(model, 48000.0, 0, mode, 1 - mode, cancel);
    std::cout << "Queued note release: " << fixtures << " fixtures, " << failures << " failures\n";
    return failures ? 1 : 0;
}
