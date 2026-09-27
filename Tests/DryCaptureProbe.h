#pragma once

// Recording comparisons need an anechoic microphone, not the complete Room
// capture exposed by the device. Let the real engine configure/excite its
// voice, then read its physical microphone before Room. This test-only probe
// has no production bypass, socket or behavior flag.
#include "MarembaEngine.h"
#include "MicMixer.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static std::vector<double> DryCapture(const EngineParameters& params,
                                         int note, float velocity, double rate,
                                         double seconds, bool far) {
        // These reference fixtures isolate a wooden bar and its tube. Shared
        // frame/mesh/box contributions cannot be represented by one raw voice.
        if (params.model < 0 || params.model > 2 || params.bodyBloom != 0
            || params.sympathetic != 0 || params.velocityToBloom != 0
            || params.velocityToSympathetic != 0 || params.modWheel != 0)
            throw std::runtime_error("DryCapture requires a bar with shared acoustic layers and rolls disabled");
        MarembaEngine engine(rate);
        engine.SetParameters(params);
        engine.NoteOn(note, velocity);
        const int factor = engine.mActiveFactor;
        const int frames = static_cast<int>(std::ceil(seconds * rate));
        MonoOversampler left, right;
        // Preserve the historical anechoic reference fixture's panning. The
        // production capture scene includes propagation and measured ears.
        MicMixer referenceMixer;
        std::array<float, MarembaEngine::kMaxOversample> subLeft{}, subRight{};
        std::vector<double> result;
        result.reserve(static_cast<size_t>(frames));
        const float gain = MicMixer::kMasterMixScale * .06f * engine.mVolumeGain;
        for (int frame = 0; frame < frames; ++frame) {
            engine.mClock += 1;
            for (int sub = 0; sub < factor; ++sub) {
                float closeL = 0, closeR = 0, farL = 0, farR = 0, piezo = 0;
                for (auto& voice : engine.mVoices) if (voice.IsActive()) {
                    float vc = 0, vf = 0, vp = 0;
                    const int noteNumber = voice.GetNoteNumber();
                    if (voice.ProcessSample(vc, vf, vp))
                        referenceMixer.MixVoiceSample(noteNumber, false, vc, vf, vp,
                                                       closeL, closeR, farL, farR, piezo);
                }
                // Original dry Far calibration, before adding reflected sound.
                subLeft[sub] = far ? .7f * farL : closeL;
                subRight[sub] = far ? .7f * farR : closeR;
            }
            const float l = left.DownsampleFrame(subLeft.data(), factor) * gain;
            const float r = right.DownsampleFrame(subRight.data(), factor) * gain;
            result.push_back(l + r);
            // Match the engine's 16-host-frame voice lifetime/state-flush grid.
            if (++engine.mControlCounter >= MarembaEngine::kControlInterval) {
                engine.mControlCounter = 0;
                engine.ControlTick();
                left.FlushTiny(); right.FlushTiny();
            }
        }
        // No final 2 ms output delay: measurements remain aligned to the
        // physical strike, as the original 1.4.0f3 anechoic fixtures were.
        return result;
    }
};
} // namespace maremba
