#pragma once

#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>

class MarembaAudioProcessor final : public juce::AudioProcessor {
public:
    MarembaAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // A long bar, room tail and limiter recovery at the longest decay setting.
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return static_cast<int>(maremba::kFactoryPresets.size()); }
    int getCurrentProgram() override { return fCurrentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return fParameters; }

    // The controls as the engine reads them: the Rack property values, with
    // the plug-in's hidden legacy controls at their defaults.
    maremba::ControlValues currentControls() const;

    // X.Y.Z-build.<run>, as the About box shows it.
    static juce::String buildIdentity();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState fParameters;
    std::array<std::atomic<float>*, maremba::kControlCount> fValues {};
    maremba::MarembaPlayer fPlayer;
    int fCurrentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarembaAudioProcessor)
};
