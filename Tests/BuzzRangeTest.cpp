// The normalized Buzz property retains its storage and automation contract,
// while all voice-entry paths use 70% of the former physical response.
#include "MarembaEngine.h"
#include <array>
#include <cmath>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static bool Mapping(const MarembaVoice& voice, float knob) {
        const double physical = static_cast<double>(0.7f * knob);
        if (voice.mModel == EMarimbaModel::KalimbaArtisan) {
            // Compare the actual configured buzzer's audio/impact response
            // with the physical kernel at the specified reduced looseness.
            // Reset only the copied probe, leaving the ringing voice intact.
            BuzzerRattle actual = voice.mBuzzer;
            actual.Reset();
            BuzzerRattle reference;
            const double hz = 440.0 * std::exp2((voice.mNoteNumber - 69) / 12.0);
            reference.Configure(voice.mSampleRate, std::sqrt(hz / 261.625565));
            reference.SetLooseness(physical);
            double energy = 0;
            for (int i = 0; i < 4096; ++i) {
                // The buzzers lift only when the board swings hard (a firm
                // treble nail or pick pluck), so the probe board swings that hard.
                const double board = 3. * std::sin(.3 * i) + 1. * std::sin(.037 * i);
                double actualImpact = 0, referenceImpact = 0;
                const double sound = actual.Process(board, 1, actualImpact);
                const double expected = reference.Process(board, 1, referenceImpact);
                if (sound != expected || actualImpact != referenceImpact) return false;
                energy += sound * sound;
            }
            return !voice.mHasMirliton && (knob == 0 ? energy == 0 : energy > 0);
        }
        // Pin the physical amount every entry path hands the membranes: the
        // voice derives each mirliton's gaps (the snap threshold), drive and
        // radiation from this one stored value in SetBuzz.
        if (voice.mBuzzAmount != static_cast<float>(physical)) return false;
        if (voice.mHasMirliton != (physical > .001 && voice.mNumMembranes > 0)) return false;
        for (int i = 0; i < voice.mNumMembranes; ++i)
            if ((voice.mMembraneDrive[i] > 0) != (physical > 0)) return false;
        return true;
    }
};

struct MarembaEngineTestAccess {
    static bool Mapping(const MarembaEngine& engine, float knob) {
        int live = 0;
        for (const auto& voice : engine.mVoices) {
            if (!voice.IsLive()) continue;
            ++live;
            if (!MarembaVoiceTestAccess::Mapping(voice, knob)) return false;
        }
        return live > 0 && engine.GetParameters().buzzAmount == knob;
    }
};
} // namespace maremba

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message, int model) {
        if (!ok) { ++failures; std::cerr << "FAIL model=" << model << ' ' << message << '\n'; }
    };
    for (double rate : {44100., 48000., 96000.}) for (int model = 0; model < 4; ++model) {
        maremba::MarembaEngine engine(rate);
        maremba::EngineParameters p;
        p.model = model;
        p.detune = p.strikeJitter = p.artifacts = 0;
        p.buzzAmount = .25f;
        engine.SetParameters(p);
        engine.NoteOn(60, 1);
        check(maremba::MarembaEngineTestAccess::Mapping(engine, p.buzzAmount), "initial mapping", model);
        for (float knob : {.1f, .5f, 1.f, 0.f}) {
            std::array<float, 64> left{}, right{};
            // A live note must survive the test, and its changing physical
            // state must remain finite while Buzz is raised and removed.
            for (int batch = 0; batch < 20; ++batch) {
                engine.RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, 64);
                for (int i = 0; i < 64; ++i)
                    check(std::isfinite(left[i]) && std::isfinite(right[i]), "non-finite audio", model);
            }
            p.buzzAmount = knob;
            engine.SetParameters(p);
            check(maremba::MarembaEngineTestAccess::Mapping(engine, knob), "live mapping", model);
            engine.NoteOn(60, .9f);
            check(maremba::MarembaEngineTestAccess::Mapping(engine, knob), "restrike mapping", model);
            engine.NoteOn(72, 1);
            check(maremba::MarembaEngineTestAccess::Mapping(engine, knob), "new polyphonic note mapping", model);
        }
    }
    std::cout << "BuzzRangeTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
