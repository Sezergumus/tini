#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** The tuner: a big note name, a cent strip and the frequency. It takes the place of the block page
    (BlockEditor::showOverlay). While it exists the processor copies the guitar input into the tuner
    FIFO; closing it (deleting it) switches that off again and lifts the mute. */
class TunerView : public juce::Component, private juce::Timer
{
public:
    explicit TunerView (NeuralFlowAmpAudioProcessor& p)
        : processor (p), muteSwitch (Theme::accent)
    {
        muteSwitch.setToggleState (false, juce::dontSendNotification);
        muteSwitch.onClick = [this] { processor.setTunerMute (muteSwitch.getToggleState()); };
        addAndMakeVisible (muteSwitch);

        processor.setTunerActive (true);
        startTimerHz (30);
    }

    ~TunerView() override
    {
        stopTimer();
        processor.setTunerActive (false);
    }

    // What the tuner shows right now (the tests read these).
    bool hasPitch() const         { return noteShown; }
    juce::String getNoteName() const { return noteShown ? note.name : juce::String(); }
    int getOctave() const         { return note.octave; }
    float getCents() const        { return shownCents; }
    float getFrequency() const    { return shownHz; }
    bool isInTune() const         { return noteShown && std::abs (shownCents) <= inTuneCents; }
    ToggleSwitch& getMuteSwitch() { return muteSwitch; }

    /** Reads what arrived in the FIFO and updates the display. Runs from the timer; public so tests can call it. */
    void update()
    {
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
        const int needed = PitchDetector::samplesNeeded (sr);

        if ((int) history.size() != needed)
        {
            history.assign ((size_t) needed, 0.0f);
            filled = 0;
        }

        // slide the newest samples into the window
        float chunk[2048];
        bool gotAny = false;
        for (int n; (n = processor.readTunerSamples (chunk, 2048)) > 0;)
        {
            gotAny = true;
            if (n >= needed)
            {
                std::copy (chunk + (n - needed), chunk + n, history.begin());
            }
            else
            {
                std::move (history.begin() + n, history.end(), history.begin());
                std::copy (chunk, chunk + n, history.end() - n);
            }
            filled = juce::jmin (needed, filled + n);
        }

        if (gotAny && filled >= needed)
            handle (detector.detect (history.data(), needed, sr));

        if (noteShown && ++framesSincePitch > holdFrames)
        {
            noteShown = false;   // the string stopped: show a dash
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (20.0f, true));
        g.drawText ("Tuner", titleArea, juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (13.0f));
        g.drawText ("A4 = " + juce::String (processor.getTunerReferenceHz(), 0) + " Hz", titleArea.withTrimmedLeft (74),
                    juce::Justification::centredLeft, false);

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (12.5f));
        g.drawText ("Mute output", muteLabel, juce::Justification::centredRight, false);

        const auto tint = isInTune() ? Theme::accent : Theme::textHi;

        // note name and octave
        if (noteShown)
        {
            const auto big = Theme::ui (128.0f, true);
            const auto name = note.name;
            const float nameW = big.getStringWidthFloat (name);
            const auto octave = juce::String (note.octave);
            const auto small = Theme::ui (34.0f);
            const float octaveW = small.getStringWidthFloat (octave);
            const float total = nameW + 6.0f + octaveW;
            const float x = (float) noteArea.getCentreX() - total * 0.5f;

            g.setColour (tint);
            g.setFont (big);
            g.drawText (name, juce::Rectangle<float> (x, (float) noteArea.getY(), nameW + 2.0f, (float) noteArea.getHeight()),
                        juce::Justification::centredLeft, false);
            g.setColour (isInTune() ? Theme::accent : Theme::textMid);
            g.setFont (small);
            g.drawText (octave, juce::Rectangle<float> (x + nameW + 6.0f, (float) noteArea.getY() + 22.0f, octaveW + 2.0f, 44.0f),
                        juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (96.0f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), noteArea, juce::Justification::centred, false);
        }

        // cent strip
        const auto strip = stripArea.toFloat();
        const float midX = strip.getCentreX();
        const float unit = strip.getWidth() / 100.0f;   // pixels per cent, -50..+50

