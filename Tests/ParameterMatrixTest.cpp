// The shared parameter-extremes matrix (U4) over MarembaPlayer, with the
// plug-in's parameter list: every control the plug-in exposes, a knob as its
// 0..1 value and a choice by its steps, and the factory presets.
//
//   MarembaParameterMatrix [--jobs N] [--rotation] [--shard K/N] [--case LABEL]
//                          [--count] [--list] [--quality-only]
//   MarembaParameterMatrix --reach [--jobs N] [--case PARAMETER]
//
// The shared command line (U9): --rotation runs each case at one rate and
// block size, for a quick pass; --shard K/N runs the cases whose index modulo
// N is K, numbered from 0, for splitting the matrix across jobs. --reach is
// the U11 reach test over the same device: every control change must reach
// the sound after long play.

#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"
#include "ParameterMatrix.h"
#include "PluginControls.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace maremba;
using protocodus::matrix::Parameter;

int Steps(std::size_t control) {
    const auto& display = kControlDisplay[control];
    if (display.style != EControlStyle::Choice) return 0;
    int steps = 0;
    for (const char* choice : display.choices) steps += choice != nullptr;
    return steps;
}

class MarembaDevice final : public protocodus::matrix::Device {
public:
    MarembaDevice() {
        for (std::size_t control = 0; control < kControlCount; ++control)
            if (kControlDisplay[control].style != EControlStyle::Hidden) exposed.push_back(control);
    }
    std::vector<Parameter> Parameters() const override {
        std::vector<Parameter> result;
        for (const std::size_t control : exposed)
            result.push_back({ kControlNames[control], Steps(control), Normalised(control, kControlDefaults[control]) });
        return result;
    }
    int PresetCount() const override { return static_cast<int>(kFactoryPresets.size()); }
    void Prepare(double rate, int) override {
        player.Prepare(rate);
        player.SetControls(controls);
    }
    void LoadPreset(int index) override {
        controls = kFactoryPresets[static_cast<std::size_t>(index)].values;
        player.SetControls(controls);
    }
    float GetParameter(std::size_t index) const override {
        return Normalised(exposed[index], controls[exposed[index]]);
    }
    void SetParameter(std::size_t index, float normalised) override {
        const std::size_t control = exposed[index];
        const int steps = Steps(control);
        // A choice stores its step index, as the plug-in's choice parameter does.
        controls[control] = steps ? std::lround(normalised * static_cast<float>(steps - 1)) : normalised;
        player.SetControls(controls);
    }
    void Process(float* left, float* right, int frames, const protocodus::matrix::Event* events, int count) override {
        int done = 0;
        for (int e = 0; e <= count; ++e) {
            const int until = e < count ? events[e].offset : frames;
            if (until > done)
                player.Render(left + done, right + done, nullptr, nullptr, nullptr, nullptr, nullptr, until - done);
            done = std::max(done, until);
            if (e < count) player.HandleMidi(events[e].bytes, events[e].size);
        }
    }
    std::vector<std::uint8_t> SaveState() override {
        std::vector<std::uint8_t> state(sizeof controls);
        std::memcpy(state.data(), controls.data(), sizeof controls);
        return state;
    }
    void RestoreState(const std::vector<std::uint8_t>& state) override {
        if (state.size() != sizeof controls) return;
        std::memcpy(controls.data(), state.data(), sizeof controls);
        player.SetControls(controls);
    }

    protocodus::matrix::Limits LimitsFor(const protocodus::matrix::Case& c) const override {
        protocodus::matrix::Limits limits;
        const auto value = [&](const std::vector<float>& v, EControl control) {
            return v[static_cast<std::size_t>(std::find(exposed.begin(), exposed.end(), control) - exposed.begin())];
        };
        const auto step = [&](const std::vector<float>& v, EControl control) {
            return std::lround(value(v, control) * static_cast<float>(Steps(control) - 1));
        };
        const auto either = [&](EControl control, auto test) { return test(c.values, control) || test(c.base, control); };
        const auto stepIs = [&](long target) {
            return [&, target](const std::vector<float>& v, EControl control) { return step(v, control) == target; };
        };
        // Bars are struck, not sustained: a held note is the ring of one
        // strike. Below its natural midpoint (x1) Bar Decay shortens that ring
        // to less than the matrix's 0.8 s hold, and Dead keeps the mallet on
        // the key, choking it at contact.
        if (either(kDecay, [&](const std::vector<float>& v, EControl k) { return value(v, k) < 0.5f; })
            || either(kNoteOffMode, stepIs(2)))
            limits.soundsWhileHeld = false;
        // An Oversampling change rebuilds the engine at the new rate: ringing
        // voices fade out and are killed (MarembaEngine::ApplyFactorSwitch).
        if (step(c.values, kOversampling) != step(c.base, kOversampling)) limits.soundsWhileHeld = false;
        // Volume at zero mutes the main output.
        if (value(c.values, kVolume) == 0.0f) limits.soundsWhileHeld = false;
        // Ring lets keys ring freely after note-off, so no silence is due;
        // the tail must still not grow.
        if (either(kNoteOffMode, stepIs(0))) limits.releaseSeconds = -1.0;
        return limits;
    }

