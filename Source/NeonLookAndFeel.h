/*
  ==============================================================================
    super-autotune — Neon Cyberpunk LookAndFeel
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();

    // Colours
    static const juce::Colour bgDark;          // #0a0a14
    static const juce::Colour bgSurface;       // #12122a
    static const juce::Colour accentCyan;      // #00ffcc
    static const juce::Colour accentMagenta;   // #ff44cc
    static const juce::Colour textSoft;        // #e0e0f0
    static const juce::Colour textDim;         // #8888aa

    // Slider
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override;

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;

    // Toggle Button
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // Combo Box
    void drawComboBox(juce::Graphics& g, int w, int h, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    // Text Button
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    // Labels
    juce::Font getLabelFont(juce::Label& label) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonLookAndFeel)
};
