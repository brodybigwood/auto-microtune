/*
  ==============================================================================
    super-autotune — Plugin Editor with Neon theme
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NeonLookAndFeel.h"
#include "EditorComponents.h"
#include "SpectrumAnalyzer.h"

//==============================================================================
SuperautotuneAudioProcessorEditor::SuperautotuneAudioProcessorEditor (SuperautotuneAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Apply neon theme
    neonLookAndFeel = std::make_unique<NeonLookAndFeel>();
    setLookAndFeel(neonLookAndFeel.get());

    auto& apvts = audioProcessor.getAPVTS();

    // --- Spectrum Display (custom component) ---
    spectrumDisplay = std::make_unique<SpectrumDisplay>();
    addAndMakeVisible(*spectrumDisplay);

    // --- Note selection list ---
    noteList = std::make_unique<NoteList>(audioProcessor);
    addAndMakeVisible(*noteList);

    // --- Retune Speed slider ---
    retuneSpeedSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    retuneSpeedSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 18);
    retuneSpeedSlider.setRange(0.0, 1.0, 0.01);
    addAndMakeVisible(retuneSpeedSlider);
    retuneSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamIDs::retuneSpeed, retuneSpeedSlider);

    retuneSpeedLabel.setText("RETUNE", juce::dontSendNotification);
    retuneSpeedLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(retuneSpeedLabel);

    // --- FFT Size combo ---
    fftSizeCombo.addItemList(FFTSizes::choices, 1);
    addAndMakeVisible(fftSizeCombo);
    fftSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamIDs::fftSize, fftSizeCombo);

    fftSizeLabel.setText("FFT", juce::dontSendNotification);
    fftSizeLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(fftSizeLabel);

    // --- Tuning Mode combo ---
    tuningModeCombo.addItemList(TuningMode::choices, 1);
    addAndMakeVisible(tuningModeCombo);
    tuningModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamIDs::tuningMode, tuningModeCombo);

    tuningModeLabel.setText("MODE", juce::dontSendNotification);
    tuningModeLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(tuningModeLabel);

    // --- MIDI control toggle ---
    midiEnabledButton.setButtonText("MIDI");
    addAndMakeVisible(midiEnabledButton);
    midiEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamIDs::midiEnabled, midiEnabledButton);

    // --- Octave Extend toggle ---
    octaveExtendButton.setButtonText("OCTAVE EXTEND");
    addAndMakeVisible(octaveExtendButton);
    octaveExtendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamIDs::octaveExtend, octaveExtendButton);

    // --- Wet/Dry slider ---
    wetDrySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wetDrySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
    wetDrySlider.setRange(0.0, 1.0, 0.01);
    addAndMakeVisible(wetDrySlider);
    wetDryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamIDs::wetDry, wetDrySlider);

    wetDryLabel.setText("MIX", juce::dontSendNotification);
    wetDryLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(wetDryLabel);

    // --- Bypass toggle ---
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamIDs::bypass, bypassButton);

    // --- Load Tuning File button ---
    loadTuningFileButton.setButtonText("LOAD TUNING FILE...");
    loadTuningFileButton.onClick = [this]
    {
        auto* chooser = new juce::FileChooser("Select a tuning file (.scl, .tun)",
                                              juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                              "*.scl;*.tun");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto results = fc.getResults();
                if (!results.isEmpty())
                {
                    auto file = results.getFirst();
                    audioProcessor.setTuningFilePath(file.getFullPathName());

                    // Every row's frequency column has just changed.
                    if (noteList)
                        noteList->refresh();
                }
                delete chooser;
            });
    };
    addAndMakeVisible(loadTuningFileButton);

    // --- Window ---
    setSize (850, 550);
    setResizable(true, true);
    setResizeLimits(600, 400, 1600, 1000);

    // 30Hz refresh for spectrum display
    startTimerHz(30);
}

SuperautotuneAudioProcessorEditor::~SuperautotuneAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void SuperautotuneAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark background
    g.fillAll(NeonLookAndFeel::bgDark);

    // Title bar
    auto titleBar = getLocalBounds().removeFromTop(32).toFloat();
    g.setColour(NeonLookAndFeel::bgSurface);
    g.fillRect(titleBar);

    g.setColour(NeonLookAndFeel::accentCyan);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("SUPER AUTOTUNE", titleBar.toNearestInt(), juce::Justification::centred);

    // Bottom line on title bar
    g.setColour(NeonLookAndFeel::accentCyan.withAlpha(0.3f));
    g.drawHorizontalLine(32, 0.0f, float(getWidth()));
}

void SuperautotuneAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    area.removeFromTop(36);  // skip title bar area

    const int rowH    = 28;
    const int spacing = 6;

    // The note list runs down the left where it has the height to show the note
    // number, name and tuned frequency for each of the 128 notes.
    noteList->setBounds(area.removeFromLeft(210).reduced(0, 4));
    area.removeFromLeft(8);

    // === RIGHT SIDE ===
    auto rightPanel = area;

    // Controls along the bottom of the right side.
    auto controlArea = rightPanel.removeFromBottom(158);

    auto retuneRow = controlArea.removeFromTop(rowH);
    retuneSpeedLabel.setBounds(retuneRow.removeFromLeft(60));
    retuneSpeedSlider.setBounds(retuneRow);
    controlArea.removeFromTop(spacing);

    auto modeRow = controlArea.removeFromTop(rowH);
    fftSizeLabel.setBounds(modeRow.removeFromLeft(30));
    fftSizeCombo.setBounds(modeRow.removeFromLeft(80));
    tuningModeLabel.setBounds(modeRow.removeFromLeft(52));
    tuningModeCombo.setBounds(modeRow.removeFromLeft(110));
    controlArea.removeFromTop(spacing);

    auto toggleRow = controlArea.removeFromTop(rowH);
    midiEnabledButton.setBounds(toggleRow.removeFromLeft(70));
    toggleRow.removeFromLeft(6);
    octaveExtendButton.setBounds(toggleRow.removeFromLeft(130));
    toggleRow.removeFromLeft(6);
    bypassButton.setBounds(toggleRow.removeFromLeft(80));
    controlArea.removeFromTop(spacing);

    auto mixRow = controlArea.removeFromTop(rowH);
    wetDryLabel.setBounds(mixRow.removeFromLeft(40));
    wetDrySlider.setBounds(mixRow.removeFromLeft(160));
    controlArea.removeFromTop(spacing);

    loadTuningFileButton.setBounds(controlArea.removeFromTop(28));

    // Spectrum takes the rest.
    spectrumDisplay->setBounds(rightPanel.reduced(0, 4));
}

void SuperautotuneAudioProcessorEditor::timerCallback()
{
    // Poll the processor for spectrum data and pitch markers.
    if (spectrumDisplay)
    {
        int numBins = audioProcessor.getNumSpectrumBins();
        if (numBins > 0)
        {
            static std::vector<float> mags;
            mags.resize(numBins);
            audioProcessor.getSpectrumMagnitudes(mags.data(), numBins);

            spectrumDisplay->updateData(mags.data(), numBins,
                                        audioProcessor.getDetectedPitch(),
                                        audioProcessor.getTargetPitch(),
                                        audioProcessor.getSampleRateForGUI(),
                                        0 /* unused fftSize hint */);
            spectrumDisplay->repaint();
        }
    }

    // Highlight the note currently being snapped to in the list.
    if (noteList)
        noteList->setActiveNote(audioProcessor.getTargetNote());
}
