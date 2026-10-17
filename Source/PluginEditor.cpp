#include "PluginEditor.h"

#include "PluginControls.h"

namespace {

using namespace maremba;

constexpr int kWidth = 1290;
constexpr int kHeight = 616;
constexpr int kMargin = 16;
constexpr int kHeader = 56;
constexpr int kTitleWidth = 110;
constexpr int kRowHeight = 104;
constexpr int kRowGap = 8;
constexpr int kKnobWidth = 84;
constexpr int kChoiceWidth = 152;

const juce::Colour kBackground { 0xff1d1a17 };
const juce::Colour kPanel { 0xff2a2521 };
const juce::Colour kAccent { 0xffc98b4a };
const juce::Colour kText { 0xffeae1d6 };
const juce::Colour kDim { 0xffa89c8f };

bool IsChoice(int control) {
    return kControlDisplay[static_cast<std::size_t>(control)].style == EControlStyle::Choice;
}

} // namespace

MarembaAudioProcessorEditor::MarembaAudioProcessorEditor(MarembaAudioProcessor& processor)
    : AudioProcessorEditor(processor), fProcessor(processor)
{
    fLook.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    fLook.setColour(juce::Slider::rotarySliderOutlineColourId, kBackground);
    fLook.setColour(juce::Slider::thumbColourId, kText);
    fLook.setColour(juce::Slider::textBoxTextColourId, kText);
    fLook.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    fLook.setColour(juce::ComboBox::backgroundColourId, kBackground);
    fLook.setColour(juce::ComboBox::outlineColourId, kAccent.withAlpha(0.5f));
    fLook.setColour(juce::ComboBox::textColourId, kText);
    fLook.setColour(juce::ComboBox::arrowColourId, kAccent);
    fLook.setColour(juce::PopupMenu::backgroundColourId, kPanel);
    fLook.setColour(juce::PopupMenu::textColourId, kText);
    fLook.setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    fLook.setColour(juce::Label::textColourId, kDim);
    setLookAndFeel(&fLook);

    fSections = {
        { "Instrument", { kModel, kMalletType, kTuningScale, kNoteOffMode, kMalletHardness,
                          kStrikePosition, kStrikeJitter, kCarving, kMembraneTension }, {} },
        { "Resonance", { kResonatorTune, kResonatorCoupling, kDecay, kBuzzAmount, kArtifacts,
                         kSympathetic, kBodyBloom }, {} },
        { "Capture", { kRoomType, kInvertPiezo, kCloseLevel, kFarLevel, kPiezoLevel, kStereoWidth }, {} },
        { "Output", { kOversampling, kPolyphony, kPreampDrive, kWarmth, kCompAmount, kCompAttack,
                      kCompRelease, kVolume, kMasterTune, kDetune }, {} },
        { "Velocity", { kVelocityCurve, kVelocityToHardness, kVelocityToPosition, kVelocityToDecay,
                        kVelocityToCarving, kVelocityToCoupling, kVelocityToBloom, kVelocityToSympathetic,
                        kVelocityToBuzz, kVelocityToTension, kVelocityToArtifacts, kVelocityToVariance }, {} },
    };

    auto& state = fProcessor.parameters();
    for (const auto& section : fSections) {
        for (const int id : section.controls) {
            const auto index = static_cast<std::size_t>(id);
            Control control;
            control.id = id;
            // Velocity knobs share their section's name: "VEL - Hardness" reads as "Hardness".
            const juce::String label = kControlDisplay[index].label;
            control.label = std::make_unique<juce::Label>(
                juce::String(), label.startsWith("VEL - ") ? label.fromFirstOccurrenceOf("VEL - ", false, false) : label);
            control.label->setJustificationType(juce::Justification::centred);
            control.label->setFont(juce::FontOptions(12.0f));
            control.label->setMinimumHorizontalScale(0.5f);
            addAndMakeVisible(*control.label);
            if (IsChoice(id)) {
                control.choice = std::make_unique<juce::ComboBox>();
                // The attachment selects items but does not add them.
                if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(kControlNames[index])))
                    control.choice->addItemList(parameter->choices, 1);
                addAndMakeVisible(*control.choice);
                control.choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                    state, kControlNames[index], *control.choice);
            } else {
                control.knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                              juce::Slider::TextBoxBelow);
                control.knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kKnobWidth, 16);
                control.knob->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                addAndMakeVisible(*control.knob);
                control.knobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    state, kControlNames[index], *control.knob);
            }
            fControls.push_back(std::move(control));
        }
    }

    refreshPresetMenu();
    fPresets.onChange = [this] {
        if (const int id = fPresets.getSelectedId(); id > 0) fProcessor.setCurrentProgram(id - 1);
    };
    addAndMakeVisible(fPresets);

    fAbout.setText("Version " + MarembaAudioProcessor::buildIdentity() + "  |  Protocodus",
                   juce::dontSendNotification);
    fAbout.setJustificationType(juce::Justification::centredRight);
    fAbout.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(fAbout);

    setSize(kWidth, kHeight);
}

MarembaAudioProcessorEditor::~MarembaAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void MarembaAudioProcessorEditor::refreshPresetMenu() {
    fPresets.clear(juce::dontSendNotification);
    for (int program = 0; program < fProcessor.getNumPrograms(); ++program)
        fPresets.addItem(fProcessor.getProgramName(program), program + 1);
    fPresets.setSelectedId(fProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
}

void MarembaAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    g.setColour(kText);
    g.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    g.drawText("MAREMBA", kMargin, 0, 220, kHeader, juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Physically modelled marimba, balafon and kalimba", kMargin + 160, 0, 360, kHeader,
               juce::Justification::centredLeft);

    for (const auto& section : fSections) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(section.bounds.toFloat(), 6.0f);
        g.setColour(kAccent);
        g.fillRect(section.bounds.getX(), section.bounds.getY() + 10, 3, section.bounds.getHeight() - 20);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(section.title.toUpperCase(), section.bounds.getX() + 14, section.bounds.getY(),
                   kTitleWidth - 14, section.bounds.getHeight(), juce::Justification::centredLeft);
    }
}

void MarembaAudioProcessorEditor::resized() {
    fPresets.setBounds(kWidth - kMargin - 600, 14, 280, 28);
    fAbout.setBounds(kWidth - kMargin - 300, 14, 300, 28);

    auto control = fControls.begin();
    int y = kHeader;
    for (auto& section : fSections) {
        section.bounds = { kMargin, y, kWidth - 2 * kMargin, kRowHeight };
        int x = kMargin + kTitleWidth;
        for (std::size_t i = 0; i < section.controls.size(); ++i, ++control) {
            const bool choice = control->choice != nullptr;
            const int width = choice ? kChoiceWidth : kKnobWidth;
            control->label->setBounds(x, y + 8, width, 16);
            if (choice)
                control->choice->setBounds(x + 6, y + 44, width - 12, 26);
            else
                control->knob->setBounds(x, y + 24, width, kRowHeight - 30);
            x += width;
        }
        y += kRowHeight + kRowGap;
    }
}
