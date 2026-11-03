#pragma once

#include "../PluginProcessor.h"
#include "../PedalCatalog.h"
#include "Theme.h"
#include "Widgets.h"
#include "Panels.h"
#include "FileBrowser.h"

//==============================================================================
/** What a ModelSlot works on: a NAM engine of the processor and how to load / clear it. The amp slot
    and every Capture pedal copy have one. */
struct ModelTarget
{
    NamEngine& engine;
    std::function<void (const juce::File&)> load;
    std::function<void()> unload;
    FileLibrary::Kind kind = FileLibrary::Kind::amps;   // which library folder it browses
    juce::String levelParam;                            // "Level match" switch; empty = none
    juce::String emptyHint = "Load a .nam file to use it as the amp";
};

/** One line that shows a Neural Amp Modeler model: its name, a short detail line (training rate,
    loudness, loading or error state) and the buttons to load, clear and level-match it.
    Works for any slot that holds a NAM model (see ModelTarget). */
class ModelSlot : public juce::Component
{
public:
    static constexpr int height = 52;

    /** The amp slot of the processor. */
    explicit ModelSlot (NeuralFlowAmpAudioProcessor& p)
        : ModelSlot (p, ModelTarget { p.nam,
                                      [&p] (const juce::File& f) { p.loadNamModel (f); },
                                      [&p] { p.unloadNamModel(); },
                                      FileLibrary::Kind::amps, "NAM_NORM",
                                      "Load a .nam file to use it as the amp" })
    {
    }

    ModelSlot (NeuralFlowAmpAudioProcessor& p, ModelTarget modelTarget)
        : processor (p), target (std::move (modelTarget))
    {
        loadButton.setButtonText ("Load model");
        loadButton.onClick = [this] { chooseFile(); };

        clearButton.setButtonText ("Clear");
        clearButton.onClick = [this] { target.unload(); refresh(); };

        levelButton.setButtonText ("Level match");
        levelButton.setTooltip ("Level every model to the same loudness (-18 dB)");
        levelButton.setClickingTogglesState (true);
        if (target.levelParam.isNotEmpty())
            levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, target.levelParam, levelButton);
        levelButton.setVisible (target.levelParam.isNotEmpty());

