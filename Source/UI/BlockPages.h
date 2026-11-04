#pragma once

#include "../PluginProcessor.h"
#include "../PedalCatalog.h"
#include "Theme.h"
#include "Widgets.h"
#include "Panels.h"
#include "FileBrowser.h"

//==============================================================================
/** What a ModelSlot works on: a NAM engine of the processor and how to load / clear it. The amp slot
    and every NAM pedal copy have one. */
struct ModelTarget
{
    NamEngine& engine;
    std::function<void (const juce::File&)> load;
    std::function<void()> unload;
    FileLibrary::Kind kind = FileLibrary::Kind::amps;   // which library folder it browses
    juce::String emptyHint = "Load a .nam file to use it as the amp";
};

//==============================================================================
/** The strip of what is loaded in a slot: a name, a short detail line, the arrows to the previous / next
    file of the list, and a "more" menu (load from a file anywhere, show in the file manager, clear).
    Picking is the list's job (FileBrowserList, next to the slot); while the list is hidden the strip
    shows a Browse button that brings it back. ModelSlot and IrSlot share this. */
class SlotStrip : public juce::Component
{
public:
    static constexpr int height = 52;

    SlotStrip (const juce::String& prevName, const juce::String& nextName, const juce::String& what)
        : prevButton (false, prevName), nextButton (true, nextName)
    {
        prevButton.setTooltip ("Previous " + what + " in the list");
        nextButton.setTooltip ("Next " + what + " in the list");
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (+1); };
        moreButton.onClick = [this] { showMoreMenu(); };

        browseButton.setButtonText ("Browse");
        browseButton.setTooltip ("Show the list");
        browseButton.onClick = [this] { setBrowseOpen (true); };

