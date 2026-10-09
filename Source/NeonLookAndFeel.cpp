/*
  ==============================================================================
    super-autotune — Neon LookAndFeel implementation
  ==============================================================================
*/

#include "NeonLookAndFeel.h"

const juce::Colour NeonLookAndFeel::bgDark        = juce::Colour(0xff0a0a14);
const juce::Colour NeonLookAndFeel::bgSurface     = juce::Colour(0xff12122a);
const juce::Colour NeonLookAndFeel::accentCyan    = juce::Colour(0xff00ffcc);
const juce::Colour NeonLookAndFeel::accentMagenta = juce::Colour(0xffff44cc);
const juce::Colour NeonLookAndFeel::textSoft      = juce::Colour(0xffe0e0f0);
const juce::Colour NeonLookAndFeel::textDim       = juce::Colour(0xff8888aa);

NeonLookAndFeel::NeonLookAndFeel()
{
    // Override the default colour scheme
    setColour(juce::ResizableWindow::backgroundColourId, bgDark);
    setColour(juce::Slider::backgroundColourId, bgSurface);
    setColour(juce::Slider::thumbColourId, accentCyan);
    setColour(juce::Slider::trackColourId, accentCyan.withAlpha(0.3f));
    setColour(juce::Slider::textBoxTextColourId, textSoft);
    setColour(juce::Slider::textBoxBackgroundColourId, bgDark);
    setColour(juce::Slider::textBoxOutlineColourId, accentCyan.withAlpha(0.3f));

    setColour(juce::ToggleButton::textColourId, textSoft);
    setColour(juce::ToggleButton::tickColourId, accentCyan);
    setColour(juce::ToggleButton::tickDisabledColourId, textDim);

    setColour(juce::ComboBox::backgroundColourId, bgSurface);
    setColour(juce::ComboBox::textColourId, textSoft);
    setColour(juce::ComboBox::outlineColourId, accentCyan.withAlpha(0.3f));
    setColour(juce::ComboBox::buttonColourId, accentCyan);
    setColour(juce::ComboBox::arrowColourId, accentCyan);

    setColour(juce::TextButton::buttonColourId, bgSurface);
    setColour(juce::TextButton::buttonOnColourId, accentCyan.withAlpha(0.3f));
    setColour(juce::TextButton::textColourOffId, accentCyan);
    setColour(juce::TextButton::textColourOnId, accentCyan);

    setColour(juce::PopupMenu::backgroundColourId, bgSurface);
    setColour(juce::PopupMenu::textColourId, textSoft);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentCyan.withAlpha(0.2f));
    setColour(juce::PopupMenu::highlightedTextColourId, accentCyan);

    setColour(juce::Label::textColourId, textSoft);

    setColour(juce::TextEditor::backgroundColourId, bgDark);
    setColour(juce::TextEditor::textColourId, textSoft);
    setColour(juce::TextEditor::outlineColourId, accentCyan.withAlpha(0.3f));
    setColour(juce::TextEditor::focusedOutlineColourId, accentCyan);
}

//==============================================================================
void NeonLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        const juce::Slider::SliderStyle style,
                                        juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        auto trackY = y + h / 2 - 2;
        // Track background
        g.setColour(bgSurface);
        g.fillRoundedRectangle(float(x), float(trackY), float(w), 4.0f, 2.0f);

        // Filled track (up to thumb position)
        auto thumbWidth = 16.0f;
        float fillEnd = sliderPos - thumbWidth / 2.0f;
        g.setColour(accentCyan.withAlpha(0.5f));
        g.fillRoundedRectangle(float(x), float(trackY), fillEnd - float(x), 4.0f, 2.0f);

        // Thumb
        g.setColour(accentCyan);
        g.fillEllipse(sliderPos - thumbWidth / 2.0f, float(trackY - 4), thumbWidth, 12.0f);

        // Glow
        g.setColour(accentCyan.withAlpha(0.2f));
        g.fillEllipse(sliderPos - thumbWidth / 2.0f - 3.0f, float(trackY - 7), thumbWidth + 6.0f, 18.0f);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}

void NeonLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    auto radius = juce::jmin(w / 2, h / 2) - 8.0f;
    auto centreX = float(x) + float(w) * 0.5f;
    auto centreY = float(y) + float(h) * 0.5f;
    auto rw = radius * 2.0f;
    auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Glow ring
    juce::Path glowPath;
    glowPath.addCentredArc(centreX, centreY, radius + 3.0f, radius + 3.0f, 0.0f, 0.0f,
                           juce::MathConstants<float>::twoPi);
    g.setColour(accentCyan.withAlpha(0.08f));
    g.strokePath(glowPath, juce::PathStrokeType(6.0f));

    // Background arc
    juce::Path bgArc;
    bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, startAngle, endAngle);
    g.setColour(bgSurface);
    g.strokePath(bgArc, juce::PathStrokeType(4.0f));

    // Filled arc
    juce::Path filledArc;
    filledArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, startAngle, angle);
    g.setColour(accentCyan);
    g.strokePath(filledArc, juce::PathStrokeType(4.0f));

    // Thumb line
    juce::Path thumb;
    float thumbRadius = radius - 8.0f;
    thumb.addLineSegment(juce::Line<float>(
        centreX + thumbRadius * std::cos(angle),
        centreY + thumbRadius * std::sin(angle),
        centreX + radius * std::cos(angle),
        centreY + radius * std::sin(angle)), 2.0f);
    g.setColour(accentMagenta);
    g.strokePath(thumb, juce::PathStrokeType(3.0f));
}

juce::Slider::SliderLayout NeonLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    auto layout = LookAndFeel_V4::getSliderLayout(slider);
    // Slightly smaller text box
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal)
    {
        layout.textBoxBounds.setHeight(18);
    }
    return layout;
}

//==============================================================================
void NeonLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    auto tickBox = bounds.removeFromLeft(bounds.getHeight());

    // Checkbox
    g.setColour(button.getToggleState() ? accentCyan.withAlpha(0.2f) : bgSurface);
    g.fillRoundedRectangle(tickBox.reduced(2.0f), 4.0f);

    g.setColour(button.getToggleState() ? accentCyan : textDim);
    g.drawRoundedRectangle(tickBox.reduced(2.0f), 4.0f, 1.5f);

    if (button.getToggleState())
    {
        // Draw check
        juce::Path check;
        auto cx = tickBox.getCentreX();
        auto cy = tickBox.getCentreY();
        check.addLineSegment(juce::Line<float>(cx - 3, cy, cx - 1, cy + 2.5f), 1.5f);
        check.addLineSegment(juce::Line<float>(cx - 1, cy + 2.5f, cx + 4, cy - 3), 1.5f);
        g.setColour(accentCyan);
        g.strokePath(check, juce::PathStrokeType(1.5f));
    }

    // Text
    g.setColour(textSoft);
    g.setFont(14.0f);
    g.drawText(button.getButtonText(), bounds, juce::Justification::centredLeft);
}

//==============================================================================
void NeonLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool isButtonDown,
                                    int buttonX, int buttonY, int buttonW, int buttonH,
                                    juce::ComboBox& box)
{
    g.setColour(bgSurface);
    g.fillRoundedRectangle(float(0), float(0), float(w), float(h), 4.0f);

    g.setColour(accentCyan.withAlpha(0.3f));
    g.drawRoundedRectangle(float(0.5f), float(0.5f), float(w - 1), float(h - 1), 4.0f, 1.0f);

    // Arrow
    juce::Path arrow;
    auto arrowX = float(buttonX + buttonW / 2 - 5);
    auto arrowY = float(buttonY + buttonH / 2 - 3);
    arrow.addTriangle(arrowX, arrowY,
                      arrowX + 10.0f, arrowY,
                      arrowX + 5.0f, arrowY + 6.0f);
    g.setColour(accentCyan);
    g.fillPath(arrow);
}

void NeonLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 1, box.getWidth() - 32, box.getHeight() - 2);
    label.setFont(juce::FontOptions(14.0f));
}

//==============================================================================
void NeonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& /*backgroundColour*/,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    auto baseColour = bgSurface;
    if (shouldDrawButtonAsDown)
        baseColour = accentCyan.withAlpha(0.3f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = accentCyan.withAlpha(0.15f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(accentCyan.withAlpha(0.4f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

//==============================================================================
juce::Font NeonLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::FontOptions(13.0f);
}
