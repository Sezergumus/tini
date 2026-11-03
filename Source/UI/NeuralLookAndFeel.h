#pragma once

#include "Theme.h"

/** The "Apple" look: capsule buttons, an iOS segmented control, macOS menus with a solid blue
    highlight and Activity-ring knobs. The colour of a knob ring, a slider or a toggled button is
    whatever the control was given (rotarySliderFillColourId), so a page tints its controls with
    its own colour. */
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
        setColour (juce::TextEditor::highlightColourId,    Theme::accent.withAlpha (0.45f));
        setColour (juce::TextEditor::outlineColourId,      juce::Colours::transparentBlack);
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
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

        setColour (juce::TooltipWindow::backgroundColourId, Theme::surfaceHi);
        setColour (juce::TooltipWindow::textColourId,       Theme::textHi);
        setColour (juce::TooltipWindow::outlineColourId,    Theme::panelBorder);
    }

    //==========================================================================
    // Text boxes (search, rename, typed knob values): soft corners, a light fill and a blue focus ring.
    // An editor drawn with this must not be opaque (setOpaque (false)), or its corners stay unpainted.
    static constexpr float editorRadius = 9.0f;

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
        const auto r = juce::Rectangle<float> ((float) width, (float) height);

        if (focused)
        {
            // macOS focus ring: a soft halo in the accent around a crisp line
            g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId).withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (1.5f), editorRadius, 3.0f);
            g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId));
            g.drawRoundedRectangle (r.reduced (0.75f), editorRadius, 1.5f);
            return;
        }

        g.setColour (editor.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), editorRadius, 1.0f);
    }

    //==========================================================================
    // Popup menus (sort order, the right click menu of a file): a rounded card, a solid blue highlight with
    // white text as on macOS. The window is not opaque (see the background colour), so the corners stay clear.
    int getPopupMenuBorderSize() override { return 6; }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setColour (Theme::surfaceHi);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
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
        idealHeight = 28;
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
        const bool lit = isHighlighted && isActive;

        if (lit)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }

        g.setColour (! isActive ? Theme::textLo
                                : lit ? juce::Colours::white
                                : textColour != nullptr ? *textColour
                                : Theme::textHi);
        g.setFont (Theme::ui (13.0f, isTicked));
        g.drawText (text, r.withTrimmedLeft (12).withTrimmedRight (30), juce::Justification::centredLeft, true);

        if (isTicked)
        {
            const auto c = juce::Point<float> ((float) r.getRight() - 16.0f, (float) r.getCentreY());
            juce::Path check;
            check.startNewSubPath (c.x - 4.0f, c.y);
            check.lineTo (c.x - 1.5f, c.y + 3.0f);
            check.lineTo (c.x + 4.0f, c.y - 3.0f);
            g.setColour (lit ? juce::Colours::white : Theme::accent);
            g.strokePath (check, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (hasSubMenu)
        {
            const auto c = juce::Point<float> ((float) r.getRight() - 14.0f, (float) r.getCentreY());
            juce::Path arrow;
            arrow.startNewSubPath (c.x - 1.5f, c.y - 3.5f);
            arrow.lineTo (c.x + 2.0f, c.y);
            arrow.lineTo (c.x - 1.5f, c.y + 3.5f);
            g.setColour (lit ? juce::Colours::white : Theme::textMid);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==========================================================================
    // Call-out bubbles (the add-pedal popover): card, hairline border, deep soft shadow, arrow.
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 28, { 0, 10 }).drawForPath (g, path);

        g.setColour (Theme::surfaceHi);
        g.fillPath (path);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
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

        g.setColour (juce::Colours::white.withAlpha (over || down ? 0.35f : 0.18f));
        g.fillRoundedRectangle (thumb, thumb.getHeight() > thumb.getWidth() ? thumb.getWidth() * 0.5f : thumb.getHeight() * 0.5f);
    }

    //==========================================================================
    /** Activity-ring knob: a thick ring in a soft tint of the knob's colour, the value drawn over it as a
        gradient from a lighter tint to the full colour with a glowing round head, a raised dial and a white dot. */
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds   = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto c        = bounds.getCentre();

        const float trackW  = juce::jmax (4.0f, diameter * 0.085f);
        const float outerR  = diameter * 0.5f - trackW * 0.4f;   // room for the glow of the head
        const float arcR    = outerR - trackW * 0.5f;
        const float bodyR   = outerR - trackW * 2.0f;
        const float angle   = startAngle + pos * (endAngle - startAngle);
        const auto  colour  = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  hot     = slider.isEnabled() && slider.isMouseOverOrDragging();

        // --- ring + value arc ----------------------------------------------------
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colour.withAlpha (hot ? 0.26f : 0.20f));
        g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto to = hot ? colour.brighter (0.12f) : colour;

        if (pos > 0.002f)
        {
            // JUCE has no conic gradient: the arc is drawn in short pieces, each a step further along
            const auto from = Theme::light (colour);
            const int steps = juce::jmax (2, (int) std::ceil ((angle - startAngle) / 0.07f));

            for (int i = 0; i < steps; ++i)
            {
                const float a0 = startAngle + (angle - startAngle) * (float) i / (float) steps;
                const float a1 = startAngle + (angle - startAngle) * (float) (i + 1) / (float) steps;
                juce::Path piece;
                piece.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, juce::jmin (angle, a1 + 0.012f), true);
                g.setColour (from.interpolatedWith (to, (float) (i + 1) / (float) steps));
                g.strokePath (piece, juce::PathStrokeType (trackW, juce::PathStrokeType::curved,
                                                           i == 0 ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }
        }

        // round head of the arc, with a soft glow in the knob's colour (also at zero, as on the rings)
        {
            const juce::Point<float> head (c.x + std::sin (angle) * arcR, c.y - std::cos (angle) * arcR);
            juce::Path cap;
            cap.addEllipse (head.x - trackW * 0.5f, head.y - trackW * 0.5f, trackW, trackW);
            juce::DropShadow (to.withAlpha (0.6f), (int) (trackW * 1.4f), {}).drawForPath (g, cap);
            g.setColour (to);
            g.fillPath (cap);
        }

        // --- dial ----------------------------------------------------------------
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
        juce::Path bodyPath;
        bodyPath.addEllipse (body);
        juce::DropShadow (juce::Colours::black.withAlpha (0.65f), juce::jmax (4, (int) (bodyR * 0.3f)), { 0, (int) (bodyR * 0.06f) + 1 })
            .drawForPath (g, bodyPath);

        g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff4a4a4e : 0xff3c3c40), c.x, body.getY(),
                                                 juce::Colour (0xff1f1f22), c.x, body.getBottom(), false));
        g.fillPath (bodyPath);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        // --- dot -----------------------------------------------------------------
        const float dotR = juce::jmax (2.2f, bodyR * 0.085f);
        const float dist = bodyR * 0.70f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + std::sin (angle) * dist - dotR, c.y - std::cos (angle) * dist - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    //==========================================================================
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float cy     = (float) y + (float) height * 0.5f;
        const auto  colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  hot    = slider.isEnabled() && slider.isMouseOverOrDragging();

        juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) width, 4.0f);
        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

        const float r = hot ? 8.0f : 7.0f;
        juce::Path thumb;
        thumb.addEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 6, { 0, 2 }).drawForPath (g, thumb);
        g.setColour (juce::Colours::white);
        g.fillPath (thumb);
    }

    //==========================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::ui (12.5f, true); }

    /** Buttons are capsules. A button with the "primary" property is filled with the accent (Save, Load to
        amp); a toggled button is filled with its on-colour. Segments of a segmented control ("segment") sit
        on a shared track and the chosen one is a raised grey pill, as on iOS. */
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat();
        const bool on = button.getToggleState();
        const bool segment = (bool) button.getProperties().getWithDefault ("segment", false);
        const bool primary = (bool) button.getProperties().getWithDefault ("primary", false);
        const float radius = juce::jmin (r.getHeight() * 0.5f, 16.0f);

        if (! button.isEnabled())
        {
            if (! segment)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (r, radius);
            }
            return;
        }

        if (segment)
        {
            auto pill = r.reduced (2.0f);
            if (on)
            {
                juce::Path p;
                p.addRoundedRectangle (pill, 7.0f);
                juce::DropShadow (juce::Colours::black.withAlpha (0.4f), 4, { 0, 1 }).drawForPath (g, p);
                g.setColour (juce::Colour (down ? 0xff76767b : 0xff636366));
                g.fillPath (p);
            }
            else if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (pill, 7.0f);
            }
            return;
        }

        if (primary || on)
        {
            const auto c = primary ? Theme::accent : button.findColour (juce::TextButton::textColourOnId);
            g.setGradientFill (Theme::fillGradient (down ? c.darker (0.2f) : highlighted ? c.brighter (0.12f) : c, r));
            g.fillRoundedRectangle (r, radius);
            return;
        }

        g.setColour (juce::Colours::white.withAlpha (down ? 0.20f : highlighted ? 0.15f : 0.10f));
        g.fillRoundedRectangle (r, radius);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool) override
    {
        const bool segment = (bool) button.getProperties().getWithDefault ("segment", false);
        const bool primary = (bool) button.getProperties().getWithDefault ("primary", false);
        const bool on = button.getToggleState();

        g.setFont (getTextButtonFont (button, button.getHeight()));

        juce::Colour colour = Theme::textHi;
        if (! button.isEnabled())              colour = Theme::textLo;
        else if (segment)                      colour = on || highlighted ? Theme::textHi : Theme::textMid;
        else if (primary || on)                colour = juce::Colours::white;

        g.setColour (colour);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }
};
