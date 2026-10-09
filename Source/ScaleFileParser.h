/*
  ==============================================================================
    super-autotune — Scale File Parser (.scl and .tun)
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

//==============================================================================
struct TuningData
{
    juce::String description;
    // For .scl: the scale's note frequencies in Hz (one or more octaves).
    std::vector<float> frequencies;  // always stored as Hz
    // For .tun: 128 entries, frequency in Hz for each MIDI note (0.0 = unmapped).
    std::vector<float> midiFreqs;
    // For .tun: the cents each note was defined at, 128 entries. This is the value
    // as written in the file's [Tuning] section, which is what a microtonal user
    // recognises (a 24-TET table reads 0, 50, 100, ...).
    std::vector<float> midiCents;
    float baseFrequency = 261.6256f; // middle C (C4)

    // Frequency that 0 cents refers to: MIDI note 0 in 12-TET at A440.
    static constexpr double note0Hz = 8.1757989156437073336;
};

//==============================================================================
class ScaleFileParser
{
public:
    ScaleFileParser() = default;

    // Parse a .scl file. Returns true on success.
    // The content should be the full text of the file.
    static bool parseScl(const juce::String& content, TuningData& outData);

    // Parse a .tun file. Returns true on success.
    static bool parseTun(const juce::String& content, TuningData& outData);

    // Convenience: auto-detect format from file extension and parse.
    static bool parseFile(const juce::File& file, TuningData& outData);

    // Convert cents to frequency ratio.
    static float centsToRatio(float cents);

    // Convert a frequency ratio (num/den) to a frequency at the given base.
    static float ratioToFrequency(float numerator, float denominator, float baseHz);

    // Get the default 12-TET scale.
    static TuningData getDefault12TET();

private:
    // Parse a single scalar pitch value (cents or ratio) into a frequency.
    static float parsePitchValue(const juce::String& line, float baseHz, bool& ok);

    // Get MIDI note name from note number (e.g., 69 -> "A4").
    static juce::String midiNoteToName(int noteNumber);

    // Get MIDI note frequency (12-TET).
    static float midiNoteToFrequency(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaleFileParser)
};
