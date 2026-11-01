#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** Common card with a title, optional chip and an optional right-aligned status. */
class PanelBase : public juce::Component
{
public:
    static constexpr int headerHeight = 54;

    PanelBase (const juce::String& panelTitle, const juce::String& chipText = {})
        : title (panelTitle), chip (chipText) {}

    void setStatus (const juce::String& text, bool showDot)
    {
        status = text;
        statusDot = showDot;
        repaint (getLocalBounds().removeFromTop (headerHeight));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawPanel (g, r);

        auto header = r.removeFromTop ((float) headerHeight).reduced (20.0f, 0.0f);

        // title
        const auto titleFont = Theme::ui (16.0f, true, 0.14f);
        const float titleW = titleFont.getStringWidthFloat (title);
        g.setColour (Theme::textHi);
        g.setFont (titleFont);
        g.drawText (title, header.removeFromLeft (titleW + 4.0f), juce::Justification::centredLeft, false);

        // chip
        if (chip.isNotEmpty())
        {
            header.removeFromLeft (10.0f);
            const auto chipFont = Theme::ui (9.5f, true, 0.16f);
            auto chipRect = header.removeFromLeft (chipFont.getStringWidthFloat (chip) + 18.0f)
                                  .withSizeKeepingCentre (chipFont.getStringWidthFloat (chip) + 18.0f, 20.0f);

            g.setColour (Theme::accent.withAlpha (0.10f));
            g.fillRoundedRectangle (chipRect, 4.0f);
            g.setColour (Theme::accent.withAlpha (0.45f));
            g.drawRoundedRectangle (chipRect.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Theme::accent);
            g.setFont (chipFont);
            g.drawText (chip, chipRect, juce::Justification::centred, false);
        }

        // status (right)
        if (status.isNotEmpty())
        {
            const auto statusFont = Theme::ui (10.5f, true, 0.18f);
            g.setFont (statusFont);
            g.setColour (Theme::textMid);

            auto right = header.removeFromRight (statusFont.getStringWidthFloat (status) + 4.0f);
            g.drawText (status, right, juce::Justification::centredRight, false);

            if (statusDot)
            {
                auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ right.getX() - 12.0f, right.getCentreY() });
                g.setColour (Theme::accent.withAlpha (0.25f));
                g.fillEllipse (dot.expanded (4.0f));
                g.setColour (Theme::accent);
                g.fillEllipse (dot);
            }
        }

        g.setColour (Theme::divider);
        g.fillRect (20.0f, (float) headerHeight, r.getWidth() - 40.0f, 1.0f);
    }

    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().withTrimmedTop (headerHeight).reduced (18, 16);
    }

private:
    juce::String title, chip, status;
    bool statusDot = false;
};

//==============================================================================
/** One stompbox: header, a grid of knobs, status LED and a footswitch bound to a bypass parameter. */
class PedalView : public juce::Component
{
public:
    PedalView (const juce::String& pedalName, const juce::String& subtitleText, juce::Colour colour,
               juce::AudioProcessorValueTreeState& state, const juce::String& activeParamID)
        : name (pedalName), subtitle (subtitleText), tint (colour), apvts (state), footSwitch (colour)
    {
        addAndMakeVisible (footSwitch);
        activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, activeParamID, footSwitch);
        lastActive = isActive();
    }

    void addKnob (const juce::String& label, const juce::String& paramID, int decimals, const juce::String& suffix = {})
    {
        auto* knob = knobs.add (new LabeledKnob (label, tint, decimals, suffix));
        knob->attach (apvts, paramID);
        knob->setMaxDiameter (68);
        addAndMakeVisible (knob);
    }

    bool isActive() const { return footSwitch.getToggleState(); }

    /** Called from the UI timer so host automation of the bypass state is reflected too. */
    bool refresh()
    {
        const bool now = isActive();
        if (now == lastActive)
            return false;

        lastActive = now;
        for (auto* k : knobs)
            k->setAlpha (now ? 1.0f : 0.55f);
        repaint();
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = isActive();
        const float radius = 12.0f;

        // body
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff1e2429) : juce::Colour (0xff181b1f), 0.0f, r.getY(),
                                                 on ? juce::Colour (0xff14181c) : juce::Colour (0xff121417), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);

        if (on)
        {
            // accent wash from the top edge
            g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.14f), 0.0f, r.getY(),
                                                     tint.withAlpha (0.0f), 0.0f, r.getY() + r.getHeight() * 0.5f, false));
            g.fillRoundedRectangle (r, radius);

            g.setColour (tint.withAlpha (0.07f));
            g.drawRoundedRectangle (r.expanded (1.5f), radius + 1.5f, 3.0f);
        }

        g.setColour (on ? tint.withAlpha (0.60f) : Theme::panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

        // header
        auto header = r.removeFromTop (60.0f);
        g.setColour (on ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.5f, true, 0.20f));
        g.drawText (name, header.withTrimmedTop (14.0f).removeFromTop (20.0f), juce::Justification::centred, false);

        g.setColour (on ? tint.withAlpha (0.85f) : Theme::textLo);
        g.setFont (Theme::ui (9.0f, true, 0.22f));
        g.drawText (subtitle, header.withTrimmedTop (34.0f).removeFromTop (14.0f), juce::Justification::centred, false);

        g.setColour (Theme::divider);
        g.fillRect (r.getX() + 20.0f, header.getBottom(), r.getWidth() - 40.0f, 1.0f);

        // LED above the footswitch
        auto led = juce::Rectangle<float> (8.0f, 8.0f).withCentre (ledCentre);
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.45f), led.getCentreX(), led.getCentreY(),
                                                     tint.withAlpha (0.0f), led.getCentreX() + 14.0f, led.getCentreY(), true));
            g.fillEllipse (led.expanded (14.0f));
        }
        g.setColour (on ? tint.brighter (0.4f) : juce::Colour (0xff2a2f35));
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (led, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10, 0);
        b.removeFromTop (66);

        auto footArea = b.removeFromBottom (112);
        ledCentre = juce::Point<float> ((float) footArea.getCentreX(), (float) footArea.getY() + 14.0f);
        footSwitch.setBounds (juce::Rectangle<int> (60, 60).withCentre ({ footArea.getCentreX(), footArea.getY() + 64 }));

        // knob grid: two per row, last row centred
        const int n = knobs.size();
        if (n == 0)
            return;

        const int rows = (n + 1) / 2;
        const int rowH = b.getHeight() / rows;
        const int cellW = b.getWidth() / 2;

        for (int i = 0; i < n; ++i)
        {
            const int row = i / 2;
            const int inRow = juce::jmin (2, n - row * 2);
            const int col = i % 2;

            const int rowW = inRow * cellW;
            const int x0 = b.getX() + (b.getWidth() - rowW) / 2 + col * cellW;
            knobs[i]->setBounds (x0, b.getY() + row * rowH, cellW, rowH);
        }
    }

