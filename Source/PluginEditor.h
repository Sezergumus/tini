#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/MainView.h"

//==============================================================================
/** Thin host window: it owns a MainView that is drawn in a fixed design space
    and simply scales it to the current window size (aspect ratio is locked). */
class TiniAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TiniAudioProcessorEditor (TiniAudioProcessor&);
    ~TiniAudioProcessorEditor() override = default;

    void resized() override;

private:
    MainView view;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TiniAudioProcessorEditor)
};
