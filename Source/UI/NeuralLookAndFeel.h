#pragma once

#include "Theme.h"

/** Flat, quiet controls: thin tracks, no gradients, no drop shadows. The colour of a knob arc, a
    slider or a toggled button is whatever the control was given (rotarySliderFillColourId), so a
    page can tint all of its controls with its own colour. */
class NeuralLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuralLookAndFeel()
    {
        setDefaultSansSerifTypeface (Theme::interTypeface (false));

        setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
        setColour (juce::Slider::thumbColourId,            Theme::textHi);
        setColour (juce::Label::textColourId,              Theme::textHi);
        setColour (juce::TextButton::textColourOnId,       Theme::accent);   // a switched-on toggle button
        setColour (juce::TextEditor::backgroundColourId,   Theme::inset);
        setColour (juce::TextEditor::textColourId,         Theme::textHi);
        setColour (juce::TextEditor::highlightColourId,    Theme::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId,      Theme::panelBorder);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (juce::CaretComponent::caretColourId,    Theme::accent);

        setColour (juce::ComboBox::backgroundColourId,     Theme::inset);
        setColour (juce::ComboBox::textColourId,           Theme::textHi);
        setColour (juce::ComboBox::outlineColourId,        Theme::panelBorder);
        setColour (juce::ComboBox::arrowColourId,          Theme::textMid);
        setColour (juce::ComboBox::focusedOutlineColourId, Theme::accent);

        // list boxes that nobody styled (the channel lists of the audio device selector)
        setColour (juce::ListBox::backgroundColourId,      Theme::inset);
        setColour (juce::ListBox::textColourId,            Theme::textHi);
        setColour (juce::ListBox::outlineColourId,         Theme::panelBorder);

        // not quite opaque on purpose: the menu window then has transparent corners (see drawPopupMenuBackground)
        setColour (juce::PopupMenu::backgroundColourId,            Theme::surfaceHi.withAlpha ((juce::uint8) 254));
        setColour (juce::PopupMenu::textColourId,                  Theme::textHi);
        setColour (juce::PopupMenu::headerTextColourId,            Theme::textLo);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId,       Theme::textHi);

        setColour (juce::TooltipWindow::backgroundColourId, Theme::surfaceHi);
        setColour (juce::TooltipWindow::textColourId,       Theme::textHi);
        setColour (juce::TooltipWindow::outlineColourId,    Theme::panelBorder);
    }

    //==========================================================================
    // Text boxes (search, rename, typed knob values): the same soft corners as the buttons.
    // An editor drawn with this must not be opaque (setOpaque (false)), or its corners stay unpainted.
    static constexpr float editorRadius = 6.0f;

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (dynamic_cast<juce::AlertWindow*> (editor.getParentComponent()) != nullptr)
        {
            LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
            return;
        }

        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, editorRadius);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (dynamic_cast<juce::AlertWindow*> (editor.getParentComponent()) != nullptr)
        {
            LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
            return;
        }

        if (! editor.isEnabled())
            return;

        const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
        const float thickness = focused ? 1.5f : 1.0f;

        g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (thickness * 0.5f),
                                editorRadius, thickness);
    }

    //==========================================================================
    // Popup menus (sort order, the right click menu of a file): a rounded card with a soft highlight and
    // an accent tick. The window is not opaque (see the background colour), so the corners stay clear.
    int getPopupMenuBorderSize() override { return 6; }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setColour (Theme::surfaceHi);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Theme::panelBorder.brighter (0.2f));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = 9;
            return;
        }

        idealWidth = (int) std::ceil (Theme::ui (13.0f).getStringWidthFloat (text)) + 14 + 34;
        idealHeight = 30;
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String&, const juce::Drawable*, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour (Theme::divider);
            g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
            return;
        }

        auto r = area.reduced (2, 1);

        if (isHighlighted && isActive)
        {
            g.setColour (Theme::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }

        g.setColour (! isActive ? Theme::textLo
                                : textColour != nullptr ? *textColour
                                : isHighlighted ? Theme::textHi : Theme::textMid.brighter (0.25f));
        g.setFont (Theme::ui (13.0f, isTicked));
        g.drawText (text, r.withTrimmedLeft (12).withTrimmedRight (30), juce::Justification::centredLeft, true);

        if (isTicked)
        {
            const auto c = juce::Point<float> ((float) r.getRight() - 16.0f, (float) r.getCentreY());
            juce::Path check;
            check.startNewSubPath (c.x - 4.0f, c.y);
            check.lineTo (c.x - 1.5f, c.y + 3.0f);
            check.lineTo (c.x + 4.0f, c.y - 3.0f);
            g.setColour (Theme::accent);
            g.strokePath (check, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (hasSubMenu)
        {
            const auto c = juce::Point<float> ((float) r.getRight() - 14.0f, (float) r.getCentreY());
            juce::Path arrow;
            arrow.startNewSubPath (c.x - 1.5f, c.y - 3.5f);
            arrow.lineTo (c.x + 2.0f, c.y);
            arrow.lineTo (c.x - 1.5f, c.y + 3.5f);
            g.setColour (Theme::textMid);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==========================================================================
    // Call-out bubbles (the add-pedal popover): flat card, thin border, soft shadow, arrow.
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 20, { 0, 6 }).drawForPath (g, path);

        g.setColour (Theme::surfaceHi);
        g.fillPath (path);

        g.setColour (Theme::panelBorder.brighter (0.2f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 16; }

    //==========================================================================
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize, bool over, bool down) override
    {
        auto thumb = isVertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) width, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) height);
        thumb = thumb.reduced (isVertical ? 2.0f : 0.0f, isVertical ? 0.0f : 2.0f);

        g.setColour (juce::Colours::white.withAlpha (over || down ? 0.28f : 0.14f));
        g.fillRoundedRectangle (thumb, thumb.getHeight() > thumb.getWidth() ? thumb.getWidth() * 0.5f : thumb.getHeight() * 0.5f);
    }

    //==========================================================================
    /** Flat knob: a thin track, a value arc in the knob's colour, a quiet disc and a line pointer. */
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds   = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto c        = bounds.getCentre();

        const float outerR  = diameter * 0.5f;
        const float trackW  = juce::jmax (2.0f, diameter * 0.04f);
        const float arcR    = outerR - trackW * 0.5f;
        const float bodyR   = outerR - trackW * 3.2f;
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
            g.setColour (hot ? colour.brighter (0.15f) : colour);
            g.strokePath (value, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // --- knob body -----------------------------------------------------------
        g.setColour (hot ? Theme::surfaceHi.brighter (0.1f) : Theme::surfaceHi);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawEllipse (c.x - bodyR + 0.5f, c.y - bodyR + 0.5f, bodyR * 2.0f - 1.0f, bodyR * 2.0f - 1.0f, 1.0f);

        // --- pointer -------------------------------------------------------------
        const float inner = bodyR * 0.38f, outer = bodyR * 0.82f;
        const float dx = std::sin (angle), dy = -std::cos (angle);
        g.setColour (Theme::textHi);
        g.drawLine (c.x + dx * inner, c.y + dy * inner, c.x + dx * outer, c.y + dy * outer,
                    juce::jmax (2.0f, bodyR * 0.07f));
    }

    //==========================================================================
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float cy     = (float) y + (float) height * 0.5f;
        const auto  colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  hot    = slider.isEnabled() && slider.isMouseOverOrDragging();

        juce::Rectangle<float> track ((float) x, cy - 1.0f, (float) width, 2.0f);
        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (track, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (track.withRight (sliderPos), 1.0f);

        const float r = hot ? 6.0f : 5.0f;
        g.setColour (Theme::textHi);
        g.fillEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
    }

    //==========================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::ui (12.0f, true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat();
        const bool on = button.getToggleState();
        const bool segment = (bool) button.getProperties().getWithDefault ("segment", false);

        if (! button.isEnabled())
        {
            if (! segment)
            {
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            return;
        }

        if (segment)
        {
            // buttons of a segmented switch sit on a shared track; the selected one is a lighter pill
            if (on || highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (on ? (down ? 0.16f : 0.12f) : 0.06f));
                g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
            }
            return;
        }

        g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : highlighted ? 0.10f : 0.06f));
        g.fillRoundedRectangle (r, 6.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool) override
    {
        const bool segment = (bool) button.getProperties().getWithDefault ("segment", false);
        const bool on = button.getToggleState();

        g.setFont (getTextButtonFont (button, button.getHeight()));

        juce::Colour colour = Theme::textHi;
        if (! button.isEnabled())              colour = Theme::textLo;
        else if (segment)                      colour = on ? Theme::textHi : (highlighted ? Theme::textHi : Theme::textMid);
        else if (on)                           colour = button.findColour (juce::TextButton::textColourOnId);

        g.setColour (colour);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }
};
