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
/** One stompbox: header, a grid of knobs, status LED and a footswitch bound to a bypass parameter.
    Everything (name, colour, knobs, parameters) comes from a PedalCatalog entry. */
class PedalView : public juce::Component
{
public:
    static constexpr int width = 196;

    PedalView (const PedalCatalog::Pedal& pedalInfo, juce::AudioProcessorValueTreeState& state)
        : info (pedalInfo), tint (pedalInfo.colour), apvts (state), footSwitch (tint)
    {
        addAndMakeVisible (footSwitch);
        activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, info.onID, footSwitch);
        lastActive = isActive();

        for (const auto& k : info.knobs)
        {
            auto* knob = knobs.add (new LabeledKnob (k.label, tint, k.decimals, k.suffix));
            knob->attach (apvts, k.paramID);
            knob->setMaxDiameter (60);
            addAndMakeVisible (knob);
        }

        if (info.isRemovable())
        {
            removeButton.onClick = [this] { if (onRemove) onRemove(); };
            addAndMakeVisible (removeButton);
        }
    }

    const PedalCatalog::Pedal& getInfo() const { return info; }
    bool isActive() const { return footSwitch.getToggleState(); }

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
        auto header = r.removeFromTop (60.0f);
        g.setColour (on ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (12.5f, true, 0.16f));
        g.drawText (info.name, header.withTrimmedTop (15.0f).removeFromTop (20.0f), juce::Justification::centred, false);

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
        removeButton.setBounds (getWidth() - 30, 8, 22, 22);

        auto b = getLocalBounds().reduced (8, 0);
        b.removeFromTop (64);

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
    const PedalCatalog::Pedal& info;
    juce::Colour tint;
    juce::AudioProcessorValueTreeState& apvts;

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
/** Marks the point where the signal goes through the amp (pedals before it are pre-amp,
    pedals after it are in the effects loop). */
class AmpNode : public juce::Component
{
public:
    static constexpr int width = 64;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) width - 8.0f, 148.0f);
        Theme::drawInset (g, r, 10.0f);

        const float heights[] = { 7.0f, 14.0f, 21.0f, 12.0f, 5.0f };
        const float x0 = r.getCentreX() - 14.0f, cy = r.getCentreY() - 14.0f;
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (Theme::accent.withAlpha (i == 2 ? 1.0f : 0.6f));
            g.fillRoundedRectangle (x0 + (float) i * 6.0f, cy - heights[i] * 0.5f, 3.0f, heights[i], 1.5f);
        }

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (11.5f, true, 0.2f));
        g.drawText ("AMP", r.withTop (r.getCentreY() + 4.0f).withHeight (16.0f), juce::Justification::centred, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (8.0f, true, 0.2f));
        g.drawText ("+ CAB", r.withTop (r.getCentreY() + 22.0f).withHeight (12.0f), juce::Justification::centred, false);
    }
};

//==============================================================================
/** The scrolling row of pedals: [pre-amp pedals] -> AMP -> [post-amp pedals] -> [+ add]. */
class ChainStrip : public juce::Component
{
public:
    explicit ChainStrip (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        addAndMakeVisible (ampNode);
        addChildComponent (addCard);
    }

    static bool isInChain (juce::AudioProcessorValueTreeState& state, const PedalCatalog::Pedal& p)
    {
        return ! p.isRemovable() || state.getRawParameterValue (p.addedID)->load() > 0.5f;
    }

    /** Recreates the pedals from the current parameter state. */
    void rebuild()
    {
        pedals.clear();

        for (const auto& info : PedalCatalog::all())
            if (isInChain (apvts, info))
                addAndMakeVisible (pedals.add (new PedalView (info, apvts)));

        bool anyLeft = false;
        for (const auto& info : PedalCatalog::all())
            anyLeft = anyLeft || ! isInChain (apvts, info);
        addCard.setVisible (anyLeft);
    }

    /** Positions every item for the given height and resizes the strip to fit. */
    void layoutItems (int height)
    {
        gaps.clear();
        const int gap = 28;
        int x = 0;
        bool ampPlaced = false;
        int previousRight = -1;

        auto place = [&] (juce::Component& c, int w, bool cable)
        {
            if (previousRight >= 0)
            {
                x += gap;
                if (cable)
                    gaps.push_back ({ previousRight, x });
            }
            c.setBounds (x, 0, w, height);
            x += w;
            previousRight = x;
        };

        for (auto* p : pedals)
        {
            if (! ampPlaced && p->getInfo().postAmp)
            {
                place (ampNode, AmpNode::width, true);
                ampPlaced = true;
            }
            place (*p, PedalView::width, true);
        }

        if (! ampPlaced)
            place (ampNode, AmpNode::width, true);

        if (addCard.isVisible())
            place (addCard, AddPedalCard::width, false);

        setSize (x, height);
    }

    int getXOfLastPedal() const { return pedals.isEmpty() ? 0 : pedals.getLast()->getX(); }

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
    }

    juce::OwnedArray<PedalView> pedals;
    AddPedalCard addCard;

private:
    juce::AudioProcessorValueTreeState& apvts;
    AmpNode ampNode;
    std::vector<std::pair<int, int>> gaps;
};

//==============================================================================
class SignalChainPanel : public PanelBase
{
public:
    explicit SignalChainPanel (juce::AudioProcessorValueTreeState& state)
        : PanelBase ("SIGNAL CHAIN"), apvts (state), strip (state)
    {
        viewport.setViewedComponent (&strip, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        strip.addCard.onClick = [this] { showAddMenu(); };

        addButton.setButtonText ("+  ADD PEDAL");
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);
        headerRightInset = 124;

        rebuild();
    }

    /** Called from the UI timer: reflects automation / state changes and keeps the status text current. */
    void refresh()
    {
        if (currentMask() != shownMask)
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

private:
    int currentMask() const
    {
        int mask = 0, bit = 0;
        for (const auto& info : PedalCatalog::all())
            mask |= (ChainStrip::isInChain (apvts, info) ? 1 : 0) << bit++;
        return mask;
    }

    void rebuild()
    {
        shownMask = currentMask();
        strip.rebuild();

        for (auto* p : strip.pedals)
        {
            if (p->getInfo().isRemovable())
            {
                const auto* info = &p->getInfo();
                p->onRemove = [self = this, info]
                {
                    self->setAdded (*info, false);
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

    void setAdded (const PedalCatalog::Pedal& info, bool added)
    {
        if (auto* param = apvts.getParameter (info.addedID))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (added ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    }

    void showAddMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("INSERT PEDAL");

        const auto& all = PedalCatalog::all();
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& info = all[(size_t) i];
            if (! ChainStrip::isInChain (apvts, info))
                menu.addItem (i + 1, juce::String (info.name) + "   -   " + (info.postAmp ? "after amp" : "before amp"));
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                            [safe = juce::Component::SafePointer<SignalChainPanel> (this)] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;

            const auto& info = PedalCatalog::all()[(size_t) (result - 1)];
            safe->setAdded (info, true);
            safe->rebuild();

            // bring the new pedal into view
            for (auto* p : safe->strip.pedals)
                if (&p->getInfo() == &info)
                    safe->viewport.setViewPosition (juce::jmax (0, p->getRight() - safe->viewport.getMaximumVisibleWidth() + 40), 0);
        });
    }

    juce::AudioProcessorValueTreeState& apvts;
    ChainStrip strip;
    juce::Viewport viewport;
    juce::TextButton addButton;
    int shownMask = 0;
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
