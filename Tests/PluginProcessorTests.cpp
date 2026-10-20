// Plug-in processor contracts: stable parameter IDs, the factory bank, state
// save and restore, MIDI timing independent of the host's block size, the
// optional direct-output buses and unsupported sample rates.

#include "PluginProcessor.h"

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// Hosts recall automation and sessions by these IDs: never rename, reorder
// freely. They are the Rack Extension's property names.
const char* const kExpectedIds[] = {
    "model", "malletType", "malletHardness", "strikePosition", "strikeJitter", "resonatorTune",
    "resonatorCoupling", "decay", "buzzAmount", "artifacts", "sympathetic", "bodyBloom", "closeLevel",
    "farLevel", "piezoLevel", "invertPiezo", "stereoWidth", "preampDrive", "warmth", "compAmount",
    "compAttack", "compRelease", "volume", "oversampling", "velocityCurve", "polyphony", "masterTune",
    "detune", "roomType", "velocityToHardness", "velocityToPosition", "velocityToDecay", "tuningScale",
    "noteOffMode", "carving", "membraneTension", "velocityToCarving", "velocityToCoupling",
    "velocityToBloom", "velocityToSympathetic", "velocityToBuzz", "velocityToTension",
    "velocityToArtifacts", "velocityToVariance",
};

juce::MidiBuffer Phrase() {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 100);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.5f), 4100);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 90), 9000);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 12000), 12000);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20000);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 20000);
    midi.addEvent(juce::MidiMessage::noteOff(1, 67), 30001);
    midi.addEvent(juce::MidiMessage::noteOff(1, 72), 40000);
    return midi;
}

// The phrase rendered in host blocks of blockSize, main stereo output only.
std::vector<float> Render(MarembaAudioProcessor& processor, int blockSize, int frames, double rate = 48000.0) {
    processor.setRateAndBufferSizeDetails(rate, blockSize);
    processor.prepareToPlay(rate, blockSize);
    const auto phrase = Phrase();
    std::vector<float> out;
    for (int start = 0; start < frames; start += blockSize) {
        const int n = std::min(blockSize, frames - start);
        juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), n);
        buffer.clear();
        juce::MidiBuffer midi;
        for (const auto event : phrase)
            if (event.samplePosition >= start && event.samplePosition < start + n)
                midi.addEvent(event.getMessage(), event.samplePosition - start);
        processor.processBlock(buffer, midi);
        for (int i = 0; i < n; ++i) {
            out.push_back(buffer.getSample(0, i));
            out.push_back(buffer.getSample(1, i));
        }
    }
    return out;
}

double Peak(const std::vector<float>& samples) {
    double peak = 0.0;
    for (const float s : samples) peak = std::isfinite(s) ? std::max(peak, std::fabs(static_cast<double>(s))) : INFINITY;
    return peak;
}

void TestParameterIds() {
    MarembaAudioProcessor processor;
    const auto& parameters = processor.getParameters();
    Expect(parameters.size() == static_cast<int>(std::size(kExpectedIds)), "one parameter per visible control");
    for (const char* id : kExpectedIds) {
        auto* parameter = dynamic_cast<juce::AudioProcessorParameterWithID*>(processor.parameters().getParameter(id));
        Expect(parameter != nullptr && parameter->getVersionHint() == 1, id);
    }
}

void TestFactoryBank() {
    MarembaAudioProcessor processor;
    auto* quality = dynamic_cast<juce::AudioParameterChoice*>(processor.parameters().getParameter("oversampling"));
    Expect(quality && quality->choices.size() == 4 && quality->getIndex() == 3,
           "four Quality choices preserve legacy indices and default to native 1x");
    Expect(processor.getNumPrograms() == 20, "twenty factory presets");
    for (int program = 0; program < processor.getNumPrograms(); ++program) {
        processor.setCurrentProgram(program);
        Expect(processor.getCurrentProgram() == program, "program selects");
        Expect(processor.getProgramName(program) == maremba::kFactoryPresets[static_cast<std::size_t>(program)].name,
               "program names are the bank's");
        const auto controls = processor.currentControls();
        const auto& preset = maremba::kFactoryPresets[static_cast<std::size_t>(program)].values;
        Expect(preset[maremba::kOversampling] == 3 && controls[maremba::kOversampling] == 3,
               "every factory program selects native 1x");
        for (const char* id : kExpectedIds) {
            int control = 0;
            while (std::string(maremba::kControlNames[static_cast<std::size_t>(control)]) != id) ++control;
            const auto index = static_cast<std::size_t>(control);
            // Parameters hold floats, and JUCE may leave a parameter within a
            // float step of a nearby value; the bank holds the patch's doubles.
            Expect(std::fabs(controls[index] - preset[index]) < 1.0e-6, "a preset loads every value");
        }
    }
}

