#pragma once

#include "../PluginProcessor.h"
#include "Panels.h"
#include "ModelList.h"
#include "FilterBar.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** One section of the Library page: its name and how many files it holds. */
class SectionButton : public juce::Button
{
public:
    static constexpr int height = 40;

    SectionButton (const juce::String& label, int radioGroup) : juce::Button (label)
    {
        setButtonText (label);
        setClickingTogglesState (true);
        setRadioGroupId (radioGroup);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setCount (int n)
    {
        if (count == n)
            return;
        count = n;
        repaint();
    }

    int getCount() const { return count; }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
        const bool open = getToggleState();

        if (open || highlighted)
        {
            g.setColour (open ? Theme::surfaceHi : juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r, 8.0f);
        }

        if (open)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.getX() + 2.0f, r.getY() + 11.0f, 3.0f, r.getHeight() - 22.0f, 1.5f);
        }

        auto text = r.reduced (18.0f, 0.0f);
        g.setColour (open ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (14.0f, open));
        g.drawText (getButtonText(), text, juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (12.0f));
        g.drawText (juce::String (count), text, juce::Justification::centredRight, false);
    }

private:
    int count = 0;
};

//==============================================================================
/** A small arc that turns while something runs in the background (the Tidy up button). */
class BusySpinner : public juce::Component, private juce::Timer
{
public:
    BusySpinner() { setInterceptsMouseClicks (false, false); }
    ~BusySpinner() override { stopTimer(); }

    void setRunning (bool on)
    {
        setVisible (on);

        if (on)
            startTimerHz (30);
        else
            stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);

        juce::Path arc;
        arc.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() * 0.5f, r.getHeight() * 0.5f, 0.0f,
                           angle, angle + juce::MathConstants<float>::pi * 1.4f, true);

        g.setColour (Theme::accent);
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    void timerCallback() override
    {
        angle = std::fmod (angle + 0.35f, juce::MathConstants<float>::twoPi);
        repaint();
    }

    float angle = 0.0f;
};

//==============================================================================
/** The Library: everything in Documents/tini in four sections (Amps, Pedals, Cabs, Other models).

    Left, Add packs above the sections with their file counts. Right, a search field with Open folder, the
    files of the open section (name and what the file says about itself, a dot on the ones in use) and
    under them the actions for the selected file:

        Amps      Load to amp
        Pedals    Add to chain       (a NAM pedal named after the model)
        Cabs      Load to cab
        Other     Load to amp, Add to chain
        all       Edit (name, make, model, type, tone; a cab is only renamed), Show in file manager, Delete

    The page does not know what "load" means for the rest of the app: MainView hands it the callbacks. */
class LibraryPage : public juce::Component, private juce::Timer
{
public:
    using Kind = FileLibrary::Kind;

