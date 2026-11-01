#pragma once

#include "Theme.h"

//==============================================================================
/** Small read-out box below a knob. Double-click to type a value. */
class ValueBox : public juce::Label
{
public:
    ValueBox()
    {
        setEditable (false, true, false);
        setJustificationType (juce::Justification::centred);
        setFont (Theme::mono (12.0f));
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawInset (g, getLocalBounds().toFloat(), 4.0f);

        if (! isBeingEdited())
        {
            g.setColour (findColour (juce::Label::textColourId));
            g.setFont (Theme::mono (12.0f));
            g.drawText (getText(), getLocalBounds(), juce::Justification::centred, false);
        }
    }
};

//==============================================================================
/** Rotary knob + caption + editable value read-out, bound to a parameter. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (const juce::String& displayName, juce::Colour colour,
                 int decimals = 1, const juce::String& suffix = {})
        : name (displayName), unit (suffix), decimalPlaces (decimals), tint (colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.onValueChange = [this] { updateValueText(); };
        addAndMakeVisible (slider);

        valueBox.setColour (juce::Label::textColourId, colour);
        valueBox.onTextChange = [this]
        {
            const auto text = valueBox.getText();
            if (text.containsAnyOf ("0123456789"))
                slider.setValue (text.getDoubleValue(), juce::sendNotificationSync);
            updateValueText();
        };
        addAndMakeVisible (valueBox);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        updateValueText();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (11.0f, true, 0.12f));
        g.drawText (name, nameArea, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto b = getLocalBounds();

        const int valueH = 20, nameH = 16;
        auto valueArea = b.removeFromBottom (valueH);
        nameArea       = b.removeFromBottom (nameH);
        b.removeFromBottom (2);

        const int d = juce::jmin (b.getWidth(), b.getHeight(), maxDiameter);
        slider.setBounds (b.withSizeKeepingCentre (d, d));
        valueBox.setBounds (valueArea.withSizeKeepingCentre (juce::jmin (getWidth(), 64), valueH));
    }

    /** Caps the knob size so the arc has room to breathe inside its cell. */
    void setMaxDiameter (int d) { maxDiameter = d; resized(); }

    juce::Slider slider;

private:
    void updateValueText()
    {
        valueBox.setText (juce::String (slider.getValue(), decimalPlaces) + unit, juce::dontSendNotification);
    }

    juce::String name, unit;
    int decimalPlaces;
    int maxDiameter = 80;
    juce::Colour tint;
    juce::Rectangle<int> nameArea;
    ValueBox valueBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Horizontal slider with a caption, a formatted value and range hints. */
class FilterSlider : public juce::Component
{
public:
    FilterSlider (const juce::String& caption, const juce::String& minHint, const juce::String& maxHint,
                  std::function<juce::String (double)> formatter)
        : name (caption), lo (minHint), hi (maxHint), format (std::move (formatter))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (16);

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (10.5f, true, 0.12f));
        g.drawText (name, top, juce::Justification::centredLeft, false);

        g.setColour (Theme::accent);
        g.setFont (Theme::mono (11.0f, true));
        g.drawText (format (slider.getValue()), top, juce::Justification::centredRight, false);

        auto bottom = b.removeFromBottom (12);
        g.setColour (Theme::textLo);
        g.setFont (Theme::mono (9.5f));
        g.drawText (lo, bottom, juce::Justification::centredLeft, false);
        g.drawText (hi, bottom, juce::Justification::centredRight, false);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedTop (18).withTrimmedBottom (12));
    }

    juce::Slider slider;

private:
    juce::String name, lo, hi;
    std::function<juce::String (double)> format;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Round metal footswitch used to bypass a pedal. */
class FootSwitch : public juce::Button
{
public:
    explicit FootSwitch (juce::Colour colour) : juce::Button ("footswitch"), tint (colour)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto area = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        auto c = area.getCentre();
        auto round = juce::Rectangle<float> (d, d).withCentre (c);

        // base shadow + outer ring
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (round.translated (0.0f, 2.5f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4048), round.getX(), round.getY(),
                                                 juce::Colour (0xff111316), round.getX(), round.getBottom(), false));
        g.fillEllipse (round);

        if (getToggleState())
        {
            g.setColour (tint.withAlpha (0.55f));
            g.drawEllipse (round.reduced (0.5f), 1.2f);
        }

        // cap
        auto cap = round.reduced (d * 0.12f);
        if (down) cap = cap.reduced (1.5f);

        g.setGradientFill (juce::ColourGradient (highlighted ? juce::Colour (0xff4b525b) : juce::Colour (0xff3f454d),
                                                 cap.getX(), cap.getY(),
                                                 juce::Colour (0xff1a1d21), cap.getX(), cap.getBottom(), false));
        g.fillEllipse (cap);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (cap, 1.0f);

        // centre screw
        auto dot = cap.withSizeKeepingCentre (d * 0.22f, d * 0.22f);
        g.setColour (juce::Colour (0xff0c0e10));
        g.fillEllipse (dot);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (dot, 1.0f);
    }

