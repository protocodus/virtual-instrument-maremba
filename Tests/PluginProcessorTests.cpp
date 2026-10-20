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
    Expect(parameters.size() == static_cast<int>(std::size(kExpectedIds)) + 1,
           "all legacy parameters and one appended native-rate mode");
    for (std::size_t index = 0; index < std::size(kExpectedIds); ++index) {
        const char* id = kExpectedIds[index];
        auto* parameter = dynamic_cast<juce::AudioProcessorParameterWithID*>(processor.parameters().getParameter(id));
        Expect(parameter != nullptr && parameter->getVersionHint() == 1, id);
        Expect(parameters[static_cast<int>(index)] == parameter, "legacy host parameter order remains unchanged");
    }
    auto* native = processor.parameters().getParameter(MarembaAudioProcessor::kNativeRateParameterId);
    Expect(native && native->getVersionHint() == 2 && parameters.getLast() == native,
           "Native Rate appends with a newer AU ordering hint");
}

void TestFactoryBank() {
    MarembaAudioProcessor processor;
    auto* quality = dynamic_cast<juce::AudioParameterChoice*>(processor.parameters().getParameter("oversampling"));
    Expect(quality && quality->choices.size() == 3 && quality->getIndex() == 0,
           "legacy Quality retains three choices and safe raw 0 underneath native mode");
    Expect(processor.parameters().getRawParameterValue(MarembaAudioProcessor::kNativeRateParameterId)->load() == 1,
           "a new plug-in defaults to native 1x");
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

void RestoreState(MarembaAudioProcessor& processor, const juce::ValueTree& state) {
    juce::MemoryBlock data;
    const auto xml = state.createXml();
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
}

void TestLegacyQualityStateAndAutomation() {
    // Recreate a released state's raw PARAM nodes, without the newly appended
    // flag. Raw 0 matters: restoring the same value cannot rely on a callback.
    for (int stored = 0; stored < 3; ++stored) {
        MarembaAudioProcessor restored;
        auto state = restored.parameters().copyState();
        state.removeChild(state.getChildWithProperty("id", MarembaAudioProcessor::kNativeRateParameterId), nullptr);
        state.getChildWithProperty("id", "oversampling").setProperty("value", stored, nullptr);
        RestoreState(restored, state);
        Expect(restored.currentControls()[maremba::kOversampling] == stored,
               "a pre-native state restores its exact legacy quality, including raw 0");
        Expect(restored.parameters().getRawParameterValue(MarembaAudioProcessor::kNativeRateParameterId)->load() == 0,
               "pre-native states explicitly disable the new-instance native default");
        auto* legacy = restored.parameters().getParameter("oversampling");
        // A frozen three-choice JUCE parameter is the released host contract,
        // including its quarter-position/tie rounding, not the new UI count.
        juce::AudioParameterChoice oldParameter({"old", 1}, "Old Quality", {"2x", "4x", "8x"}, 0);
        for (float value : {0.f, .125f, .25f, .375f, .5f, .625f, .75f, .875f, 1.f, 0.f}) {
            oldParameter.setValueNotifyingHost(value);
            legacy->setValueNotifyingHost(value);
            Expect(restored.currentControls()[maremba::kOversampling] == oldParameter.getIndex(),
                   "old normalized automation replays the exact three-choice mapping");
        }
        for (int quality = 0; quality < 3; ++quality) {
            const float normalized = static_cast<float>(quality) * .5f;
            Expect(legacy->convertTo0to1(static_cast<float>(quality)) == normalized,
                   "legacy Quality normalized 0/.5/1 values remain exact");
            legacy->setValueNotifyingHost(normalized);
            Expect(restored.currentControls()[maremba::kOversampling] == quality,
                   "legacy normalized endpoints and midpoint retain raw indices");
        }
    }
}

void TestNativeQualityMigrationAndMode() {
    MarembaAudioProcessor processor;
    auto* legacy = processor.parameters().getParameter("oversampling");
    auto* native = processor.parameters().getParameter(MarembaAudioProcessor::kNativeRateParameterId);
    for (float value : {0.f, .5f, 1.f, 0.f}) {
        legacy->setValueNotifyingHost(value);
        Expect(processor.currentControls()[maremba::kOversampling] == 3,
               "explicit Native Rate mode gates the legacy quality selector");
    }
    legacy->setValueNotifyingHost(.5f);
    native->setValueNotifyingHost(0);
    Expect(processor.currentControls()[maremba::kOversampling] == 1,
           "leaving native mode uses the current legacy 4x choice");
    auto state = processor.parameters().copyState();
    MarembaAudioProcessor restored;
    RestoreState(restored, state);
    Expect(restored.currentControls()[maremba::kOversampling] == 1,
           "modern non-native state preserves the mode and legacy choice");
    native->setValueNotifyingHost(1);
    state = processor.parameters().copyState();
    RestoreState(restored, state);
    Expect(restored.currentControls()[maremba::kOversampling] == 3
           && restored.parameters().getRawParameterValue("oversampling")->load() == 1,
           "modern native state preserves both mode and dormant legacy choice");

    // The interim development branch stored Rack raw 3 directly. Accept it
    // without exposing a fourth normalized choice under the released ID.
    state.removeChild(state.getChildWithProperty("id", MarembaAudioProcessor::kNativeRateParameterId), nullptr);
    state.getChildWithProperty("id", "oversampling").setProperty("value", 3, nullptr);
    RestoreState(restored, state);
    Expect(restored.currentControls()[maremba::kOversampling] == 3
           && restored.parameters().getRawParameterValue("oversampling")->load() == 0,
           "interim raw-3 states migrate to native mode and a valid legacy choice");
}

void TestCompositeQualityEditor() {
    MarembaAudioProcessor processor;
    const std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* combo = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("quality"));
    Expect(combo && combo->getNumItems() == 4 && combo->getSelectedItemIndex() == 3,
           "the editor presents four effective Quality choices with native selected");
    if (!combo) return;
    for (int choice : {1, 2, 0, 3, 2, 3}) {
        combo->setSelectedItemIndex(choice, juce::sendNotificationSync);
        Expect(processor.currentControls()[maremba::kOversampling] == choice
               && combo->getSelectedItemIndex() == choice,
               "composite Quality writes the effective legacy/native selection");
    }
    processor.parameters().getParameter("oversampling")->setValueNotifyingHost(.5f);
    Expect(combo->getSelectedItemIndex() == 3, "dormant legacy automation leaves native UI selected");
    processor.parameters().getParameter(MarembaAudioProcessor::kNativeRateParameterId)->setValueNotifyingHost(0);
    Expect(combo->getSelectedItemIndex() == 1, "host mode automation updates the composite Quality UI");
    processor.setCurrentProgram(1);
    Expect(combo->getSelectedItemIndex() == 3, "factory selection restores native in the composite UI");
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
    TestLegacyQualityStateAndAutomation();
    TestNativeQualityMigrationAndMode();
    TestCompositeQualityEditor();
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
