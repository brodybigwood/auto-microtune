/*
  ==============================================================================
    super-autotune — MIDI Manager
    Tracks active notes from MPE, standard MIDI, and MIDI 2.0 input.
    Exposes a vector of active note frequencies for tuning.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>

//==============================================================================
class MidiManager
{
public:
    MidiManager();

    // Process a MidiBuffer. Call once per processBlock.
    void process(const juce::MidiBuffer& midiBuffer, double sampleRate);

    // Get the frequencies of all currently active notes.
    // For MPE: includes per-note pitch bend adjustment.
    // For standard MIDI: 12-TET note frequencies.
    std::vector<float> getActiveFrequencies() const;

    // Active notes as (MIDI note number, frequency) pairs. Same frequencies as
    // above, but carrying the note number the key was pressed at, so a
    // MIDI-driven target can report which note it belongs to as well as what
    // pitch it should produce.
    std::vector<std::pair<int, float>> getActiveNoteFrequencies() const;

    // Returns true if any notes are currently held.
    bool hasActiveNotes() const { return !activeNotes.empty(); }

    // Get the number of active notes.
    int getNumActiveNotes() const { return static_cast<int>(activeNotes.size()); }

    // Reset state (call on sample rate change or prepare).
    void reset();

private:
    // Convert MIDI note number to frequency (12-TET).
    static float noteToFrequency(int noteNumber, float baseTune = 440.0f);

    // A tracked note (standard MIDI or per-MPE-channel).
    struct TrackedNote
    {
        int noteNumber   = -1;
        int channel      = 0;
        float frequency  = 0.0f;  // current frequency including pitch bend
        float baseFreq   = 0.0f;  // frequency from note number alone
        bool  isMPE      = false;
    };

    // Active notes, keyed by (channel << 8) | noteNumber for MPE,
    // or by noteNumber for standard MIDI.
    std::map<int, TrackedNote> activeNotes;

    // Per-channel pitch bend values (14-bit, center = 8192).
    // For MPE, each channel's pitch bend applies to its one note.
    std::array<int, 16> channelPitchBend{};

    // MPE pitch bend range in semitones (default MPE: +/- 48 semitones for full range,
    // but actual typical usage is +/- 2 semitones for per-note expression).
    // We use a configurable range. MPE standard default is +/- 2 semitones
    // with a registration message that can set +/- 24 or +/- 48.
    float mpeBendRangeSemitones = 48.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiManager)
};
