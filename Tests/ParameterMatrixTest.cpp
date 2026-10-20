// The shared parameter-extremes matrix (U4) over MarembaPlayer, with the
// plug-in's parameter list: every control the plug-in exposes, a knob as its
// 0..1 value and a choice by its steps, and the factory presets.
//
//   MarembaParameterMatrix [--rotate] [--jobs N] [--shard K/N] [--quality-only]
//
// --rotate runs each case at one rate and block size, for a quick pass;
// --shard runs every Nth case from the Kth, for splitting the matrix across jobs.

#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"
#include "ParameterMatrix.h"
#include "PluginControls.h"

#include <cstring>
#include <string>

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
    if (quality == parameters.end() || quality->steps != 4 || quality->defaultValue != 1.f) {
        std::fprintf(stderr, "error: matrix must expose four Quality states with native 1x as default index 3\n");
        return 1;
    }
    protocodus::matrix::Options options;
    options.jobs = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rotate") options.everyConfiguration = false;
        else if (arg == "--quality-only")
            options.select = [](std::size_t, const protocodus::matrix::Case& c) { return c.label.rfind("oversampling=", 0) == 0; };
        else if (arg == "--jobs" && i + 1 < argc) options.jobs = std::max(1, std::stoi(argv[++i]));
        else if (arg == "--shard" && i + 1 < argc) {
            const std::string shard = argv[++i];
            const std::size_t k = std::stoul(shard), n = std::stoul(shard.substr(shard.find('/') + 1));
            if (n == 0 || k >= n) {
                std::fprintf(stderr, "error: --shard needs K/N with K < N\n");
                return 2;
            }
            options.select = [k, n](std::size_t index, const protocodus::matrix::Case&) { return index % n == k; };
        }
        else {
            std::fprintf(stderr, "usage: %s [--rotate] [--jobs N] [--shard K/N] [--quality-only]\n", argv[0]);
            return 2;
        }
    }
    return protocodus::matrix::Run([] { return std::make_unique<MarembaDevice>(); }, options) == 0 ? 0 : 1;
}
