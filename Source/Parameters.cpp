/*
  ==============================================================================
    super-autotune — Parameter layout factory
  ==============================================================================
*/

#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    auto layout = AudioProcessorValueTreeState::ParameterLayout();

    // FFT Size
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID(ParamIDs::fftSize, 1),
        "FFT Size",
        FFTSizes::choices,
        2  // default: 1024
    ));

    // Overlap / Hop Percent
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID(ParamIDs::hopPercent, 1),
        "Overlap",
        HopPercent::choices,
        1  // default: 75%
    ));

    // Retune Speed
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID(ParamIDs::retuneSpeed, 1),
        "Retune Speed",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f
    ));

    // Tuning Mode: Snap vs Multiply
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID(ParamIDs::tuningMode, 1),
        "Tuning Mode",
        TuningMode::choices,
        0  // default: Snap
    ));

    // MIDI on/off — whether incoming MIDI notes drive the tuning target.
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID(ParamIDs::midiEnabled, 1),
        "MIDI Control",
        false
    ));

    // Octave Extend
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID(ParamIDs::octaveExtend, 1),
        "Extend Octaves",
        true
    ));

    // Wet / Dry
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID(ParamIDs::wetDry, 1),
        "Wet/Dry",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f
    ));

    // Bypass
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID(ParamIDs::bypass, 1),
        "Bypass",
        false
    ));

    return layout;
}
