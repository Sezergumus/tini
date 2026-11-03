#pragma once

#include "../PluginProcessor.h"
#include "../PedalCatalog.h"
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

    void setChip (const juce::String& text)
    {
        chip = text;
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
        header.removeFromRight ((float) headerRightInset);
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

protected:
    /** Space kept free at the right end of the header (e.g. for a button). */
    int headerRightInset = 0;

private:
    juce::String title, chip, status;
    bool statusDot = false;
};

//==============================================================================
/** Small round "x" that removes a pedal from the chain. */
class RemoveButton : public juce::Button
{
public:
    RemoveButton() : juce::Button ("remove")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Remove pedal");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);

        if (highlighted || down)
        {
            g.setColour (Theme::danger.withAlpha (down ? 0.35f : 0.20f));
            g.fillEllipse (r);
        }

        auto c = r.getCentre();
        const float k = r.getWidth() * 0.22f;
        juce::Path x;
        x.startNewSubPath (c.x - k, c.y - k);  x.lineTo (c.x + k, c.y + k);
        x.startNewSubPath (c.x + k, c.y - k);  x.lineTo (c.x - k, c.y + k);

        g.setColour (highlighted ? Theme::danger : Theme::textLo);
        g.strokePath (x, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
/** Invisible strip over a pedal's header: shows the grab cursor and six dots, and lets the
    ChainStrip pick up mouse events to drag the pedal around. */
class DragGrip : public juce::Component
{
public:
    DragGrip() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (hover ? Theme::textMid : Theme::textLo.withAlpha (0.8f));
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 3; ++row)
                g.fillEllipse (14.0f + (float) col * 5.0f, 17.0f + (float) row * 5.0f, 2.4f, 2.4f);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false;
};

//==============================================================================
/** One stompbox: header, a grid of knobs, status LED and a footswitch bound to a bypass parameter.
    Everything (name, colour, knobs, parameters) comes from a PedalCatalog entry. */
class PedalView : public juce::Component
{
public:
    static constexpr int width = 196;
    static constexpr int headerHeight = 60;

    /** `chainItem` is a pedal item number (see PedalCatalog): the pedal type plus which copy of it. */
    PedalView (int chainItem, juce::AudioProcessorValueTreeState& state)
        : item (chainItem), info (PedalCatalog::pedalOf (chainItem)), title (PedalCatalog::displayName (chainItem)),
          tint (PedalCatalog::pedalOf (chainItem).colour), apvts (state), footSwitch (tint)
    {
        addAndMakeVisible (grip);
        addAndMakeVisible (footSwitch);
        activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, PedalCatalog::onId (item), footSwitch);
        lastActive = isActive();

        for (size_t i = 0; i < info.knobs.size(); ++i)
        {
            const auto& k = info.knobs[i];
            auto* knob = knobs.add (new LabeledKnob (k.label, tint, k.decimals, k.suffix));
            knob->attach (apvts, PedalCatalog::knobId (item, (int) i));
            knob->setMaxDiameter (60);
            addAndMakeVisible (knob);
        }

        if (PedalCatalog::isRemovable (item))
        {
            removeButton.onClick = [this] { if (onRemove) onRemove(); };
            addAndMakeVisible (removeButton);
        }
    }

    const PedalCatalog::Pedal& getInfo() const { return info; }
    int getItem() const { return item; }
    bool isActive() const { return footSwitch.getToggleState(); }
    DragGrip& getGrip() { return grip; }

    std::function<void()> onRemove;

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
        auto header = r.removeFromTop ((float) headerHeight);
        g.setColour (on ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (12.5f, true, 0.16f));
        g.drawText (title, header.withTrimmedTop (15.0f).removeFromTop (20.0f), juce::Justification::centred, false);

        g.setColour (on ? tint.withAlpha (0.85f) : Theme::textLo);
        g.setFont (Theme::ui (8.5f, true, 0.2f));
        g.drawText (info.subtitle, header.withTrimmedTop (35.0f).removeFromTop (14.0f), juce::Justification::centred, false);

        g.setColour (Theme::divider);
        g.fillRect (r.getX() + 18.0f, header.getBottom(), r.getWidth() - 36.0f, 1.0f);

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
        grip.setBounds (0, 0, getWidth(), headerHeight);
        removeButton.setBounds (getWidth() - 30, 8, 22, 22);

        auto b = getLocalBounds().reduced (8, 0);
        b.removeFromTop (headerHeight + 4);

        auto footArea = b.removeFromBottom (108);
        ledCentre = juce::Point<float> ((float) footArea.getCentreX(), (float) footArea.getY() + 14.0f);
        footSwitch.setBounds (juce::Rectangle<int> (56, 56).withCentre ({ footArea.getCentreX(), footArea.getY() + 62 }));

        // knob grid: two per row, last row centred, the whole grid centred vertically
        const int n = knobs.size();
        if (n == 0)
            return;