private:
    juce::Colour tint;
};

//==============================================================================
/** Vertical peak meter with peak-hold and a rotated caption. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& caption) : title (caption) {}

    /** Feed the newest block peak (dBFS). Call at the UI refresh rate. */
    void pushLevel (float db)
    {
        db = juce::jmax (db, floorDb);

        shown = db > shown ? db : juce::jmax (db, shown - 0.9f);   // ~54 dB/s fall-off

        if (db >= peak) { peak = db; holdFrames = 45; }
        else if (holdFrames > 0) --holdFrames;
        else peak = juce::jmax (floorDb, peak - 0.6f);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // caption, rotated
        auto captionArea = r.removeFromTop (74.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                            captionArea.getCentreX(), captionArea.getCentreY()));
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (10.5f, true, 0.25f));
            g.drawText (title, juce::Rectangle<float> (60.0f, 14.0f).withCentre (captionArea.getCentre()),
                        juce::Justification::centred, false);
        }

        // read-out
        auto readout = r.removeFromBottom (24.0f);
        g.setFont (Theme::mono (11.0f, true));
        g.setColour (shown > -1.0f ? Theme::danger : Theme::accent);
        g.drawText (shown <= floorDb + 0.1f ? "-inf" : juce::String (shown, 1), readout,
                    juce::Justification::centred, false);

        // bar
        auto bar = r.withSizeKeepingCentre (10.0f, r.getHeight() - 6.0f);
        Theme::drawInset (g, bar.expanded (2.0f), 5.0f);

        const float h = bar.getHeight() * Theme::dbToUnit (shown, floorDb);
        if (h > 0.5f)
        {
            juce::ColourGradient grad (Theme::accent, 0.0f, bar.getBottom(), Theme::danger, 0.0f, bar.getY(), false);
            grad.addColour (0.74, Theme::accent);
            grad.addColour (0.88, Theme::amber);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - h), 3.0f);
        }

        // LED-style segmentation
        g.setColour (Theme::inset);
        for (float y = bar.getBottom() - 4.0f; y > bar.getY(); y -= 4.0f)
            g.fillRect (bar.getX() - 1.0f, y, bar.getWidth() + 2.0f, 1.2f);

        // peak hold
        const float py = bar.getBottom() - bar.getHeight() * Theme::dbToUnit (peak, floorDb);
        if (peak > floorDb + 1.0f)
        {
            g.setColour (peak > -3.0f ? Theme::danger : Theme::textHi.withAlpha (0.85f));
            g.fillRect (bar.getX() - 2.0f, py - 1.0f, bar.getWidth() + 4.0f, 2.0f);
        }

        // scale ticks
        g.setColour (Theme::textLo.withAlpha (0.6f));
        for (float db : { -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float ty = bar.getBottom() - bar.getHeight() * Theme::dbToUnit (db, floorDb);
            g.fillRect (bar.getRight() + 4.0f, ty, 3.0f, 1.0f);
        }
    }

private:
    static constexpr float floorDb = -60.0f;
    juce::String title;
    float shown = floorDb, peak = floorDb;
    int holdFrames = 0;
};

//==============================================================================
/** Oscilloscope. Triggers on a rising zero-crossing and auto-scales the trace. */
class WaveformView : public juce::Component
{
public:
    void setData (const float* samples, int numSamples)
    {
        // find a rising edge in the first half for a stable picture
        int start = 0;
        const int window = numSamples / 2;
        for (int i = 1; i < numSamples - window; ++i)
            if (samples[i - 1] <= 0.0f && samples[i] > 0.0f) { start = i; break; }

        float peak = 0.0f;
        for (int i = 0; i < window; ++i)
        {
            points[(size_t) i] = samples[start + i];
            peak = juce::jmax (peak, std::abs (points[(size_t) i]));
        }
        numPoints = window;

        const float target = peak > 0.002f ? juce::jmin (0.9f / peak, 12.0f) : 1.0f;
        gain += (target - gain) * 0.15f;

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawInset (g, r, 8.0f);

        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), 7.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        // soft glow behind the trace
        g.setGradientFill (juce::ColourGradient (Theme::accent.withAlpha (0.07f), r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::transparentBlack, r.getX(), r.getY(), true));
        g.fillRect (r);

