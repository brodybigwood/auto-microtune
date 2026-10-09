/*
  ==============================================================================
    super-autotune — Tuning Manager implementation
  ==============================================================================
*/

#include "TuningManager.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Where the 1/1 of a loaded scale lands. MIDI 60 is the conventional root for
    // scale files that do not carry their own note mapping.
    constexpr int kScaleRootNote = 60;
}

//==============================================================================
TuningManager::TuningManager()
{
    rebuild();
}

//==============================================================================
bool TuningManager::loadScaleFile (const juce::File& file)
{
    TuningData data;
    if (!ScaleFileParser::parseFile (file, data))
        return false;

    scaleDescription = data.description;
    std::fill (noteFreqs.begin(), noteFreqs.end(), 0.0f);
    std::fill (noteCents.begin(), noteCents.end(), 0.0f);

    const bool isTun = file.getFileExtension().equalsIgnoreCase (".tun");

    if (isTun)
    {
        // .tun already carries an explicit MIDI note -> frequency table, plus the
        // cents each note was written at.
        const int n = std::min (128, static_cast<int> (data.midiFreqs.size()));
        for (int i = 0; i < n; ++i)
        {
            noteFreqs[static_cast<size_t> (i)] = data.midiFreqs[static_cast<size_t> (i)];
            if (i < static_cast<int> (data.midiCents.size()))
                noteCents[static_cast<size_t> (i)] = data.midiCents[static_cast<size_t> (i)];
        }
    }
    else if (! data.frequencies.empty())
    {
        // .scl is a periodic scale: `steps` pitches covering one period. Map step i
        // to MIDI note kScaleRootNote + i and tile that upward and downward by
        // octaves, rather than rounding each step to the nearest 12-TET note —
        // rounding collapses scales that do not have 12 steps per period (a 24-EDO
        // file has two pitches per semitone and the old code folded them together).
        const int steps = static_cast<int> (data.frequencies.size());

        // A .scl period is not always an octave — the last step is the interval the
        // scale repeats at, which is 2/1 for most scales but not all (Bohlen-Pierce
        // repeats at 3/1). Tile by that interval rather than assuming 2/1.
        const double periodRatio = (data.frequencies.size() > 1 && data.frequencies[0] > 0.0f)
                                 ? static_cast<double> (data.frequencies.back() / data.frequencies.front())
                                 : 2.0;
        const double safePeriod = (periodRatio > 1.0) ? periodRatio : 2.0;

        for (int n = 0; n < 128; ++n)
        {
            int idx = (n - kScaleRootNote) % steps;
            if (idx < 0) idx += steps;

            const int oct = (n - kScaleRootNote - idx) / steps;
            noteFreqs[static_cast<size_t> (n)] =
                static_cast<float> (data.frequencies[static_cast<size_t> (idx)]
                                    * std::pow (safePeriod, static_cast<double> (oct)));
        }

        // Cents above the scale's 1/1, so the list shows the note's position in the
        // scale rather than a number that depends on which file format was loaded.
        const float rootFreq = data.frequencies[0];
        if (rootFreq > 0.0f)
            for (int n = 0; n < 128; ++n)
                if (noteFreqs[static_cast<size_t> (n)] > 0.0f)
                    noteCents[static_cast<size_t> (n)] =
                        1200.0f * std::log2 (noteFreqs[static_cast<size_t> (n)] / rootFreq);
    }
    else
    {
        return false;
    }

    tuningFileLoaded = true;
    rebuild();
    return true;
}

void TuningManager::clearScaleFile()
{
    std::fill (noteFreqs.begin(), noteFreqs.end(), 0.0f);
    tuningFileLoaded = false;
    scaleDescription.clear();
    rebuild();
}

//==============================================================================
void TuningManager::toggleNote (int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    enabledNotes[static_cast<size_t> (midiNote)] = ! enabledNotes[static_cast<size_t> (midiNote)];
    rebuild();
}

void TuningManager::setNoteEnabled (int midiNote, bool enabled)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    enabledNotes[static_cast<size_t> (midiNote)] = enabled;
    rebuild();
}

bool TuningManager::isNoteEnabled (int midiNote) const
{
    return midiNote >= 0 && midiNote < 128 && enabledNotes[static_cast<size_t> (midiNote)];
}

