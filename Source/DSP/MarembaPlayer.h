#pragma once

// Plays Maremba from MIDI: the plug-in and the demo renderer share it, so a
// demo is what the plug-in plays from the same MIDI file. The Reason Rack
// Extension has its own MIDI and CV front end and does not use this.

#include "MarembaControls.h"
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace maremba {

class MarembaPlayer {
public:
    // The engine supports 22.05 to 192 kHz; any other rate renders silence
    // rather than a model built for the wrong rate.
    static bool SupportsSampleRate(double sampleRate) {
        return sampleRate >= 22050.0 && sampleRate <= 192000.0;
    }

    // Allocates; call off the audio thread.
    void Prepare(double sampleRate) {
        fEngine = SupportsSampleRate(sampleRate) ? std::make_unique<MarembaEngine>(sampleRate) : nullptr;
        fHeld.fill(0);
        fLoaded = false;
    }

    void Reset() {
        fHeld.fill(0);
        if (fEngine) fEngine->Reset();
    }

    // Stored control values, except the MIDI-driven mod wheel and pitch bend,
    // which keep their last MIDI value.
    void SetControls(const ControlValues& values) {
        ControlValues next = values;
        next[kModWheel] = fControls[kModWheel];
        next[kPitchBend] = fControls[kPitchBend];
        Apply(next);
    }

    const ControlValues& Controls() const { return fControls; }

    // One short MIDI message: status byte, then up to two data bytes.
    void HandleMidi(const std::uint8_t* data, int size) {
        if (size < 1) return;
        const int status = data[0] & 0xF0;
        const int a = size > 1 ? data[1] & 0x7F : 0;
        const int b = size > 2 ? data[2] & 0x7F : 0;
        if (status == 0x90 && b > 0) {
            StartNote(a, static_cast<float>(b) / 127.0f);
        } else if (status == 0x80 || status == 0x90) {
            StopNote(a);
        } else if (status == 0xE0) {
            ControlValues next = fControls;
            next[kPitchBend] = static_cast<double>((b << 7) | a) / 16383.0;
            Apply(next);
        } else if (status == 0xB0 && a == 1) {
            ControlValues next = fControls;
            next[kModWheel] = static_cast<double>(b) / 127.0;
            Apply(next);
        } else if (status == 0xB0 && (a == 120 || a == 123)) {
            // All Notes Off releases every key, as its note-offs would;
            // All Sound Off also gives every sounding bar the hand damping.
            for (int note = 0; note < 128; ++note) {
                if (fHeld[static_cast<std::size_t>(note)] == 0) continue;
                fHeld[static_cast<std::size_t>(note)] = 0;
                if (fEngine) fEngine->NoteOff(note);
            }
            if (a == 120 && fEngine) fEngine->DampAll();
        }
    }

    // The hand damping Reason applies when its transport stops.
    void DampAll() {
        if (fEngine) fEngine->DampAll();
    }

    // Renders any number of frames. A null direct output is not computed; a
    // silent engine writes zeros.
    void Render(float* mainL, float* mainR, float* closeL, float* closeR,
                float* farL, float* farR, float* piezo, int frames) {
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, MarembaEngine::kMaxFrames);
            const auto at = [done](float* p) { return p ? p + done : nullptr; };
            if (!fEngine || !fLoaded || fEngine->IsSilent()) {
                for (float* p : { mainL, mainR, closeL, closeR, farL, farR, piezo })
                    if (p) std::fill(p + done, p + done + n, 0.0f);
            } else {
                fEngine->RenderBatch(at(mainL), at(mainR), at(closeL), at(closeR),
                                     at(farL), at(farR), at(piezo), n);
            }
            done += n;
        }
    }

private:
    void Apply(const ControlValues& next) {
        if (fLoaded && next == fControls) return;
        fControls = next;
        fLoaded = true;
        if (!fEngine) return;
        fEngine->SetParameters(EngineParametersFromControls(fControls));
        fEngine->SetPitchBendCents(PitchBendCentsFromControls(fControls));
    }

    // A key is released only when its last note-on has had its note-off, as
    // in the Rack Extension.
    void StartNote(int note, float velocity) {
        auto& held = fHeld[static_cast<std::size_t>(note)];
        if (held < 0xFFFF) ++held;
        if (fEngine) fEngine->NoteOn(note, velocity);
    }

    void StopNote(int note) {
        auto& held = fHeld[static_cast<std::size_t>(note)];
        if (held == 0) return;
        if (--held == 0 && fEngine) fEngine->NoteOff(note);
    }

    std::unique_ptr<MarembaEngine> fEngine;
    ControlValues fControls = kControlDefaults;
    std::array<std::uint16_t, 128> fHeld {};
    bool fLoaded = false;
};

} // namespace maremba