        for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &moreButton })
            addAndMakeVisible (b);
        addChildComponent (browseButton);
    }

    /** Called when the list was shown or hidden. */
    std::function<void()> onBrowseToggled;
    bool isBrowseOpen() const { return browseOpen; }

    void setBrowseOpen (bool open)
    {
        if (open == browseOpen)
            return;

        browseOpen = open;
        browseButton.setVisible (! open);
        resized();
        repaint();

        if (onBrowseToggled)
            onBrowseToggled();
    }

    /** How the arrows find the file before / after the loaded one: the page hands in its list's order
        (FileBrowserList::neighbourOf). Without it they step through the file's folder. */
    std::function<juce::File (const juce::File&, int)> neighbour;

    void paint (juce::Graphics& g) override
    {
        Theme::drawInset (g, getLocalBounds().toFloat(), 8.0f);

        auto text = getLocalBounds().reduced (16, 7).withTrimmedRight (buttonsWidth() + 12);
        auto top = text.removeFromTop (text.getHeight() / 2);

        g.setColour (nameIsDim ? Theme::textMid : Theme::textHi);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (name, top, juce::Justification::centredLeft, true);

        g.setColour (detailIsError ? Theme::danger : Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (detail, text, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto buttons = getLocalBounds().reduced (10, 0).withSizeKeepingCentre (getWidth() - 20, 30);

        if (! browseOpen)
        {
            browseButton.setBounds (buttons.removeFromRight (browseWidth));
            buttons.removeFromRight (8);
        }

        moreButton.setBounds (buttons.removeFromRight (IconButton::size).withSizeKeepingCentre (IconButton::size, IconButton::size));
        buttons.removeFromRight (4);
        nextButton.setBounds (buttons.removeFromRight (28));
        prevButton.setBounds (buttons.removeFromRight (28));
    }

    // exposed so tests can drive them
    StepButton prevButton, nextButton;
    IconButton moreButton { "slot_more", Icons::more(), "More" };
    juce::TextButton browseButton;

protected:
    /** The file shown in the strip (the arrows step from it). */
    virtual juce::File currentFile() const = 0;
    virtual void loadFile (const juce::File& file) = 0;
    /** Opens the file dialog for a file from anywhere. */
    virtual void chooseFile() = 0;
    /** Takes the file out of the slot; `label` is what the menu calls that. */
    virtual void clearSlot() = 0;
    virtual juce::String clearLabel() const = 0;
    virtual bool canClear() const = 0;

    void setText (const juce::String& newName, const juce::String& newDetail, bool dim, bool error)
    {
        if (newName == name && newDetail == detail && dim == nameIsDim && error == detailIsError)
            return;

        name = newName;
        detail = newDetail;
        nameIsDim = dim;
        detailIsError = error;
        repaint();
    }

private:
    static constexpr int browseWidth = 72;

    int buttonsWidth() const
    {
        return 2 * 28 + 4 + IconButton::size + (browseOpen ? 0 : browseWidth + 8);
    }

    void step (int delta)
    {
        const auto from = currentFile();
        const auto file = neighbour ? neighbour (from, delta) : juce::File();
        if (file.existsAsFile() && file != from)
            loadFile (file);
    }

    void showMoreMenu()
    {
        const auto file = currentFile();

        juce::PopupMenu menu;
        menu.addItem (1, "Load from file...");
        menu.addItem (2, "Show in file manager", file.existsAsFile());
        menu.addSeparator();
        menu.addItem (3, clearLabel(), canClear());
        Theme::styleMenu (menu, *this);

        menu.showMenuAsync (Theme::menuBelow (moreButton),
                            [safe = juce::Component::SafePointer<SlotStrip> (this), file] (int choice)
        {
            if (safe == nullptr)
                return;

            switch (choice)
            {
                case 1: safe->chooseFile(); break;
                case 2: file.revealToUser(); break;
                case 3: safe->clearSlot(); break;
                default: break;
            }
        });
    }

    juce::String name, detail;
    bool nameIsDim = true, detailIsError = false, browseOpen = true;
};

//==============================================================================
/** The strip of a Neural Amp Modeler model: its name and a short detail line (training rate, loudness,
    loading or error state). Works for any slot that holds a NAM model (see ModelTarget). */
class ModelSlot : public SlotStrip
{
public:
    /** The amp slot of the processor. */
    explicit ModelSlot (TiniAudioProcessor& p)
        : ModelSlot (p, ModelTarget { p.nam,
                                      [&p] (const juce::File& f) { p.loadNamModel (f); },
                                      [&p] { p.unloadNamModel(); },
                                      FileLibrary::Kind::amps,
                                      "Load a .nam file to use it as the amp" })
    {
    }

    ModelSlot (TiniAudioProcessor&, ModelTarget modelTarget)
        : SlotStrip ("model_prev", "model_next", "model"), target (std::move (modelTarget))
    {
        neighbour = [kind = target.kind] (const juce::File& f, int delta) { return FileLibrary::neighbour (f, kind, delta); };
        refresh();
    }

    /** Loads a model file (from the dialog, the arrows or the browser list). */
    void loadFile (const juce::File& file) override
    {
        if (! file.existsAsFile())
            return;

        target.load (file);
        if (onModelPicked)
            onModelPicked();   // picking a model means the user wants to hear it
        refresh();
    }

    /** Called when the user picked a model file and it started loading. */
    std::function<void()> onModelPicked;

    /** Takes the model out (the "Clear" of the more menu). */
    void clear()
    {
        target.unload();
        refresh();
    }

    /** Called from the UI timer: follows the model's loading state. */
    void refresh()
    {
        const auto status = target.engine.getStatus();
        const auto info = target.engine.getModelInfo();

        juce::String name, detail;
        using State = NamEngine::Status::State;

        switch (status.state)
        {
            case State::empty:   name = "No model loaded"; detail = target.emptyHint; break;
            case State::loading: name = target.engine.getFile().getFileNameWithoutExtension(); detail = "Loading..."; break;
            case State::failed:  name = target.engine.getFile().getFileName(); detail = "Could not load: " + status.message.upToFirstOccurrenceOf ("\n", false, false); break;
            case State::ready:
            {
                name = info.name;
                const juce::String sep (juce::CharPointer_UTF8 (" \xc2\xb7 "));
                detail = (info.trainingRate > 0.0 ? juce::String (info.trainingRate / 1000.0, 1) + " kHz" : juce::String ("? kHz"))
                         + (info.hasLoudness ? sep + juce::String (info.loudnessDb, 1) + " dB" : juce::String())
                         + (info.resampled ? sep + "resampled" : juce::String());
                break;
            }
        }

        setText (name, detail, status.state == State::empty, status.state == State::failed);
    }

protected:
    juce::File currentFile() const override { return target.engine.getFile(); }
    void clearSlot() override               { clear(); }
    juce::String clearLabel() const override { return "Clear"; }
    bool canClear() const override          { return target.engine.getStatus().state != NamEngine::Status::State::empty; }

    void chooseFile() override
    {
        const auto current = target.engine.getFile();
        const auto library = FileLibrary::libraryFolder (target.kind);
        fileChooser = std::make_unique<juce::FileChooser> ("Select a Neural Amp Modeler model (.nam)",
                                                           current.existsAsFile() ? current.getParentDirectory()
                                                           : library.isDirectory() ? library
                                                           : FileLibrary::downloadsFolder(),
                                                           "*.nam");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<ModelSlot> (this)] (const juce::FileChooser& fc)
        {
            // a model from anywhere is copied into the library first, so the list and arrows find it
            const auto picked = fc.getResult();
            if (safe != nullptr && picked.existsAsFile())
            {
                const auto imported = FileLibrary::importFile (picked, safe->target.kind);
                safe->loadFile (imported.existsAsFile() ? imported : picked);
            }
        });
    }