        prevButton.setTooltip ("Previous model in this folder");
        nextButton.setTooltip ("Next model in this folder");
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (+1); };

        browseButton.setButtonText ("Browse");
        browseButton.setTooltip ("Show the models in this folder");
        browseButton.setClickingTogglesState (true);
        browseButton.setToggleState (true, juce::dontSendNotification);
        browseButton.onClick = [this] { if (onBrowseToggled) onBrowseToggled(); };

        for (auto* b : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &browseButton, &prevButton, &nextButton })
            addAndMakeVisible (b);

        if (target.levelParam.isNotEmpty())
            addAndMakeVisible (levelButton);

        refresh();
    }

    /** Called when the Browse button was switched. */
    std::function<void()> onBrowseToggled;
    bool isBrowseOpen() const { return browseButton.getToggleState(); }

    /** Loads a model file (from the dialog, the arrows or the browser list). */
    void loadFile (const juce::File& file)
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

        const bool failed = status.state == State::failed;
        clearButton.setEnabled (status.state != State::empty);

        if (name == modelName && detail == modelDetail && failed == shownFailed)
            return;

        modelName = name;
        modelDetail = detail;
        shownFailed = failed;
        hasModel = status.state != State::empty;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawInset (g, getLocalBounds().toFloat(), 8.0f);

        auto text = getLocalBounds().reduced (16, 7).withTrimmedRight (buttonsWidth() + 12);
        auto top = text.removeFromTop (text.getHeight() / 2);

        g.setColour (hasModel ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (modelName, top, juce::Justification::centredLeft, true);

        g.setColour (shownFailed ? Theme::danger : Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (modelDetail, text, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto buttons = getLocalBounds().reduced (10, 0).withSizeKeepingCentre (getWidth() - 20, 30);
        clearButton.setBounds (buttons.removeFromRight (64));
        buttons.removeFromRight (6);
        if (levelButton.isVisible())
        {
            levelButton.setBounds (buttons.removeFromRight (96));
            buttons.removeFromRight (6);
        }
        loadButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (6);
        browseButton.setBounds (buttons.removeFromRight (72));
        buttons.removeFromRight (6);
        nextButton.setBounds (buttons.removeFromRight (28));
        prevButton.setBounds (buttons.removeFromRight (28));
    }

    // exposed so tests can drive them
    juce::TextButton loadButton, clearButton, levelButton, browseButton;
    StepButton prevButton { false, "model_prev" }, nextButton { true, "model_next" };

private:
    int buttonsWidth() const
    {
        const bool level = target.levelParam.isNotEmpty();
        return 100 + (level ? 96 : 0) + 64 + 72 + 56 + (level ? 5 : 4) * 6 + 12;
    }

    /** The model before / after the loaded one in its folder. */
    void step (int delta)
    {
        const auto file = FileLibrary::neighbour (target.engine.getFile(), target.kind, delta);
        if (file.existsAsFile() && file != target.engine.getFile())
            loadFile (file);
    }

    void chooseFile()
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

    NeuralFlowAmpAudioProcessor& processor;
    ModelTarget target;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelAttachment;

    juce::String modelName, modelDetail;
    bool hasModel = false, shownFailed = false;
};

//==============================================================================
/** One line that shows an impulse response: its name, its sample rate and length, and a Load button.
    A second slot can be added next to it later for the A/B mix of two IRs. */
class IrSlot : public juce::Component
{
public:
    static constexpr int height = 52;

    explicit IrSlot (NeuralFlowAmpAudioProcessor& p) : processor (p)
    {
        loadButton.setButtonText ("Load IR");
        loadButton.onClick = [this] { chooseFile(); };

        prevButton.setTooltip ("Previous impulse response in this folder");
        nextButton.setTooltip ("Next impulse response in this folder");
        prevButton.onClick = [this] { step (-1); };
        nextButton.onClick = [this] { step (+1); };

        browseButton.setButtonText ("Browse");
        browseButton.setTooltip ("Show the impulse responses in this folder");
        browseButton.setClickingTogglesState (true);
        browseButton.setToggleState (true, juce::dontSendNotification);
        browseButton.onClick = [this] { if (onBrowseToggled) onBrowseToggled(); };

        for (auto* b : std::initializer_list<juce::Component*> { &loadButton, &browseButton, &prevButton, &nextButton })
            addAndMakeVisible (b);

        refresh();
    }

    /** Called when the Browse button was switched. */
    std::function<void()> onBrowseToggled;
    bool isBrowseOpen() const { return browseButton.getToggleState(); }

    /** Loads an impulse response (from the dialog, the arrows or the browser list). */
    void loadFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;

        processor.loadImpulseResponse (file);
        showFile (file);
    }

    /** Called from the UI timer: a preset or session may have loaded another IR. */
    void refresh()
    {
        if (processor.loadedIrFile != shownFile)
            showFile (processor.loadedIrFile);
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawInset (g, getLocalBounds().toFloat(), 8.0f);

        auto text = getLocalBounds().reduced (16, 7).withTrimmedRight (buttonsWidth + 12);
        auto top = text.removeFromTop (text.getHeight() / 2);

        g.setColour (shownFile.existsAsFile() ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (irName, top, juce::Justification::centredLeft, true);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (irInfo.isNotEmpty() ? irInfo : juce::String ("Load a .wav file to shape the cabinet"), text,
                    juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto buttons = getLocalBounds().reduced (10, 0).withSizeKeepingCentre (getWidth() - 20, 30);
        loadButton.setBounds (buttons.removeFromRight (84));
        buttons.removeFromRight (6);
        browseButton.setBounds (buttons.removeFromRight (72));
        buttons.removeFromRight (6);
        nextButton.setBounds (buttons.removeFromRight (28));
        prevButton.setBounds (buttons.removeFromRight (28));
    }

    juce::TextButton loadButton, browseButton;
    StepButton prevButton { false, "ir_prev" }, nextButton { true, "ir_next" };

private:
    static constexpr int buttonsWidth = 84 + 72 + 56 + 3 * 6 + 12;

    /** The impulse response before / after the loaded one in its folder. */
    void step (int delta)
    {
        const auto file = FileLibrary::neighbour (processor.loadedIrFile, FileLibrary::Kind::cabs, delta);
        if (file.existsAsFile() && file != processor.loadedIrFile)
            loadFile (file);
    }

    void chooseFile()
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

    void showFile (const juce::File& file)
    {
        shownFile = file;
        irName = file.existsAsFile() ? file.getFileName() : juce::String ("No IR loaded");
        irInfo = {};

        if (file.existsAsFile())
        {
            if (std::unique_ptr<juce::AudioFormatReader> reader { processor.formatManager.createReaderFor (file) })
            {
                const double ms = 1000.0 * (double) reader->lengthInSamples / reader->sampleRate;
                irInfo = juce::String (reader->sampleRate / 1000.0, 1) + " kHz" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                         + juce::String (juce::roundToInt (ms)) + " ms";
            }
        }

        repaint();
    }

    NeuralFlowAmpAudioProcessor& processor;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File shownFile;
    juce::String irName { "No IR loaded" }, irInfo;
};

//==============================================================================
/** The amp: Built-in / NAM switch, the model slot while NAM is selected, and the knobs.
    Drive is the big main knob (Input takes its place for a NAM model), then Low, Mid and High,
    then Master. */
class AmpPage : public juce::Component
{
public:
    explicit AmpPage (NeuralFlowAmpAudioProcessor& p)
        : modelSlot (p), processor (p),
          drive  ("Drive",  Theme::accent, 1),
          namIn  ("Input",  Theme::accent, 1, " dB"),
          low    ("Low",    Theme::accent, 1),
          mid    ("Mid",    Theme::accent, 1),
          high   ("High",   Theme::accent, 1),
          master ("Master", Theme::accent, 1)
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

        modelSlot.onModelPicked = [this] { setAmpType (1); };
        modelSlot.onBrowseToggled = [this] { resized(); refresh(); };
        addChildComponent (modelSlot);

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
        g.setColour (Theme::inset);
        g.fillRoundedRectangle (segmentArea.toFloat(), 8.0f);

        g.setColour (Theme::divider);
        for (auto x : dividerX)
            g.fillRect ((float) x, (float) dividerTop, 1.0f, (float) (dividerBottom - dividerTop));
    }

    void resized() override
    {
        auto b = getLocalBounds();

        auto top = b.removeFromTop (ModelSlot::height);
        segmentArea = top.removeFromLeft (172).withSizeKeepingCentre (172, 36);
        builtInButton.setBounds (segmentArea.withWidth (86));
        namButton.setBounds (segmentArea.withTrimmedLeft (86));
        top.removeFromLeft (16);
        modelSlot.setBounds (top);

        b.removeFromTop (8);

        // the list of models takes the right side while it is open (NAM mode only)
        const bool namMode = processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f;
        laidOutWithBrowser = namMode && modelSlot.isBrowseOpen();
        if (laidOutWithBrowser)
        {
            browser.setBounds (b.removeFromRight (browserWidth));
            b.removeFromRight (16);
        }

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
        place (namIn, driveSection, 176);

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

private:
    static constexpr int browserWidth = 300;
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

    NeuralFlowAmpAudioProcessor& processor;
    LabeledKnob drive, namIn, low, mid, high, master;
    juce::Rectangle<int> segmentArea;
    std::vector<int> dividerX;
    int dividerTop = 0, dividerBottom = 0;

public:
    LabeledKnob& getDriveKnob() { return drive; }
    LabeledKnob& getInputKnob() { return namIn; }
};

//==============================================================================
/** A pedal: its name and category, the bypass switch and one knob per parameter, all in the colour
    of the pedal's category. The Capture pedal also shows its model slot and the list of pedal captures. */
class PedalPage : public juce::Component
{
public:
    PedalPage (int chainItem, NeuralFlowAmpAudioProcessor& processor)
        : PedalPage (chainItem, processor.apvts)
    {
        if (PedalCatalog::typeOf (chainItem) != PedalCatalog::captureType)
            return;

        const auto copy = PedalCatalog::copyOf (chainItem);
        auto& engine = processor.pedalNam[(size_t) copy];

        modelSlot = std::make_unique<ModelSlot> (processor,
            ModelTarget { engine,
                          [&processor, copy] (const juce::File& f) { processor.loadPedalModel (copy, f); },
                          [&processor, copy] { processor.unloadPedalModel (copy); },
                          FileLibrary::Kind::pedals, {},
                          "Load a .nam capture of a pedal" });
        modelSlot->onBrowseToggled = [this] { resized(); refresh(); };
        addAndMakeVisible (*modelSlot);

        browser = std::make_unique<FileBrowserList> (FileLibrary::Kind::pedals);
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
          title (Theme::titleCase (PedalCatalog::displayName (chainItem))),
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
    ToggleSwitch& getBypass() { return bypass; }

    void paint (juce::Graphics& g) override
    {
        auto header = getLocalBounds().removeFromTop (headerHeight);

        const auto titleFont = Theme::ui (20.0f, true);
        const float titleW = titleFont.getStringWidthFloat (title);
        g.setColour (Theme::textHi);
        g.setFont (titleFont);
        g.drawText (title, header.removeFromLeft ((int) titleW + 4), juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (13.0f));
        g.drawText (category, header.withTrimmedLeft (12), juce::Justification::centredLeft, false);

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
            modelSlot->setBounds (b.removeFromTop (ModelSlot::height));
            b.removeFromTop (8);

            if (modelSlot->isBrowseOpen())
            {
                browser->setBounds (b.removeFromRight (300));
                b.removeFromRight (16);
            }
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

    // Capture pedal only
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
    explicit CabPage (NeuralFlowAmpAudioProcessor& p)
        : irSlot (p), processor (p),
          lowCut  ("Low cut",  Theme::accent, 0, " Hz"),
          highCut ("High cut", Theme::accent, 1, " kHz")
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
        irSlot.setBounds (b.removeFromTop (IrSlot::height));
        b.removeFromTop (8);

        if (irSlot.isBrowseOpen())
        {
            browser.setBounds (b.removeFromRight (300));
            b.removeFromRight (16);
        }

        const int cellW = 200, cellH = juce::jmin (b.getHeight(), 120 + 28);
        auto row = b.withSizeKeepingCentre (cellW * 2, cellH);
        lowCut.setBounds (row.removeFromLeft (cellW));
        highCut.setBounds (row);
    }

    IrSlot irSlot;
    FileBrowserList browser { FileLibrary::Kind::cabs };

private:
    NeuralFlowAmpAudioProcessor& processor;
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
    enum class Overlay { none, settings, tuner };

    explicit BlockEditor (NeuralFlowAmpAudioProcessor& p)
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

    NeuralFlowAmpAudioProcessor& processor;
    AmpPage ampPage;
    CabPage cabPage;
    std::unique_ptr<PedalPage> pedalPage;
    std::unique_ptr<juce::Component> overlay;
    Overlay overlayKind = Overlay::none;
    int shown = PedalCatalog::ampItem;
};
