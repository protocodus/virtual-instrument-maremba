#include "PluginProcessor.h"

#include "PluginControls.h"
#include "PluginEditor.h"

#include <cmath>

namespace {

using maremba::EControlStyle;
using maremba::kControlDisplay;
using maremba::kControlNames;

bool IsParameter(int control) {
    return kControlDisplay[static_cast<std::size_t>(control)].style != EControlStyle::Hidden;
}

juce::String KnobText(int control, float value) {
    const auto& display = kControlDisplay[static_cast<std::size_t>(control)];
    const double shown = display.style == EControlStyle::DecayKnob
        ? static_cast<double>(maremba::DecayFromControl(value))
        : display.min + static_cast<double>(value) * (display.max - display.min);
    // juce::String(x, 0) would mean "as many places as needed".
    const auto number = display.decimals > 0 ? juce::String(shown, display.decimals) : juce::String(juce::roundToInt(shown));
    return juce::String(display.prefix) + number + display.suffix;
}

float KnobValue(int control, const juce::String& text) {
    const auto& display = kControlDisplay[static_cast<std::size_t>(control)];
    const double shown = text.retainCharacters("-+.0123456789").getDoubleValue();
    const double value = display.style == EControlStyle::DecayKnob
        ? static_cast<double>(maremba::DecayToControl(static_cast<float>(shown)))
        : (shown - display.min) / (display.max - display.min);
    return static_cast<float>(juce::jlimit(0.0, 1.0, value));
}

const juce::Identifier kProgramProperty { "program" };

} // namespace

MarembaAudioProcessor::MarembaAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Main", juce::AudioChannelSet::stereo(), true)
          .withOutput("Close", juce::AudioChannelSet::stereo(), false)
          .withOutput("Room", juce::AudioChannelSet::stereo(), false)
          .withOutput("Piezo", juce::AudioChannelSet::mono(), false))
    , fParameters(*this, nullptr, "Maremba", createLayout())
{
    for (int control = 0; control < maremba::kControlCount; ++control)
        if (IsParameter(control))
            fValues[static_cast<std::size_t>(control)] = fParameters.getRawParameterValue(kControlNames[static_cast<std::size_t>(control)]);
    fNativeRate = fParameters.getRawParameterValue(kNativeRateParameterId);
    // A new instance opens on the first factory preset, which the editor's
    // preset menu shows; a restored session replaces it.
    setCurrentProgram(0);
}

// Parameter IDs are the Rack property names, and a parameter's raw value is
// the Rack property value: 0..1 for a knob, the step index for a choice.
// Quality is composed from the original 2x/4x/8x parameter and an appended
// native-rate switch, preserving old host automation at normalized 0/.5/1.
juce::AudioProcessorValueTreeState::ParameterLayout MarembaAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int control = 0; control < maremba::kControlCount; ++control) {
        if (!IsParameter(control)) continue;
        const auto index = static_cast<std::size_t>(control);
        const auto& display = kControlDisplay[index];
        const juce::ParameterID id { kControlNames[index], 1 };
        const auto initial = maremba::kControlDefaults[index];
        if (display.style == EControlStyle::Choice) {
            juce::StringArray choices;
            for (const char* choice : display.choices) {
                if (choice == nullptr) continue;
                if (control == maremba::kOversampling && choices.size() == 3) break;
                choices.add(choice);
            }
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                id, display.label, choices, control == maremba::kOversampling ? 0 : static_cast<int>(initial)));
        } else {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id, display.label, juce::NormalisableRange<float>(0.0f, 1.0f), static_cast<float>(initial),
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction([control](float value, int) { return KnobText(control, value); })
                    .withValueFromStringFunction([control](const juce::String& text) { return KnobValue(control, text); })));
        }
    }
    // Keep every legacy parameter's position and version hint unchanged.
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kNativeRateParameterId, 2 }, "Native Rate (1x)", true));
    return layout;
}

maremba::ControlValues MarembaAudioProcessor::currentControls() const {
    // UI/preset writers set the legacy choice before disabling native mode.
    // Observe that flag first so a newly observed false cannot pair with an
    // older dormant legacy value during the transition out of 1x.
    const bool nativeRate = fNativeRate->load() >= 0.5f;
    maremba::ControlValues values = maremba::kControlDefaults;
    for (std::size_t control = 0; control < values.size(); ++control)
        if (fValues[control] != nullptr) values[control] = fValues[control]->load();
    if (nativeRate) values[maremba::kOversampling] = 3;
    return values;
}