        // grid
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        for (int i = 1; i < 10; ++i)
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 10.0f), r.getY(), r.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        // trace
        if (numPoints > 1)
        {
            auto area = r.reduced (0.0f, 18.0f);
            juce::Path path;
            for (int i = 0; i < numPoints; ++i)
            {
                const float x = area.getX() + area.getWidth() * (float) i / (float) (numPoints - 1);
                const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, points[(size_t) i] * gain) * area.getHeight() * 0.5f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }

            auto fadeGradient = [&r] (juce::Colour c, float alpha)
            {
                juce::ColourGradient fade (c.withAlpha (0.0f), r.getX(), 0.0f,
                                           c.withAlpha (0.0f), r.getRight(), 0.0f, false);
                fade.addColour (0.12, c.withAlpha (alpha));
                fade.addColour (0.88, c.withAlpha (alpha));
                return fade;
            };

            const juce::PathStrokeType wide (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const juce::PathStrokeType mid  (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const juce::PathStrokeType core (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            g.setGradientFill (fadeGradient (Theme::accent, 0.07f));
            g.strokePath (path, wide);
            g.setGradientFill (fadeGradient (Theme::accent, 0.16f));
            g.strokePath (path, mid);
            g.setGradientFill (fadeGradient (Theme::accent.brighter (0.6f), 1.0f));
            g.strokePath (path, core);
        }

        g.restoreState();

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (9.5f, true, 0.2f));
        g.drawText ("OUTPUT SCOPE", getLocalBounds().reduced (12, 8), juce::Justification::topLeft, false);
        g.drawText ("AUTO GAIN",    getLocalBounds().reduced (12, 8), juce::Justification::bottomRight, false);
    }

private:
    std::array<float, 512> points {};
    int numPoints = 0;
    float gain = 1.0f;
};

//==============================================================================
/** Illustration of the cabinet. The speaker rings react to the output level. */
class CabView : public juce::Component
{
public:
    void setIrLoaded (bool loaded)
    {
        irLoaded = loaded;
        repaint();
    }

    void setLevel (float unit)
    {
        glow += (unit - glow) * 0.35f;
        repaint();
    }

    void resized() override
    {
        weave.clear();
        auto r = getLocalBounds().toFloat();
        for (float x = -r.getHeight(); x < r.getWidth(); x += 7.0f)
        {
            weave.startNewSubPath (x, 0.0f);
            weave.lineTo (x + r.getHeight(), r.getHeight());
            weave.startNewSubPath (x + r.getHeight(), 0.0f);
            weave.lineTo (x, r.getHeight());
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawInset (g, r, 8.0f);

        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), 7.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        // grille cloth
        g.setColour (juce::Colours::white.withAlpha (0.022f));
        g.strokePath (weave, juce::PathStrokeType (1.0f));

        auto c = r.getCentre().translated (0.0f, -6.0f);
        const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.36f;

        // level glow
        const auto glowColour = irLoaded ? Theme::accent : Theme::textLo;
        const float a = (irLoaded ? 0.10f : 0.03f) + glow * 0.38f;
        g.setGradientFill (juce::ColourGradient (glowColour.withAlpha (a), c.x, c.y,
                                                 glowColour.withAlpha (0.0f), c.x + R * 1.9f, c.y, true));
        g.fillEllipse (c.x - R * 1.9f, c.y - R * 1.9f, R * 3.8f, R * 3.8f);

        // speaker frame
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343a41), c.x, c.y - R,
                                                 juce::Colour (0xff111316), c.x, c.y + R, false));
        g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);

        // cone
        const float cone = R * 0.86f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23282e), c.x - cone * 0.4f, c.y - cone * 0.5f,
                                                 juce::Colour (0xff0e1013), c.x + cone, c.y + cone, true));
        g.fillEllipse (c.x - cone, c.y - cone, cone * 2.0f, cone * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (float k : { 0.82f, 0.64f, 0.46f })
            g.drawEllipse (c.x - cone * k, c.y - cone * k, cone * k * 2.0f, cone * k * 2.0f, 1.0f);

        // dust cap
        const float cap = R * 0.24f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4048), c.x - cap * 0.4f, c.y - cap * 0.6f,
                                                 juce::Colour (0xff15181b), c.x + cap, c.y + cap, true));
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);

        // active ring
        g.setColour (glowColour.withAlpha (irLoaded ? 0.35f + glow * 0.6f : 0.12f));
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.5f);

        g.restoreState();

        // caption
        g.setFont (Theme::mono (10.5f));
        g.setColour (Theme::textLo);
        g.drawText (irLoaded ? "CONVOLUTION ACTIVE" : "NO IMPULSE RESPONSE",
                    getLocalBounds().reduced (12, 9), juce::Justification::bottomLeft, false);
        g.drawText ("1 x 12", getLocalBounds().reduced (12, 9), juce::Justification::bottomRight, false);
    }

private:
    bool irLoaded = false;
    float glow = 0.0f;
    juce::Path weave;
};