    explicit LibraryPage (TiniAudioProcessor& p)
        : processor (p), list (p, Kind::amps, "Load to amp"), filterBar (list, true)
    {
        for (auto k : FileLibrary::allKinds)
        {
            auto* button = sections.add (new SectionButton (sectionName (k), 7001));
            button->onClick = [this, k] { if (sectionFor (k).getToggleState()) showSection (k); };
            addAndMakeVisible (button);
        }

        list.onSelected = [this] (const juce::File&) { updateActions(); repaint(); };
        list.onPick = [this] (const juce::File& file) { runPrimary (file); };
        list.isLoaded = [this] (const juce::File& file) { return isInUse (file); };
        list.onRenamed = [this] (const juce::File& oldFile, const juce::File& newFile) { fileRenamed (oldFile, newFile); };
        list.onMoved = [this] (const juce::File& oldFile, const juce::File& newFile, Kind)
        {
            fileRenamed (oldFile, newFile);   // a model in use keeps being used from its new place
        };
        list.onMovedAll = [this] { countFiles(); updateActions(); };
        list.onFilesDropped = [this] (const juce::Array<juce::File>& dropped) { addSortedChecked (dropped); };
        list.onDeleted = [this] (const juce::File&) { updateActions(); countFiles(); };
        list.onEdited = [this] (const juce::File&) { updateActions(); repaint(); };
        list.onAdded = [this] (const juce::Array<juce::File>&) { countFiles(); };
        addAndMakeVisible (list);
        addAndMakeVisible (filterBar);

        searchBox.setOpaque (false);   // rounded corners
        searchBox.setTextToShowWhenEmpty ("Search", Theme::textLo);
        searchBox.setFont (Theme::ui (13.0f));
        searchBox.setIndents (10, 7);
        searchBox.onTextChange = [this] { list.setSearch (searchBox.getText()); updateActions(); };
        addAndMakeVisible (searchBox);

        addButton.setButtonText ("Add packs");
        addButton.onClick = [this] { chooseFilesToAdd(); };
        folderButton.setButtonText ("Open folder");
        folderButton.setTooltip ("Show this folder in the file manager");
        folderButton.onClick = [this] { FileLibrary::ensureFolders(); list.getFolder().startAsProcess(); };

        primaryButton.onClick = [this] { if (list.getSelectedFile() != juce::File()) runPrimary (list.getSelectedFile()); };
        secondaryButton.setButtonText ("Add to chain");
        secondaryButton.onClick = [this] { if (list.getSelectedFile() != juce::File() && onAddToChain) onAddToChain (list.getSelectedFile()); };
        renameButton.setButtonText ("Edit");
        renameButton.setTooltip ("Change the name and what the file says about itself");
        renameButton.onClick = [this] { if (list.getSelectedFile() != juce::File()) list.askEdit (list.getSelectedFile()); };
        revealButton.setButtonText ("Show");
        revealButton.setTooltip ("Show the file in the file manager");
        revealButton.onClick = [this] { if (list.getSelectedFile() != juce::File()) list.getSelectedFile().revealToUser(); };
        deleteButton.setButtonText ("Delete");
        deleteButton.onClick = [this] { if (list.getSelectedFile() != juce::File()) list.askDelete (list.getSelectedFile()); };

        tidyButton.setButtonText ("Tidy up");
        tidyButton.setTooltip ("Put models in the right section and keep captures of one make and model together in a pack");
        tidyButton.onClick = [this] { askTidy(); };

        for (auto* b : { &addButton, &tidyButton, &folderButton, &primaryButton, &secondaryButton, &renameButton, &revealButton, &deleteButton })
            addAndMakeVisible (b);

        addChildComponent (spinner);   // above the buttons

        sections[0]->setToggleState (true, juce::dontSendNotification);
        showSection (Kind::amps);
        startTimerHz (2);
    }

    ~LibraryPage() override { stopTimer(); }

    //==========================================================================
    std::function<void (const juce::File&)> onLoadToAmp, onAddToChain, onLoadToCab;

    Kind getSection() const { return section; }
    ModelList& getList() { return list; }
    FilterBar& getFilterBar() { return filterBar; }
    juce::TextEditor& getSearchBox() { return searchBox; }
    SectionButton& getSectionButton (Kind k) { return sectionFor (k); }

    juce::TextButton& getPrimaryButton() { return primaryButton; }
    juce::TextButton& getSecondaryButton() { return secondaryButton; }
    juce::TextButton& getRenameButton() { return renameButton; }
    juce::TextButton& getDeleteButton() { return deleteButton; }
    juce::TextButton& getAddButton() { return addButton; }

    void showSection (Kind kind)
    {
        section = kind;
        sectionFor (kind).setToggleState (true, juce::dontSendNotification);

        searchBox.setText ({}, false);
        list.setSearch ({});
        list.clearFilters();
        list.setKind (kind);
        list.setPrimaryLabel (primaryLabel (kind));
        list.setEmptyText (emptyText (kind));

        countFiles();
        updateActions();
        repaint();
    }

    /** Whenever the page is opened the folders are read again (files may have come and gone meanwhile). */
    void visibilityChanged() override
    {
        if (isVisible())
        {
            list.rescan();
            countFiles();
            updateActions();
        }
    }

