/*
  ==============================================================================
    super-autotune — Plugin Editor
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Forward declarations for GUI components
class SpectrumDisplay;
class NoteList;

//==============================================================================
class SuperautotuneAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                            public juce::Timer
{
public:
    SuperautotuneAudioProcessorEditor (SuperautotuneAudioProcessor&);
    ~SuperautotuneAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SuperautotuneAudioProcessor& audioProcessor;

    // Parameter attachments (created in constructor body after APVTS is ready)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> retuneSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuningModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> octaveExtendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetDryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Basic controls (will be expanded in Phase 6)
    juce::Slider  retuneSpeedSlider;
    juce::ComboBox fftSizeCombo;     // spectrum display resolution only
    juce::ComboBox tuningModeCombo;
    juce::ToggleButton midiEnabledButton;
    juce::ToggleButton octaveExtendButton;
    juce::Slider  wetDrySlider;
    juce::ToggleButton bypassButton;

    // Labels
    juce::Label retuneSpeedLabel;
    juce::Label fftSizeLabel;
    juce::Label tuningModeLabel;
    juce::Label wetDryLabel;

    juce::TextButton loadTuningFileButton;

    // Custom GUI components (implemented in Phase 6)
    std::unique_ptr<SpectrumDisplay> spectrumDisplay;
    std::unique_ptr<NoteList>        noteList;

    // Custom LookAndFeel
    std::unique_ptr<juce::LookAndFeel> neonLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperautotuneAudioProcessorEditor)
};
