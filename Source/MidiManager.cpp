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

                // Apply this channel's current bend on EVERY channel, not just the
                // MPE ones. An MPE host assigns notes to member channels 1..15 and
                // sends the bend on the note's own channel before the note-on, so
                // treating channel 1 as plain MIDI dropped the fractional tuning of
                // whichever note it was given.
                //
                // For standard MIDI this is also right: a channel-wide bend applies
                // to every note on that channel.
                const float bend = static_cast<float>(channelPitchBend[channel - 1] - 8192) / 8192.0f;
                const float bendCents = bend * mpeBendRangeSemitones * 100.0f;
                note.frequency = note.baseFreq * std::pow(2.0f, bendCents / 1200.0f);

                // Channels 2-15 are per-note in the MPE lower zone, so two notes on
                // the same number but different channels are separate voices.
                note.isMPE = (channel >= 2 && channel <= 15);
                key = note.isMPE ? ((channel << 8) | noteNumber) : noteNumber;

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
                // Any note on this channel follows its bend — see the note-on path.
                if (note.channel == channel)
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
