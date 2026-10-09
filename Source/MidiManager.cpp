/*
  ==============================================================================
    super-autotune — MIDI Manager implementation
  ==============================================================================
*/

#include "MidiManager.h"

MidiManager::MidiManager()
{
    std::fill(channelPitchBend.begin(), channelPitchBend.end(), 8192);
}

//==============================================================================
void MidiManager::process(const juce::MidiBuffer& midiBuffer, double /*sampleRate*/)
{
    for (const auto metadata : midiBuffer)
    {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel();  // 1-16

        if (msg.isNoteOn())
        {
            int noteNumber = msg.getNoteNumber();
            float velocity = msg.getFloatVelocity();

            if (velocity > 0.0f)
            {
                // Note on
                int key;
                TrackedNote note;
                note.noteNumber = noteNumber;
                note.channel    = channel;
                note.baseFreq   = noteToFrequency(noteNumber);

                // Check if this is an MPE channel (channels 2-15 are per-note in lower zone)
                if (channel >= 2 && channel <= 15)
                {
                    note.isMPE = true;
                    key = (channel << 8) | noteNumber;

                    // Apply current pitch bend for this channel
                    float bend = static_cast<float>(channelPitchBend[channel - 1] - 8192) / 8192.0f;
                    float bendCents = bend * mpeBendRangeSemitones * 100.0f;
                    note.frequency = note.baseFreq * std::pow(2.0f, bendCents / 1200.0f);
                }
                else
                {
                    // Standard MIDI: use noteNumber as key (all channels contribute)
                    note.isMPE = false;
                    key = noteNumber;  // simple key for standard MIDI
                    note.frequency = note.baseFreq;
                }

                activeNotes[key] = note;
            }
            else
            {
                // Note on with velocity 0 = note off
                if (channel >= 2 && channel <= 15)
                    activeNotes.erase((channel << 8) | noteNumber);
                else
                    activeNotes.erase(noteNumber);
            }
        }
        else if (msg.isNoteOff())
        {
            int noteNumber = msg.getNoteNumber();

            if (channel >= 2 && channel <= 15)
                activeNotes.erase((channel << 8) | noteNumber);
            else
                activeNotes.erase(noteNumber);
        }
        else if (msg.isPitchWheel())
        {
            int bendValue = msg.getPitchWheelValue();  // 0-16383, center = 8192
            channelPitchBend[channel - 1] = bendValue;

            // Update frequency for any active MPE note on this channel
            for (auto& [key, note] : activeNotes)
            {
                if (note.isMPE && note.channel == channel)
                {
                    float bend = static_cast<float>(bendValue - 8192) / 8192.0f;
                    float bendCents = bend * mpeBendRangeSemitones * 100.0f;
                    note.frequency = note.baseFreq * std::pow(2.0f, bendCents / 1200.0f);
                }
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            activeNotes.clear();
            std::fill(channelPitchBend.begin(), channelPitchBend.end(), 8192);
        }
    }
}

//==============================================================================
std::vector<float> MidiManager::getActiveFrequencies() const
{
    std::vector<float> freqs;
    freqs.reserve(activeNotes.size());

    for (const auto& [key, note] : activeNotes)
        freqs.push_back(note.frequency);

    // Sort ascending
    std::sort(freqs.begin(), freqs.end());
    return freqs;
}

//==============================================================================
std::vector<std::pair<int, float>> MidiManager::getActiveNoteFrequencies() const
{
    std::vector<std::pair<int, float>> notes;
    notes.reserve(activeNotes.size());

    for (const auto& [key, note] : activeNotes)
        notes.emplace_back(note.noteNumber, note.frequency);

    // Ascending by note number, so voice ordering in multiply mode is stable.
    std::sort(notes.begin(), notes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return notes;
}

//==============================================================================
void MidiManager::reset()
{
    activeNotes.clear();
    std::fill(channelPitchBend.begin(), channelPitchBend.end(), 8192);
}

//==============================================================================
float MidiManager::noteToFrequency(int noteNumber, float baseTune)
{
    return baseTune * std::pow(2.0f, (noteNumber - 69) / 12.0f);
}
