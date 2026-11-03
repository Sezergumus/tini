#pragma once

#include "../PluginProcessor.h"
#include "../LatencyProbe.h"
#include "NeuralLookAndFeel.h"
#include "Panels.h"
#include "PresetBar.h"

/** The whole UI, laid out in a fixed design space (Theme::designWidth x designHeight).
    The editor scales this component to whatever window size the user picks. */
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (NeuralFlowAmpAudioProcessor& p)
        : processor (p),
          inputMeter ("INPUT"), outputMeter ("OUTPUT"),
          presetBar (p), lead (p), chain (p), cabPanel (p)
    {
        setLookAndFeel (&laf);

        addAndMakeVisible (inputMeter);
        addAndMakeVisible (outputMeter);
        addAndMakeVisible (presetBar);
        addAndMakeVisible (lead);
        addAndMakeVisible (chain);
        addAndMakeVisible (cabPanel);

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
        auto r = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (Theme::bgTop, 0.0f, 0.0f, Theme::bgBottom, 0.0f, r.getBottom(), false));
        g.fillAll();

        paintHeader (g, r.removeFromTop ((float) headerH));
        paintFooter (g, r.removeFromBottom ((float) footerH));
    }

    void resized() override
    {
        presetBar.setBounds (juce::Rectangle<int> (PresetBar::width, PresetBar::height)
                                 .withCentre ({ getWidth() / 2, headerH / 2 }));

        auto b = getLocalBounds();
        b.removeFromTop (headerH);
        b.removeFromBottom (footerH);

        auto meters = b.reduced (12, 6);
        inputMeter.setBounds  (meters.removeFromLeft (40));
        outputMeter.setBounds (meters.removeFromRight (40));

        auto content = b.withTrimmedLeft (64).withTrimmedRight (64).withTrimmedTop (6).withTrimmedBottom (8);

        lead.setBounds (content.removeFromTop (262));
        content.removeFromTop (16);

        const int chainW = juce::roundToInt ((float) (content.getWidth() - 16) * 0.58f);
        chain.setBounds (content.removeFromLeft (chainW));
        content.removeFromLeft (16);
        cabPanel.setBounds (content);
    }

private:
    static constexpr int headerH = 58;
    static constexpr int footerH = 38;

    //==========================================================================
    void paintHeader (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Theme::divider);
        g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);

        // product name (text only, no logo mark)
        g.setFont (Theme::ui (24.0f, true));
        g.setColour (Theme::textHi);
        g.drawText (juce::String (juce::CharPointer_UTF8 ("t\xc4\xb1n\xc4\xb1")),   // "tını"
                    juce::Rectangle<float> (26.0f, r.getY(), 200.0f, r.getHeight()), juce::Justification::centredLeft, false);

        // audio engine chip
        const auto chipFont = Theme::mono (11.0f);
        const float cw = chipFont.getStringWidthFloat (audioInfo) + 36.0f;
        auto chip = juce::Rectangle<float> (cw, 28.0f).withCentre ({ r.getRight() - 26.0f - cw * 0.5f, r.getCentreY() });
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (chip, 6.0f);
        g.setColour (Theme::panelBorder);
        g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (Theme::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ chip.getX() + 14.0f, chip.getCentreY() }));
        g.setColour (Theme::textMid);
        g.setFont (chipFont);
        g.drawText (audioInfo, chip.withTrimmedLeft (26.0f), juce::Justification::centredLeft, false);
    }

    void paintFooter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Theme::divider);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);

        r = r.reduced (26.0f, 0.0f);
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (10.0f, true, 0.18f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xa9 2026 TINI")), r, juce::Justification::centredLeft, false);
        g.drawText ("v" + juce::String (JucePlugin_VersionString), r, juce::Justification::centredRight, false);
    }

    //==========================================================================
    void timerCallback() override
    {
        // oscilloscope
        if (processor.isNextFrameReady.load())
        {
            processor.isNextFrameReady.store (false);
            scopeCopy = processor.scopeData;
            lead.scope.setData (scopeCopy.data(), (int) scopeCopy.size());
        }

        // meters
        const float inDb  = juce::Decibels::gainToDecibels (processor.inputPeak.exchange (0.0f),  -60.0f);
        const float outDb = juce::Decibels::gainToDecibels (processor.outputPeak.exchange (0.0f), -60.0f);
        inputMeter.pushLevel (inDb);
        outputMeter.pushLevel (outDb);
        cabPanel.setLevel (Theme::dbToUnit (outDb, -48.0f));

        presetBar.refresh();
        lead.refresh();
        chain.refresh();

        if (++frame % 30 == 0)
            updateAudioInfo();
    }

    void updateAudioInfo()
    {
        const double sr = processor.getSampleRate();
        const int block = processor.getBlockSize();

        // The old readout was just one buffer (block / sampleRate), which is far below what you
        // actually hear. In the standalone we can ask the device for its real in + out latency;
        // inside a DAW only the buffer size is known, so no millisecond figure is shown.
        const auto device = LatencyProbe::query();
        const double roundTrip = LatencyProbe::roundTripMs (device, processor.getLatencySamples());

        juce::String text = "AUDIO  " + juce::String (sr / 1000.0, 1) + " kHz";
        if (block > 0 && sr > 0.0)
            text << "  |  " << block << " smp";

        if (device.valid && device.driverName.isNotEmpty())
            text << "  |  " << device.driverName;

        if (roundTrip >= 0.0)
            text << "  |  " << juce::String::charToString (0x2248) << " " << juce::String (roundTrip, 1) << " ms";

        if (text != audioInfo)
        {
            audioInfo = text;
            repaint (0, 0, getWidth(), headerH);
        }
    }

    //==========================================================================
    NeuralFlowAmpAudioProcessor& processor;
    NeuralLookAndFeel laf;

    LevelMeter inputMeter, outputMeter;
    PresetBar presetBar;
    LeadChannelPanel lead;
    SignalChainPanel chain;
    CabPanel cabPanel;

    std::array<float, NeuralFlowAmpAudioProcessor::scopeSize> scopeCopy {};
    juce::String audioInfo;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
