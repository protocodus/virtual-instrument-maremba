#pragma once

#include "PluginProcessor.h"

#include <JuceHeader.h>

#include <memory>
#include <vector>

class MarembaAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit MarembaAudioProcessorEditor(MarembaAudioProcessor&);
    ~MarembaAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Control {
        int id = 0;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::ComboBox> choice;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
    };
    struct Section {
        juce::String title;
        std::vector<int> controls;
        juce::Rectangle<int> bounds;
    };

    void refreshPresetMenu();

    MarembaAudioProcessor& fProcessor;
    juce::LookAndFeel_V4 fLook;
    juce::ComboBox fPresets;
    juce::Label fAbout;
    std::vector<Section> fSections;
    std::vector<Control> fControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarembaAudioProcessorEditor)
};