        const int rows = (n + 1) / 2;
        const int rowH = juce::jmin (b.getHeight() / 2, 124);
        const int cellW = b.getWidth() / 2;
        const int top = b.getY() + (b.getHeight() - rows * rowH) / 2;

        for (int i = 0; i < n; ++i)
        {
            const int row = i / 2;
            const int inRow = juce::jmin (2, n - row * 2);
            const int col = i % 2;

            const int x0 = b.getX() + (b.getWidth() - inRow * cellW) / 2 + col * cellW;
            knobs[i]->setBounds (x0, top + row * rowH, cellW, rowH);
        }
    }

private:
    int item;
    const PedalCatalog::Pedal& info;
    juce::String title;
    juce::Colour tint;
    juce::AudioProcessorValueTreeState& apvts;

    DragGrip grip;
    FootSwitch footSwitch;
    RemoveButton removeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;
    juce::OwnedArray<LabeledKnob> knobs;

    juce::Point<float> ledCentre;
    bool lastActive = true;
};

//==============================================================================
class LeadChannelPanel : public PanelBase
{
public:
    explicit LeadChannelPanel (NeuralFlowAmpAudioProcessor& p)
        : PanelBase ("LEAD CHANNEL", "ANALOG MODEL"),
          processor (p),
          drive  ("DRIVE",  Theme::amber,  1),
          namIn  ("INPUT",  Theme::amber,  1, " dB"),
          low    ("LOW",    Theme::accent, 1),
          mid    ("MID",    Theme::accent, 1),
          high   ("HIGH",   Theme::accent, 1),
          master ("MASTER", Theme::accent, 1)
    {
        auto& apvts = processor.apvts;

        addAndMakeVisible (scope);

        drive.attach  (apvts, "DRIVE");
        namIn.attach  (apvts, "NAM_IN");
        low.attach    (apvts, "LOW");
        mid.attach    (apvts, "MID");
        high.attach   (apvts, "HIGH");
        master.attach (apvts, "MASTER");

        for (auto* k : { &drive, &namIn, &low, &mid, &high, &master })
            addAndMakeVisible (k);

        // amp type switch in the header
        builtInButton.setButtonText ("BUILT-IN");
        namButton.setButtonText ("NAM");
        for (auto* b : { &builtInButton, &namButton })
        {
            b->setClickingTogglesState (false);
            addAndMakeVisible (b);
        }
        builtInButton.onClick = [this] { setAmpType (0); };
        namButton.onClick     = [this] { setAmpType (1); };
        headerRightInset = 2 * 84 + 14;

        // model card
        loadButton.setButtonText ("LOAD MODEL");
        loadButton.onClick = [this] { chooseFile(); };
        clearButton.setButtonText ("CLEAR");
        clearButton.onClick = [this] { processor.unloadNamModel(); refresh(); };
        levelButton.setButtonText ("LEVEL MATCH");
        levelButton.setTooltip ("Level every model to the same loudness (-18 dB)");
        levelButton.setClickingTogglesState (true);
        levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "NAM_NORM", levelButton);
        for (auto* b : { &loadButton, &clearButton, &levelButton })
            addAndMakeVisible (b);