private:
    juce::String name, subtitle;
    juce::Colour tint;
    juce::AudioProcessorValueTreeState& apvts;

    FootSwitch footSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;
    juce::OwnedArray<LabeledKnob> knobs;

    juce::Point<float> ledCentre;
    bool lastActive = true;
};

//==============================================================================
class LeadChannelPanel : public PanelBase
{
public:
    explicit LeadChannelPanel (juce::AudioProcessorValueTreeState& apvts)
        : PanelBase ("LEAD CHANNEL", "ANALOG MODEL"),
          drive  ("DRIVE",  Theme::amber,  1),
          low    ("LOW",    Theme::accent, 1),
          mid    ("MID",    Theme::accent, 1),
          high   ("HIGH",   Theme::accent, 1),
          master ("MASTER", Theme::accent, 1)
    {
        setStatus ("ACTIVE", true);

        addAndMakeVisible (scope);

        drive.attach  (apvts, "DRIVE");
        low.attach    (apvts, "LOW");
        mid.attach    (apvts, "MID");
        high.attach   (apvts, "HIGH");
        master.attach (apvts, "MASTER");

        for (auto* k : { &drive, &low, &mid, &high, &master })
            addAndMakeVisible (k);
    }

    void paint (juce::Graphics& g) override
    {
        PanelBase::paint (g);
        Theme::drawInset (g, knobArea.toFloat(), 8.0f);
    }

    void resized() override
    {
        auto b = getContentBounds();

        knobArea = b.removeFromRight (juce::roundToInt ((float) b.getWidth() * 0.50f));
        b.removeFromRight (16);
        scope.setBounds (b);

        auto inner = knobArea.reduced (10, 12);
        const int w = inner.getWidth() / 5;
        for (auto* k : { &drive, &low, &mid, &high, &master })
            k->setBounds (inner.removeFromLeft (w));
    }

    WaveformView scope;

private:
    LabeledKnob drive, low, mid, high, master;
    juce::Rectangle<int> knobArea;
};

//==============================================================================
class SignalChainPanel : public PanelBase
{
public:
    explicit SignalChainPanel (juce::AudioProcessorValueTreeState& apvts)
        : PanelBase ("SIGNAL CHAIN"),
          compressor ("COMPRESSOR", "STUDIO FET", Theme::accent, apvts, "COMP_ON"),
          overdrive  ("OVERDRIVE",  "TS-MOD CIRCUIT", Theme::amber, apvts, "OD_ON")
    {
        compressor.addKnob ("SUSTAIN", "COMP_SUST", 0);
        compressor.addKnob ("ATTACK",  "COMP_ATT",  0, " ms");
        compressor.addKnob ("BLEND",   "COMP_MIX",  0, " %");
        compressor.addKnob ("LEVEL",   "COMP_LVL",  1, " dB");

        overdrive.addKnob ("GAIN",  "OD_GAIN", 1);
        overdrive.addKnob ("TONE",  "OD_TONE", 1);
        overdrive.addKnob ("LEVEL", "OD_LVL",  1);

        addAndMakeVisible (compressor);
        addAndMakeVisible (overdrive);
        refreshStatus();
    }

