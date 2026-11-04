#pragma once

#include "Theme.h"

/** Small line icons, drawn as paths in a unit square (0..1) so they scale with whatever they sit in:
    the Library sections, the pedal types and the Settings button. Outlines are stroked with round
    ends, the few solid bits (dots) are filled. */
namespace Icons
{
    struct Glyph
    {
        juce::Path line;   // stroked
        juce::Path dots;   // filled
    };

    namespace detail
    {
        inline void circle (juce::Path& p, float cx, float cy, float r) { p.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f); }

        inline void poly (juce::Path& p, std::initializer_list<juce::Point<float>> points)
        {
            bool first = true;
            for (auto pt : points)
            {
                if (first) p.startNewSubPath (pt); else p.lineTo (pt);
                first = false;
            }
        }

        inline void wave (juce::Path& p, float phase, float amplitude)
        {
            for (int i = 0; i <= 32; ++i)
            {
                const float x = (float) i / 32.0f;
                const float y = 0.5f - amplitude * std::sin (x * juce::MathConstants<float>::twoPi * 1.25f + phase);
                const juce::Point<float> pt (0.06f + x * 0.88f, y);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
        }
    }

    //==========================================================================
    // Library sections

    inline Glyph amp()
    {
        Glyph g;
        g.line.addRoundedRectangle (0.06f, 0.2f, 0.88f, 0.6f, 0.1f);
        detail::poly (g.line, { { 0.06f, 0.4f }, { 0.94f, 0.4f } });
        detail::circle (g.line, 0.5f, 0.6f, 0.1f);
        for (float x : { 0.26f, 0.5f, 0.74f })
            detail::circle (g.dots, x, 0.3f, 0.035f);
        return g;
    }

    inline Glyph stompbox()
    {
        Glyph g;
        g.line.addRoundedRectangle (0.22f, 0.07f, 0.56f, 0.86f, 0.1f);
        detail::circle (g.line, 0.5f, 0.69f, 0.12f);
        detail::circle (g.line, 0.5f, 0.3f, 0.07f);
        return g;
    }

    inline Glyph cab()
    {
        Glyph g;
        g.line.addRoundedRectangle (0.1f, 0.1f, 0.8f, 0.8f, 0.12f);
        detail::circle (g.line, 0.5f, 0.5f, 0.25f);
        detail::circle (g.dots, 0.5f, 0.5f, 0.07f);
        return g;
    }

    inline Glyph other()
    {
        Glyph g;
        for (float x : { 0.1f, 0.54f })
            for (float y : { 0.1f, 0.54f })
                g.line.addRoundedRectangle (x, y, 0.36f, 0.36f, 0.08f);
        return g;
    }

    //==========================================================================
    // Pedal types (the names of PedalCatalog)