float TuningManager::getNoteCents (int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return 0.0f;

    // No file loaded: the plain 12-TET value, which is what those notes are.
    if (! tuningFileLoaded)
        return static_cast<float> (midiNote) * 100.0f;

    return noteCents[static_cast<size_t> (midiNote)];
}

float TuningManager::getNoteFrequency (int midiNote) const
{
    if (midiNote < 0 || midiNote > 127)
        return 0.0f;

    const float f = noteFreqs[static_cast<size_t> (midiNote)];
    if (f > 0.0f)
        return f;

    return 440.0f * std::pow (2.0f, static_cast<float> (midiNote - 69) / 12.0f);
}

//==============================================================================
void TuningManager::setOctaveExtend (bool extend)
{
    if (extend == octaveExtend)
        return;
    octaveExtend = extend;
    rebuild();
}

void TuningManager::setMidiEnabled (bool enabled)
{
    if (enabled == midiEnabled)
        return;
    midiEnabled = enabled;
    rebuild();
}

void TuningManager::setMidiNotes (const std::vector<std::pair<int, float>>& notes)
{
    // Called every block. Bail out on an unchanged set so the common case does not
    // copy a vector and rebuild the target list inside the audio callback.
    if (notes == midiNotes)
        return;

    midiNotes = notes;
    if (midiEnabled)
        rebuild();
}

void TuningManager::clearMidiNotes()
{
    midiNotes.clear();
    if (midiEnabled)
        rebuild();
}

//==============================================================================
void TuningManager::rebuild()
{
    // Display list only. findTarget() reads the selection directly, so there is no
    // precomputed target table to keep in sync with it — and nothing to go stale
    // or disagree.
    activeFreqs.clear();

    if (midiEnabled)
    {
        for (const auto& [note, freq] : midiNotes)
            if (note >= 0 && note < 128 && freq > 0.0f)
                activeFreqs.push_back (freq);
    }
    else
    {
        for (int n = 0; n < 128; ++n)
            if (enabledNotes[static_cast<size_t> (n)])
                activeFreqs.push_back (getNoteFrequency (n));
    }
}

//==============================================================================
TuningTarget TuningManager::findTarget (float inputHz) const
{
    TuningTarget out;

    if (inputHz < 1.0f)
        return out;

    // Snap to the nearest allowed note, taking each selected note at the octave
    // nearest the input. Nothing is precomputed and nothing is replicated: the
    // selected notes ARE the pitch classes, and the octave is whatever is closest
    // to the input right now.
    //
    // Distance is measured in cents (log domain). Measuring in Hz would put the
    // boundary between two notes at their arithmetic mean, which is off by several
    // cents for wide intervals.
    double best = 1.0e30;

    auto consider = [&] (int /*note*/, float baseFreq)
    {
        if (baseFreq <= 0.0f)
            return;

        // Whole octaves between the input and this note. With octave extend off the
        // note is only allowed at its own pitch, so the shift is forced to zero.
        const double octaves = octaveExtend
                             ? std::round (std::log2 (static_cast<double> (inputHz) / baseFreq))
                             : 0.0;
        const double f = baseFreq * std::pow (2.0, octaves);

        const double dist = std::abs (std::log2 (static_cast<double> (inputHz) / f));
        if (dist < best)
        {
            best       = dist;
            out.freqHz = static_cast<float> (f);
            out.valid  = true;
        }
    };

    if (midiEnabled)
    {
        // Armed: held MIDI notes are the sole source.
        for (const auto& [note, freq] : midiNotes)
            consider (note, freq);
    }
    else
    {
        for (int n = 0; n < 128; ++n)
            if (enabledNotes[static_cast<size_t> (n)])
                consider (n, getNoteFrequency (n));
    }

    if (! out.valid)
        return out;

    // Which row to highlight: whichever note's pitch is nearest the target.
    //
    // Reporting it as `note + 12 * octaves` assumed 12 notes to the octave, which is
    // wrong for any other tuning. In a 22-TET file an octave is 22 notes, so note 40
    // taken three octaves up is note 106 — but that arithmetic named note 76, a
    // different pitch entirely, while the audio went to the right place. Asking the
    // mapping which note sits at the target frequency needs no such assumption and
    // is correct for any scale.
    double bestNoteDist = 1.0e30;
    for (int m = 0; m < 128; ++m)
    {
        const double fm = getNoteFrequency (m);
        if (fm <= 0.0f)
            continue;

        const double d = std::abs (std::log2 (static_cast<double> (out.freqHz) / fm));
        if (d < bestNoteDist)
        {
            bestNoteDist = d;
            out.midiNote = m;
        }
    }

    return out;
}