    /** Called from the UI timer: the dots follow what is loaded. */
    void refresh()
    {
        const auto sig = usageSignature();
        if (sig != shownUsage)
        {
            shownUsage = sig;

            if (list.getFilter().onlyInUse)
                list.refilter();   // what is in use changed: so does the list
            else
                list.repaintRows();
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        Theme::drawPanel (g, getLocalBounds().toFloat());

        // the section column and the list are separated by a line
        g.setColour (Theme::divider);
        g.fillRect (juce::Rectangle<int> (padding + columnWidth + gap / 2, padding, 1, getHeight() - 2 * padding));

        auto right = rightArea();
        auto head = right.removeFromTop (headerHeight);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (16.0f, true));
        g.drawText (FileLibrary::title (section), head.removeFromLeft (200), juce::Justification::centredLeft, false);

        // the strip under the list
        auto strip = right.removeFromBottom (stripHeight);
        Theme::drawInset (g, strip.toFloat(), 8.0f);

        auto text = strip.reduced (16, 10).withTrimmedRight (stripButtonsWidth());
        const auto file = list.getSelectedFile();

        if (file == juce::File())
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.5f));
            g.drawFittedText (list.getNumFiles() > 0 ? "Select a file to see what you can do with it." : "", text,
                              juce::Justification::centredLeft, 2);
            return;
        }

        const auto info = section == Kind::cabs ? NamMetadata::Info() : processor.metadata.get (file);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (14.0f, true));
        g.drawText (file.getFileNameWithoutExtension(), text.removeFromTop (22), juce::Justification::centredLeft, true);

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (12.0f));
        g.drawText (list.detailOf (file), text.removeFromTop (18), juce::Justification::centredLeft, true);

        const auto sep = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        juce::StringArray facts;
        if (info.gearType.isNotEmpty())
            facts.add (gearTypeLabel (info.gearType));
        facts.add (juce::String (juce::roundToInt ((double) file.getSize() / 1024.0)) + " KB");

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (facts.joinIntoString (sep), text, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (padding);

        auto column = b.removeFromLeft (columnWidth);
        addButton.setBounds (column.removeFromTop (headerHeight).withSizeKeepingCentre (columnWidth, 30));   // level with the title on the right
        for (auto* s : sections)
        {
            s->setBounds (column.removeFromTop (SectionButton::height));
            column.removeFromTop (2);
        }

        tidyButton.setBounds (column.removeFromBottom (30));
        spinner.setBounds (juce::Rectangle<int> (16, 16).withCentre ({ tidyButton.getX() + 20, tidyButton.getBounds().getCentreY() }));

        auto right = rightArea();
        auto head = right.removeFromTop (headerHeight);
        folderButton.setBounds (head.removeFromRight (88).withSizeKeepingCentre (88, 28));
        head.removeFromRight (12);
        searchBox.setBounds (head.removeFromRight (juce::jmin (260, head.getWidth() - 200)).withSizeKeepingCentre (juce::jmin (260, head.getWidth() - 200), 30));

        filterBar.setBounds (right.removeFromTop (FilterBar::height));
        right.removeFromTop (4);

        auto strip = right.removeFromBottom (stripHeight);
        right.removeFromBottom (10);
        list.setBounds (right);

        // buttons of the strip: the file's own actions on top, the housekeeping under them
        auto buttons = strip.reduced (12, 10).removeFromRight (stripButtonsWidth() - 12);
        auto top = buttons.removeFromTop (30);
        buttons.removeFromTop (8);
        auto bottom = buttons.removeFromTop (30);

        const int w = 118;
        if (secondaryButton.isVisible())
        {
            secondaryButton.setBounds (top.removeFromRight (w));
            top.removeFromRight (6);
        }
        primaryButton.setBounds (top.removeFromRight (w));

        deleteButton.setBounds (bottom.removeFromRight (72));
        bottom.removeFromRight (6);
        revealButton.setBounds (bottom.removeFromRight (60));
        bottom.removeFromRight (6);
        renameButton.setBounds (bottom.removeFromRight (72));
    }

