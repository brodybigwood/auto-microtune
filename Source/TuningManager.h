/*
  ==============================================================================
    super-autotune — Tuning Manager

    Owns what notes are allowed and what frequency each one is.

    The important part is that there is exactly ONE lookup, findTarget(). It
    returns the frequency to snap to AND the MIDI note that frequency belongs to,
    from the same computation. The previous version had two independent searches
    — getNearestNote() over a frequency list and getNearestNoteNumber() over the
    enabled-note flags — and they could disagree, so the note the audio snapped to
    was not always the note the UI highlighted.

    Selection semantics:
      * Nothing is selected by default.
      * Clicking a note in the list toggles that MIDI note.
      * With octave extend on, a selected note is replicated across octaves by
        exact powers of two, and the replicas are real entries with their own MIDI
        note numbers, so the highlight still matches the audio.
      * Loading a tuning file sets the note -> frequency mapping only. It does not
        change the selection.
      * With MIDI armed, held MIDI notes are the sole source; the list selection
        is ignored while it is armed.
      * No targets at all means no correction.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <utility>
#include "ScaleFileParser.h"

//==============================================================================
// The result of matching an input frequency to the allowed notes.
struct TuningTarget
{
    float freqHz   = 0.0f;   // what the audio should snap to
    int   midiNote = -1;     // what the UI should highlight
    bool  valid    = false;  // false when nothing is selected
};

//==============================================================================
class TuningManager
{
public:
    TuningManager();

    // Load a tuning file (.scl or .tun). Sets the note -> frequency mapping only;
    // the note selection is left alone. Returns true on success.
    bool loadScaleFile (const juce::File& file);

    // Drop any loaded mapping and fall back to 12-TET. Does not clear the selection.
    void clearScaleFile();

    bool hasTuningFile() const { return tuningFileLoaded; }
    juce::String getScaleDescription() const { return scaleDescription; }

    //==============================================================================
    // The single source of truth. `inputHz` <= 0 or no targets gives an invalid
    // result, which the caller should treat as "no correction".
    TuningTarget findTarget (float inputHz) const;

    //==============================================================================
    // Note selection (MIDI notes 0-127).
    void toggleNote (int midiNote);
    void setNoteEnabled (int midiNote, bool enabled);
    bool isNoteEnabled (int midiNote) const;

    // The frequency this MIDI note maps to under the loaded file, or 12-TET.
    float getNoteFrequency (int midiNote) const;

    // The cents this note is tuned to, as the loaded file defines it. For a .tun
    // that is the value written in its [Tuning] section, so a 24-TET table reads
    // 0, 50, 100, ... For a .scl it is the note's pitch in cents above the scale's
    // 1/1. With no file loaded it is plain 12-TET, noteNumber * 100.
    float getNoteCents (int midiNote) const;

    //==============================================================================
    // Settings.
    void setOctaveExtend (bool extend);
    void setMidiEnabled (bool enabled);

    // Held MIDI notes as (note number, frequency) pairs. Only used when MIDI is
    // armed. Frequencies already include any per-note pitch bend.
    void setMidiNotes (const std::vector<std::pair<int, float>>& notes);
    void clearMidiNotes();

    //==============================================================================
    // Persistence of the selection.
    juce::String getNoteMask() const;
    void setNoteMask (const juce::String& mask);

    // The whole note -> frequency mapping, serialised. Saved with the project so a
    // reload does not depend on the tuning file still being at the path it came
    // from — which would otherwise silently fall back to 12-TET if the file moved,
    // and look like the tuning had changed on its own.
    juce::String getTuningTable() const;
    void setTuningTable (const juce::String& table);

    //==============================================================================
    // Multiply mode: one voice per selected note, shifted to the octave nearest
    // the input so the harmony sits around the singer rather than a fixed distance
    // away. Returns the input frequency when there is no such voice.
    int   getVoiceCount() const;
    float getMultiplyNote (float inputHz, int voiceIndex) const;

    // Target frequencies, for display.
    const std::vector<float>& getActiveNotes() const { return activeFreqs; }
    juce::StringArray getActiveNoteNames() const;

private:
    // Rebuild the target list from the selection and the current settings.
    void rebuild();

    // The voiceIndex-th selected note, or false when out of range.
    bool getVoiceSource (int voiceIndex, int& midiNote, float& freqHz) const;

    static juce::String freqToNoteName (float freqHz);

    //==============================================================================
    // Which MIDI notes are selected. Default: nothing.
    std::vector<bool> enabledNotes = std::vector<bool> (128, false);

    // Frequency per MIDI note from the loaded file (0.0f = use 12-TET).
    std::vector<float> noteFreqs = std::vector<float> (128, 0.0f);

    // Cents per MIDI note from the loaded file. Only meaningful when a file is
    // loaded; getNoteCents falls back to 12-TET otherwise.
    std::vector<float> noteCents = std::vector<float> (128, 0.0f);

    // The selected notes' own frequencies, for display only. findTarget() reads the
    // selection directly, so there is no precomputed target table.
    std::vector<float> activeFreqs;

    // Held MIDI notes (note number, frequency).
    std::vector<std::pair<int, float>> midiNotes;

    // Settings.
    bool octaveExtend    = true;
    bool midiEnabled     = false;
    bool tuningFileLoaded = false;

    juce::String scaleDescription;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningManager)
};
