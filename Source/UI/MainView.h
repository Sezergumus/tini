#pragma once

#include "../PluginProcessor.h"
#include "NeuralLookAndFeel.h"
#include "Panels.h"
#include "Footer.h"
#include "BlockPages.h"
#include "PresetBar.h"
#include "HeaderButtons.h"
#include "SettingsPage.h"
#include "TunerView.h"
#include "../StandaloneHooks.h"

/** The whole UI, laid out in a fixed design space (Theme::designWidth x designHeight).
    The editor scales this component to whatever window size the user picks.

    Rig: a header (name, presets, settings), the chain strip, the page of the block that is selected
    in the strip, and a footer with the in / out meters and the audio device.

    In the standalone the header is also the title bar: dragging it moves the window, a double click
    fits it to the screen, and minimise / fit / close buttons sit at its right end (StandaloneHooks). */
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

        gearButton.onClick = [this] { toggleSettings(); };
        addAndMakeVisible (gearButton);

        if (StandaloneHooks::controller().available())
        {
            minimiseButton.onClick = [] { if (auto& f = StandaloneHooks::controller().minimise) f(); };
            maximiseButton.onClick = [] { if (auto& f = StandaloneHooks::controller().toggleMaximise) f(); };
            closeButton.onClick    = [] { if (auto& f = StandaloneHooks::controller().close) f(); };

            for (auto* b : { &minimiseButton, &maximiseButton, &closeButton })
                addAndMakeVisible (b);
        }

        footer.onTunerClicked = [this] { toggleTuner(); };
        blocks.onOverlayChanged = [this]
        {
            const bool settingsOpen = blocks.getOverlayKind() == BlockEditor::Overlay::settings;

            gearButton.setToggleState (settingsOpen, juce::dontSendNotification);
            footer.setTunerOpen (blocks.getOverlayKind() == BlockEditor::Overlay::tuner);

            // Settings fills the area between header and footer, so the chain strip steps aside
            chain.setVisible (! settingsOpen);
            resized();
        };

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
        // right end of the header: window buttons (standalone only), then the gear
        auto right = juce::Rectangle<int> (getWidth(), headerH).reduced (0, (headerH - WindowButton::height) / 2);
        if (minimiseButton.isVisible())
        {
            right.removeFromRight (6);
            closeButton.setBounds    (right.removeFromRight (WindowButton::width));
            maximiseButton.setBounds (right.removeFromRight (WindowButton::width));
            minimiseButton.setBounds (right.removeFromRight (WindowButton::width));
            right.removeFromRight (10);
        }
        else
        {
            right.removeFromRight (margin - 6);
        }
        gearButton.setBounds (right.removeFromRight (GearButton::size).withSizeKeepingCentre (GearButton::size, GearButton::size));

        presetBar.setBounds (juce::Rectangle<int> (PresetBar::width, PresetBar::height)
                                 .withCentre ({ getWidth() / 2, headerH / 2 }));

        auto b = getLocalBounds();
        b.removeFromTop (headerH);

        footer.setBounds (b.removeFromBottom (footerH).reduced (margin, 0));
        const auto chainArea = b.removeFromTop (chainH).reduced (margin, 0);
        chain.setBounds (chainArea);

        auto blockArea = b.reduced (margin, 0).withTrimmedBottom (8).withTrimmedTop (4);
        if (blocks.getOverlayKind() == BlockEditor::Overlay::settings)
            blockArea.setTop (chainArea.getY());   // the page also takes the chain strip's place
        blocks.setBounds (blockArea);
    }

    //==========================================================================
    // The header drags the window (standalone). Mouse events that no child took end up here.
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = StandaloneHooks::controller().available() && e.getPosition().y < headerH;

        if (dragging)
            if (auto* window = getTopLevelComponent())
                dragger.startDraggingComponent (window, e.getEventRelativeTo (window));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            if (auto* window = getTopLevelComponent())
                dragger.dragComponent (window, e.getEventRelativeTo (window), nullptr);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (StandaloneHooks::controller().available() && e.getPosition().y < headerH)
            if (auto& toggle = StandaloneHooks::controller().toggleMaximise)
                toggle();
    }

    /** Opens the Settings page over the chain strip and the block page, or closes it again. */
    void toggleSettings()
    {
        if (blocks.getOverlayKind() == BlockEditor::Overlay::settings)
        {
            blocks.closeOverlay();
        }
        else
        {
            auto page = std::make_unique<SettingsPage> (processor);

            // the Done button lives inside the page: close it after the click has returned
            page->onClose = [safe = juce::Component::SafePointer<MainView> (this)]
            {
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->blocks.closeOverlay(); });
            };
            blocks.showOverlay (std::move (page), BlockEditor::Overlay::settings);
        }
    }

    /** Opens the tuner in place of the block page (the footer button), or closes it again. */
    void toggleTuner()
    {
        if (blocks.getOverlayKind() == BlockEditor::Overlay::tuner)
            blocks.closeOverlay();
        else
            blocks.showOverlay (std::make_unique<TunerView> (processor), BlockEditor::Overlay::tuner);
    }

    GearButton& getGearButton() { return gearButton; }
    WindowButton& getMinimiseButton() { return minimiseButton; }
    WindowButton& getMaximiseButton() { return maximiseButton; }
    WindowButton& getCloseButton() { return closeButton; }

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

        if (maximiseButton.isVisible())
            if (auto& fitted = StandaloneHooks::controller().isMaximised)
                maximiseButton.setFitted (fitted());
    }

    //==========================================================================
    NeuralFlowAmpAudioProcessor& processor;
    NeuralLookAndFeel laf;

    PresetBar presetBar;
    ChainBar chain;
    BlockEditor blocks;
    Footer footer;

    GearButton gearButton;
    WindowButton minimiseButton { WindowButton::Kind::minimise },
                 maximiseButton { WindowButton::Kind::maximise },
                 closeButton    { WindowButton::Kind::close };
    juce::ComponentDragger dragger;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