private:
    ModelTarget target;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

//==============================================================================
/** The strip of an impulse response: its name, its sample rate and length.
    A second slot can be added next to it later for the A/B mix of two IRs. */
class IrSlot : public SlotStrip
{
public:
    explicit IrSlot (TiniAudioProcessor& p)
        : SlotStrip ("ir_prev", "ir_next", "impulse response"), processor (p)
    {
        neighbour = [] (const juce::File& f, int delta) { return FileLibrary::neighbour (f, FileLibrary::Kind::cabs, delta); };
        showFile (processor.loadedIrFile);
    }

    /** Loads an impulse response (from the dialog, the arrows or the browser list). */
    void loadFile (const juce::File& file) override
    {
        if (! file.existsAsFile())
            return;

        processor.loadImpulseResponse (file);
        showFile (file);
    }

    /** Goes back to the built-in cabinet (the "Use built-in cabinet" of the more menu). */
    void useBuiltIn()
    {
        processor.useBuiltInCab();
        showFile ({});
    }

    /** Called from the UI timer: a preset or session may have loaded another IR. */
    void refresh()
    {
        if (processor.loadedIrFile != shownFile)
            showFile (processor.loadedIrFile);
    }

protected:
    juce::File currentFile() const override  { return processor.loadedIrFile; }
    void clearSlot() override                { useBuiltIn(); }
    juce::String clearLabel() const override { return "Use built-in cabinet"; }
    bool canClear() const override           { return shownFile.existsAsFile(); }

    void chooseFile() override
    {
        const auto library = FileLibrary::libraryFolder (FileLibrary::Kind::cabs);
        fileChooser = std::make_unique<juce::FileChooser> ("Select an impulse response (.wav)",
                                                           processor.loadedIrFile.existsAsFile() ? processor.loadedIrFile.getParentDirectory()
                                                           : library.isDirectory() ? library
                                                           : FileLibrary::downloadsFolder(),
                                                           FileLibrary::irWildcard);

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<IrSlot> (this)] (const juce::FileChooser& fc)
        {
            // an impulse response from anywhere is copied into the library first
            const auto picked = fc.getResult();
            if (safe != nullptr && picked.existsAsFile())
            {
                const auto imported = FileLibrary::importFile (picked, FileLibrary::Kind::cabs);
                safe->loadFile (imported.existsAsFile() ? imported : picked);
            }
        });
    }

private:
    void showFile (const juce::File& file)
    {
        shownFile = file;
        juce::String info;

        if (file.existsAsFile())
        {
            if (std::unique_ptr<juce::AudioFormatReader> reader { processor.formatManager.createReaderFor (file) })
            {
                const double ms = 1000.0 * (double) reader->lengthInSamples / reader->sampleRate;
                info = juce::String (reader->sampleRate / 1000.0, 1) + " kHz" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                       + juce::String (juce::roundToInt (ms)) + " ms";
            }
        }

        setText (file.existsAsFile() ? file.getFileName() : juce::String ("Built-in cabinet"),
                 info.isNotEmpty() ? info : juce::String ("Load a .wav file to use your own cabinet"),
                 ! file.existsAsFile(), false);
    }

    TiniAudioProcessor& processor;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File shownFile;
};

//==============================================================================
/** The amp: Built-in / NAM switch, the model slot while NAM is selected, and the knobs.
    Drive is the big main knob (Input takes its place for a NAM model, with the Level match switch
    under it), then Low, Mid and High, then Master. In NAM mode the list of models takes the right
    column, from the top of the page down. */
