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

    // Declared early so it is destroyed LAST, after every component that draws
    // with it. Declared late, it would die first and leave the controls holding a
    // dangling LookAndFeel pointer while they are torn down.
    std::unique_ptr<juce::LookAndFeel> neonLookAndFeel;

    // Controls FIRST, attachments second.
    //
    // Members are destroyed in reverse order of declaration, so an attachment
    // declared before its control is destroyed AFTER it. SliderParameterAttachment's
    // destructor calls slider.removeListener() on the slider it was given, so that
    // ordering means it reaches through a freed object — which crashed on every
    // editor close. Declaring the controls first makes the attachments die first.
    juce::Slider  retuneSpeedSlider;
    juce::ComboBox fftSizeCombo;     // spectrum display resolution only
    juce::ComboBox tuningModeCombo;
    juce::ToggleButton midiEnabledButton;
    juce::ToggleButton octaveExtendButton;
    juce::Slider  wetDrySlider;
    juce::ToggleButton bypassButton;

    // Created in the constructor body, once the APVTS exists.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> retuneSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuningModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> octaveExtendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetDryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Labels
    juce::Label retuneSpeedLabel;
    juce::Label fftSizeLabel;
    juce::Label tuningModeLabel;
    juce::Label wetDryLabel;

    juce::TextButton loadTuningFileButton;

    // Custom GUI components (implemented in Phase 6)
    std::unique_ptr<SpectrumDisplay> spectrumDisplay;
    std::unique_ptr<NoteList>        noteList;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperautotuneAudioProcessorEditor)
};
