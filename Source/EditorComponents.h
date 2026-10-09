/*
  ==============================================================================
    super-autotune — Custom Editor Components
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class SuperautotuneAudioProcessor;

//==============================================================================
// Real-time spectrum display with pitch markers
//==============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay();

    void paint(juce::Graphics& g) override;

    // Feed new spectrum data from the audio thread.
    // mags: magnitude bins, numBins long
    // detectedHz: current detected pitch (0 = none)
    // targetHz: current target pitch (0 = none)
    void updateData(const float* mags, int numBins,
                    float detectedHz, float targetHz,
                    double sampleRate, int fftSize);

private:
    std::vector<float> displayMags;
    int numBins = 512;
    float detectedPitch = 0.0f;
    float targetPitch   = 0.0f;
    double sampleRate  = 44100.0;
    int fftSize        = 1024;

    juce::CriticalSection dataLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

//==============================================================================
// Note list — a scrollable list of all 128 MIDI notes.
//
// This replaces a piano keyboard. A keyboard shows 12 pitch classes and hides
// everything else, so once a tuning file is loaded you cannot see which notes it
// actually defines or what frequency each one is. This shows every note number
// with the frequency it maps to, which is the information you need to choose.
//==============================================================================
class NoteList : public juce::Component
{
public:
    NoteList(SuperautotuneAudioProcessor& processor);

    void resized() override;

    // Re-read the note -> frequency mapping (call after loading a tuning file).
    void refresh();

    // Highlight the note currently being snapped to (-1 = none) and scroll it
    // into view.
    void setActiveNote(int midiNote);

private:
    // Number of MIDI notes. Row 0 of the list is the highest note, so note numbers
    // read upward from the bottom.
    static constexpr int kNumNotes = 128;

    struct Model : public juce::ListBoxModel
    {
        explicit Model(NoteList& o) : owner(o) {}

        int  getNumRows() override { return kNumNotes; }
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

        NoteList& owner;
    };

    SuperautotuneAudioProcessor& audioProcessor;

    juce::ListBox list;
    std::unique_ptr<Model> model;
    int activeNote = -1;   // note currently snapped to (-1 = none)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteList)
};
