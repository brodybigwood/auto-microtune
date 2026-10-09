cmake_minimum_required(VERSION 3.22)

project(super-autotune VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point CMake at the JUCE source tree (overridable with -DJUCE_DIR=...).
set(JUCE_DIR "" CACHE PATH "Path to the JUCE SDK")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(super-autotune
    VERSION 1.0.0
    COMPANY_NAME "yourcompany"
    NEEDS_MIDI_INPUT TRUE
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Kmzo
    FORMATS VST3 Standalone
    PRODUCT_NAME "super-autotune")

# Generate a JuceHeader.h (my sources include <JuceHeader.h>).
juce_generate_juce_header(super-autotune)

target_sources(super-autotune
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/PitchDetector.cpp
        Source/PitchShifter.cpp
        Source/TuningManager.cpp
        Source/ScaleFileParser.cpp
        Source/MidiManager.cpp
        Source/SpectrumAnalyzer.cpp
        Source/NeonLookAndFeel.cpp
        Source/EditorComponents.cpp)

# My sources include sibling headers via relative quotes (e.g. "PitchDetector.h").
target_include_directories(super-autotune PRIVATE Source)

target_compile_definitions(super-autotune
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(super-autotune
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ============================================================================
# Offline DSP self-test harness (development only, not shipped as a plugin).
# Feeds sines and harmonic-rich material through PitchDetector + PitchShifter and
# asserts tuning accuracy in cents, latency, transparency and click-freeness.
# Build with:  cmake --build <build> --target super-autotune_dsptest
# ============================================================================
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Source/DspTest.cpp)
    juce_add_console_app(super-autotune_dsptest
        PRODUCT_NAME "super-autotune-dsptest")

    target_sources(super-autotune_dsptest
        PRIVATE
            Source/DspTest.cpp
            Source/PitchDetector.cpp
            Source/PitchShifter.cpp
            Source/ScaleFileParser.cpp)

    # The harness sources include <JuceHeader.h>.
    juce_generate_juce_header(super-autotune_dsptest)

    target_include_directories(super-autotune_dsptest PRIVATE Source)

    target_compile_definitions(super-autotune_dsptest
        PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    target_link_libraries(super-autotune_dsptest
        PRIVATE
            juce::juce_dsp
            juce::juce_core
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
