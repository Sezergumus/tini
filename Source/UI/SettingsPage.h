#pragma once

#include "../PluginProcessor.h"
#include "../StandaloneHooks.h"
#include "Theme.h"

//==============================================================================
/** The Settings page (the gear in the header): audio device in the standalone, the tuner's reference
    pitch, and the version. It takes the place of the block page until a block is clicked. */
class SettingsPage : public juce::Component
{
public:
    explicit SettingsPage (NeuralFlowAmpAudioProcessor& p) : processor (p)
    {
        // Audio: the standalone owns the device; inside a DAW the host does.
        if (auto* manager = StandaloneHooks::controller().deviceManager ? StandaloneHooks::controller().deviceManager() : nullptr)
        {
            audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (*manager, 0, 2, 0, 2,
                                                                                  false, false, true, false);
            audioSelector->setItemHeight (28);

            // the selector sizes itself to its content, which can be taller than the page: scroll it
            audioViewport.setViewedComponent (audioSelector.get(), false);
            audioViewport.setScrollBarsShown (true, false);
            audioViewport.setScrollBarThickness (8);
            addAndMakeVisible (audioViewport);
        }

        referenceSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        referenceSlider.setRange (415.0, 466.0, 1.0);
        referenceSlider.setDoubleClickReturnValue (true, 440.0);
        referenceSlider.setValue (processor.getTunerReferenceHz(), juce::dontSendNotification);
        referenceSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
        referenceSlider.setTextValueSuffix (" Hz");
        referenceSlider.setColour (juce::Slider::textBoxTextColourId, Theme::textHi);
        referenceSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        referenceSlider.onValueChange = [this] { processor.setTunerReferenceHz ((float) referenceSlider.getValue()); };
        addAndMakeVisible (referenceSlider);
    }

    bool hasAudioSelector() const { return audioSelector != nullptr; }
    juce::Slider& getReferenceSlider() { return referenceSlider; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (20.0f, true));
        g.drawText ("Settings", titleArea, juce::Justification::centredLeft, false);

        auto heading = [&] (const juce::String& text, juce::Rectangle<int> area)
        {
            g.setColour (Theme::textMid);
            g.setFont (Theme::ui (13.0f, true));
            g.drawText (text, area, juce::Justification::centredLeft, false);
        };

        heading ("Audio", audioHeading);
        if (audioSelector == nullptr)
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.5f));
            g.drawText ("The host application manages the audio device.", audioNote, juce::Justification::topLeft, true);
        }

        heading ("Tuner", tunerHeading);
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText ("Reference pitch (A4)", referenceLabel, juce::Justification::centredLeft, false);

        heading ("About", aboutHeading);
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("t\xc4\xb1n\xc4\xb1")) + " " + JucePlugin_VersionString, aboutLines[0],
                    juce::Justification::centredLeft, false);
        g.drawText ("Typeface: Inter (SIL Open Font License 1.1)", aboutLines[1], juce::Justification::centredLeft, false);
        g.drawText ("Neural Amp Modeler core (MIT License)", aboutLines[2], juce::Justification::centredLeft, false);

        g.setColour (Theme::divider);
        g.fillRect (dividerX, audioHeading.getY(), 1, getHeight() - audioHeading.getY());
    }

    void resized() override
    {
        auto b = getLocalBounds();
        titleArea = b.removeFromTop (36);
        b.removeFromTop (8);

        auto left = b.removeFromLeft (b.getWidth() * 60 / 100);
        dividerX = left.getRight() + 20;
        auto right = b.withTrimmedLeft (40);

        audioHeading = left.removeFromTop (24);
        left.removeFromTop (6);
        if (audioSelector == nullptr)
        {
            audioNote = left.removeFromTop (40);
        }
        else
        {
            audioViewport.setBounds (left.withTrimmedRight (12));
            audioSelector->setSize (audioViewport.getMaximumVisibleWidth(), 100);   // it sets its own height
        }

        tunerHeading = right.removeFromTop (24);
        right.removeFromTop (6);
        referenceLabel = right.removeFromTop (20);
        referenceSlider.setBounds (right.removeFromTop (28));
        right.removeFromTop (28);

        aboutHeading = right.removeFromTop (24);
        right.removeFromTop (6);
        for (auto& line : aboutLines)
            line = right.removeFromTop (20);
    }

private:
    NeuralFlowAmpAudioProcessor& processor;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
    juce::Viewport audioViewport;
    juce::Slider referenceSlider;

    juce::Rectangle<int> titleArea, audioHeading, audioNote, tunerHeading, referenceLabel, aboutHeading;
    std::array<juce::Rectangle<int>, 3> aboutLines;
    int dividerX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPage)
};
