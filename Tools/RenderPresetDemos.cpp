// The README's preset demos (G3): one per factory preset, played through
// MarembaPlayer, the MIDI front end the plug-in plays through. The shared
// Tools/PresetDemoRenderer.h owns the MIDI, render, checks and encoding.
#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"
#include "PresetDemoRenderer.h"

namespace {
class MarembaVoice final : public protocodus::demos::Voice {
public:
    MarembaVoice(std::size_t preset, double rate) {
        player.Prepare(rate);
        player.SetControls(maremba::kFactoryPresets[preset].values);
    }
    void Midi(const std::uint8_t* bytes, int size) override { player.HandleMidi(bytes, size); }
    void Render(float* left, float* right, int frames) override {
        player.Render(left, right, nullptr, nullptr, nullptr, nullptr, nullptr, frames);
    }

private:
    maremba::MarembaPlayer player;
};
} // namespace

int main(int argc, char** argv) {
    protocodus::demos::Instrument instrument;
    instrument.name = "Maremba";
    for (const auto& preset : maremba::kFactoryPresets) instrument.presets.emplace_back(preset.name);
    instrument.open = [](std::size_t preset, double rate, int, std::uint32_t) {
        return std::make_unique<MarembaVoice>(preset, rate);
    };
    return protocodus::demos::Main(argc, argv, instrument);
}