        refresh();
    }

    /** Called from the UI timer: follows the amp type parameter and the model's loading state. */
    void refresh()
    {
        const bool namMode = processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f;
        const auto status = processor.nam.getStatus();
        const auto info = processor.nam.getModelInfo();

        juce::String name, detail;
        using State = NamEngine::Status::State;

        switch (status.state)
        {
            case State::empty:   name = "No model loaded"; detail = namMode ? "USING THE BUILT-IN AMP UNTIL A MODEL IS LOADED" : "LOAD A .NAM FILE TO USE IT AS THE AMP"; break;
            case State::loading: name = processor.nam.getFile().getFileNameWithoutExtension(); detail = "LOADING..."; break;
            case State::failed:  name = processor.nam.getFile().getFileName(); detail = "COULD NOT LOAD: " + status.message.upToFirstOccurrenceOf ("\n", false, false); break;
            case State::ready:
                name = info.name;
                detail = (info.trainingRate > 0.0 ? juce::String (info.trainingRate / 1000.0, 1) + " kHz" : juce::String ("? kHz"))
                         + (info.hasLoudness ? "  |  " + juce::String (info.loudnessDb, 1) + " dB" : juce::String())
                         + (info.resampled ? "  |  RESAMPLED" : juce::String());
                break;
        }

        juce::String statusText;
        bool dot = false;
        if (! namMode)                               { statusText = "ACTIVE"; dot = true; }
        else if (status.state == State::ready)       { statusText = "NAM ACTIVE"; dot = true; }
        else if (status.state == State::loading)     { statusText = "LOADING"; }
        else if (status.state == State::failed)      { statusText = "MODEL ERROR"; }
        else                                         { statusText = "NO MODEL"; }

        const bool changed = namMode != shownNamMode || name != modelName || detail != modelDetail || statusText != shownStatus;
        if (! changed)
            return;

        shownNamMode = namMode;
        modelName = name;
        modelDetail = detail;
        shownStatus = statusText;

        setStatus (statusText, dot);
        setChip (namMode ? "NAM MODEL" : "ANALOG MODEL");

        builtInButton.setToggleState (! namMode, juce::dontSendNotification);
        namButton.setToggleState (namMode, juce::dontSendNotification);
        drive.setVisible (! namMode);
        namIn.setVisible (namMode);
        clearButton.setEnabled (status.state != State::empty);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        PanelBase::paint (g);
        Theme::drawInset (g, knobArea.toFloat(), 8.0f);
        Theme::drawInset (g, modelCard.toFloat(), 8.0f);

        auto text = modelCard.reduced (14, 8).withTrimmedRight (buttonsWidth + 10);
        const bool hasModel = processor.nam.getStatus().state != NamEngine::Status::State::empty;

        g.setColour (hasModel ? Theme::textHi : Theme::textLo);
        g.setFont (Theme::mono (12.5f));
        g.drawText (modelName, text.removeFromTop (text.getHeight() / 2 + 2), juce::Justification::centredLeft, true);

        g.setColour (modelDetail.startsWith ("COULD NOT") ? Theme::amber : Theme::textLo);
        g.setFont (Theme::ui (9.0f, true, 0.12f));
        g.drawText (modelDetail, text, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto bar = juce::Rectangle<int> (getWidth() - 20 - 84, (headerHeight - 28) / 2, 84, 28);
        namButton.setBounds (bar);
        builtInButton.setBounds (bar.translated (-86, 0));

        auto b = getContentBounds();

        knobArea = b.removeFromRight (juce::roundToInt ((float) b.getWidth() * 0.50f));
        b.removeFromRight (16);

        modelCard = b.removeFromBottom (58);
        b.removeFromBottom (10);
        scope.setBounds (b);

        auto buttons = modelCard.reduced (12, 0).withSizeKeepingCentre (modelCard.getWidth() - 24, 28);
        clearButton.setBounds (buttons.removeFromRight (56));
        buttons.removeFromRight (6);
        levelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (6);
        loadButton.setBounds (buttons.removeFromRight (92));

        auto inner = knobArea.reduced (10, 12);
        const int w = inner.getWidth() / 5;

        auto first = inner.removeFromLeft (w);
        drive.setBounds (first);
        namIn.setBounds (first);
        for (auto* k : { &low, &mid, &high, &master })
            k->setBounds (inner.removeFromLeft (w));
    }

    WaveformView scope;

    // exposed so tests can drive them
    juce::TextButton builtInButton, namButton, loadButton, clearButton;
    juce::TextButton levelButton;

private:
    static constexpr int buttonsWidth = 92 + 100 + 56 + 12;

    void setAmpType (int type)
    {
        if (auto* param = processor.apvts.getParameter ("AMP_TYPE"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) type));
            param->endChangeGesture();
        }
        refresh();
    }

    void chooseFile()
    {
        const auto current = processor.nam.getFile();
        fileChooser = std::make_unique<juce::FileChooser> ("Select a Neural Amp Modeler model (.nam)",
                                                           current.existsAsFile()
                                                               ? current.getParentDirectory()
                                                               : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                           "*.nam");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<LeadChannelPanel> (this)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || ! file.existsAsFile())
                return;

            safe->processor.loadNamModel (file);
            safe->setAmpType (1);   // picking a model means the user wants to hear it
        });
    }

    NeuralFlowAmpAudioProcessor& processor;
    LabeledKnob drive, namIn, low, mid, high, master;
    juce::Rectangle<int> knobArea, modelCard;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelAttachment;

    bool shownNamMode = false;
    juce::String modelName, modelDetail, shownStatus;
};

/** Placeholder card at the end of the chain: click to insert another pedal. */
class AddPedalCard : public juce::Component
{
public:
    static constexpr int width = 168;

    std::function<void()> onClick;

    AddPedalCard() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float a = hover ? 1.0f : 0.6f;