private:
    static constexpr int padding = 24, columnWidth = 200, gap = 32, headerHeight = 48, stripHeight = 84;

    int stripButtonsWidth() const { return 12 + 118 * 2 + 6 + 12; }

    juce::Rectangle<int> rightArea() const
    {
        auto b = getLocalBounds().reduced (padding);
        b.removeFromLeft (columnWidth + gap);
        return b;
    }

    static juce::String sectionName (Kind k)
    {
        switch (k)
        {
            case Kind::amps:   return "Amps";
            case Kind::pedals: return "Pedals";
            case Kind::cabs:   return "Cabs";
            case Kind::other:  return "Other models";
        }
        return {};
    }

    static juce::String primaryLabel (Kind k)
    {
        switch (k)
        {
            case Kind::amps:
            case Kind::other:  return "Load to amp";
            case Kind::pedals: return "Add to chain";
            case Kind::cabs:   return "Load to cab";
        }
        return {};
    }

    static juce::String emptyText (Kind k)
    {
        switch (k)
        {
            case Kind::amps:   return "No amp models yet.\nDrop .nam files or zip packs here or use Add packs. Models whose gear type is amp or preamp are sorted in here by themselves.";
            case Kind::pedals: return "No pedal captures yet.\nDrop .nam files or zip packs here or use Add packs. Each one becomes a pedal named after its model.";
            case Kind::cabs:   return "No cabinet impulse responses yet.\nDrop .wav files or zip packs here or use Add packs.";
            case Kind::other:  return "Nothing here yet.\nModels that are neither an amp nor a pedal (amp with cab, studio chains) are kept here.";
        }
        return {};
    }

    static juce::String gearTypeLabel (const juce::String& gearType) { return LibraryFilter::prettify (gearType); }

    SectionButton& sectionFor (Kind k) { return *sections[(int) k]; }

    /** A model without metadata goes where the open section is, if that is a model section. */
    Kind namDefault() const { return FileLibrary::isModelKind (section) ? section : Kind::amps; }

    /** The Tidy up button shows what is going on while the work runs in the background. */
    void setBusy (bool on, const juce::String& text)
    {
        busy = on;
        tidyButton.setEnabled (! on);
        tidyButton.setButtonText (on ? text : juce::String ("Tidy up"));
        spinner.setRunning (on);
        resized();
    }

    /** Which buttons the open section offers; they need a selected file to do anything. */
    void updateActions()
    {
        const bool hasFile = list.getSelectedFile() != juce::File();

        primaryButton.setButtonText (primaryLabel (section));
        renameButton.setButtonText (section == Kind::cabs ? "Rename" : "Edit");
        secondaryButton.setVisible (section == Kind::other);

        for (auto* b : { &primaryButton, &secondaryButton, &renameButton, &revealButton, &deleteButton })
            b->setEnabled (hasFile);

        resized();
        repaint();
    }

    void countFiles()
    {
        for (auto k : FileLibrary::allKinds)
            sectionFor (k).setCount (FileLibrary::listTree (FileLibrary::libraryFolder (k), FileLibrary::wildcardFor (k)).size());
    }

    void runPrimary (const juce::File& file)
    {
        switch (section)
        {
            case Kind::amps:
            case Kind::other:  if (onLoadToAmp)  onLoadToAmp (file);  break;
            case Kind::pedals: if (onAddToChain) onAddToChain (file); break;
            case Kind::cabs:   if (onLoadToCab)  onLoadToCab (file);  break;
        }
    }

    /** A file in use keeps being used when it is renamed here: the loaded copy follows the new path. */
    void fileRenamed (const juce::File& oldFile, const juce::File& newFile)
    {
        if (processor.nam.getFile() == oldFile)
            processor.loadNamModel (newFile);

        if (processor.loadedIrFile == oldFile)
            processor.loadImpulseResponse (newFile);

        for (int copy = 0; copy < PedalCatalog::namCopies; ++copy)
            if (processor.pedalNam[(size_t) copy].getFile() == oldFile)
                processor.loadPedalModel (copy, newFile);

        updateActions();
    }

    bool isInUse (const juce::File& file) const
    {
        if (section == Kind::cabs)
            return processor.loadedIrFile == file;

        if (processor.nam.getFile() == file && processor.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f)
            return true;

        for (int copy = 0; copy < PedalCatalog::namCopies; ++copy)
            if (processor.pedalNam[(size_t) copy].getFile() == file
                && ChainStrip::isInChain (processor.apvts, PedalCatalog::makeItem (PedalCatalog::captureType, copy)))
                return true;

        return false;
    }

    juce::String usageSignature() const
    {
        juce::String sig = processor.nam.getFile().getFullPathName() + "|" + processor.loadedIrFile.getFullPathName()
                         + "|" + juce::String (processor.apvts.getRawParameterValue ("AMP_TYPE")->load());

        for (int copy = 0; copy < PedalCatalog::namCopies; ++copy)
            sig += "|" + processor.pedalNam[(size_t) copy].getFile().getFullPathName()
                 + (ChainStrip::isInChain (processor.apvts, PedalCatalog::makeItem (PedalCatalog::captureType, copy)) ? "+" : "-");

        return sig;
    }

    /** Add packs: models, impulse responses and zip packs alike. Every file is sorted by itself (a model by the gear
        type in its metadata, an audio file is a cab); the section of the file that came first opens. */
    void chooseFilesToAdd()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Add models (.nam), impulse responses or a zip pack",
                                                           FileLibrary::downloadsFolder(),
                                                           FileLibrary::modelWildcard + ";" + FileLibrary::irWildcard + ";*.zip");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<LibraryPage> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->addSortedChecked (fc.getResults());
        });
    }

