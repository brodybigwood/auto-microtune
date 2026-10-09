/*
  ==============================================================================
    super-autotune — Scale File Parser implementation
  ==============================================================================
*/

#include "ScaleFileParser.h"

//==============================================================================
bool ScaleFileParser::parseScl(const juce::String& content, TuningData& outData)
{
    // Split into lines, handle CR/LF
    juce::StringArray lines;
    lines.addLines(content);

    // Filter out comment lines (starting with !) and empty lines
    juce::StringArray nonCommentLines;
    for (auto& line : lines)
    {
        auto trimmed = line.trimStart();
        if (trimmed.isEmpty() || trimmed.startsWith("!"))
            continue;
        nonCommentLines.add(trimmed);
    }

    if (nonCommentLines.size() < 2)
        return false;

    // First non-comment line: description
    outData.description = nonCommentLines[0];

    // Second non-comment line: number of notes
    int noteCount = nonCommentLines[1].getIntValue();
    if (noteCount <= 0 || noteCount > 256)
        return false;

    if (nonCommentLines.size() < 2 + noteCount)
        return false;

    // Parse pitch values. The .scl file lists `noteCount` notes (the first is the
    // 1/1 unison), so read exactly that many — no implicit extra.
    outData.frequencies.clear();
    for (int i = 0; i < noteCount; ++i)
    {
        bool ok = true;
        float freq = parsePitchValue(nonCommentLines[2 + i], outData.baseFrequency, ok);
        if (!ok)
            return false;
        outData.frequencies.push_back(freq);
    }

    // Sort frequencies
    std::sort(outData.frequencies.begin(), outData.frequencies.end());

    return true;
}

//==============================================================================
bool ScaleFileParser::parseTun(const juce::String& content, TuningData& outData)
{
    // .tun (Anamark) format: INI-like, comment lines start with ';'.
    //
    // The [Tuning] section holds ABSOLUTE CENTS, not Hz. A 12-TET table is
    // "note 0 = 0, note 1 = 100, ... note 69 = 6900", and a 24-TET table steps by
    // 50 instead of 100. Reading those as Hz is what made a 24-TET file snap to
    // nonsense: every note came out as its own cent offset.
    //
    // The cents are measured from the frequency of note 0, which defaults to MIDI
    // note 0 in 12-TET at A440 — 8.1757989156437073336 Hz. Sanity check: 12-TET
    // note 69 is 6900 cents, and 8.1757989156 * 2^(6900/1200) = 440.000 Hz.
    std::vector<float> noteFreqs(128, 0.0f);
    std::vector<float> noteCents(128, 0.0f);

    juce::StringArray lines;
    lines.addLines(content);

    bool inTuning = false;
    for (auto& line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isEmpty() || trimmed.startsWith(";") || trimmed.startsWith("!"))
            continue;

        if (trimmed.startsWith("["))
        {
            inTuning = trimmed.startsWithIgnoreCase("[Tuning]");
            continue;
        }

        if (!inTuning)
            continue;

        // Split on '=' by hand. The previous version tokenised on " =" and then
        // indexed [1] and [2], which depends on whether the tokeniser keeps empty
        // fields — a silent way to read the wrong column.
        const int eq = trimmed.indexOfChar('=');
        if (eq < 0)
            continue;

        auto key    = trimmed.substring(0, eq).trim();
        auto valStr = trimmed.substring(eq + 1).trim();

        auto keyParts = juce::StringArray::fromTokens(key, " \t", "");
        if (keyParts.size() < 2 || !keyParts[0].equalsIgnoreCase("note"))
            continue;

        const int    noteNum = keyParts[1].getIntValue();
        const double cents   = valStr.getDoubleValue();

        if (noteNum < 0 || noteNum > 127)
            continue;

        noteFreqs[static_cast<size_t>(noteNum)] =
            static_cast<float>(TuningData::note0Hz * std::pow(2.0, cents / 1200.0));
        noteCents[static_cast<size_t>(noteNum)] = static_cast<float>(cents);
    }

    // Keep the MIDI-note -> frequency mapping (index = MIDI note). Do NOT sort or
    // round-trip through 12-TET, which would collapse microtonal notes — a 24-TET
    // tuning has two notes per 12-TET semitone.
    outData.midiFreqs = std::move(noteFreqs);
    outData.midiCents = std::move(noteCents);
    outData.description = ".tun tuning";
    return true;
}

//==============================================================================
bool ScaleFileParser::parseFile(const juce::File& file, TuningData& outData)
{
    if (!file.existsAsFile())
        return false;

    juce::String content = file.loadFileAsString();
    if (content.isEmpty())
        return false;

    auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".scl")
        return parseScl(content, outData);
    else if (ext == ".tun")
        return parseTun(content, outData);

    return false;
}

//==============================================================================
float ScaleFileParser::centsToRatio(float cents)
{
    return std::pow(2.0f, cents / 1200.0f);
}

float ScaleFileParser::ratioToFrequency(float numerator, float denominator, float baseHz)
{
    if (denominator == 0.0f)
        return baseHz;
    return baseHz * numerator / denominator;
}

float ScaleFileParser::midiNoteToFrequency(int note)
{
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

//==============================================================================
float ScaleFileParser::parsePitchValue(const juce::String& line, float baseHz, bool& ok)
{
    ok = true;
    auto val = line.upToFirstOccurrenceOf(" ", false, false).trim();  // strip trailing text

    if (val.contains("."))
    {
        // Cents value
        float cents = val.getFloatValue();
        return baseHz * centsToRatio(cents);
    }
    else if (val.contains("/"))
    {
        // Ratio
        auto parts = juce::StringArray::fromTokens(val, "/", "");
        if (parts.size() >= 2)
        {
            float num = parts[0].getFloatValue();
            float den = parts[1].getFloatValue();
            if (num < 0.0f)
            {
                ok = false;
                return baseHz;
            }
            return ratioToFrequency(num, den, baseHz);
        }
    }
    else
    {
        // Integer ratio (e.g., "2" means "2/1")
        float num = val.getFloatValue();
        if (num < 0.0f)
        {
            ok = false;
            return baseHz;
        }
        return ratioToFrequency(num, 1.0f, baseHz);
    }

    ok = false;
    return baseHz;
}

//==============================================================================
TuningData ScaleFileParser::getDefault12TET()
{
    TuningData data;
    data.description = "12-Tone Equal Temperament";
    data.baseFrequency = 261.6256f;  // C4

    for (int i = 0; i <= 12; ++i)
    {
        data.frequencies.push_back(data.baseFrequency * centsToRatio(i * 100.0f));
    }

    return data;
}