        if (hover)
        {
            g.setColour (Theme::accent.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 12.0f);
        }

        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (0.5f), 12.0f);
        const float dashes[] = { 5.0f, 5.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour ((hover ? Theme::accent : Theme::panelBorder.brighter (0.25f)).withAlpha (a));
        g.fillPath (dashed);

        auto plus = juce::Rectangle<float> (50.0f, 50.0f).withCentre ({ r.getCentreX(), r.getCentreY() - 22.0f });
        g.setColour (juce::Colours::white.withAlpha (hover ? 0.08f : 0.04f));
        g.fillRoundedRectangle (plus, 10.0f);
        g.setColour ((hover ? Theme::accent : Theme::panelBorder.brighter (0.4f)).withAlpha (a));
        g.drawRoundedRectangle (plus.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (hover ? Theme::accent : Theme::textMid);
        g.fillRoundedRectangle (plus.withSizeKeepingCentre (18.0f, 2.0f), 1.0f);
        g.fillRoundedRectangle (plus.withSizeKeepingCentre (2.0f, 18.0f), 1.0f);

        g.setColour (hover ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (12.0f, true, 0.18f));
        g.drawText ("ADD PEDAL", juce::Rectangle<float> (r.getX(), plus.getBottom() + 14.0f, r.getWidth(), 16.0f),
                    juce::Justification::centred, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (9.0f, true, 0.2f));
        g.drawText ("GATE  BOOST  MOD  DELAY  REVERB",
                    juce::Rectangle<float> (r.getX() + 6.0f, plus.getBottom() + 34.0f, r.getWidth() - 12.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    bool hover = false;
};

//==============================================================================
/** Marks the point where the signal goes through the amp. It is an item of the chain like the
    pedals: drag it to put pedals on the other side. */
class AmpNode : public juce::Component
{
public:
    static constexpr int width = 64;

    AmpNode() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) width - 8.0f, 148.0f);
        Theme::drawInset (g, r, 10.0f);

        if (hover)
        {
            g.setColour (Theme::accent.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }

        // grip dots
        g.setColour (hover ? Theme::textMid : Theme::textLo.withAlpha (0.8f));
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 2; ++row)
                g.fillEllipse (r.getCentreX() - 7.5f + (float) col * 5.0f, r.getY() + 9.0f + (float) row * 5.0f, 2.4f, 2.4f);

        const float heights[] = { 7.0f, 14.0f, 21.0f, 12.0f, 5.0f };
        const float x0 = r.getCentreX() - 14.0f, cy = r.getCentreY() - 10.0f;
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (Theme::accent.withAlpha (i == 2 ? 1.0f : 0.6f));
            g.fillRoundedRectangle (x0 + (float) i * 6.0f, cy - heights[i] * 0.5f, 3.0f, heights[i], 1.5f);
        }

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (11.5f, true, 0.2f));
        g.drawText ("AMP", r.withTop (r.getCentreY() + 8.0f).withHeight (16.0f), juce::Justification::centred, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (8.0f, true, 0.2f));
        g.drawText ("+ CAB", r.withTop (r.getCentreY() + 26.0f).withHeight (12.0f), juce::Justification::centred, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false;
};

//==============================================================================
/** The scrolling row of chain items (pedals and the amp, in signal order) followed by the
    "+ add" card. Items can be dragged sideways to change their place in the chain. */
class ChainStrip : public juce::Component, private juce::Timer
{
public:
    /** One thing in the chain: a catalog pedal index, or PedalCatalog::ampItem. */
    struct Item
    {
        int id;
        juce::Component* comp;
    };

    explicit ChainStrip (NeuralFlowAmpAudioProcessor& p) : processor (p), apvts (p.apvts)
    {
        addAndMakeVisible (ampNode);
        ampNode.addMouseListener (this, false);
        addChildComponent (addCard);
    }

    /** True if this pedal item (type + copy) is part of the signal chain. */
    static bool isInChain (juce::AudioProcessorValueTreeState& state, int item)
    {
        const auto added = PedalCatalog::addedId (item);
        return added.isEmpty() || state.getRawParameterValue (added)->load() > 0.5f;
    }

    /** First copy of this pedal type that is not in the chain yet, as an item number; -1 if all are used. */
    static int firstFreeCopy (juce::AudioProcessorValueTreeState& state, int type)
    {
        for (int copy = 0; copy < PedalCatalog::maxCopies; ++copy)
            if (! isInChain (state, PedalCatalog::makeItem (type, copy)))
                return PedalCatalog::makeItem (type, copy);
        return -1;
    }

    static int copiesInChain (juce::AudioProcessorValueTreeState& state, int type)
    {
        int n = 0;
        for (int copy = 0; copy < PedalCatalog::maxCopies; ++copy)
            n += isInChain (state, PedalCatalog::makeItem (type, copy)) ? 1 : 0;
        return n;
    }

    /** Called after the user dropped an item in a new place, with the item ids in their new order. */
    std::function<void (const std::vector<int>&)> onOrderChanged;

    /** Recreates the pedals from the current parameter state and the processor's chain order. */
    void rebuild()
    {
        endDrag();
        animator.cancelAllAnimations (false);

        items.clear();
        pedals.clear();

        for (int id : processor.getChainOrder())
        {
            if (id == PedalCatalog::ampItem)
            {
                items.push_back ({ id, &ampNode });
                continue;
            }

            if (! isInChain (apvts, id))
                continue;

            auto* view = pedals.add (new PedalView (id, apvts));
            addAndMakeVisible (view);
            view->getGrip().addMouseListener (this, false);
            items.push_back ({ id, view });
        }

        bool anyLeft = false;
        for (int type = 0; type < PedalCatalog::count; ++type)
            anyLeft = anyLeft || firstFreeCopy (apvts, type) >= 0;
        addCard.setVisible (anyLeft);
    }