//==============================================================================
bool TuningManager::getVoiceSource (int voiceIndex, int& midiNote, float& freqHz) const
{
    if (voiceIndex < 0)
        return false;

    if (midiEnabled)
    {
        if (voiceIndex >= static_cast<int> (midiNotes.size()))
            return false;
        midiNote = midiNotes[static_cast<size_t> (voiceIndex)].first;
        freqHz   = midiNotes[static_cast<size_t> (voiceIndex)].second;
        return freqHz > 0.0f;
    }

    int seen = 0;
    for (int n = 0; n < 128; ++n)
    {
        if (! enabledNotes[static_cast<size_t> (n)])
            continue;
        if (seen++ == voiceIndex)
        {
            midiNote = n;
            freqHz   = getNoteFrequency (n);
            return freqHz > 0.0f;
        }
    }
    return false;
}

int TuningManager::getVoiceCount() const
{
    if (midiEnabled)
    {
        int c = 0;
        for (const auto& m : midiNotes)
            if (m.second > 0.0f) ++c;
        return c;
    }

    int c = 0;
    for (bool b : enabledNotes)
        if (b) ++c;
    return c;
}

float TuningManager::getMultiplyNote (float inputHz, int voiceIndex) const
{
    int   note = -1;
    float freq = 0.0f;

    if (! getVoiceSource (voiceIndex, note, freq) || inputHz < 1.0f)
        return inputHz;

    // Shift the voice by whole octaves so it sits near the input rather than a
    // fixed distance away, then let the caller shift it to the target.
    const double octaves = std::round (std::log2 (static_cast<double> (inputHz) / freq));
    return static_cast<float> (freq * std::pow (2.0, octaves));
}

//==============================================================================
juce::String TuningManager::getNoteMask() const
{
    juce::String mask;
    mask.preallocateBytes (128);
    for (bool b : enabledNotes)
        mask += (b ? '1' : '0');
    return mask;
}

void TuningManager::setNoteMask (const juce::String& mask)
{
    for (int i = 0; i < 128 && i < mask.length(); ++i)
        enabledNotes[static_cast<size_t> (i)] = (mask[i] == '1');
    rebuild();
}

//==============================================================================
juce::String TuningManager::getTuningTable() const
{
    // Two comma-separated lists, frequencies then cents. Cents are carried too
    // because the note list displays them and they are not recoverable from the
    // frequencies for a tuning whose root is not note 0.
    juce::String freqs, cents;

    for (int i = 0; i < 128; ++i)
    {
        if (i > 0) { freqs << ','; cents << ','; }
        freqs << juce::String (noteFreqs[static_cast<size_t> (i)], 6);
        cents << juce::String (noteCents[static_cast<size_t> (i)], 4);
    }

    return freqs + ';' + cents;
}

void TuningManager::setTuningTable (const juce::String& table)
{
    auto sections = juce::StringArray::fromTokens (table, ";", "");
    if (sections.size() < 2)
        return;

    auto freqs = juce::StringArray::fromTokens (sections[0], ",", "");
    auto cents = juce::StringArray::fromTokens (sections[1], ",", "");

    for (int i = 0; i < 128; ++i)
    {
        noteFreqs[static_cast<size_t> (i)] = (i < freqs.size()) ? freqs[i].getFloatValue() : 0.0f;
        noteCents[static_cast<size_t> (i)] = (i < cents.size()) ? cents[i].getFloatValue() : 0.0f;
    }

    // A mapping is loaded, whether or not it came from a file.
    tuningFileLoaded = true;
    rebuild();
}

//==============================================================================
juce::StringArray TuningManager::getActiveNoteNames() const
{
    juce::StringArray names;
    for (float f : activeFreqs)
        if (f >= 130.0f && f <= 1000.0f)
            names.add (freqToNoteName (f));

    names.removeDuplicates (false);
    return names;
}

juce::String TuningManager::freqToNoteName (float freqHz)
{
    if (freqHz < 1.0f)
        return "---";

    const float noteFloat = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
    const int   noteNum   = static_cast<int> (std::round (noteFloat));

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (noteNum / 12) - 1;
    int pc     = noteNum % 12;
    if (pc < 0) pc += 12;

    return juce::String (names[pc]) + juce::String (octave);
}