void TestStateRoundTrip() {
    MarembaAudioProcessor source;
    source.setCurrentProgram(7);
    auto* hardness = source.parameters().getParameter("malletHardness");
    hardness->setValueNotifyingHost(0.123f);
    juce::MemoryBlock state;
    source.getStateInformation(state);

    MarembaAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    Expect(restored.getCurrentProgram() == 7, "state restores the program");
    Expect(restored.currentControls() == source.currentControls(), "state restores every control");

    MarembaAudioProcessor untouched;
    const auto before = untouched.currentControls();
    const char junk[] = "not a Maremba state";
    untouched.setStateInformation(junk, static_cast<int>(sizeof junk));
    Expect(untouched.currentControls() == before, "a foreign state is ignored");
}

void TestBlockPartitionInvariance() {
    const int frames = 48000 * 2;
    MarembaAudioProcessor a, b, c;
    for (auto* processor : { &a, &b, &c }) processor->setCurrentProgram(1);
    const auto reference = Render(a, 512, frames);
    Expect(Peak(reference) > 1.0e-3 && std::isfinite(Peak(reference)), "the phrase is audible and finite");
    Expect(Render(b, 37, frames) == reference, "37-frame blocks render the same samples as 512");
    Expect(Render(c, 1, frames) == reference, "1-frame blocks render the same samples as 512");
    Expect(std::all_of(reference.begin(), reference.begin() + 200, [](float s) { return s == 0.0f; }),
           "silence before the first note");
}

void TestDirectOutputs() {
    MarembaAudioProcessor processor;
    auto layout = processor.getBusesLayout();
    layout.outputBuses.set(1, juce::AudioChannelSet::stereo());
    layout.outputBuses.set(2, juce::AudioChannelSet::stereo());
    layout.outputBuses.set(3, juce::AudioChannelSet::mono());
    Expect(processor.setBusesLayout(layout), "Close, Room and Piezo buses enable");
    auto wrong = layout;
    wrong.outputBuses.set(3, juce::AudioChannelSet::stereo());
    Expect(!processor.checkBusesLayoutSupported(wrong), "Piezo is mono only");
    Expect(processor.getTotalNumOutputChannels() == 7, "seven output channels");

    processor.setCurrentProgram(2);
    processor.prepareToPlay(48000.0, 512);
    std::array<double, 7> peaks {};
    const auto phrase = Phrase();
    for (int start = 0; start < 48000; start += 512) {
        juce::AudioBuffer<float> buffer(7, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        for (const auto event : phrase)
            if (event.samplePosition >= start && event.samplePosition < start + 512)
                midi.addEvent(event.getMessage(), event.samplePosition - start);
        processor.processBlock(buffer, midi);
        for (int channel = 0; channel < 7; ++channel)
            peaks[static_cast<std::size_t>(channel)] = std::max(peaks[static_cast<std::size_t>(channel)],
                static_cast<double>(buffer.getMagnitude(channel, 0, 512)));
    }
    for (const double peak : peaks) Expect(peak > 1.0e-4 && std::isfinite(peak), "every output carries the phrase");
}

void TestUnsupportedRate() {
    MarembaAudioProcessor processor;
    const auto out = Render(processor, 256, 4096, 384000.0);
    Expect(Peak(out) == 0.0, "an unsupported sample rate renders silence");
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI gui;
    TestParameterIds();
    TestFactoryBank();
    TestStateRoundTrip();
    TestBlockPartitionInvariance();
    TestDirectOutputs();
    TestUnsupportedRate();
    if (gFailures != 0) {
        std::fprintf(stderr, "PluginProcessorTests: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("PluginProcessorTests OK\n");
    return 0;
}