    // U11. Every control not named here must reach bars already ringing.
    protocodus::matrix::ReachRule ReachFor(std::size_t index, float value) const override {
        using protocodus::matrix::Reach;
        const std::size_t control = exposed[index];
        const auto at = [&](EControl other) {
            return static_cast<std::size_t>(std::find(exposed.begin(), exposed.end(), other) - exposed.begin());
        };
        switch (control) {
        // MarembaEngine::NoteOn evaluates these at the strike boundary and
        // hands them to the voice (MarembaVoice::Trigger): a bar keeps the
        // strike, mallet, resonator, material and pitch it was struck with.
        case kModel:
            return { Reach::NewNotes, "the instrument each new strike plays; ringing bars keep theirs" };
        case kMalletType: case kMalletHardness: case kStrikePosition: case kStrikeJitter: case kVelocityCurve:
        case kVelocityToHardness: case kVelocityToPosition:
            return { Reach::NewNotes, "a property of each new strike" };
        case kVelocityToVariance:
            // Added to Strike Jitter's 15 %, it moves three notes by under
            // 0.5 dB; on the Linear curve it moves the probe's v80 note by
            // about that, and the Soft curve gives that note 79 % of it.
            return { Reach::NewNotes, "a property of each new strike",
                     { { at(kStrikeJitter), 0.0f }, { at(kVelocityCurve), 0.0f } } };
        case kResonatorTune: case kResonatorCoupling: case kDecay: case kArtifacts: case kCarving:
        case kVelocityToDecay: case kVelocityToCoupling: case kVelocityToBuzz: case kVelocityToTension:
        case kVelocityToArtifacts:
            return { Reach::NewNotes, "set on each bar as it is struck" };
        case kDetune: case kTuningScale:
            // Sambla tunes C, E and G exactly as Equal does (Tunings.h).
            if (control == kTuningScale && std::lround(value * 6.0f) == 4)
                return { Reach::Exempt, "Sambla leaves the probe's C, E and G at equal temperament" };
            return { Reach::NewNotes, "the pitch each bar is struck at; a ringing bar keeps its pitch" };
        case kNoteOffMode:
            // Dead keeps the mallet on the key from the stroke (StrikeOptions).
            if (std::lround(value * 2.0f) == 2) return { Reach::NewNotes, "Dead presses each new stroke's mallet on its key" };
            return {};
        case kVelocityToCarving:
            // Carving is at its top in the default patch, so no velocity can add to it.
            return { Reach::NewNotes, "set on each bar as it is struck", { { at(kCarving), 0.5f } } };
        // The frame body barely sounds under a mid-register chord on the
        // default Imperial Rosewood (Body Bloom 0 to 1 moves it 0.03 dB); on
        // the Kalimba it drives the box.
        case kBodyBloom:
            return { Reach::Held, "", { { at(kModel), 1.0f } } };
        case kVelocityToBloom:
            return { Reach::NewNotes, "how hard each new strike drives the body", { { at(kModel), 1.0f } } };
        // The other keys answer 40-60 dB under the bars: Sympathetic 40 % to
        // 100 % moves this phrase 0.04 dB on Imperial Rosewood and at most
        // 0.5 dB on any model. SympatheticMesh::AdvanceAndRead applies it
        // every sample.
        case kSympathetic: case kVelocityToSympathetic:
            return { Reach::Exempt, "the other keys ring 40-60 dB under the bars, inaudible on this phrase" };
        // The default patch compresses at 5 %, too little to hear its timing,
        // and an attack acts only where the level rises.
        case kCompAttack:
            return { Reach::NewNotes, "the compressor's attack meets only the next strike", { { at(kCompAmount), 1.0f } } };
        case kCompRelease:
            return { Reach::Held, "", { { at(kCompAmount), 1.0f } } };
        case kOversampling:
            return { Reach::Exempt, "the internal rate; at every factor the instrument is meant to sound the same" };
        case kPolyphony:
            return { Reach::Exempt, "a voice limit; the probe's three notes fit every limit" };
        default:
            return {};
        }
    }

private:
    static float Normalised(std::size_t control, double value) {
        const int steps = Steps(control);
        return static_cast<float>(steps ? value / (steps - 1) : value);
    }

    std::vector<std::size_t> exposed;
    ControlValues controls = kControlDefaults;
    MarembaPlayer player;
};
} // namespace

int main(int argc, char** argv) {
    const MarembaDevice inventory;
    const auto parameters = inventory.Parameters();
    const auto quality = std::find_if(parameters.begin(), parameters.end(), [](const Parameter& p) { return p.name == "oversampling"; });
    // The selector reads 1x 2x 4x 8x left to right since 083350e, so native
    // 1x, the default, is index 0 (it was index 3).
    if (quality == parameters.end() || quality->steps != 4 || quality->defaultValue != 0.f) {
        std::fprintf(stderr, "error: matrix must expose four Quality states with native 1x as default index 0\n");
        return 1;
    }
    // --quality-only, Maremba's own flag, keeps the Quality cases; the rest of
    // the command line is the shared matrix CLI (U9).
    protocodus::matrix::Options options;
    options.jobs = 1;
    std::vector<char*> args(argv, argv + argc);
    const auto qualityOnly = std::find_if(args.begin() + 1, args.end(), [](const char* arg) { return std::string(arg) == "--quality-only"; });
    if (qualityOnly != args.end()) {
        options.select = [](std::size_t, const protocodus::matrix::Case& c) { return c.label.rfind("oversampling=", 0) == 0; };
        args.erase(qualityOnly);
    }
    return protocodus::matrix::Main(static_cast<int>(args.size()), args.data(), [] { return std::make_unique<MarembaDevice>(); }, options);
}
