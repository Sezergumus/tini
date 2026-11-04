#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** The bottom strip of the Rig: the input meter on the left, the output meter on the right and the
    Tuner button in the middle. Each meter also carries a gain line (input / output level in dB).
    Everything is centred on the strip's middle line. (The audio device text lives on the Settings page.) */
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
    }

    /** Called when the Tuner button was pressed; MainView opens or closes the tuner. */
    std::function<void()> onTunerClicked;

    void setTunerOpen (bool isOpen) { tunerButton.setToggleState (isOpen, juce::dontSendNotification); }

    /** Called from the UI timer: feeds the meters. */
    void refresh()
    {
        const float inDb  = juce::Decibels::gainToDecibels (processor.inputPeak.exchange (0.0f),  -60.0f);
        const float outDb = juce::Decibels::gainToDecibels (processor.outputPeak.exchange (0.0f), -60.0f);
        inMeter.pushLevel (inDb);
        outMeter.pushLevel (outDb);
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
    }

    juce::TextButton& getTunerButton() { return tunerButton; }

    LevelMeter inMeter, outMeter;

private:
    TiniAudioProcessor& processor;
    juce::TextButton tunerButton;
};
