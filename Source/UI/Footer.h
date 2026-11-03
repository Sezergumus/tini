#pragma once

#include "../PluginProcessor.h"
#include "../LatencyProbe.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** The bottom strip of the Rig: the input meter on the left, the output meter on the right, and in
    the middle the Tuner button with the audio device in plain text. */
class Footer : public juce::Component
{
public:
    static constexpr int height = 52;

    explicit Footer (NeuralFlowAmpAudioProcessor& p)
        : processor (p), inMeter ("In"), outMeter ("Out")
    {
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
        auto b = getLocalBounds().reduced (0, 6);

        inMeter.setBounds (b.removeFromLeft (meterWidth));
        outMeter.setBounds (b.removeFromRight (meterWidth));
        b.reduce (24, 0);

        tunerButton.setBounds (b.removeFromLeft (76).withSizeKeepingCentre (76, 28));
        b.removeFromLeft (16);
        infoArea = b.toFloat();
    }

    const juce::String& getAudioInfo() const { return audioInfo; }
    juce::TextButton& getTunerButton() { return tunerButton; }

    LevelMeter inMeter, outMeter;

private:
    static constexpr int meterWidth = 300;

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

    NeuralFlowAmpAudioProcessor& processor;
    juce::TextButton tunerButton;
    juce::Rectangle<float> infoArea;
    juce::String audioInfo;
    int frame = 0;
};
