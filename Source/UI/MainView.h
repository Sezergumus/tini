#pragma once

#include "../PluginProcessor.h"
#include "../LatencyProbe.h"
#include "NeuralLookAndFeel.h"
#include "Panels.h"
#include "BlockPages.h"
#include "PresetBar.h"

/** The whole UI, laid out in a fixed design space (Theme::designWidth x designHeight).
    The editor scales this component to whatever window size the user picks.

    Rig: a header (name, presets, audio device), the chain strip with the in / out meters, and the
    page of the block that is selected in the strip. */
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (NeuralFlowAmpAudioProcessor& p)
        : processor (p), presetBar (p), chain (p), blocks (p)
    {
        setLookAndFeel (&laf);

        addAndMakeVisible (presetBar);
        addAndMakeVisible (chain);
        addAndMakeVisible (blocks);

        chain.onSelect = [this] (int block) { blocks.show (block); };
        blocks.show (chain.getSelected());

        updateAudioInfo();
        startTimerHz (60);
    }

    ~MainView() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        paintHeader (g, getLocalBounds().toFloat().removeFromTop ((float) headerH));
    }

    void resized() override
    {
        presetBar.setBounds (juce::Rectangle<int> (PresetBar::width, PresetBar::height)
                                 .withCentre ({ getWidth() / 2, headerH / 2 }));

        auto b = getLocalBounds();
        b.removeFromTop (headerH);

        chain.setBounds (b.removeFromTop (chainH).reduced (margin, 0));
        blocks.setBounds (b.reduced (margin, 0).withTrimmedBottom (margin).withTrimmedTop (4));
    }

    ChainBar& getChain() { return chain; }
    BlockEditor& getBlocks() { return blocks; }

private:
    static constexpr int headerH = 56;
    static constexpr int chainH  = 132;
    static constexpr int margin  = 24;

    //==========================================================================
    void paintHeader (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // product name (text only, no logo mark)
        g.setFont (Theme::ui (22.0f, true));
        g.setColour (Theme::textHi);
        g.drawText (juce::String (juce::CharPointer_UTF8 ("t\xc4\xb1n\xc4\xb1")),   // "tını"
                    juce::Rectangle<float> ((float) margin + 2.0f, r.getY(), 200.0f, r.getHeight()), juce::Justification::centredLeft, false);

        // audio device, plain text
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText (audioInfo, r.withTrimmedRight ((float) margin).withTrimmedLeft (r.getWidth() * 0.62f),
                    juce::Justification::centredRight, true);
    }

    //==========================================================================
    void timerCallback() override
    {
        presetBar.refresh();
        chain.refresh();
        blocks.refresh();

        if (++frame % 30 == 0)
            updateAudioInfo();
    }

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
            repaint (0, 0, getWidth(), headerH);
        }
    }

    //==========================================================================
    NeuralFlowAmpAudioProcessor& processor;
    NeuralLookAndFeel laf;

    PresetBar presetBar;
    ChainBar chain;
    BlockEditor blocks;

    juce::String audioInfo;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