class AmpPage : public juce::Component
{
public:
    explicit AmpPage (TiniAudioProcessor& p)
        : modelSlot (p), processor (p),
          drive  ("Drive",  Theme::ampColour, 1),
          namIn  ("Input",  Theme::ampColour, 1, " dB"),
          low    ("Low",    Theme::ampColour, 1),
          mid    ("Mid",    Theme::ampColour, 1),
          high   ("High",   Theme::ampColour, 1),
          master ("Master", Theme::ampColour, 1)
    {
        auto& apvts = processor.apvts;

        drive.attach  (apvts, "DRIVE");
        namIn.attach  (apvts, "NAM_IN");
        low.attach    (apvts, "LOW");
        mid.attach    (apvts, "MID");
        high.attach   (apvts, "HIGH");
        master.attach (apvts, "MASTER");

        drive.setMaxDiameter (176);
        namIn.setMaxDiameter (176);
        for (auto* k : { &low, &mid, &high, &master })
            k->setMaxDiameter (104);

        for (auto* k : { &drive, &namIn, &low, &mid, &high, &master })
            addAndMakeVisible (k);

        builtInButton.setButtonText ("Built-in");
        namButton.setButtonText ("NAM");
        for (auto* b : { &builtInButton, &namButton })
        {
            b->setClickingTogglesState (false);
            b->getProperties().set ("segment", true);
            addAndMakeVisible (b);
        }
        builtInButton.onClick = [this] { setAmpType (0); };
        namButton.onClick     = [this] { setAmpType (1); };

        levelSwitch.setTooltip ("Level match: every model plays at the same loudness (-18 dB)");
        levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "NAM_NORM", levelSwitch);
        addChildComponent (levelSwitch);

        modelSlot.onModelPicked = [this] { setAmpType (1); };
        modelSlot.onBrowseToggled = [this] { resized(); refresh(); };
        modelSlot.neighbour = [this] (const juce::File& f, int delta) { return browser.neighbourOf (f, delta); };
        addChildComponent (modelSlot);

        browser.onHide = [this] { modelSlot.setBrowseOpen (false); };
        browser.onPick = [this] (const juce::File& file) { modelSlot.loadFile (file); };
        browser.onRenamed = [this] (const juce::File& oldFile, const juce::File& newFile)
        {
            if (processor.nam.getFile() == oldFile)
                processor.loadNamModel (newFile);   // keep the loaded model; only its path changed
        };
        addChildComponent (browser);

