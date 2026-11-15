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
// N is K, numbered from 0, for splitting the matrix across jobs. --reach runs
// the U11 parameter-reach test (Maremba.ParameterReach) over the same device.

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
        // A Quality change rebuilds the engine at the new rate: ringing
        // voices fade out and are killed (MarembaEngine::ApplyFactorSwitch).
        if (step(c.values, kQuality) != step(c.base, kQuality)) limits.soundsWhileHeld = false;
        // Volume at zero mutes the main output.
        if (value(c.values, kVolume) == 0.0f) limits.soundsWhileHeld = false;
        // Ring lets keys ring freely after note-off, so no silence is due;
        // the tail must still not grow.
        if (either(kNoteOffMode, stepIs(0))) limits.releaseSeconds = -1.0;
        return limits;
    }

    // U11. Quality, the one parameter the plug-in's editor changes and a host
    // cannot automate, is in Parameters() with the rest: the plug-in hands
    // the player every parameter the same way, whoever moved it
    // (MarembaAudioProcessor::processBlock calls SetControls each block), so
    // here the editor's path and the host's are both SetParameter.
    //
    // The design, Docs/validation/physical-parameter-audit.md: "Strike
    // properties are captured at note-on/restrike: model, striker, hardness,
    // position, cavity settings, decay, artifacts, variance, detune and
    // velocity depths. Their automation is expected to affect subsequent
    // strokes. Buzz, shared acoustic levels, mixer, tone/dynamics, volume and
    // global tuning operate on ringing sound." MarembaEngine::TriggerNote
    // hands the strike properties to the voice it strikes; SetParameters
    // carries the others to every sounding voice and shared stage. The
    // strike properties are NewNotes below; the rest reach held notes.
    protocodus::matrix::ReachRule ReachFor(std::size_t index, float value) const override {
        using protocodus::matrix::Reach;
        const auto strike = [](const char* reason) { return protocodus::matrix::ReachRule { Reach::NewNotes, reason }; };
        switch (exposed[index]) {
        // TriggerNote: "A bar struck with another model cannot be carried
        // into the new strike": it rings on as it was struck until a stroke
        // on the new model fades it. The shared body follows the model at
        // once (ConfigureModel), which the held chord hears from Balafon
        // (2.7 dB) but hardly from Padauk or Kalimba (0.2 dB).
        case kModel: return strike("a ringing bar keeps the model it was struck on (TriggerNote)");
        case kMalletType:
        case kMalletHardness:
        case kStrikePosition: return strike("the mallet and where it lands, taken at each strike");
        // Drawn from the strike's own random draws (GenerateStrikeCharacter,
        // GenerateStrikeArtifacts in MarembaVoice::Trigger).
        case kStrikeJitter:
        case kArtifacts: return strike("drawn for each strike from its own random draws");
        // The air mode under each bar is tuned and coupled when it is struck
        // (the audit's "cavity settings"; Resonator Tune "remains active on
        // new strikes"). Only the kalimba's shared box follows them live.
        case kResonatorTune:
        case kResonatorCoupling: return strike("the tube or gourd under each bar, tuned and coupled at its strike");
        case kDecay: return strike("each bar's sustain, set at its strike");
        // Detune and Scale both enter the key's own offset TriggerNote
        // passes to the voice ("a repeated key retains its tuning"); Master
        // Tune and pitch bend are the global tuning that retunes ringing
        // bars (ApplyGlobalTuning) and the ringing-along keys.
        case kDetune: return strike("each key's own tuning, set at its strike; Master Tune and bend retune ringing bars");
        // Sambla keeps C, E and G where equal temperament has them (0, 400
        // and 700 cents: TuningOffsetCents is 0 on G3, E4 and C5), and those
        // are the probes' keys, so only the long play's other keys hear it.
        // Round5ResponseTest strikes Sambla's D sharp and F as plain retunes.
        case kTuningScale:
            if (std::lround(value * 6.0f) == static_cast<long>(ETuningScale::Sambla)) {
                auto rule = protocodus::matrix::ReachRule { Reach::Exempt, "Sambla tunes the probes' keys, G3 E4 C5, as Equal does "
                                                                           "(Round5ResponseTest retunes its other keys)" };
                rule.history = "the long play's other keys, which Sambla retunes";
                return rule;
            }
            return strike("each key's own tuning, set at its strike; Master Tune and bend retune ringing bars");
        // Docs/research/physical-models-review-2026-10-14.md: a geometry
        // change on a ringing key "warrant[s] a separate transition design";
        // until it has one, Carving is read at each strike and restrike.
        case kCarving: return strike("the bars' carved geometry, read at each strike and restrike");
        case kVelocityCurve: return strike("how a key's velocity drives its strike");
        case kVelocityToHardness:
        case kVelocityToPosition:
        case kVelocityToDecay:
        case kVelocityToCoupling:
        case kVelocityToBloom:
        case kVelocityToSympathetic:
        case kVelocityToBuzz:
        case kVelocityToTension:
        case kVelocityToArtifacts:
        case kVelocityToVariance: return strike("a velocity route, applied to each strike by its velocity");
        // The route adds to Carving up to the keys as built (1), where the
        // default and all 20 factory presets sit, so it is heard below them.
        case kVelocityToCarving: {
            auto rule = strike("a velocity route, applied to each strike by its velocity");
            rule.with = { { Index(kCarving), 0.5f } };
            return rule;
        }
        // Damp stops the keys released after it, the held chord among them;
        // Dead is a kind of stroke, the mallet held on the key it strikes
        // (StrikeOptions::deadStroke), so keys already ringing release as
        // they were struck.
        case kNoteOffMode:
            if (std::lround(value * 2.0f) == 2) return strike("a dead stroke holds the mallet on the keys struck after it");
            return {};
        // A voice count, heard only once more keys ring than it allows. On
        // the default patch R and T sound alike at probe 1 (0.04 dB apart for
        // 8 voices, 0.03 dB for 24). At the longest Bar Decay, where more
        // keys ring than any limit, the probes' keys still ring from the
        // probe before, and a restrike keeps its voice (TriggerNote): probes
        // 2 and 3 then sound the same at either limit. test_dsp covers the
        // limit itself (TestPolyphonyReduction, TestStealVictim,
        // TestChordBurstFadesVictims).
        case kPolyphony:
            return { Reach::Exempt, "a voice count; the probes restrike their own ringing keys and need no more voices "
                                    "(test_dsp's polyphony and steal tests)" };
        // The limiter's timing acts only while it limits. Attack is how fast
        // it clamps a level rising past its threshold (AnalogLimiter::Process
        // charges at the attack rate only while the demand rises), which the
        // onsets of new strikes are and a held chord's decay is not: driven
        // to full Drive and Volume as well, the held chord moves 0.04 dB.
        case kCompAttack: {
            auto rule = strike("how fast the limiter clamps a rising level: the onsets of the notes after it");
            rule.with = { { Index(kCompAmount), 1.0f } };
            return rule;
        }
        case kCompRelease: return { Reach::Held, "", { { Index(kCompAmount), 1.0f } } };
        // MarembaEngine::SetParameters: a Quality change fades every voice
        // and tail out and restarts the engine at the new rate
        // (ApplyFactorSwitch); notes from then on play at it. It is an alias
        // policy, not a tone control, so on the default patch 2x sounds as
        // 1x does, and the test meets it on the first preset where it does
        // not.
        case kQuality: {
            auto rule = strike("a Quality change fades out what sounds and restarts the engine at the new rate");
            rule.history = "the switch clears the tails the long play and the held chord leave in R and T";
            return rule;
        }
        default: return {};
        }
    }

private:
    std::size_t Index(EControl control) const {
        return static_cast<std::size_t>(std::find(exposed.begin(), exposed.end(), control) - exposed.begin());
    }

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
    const auto quality = std::find_if(parameters.begin(), parameters.end(), [](const Parameter& p) { return p.name == "quality"; });
    if (quality == parameters.end() || quality->steps != 4 || quality->defaultValue != 0.f) {
        std::fprintf(stderr, "error: matrix must expose four Quality states with 1x as default index 0\n");
        return 1;
    }
    // --quality-only, Maremba's own flag, keeps the Quality cases; the rest of
    // the command line is the shared matrix CLI (U9).
    protocodus::matrix::Options options;
    options.jobs = 1;
    std::vector<char*> args(argv, argv + argc);
    const auto qualityOnly = std::find_if(args.begin() + 1, args.end(), [](const char* arg) { return std::string(arg) == "--quality-only"; });
    if (qualityOnly != args.end()) {
        options.select = [](std::size_t, const protocodus::matrix::Case& c) { return c.label.rfind("quality=", 0) == 0; };
        args.erase(qualityOnly);
    }
    return protocodus::matrix::Main(static_cast<int>(args.size()), args.data(), [] { return std::make_unique<MarembaDevice>(); }, options);
}
