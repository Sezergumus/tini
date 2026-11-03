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
#include "PedalPicker.h"
#include "LibraryPage.h"
#include "../StandaloneHooks.h"

/** The whole UI, laid out in a fixed design space (Theme::designWidth x designHeight).
    The editor scales this component to whatever window size the user picks.

    Two pages, chosen with the tabs in the header. Rig: the chain strip and the page of the block that is
    selected in the strip. Library: the files in Documents/tini in four sections. The header (name, tabs,
    presets, settings) and the footer with the in / out meters and the audio device stay on both. Settings
    and the tuner open over the page that is showing.

    Models (.nam) and impulse responses dropped anywhere on the window are copied into the library
    (Documents/tini) and loaded: a model into the amp (a pedal capture becomes a NAM pedal in the chain),
    an impulse response into the cab.

    In the standalone the header is also the title bar: dragging it moves the window, a double click
    fits it to the screen, and minimise / fit / close buttons sit at its right end (StandaloneHooks). */
class MainView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MainView (NeuralFlowAmpAudioProcessor& p)
        : processor (p), presetBar (p), chain (p), blocks (p), library (p), footer (p)
    {
        setLookAndFeel (&laf);

        addAndMakeVisible (presetBar);
        addAndMakeVisible (chain);
        addAndMakeVisible (blocks);
        addChildComponent (library);
        addAndMakeVisible (footer);

        rigTab.onClick     = [this] { if (rigTab.getToggleState()) showPage (Page::rig); };
        libraryTab.onClick = [this] { if (libraryTab.getToggleState()) showPage (Page::library); };
        rigTab.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (rigTab);
        addAndMakeVisible (libraryTab);

        library.onLoadToAmp = [this] (const juce::File& file) { loadToAmp (file); };
        library.onLoadToCab = [this] (const juce::File& file) { loadToCab (file); };
        library.onAddToChain = [this] (const juce::File& file)
        {
            showPage (Page::rig);
            chain.addNamPedal (file);
        };

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
            chain.setAddOpen (blocks.getOverlayKind() == BlockEditor::Overlay::addPedal);

            updatePages();
            resized();
        };

        chain.onSelect = [this] (int block) { blocks.show (block); };
        chain.onAddRequested = [this] { togglePedalPicker(); };
        blocks.show (chain.getSelected());

        startTimerHz (60);
    }

    ~MainView() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 10.0f, 2.0f);
        }
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

        // the tabs sit right after the name
        {
            auto tabs = juce::Rectangle<int> (margin + 72, 0, getWidth(), headerH);
            rigTab.setBounds (tabs.removeFromLeft (rigTab.getIdealWidth()));
            libraryTab.setBounds (tabs.removeFromLeft (libraryTab.getIdealWidth()));
        }

        auto b = getLocalBounds();
        b.removeFromTop (headerH);

        footer.setBounds (b.removeFromBottom (footerH).reduced (margin, 0));
        const auto chainArea = b.removeFromTop (chainH).reduced (margin, 0);
        chain.setBounds (chainArea);

        auto blockArea = b.reduced (margin, 0).withTrimmedBottom (8).withTrimmedTop (4);
        if (blocks.getOverlayKind() == BlockEditor::Overlay::settings)
            blockArea.setTop (chainArea.getY());   // the page also takes the chain strip's place
        blocks.setBounds (blockArea);

        // the Library takes the place of the chain strip and the block page
        library.setBounds (blockArea.withTop (chainArea.getY()));
    }

    //==========================================================================
    enum class Page { rig, library };

    Page getPage() const { return page; }

    /** Opens a page. Anything open over the page (Settings, the tuner, the pedal picker) closes. */
    void showPage (Page newPage)
    {
        page = newPage;
        rigTab.setToggleState (newPage == Page::rig, juce::dontSendNotification);
        libraryTab.setToggleState (newPage == Page::library, juce::dontSendNotification);
        blocks.closeOverlay();   // (updates the pages through onOverlayChanged when something was open)
        updatePages();
        resized();
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

    //==========================================================================
    // Files dragged from the file manager. A list inside the window that takes the same kind of file
    // (the model / IR browser) gets the drop first and only adds it to the library.
    bool isInterestedInFileDrag (const juce::StringArray& paths) override
    {
        return FileLibrary::anythingToAdd (paths);
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override            { setDropHighlight (false); }

    void filesDropped (const juce::StringArray& paths, int, int) override
    {
        setDropHighlight (false);

        juce::Array<juce::File> files;
        for (const auto& path : paths)
            files.add (juce::File (path));

        importAndLoad (files);
    }

    /** Copies models and impulse responses into the library and loads the first of each: the model into
        the amp (and switches the amp to NAM), the impulse response into the cab. Other files are ignored. */
    void importAndLoad (const juce::Array<juce::File>& files)
    {
        juce::File model, ir;
        juce::Array<juce::File> pedals;

        const FileLibrary::Batch batch (files);   // zips and folders become packs

        for (const auto& item : batch.items)
        {
            const auto& file = item.file;
            FileLibrary::Kind kind;
            if (! FileLibrary::kindOfFile (file, kind))
                continue;

            auto imported = FileLibrary::importFile (file, kind, item.pack);
            if (! imported.existsAsFile())
                imported = file;   // the library could not be written: use the file where it is

            if (kind == FileLibrary::Kind::pedals)
            {
                pedals.add (imported.existsAsFile() ? imported : file);   // a pedal capture joins the chain as a NAM pedal
                continue;
            }

            auto& first = kind == FileLibrary::Kind::cabs ? ir : model;
            if (first == juce::File())
                first = imported;
        }

        if (model != juce::File() || ir != juce::File() || ! pedals.isEmpty())
            showPage (Page::rig);   // show what was loaded

        for (const auto& pedal : pedals)
            chain.addNamPedal (pedal);

        if (model != juce::File())
        {
            chain.select (PedalCatalog::ampItem);
            blocks.getAmpPage().modelSlot.loadFile (model);
        }

        if (ir != juce::File())
        {
            chain.select (cabBlock);
            blocks.getCabPage().irSlot.loadFile (ir);
        }
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

    /** Opens the pedal picker in place of the block page (the "+" button), or closes it again. */
    void togglePedalPicker()
    {
        if (blocks.getOverlayKind() == BlockEditor::Overlay::addPedal)
        {
            blocks.closeOverlay();
            return;
        }

        auto picker = std::make_unique<PedalPicker> (processor);
        picker->onPickBuiltIn = [safe = juce::Component::SafePointer<MainView> (this)] (int type)
        {
            if (safe != nullptr)
                safe->chain.addPedal (type);   // selects the new pedal, which closes the picker
        };
        picker->onPickFile = [safe = juce::Component::SafePointer<MainView> (this)] (const juce::File& file)
        {
            if (safe != nullptr)
                safe->chain.addNamPedal (file);
        };
        blocks.showOverlay (std::move (picker), BlockEditor::Overlay::addPedal);
    }

    /** Opens the tuner in place of the block page (the footer button), or closes it again. */
    void toggleTuner()
    {
        if (blocks.getOverlayKind() == BlockEditor::Overlay::tuner)
            blocks.closeOverlay();
        else
            blocks.showOverlay (std::make_unique<TunerView> (processor), BlockEditor::Overlay::tuner);
    }

    LibraryPage& getLibrary() { return library; }
    TabButton& getRigTab() { return rigTab; }
    TabButton& getLibraryTab() { return libraryTab; }

    /** Loads a model into the amp and switches the amp to NAM (the Library page's "Load to amp"). */
    void loadToAmp (const juce::File& file)
    {
        blocks.getAmpPage().modelSlot.loadFile (file);
    }

    /** Loads an impulse response into the cab. */
    void loadToCab (const juce::File& file)
    {
        blocks.getCabPage().irSlot.loadFile (file);
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
        footer.refresh();

        if (library.isVisible())
            library.refresh();

        if (page == Page::rig || blocks.getOverlayKind() != BlockEditor::Overlay::none)
        {
            chain.refresh();
            blocks.refresh();
        }

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
    LibraryPage library;
    Footer footer;

    Page page = Page::rig;
    TabButton rigTab { "Rig", 7000 }, libraryTab { "Library", 7000 };

    GearButton gearButton;
    WindowButton minimiseButton { WindowButton::Kind::minimise },
                 maximiseButton { WindowButton::Kind::maximise },
                 closeButton    { WindowButton::Kind::close };
    juce::ComponentDragger dragger;
    bool dragging = false;
    bool dropHighlight = false;

    /** Which page shows: the Library covers the chain strip and the block page, unless something is open
        over them (Settings and the tuner use the block area; Settings also takes the chain strip's place). */
    void updatePages()
    {
        const bool overlayOpen = blocks.getOverlayKind() != BlockEditor::Overlay::none
                              && blocks.getOverlayKind() != BlockEditor::Overlay::addPedal;
        const bool settingsOpen = blocks.getOverlayKind() == BlockEditor::Overlay::settings;
        const bool libraryShown = page == Page::library && ! overlayOpen;

        library.setVisible (libraryShown);
        blocks.setVisible (! libraryShown);
        chain.setVisible (page == Page::rig && ! settingsOpen);
    }

    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