public:
    /** Copies the files into the section they belong to; zips and folders keep their captures together in
        a pack folder. Returns how many came in. */
    int addSorted (const juce::Array<juce::File>& sources)
    {
        const FileLibrary::Batch batch (sources);
        return addBatch (batch, false);
    }

    /** The same, but asks first if something is in the library already. */
    void addSortedChecked (const juce::Array<juce::File>& sources)
    {
        auto batch = std::make_shared<FileLibrary::Batch> (sources);
        const auto report = FileLibrary::inspect (*batch, nullptr, namDefault());

        FileActions::confirmImport (this, report, {}, [safe = juce::Component::SafePointer<LibraryPage> (this), batch] (bool again)
        {
            if (safe != nullptr)
                safe->addBatch (*batch, again);
        });
    }

    int addBatch (const FileLibrary::Batch& batch, bool again)
    {
        const auto done = FileLibrary::importBatch (batch, nullptr, namDefault(), again);

        if (! done.empty())
        {
            const auto firstKind = done.front().kind;
            showSection (firstKind);
            list.rescan();   // the section may be the one that was already open
            countFiles();

            juce::Array<juce::File> added;
            for (const auto& item : done)
                if (item.kind == firstKind)
                    added.add (item.file);

            list.highlightFiles (added);   // show where it all went
            updateActions();
        }

        return (int) done.size();
    }

    /** Looks for what is out of order (models in the wrong section, loose captures of one make and model) and
        offers to put it right. Looking means opening every model file, so it runs in the background while a
        spinner shows on the button. */
    void askTidy()
    {
        if (busy)
            return;

        setBusy (true, "Looking...");

        juce::Thread::launch ([safe = juce::Component::SafePointer<LibraryPage> (this)]
        {
            auto moves = FileLibrary::planTidy();

            juce::MessageManager::callAsync ([safe, moves = std::move (moves)]
            {
                if (safe != nullptr)
                    safe->offerTidy (moves);
            });
        });
    }

    bool isBusy() const { return busy; }

    /** The answer of the background look: say what would change and ask. */
    void offerTidy (const std::vector<FileLibrary::TidyMove>& moves)
    {
        setBusy (false, {});

        int sections = 0, grouped = 0;
        std::set<juce::String> packs;
        for (const auto& m : moves)
        {
            sections += m.changesSection ? 1 : 0;
            if (m.pack.isNotEmpty())
            {
                ++grouped;
                packs.insert (juce::String ((int) m.kind) + "|" + m.pack);
            }
        }

        if (moves.empty())
        {
            auto info = std::make_unique<FormDialog> ("Tidy up", "Everything is in order: every model is in its section and nothing needs a pack.", "OK");
            info->onSubmit = [] (FormDialog&) { return juce::Result::ok(); };
            FormDialog::show (std::move (info), this);
            return;
        }

        juce::StringArray lines;
        if (sections > 0)
            lines.add ("move " + juce::String (sections) + (sections == 1 ? " model" : " models") + " to the section its metadata says");
        if (grouped > 0)
            lines.add ("put " + juce::String (grouped) + " files into " + juce::String ((int) packs.size()) + (packs.size() == 1 ? " pack folder" : " pack folders"));

        auto dialog = std::make_unique<FormDialog> ("Tidy up the library", "This will " + lines.joinIntoString (" and ") + ". Nothing is deleted.", "Tidy up");
        dialog->onSubmit = [safe = juce::Component::SafePointer<LibraryPage> (this), moves] (FormDialog&)
        {
            if (safe != nullptr)
                safe->applyTidy (moves);
            return juce::Result::ok();
        };
        FormDialog::show (std::move (dialog), this);
    }

    /** Does the moves in the background; the page follows when they are done. */
    void applyTidy (const std::vector<FileLibrary::TidyMove>& moves)
    {
        setBusy (true, "Tidying up...");

        juce::Thread::launch ([safe = juce::Component::SafePointer<LibraryPage> (this), moves]
        {
            std::vector<std::pair<juce::File, juce::File>> moved;

            for (const auto& m : moves)
            {
                const auto now = FileLibrary::moveTo (m.file, m.kind, m.pack);
                if (now.existsAsFile() && now != m.file)
                    moved.emplace_back (m.file, now);
            }

            juce::MessageManager::callAsync ([safe, moved = std::move (moved)]
            {
                if (safe != nullptr)
                    safe->tidyDone (moved);
            });
        });
    }

    void tidyDone (const std::vector<std::pair<juce::File, juce::File>>& moved)
    {
        for (const auto& m : moved)
            fileRenamed (m.first, m.second);   // a model in use keeps being used from its new place

        setBusy (false, {});
        list.rescan();
        countFiles();
        updateActions();
    }

private:
    /** The counts also follow files that came and went in the file manager. */
    void timerCallback() override
    {
        if (isShowing())
            countFiles();
    }

    TiniAudioProcessor& processor;
    Kind section = Kind::amps;
    juce::OwnedArray<SectionButton> sections;
    ModelList list;
    FilterBar filterBar;
    juce::TextEditor searchBox;
    BusySpinner spinner;
    bool busy = false;
    juce::TextButton addButton, tidyButton, folderButton, primaryButton, secondaryButton, renameButton, revealButton, deleteButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String shownUsage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPage)
};
