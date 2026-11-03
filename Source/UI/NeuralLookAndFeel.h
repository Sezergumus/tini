#pragma once

#include "Theme.h"

class NeuralLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuralLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
        setColour (juce::Slider::thumbColourId,            Theme::textHi);
        setColour (juce::Label::textColourId,              Theme::textHi);
        setColour (juce::TextEditor::backgroundColourId,   Theme::inset);
        setColour (juce::TextEditor::textColourId,         Theme::textHi);
        setColour (juce::TextEditor::highlightColourId,    Theme::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId,      Theme::accent.withAlpha (0.6f));
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (juce::CaretComponent::caretColourId,    Theme::accent);

        setColour (juce::PopupMenu::backgroundColourId,            Theme::panelTop);
        setColour (juce::PopupMenu::textColourId,                  Theme::textHi);
        setColour (juce::PopupMenu::headerTextColourId,            Theme::textLo);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId,       Theme::textHi);
    }

    //==========================================================================
    // Call-out bubbles (the add-pedal popover): graphite card, thin border, soft shadow, arrow.
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 20, { 0, 6 }).drawForPath (g, path);

        const auto bounds = path.getBounds();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff20252b), 0.0f, bounds.getY(),
                                                 juce::Colour (0xff15181c), 0.0f, bounds.getBottom(), false));
        g.fillPath (path);

        g.setColour (juce::Colour (0xff3a424b));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 16; }

    //==========================================================================
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize, bool over, bool down) override
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (track.reduced (isVertical ? 2.0f : 0.0f, isVertical ? 0.0f : 2.0f), 3.0f);

        auto thumb = isVertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) width, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) height);
        g.setColour (over || down ? Theme::accent.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.20f));
        g.fillRoundedRectangle (thumb.reduced (isVertical ? 2.0f : 0.0f, isVertical ? 0.0f : 2.0f), 3.0f);
    }

    //==========================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds   = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto c        = bounds.getCentre();

        const float outerR  = diameter * 0.5f;
        const float trackW  = juce::jmax (2.5f, diameter * 0.055f);
        const float arcR    = outerR - trackW * 0.5f;
        const float bodyR   = outerR - trackW * 2.6f;
        const float angle   = startAngle + pos * (endAngle - startAngle);
        const auto  colour  = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  hot     = slider.isEnabled() && slider.isMouseOverOrDragging();

        // --- track + value arc -------------------------------------------------
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::trackBg);
        g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (pos > 0.002f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);

            g.setColour (colour.withAlpha (hot ? 0.30f : 0.17f));
            g.strokePath (value, juce::PathStrokeType (trackW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (value, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // --- knob body -----------------------------------------------------------
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - bodyR, c.y - bodyR + 2.5f, bodyR * 2.0f, bodyR * 2.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a515a), c.x - bodyR * 0.5f, c.y - bodyR * 0.9f,
                                                 juce::Colour (0xff14171b), c.x + bodyR * 0.4f, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        const float faceR = bodyR * 0.84f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30353c), c.x, c.y - faceR * 0.5f,
                                                 juce::Colour (0xff1a1d22), c.x + faceR, c.y, true));
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

        // --- pointer -------------------------------------------------------------
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -faceR + faceR * 0.13f, 3.0f, faceR * 0.38f, 1.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (hot ? colour.brighter (0.5f) : juce::Colours::white.withAlpha (0.92f));
        g.fillPath (pointer);
    }

    //==========================================================================
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float cy     = (float) y + (float) height * 0.5f;
        const auto  colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  hot    = slider.isEnabled() && slider.isMouseOverOrDragging();

        juce::Rectangle<float> track ((float) x, cy - 1.5f, (float) width, 3.0f);
        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (track, 1.5f);

        g.setColour (colour.withAlpha (0.20f));
        g.fillRoundedRectangle (track.withRight (sliderPos).expanded (0.0f, 2.0f), 3.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (track.withRight (sliderPos), 1.5f);

        const float r = hot ? 7.0f : 6.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (sliderPos - r, cy - r + 1.5f, r * 2.0f, r * 2.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f5f8), sliderPos, cy - r,
                                                 juce::Colour (0xff9aa3ad), sliderPos, cy + r, false));
        g.fillEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);

        if (hot)
        {
            g.setColour (colour.withAlpha (0.8f));
            g.drawEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        }
    }

    //==========================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::ui (11.0f, true, 0.1f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);

        if (! button.isEnabled())
        {
            g.setColour (juce::Colours::white.withAlpha (0.02f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (juce::Colour (0xff262b32));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            return;
        }

        // a button that is switched on (toggle / segmented control) keeps an accent tint
        const bool on = button.getToggleState();

        g.setColour (down ? Theme::accent.withAlpha (0.25f)
                          : highlighted ? Theme::accent.withAlpha (on ? 0.26f : 0.14f)
                                        : on ? Theme::accent.withAlpha (0.18f)
                                             : juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 5.0f);

        g.setColour (highlighted || down || on ? Theme::accent.withAlpha (on ? 0.9f : 0.85f) : juce::Colour (0xff343b43));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool) override
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (! button.isEnabled() ? Theme::textLo
                     : highlighted || button.getToggleState() ? Theme::accent.brighter (0.3f) : Theme::textHi);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }
};