        refresh();
    }

    /** Called from the UI timer: follows the amp type parameter and the model's loading state. */
    void refresh()
    {
        const bool namMode = processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f;

        const bool modeChanged = namMode != laidOutInNamMode;
        laidOutInNamMode = namMode;

        builtInButton.setToggleState (! namMode, juce::dontSendNotification);
        namButton.setToggleState (namMode, juce::dontSendNotification);
        drive.setVisible (! namMode);
        namIn.setVisible (namMode);
        levelSwitch.setVisible (namMode);
        modelSlot.setVisible (namMode);

        const bool showBrowser = namMode && modelSlot.isBrowseOpen();
        if (browser.isVisible() != showBrowser)
            browser.setVisible (showBrowser);

        if (modeChanged || showBrowser != laidOutWithBrowser)
            resized();   // the knobs make room for the list; the whole page is repainted

        if (namMode)
            modelSlot.refresh();

        if (showBrowser && processor.nam.getFile() != browsedFile)
        {
            browsedFile = processor.nam.getFile();
            browser.setCurrentFile (browsedFile);
        }
    }

    void paint (juce::Graphics& g) override
    {
        // track of the segmented switch
        g.setColour (juce::Colour (0x3d767680));   // tertiarySystemFill
        g.fillRoundedRectangle (segmentArea.toFloat(), 9.0f);

        g.setColour (Theme::divider);
        for (auto x : dividerX)
            g.fillRect ((float) x, (float) dividerTop, 1.0f, (float) (dividerBottom - dividerTop));

        if (levelSwitch.isVisible())
        {
            g.setColour (levelSwitch.getToggleState() ? Theme::textMid : Theme::textLo);
            g.setFont (Theme::ui (12.0f));
            g.drawText ("Level match", levelLabel, juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();

        // the list of models takes the right column while it is open (NAM mode only), the whole height:
        // everything for picking a model is in it, the strip on the left shows what plays
        const bool namMode = processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f;
        laidOutWithBrowser = namMode && modelSlot.isBrowseOpen();
        if (laidOutWithBrowser)
        {
            browser.setBounds (b.removeFromRight (browserWidth));
            b.removeFromRight (20);
        }

        auto top = b.removeFromTop (ModelSlot::height);
        segmentArea = top.removeFromLeft (172).withSizeKeepingCentre (172, 36);
        builtInButton.setBounds (segmentArea.withWidth (86));
        namButton.setBounds (segmentArea.withTrimmedLeft (86));
        top.removeFromLeft (16);
        modelSlot.setBounds (top);

        b.removeFromTop (8);

        const int w = b.getWidth();
        auto driveSection  = b.removeFromLeft (w * 30 / 100);
        auto eqSection     = b.removeFromLeft (w * 50 / 100);
        auto masterSection = b;

        dividerX = { driveSection.getRight(), eqSection.getRight() };
        dividerTop = b.getY() + 24;
        dividerBottom = b.getBottom() - 24;

        auto place = [] (LabeledKnob& knob, juce::Rectangle<int> area, int maxDiameter)
        {
            const int cellH = juce::jmin (area.getHeight(), maxDiameter + 28);
            knob.setBounds (area.reduced (12, 0).withSizeKeepingCentre (area.getWidth() - 24, cellH));
        };

        place (drive, driveSection, 176);

        // Input, with the Level match switch under it (it acts on the level the model gets)
        place (namIn, driveSection.withTrimmedBottom (levelRowHeight), 176);
        {
            const auto font = Theme::ui (12.0f);
            const int labelW = (int) std::ceil (font.getStringWidthFloat ("Level match")) + 2;
            const int rowW = 44 + 8 + labelW;
            auto row = juce::Rectangle<int> (rowW, 28).withCentre ({ namIn.getBounds().getCentreX(), namIn.getBottom() + 6 + 14 });
            levelSwitch.setBounds (row.removeFromLeft (44));
            row.removeFromLeft (8);
            levelLabel = row;
        }

        const int cellW = eqSection.getWidth() / 3;
        for (auto* k : { &low, &mid, &high })
            place (*k, eqSection.removeFromLeft (cellW), 104);

        place (master, masterSection, 104);

        // the dividers are painted here, not by a child: without this the old lines stay on screen
        repaint();
    }

    // exposed so tests can drive them
    juce::TextButton builtInButton, namButton;
    ModelSlot modelSlot;
    FileBrowserList browser { FileLibrary::Kind::amps };
    ToggleSwitch levelSwitch { Theme::ampColour };

private:
    static constexpr int browserWidth = 300, levelRowHeight = 40;
    juce::File browsedFile;
    bool laidOutWithBrowser = false;
    bool laidOutInNamMode = false;

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

    TiniAudioProcessor& processor;
    LabeledKnob drive, namIn, low, mid, high, master;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelAttachment;
    juce::Rectangle<int> segmentArea, levelLabel;
    std::vector<int> dividerX;
    int dividerTop = 0, dividerBottom = 0;

public:
    LabeledKnob& getDriveKnob() { return drive; }
    LabeledKnob& getInputKnob() { return namIn; }
};

//==============================================================================
/** A pedal: its name and category, the bypass switch and one knob per parameter, all in the colour
    of the pedal's category. A NAM pedal is named after its model and also shows its model slot and the list
    of pedal captures. */
class PedalPage : public juce::Component
{
public:
    PedalPage (int chainItem, TiniAudioProcessor& processor)
        : PedalPage (chainItem, processor.apvts)
    {
        if (PedalCatalog::typeOf (chainItem) != PedalCatalog::captureType)
            return;

        const auto copy = PedalCatalog::copyOf (chainItem);
        auto& engine = processor.pedalNam[(size_t) copy];
        owner = &processor;

        modelSlot = std::make_unique<ModelSlot> (processor,
            ModelTarget { engine,
                          [&processor, copy] (const juce::File& f) { processor.loadPedalModel (copy, f); },
                          [&processor, copy] { processor.unloadPedalModel (copy); },
                          FileLibrary::Kind::pedals,
                          "Load a .nam capture of a pedal" });
        modelSlot->onBrowseToggled = [this] { resized(); refresh(); };
        addAndMakeVisible (*modelSlot);

        browser = std::make_unique<FileBrowserList> (FileLibrary::Kind::pedals);
        modelSlot->neighbour = [this] (const juce::File& f, int delta) { return browser->neighbourOf (f, delta); };
        browser->onHide = [this] { modelSlot->setBrowseOpen (false); };
        browser->onPick = [this] (const juce::File& file) { modelSlot->loadFile (file); };
        browser->onRenamed = [&processor, copy] (const juce::File& oldFile, const juce::File& newFile)
        {
            if (processor.pedalNam[(size_t) copy].getFile() == oldFile)
                processor.loadPedalModel (copy, newFile);   // keep the loaded model; only its path changed
        };
        addAndMakeVisible (*browser);
        captureEngine = &engine;

        refresh();
    }

    /** Called from the UI timer: follows the model's loading state and the list's folder. */
    void refresh()
    {
        if (modelSlot == nullptr)
            return;

        modelSlot->refresh();

        // the page carries the name of the model, and under it what the model says about itself
        const int copy = PedalCatalog::copyOf (item);
        const auto newTitle = namPedalName (*owner, copy);
        const auto detail = namPedalInfo (*owner, copy).detailLine();
        const auto newCategory = detail.isNotEmpty() ? detail : juce::String ("NAM model");

        if (newTitle != title || newCategory != category)
        {
            title = newTitle;
            category = newCategory;
            repaint();
        }

        const bool showBrowser = modelSlot->isBrowseOpen();
        if (browser->isVisible() != showBrowser)
            browser->setVisible (showBrowser);

        if (showBrowser && captureEngine->getFile() != browsedFile)
        {
            browsedFile = captureEngine->getFile();
            browser->setCurrentFile (browsedFile);
        }
    }

    ModelSlot* getModelSlot() { return modelSlot.get(); }
    FileBrowserList* getBrowser() { return browser.get(); }

    PedalPage (int chainItem, juce::AudioProcessorValueTreeState& state)
        : item (chainItem), info (PedalCatalog::pedalOf (chainItem)),
          title (PedalCatalog::typeOf (chainItem) == PedalCatalog::captureType ? juce::String ("NAM Pedal")
                                                                               : Theme::titleCase (PedalCatalog::displayName (chainItem))),
          category (Theme::titleCase (info.category)),
          tint (blockColour (chainItem)), bypass (tint)
    {
        addAndMakeVisible (bypass);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, PedalCatalog::onId (item), bypass);
        bypass.onStateChange = [this] { repaint(); };

        for (size_t i = 0; i < info.knobs.size(); ++i)
        {
            const auto& k = info.knobs[i];
            auto* knob = knobs.add (new LabeledKnob (Theme::titleCase (k.label), tint, k.decimals, k.suffix));
            knob->attach (state, PedalCatalog::knobId (item, (int) i));
            knob->setMaxDiameter (120);
            addAndMakeVisible (knob);
        }
    }

    int getItem() const { return item; }
    const juce::String& getTitle() const { return title; }
    const juce::String& getCategory() const { return category; }
    ToggleSwitch& getBypass() { return bypass; }

    void paint (juce::Graphics& g) override
    {
        auto header = getLocalBounds().removeFromTop (headerHeight);

        const auto titleFont = Theme::ui (24.0f, true, -0.02f);
        const float titleW = titleFont.getStringWidthFloat (title);
        g.setColour (Theme::textHi);
        g.setFont (titleFont);
        g.drawText (title, header.removeFromLeft ((int) titleW + 4), juce::Justification::centredLeft, false);

        // the category as a chip in its colour
        const auto chipFont = Theme::ui (11.5f, true);
        const auto chip = juce::Rectangle<float> ((float) header.getX() + 12.0f, (float) header.getCentreY() - 11.0f,
                                                  chipFont.getStringWidthFloat (category) + 20.0f, 22.0f);
        g.setColour (tint.withAlpha (0.22f));
        g.fillRoundedRectangle (chip, 11.0f);
        g.setColour (Theme::light (tint));
        g.setFont (chipFont);
        g.drawText (category, chip, juce::Justification::centred, false);
        g.setFont (Theme::ui (13.0f));

        g.setColour (bypass.getToggleState() ? Theme::textMid : Theme::textLo);
        g.drawText (bypass.getToggleState() ? "On" : "Bypassed",
                    getLocalBounds().removeFromTop (headerHeight).withTrimmedRight (52), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto header = b.removeFromTop (headerHeight);
        bypass.setBounds (header.removeFromRight (44).withSizeKeepingCentre (44, 28));

        if (modelSlot != nullptr)
        {
            // the list takes the right column under the header, the strip and the knobs the rest
            if (modelSlot->isBrowseOpen())
            {
                browser->setBounds (b.removeFromRight (300));
                b.removeFromRight (20);
            }

            modelSlot->setBounds (b.removeFromTop (ModelSlot::height));
            b.removeFromTop (8);
        }

        const int n = knobs.size();
        if (n == 0)
            return;

        const int cellW = juce::jmin (180, b.getWidth() / n);
        const int cellH = juce::jmin (b.getHeight(), 120 + 28);
        auto row = b.withSizeKeepingCentre (cellW * n, cellH);

        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (cellW));
    }

private:
    static constexpr int headerHeight = 44;

    int item;
    const PedalCatalog::Pedal& info;
    juce::String title, category;
    juce::Colour tint;
    ToggleSwitch bypass;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::OwnedArray<LabeledKnob> knobs;

    // NAM pedal only
    TiniAudioProcessor* owner = nullptr;
    std::unique_ptr<ModelSlot> modelSlot;
    std::unique_ptr<FileBrowserList> browser;
    NamEngine* captureEngine = nullptr;
    juce::File browsedFile;
};

