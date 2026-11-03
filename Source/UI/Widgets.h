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

    /** The typing box: the app's own text box (rounded, inset, accent outline) in the label's font,
        text centred under the knob. Not taken from the label's colours (which dim while idle). */
    juce::TextEditor* createEditorComponent() override
    {
        auto* editor = new juce::TextEditor (getName());
        editor->setOpaque (false);
        editor->setBorder ({});
        editor->setIndents (4, 0);
        editor->setJustification (juce::Justification::centred);
        editor->setFont (getFont());
        editor->applyFontToAllText (getFont());
        return editor;
    }

    void resized() override
    {
        juce::Label::resized();

        // a value is short: a compact box in the middle instead of the whole caption row
        if (auto* editor = getCurrentTextEditor())
            editor->setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth(), 76), getHeight()));
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
        // iOS switch: a capsule filled with the colour when on, a white knob with a soft shadow
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (42.0f, 25.0f);

        if (on)
            g.setGradientFill (Theme::fillGradient (highlighted ? tint.brighter (0.1f) : tint, r));
        else
            g.setColour (juce::Colour (0xff39393d).brighter (highlighted ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

        const float d = r.getHeight() - 4.0f;
        const float x = on ? r.getRight() - 2.0f - d : r.getX() + 2.0f;
        juce::Path knob;
        knob.addEllipse (x, r.getY() + 2.0f, d, d);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 4, { 0, 2 }).drawForPath (g, knob);
        g.setColour (juce::Colours::white);
        g.fillPath (knob);
    }

private:
    juce::Colour tint;
};

//==============================================================================
/** Previous / next arrow (presets, models, impulse responses). */
class StepButton : public juce::Button
{
public:
    explicit StepButton (bool pointsRight, const juce::String& name = {})
        : juce::Button (name.isNotEmpty() ? name : (pointsRight ? "preset_next" : "preset_prev")), right (pointsRight)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.07f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        const auto c = r.getCentre();
        const float dx = right ? 2.5f : -2.5f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x - dx, c.y - 5.0f);
        chevron.lineTo (c.x + dx, c.y);
        chevron.lineTo (c.x - dx, c.y + 5.0f);

        g.setColour (highlighted ? Theme::textHi : Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool right;
};