        g.setColour (Theme::trackBg);
        g.fillRoundedRectangle (strip.withSizeKeepingCentre (strip.getWidth(), 4.0f), 2.0f);

        g.setColour (Theme::accent.withAlpha (0.28f));
        g.fillRoundedRectangle (juce::Rectangle<float> (midX - inTuneCents * unit, strip.getCentreY() - 2.0f, inTuneCents * 2.0f * unit, 4.0f), 2.0f);

        g.setFont (Theme::ui (11.0f));
        for (int c = -50; c <= 50; c += 25)
        {
            const float x = midX + (float) c * unit;
            g.setColour (c == 0 ? Theme::textMid : Theme::textLo);
            g.fillRect (x - 0.75f, strip.getCentreY() - (c == 0 ? 14.0f : 9.0f), 1.5f, c == 0 ? 28.0f : 18.0f);
            g.setColour (Theme::textLo);
            g.drawText (c == 0 ? "0" : (c > 0 ? "+" : "") + juce::String (c),
                        juce::Rectangle<float> (x - 20.0f, strip.getCentreY() + 18.0f, 40.0f, 14.0f), juce::Justification::centred, false);
        }

        if (noteShown)
        {
            const float x = midX + juce::jlimit (-50.0f, 50.0f, shownCents) * unit;
            g.setColour (tint);
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 36.0f).withCentre ({ x, strip.getCentreY() }), 1.5f);
        }

        // flat / sharp hints at the ends of the strip
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText ("Flat",  juce::Rectangle<float> (strip.getX() - 56.0f, strip.getCentreY() - 8.0f, 48.0f, 16.0f), juce::Justification::centredRight, false);
        g.drawText ("Sharp", juce::Rectangle<float> (strip.getRight() + 8.0f, strip.getCentreY() - 8.0f, 48.0f, 16.0f), juce::Justification::centredLeft, false);

        // readout
        g.setFont (Theme::ui (14.0f));
        if (noteShown)
        {
            g.setColour (isInTune() ? Theme::accent : Theme::textMid);
            g.drawText ((shownCents >= 0.0f ? "+" : "") + juce::String (shownCents, 1) + " cents   "
                            + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "   " + juce::String (shownHz, 1) + " Hz",
                        readoutArea, juce::Justification::centred, false);
        }
        else
        {
            g.setColour (Theme::textLo);
            g.drawText ("Play a string", readoutArea, juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (36);
        titleArea = top.removeFromLeft (300);

        muteSwitch.setBounds (top.removeFromRight (44).withSizeKeepingCentre (44, 28));
        top.removeFromRight (8);
        muteLabel = top.removeFromRight (100);

        noteArea = b.removeFromTop (b.getHeight() * 46 / 100);
        stripArea = b.removeFromTop (60).withSizeKeepingCentre (560, 60);
        b.removeFromTop (8);
        readoutArea = b.removeFromTop (28);
    }

private:
    static constexpr float inTuneCents = 3.0f;
    static constexpr int holdFrames = 45;   // ~1.5 s of showing the last note after the sound has stopped

    void timerCallback() override { update(); }

    void handle (const PitchDetector::Result& r)
    {
        if (! r.valid() || r.confidence < 0.7f)
            return;

        const auto described = PitchDetector::describe (r.frequency, processor.getTunerReferenceHz());
        framesSincePitch = 0;

        if (! noteShown || described.midi != note.midi)
        {
            // a new note: jump to it instead of sliding there from the old one
            shownCents = described.cents;
            shownHz = r.frequency;
        }
        else
        {
            shownCents += 0.4f * (described.cents - shownCents);
            shownHz    += 0.4f * (r.frequency - shownHz);
        }

        note = described;
        noteShown = true;
        repaint();
    }

    NeuralFlowAmpAudioProcessor& processor;
    PitchDetector detector;
    std::vector<float> history;
    int filled = 0;

    PitchDetector::Note note;
    bool noteShown = false;
    float shownCents = 0.0f, shownHz = 0.0f;
    int framesSincePitch = 0;

    ToggleSwitch muteSwitch;
    juce::Rectangle<int> titleArea, muteLabel, noteArea, stripArea, readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerView)
};