//==============================================================================
/** The cab: the impulse response slot and the two filters around it. */
class CabPage : public juce::Component
{
public:
    explicit CabPage (TiniAudioProcessor& p)
        : irSlot (p), processor (p),
          lowCut  ("Low cut",  Theme::cabColour, 0, " Hz"),
          highCut ("High cut", Theme::cabColour, 1, " kHz")
    {
        lowCut.attach  (p.apvts, "CAB_LOWCUT");
        highCut.attach (p.apvts, "CAB_HIGHCUT");

        lowCut.setValueFormat ([] (double v) { return juce::String (juce::roundToInt (v)) + " Hz"; },
                               [] (const juce::String& t) { return t.getDoubleValue(); });
        highCut.setValueFormat ([] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; },
                                [] (const juce::String& t)
                                {
                                    const double v = t.getDoubleValue();
                                    return t.containsIgnoreCase ("k") || v < 100.0 ? v * 1000.0 : v;   // "12" means 12 kHz
                                });

        lowCut.setMaxDiameter (120);
        highCut.setMaxDiameter (120);

        addAndMakeVisible (irSlot);
        addAndMakeVisible (lowCut);
        addAndMakeVisible (highCut);

        irSlot.onBrowseToggled = [this] { resized(); refresh(); };
        irSlot.neighbour = [this] (const juce::File& f, int delta) { return browser.neighbourOf (f, delta); };
        browser.onHide = [this] { irSlot.setBrowseOpen (false); };
        browser.onPick = [this] (const juce::File& file) { irSlot.loadFile (file); };
        browser.onRenamed = [this] (const juce::File& oldFile, const juce::File& newFile)
        {
            if (processor.loadedIrFile == oldFile)
                processor.loadImpulseResponse (newFile);
        };
        addAndMakeVisible (browser);
    }

    void refresh()
    {
        irSlot.refresh();

        const bool showBrowser = irSlot.isBrowseOpen();
        if (browser.isVisible() != showBrowser)
            browser.setVisible (showBrowser);

        if (showBrowser && processor.loadedIrFile != browsedFile)
        {
            browsedFile = processor.loadedIrFile;
            browser.setCurrentFile (browsedFile);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();

        // the list takes the right column, the whole height; the strip and the filters the rest
        if (irSlot.isBrowseOpen())
        {
            browser.setBounds (b.removeFromRight (300));
            b.removeFromRight (20);
        }

        irSlot.setBounds (b.removeFromTop (IrSlot::height));
        b.removeFromTop (8);

        const int cellW = 200, cellH = juce::jmin (b.getHeight(), 120 + 28);
        auto row = b.withSizeKeepingCentre (cellW * 2, cellH);
        lowCut.setBounds (row.removeFromLeft (cellW));
        highCut.setBounds (row);
    }

    IrSlot irSlot;
    FileBrowserList browser { FileLibrary::Kind::cabs };

private:
    TiniAudioProcessor& processor;
    juce::File browsedFile;
    LabeledKnob lowCut, highCut;
};