    inline Glyph pedal (const juce::String& name)
    {
        Glyph g;
        const auto n = name.toUpperCase();

        if (n == "NOISE GATE")
        {
            detail::poly (g.line, { { 0.06f, 0.72f }, { 0.4f, 0.72f }, { 0.4f, 0.28f }, { 0.94f, 0.28f } });
        }
        else if (n == "COMPRESSOR")
        {
            detail::poly (g.line, { { 0.1f, 0.22f }, { 0.4f, 0.5f }, { 0.1f, 0.78f } });
            detail::poly (g.line, { { 0.9f, 0.22f }, { 0.6f, 0.5f }, { 0.9f, 0.78f } });
        }
        else if (n == "BOOST")
        {
            detail::poly (g.line, { { 0.5f, 0.88f }, { 0.5f, 0.14f } });
            detail::poly (g.line, { { 0.22f, 0.42f }, { 0.5f, 0.14f }, { 0.78f, 0.42f } });
        }
        else if (n == "OVERDRIVE")
        {
            // a sine wave with its peaks cut flat
            detail::poly (g.line, { { 0.05f, 0.5f }, { 0.2f, 0.24f }, { 0.4f, 0.24f }, { 0.5f, 0.5f },
                                    { 0.6f, 0.76f }, { 0.8f, 0.76f }, { 0.95f, 0.5f } });
        }
        else if (n == "CHORUS")
        {
            detail::wave (g.line, 0.0f, 0.22f);
            detail::wave (g.line, 1.1f, 0.22f);
        }
        else if (n == "DELAY")
        {
            // echoes that get shorter
            const float heights[] = { 0.72f, 0.52f, 0.36f, 0.22f };
            for (int i = 0; i < 4; ++i)
                detail::poly (g.line, { { 0.16f + 0.23f * (float) i, 0.5f - heights[i] * 0.5f },
                                        { 0.16f + 0.23f * (float) i, 0.5f + heights[i] * 0.5f } });
        }
        else if (n == "REVERB")
        {
            // ripples spreading to the right
            for (float r : { 0.2f, 0.42f, 0.64f })
                g.line.addCentredArc (0.16f, 0.5f, r, r, 0.0f,
                                      juce::MathConstants<float>::halfPi - 0.85f,
                                      juce::MathConstants<float>::halfPi + 0.85f, true);
            detail::circle (g.dots, 0.16f, 0.5f, 0.045f);
        }
        else   // NAM PEDAL and anything unknown: a captured waveform
        {
            const float heights[] = { 0.3f, 0.62f, 0.9f, 0.5f, 0.74f, 0.34f };
            for (int i = 0; i < 6; ++i)
                detail::poly (g.line, { { 0.12f + 0.152f * (float) i, 0.5f - heights[i] * 0.5f },
                                        { 0.12f + 0.152f * (float) i, 0.5f + heights[i] * 0.5f } });
        }

        return g;
    }

    //==========================================================================
    /** A soft gear: eight teeth with rounded tops and shoulders (the outline is one smooth curve), a round hole. */
    inline Glyph gear()
    {
        Glyph g;
        constexpr int teeth = 8, steps = 240;
        constexpr float inner = 0.33f, outer = 0.45f;
        auto smooth = [] (float a, float b, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); };

        for (int i = 0; i < steps; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
            const float x = std::fmod ((float) i * (float) teeth / (float) steps, 1.0f);
            const float tooth = smooth (0.04f, 0.3f, x) * (1.0f - smooth (0.5f, 0.76f, x));
            const float r = inner + (outer - inner) * tooth;
            const juce::Point<float> pt (0.5f + std::cos (angle) * r, 0.5f + std::sin (angle) * r);
            if (i == 0) g.line.startNewSubPath (pt); else g.line.lineTo (pt);
        }
        g.line.closeSubPath();
        detail::circle (g.line, 0.5f, 0.5f, 0.135f);
        return g;
    }

    //==========================================================================
    /** Draws `glyph` into `area` (a square is best) in `colour`. */
    inline void draw (juce::Graphics& g, const Glyph& glyph, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth = 1.6f)
    {
        const auto side = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
        const auto transform = juce::AffineTransform::scale (side).translated (box.getX(), box.getY());

        g.setColour (colour);

        juce::Path line (glyph.line), dots (glyph.dots);
        line.applyTransform (transform);
        dots.applyTransform (transform);

        g.strokePath (line, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (dots);
    }

    /** An iOS-style app icon: a rounded square filled with `colour` (lighter at the top) and the glyph in
        white on it. `opacity` fades the whole badge (a pedal that is all in use). */
    inline void drawBadge (juce::Graphics& g, const Glyph& glyph, juce::Rectangle<float> badge, juce::Colour colour,
                           float opacity = 1.0f)
    {
        g.setGradientFill (Theme::fillGradient (colour.withMultipliedAlpha (opacity), badge));
        g.fillRoundedRectangle (badge, badge.getWidth() * 0.27f);
        draw (g, glyph, badge.reduced (badge.getWidth() * 0.2f), juce::Colours::white.withAlpha (opacity),
              juce::jmax (1.2f, badge.getWidth() * 0.065f));
    }
}
