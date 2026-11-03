#pragma once

#include <JuceHeader.h>
#include <InterFontData.h>

// Central place for the visual language of the plugin: a neutral grey interface with ONE colour per
// screen. Amp and Cab pages use the cyan accent, every pedal page uses the soft colour of its
// category (see categoryColour). Red is only for clipping and errors.
namespace Theme
{
    // Design-space size of the whole UI. The editor scales this to the real window size.
    constexpr int designWidth  = 1120;
    constexpr int designHeight = 700;

    // 8 px grid
    constexpr int unit = 8;

    // Surfaces. One level of surface on top of the background, no gradients.
    inline const juce::Colour bg           { 0xff0f1113 };
    inline const juce::Colour surface      { 0xff17191c };
    inline const juce::Colour surfaceHi    { 0xff202327 };   // hovered / selected surface
    inline const juce::Colour panelBorder  { 0xff23262a };
    inline const juce::Colour inset        { 0xff111315 };
    inline const juce::Colour insetBorder  { 0xff1f2226 };
    inline const juce::Colour trackBg      { 0xff2a2d32 };
    inline const juce::Colour divider      { 0x14ffffff };

    // Text
    inline const juce::Colour textHi       { 0xffe6e8ea };
    inline const juce::Colour textMid      { 0xff9097a0 };
    inline const juce::Colour textLo       { 0xff5c626a };

    // Colours
    inline const juce::Colour accent       { 0xff3dd3ee };   // cyan: amp, cab and everything generic
    inline const juce::Colour amber        { 0xffe8a54b };   // the Gain category
    inline const juce::Colour danger       { 0xffff5d55 };   // clip and errors only

    // Kept so older call sites compile; the surfaces are flat now.
    inline const juce::Colour bgTop        = bg;
    inline const juce::Colour bgBottom     = bg;
    inline const juce::Colour panelTop     = surface;
    inline const juce::Colour panelBottom  = surface;

    /** Soft colour of a pedal category (the strings used by PedalCatalog). Unknown names get the accent. */
    inline juce::Colour categoryColour (const juce::String& category)
    {
        if (category == "DYNAMICS")   return juce::Colour (0xff9d8cf0);   // violet
        if (category == "GAIN")       return amber;
        if (category == "MODULATION") return juce::Colour (0xff5b9cf0);   // blue
        if (category == "TIME")       return juce::Colour (0xffe57fa8);   // pink
        if (category == "SPACE")      return juce::Colour (0xff4fc4b0);   // teal
        if (category == "NAM MODEL")  return juce::Colour (0xffe0825f);   // terracotta
        return accent;
    }

    //==========================================================================
    inline juce::Typeface::Ptr interTypeface (bool semiBold)
    {
        static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
            InterFontData::InterRegular_ttf, (size_t) InterFontData::InterRegular_ttfSize);
        static const juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (
            InterFontData::InterSemiBold_ttf, (size_t) InterFontData::InterSemiBold_ttfSize);
        return semiBold ? bold : regular;
    }

    /** Inter, in the two weights the UI uses: Regular and SemiBold (`bold`). */
    inline juce::Font ui (float size, bool bold = false, float kerning = 0.0f)
    {
        juce::Font f (juce::FontOptions (interTypeface (bold)).withHeight (size));
        return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
    }

    /** Numbers use Inter as well; kept as a name so call sites say what they show. */
    inline juce::Font mono (float size, bool bold = false) { return ui (size, bold); }

    /** "NOISE GATE 2" -> "Noise Gate 2". The catalog keeps its names in capitals; the UI shows Title Case. */
    inline juce::String titleCase (const juce::String& text)
    {
        juce::String out;
        bool wordStart = true;
        for (auto c : text)
        {
            out += wordStart ? juce::CharacterFunctions::toUpperCase (c) : juce::CharacterFunctions::toLowerCase (c);
            wordStart = (c == ' ' || c == '-' || c == '/');
        }
        return out;
    }

    //==========================================================================
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f)
    {
        g.setColour (surface);
        g.fillRoundedRectangle (r, radius);
    }

    inline void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
    {
        g.setColour (inset);
        g.fillRoundedRectangle (r, radius);
    }

    inline float dbToUnit (float db, float floorDb = -60.0f)
    {
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
    }
}