//==============================================================================
/** Slim horizontal peak meter with a gain slider: a label on the left; on top a thin gain line with a
    thumb (drag, wheel, double click = 0 dB) and the value on its right; under it the level bar with
    small dB marks. One colour, a peak-hold tick, red only while the level clips.

    The level bar always shows the signal at its own point (after the input gain, after the output
    gain), so it also works as the visualiser for the gain you set. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& caption) : title (caption) {}

    /** Makes the gain line work: it edits `parameterID` (a dB value) of `apvts`, host-automatable. */
    void attachGain (juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID)
    {
        gainParam = apvts.getParameter (parameterID);
        gainRange = apvts.getParameterRange (parameterID);
        syncGain();
        repaint();
    }

    /** Current gain in dB (0 when no gain is attached). */
    float getGainDb() const { return gainDb; }

    /** Sets the gain in dB (clamped to the parameter's range) and tells the host. */
    void setGainDb (float db)
    {
        if (gainParam == nullptr)
            return;

        db = gainRange.snapToLegalValue (gainRange.getRange().clipValue (db));
        gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (db));
        syncGain();
        repaint();
    }

    /** Feed the newest block peak (dBFS). Call at the UI refresh rate. */
    void pushLevel (float db)
    {
        db = juce::jmax (db, floorDb);

        shown = db > shown ? db : juce::jmax (db, shown - 0.9f);   // ~54 dB/s fall-off

        if (db >= peak) { peak = db; holdFrames = 45; }
        else if (holdFrames > 0) --holdFrames;
        else peak = juce::jmax (floorDb, peak - 0.6f);

        syncGain();   // follows host automation and presets
        repaint();
    }

    /** True while the meter shows a level that is at or very near full scale. */
    bool isClipping() const { return peak > -1.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto area = contentArea();

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (title, area.withWidth (labelWidth).withHeight (area.getHeight() - marksHeight),
                    juce::Justification::centredLeft, false);

        const auto bar = barArea();
        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (bar, barHeight * 0.5f);

        const float w = bar.getWidth() * Theme::dbToUnit (shown, floorDb);
        if (w > 0.5f)
        {
            // green through yellow to orange along the scale (fixed to dB, not to the level), red while clipping
            if (isClipping())
            {
                g.setColour (Theme::danger);
            }
            else
            {
                juce::ColourGradient grad (Theme::green, bar.getX(), 0.0f, Theme::ampColour, bar.getRight(), 0.0f, false);
                grad.addColour (Theme::dbToUnit (-18.0f, floorDb), Theme::green);
                grad.addColour (Theme::dbToUnit (-6.0f, floorDb), Theme::amber);
                g.setGradientFill (grad);
            }
            g.fillRoundedRectangle (bar.withWidth (w), barHeight * 0.5f);
        }

        if (peak > floorDb + 1.0f)
        {
            const float px = bar.getX() + bar.getWidth() * Theme::dbToUnit (peak, floorDb);
            g.setColour (isClipping() ? Theme::danger : Theme::textHi);
            g.fillRect (px - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        }

        // dB marks under the bar
        g.setFont (Theme::ui (9.5f));
        g.setColour (Theme::textLo);
        for (const int mark : { -48, -24, -12, -6, 0 })
        {
            const float x = bar.getX() + bar.getWidth() * Theme::dbToUnit ((float) mark, floorDb);
            g.drawText (juce::String (mark), juce::Rectangle<float> (x - 14.0f, bar.getBottom() + 4.0f, 28.0f, 12.0f),
                        juce::Justification::centred, false);
        }

        if (gainParam != nullptr)
            paintGain (g);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (gainParam == nullptr || ! gainHitArea().contains (e.position))
            return;

        dragging = true;
        gainParam->beginChangeGesture();
        setGainFromX (e.position.x);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            setGainFromX (e.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
        {
            dragging = false;
            gainParam->endChangeGesture();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (gainParam != nullptr && gainHitArea().contains (e.position))
            setGainDb (0.0f);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (gainParam == nullptr || wheel.deltaY == 0.0f)
            return;

        gainParam->beginChangeGesture();
        setGainDb (gainDb + (wheel.deltaY > 0.0f ? 0.5f : -0.5f));
        gainParam->endChangeGesture();
    }

    /** The gain line's bounds (empty without a gain), so tests can drag it. */
    juce::Rectangle<float> getGainLineBounds() const { return gainParam != nullptr ? gainRowArea() : juce::Rectangle<float>(); }

private:
    static constexpr float floorDb = -60.0f;
    static constexpr float labelWidth = 28.0f;
    static constexpr float barHeight = 6.0f;
    static constexpr float rowHeight = 14.0f;       // the gain line row (only when a gain is attached)
    static constexpr float barRowHeight = 12.0f;    // the level bar row
    static constexpr float marksHeight = 16.0f;     // dB marks under the bar
    static constexpr float valueWidth = 56.0f;      // gain text on the right of the gain line

    /** The unit (gain line + bar + marks) is centred as a whole in the component, so the footer can put
        everything else on the same centre line. */
    juce::Rectangle<float> contentArea() const
    {
        const float h = (gainParam != nullptr ? rowHeight : 0.0f) + barRowHeight + marksHeight;
        return getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), h);
    }

    juce::Rectangle<float> gainRowArea() const
    {
        auto r = contentArea();
        r.removeFromLeft (labelWidth);
        return r.removeFromTop (rowHeight).withTrimmedRight (valueWidth);
    }

    juce::Rectangle<float> barArea() const
    {
        auto r = contentArea();
        r.removeFromLeft (labelWidth);
        if (gainParam != nullptr)
            r.removeFromTop (rowHeight);
        auto row = r.removeFromTop (barRowHeight).withTrimmedRight (valueWidth);
        return row.withSizeKeepingCentre (row.getWidth(), barHeight);
    }

    juce::Rectangle<float> gainHitArea() const { return gainRowArea().expanded (6.0f, 4.0f); }

    float gainToX (float db) const
    {
        const auto row = gainRowArea();
        return row.getX() + row.getWidth() * gainRange.convertTo0to1 (db);
    }

    void setGainFromX (float x)
    {
        const auto row = gainRowArea();
        const float unit = juce::jlimit (0.0f, 1.0f, (x - row.getX()) / row.getWidth());
        setGainDb (gainRange.convertFrom0to1 (unit));
    }

    void syncGain()
    {
        if (gainParam != nullptr)
            gainDb = gainRange.convertFrom0to1 (gainParam->getValue());
    }

    void paintGain (juce::Graphics& g) const
    {
        const auto row = gainRowArea();
        const float y = row.getCentreY();
        const float zeroX = gainToX (0.0f), thumbX = gainToX (gainDb);
        const bool changed = std::abs (gainDb) > 0.05f;

        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (row.getX(), y - 1.5f, row.getWidth(), 3.0f, 1.5f);

        // the filled part runs from 0 dB to the thumb, so it reads as a boost or a cut
        if (changed)
        {
            g.setColour (Theme::accent.withAlpha (0.55f));
            g.fillRect (juce::jmin (zeroX, thumbX), y - 1.5f, std::abs (thumbX - zeroX), 3.0f);
        }

        g.setColour (Theme::textLo);
        g.fillRect (zeroX - 0.5f, y - 4.5f, 1.0f, 9.0f);   // 0 dB notch

        juce::Path thumb;
        thumb.addEllipse (thumbX - 5.5f, y - 5.5f, 11.0f, 11.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 0, 1 }).drawForPath (g, thumb);
        g.setColour (juce::Colours::white);
        g.fillPath (thumb);

        g.setColour (changed ? Theme::textHi : Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        const auto text = juce::String (gainDb > 0.05f ? "+" : "") + juce::String (gainDb, 1) + " dB";
        g.drawText (text, juce::Rectangle<float> (row.getRight() + 10.0f, row.getY(), valueWidth - 10.0f, row.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    juce::String title;
    float shown = floorDb, peak = floorDb;
    int holdFrames = 0;

    juce::RangedAudioParameter* gainParam = nullptr;
    juce::NormalisableRange<float> gainRange;
    float gainDb = 0.0f;
    bool dragging = false;
};