    void refresh()
    {
        const bool a = compressor.refresh();
        const bool b = overdrive.refresh();
        if (a || b)
        {
            refreshStatus();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        PanelBase::paint (g);

        // cable between the pedals
        const float y = (float) compressor.getBounds().getCentreY();
        const float x0 = (float) compressor.getRight();
        const float x1 = (float) overdrive.getX();

        g.setColour (juce::Colour (0xff2e353d));
        g.fillRect (x0, y - 1.0f, x1 - x0, 2.0f);

        juce::Path chevron;
        const float cx = (x0 + x1) * 0.5f;
        chevron.startNewSubPath (cx - 3.0f, y - 5.0f);
        chevron.lineTo (cx + 3.0f, y);
        chevron.lineTo (cx - 3.0f, y + 5.0f);
        g.setColour (Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void resized() override
    {
        auto b = getContentBounds();
        const int gap = 36;
        const int w = (b.getWidth() - gap) / 2;
        compressor.setBounds (b.removeFromLeft (w));
        b.removeFromLeft (gap);
        overdrive.setBounds (b);
    }

private:
    void refreshStatus()
    {
        const int active = (compressor.isActive() ? 1 : 0) + (overdrive.isActive() ? 1 : 0);
        setStatus (juce::String (active) + " / 2 ACTIVE", false);
    }

    PedalView compressor, overdrive;
};

//==============================================================================
class CabPanel : public PanelBase
{
public:
    explicit CabPanel (NeuralFlowAmpAudioProcessor& p)
        : PanelBase ("CAB SIM", "IR CONVOLUTION"),
          processor (p),
          lowCut  ("LOW CUT",  "20 Hz", "300 Hz",
                   [] (double v) { return juce::String (juce::roundToInt (v)) + " Hz"; }),
          highCut ("HIGH CUT", "4 kHz",  "20 kHz",
                   [] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; })
    {
        addAndMakeVisible (cab);

        loadButton.setButtonText ("LOAD IR");
        loadButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible (loadButton);

        lowCut.attach  (processor.apvts, "CAB_LOWCUT");
        highCut.attach (processor.apvts, "CAB_HIGHCUT");
        addAndMakeVisible (lowCut);
        addAndMakeVisible (highCut);

        if (processor.loadedIrFile.existsAsFile())
            showFile (processor.loadedIrFile);
    }

    void setLevel (float unit) { cab.setLevel (unit); }

    void paint (juce::Graphics& g) override
    {
        PanelBase::paint (g);

        // IR slot card
        Theme::drawInset (g, irCard.toFloat(), 8.0f);
        auto card = irCard.reduced (14, 10);

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (10.5f, true, 0.14f));
        g.drawText ("IMPULSE RESPONSE", card.removeFromTop (16), juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::mono (10.5f));
        g.drawText (irInfo, irCard.reduced (14, 10).removeFromTop (16), juce::Justification::centredRight, false);

        const bool loaded = processor.loadedIrFile.existsAsFile();
        g.setColour (loaded ? Theme::textHi : Theme::textLo);
        g.setFont (Theme::mono (12.5f));
        g.drawText (irName, card.withTrimmedRight (92).withTrimmedTop (4), juce::Justification::centredLeft, true);

        // filter card
        Theme::drawInset (g, filterCard.toFloat(), 8.0f);
    }

    void resized() override
    {
        auto b = getContentBounds();

        filterCard = b.removeFromBottom (130);
        b.removeFromBottom (12);
        irCard = b.removeFromBottom (82);
        b.removeFromBottom (12);
        cab.setBounds (b);

        auto f = filterCard.reduced (16, 14);
        lowCut.setBounds  (f.removeFromTop (46));
        f.removeFromTop (utility_gap);
        highCut.setBounds (f.removeFromTop (46));

        loadButton.setBounds (juce::Rectangle<int> (80, 28)
                                  .withRightX (irCard.getRight() - 14)
                                  .withY (irCard.getBottom() - 10 - 28));
    }

private:
    static constexpr int utility_gap = 6;

    void chooseFile()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Select an impulse response (.wav)",
                                                           processor.loadedIrFile.existsAsFile()
                                                               ? processor.loadedIrFile.getParentDirectory()
                                                               : juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                                                           "*.wav;*.aif;*.aiff");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<CabPanel> (this)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || ! file.existsAsFile())
                return;

            safe->processor.loadImpulseResponse (file);
            safe->showFile (file);
        });
    }

    void showFile (const juce::File& file)
    {
        irName = file.getFileName();
        irInfo = {};

        if (std::unique_ptr<juce::AudioFormatReader> reader { processor.formatManager.createReaderFor (file) })
        {
            const double ms = 1000.0 * (double) reader->lengthInSamples / reader->sampleRate;
            irInfo = juce::String (reader->sampleRate / 1000.0, 1) + " kHz  |  " + juce::String (juce::roundToInt (ms)) + " ms";
        }

        cab.setIrLoaded (true);
        repaint();
    }

    NeuralFlowAmpAudioProcessor& processor;

    CabView cab;
    FilterSlider lowCut, highCut;
    juce::TextButton loadButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String irName { "No IR loaded" }, irInfo;
    juce::Rectangle<int> irCard, filterCard;
};
