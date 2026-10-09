/*
  ==============================================================================
    super-autotune — Custom Editor Components implementation
  ==============================================================================
*/

#include "EditorComponents.h"
#include "PluginProcessor.h"
#include "NeonLookAndFeel.h"

//==============================================================================
// SpectrumDisplay
//==============================================================================
SpectrumDisplay::SpectrumDisplay()
{
    displayMags.resize(512, 0.0f);
}

void SpectrumDisplay::updateData(const float* mags, int nBins,
                                  float detectedHz, float targetHz,
                                  double sr, int fftSz)
{
    juce::ScopedLock lock(dataLock);

    numBins = std::min(nBins, 512);
    std::copy(mags, mags + numBins, displayMags.begin());

    detectedPitch = detectedHz;
    targetPitch   = targetHz;
    sampleRate    = sr;
    fftSize       = fftSz;
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto w = bounds.getWidth();
    auto h = bounds.getHeight();

    // Background
    g.setColour(NeonLookAndFeel::bgDark);
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(NeonLookAndFeel::accentCyan.withAlpha(0.1f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Spectrum bars
    juce::ScopedLock lock(dataLock);

    if (numBins < 2)
        return;

    auto plotArea = bounds.reduced(10.0f, 20.0f);
    float barWidth = plotArea.getWidth() / numBins;

    // Find max magnitude for normalization
    float maxMag = 0.0001f;
    for (int i = 0; i < numBins; ++i)
        maxMag = std::max(maxMag, displayMags[i]);

    // Log scale for better visualization
    float logMax = std::log10(maxMag + 1.0f);

    for (int i = 0; i < numBins; ++i)
    {
        float normalized = std::log10(displayMags[i] + 1.0f) / logMax;
        float barH = normalized * plotArea.getHeight();
        float x = plotArea.getX() + i * barWidth;
        float y = plotArea.getBottom() - barH;

        // Gradient color from cyan (low) to magenta (high)
        auto colour = NeonLookAndFeel::accentCyan.interpolatedWith(
            NeonLookAndFeel::accentMagenta, normalized);
        g.setColour(colour.withAlpha(0.7f));
        g.fillRoundedRectangle(x, y, barWidth - 1.0f, barH, 1.5f);
    }

    // Frequency axis labels
    g.setColour(NeonLookAndFeel::textDim);
    g.setFont(10.0f);
    float maxFreq = sampleRate / 2.0f;
    for (int kHz = 0; kHz <= static_cast<int>(maxFreq / 1000.0f); kHz += 2)
    {
        float freq = kHz * 1000.0f;
        float xFrac = freq / maxFreq;
        float xPos = plotArea.getX() + xFrac * plotArea.getWidth();
        g.drawText(juce::String(kHz) + "k", int(xPos - 15), int(plotArea.getBottom() + 2),
                   30, 14, juce::Justification::centred);
    }

    // Detected pitch marker
    if (detectedPitch > 1.0f)
    {
        float xFrac = detectedPitch / maxFreq;
        float xPos = plotArea.getX() + xFrac * plotArea.getWidth();
        g.setColour(NeonLookAndFeel::accentCyan);
        g.drawVerticalLine(int(xPos), plotArea.getY(), plotArea.getBottom());
        g.setFont(12.0f);
        g.drawText(juce::String(detectedPitch, 0) + " Hz",
                   int(xPos) - 30, int(plotArea.getY() - 16), 60, 14,
                   juce::Justification::centred);
    }

    // Target pitch marker
    if (targetPitch > 1.0f)
    {
        float xFrac = targetPitch / maxFreq;
        float xPos = plotArea.getX() + xFrac * plotArea.getWidth();
        float dashLengths[] = { 4.0f, 4.0f };
        juce::Line<float> dashLine(xPos, plotArea.getY(), xPos, plotArea.getBottom());
        g.setColour(NeonLookAndFeel::accentMagenta);
        g.drawDashedLine(dashLine, dashLengths, 2);
        g.setFont(12.0f);
        g.drawText(juce::String(targetPitch, 0) + " Hz",
                   int(xPos) - 30, int(plotArea.getY() - 2), 60, 14,
                   juce::Justification::centred);
    }
}

//==============================================================================
// NoteList
//==============================================================================
NoteList::NoteList(SuperautotuneAudioProcessor& processor)
    : audioProcessor(processor)
{
    model = std::make_unique<Model>(*this);

    list.setModel(model.get());
    list.setRowHeight(20);
    list.setColour(juce::ListBox::backgroundColourId, NeonLookAndFeel::bgDark);
    list.setColour(juce::ListBox::outlineColourId, NeonLookAndFeel::accentCyan.withAlpha(0.15f));
    list.setOutlineThickness(1);
    addAndMakeVisible(list);
}

void NoteList::resized()
{
    list.setBounds(getLocalBounds());
}

void NoteList::refresh()
{
    list.repaint();
}

void NoteList::setActiveNote(int midiNote)
{
    // Highlight only. The list is for browsing the notes by hand, so it must never
    // scroll itself — following the sung note around drags the view away from
    // wherever you were looking.
    activeNote = midiNote;

    // Repaint every tick, not only on a change: the selection can also change
    // underneath us when a saved state is restored or a tuning file is loaded.
    list.repaint();
}

void NoteList::Model::paintListBoxItem(int row, juce::Graphics& g,
                                       int width, int height, bool)
{
    // The list runs bottom to top: note 0 is the bottom row, so note numbers
    // increase upward the way pitch is normally drawn. Row 0 is the highest note,
    // hence the reversal on every lookup.
    const int note = NoteList::kNumNotes - 1 - row;

    const bool on = owner.audioProcessor.isNoteEnabled(note);

    // A selected note is filled; the note being sung to is outlined.
    if (on)
    {
        g.setColour(NeonLookAndFeel::accentCyan.withAlpha(0.28f));
        g.fillRect(0, 0, width, height);
    }

    if (note == owner.activeNote)
    {
        g.setColour(juce::Colours::lime.withAlpha(0.9f));
        g.drawRect(0, 0, width, height, 2);
    }

    g.setFont(juce::FontOptions(12.0f));

    g.setColour(on ? juce::Colours::white : NeonLookAndFeel::textDim);
    g.drawText(juce::String(note), 8, 0, 44, height, juce::Justification::centredLeft);

    // The note's pitch in cents as the tuning file defines it. For a .tun that is
    // the value written in its [Tuning] section, so a 24-TET table reads
    // 0, 50, 100, ...; with no file loaded it is plain 12-TET, note * 100.
    const int cents = juce::roundToInt(owner.audioProcessor.getNoteCents(note));

    g.setColour((on ? juce::Colours::white : NeonLookAndFeel::textDim).withAlpha(0.75f));
    g.drawText(juce::String(cents), 54, 0, width - 62, height, juce::Justification::centredLeft);
}

void NoteList::Model::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    owner.audioProcessor.toggleNote(NoteList::kNumNotes - 1 - row);
    owner.list.repaint();
}