    /** Positions every item for the given height and resizes the strip to fit. */
    void layoutItems (int height, bool animate = false)
    {
        stripHeight = height;
        gaps.clear();
        targets.assign (items.size(), {});

        const int gap = 28;
        int x = 0;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const int w = items[i].comp == &ampNode ? AmpNode::width : PedalView::width;
            if (i > 0)
            {
                gaps.push_back ({ x, x + gap });
                x += gap;
            }
            targets[i] = { x, 0, w, height };
            x += w;
        }
        itemsRight = x;

        juce::Rectangle<int> addBounds;
        if (addCard.isVisible())
        {
            x += gap;
            addBounds = { x, 0, AddPedalCard::width, height };
            x += AddPedalCard::width;
        }

        setSize (x, height);

        for (size_t i = 0; i < items.size(); ++i)
            if (! (dragging && (int) i == dragIndex))
                moveTo (*items[i].comp, targets[i], animate);

        if (addCard.isVisible())
            moveTo (addCard, addBounds, animate);
    }

    bool isDragging() const { return dragging; }

    /** Ids of the items in their current order. */
    std::vector<int> getItemOrder() const
    {
        std::vector<int> ids;
        for (const auto& item : items)
            ids.push_back (item.id);
        return ids;
    }

    //==========================================================================
    // Drag and drop. The mouse handlers below call these; they are public so tests can drive them.

    /** Arms a drag on `comp`; nothing moves until the pointer travels a few pixels. */
    void startDrag (juce::Component& comp, juce::Point<int> stripPos)
    {
        endDrag();

        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].comp != &comp)
                continue;

            dragIndex = (int) i;
            downPos = stripPos;
            grabOffsetX = stripPos.x - comp.getX();
            orderAtStart = getItemOrder();
            armed = true;
            return;
        }
    }

    void dragTo (juce::Point<int> stripPos)
    {
        if (! armed)
            return;

        auto& comp = *items[(size_t) dragIndex].comp;

        if (! dragging)
        {
            if (stripPos.getDistanceFrom (downPos) < 6)
                return;

            dragging = true;
            animator.cancelAnimation (&comp, false);
            comp.toFront (false);
            comp.setAlpha (0.96f);
            startTimerHz (60);
        }

        const int x = juce::jlimit (0, juce::jmax (0, itemsRight - comp.getWidth()), stripPos.x - grabOffsetX);
        comp.setTopLeftPosition (x, 0);

        // Swap with a neighbour once the dragged item's centre passes the neighbour's centre.
        const int centre = x + comp.getWidth() / 2;

        while (dragIndex + 1 < (int) items.size() && centre > targets[(size_t) dragIndex + 1].getCentreX())
        {
            std::swap (items[(size_t) dragIndex], items[(size_t) dragIndex + 1]);
            ++dragIndex;
            layoutItems (stripHeight, true);
        }
        while (dragIndex > 0 && centre < targets[(size_t) dragIndex - 1].getCentreX())
        {
            std::swap (items[(size_t) dragIndex], items[(size_t) dragIndex - 1]);
            --dragIndex;
            layoutItems (stripHeight, true);
        }

        repaint();
    }

    void endDrag()
    {
        if (! armed)
            return;

        const bool wasDragging = dragging;
        auto* comp = items[(size_t) dragIndex].comp;

        armed = dragging = false;
        stopTimer();

        if (wasDragging)
        {
            comp->setAlpha (1.0f);
            animator.animateComponent (comp, targets[(size_t) dragIndex], 1.0f, 140, false, 1.0, 1.0);

            if (getItemOrder() != orderAtStart && onOrderChanged)
                onOrderChanged (getItemOrder());
        }

        dragIndex = -1;
        repaint();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (auto* comp = findItemComponent (e.originalComponent))
            startDrag (*comp, e.getEventRelativeTo (this).getPosition());
    }

    void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.getEventRelativeTo (this).getPosition()); }
    void mouseUp (const juce::MouseEvent&) override     { endDrag(); }

    void paint (juce::Graphics& g) override
    {
        const float y = (float) getHeight() * 0.5f;

        for (const auto& gap : gaps)
        {
            const float x0 = (float) gap.first, x1 = (float) gap.second;

            g.setColour (juce::Colour (0xff2e353d));
            g.fillRect (x0, y - 1.0f, x1 - x0, 2.0f);

            juce::Path chevron;
            const float cx = (x0 + x1) * 0.5f;
            chevron.startNewSubPath (cx - 2.5f, y - 4.5f);
            chevron.lineTo (cx + 2.5f, y);
            chevron.lineTo (cx - 2.5f, y + 4.5f);
            g.setColour (Theme::textMid);
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (dragging && juce::isPositiveAndBelow (dragIndex, (int) items.size()))
        {
            // where the item will land
            auto slot = targets[(size_t) dragIndex].toFloat().reduced (1.0f);
            g.setColour (Theme::accent.withAlpha (0.05f));
            g.fillRoundedRectangle (slot, 12.0f);

            juce::Path outline, dashed;
            outline.addRoundedRectangle (slot.reduced (0.5f), 12.0f);
            const float dashes[] = { 5.0f, 5.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::accent.withAlpha (0.7f));
            g.fillPath (dashed);

            // lift: shadow under the item being carried
            juce::DropShadow (juce::Colours::black.withAlpha (0.65f), 22, { 0, 8 })
                .drawForRectangle (g, items[(size_t) dragIndex].comp->getBounds());
        }
    }

    juce::OwnedArray<PedalView> pedals;
    AddPedalCard addCard;

private:
    void timerCallback() override
    {
        auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        if (! dragging || ! mouse.isDragging())
            return;

        auto* viewport = findParentComponentOfClass<juce::Viewport>();
        if (viewport == nullptr)
            return;

        // scroll the strip when an item is held near either edge
        const auto screenPos = mouse.getScreenPosition();
        const auto inViewport = viewport->getLocalPoint (nullptr, screenPos).roundToInt();

        const int edge = 56, visible = viewport->getMaximumVisibleWidth();
        int dx = 0;
        if (inViewport.x < edge)            dx = -(2 + (edge - inViewport.x) / 4);
        else if (inViewport.x > visible - edge) dx = 2 + (inViewport.x - (visible - edge)) / 4;

        if (dx != 0)
        {
            viewport->setViewPosition (juce::jmax (0, viewport->getViewPositionX() + dx), 0);
            dragTo (getLocalPoint (nullptr, screenPos).roundToInt());
        }
    }

    juce::Component* findItemComponent (juce::Component* c) const
    {
        for (; c != nullptr && c != this; c = c->getParentComponent())
            for (const auto& item : items)
                if (item.comp == c)
                    return c;
        return nullptr;
    }

    void moveTo (juce::Component& comp, juce::Rectangle<int> bounds, bool animate)
    {
        if (comp.getBounds() == bounds)
            return;

        if (animate && comp.isVisible() && comp.getWidth() > 0)
        {
            animator.animateComponent (&comp, bounds, 1.0f, 140, false, 1.0, 1.0);
        }
        else
        {
            animator.cancelAnimation (&comp, false);
            comp.setBounds (bounds);
        }
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    AmpNode ampNode;
    juce::ComponentAnimator animator;

    std::vector<Item> items;
    std::vector<juce::Rectangle<int>> targets;
    std::vector<std::pair<int, int>> gaps;
    int stripHeight = 0, itemsRight = 0;

    bool armed = false, dragging = false;
    int dragIndex = -1, grabOffsetX = 0;
    juce::Point<int> downPos;
    std::vector<int> orderAtStart;
};

//==============================================================================
/** One selectable line in the add-pedal popover. */
class PedalRow : public juce::Button
{
public:
    static constexpr int height = 58;

    /** `type` is the pedal type; `copiesUsed` how many copies are in the chain already.
        The row is only disabled once every copy is in use. */
    PedalRow (int pedalType, int copiesUsed)
        : juce::Button (juce::String ("row_") + PedalCatalog::all()[(size_t) pedalType].name),
          type (pedalType), info (PedalCatalog::all()[(size_t) pedalType]), tint (info.colour),
          used (copiesUsed), inChain (copiesUsed >= PedalCatalog::maxCopies)
    {
        setEnabled (! inChain);
        setMouseCursor (inChain ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);

        for (const auto& k : info.knobs)
            knobNames += (knobNames.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))) + juce::String (k.label);
    }

    const PedalCatalog::Pedal& getInfo() const { return info; }
    int getType() const { return type; }
    int getCopiesUsed() const { return used; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float alpha = inChain ? 0.45f : 1.0f;
        const bool hot = isEnabled() && (highlighted || down);

        if (hot)
        {
            g.setGradientFill (juce::ColourGradient (tint.withAlpha (down ? 0.20f : 0.13f), r.getX(), r.getY(),
                                                     tint.withAlpha (0.03f), r.getRight(), r.getY(), false));
            g.fillRoundedRectangle (r, 9.0f);
            g.setColour (tint.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
        }

        // tile with a little knob
        auto tile = juce::Rectangle<float> (38.0f, 38.0f).withCentre ({ r.getX() + 12.0f + 19.0f, r.getCentreY() });
        g.setColour (tint.withAlpha (0.13f * alpha));
        g.fillRoundedRectangle (tile, 9.0f);
        g.setColour (tint.withAlpha (0.5f * alpha));
        g.drawRoundedRectangle (tile.reduced (0.5f), 9.0f, 1.0f);

        const auto c = tile.getCentre();
        g.setColour (tint.withAlpha (0.9f * alpha));
        g.drawEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f, 1.6f);
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.0f, -9.0f, 2.0f, 8.0f, 1.0f);
        pointer.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (35.0f)).translated (c.x, c.y));
        g.fillPath (pointer);

        // text
        const float textX = r.getX() + 12.0f + 38.0f + 14.0f;
        const float textW = r.getWidth() - (textX - r.getX()) - 96.0f;

        g.setColour (Theme::textHi.withAlpha (alpha));
        g.setFont (Theme::ui (12.5f, true, 0.12f));
        g.drawText (used > 0 ? juce::String (info.name) + "  x" + juce::String (used) : juce::String (info.name),
                    juce::Rectangle<float> (textX, r.getCentreY() - 18.0f, textW, 18.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo.withAlpha (juce::jmin (1.0f, alpha + 0.2f)));
        g.setFont (Theme::ui (9.0f, true, 0.1f));
        g.drawText (knobNames, juce::Rectangle<float> (textX, r.getCentreY() + 2.0f, textW, 14.0f),
                    juce::Justification::centredLeft, false);

        // right side: category chip, or "in chain"
        const float rightEdge = r.getRight() - 12.0f;
        if (inChain)
        {
            const auto font = Theme::ui (9.0f, true, 0.16f);
            const float w = font.getStringWidthFloat ("ALL IN USE");
            g.setColour (Theme::textLo);
            g.setFont (font);
            g.drawText ("ALL IN USE", juce::Rectangle<float> (rightEdge - w - 2.0f, r.getCentreY() - 8.0f, w + 2.0f, 16.0f),
                        juce::Justification::centredRight, false);

            juce::Path tick;
            const float tx = rightEdge - w - 20.0f, ty = r.getCentreY();
            tick.startNewSubPath (tx, ty);
            tick.lineTo (tx + 3.0f, ty + 3.5f);
            tick.lineTo (tx + 9.0f, ty - 3.5f);
            g.strokePath (tick, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            const auto font = Theme::ui (8.5f, true, 0.2f);
            const float w = font.getStringWidthFloat (info.category) + 18.0f;
            auto chip = juce::Rectangle<float> (rightEdge - w, r.getCentreY() - 10.0f, w, 20.0f);
            g.setColour (tint.withAlpha (hot ? 0.24f : 0.13f));
            g.fillRoundedRectangle (chip, 5.0f);
            g.setColour (tint.withAlpha (0.95f));
            g.setFont (font);
            g.drawText (info.category, chip, juce::Justification::centred, false);
        }
    }

private:
    int type;
    const PedalCatalog::Pedal& info;
    juce::Colour tint;
    int used;
    bool inChain;   // every copy is in the chain
    juce::String knobNames;
};