//==============================================================================
/** The big area below the chain strip: shows the page of the selected block, or a page that takes
    its place (the Settings page, the tuner) until a block is clicked. The selection is a setting of
    the editor, not part of a preset. */
class BlockEditor : public juce::Component
{
public:
    enum class Overlay { none, settings, tuner, addPedal };

    explicit BlockEditor (TiniAudioProcessor& p)
        : processor (p), ampPage (p), cabPage (p)
    {
        addChildComponent (ampPage);
        addChildComponent (cabPage);
        show (PedalCatalog::ampItem);
    }

    /** Opens the page of a block: a pedal item, PedalCatalog::ampItem or cabBlock. */
    void show (int block)
    {
        closeOverlay();

        shown = block;
        pedalPage.reset();

        const bool isPedal = block != PedalCatalog::ampItem && block != cabBlock;
        if (isPedal)
        {
            pedalPage = std::make_unique<PedalPage> (block, processor);
            addAndMakeVisible (*pedalPage);
        }

        updatePages();
        resized();
        repaint();
    }

    /** Puts `page` in place of the block page. A block click (show) or closeOverlay() brings it back. */
    void showOverlay (std::unique_ptr<juce::Component> page, Overlay kind)
    {
        closeOverlay (false);

        overlay = std::move (page);
        overlayKind = kind;
        addAndMakeVisible (*overlay);

        updatePages();
        resized();
        repaint();
        notifyOverlay();
    }

