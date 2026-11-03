#pragma once

#include <JuceHeader.h>
#include <InterFontData.h>

// Central place for the visual language of the plugin. This is the "Apple" variant: the dark
// system palette of macOS / iOS (true black window, #1C1C1E cards, #2C2C2E controls) with the vivid
// system colours. Every block has a colour of its own: the amp is orange, the cab teal, each pedal
// category one system colour; generic controls use system blue. Red is only for clipping and errors.
namespace Theme
{
    // Design-space size of the whole UI. The editor scales this to the real window size.
    constexpr int designWidth  = 1120;
    constexpr int designHeight = 700;

    // 8 px grid
    constexpr int unit = 8;

    // Surfaces (systemBackground / secondarySystemBackground / tertiary)
    inline const juce::Colour bg           { 0xff000000 };
    inline const juce::Colour surface      { 0xff1c1c1e };
    inline const juce::Colour surfaceHi    { 0xff2c2c2e };   // hovered / selected surface
    inline const juce::Colour panelBorder  { 0xff38383a };
    inline const juce::Colour inset        { 0xff2c2c2e };   // fields and segmented tracks sit lighter on the card
    inline const juce::Colour insetBorder  { 0xff3a3a3c };
    inline const juce::Colour trackBg      { 0xff3a3a3c };
    inline const juce::Colour divider      { 0x26ffffff };   // separator

    // Text (label / secondaryLabel / tertiaryLabel)
    inline const juce::Colour textHi       { 0xfff5f5f7 };
    inline const juce::Colour textMid      { 0xff98989f };
    inline const juce::Colour textLo       { 0xff636366 };

    // System colours (dark appearance)
    inline const juce::Colour accent       { 0xff0a84ff };   // systemBlue: everything generic
    inline const juce::Colour amber        { 0xffffd60a };   // systemYellow: the Gain category
    inline const juce::Colour danger       { 0xffff453a };   // systemRed: clip and errors only
    inline const juce::Colour ampColour    { 0xffff9f0a };   // systemOrange
    inline const juce::Colour cabColour    { 0xff40c8e0 };   // systemTeal
    inline const juce::Colour green        { 0xff30d158 };   // systemGreen
    inline const juce::Colour purple       { 0xffbf5af2 };   // systemPurple
    inline const juce::Colour pink         { 0xffff375f };   // systemPink
    inline const juce::Colour indigo       { 0xff5e5ce6 };   // systemIndigo
    inline const juce::Colour mint         { 0xff66d4cf };   // systemMint

    // Kept so older call sites compile; the surfaces are flat now.
    inline const juce::Colour bgTop        = bg;
    inline const juce::Colour bgBottom     = bg;
    inline const juce::Colour panelTop     = surface;
    inline const juce::Colour panelBottom  = surface;

    /** Colour of a pedal category (the strings used by PedalCatalog). Unknown names get the accent. */
    inline juce::Colour categoryColour (const juce::String& category)
    {
        if (category == "DYNAMICS")   return purple;
        if (category == "GAIN")       return amber;
        if (category == "MODULATION") return indigo;
        if (category == "TIME")       return pink;
        if (category == "SPACE")      return mint;
        if (category == "NAM MODEL")  return green;
        return accent;
    }

    /** The lighter end of a colour's gradient (Apple fills run from a lighter top to the colour). */
    inline juce::Colour light (juce::Colour c)  { return c.interpolatedWith (juce::Colours::white, 0.28f); }

    /** Vertical gradient for a filled shape in `c`: lighter at the top, the colour itself, a little deeper at the bottom. */
    inline juce::ColourGradient fillGradient (juce::Colour c, juce::Rectangle<float> r)
    {
        juce::ColourGradient grad (light (c), r.getX(), r.getY(), c.darker (0.25f), r.getX(), r.getBottom(), false);
        grad.addColour (0.45, c);
        return grad;
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
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 18.0f)
    {
        g.setColour (surface);
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    /** A card with a wash of `c` coming down from its top edge (the block page takes its block's colour). */
    inline void drawTintedPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius = 18.0f)
    {
        drawPanel (g, r, radius);

        juce::Path card;
        card.addRoundedRectangle (r, radius);
        juce::ColourGradient wash (c.withAlpha (0.17f), r.getX(), r.getY(),
                                   c.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.7f, false);
        wash.addColour (0.3, c.withAlpha (0.06f));
        g.setGradientFill (wash);
        g.fillPath (card);
    }

    inline void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        g.setColour (inset);
        g.fillRoundedRectangle (r, radius);
    }

    inline float dbToUnit (float db, float floorDb = -60.0f)
    {
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
    }

    /** Options for a popup menu opened by `owner`: it appears at the mouse (a right click) or under `below`,
        and the menu is drawn with the look and feel of the app (the menu window is a desktop window of its own,
        so it would not find that look by itself). */
    inline void styleMenu (juce::PopupMenu& menu, juce::Component& owner)
    {
        menu.setLookAndFeel (&owner.getLookAndFeel());
    }

    inline juce::PopupMenu::Options menuAtMouse (juce::Component& owner)
    {
        return juce::PopupMenu::Options()
            .withParentComponent (nullptr)
            .withTargetScreenArea (juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (1));
    }

    inline juce::PopupMenu::Options menuBelow (juce::Component& button)
    {
        return juce::PopupMenu::Options().withTargetScreenArea (button.getScreenBounds());
    }
}