/** Contents of the call-out that opens from "+ ADD PEDAL": one row per pedal that can be added. */
class AddPedalMenu : public juce::Component
{
public:
    static constexpr int width = 330;

    /** Called with the pedal type the user picked. */
    std::function<void (int)> onPick;

    explicit AddPedalMenu (juce::AudioProcessorValueTreeState& apvts)
    {
        for (int type = 0; type < PedalCatalog::count; ++type)
        {
            const int used = ChainStrip::copiesInChain (apvts, type);
            available += PedalCatalog::maxCopies - used;

            auto* row = rows.add (new PedalRow (type, used));
            row->onClick = [this, row]
            {
                if (onPick)
                    onPick (row->getType());

                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
            };
            addAndMakeVisible (row);
        }

        setSize (width, padding + headerHeight + rows.size() * (PedalRow::height + rowGap) + footerHeight);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced ((float) padding, 0.0f);
        auto header = r.withTrimmedTop ((float) padding).removeFromTop ((float) headerHeight);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (12.0f, true, 0.2f));
        g.drawText ("ADD PEDAL", header.withTrimmedBottom (4.0f), juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (9.0f, true, 0.18f));
        g.drawText (available > 0 ? juce::String (available) + " AVAILABLE" : juce::String ("ALL IN USE"),
                    header.withTrimmedBottom (4.0f), juce::Justification::centredRight, false);

