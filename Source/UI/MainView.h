#pragma once

#include "../PluginProcessor.h"
#include "NeuralLookAndFeel.h"
#include "Panels.h"
#include "Footer.h"
#include "BlockPages.h"
#include "PresetBar.h"

/** The whole UI, laid out in a fixed design space (Theme::designWidth x designHeight).
    The editor scales this component to whatever window size the user picks.

    Rig: a header (name, presets), the chain strip, the page of the block that is selected in the
    strip, and a footer with the in / out meters and the audio device. */
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (NeuralFlowAmpAudioProcessor& p)
        : processor (p), presetBar (p), chain (p), blocks (p), footer (p)
    {
        setLookAndFeel (&laf);

        addAndMakeVisible (presetBar);
        addAndMakeVisible (chain);
        addAndMakeVisible (blocks);
        addAndMakeVisible (footer);

        chain.onSelect = [this] (int block) { blocks.show (block); };
        blocks.show (chain.getSelected());

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

        footer.setBounds (b.removeFromBottom (footerH).reduced (margin, 0));
        chain.setBounds (b.removeFromTop (chainH).reduced (margin, 0));
        blocks.setBounds (b.reduced (margin, 0).withTrimmedBottom (8).withTrimmedTop (4));
    }

    ChainBar& getChain() { return chain; }
    Footer& getFooter() { return footer; }
    BlockEditor& getBlocks() { return blocks; }

private:
    static constexpr int headerH = 56;
    static constexpr int chainH  = 176;
    static constexpr int footerH = Footer::height;
    static constexpr int margin  = 24;

    //==========================================================================
    void paintHeader (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // product name (text only, no logo mark)
        g.setFont (Theme::ui (22.0f, true));
        g.setColour (Theme::textHi);
        g.drawText (juce::String (juce::CharPointer_UTF8 ("t\xc4\xb1n\xc4\xb1")),   // "tını"
                    juce::Rectangle<float> ((float) margin + 2.0f, r.getY(), 200.0f, r.getHeight()), juce::Justification::centredLeft, false);
    }

    //==========================================================================
    void timerCallback() override
    {
        presetBar.refresh();
        chain.refresh();
        blocks.refresh();
        footer.refresh();
    }

    //==========================================================================
    NeuralFlowAmpAudioProcessor& processor;
    NeuralLookAndFeel laf;

    PresetBar presetBar;
    ChainBar chain;
    BlockEditor blocks;
    Footer footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
