#pragma once

#include "Theme.h"

//==============================================================================
/** A page tab in the header ("Rig", "Library"): flat text, the open page is bright with a line under it.
    Tabs share a radio group, so one is always lit. Adding a page means adding one more TabButton. */
class TabButton : public juce::Button
{
public:
    TabButton (const juce::String& label, int radioGroup) : juce::Button (label)
    {
        setButtonText (label);
        setClickingTogglesState (true);
        setRadioGroupId (radioGroup);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    /** Width that fits the label. */
    int getIdealWidth() const { return (int) Theme::ui (14.0f, true).getStringWidthFloat (getButtonText()) + 28; }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const bool open = getToggleState();

        if (highlighted && ! open)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r.reduced (2.0f, 9.0f), 6.0f);
        }

        g.setColour (open || highlighted ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText (getButtonText(), r, juce::Justification::centred, false);

        if (open)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.getX() + 14.0f, r.getBottom() - 9.0f, r.getWidth() - 28.0f, 2.0f, 1.0f);
        }
    }
};

//==============================================================================
/** Gear icon in the header: opens and closes the Settings page. Lit while the page is open. */
class GearButton : public juce::Button
{
public:
    static constexpr int size = 32;

    GearButton() : juce::Button ("settings")
    {
        setClickingTogglesState (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Settings");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.07f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        const auto c = r.getCentre();
        const float outer = 9.0f, inner = 6.8f;

        juce::Path gear;
        gear.setUsingNonZeroWinding (false);   // the hole is cut out by the even-odd rule
        gear.addStar (c, 8, inner, outer, juce::MathConstants<float>::pi / 8.0f);
        gear.addEllipse (c.x - 2.7f, c.y - 2.7f, 5.4f, 5.4f);     // axle hole

        g.setColour (getToggleState() ? Theme::accent : highlighted ? Theme::textHi : Theme::textMid);
        g.fillPath (gear);
    }
};

//==============================================================================
/** Minimise / fit-to-screen / close, drawn flat at the right end of the header. Standalone only. */
class WindowButton : public juce::Button
{
public:
    enum class Kind { minimise, maximise, close };

    static constexpr int width = 44, height = 32;

    explicit WindowButton (Kind k)
        : juce::Button (k == Kind::minimise ? "window_minimise" : k == Kind::maximise ? "window_maximise" : "window_close"),
          kind (k)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (k == Kind::minimise ? "Minimise" : k == Kind::maximise ? "Fit to screen" : "Close");
    }

    /** Maximise shows two overlapping squares while the window is fitted to the screen. */
    void setFitted (bool isFitted)
    {
        if (fitted == isFitted)
            return;
        fitted = isFitted;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (highlighted || down)
        {
            g.setColour (kind == Kind::close ? Theme::danger.withAlpha (down ? 0.85f : 1.0f)
                                             : juce::Colours::white.withAlpha (down ? 0.12f : 0.07f));
            g.fillRoundedRectangle (r.reduced (2.0f, 0.0f), 6.0f);
        }

        g.setColour (highlighted && kind == Kind::close ? juce::Colours::white : highlighted ? Theme::textHi : Theme::textMid);

        const auto c = r.getCentre();
        const juce::PathStrokeType stroke (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path p;

        switch (kind)
        {
            case Kind::minimise:
                p.startNewSubPath (c.x - 5.0f, c.y + 0.5f);
                p.lineTo (c.x + 5.0f, c.y + 0.5f);
                break;

            case Kind::maximise:
                if (fitted)
                {
                    p.addRectangle (c.x - 5.0f, c.y - 2.0f, 7.0f, 7.0f);
                    p.startNewSubPath (c.x - 2.0f, c.y - 2.0f);
                    p.lineTo (c.x - 2.0f, c.y - 5.0f);
                    p.lineTo (c.x + 5.0f, c.y - 5.0f);
                    p.lineTo (c.x + 5.0f, c.y + 2.0f);
                    p.lineTo (c.x + 2.0f, c.y + 2.0f);
                }
                else
                {
                    p.addRectangle (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
                }
                break;

            case Kind::close:
                p.startNewSubPath (c.x - 5.0f, c.y - 5.0f);  p.lineTo (c.x + 5.0f, c.y + 5.0f);
                p.startNewSubPath (c.x + 5.0f, c.y - 5.0f);  p.lineTo (c.x - 5.0f, c.y + 5.0f);
                break;
        }

        g.strokePath (p, stroke);
    }

    Kind getKind() const { return kind; }

private:
    Kind kind;
    bool fitted = false;
};