        g.setColour (Theme::divider.withAlpha (0.10f));
        g.fillRect (r.getX(), header.getBottom() - 4.0f, r.getWidth(), 1.0f);

        auto footer = getLocalBounds().toFloat().removeFromBottom ((float) footerHeight).reduced ((float) padding, 0.0f);
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (8.5f, true, 0.16f));
        g.drawText ("DRAG PEDALS IN THE CHAIN TO REORDER THEM", footer.withTrimmedBottom (4.0f),
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (padding, 0);
        b.removeFromTop (padding + headerHeight);

        for (auto* row : rows)
        {
            row->setBounds (b.removeFromTop (PedalRow::height));
            b.removeFromTop (rowGap);
        }
    }

    /** Row of a pedal type, found by its "<NAME>_ADDED" id (e.g. "DELAY_ADDED"). */
    PedalRow* findRow (const juce::String& addedID) const
    {
        for (auto* row : rows)
            if (addedID == PedalCatalog::addedBaseId (row->getType()))
                return row;
        return nullptr;
    }

private:
    static constexpr int padding = 10, headerHeight = 34, footerHeight = 28, rowGap = 4;

    juce::OwnedArray<PedalRow> rows;
    int available = 0;
};

//==============================================================================
class SignalChainPanel : public PanelBase
{
public:
    explicit SignalChainPanel (NeuralFlowAmpAudioProcessor& p)
        : PanelBase ("SIGNAL CHAIN"), processor (p), apvts (p.apvts), strip (p)
    {
        viewport.setViewedComponent (&strip, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        strip.addCard.onClick = [this] { showAddMenu(); };
        strip.onOrderChanged = [this] (const std::vector<int>& visible) { applyVisibleOrder (visible); };

        addButton.setButtonText ("+  ADD PEDAL");
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);
        headerRightInset = 124;

        rebuild();
    }

