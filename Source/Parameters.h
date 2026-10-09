/*
  ==============================================================================
    super-autotune — Parameter definitions
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace ParamIDs
{
    inline constexpr auto fftSize       = "fftSize";
    inline constexpr auto hopPercent    = "hopPercent";
    inline constexpr auto retuneSpeed   = "retuneSpeed";
    inline constexpr auto tuningMode    = "tuningMode";
    inline constexpr auto midiEnabled   = "midiEnabled";
    inline constexpr auto octaveExtend  = "octaveExtend";
    inline constexpr auto wetDry        = "wetDry";
    inline constexpr auto bypass        = "bypass";
}

namespace TuningMode
{
    inline const juce::StringArray choices { "Snap", "Multiply" };
}

namespace FFTSizes
{
    inline const juce::StringArray choices { "256", "512", "1024", "2048", "4096" };
    inline constexpr int values[] = { 256, 512, 1024, 2048, 4096 };

    inline int getValue(int index)
    {
        jassert(index >= 0 && index < 5);
        return values[index];
    }
}

namespace HopPercent
{
    // Only 50% and 75% overlap are offered: a Hann/Hann² window satisfies
    // constant overlap-add only for hops of the form N/(2m). The old "25%"
    // option (hop = 3*fftSize/4) was not COLA and caused amplitude ripple /
    // tremolo at the hop rate.
    inline const juce::StringArray choices { "50%", "75%" };

    inline int getHopSize(int fftSize, int index)
    {
        // Clamp any stale index (e.g. a host recalling the old "25%" position)
        // to the last valid option.
        if (index < 0) index = 0;
        if (index >= choices.size()) index = choices.size() - 1;
        if (index == 0) return fftSize / 2;   // 50% overlap
        return fftSize / 4;                     // 75% overlap (default)
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
