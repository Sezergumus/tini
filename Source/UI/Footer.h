#pragma once

#include "../PluginProcessor.h"
#include "../LatencyProbe.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** The bottom strip of the Rig: the input meter on the left, the output meter on the right, and in
    the middle the Tuner button, and the audio device in plain text between the In meter and the
    Tuner. Each meter also carries a gain line (input / output level in dB). Everything is centred
    on the strip's middle line. */
class Footer : public juce::Component
{
public:
    static constexpr int height = 60;

    explicit Footer (TiniAudioProcessor& p)
        : processor (p), inMeter ("In"), outMeter ("Out")
    {
        inMeter.attachGain  (processor.apvts, "IN_GAIN");
        outMeter.attachGain (processor.apvts, "OUT_GAIN");
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);

        tunerButton.setButtonText ("Tuner");
        tunerButton.setClickingTogglesState (false);
        tunerButton.onClick = [this] { if (onTunerClicked) onTunerClicked(); };
        addAndMakeVisible (tunerButton);

        updateAudioInfo();
    }

    /** Called when the Tuner button was pressed; MainView opens or closes the tuner. */
    std::function<void()> onTunerClicked;

    void setTunerOpen (bool isOpen) { tunerButton.setToggleState (isOpen, juce::dontSendNotification); }

    /** Called from the UI timer: feeds the meters and keeps the device text current. */
    void refresh()
    {
        const float inDb  = juce::Decibels::gainToDecibels (processor.inputPeak.exchange (0.0f),  -60.0f);
        const float outDb = juce::Decibels::gainToDecibels (processor.outputPeak.exchange (0.0f), -60.0f);
        inMeter.pushLevel (inDb);
        outMeter.pushLevel (outDb);

        if (++frame % 30 == 0)
            updateAudioInfo();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText (audioInfo, infoArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto b = getLocalBounds();

        const int meterW = juce::jlimit (160, 240, b.getWidth() / 5);
        auto left  = b.removeFromLeft (meterW);
        auto right = b.removeFromRight (meterW);
        inMeter.setBounds (left);
        outMeter.setBounds (right);

        // The Tuner button sits on the middle line of the strip, whatever the meters take.
        tunerButton.setBounds (juce::Rectangle<int> (76, 28).withCentre (getLocalBounds().getCentre()));

        // The device text uses the space between the In meter and the Tuner button.
        infoArea = juce::Rectangle<int> (left.getRight() + 24, 0,
                                         tunerButton.getX() - left.getRight() - 48, getHeight()).toFloat();
    }

    const juce::String& getAudioInfo() const { return audioInfo; }
    juce::TextButton& getTunerButton() { return tunerButton; }

    LevelMeter inMeter, outMeter;

private:
    void updateAudioInfo()
    {
        const double sr = processor.getSampleRate();
        const int block = processor.getBlockSize();

        // In the standalone we can ask the device for its real in + out latency; inside a DAW only
        // the buffer size is known, so no millisecond figure is shown.
        const auto device = LatencyProbe::query();
        const double roundTrip = LatencyProbe::roundTripMs (device, processor.getLatencySamples());

        juce::StringArray parts;
        if (device.valid && device.driverName.isNotEmpty())
            parts.add (device.driverName);

        parts.add (juce::String (sr / 1000.0, 1) + " kHz");

        if (block > 0 && sr > 0.0)
            parts.add (juce::String (block) + " smp");

        if (roundTrip >= 0.0)
            parts.add (juce::String::charToString (0x2248) + " " + juce::String (roundTrip, 1) + " ms");

        const auto text = parts.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));

        if (text != audioInfo)
        {
            audioInfo = text;
            repaint();
        }
    }

    TiniAudioProcessor& processor;
    juce::TextButton tunerButton;
    juce::Rectangle<float> infoArea;
    juce::String audioInfo;
    int frame = 0;
};