    /** Called from the UI timer: reflects automation / state changes and keeps the status text current. */
    void refresh()
    {
        if (! strip.isDragging() && (currentMask() != shownMask || processor.getChainOrder() != shownOrder))
        {
            rebuild();
            return;
        }

        bool changed = false;
        for (auto* p : strip.pedals)
            changed = p->refresh() || changed;

        if (changed)
            updateStatus();
    }

    void resized() override
    {
        addButton.setBounds (getWidth() - 20 - 108, (headerHeight - 28) / 2, 108, 28);

        auto b = getContentBounds();
        viewport.setBounds (b.withHeight (b.getHeight() + 10));
        strip.layoutItems (b.getHeight());
    }

    ChainStrip& getStrip() { return strip; }

    /** Opens the add-pedal popover below the "+ ADD PEDAL" button. */
    void showAddMenu()
    {
        auto* parent = getParentComponent();
        if (parent == nullptr || ! addButton.isEnabled())
            return;

        auto menu = std::make_unique<AddPedalMenu> (apvts);
        menu->onPick = [safe = juce::Component::SafePointer<SignalChainPanel> (this)] (int type)
        {
            if (safe != nullptr)
                safe->addPedal (type);
        };

        auto& box = juce::CallOutBox::launchAsynchronously (std::move (menu),
                                                            parent->getLocalArea (&addButton, addButton.getLocalBounds()),
                                                            parent);
        box.setArrowSize (10.0f);
    }

private:
    int currentMask() const
    {
        int mask = 0;
        for (int item = 0; item < PedalCatalog::ampItem; ++item)
            mask |= (ChainStrip::isInChain (apvts, item) ? 1 : 0) << item;
        return mask;
    }

    void rebuild()
    {
        shownMask = currentMask();
        shownOrder = processor.getChainOrder();
        strip.rebuild();

        for (auto* p : strip.pedals)
        {
            if (PedalCatalog::isRemovable (p->getItem()))
            {
                const int item = p->getItem();
                p->onRemove = [self = this, item]
                {
                    self->setAdded (item, false);
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SignalChainPanel> (self)]
                    {
                        if (safe != nullptr)
                            safe->refresh();
                    });
                };
            }
        }

        strip.layoutItems (getContentBounds().getHeight());
        updateStatus();
        repaint();
    }

    void updateStatus()
    {
        int active = 0;
        for (auto* p : strip.pedals)
            active += p->isActive() ? 1 : 0;

        setStatus (juce::String (active) + " / " + juce::String (strip.pedals.size()) + " ACTIVE", false);
        addButton.setEnabled (strip.addCard.isVisible());
    }

    void setAdded (int item, bool added)
    {
        if (auto* param = apvts.getParameter (PedalCatalog::addedId (item)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (added ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    }

    /** Puts the first unused copy of a pedal type into the chain. */
    void addPedal (int type)
    {
        const int item = ChainStrip::firstFreeCopy (apvts, type);
        if (item < 0)
            return;

        setAdded (item, true);
        rebuild();

        // bring the new pedal into view
        for (auto* p : strip.pedals)
            if (p->getItem() == item)
                viewport.setViewPosition (juce::jmax (0, p->getRight() - viewport.getMaximumVisibleWidth() + 40), 0);
    }

    /** The strip only knows the items that are on screen. Pedals that are not in the chain keep
        their slot in the processor's order; the visible items are poured into the slots that the
        visible items used to occupy. */
    void applyVisibleOrder (const std::vector<int>& visible)
    {
        auto full = processor.getChainOrder();
        auto isVisible = [&] (int id) { return std::find (visible.begin(), visible.end(), id) != visible.end(); };

        size_t next = 0;
        for (auto& slot : full)
            if (isVisible (slot))
                slot = visible[next++];

        processor.setChainOrder (full);
        shownOrder = processor.getChainOrder();
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    ChainStrip strip;
    juce::Viewport viewport;
    juce::TextButton addButton;
    int shownMask = 0;
    PedalCatalog::ChainOrder shownOrder {};
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
