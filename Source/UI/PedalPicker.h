#pragma once

#include "Panels.h"
#include "ModelList.h"
#include "FilterBar.h"

//==============================================================================
/** What the "+" button opens in the block area: every pedal that can join the chain, in two parts.

    Built-in    one card per built-in pedal (dot in its category colour, knob names, how many copies are
                already in the chain).
    Your library    the NAM captures in the Pedals folder of the library, with a search field, Add files,
                Open folder and drop-to-add. Picking one puts a NAM pedal named after that model into
                the chain.

    A pick is reported through onPickBuiltIn / onPickFile; the owner closes the picker (picking opens the
    page of the new pedal). The calls come from the message loop, not from inside the click, so the owner
    may delete the picker in its handler. */
class PedalPicker : public juce::Component
{
public:
    explicit PedalPicker (NeuralFlowAmpAudioProcessor& p)
        : processor (p), library (p, FileLibrary::Kind::pedals, "Add to chain"), filterBar (library, false)
    {
        for (int type = 0; type < PedalCatalog::count; ++type)
        {
            if (PedalCatalog::all()[(size_t) type].fromLibrary)
                continue;

            auto* row = rows.add (new PedalRow (type, ChainStrip::copiesInChain (p.apvts, type)));
            row->onClick = [this, type]
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PedalPicker> (this), type]
                {
                    if (safe == nullptr)
                        return;

                    auto callback = safe->onPickBuiltIn;   // the handler may delete the picker
                    if (callback)
                        callback (type);
                });
            };
            addAndMakeVisible (row);
        }

        library.setPickOnClick (true);
        library.setEmptyText ("No pedal captures yet.\nDrop .nam files here or use Add files - the list follows the Pedals folder.");
        library.onPick = [this] (const juce::File& file) { pickFile (file); };
        library.isLoaded = [this] (const juce::File& file) { return inChain (file); };
        addAndMakeVisible (library);
        addAndMakeVisible (filterBar);

        searchBox.setOpaque (false);   // rounded corners
        searchBox.setTextToShowWhenEmpty ("Search pedals", Theme::textLo);
        searchBox.setFont (Theme::ui (13.0f));
        searchBox.setIndents (10, 7);
        searchBox.onTextChange = [this] { library.setSearch (searchBox.getText()); };
        addAndMakeVisible (searchBox);

        addButton.setButtonText ("Add files");
        addButton.setTooltip ("Copy .nam captures into the Pedals folder");
        addButton.onClick = [this] { chooseFilesToAdd(); };
        addAndMakeVisible (addButton);

        folderButton.setButtonText ("Open folder");
        folderButton.setTooltip ("Show the Pedals folder in the file manager");
        folderButton.onClick = [this]
        {
            FileLibrary::ensureFolders();
            library.getFolder().startAsProcess();
        };
        addAndMakeVisible (folderButton);

        namSlotsFree = ChainStrip::firstFreeCopy (p.apvts, PedalCatalog::captureType) >= 0;
    }

    /** The user picked a built-in pedal type. */
    std::function<void (int)> onPickBuiltIn;

    /** The user picked a NAM capture from the library. */
    std::function<void (const juce::File&)> onPickFile;

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        auto left = b.removeFromLeft (leftWidth());

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText ("Built-in", left.removeFromTop (headerHeight), juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText ("Drag blocks in the chain to reorder them", left.removeFromBottom (20), juce::Justification::centredLeft, false);

        g.setColour (Theme::divider);
        g.fillRect (juce::Rectangle<int> (b.getX() + gap / 2, 0, 1, getHeight()));

        b.removeFromLeft (gap);
        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText ("From your library", b.removeFromTop (headerHeight), juce::Justification::centredLeft, false);

        if (! namSlotsFree)
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (11.5f));
            g.drawText ("All " + juce::String (PedalCatalog::namCopies) + " NAM pedal slots are in the chain.",
                        b.removeFromBottom (20), juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto left = b.removeFromLeft (leftWidth());
        left.removeFromTop (headerHeight);

        const int colW = (left.getWidth() - 8) / 2;
        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (left.getX() + (i % 2) * (colW + 8), left.getY() + (i / 2) * (PedalRow::height + 4),
                                colW, PedalRow::height);

        b.removeFromLeft (gap);
        auto head = b.removeFromTop (headerHeight);
        folderButton.setBounds (head.removeFromRight (88).withSizeKeepingCentre (88, 26));
        head.removeFromRight (6);
        addButton.setBounds (head.removeFromRight (70).withSizeKeepingCentre (70, 26));

        searchBox.setBounds (b.removeFromTop (30));
        b.removeFromTop (6);
        filterBar.setBounds (b.removeFromTop (FilterBar::height));
        b.removeFromTop (2);

        if (! namSlotsFree)
            b.removeFromBottom (20);

        library.setBounds (b);
    }

    /** Card of a built-in pedal type, found by its "<NAME>_ADDED" id (e.g. "DELAY_ADDED"). */
    PedalRow* findRow (const juce::String& addedID) const
    {
        for (auto* row : rows)
            if (addedID == PedalCatalog::addedBaseId (row->getType()))
                return row;
        return nullptr;
    }

    // exposed so tests can drive them
    const juce::OwnedArray<PedalRow>& getRows() const { return rows; }
    ModelList& getLibrary() { return library; }
    FilterBar& getFilterBar() { return filterBar; }
    juce::TextEditor& getSearchBox() { return searchBox; }
    juce::TextButton& getAddButton() { return addButton; }
    juce::TextButton& getFolderButton() { return folderButton; }
    bool hasFreeNamSlot() const { return namSlotsFree; }

    /** Picks a library file, as a click on its line does. */
    void pickFile (const juce::File& file)
    {
        if (! namSlotsFree)
            return;

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PedalPicker> (this), file]
        {
            if (safe == nullptr)
                return;

            auto callback = safe->onPickFile;   // the handler may delete the picker
            if (callback)
                callback (file);
        });
    }

private:
    static constexpr int headerHeight = 34, gap = 32;

    int leftWidth() const { return juce::jmin (520, getWidth() / 2 - gap / 2); }

    /** True if a NAM pedal of the chain has this file loaded. */
    bool inChain (const juce::File& file) const
    {
        for (int copy = 0; copy < PedalCatalog::namCopies; ++copy)
            if (processor.pedalNam[(size_t) copy].getFile() == file
                && ChainStrip::isInChain (processor.apvts, PedalCatalog::makeItem (PedalCatalog::captureType, copy)))
                return true;

        return false;
    }

    void chooseFilesToAdd()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Add pedal captures (.nam)", FileLibrary::downloadsFolder(),
                                                           FileLibrary::modelWildcard);

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<PedalPicker> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->library.addFiles (fc.getResults());
        });
    }

    NeuralFlowAmpAudioProcessor& processor;
    juce::OwnedArray<PedalRow> rows;
    ModelList library;
    FilterBar filterBar;
    juce::TextEditor searchBox;
    juce::TextButton addButton, folderButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool namSlotsFree = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalPicker)
};