    void closeOverlay (bool notify = true)
    {
        if (overlay == nullptr)
            return;

        removeChildComponent (overlay.get());
        overlay.reset();
        overlayKind = Overlay::none;

        updatePages();
        resized();
        repaint();

        if (notify)
            notifyOverlay();
    }

    Overlay getOverlayKind() const { return overlayKind; }
    juce::Component* getOverlay() { return overlay.get(); }

    /** Called whenever an overlay page opened or closed. */
    std::function<void()> onOverlayChanged;

    int getShownBlock() const { return shown; }

    void refresh()
    {
        if (ampPage.isVisible())
            ampPage.refresh();
        if (cabPage.isVisible())
            cabPage.refresh();
        if (pedalPage != nullptr && pedalPage->isVisible())
            pedalPage->refresh();
    }

    void paint (juce::Graphics& g) override
    {
        // the page takes the colour of its block; the pedal picker the accent, the tuner green
        if (overlay == nullptr)
            Theme::drawTintedPanel (g, getLocalBounds().toFloat(), blockColour (shown));
        else if (overlayKind == Overlay::addPedal)
            Theme::drawTintedPanel (g, getLocalBounds().toFloat(), Theme::accent);
        else if (overlayKind == Overlay::tuner)
            Theme::drawTintedPanel (g, getLocalBounds().toFloat(), Theme::green);
        else
            Theme::drawPanel (g, getLocalBounds().toFloat());
    }

    void resized() override
    {
        const auto inner = getLocalBounds().reduced (32, 24);
        ampPage.setBounds (inner);
        cabPage.setBounds (inner);
        if (pedalPage != nullptr)
            pedalPage->setBounds (inner);
        if (overlay != nullptr)
            overlay->setBounds (inner);
    }

    AmpPage& getAmpPage() { return ampPage; }
    CabPage& getCabPage() { return cabPage; }
    PedalPage* getPedalPage() { return pedalPage.get(); }

private:
    void updatePages()
    {
        const bool pagesVisible = overlay == nullptr;
        ampPage.setVisible (pagesVisible && shown == PedalCatalog::ampItem);
        cabPage.setVisible (pagesVisible && shown == cabBlock);
        if (pedalPage != nullptr)
            pedalPage->setVisible (pagesVisible);
    }

    void notifyOverlay()
    {
        if (onOverlayChanged)
            onOverlayChanged();
    }

    TiniAudioProcessor& processor;
    AmpPage ampPage;
    CabPage cabPage;
    std::unique_ptr<PedalPage> pedalPage;
    std::unique_ptr<juce::Component> overlay;
    Overlay overlayKind = Overlay::none;
    int shown = PedalCatalog::ampItem;
};
