#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TiniAudioProcessorEditor::TiniAudioProcessorEditor (TiniAudioProcessor& p)
    : AudioProcessorEditor (&p), view (p)
{
    addAndMakeVisible (view);

    constrainer.setFixedAspectRatio ((double) Theme::designWidth / (double) Theme::designHeight);
    constrainer.setMinimumSize (Theme::designWidth * 3 / 4, Theme::designHeight * 3 / 4);
    constrainer.setMaximumSize (Theme::designWidth * 2, Theme::designHeight * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);

    setSize (Theme::designWidth, Theme::designHeight);
}

void TiniAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) Theme::designWidth;

    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, Theme::designWidth, Theme::designHeight);
}