void MarembaAudioProcessor::prepareToPlay(double sampleRate, int) {
    fPlayer.Prepare(sampleRate);
    fPlayer.SetControls(currentControls());
}

bool MarembaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto disabledOr = [](const juce::AudioChannelSet& set, const juce::AudioChannelSet& only) {
        return set.isDisabled() || set == only;
    };
    return layouts.outputBuses.size() == 4
        && disabledOr(layouts.getChannelSet(false, 1), juce::AudioChannelSet::stereo())
        && disabledOr(layouts.getChannelSet(false, 2), juce::AudioChannelSet::stereo())
        && disabledOr(layouts.getChannelSet(false, 3), juce::AudioChannelSet::mono());
}

void MarembaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    fPlayer.SetControls(currentControls());

    const auto channel = [&](int bus, int index) -> float* {
        if (bus >= getBusCount(false) || !getBus(false, bus)->isEnabled()) return nullptr;
        return getBusBuffer(buffer, false, bus).getWritePointer(index);
    };
    float* mainL = channel(0, 0);
    float* mainR = channel(0, 1);
    float* closeL = channel(1, 0);
    float* closeR = channel(1, 1);
    float* farL = channel(2, 0);
    float* farR = channel(2, 1);
    float* piezo = channel(3, 0);
    const auto offset = [](float* p, int frames) { return p ? p + frames : nullptr; };

    // Render up to each event's sample, then apply it, so every strike lands
    // on its own sample whatever the host's block size.
    int done = 0;
    const auto renderTo = [&](int end) {
        if (end <= done) return;
        fPlayer.Render(offset(mainL, done), offset(mainR, done), offset(closeL, done), offset(closeR, done),
                       offset(farL, done), offset(farR, done), offset(piezo, done), end - done);
        done = end;
    };
    const int frames = buffer.getNumSamples();
    for (const auto event : midi) {
        renderTo(juce::jlimit(0, frames, event.samplePosition));
        fPlayer.HandleMidi(event.data, event.numBytes);
    }
    renderTo(frames);
}

void MarembaAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= getNumPrograms()) return;
    fCurrentProgram = index;
    const auto& values = maremba::kFactoryPresets[static_cast<std::size_t>(index)].values;
    for (int control = 0; control < maremba::kControlCount; ++control) {
        if (!IsParameter(control)) continue;
        auto* parameter = fParameters.getParameter(kControlNames[static_cast<std::size_t>(control)]);
        // A Rack/native preset's raw 3 is not a fourth legacy host choice.
        const auto value = control == maremba::kOversampling && values[maremba::kOversampling] == 3
            ? 0.0f : static_cast<float>(values[static_cast<std::size_t>(control)]);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    fParameters.getParameter(kNativeRateParameterId)->setValueNotifyingHost(
        values[maremba::kOversampling] == 3 ? 1.0f : 0.0f);
}

const juce::String MarembaAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= getNumPrograms()) return {};
    return maremba::kFactoryPresets[static_cast<std::size_t>(index)].name;
}

void MarembaAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = fParameters.copyState();
    state.setProperty(kProgramProperty, fCurrentProgram, nullptr);
    if (const auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MarembaAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(fParameters.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    auto legacyQuality = state.getChildWithProperty("id", "oversampling");
    auto nativeRate = state.getChildWithProperty("id", kNativeRateParameterId);
    const bool storedNative = legacyQuality.isValid()
        && static_cast<float>(legacyQuality.getProperty("value", 0.0f)) >= 2.5f;
    if (!nativeRate.isValid()) {
        // Released three-choice states must opt out of the new-instance 1x
        // default, including raw 0 when no parameter-change callback occurs.
        // Also migrate raw 3 from the interim four-choice development state.
        nativeRate = juce::ValueTree("PARAM");
        nativeRate.setProperty("id", kNativeRateParameterId, nullptr);
        nativeRate.setProperty("value", storedNative ? 1.0f : 0.0f, nullptr);
        state.addChild(nativeRate, -1, nullptr);
    }
    if (storedNative) legacyQuality.setProperty("value", 0.0f, nullptr);
    fCurrentProgram = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(state.getProperty(kProgramProperty, 0)));
    fParameters.replaceState(state);
}

juce::String MarembaAudioProcessor::buildIdentity() {
    return juce::String(JucePlugin_VersionString) + "-build." + MAREMBA_BUILD_NUMBER;
}

juce::AudioProcessorEditor* MarembaAudioProcessor::createEditor() {
    return new MarembaAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MarembaAudioProcessor();
}
