#pragma once

#include <JuceHeader.h>

// Central place for the visual language of the plugin:
// a near-black graphite palette, ONE cool accent (cyan) and ONE warm accent (amber).
namespace Theme
{
    // Design-space size of the whole UI. The editor scales this to the real window size.
    constexpr int designWidth  = 1280;
    constexpr int designHeight = 880;

    // Surfaces
    inline const juce::Colour bgTop        { 0xff14171b };
    inline const juce::Colour bgBottom     { 0xff0a0c0e };
    inline const juce::Colour panelTop     { 0xff1b1f24 };
    inline const juce::Colour panelBottom  { 0xff15181c };
    inline const juce::Colour panelBorder  { 0xff292f36 };
    inline const juce::Colour inset        { 0xff0c0e11 };
    inline const juce::Colour insetBorder  { 0xff23282e };
    inline const juce::Colour trackBg      { 0xff262b32 };
    inline const juce::Colour divider      { 0x0dffffff };

    // Text
    inline const juce::Colour textHi       { 0xffe9edf1 };
    inline const juce::Colour textMid      { 0xff98a2ad };
    inline const juce::Colour textLo       { 0xff5d6670 };

    // Accents
    inline const juce::Colour accent       { 0xff3dd3ee };   // cyan  - primary
    inline const juce::Colour amber        { 0xffffa43f };   // amber - gain / drive
    inline const juce::Colour danger       { 0xffff5d55 };

    //==========================================================================
    inline juce::Font ui (float size, bool bold = false, float kerning = 0.0f)
    {
        juce::Font f (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
    }

    inline juce::Font mono (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    //==========================================================================
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(),
                                                 panelBottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);

        g.setColour (panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

        // faint top highlight, gives the panel a machined edge
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f);
    }

    inline void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius = 7.0f)
    {
        g.setColour (inset);
        g.fillRoundedRectangle (r, radius);
        g.setColour (insetBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    inline float dbToUnit (float db, float floorDb = -60.0f)
    {
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
    }
}
