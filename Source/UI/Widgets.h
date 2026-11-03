#pragma once

#include "Theme.h"

//==============================================================================
/** The line under a knob. Shows the knob's name, and its value while the knob is hovered or
    dragged. A click turns it into a text box so a value can be typed. */
class KnobCaption : public juce::Label
{
public:
    KnobCaption()
    {
        setEditable (true, false, false);
        setJustificationType (juce::Justification::centred);
        setFont (Theme::ui (12.0f));
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    void paint (juce::Graphics& g) override
    {
        if (isBeingEdited())
            return;

        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (getFont());
        g.drawText (getText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

//==============================================================================
/** Flat rotary knob bound to a parameter. The caption below shows the name; while the knob is
    hovered or dragged it shows the value instead. Double-click resets to the default (done by the
    parameter attachment), a click on the caption lets you type a value. */
class LabeledKnob : public juce::Component
{
public:
    /** `colour` is the colour of the value arc: the page gives its own (cyan for amp and cab, the
        category colour for a pedal). */
    LabeledKnob (const juce::String& displayName, juce::Colour colour,
                 int decimals = 1, const juce::String& suffix = {})
        : name (displayName), unit (suffix), decimalPlaces (decimals), tint (colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.onValueChange = [this] { updateCaption(); };
        addAndMakeVisible (slider);

        caption.onTextChange = [this]
        {
            const auto text = caption.getText();
            if (text.containsAnyOf ("0123456789"))
                slider.setValue (parse ? parse (text) : text.getDoubleValue(), juce::sendNotificationSync);
            updateCaption();
        };
        caption.onEditorHide = [this] { updateCaption(); };
        addAndMakeVisible (caption);

        addMouseListener (this, true);   // hover over the slider or the caption
        updateCaption();
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        updateCaption();
    }

    /** Custom text for the value (e.g. "12.5 kHz") and how typed text is read back. */
    void setValueFormat (std::function<juce::String (double)> formatter, std::function<double (const juce::String&)> parser)
    {
        format = std::move (formatter);
        parse = std::move (parser);
        updateCaption();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        caption.setBounds (b.removeFromBottom (captionHeight));
        b.removeFromBottom (4);

        const int d = juce::jmin (b.getWidth(), b.getHeight(), maxDiameter);
        slider.setBounds (b.withSizeKeepingCentre (d, d));
    }

    /** Caps the knob size so the arc has room to breathe inside its cell. */
    void setMaxDiameter (int d) { maxDiameter = d; resized(); }

    /** The text under the knob right now (name or value). */
    juce::String getCaptionText() const { return caption.getText(); }
    juce::String getValueText() const
    {
        return format ? format (slider.getValue()) : juce::String (slider.getValue(), decimalPlaces) + unit;
    }

    void mouseEnter (const juce::MouseEvent&) override { updateCaption(); }
    void mouseExit  (const juce::MouseEvent&) override { updateCaption(); }
    void mouseDown  (const juce::MouseEvent&) override { updateCaption(); }
    void mouseUp    (const juce::MouseEvent&) override { updateCaption(); }

    juce::Slider slider;

private:
    static constexpr int captionHeight = 20;

    void updateCaption()
    {
        if (caption.isBeingEdited())
            return;

        const bool showValue = isMouseOverOrDragging (true) || slider.isMouseButtonDown();
        caption.setColour (juce::Label::textColourId, showValue ? Theme::textHi : Theme::textMid);
        caption.setText (showValue ? getValueText() : name, juce::dontSendNotification);
    }

    juce::String name, unit;
    int decimalPlaces;
    int maxDiameter = 96;
    juce::Colour tint;
    std::function<juce::String (double)> format;
    std::function<double (const juce::String&)> parse;
    KnobCaption caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Small pill switch, used for the bypass of a pedal. On = the pedal is active. */
class ToggleSwitch : public juce::Button
{
public:
    explicit ToggleSwitch (juce::Colour colour) : juce::Button ("switch"), tint (colour)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (36.0f, 20.0f);

        g.setColour (on ? tint.withAlpha (highlighted ? 0.95f : 0.85f) : Theme::trackBg.brighter (highlighted ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

        const float d = r.getHeight() - 6.0f;
        const float x = on ? r.getRight() - 3.0f - d : r.getX() + 3.0f;
        g.setColour (on ? juce::Colours::white : Theme::textMid);
        g.fillEllipse (x, r.getY() + 3.0f, d, d);
    }

private:
    juce::Colour tint;
};

//==============================================================================
/** Slim vertical peak meter: one colour, a small peak-hold tick, red only while it clips. */
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

    /** True while the meter shows a level that is at or very near full scale. */
    bool isClipping() const { return peak > -1.0f; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (title, r.removeFromTop (16.0f), juce::Justification::centred, false);
        r.removeFromTop (4.0f);

        auto bar = r.withSizeKeepingCentre (6.0f, r.getHeight());
        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (bar, 3.0f);

        const float h = bar.getHeight() * Theme::dbToUnit (shown, floorDb);
        if (h > 0.5f)
        {
            g.setColour (isClipping() ? Theme::danger : Theme::accent);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - h), 3.0f);
        }

        if (peak > floorDb + 1.0f)
        {
            const float py = bar.getBottom() - bar.getHeight() * Theme::dbToUnit (peak, floorDb);
            g.setColour (isClipping() ? Theme::danger : Theme::textMid);
            g.fillRect (bar.getX() - 2.0f, py - 1.0f, bar.getWidth() + 4.0f, 2.0f);
        }
    }

private:
    static constexpr float floorDb = -60.0f;
    juce::String title;
    float shown = floorDb, peak = floorDb;
    int holdFrames = 0;
};
