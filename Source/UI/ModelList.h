#pragma once

#include "../PluginProcessor.h"
#include "Dialog.h"
#include "FileBrowser.h"
#include "LibraryFilter.h"

//==============================================================================
/** A list of the files in one library folder (Amps, Pedals, Cabs or Other), two lines per file: the name
    and what the file says about itself (make · model · tone type for a model, rate · length for an IR).

    Used by the Library page and by the pedal picker. It follows its folder (about once a second while
    visible), can be filtered by a search text, and manages the files: right click offers the primary
    action, Rename, Show in the file manager and Delete (to the Recycle Bin); files dropped on it are copied
    into the folder.

    A click selects a line; with setPickOnClick (true) it also runs onPick, as Enter and a double click do. */
class ModelList : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::ListBoxModel,
                  private juce::Timer,
                  private juce::ChangeListener
{
public:
    static constexpr int rowHeight = 46;

    ModelList (NeuralFlowAmpAudioProcessor& p, FileLibrary::Kind libraryKind, juce::String primaryActionLabel)
        : processor (p), kind (libraryKind), primaryLabel (std::move (primaryActionLabel))
    {
        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getVerticalScrollBar().setAutoHide (true);
        addAndMakeVisible (list);

        processor.metadata.addChangeListener (this);
    }

    ~ModelList() override
    {
        processor.metadata.removeChangeListener (this);
        stopTimer();
        list.setModel (nullptr);
    }

    //==========================================================================
    /** The primary action: Enter, a double click, "<label>" in the menu, and a single click with setPickOnClick. */
    std::function<void (const juce::File&)> onPick;

    /** The selected line changed (single click or arrow keys); an empty File means nothing is selected. */
    std::function<void (const juce::File&)> onSelected;

    /** True for a file that is in use right now: it gets the accent dot. */
    std::function<bool (const juce::File&)> isLoaded;

    std::function<void (const juce::File& oldFile, const juce::File& newFile)> onRenamed;
    std::function<void (const juce::File&)> onDeleted;

    /** Called after what a model says about itself was changed (Edit); the file may have been renamed too. */
    std::function<void (const juce::File&)> onEdited;

    /** Called after files were copied in (with the files in the library); a drop or Add files. */
    std::function<void (const juce::Array<juce::File>&)> onAdded;

    void setPickOnClick (bool on) { pickOnClick = on; }

    /** Name of the primary action in the context menu (empty = no such entry). */
    void setPrimaryLabel (const juce::String& label) { primaryLabel = label; }

    /** Redraws the lines (a file became loaded or unloaded). */
    void repaintRows() { list.repaint(); }
    void setEmptyText (const juce::String& text) { emptyText = text; repaint(); }

    FileLibrary::Kind getKind() const { return kind; }
    juce::File getFolder() const { return FileLibrary::libraryFolder (kind); }

    /** Shows another folder of the library (the Library page's sections). */
    void setKind (FileLibrary::Kind newKind)
    {
        if (newKind == kind)
            return;

        kind = newKind;
        selected = {};
        files.clear();
        all.clear();

        // a section has other facets: the wanted values do not carry over (the order does, if it exists there)
        const auto order = filter.sort;
        const auto orders = sorts();
        filter = {};
        filter.sort = std::find (orders.begin(), orders.end(), order) != orders.end() ? order : Sort::name;
        rescan();

        if (onSelected)
            onSelected ({});
    }

    //==========================================================================
    // Filters and order. The facets on offer depend on the kind (see LibraryFilter::facetsFor).
    using Facet = LibraryFilter::Facet;
    using Sort  = LibraryFilter::Sort;

    /** Called after the shown list was rebuilt (search, filter, new files, metadata that arrived). */
    std::function<void()> onFiltersChanged;

    std::vector<Facet> facets() const { return LibraryFilter::facetsFor (kind); }
    std::vector<Sort> sorts() const   { return LibraryFilter::sortsFor (kind); }
    const LibraryFilter::State& getFilter() const { return filter; }

    /** The choices of a facet with their counts: how many files each has among those that pass all the
        other filters (and the search), so the numbers show what ticking a choice would leave. */
    std::vector<LibraryFilter::Option> getFacetOptions (Facet facet)
    {
        const auto offered = facets();
        std::map<juce::String, LibraryFilter::Option> byKey;

        for (const auto& file : all)
        {
            const auto facts = factsOf (file);
            if (! matchesSearch (file) || ! LibraryFilter::matches (filter, facts, inUse (file), offered, &facet))
                continue;

            const auto value = LibraryFilter::valueOf (facet, facts);
            auto& option = byKey[value.key];
            if (option.label.isEmpty())
                option = { value.key, value.label, 0 };
            ++option.count;
        }

        return LibraryFilter::orderedOptions (facet, std::move (byKey), filter);
    }

    void toggleFilter (Facet facet, const juce::String& key)
    {
        filter.toggle (facet, key);
        filterEdited (facet);
    }

    void setFilter (Facet facet, std::set<juce::String> keys)
    {
        filter.set (facet, std::move (keys));
        filterEdited (facet);
    }

    void setOnlyInUse (bool on)
    {
        if (filter.onlyInUse != on)
        {
            filter.onlyInUse = on;
            rebuildVisible();
        }
    }

    void setSort (Sort order)
    {
        if (filter.sort != order)
        {
            filter.sort = order;
            rebuildVisible();
        }
    }

    void clearFilters()
    {
        if (filter.isActive())
        {
            filter.clear();
            rebuildVisible();
        }
    }

    /** Applies search, filters and order again (what "in use" means changed). */
    void refilter() { rebuildVisible(); }

    /** True if the search or a filter narrows the list. */
    bool isNarrowed() const { return search.isNotEmpty() || filter.isActive(); }

    /** Only lines that contain `text` in their name or in what they say about themselves are shown. */
    void setSearch (const juce::String& text)
    {
        if (text.trim() == search)
            return;

        search = text.trim();
        rebuildVisible();
    }

    //==========================================================================
    int getNumRows() override { return files.size(); }
    int getNumFiles() const { return files.size(); }
    int getNumInFolder() const { return all.size(); }
    juce::File getFile (int row) const { return files[row]; }
    juce::File getSelectedFile() const { return selected; }

    /** Row of `file` in the shown list, or -1. */
    int rowOf (const juce::File& file) const { return files.indexOf (file); }

    void selectFile (const juce::File& file)
    {
        const int row = files.indexOf (file);
        if (row >= 0)
            list.selectRow (row);
    }

    /** Reads the folder again. */
    void rescan()
    {
        FileLibrary::ensureFolders();
        all = FileLibrary::list (getFolder(), FileLibrary::wildcardFor (kind));

        // the filters offer what every file says about itself, not only the lines on screen: start reading them
        // all in the background (the cache answers at once for files it already knows)
        if (kind != FileLibrary::Kind::cabs)
            for (const auto& file : all)
                processor.metadata.get (file);

        rebuildVisible();
    }

    /** Looks at the folder once; the list only changes when its content did. Runs by itself while visible. */
    void refreshIfChanged()
    {
        if (FileLibrary::list (getFolder(), FileLibrary::wildcardFor (kind)) != all)
            rescan();
    }

    /** Copies `sources` into the folder of this list. Returns how many were added. */
    int addFiles (const juce::Array<juce::File>& sources)
    {
        FileLibrary::ensureFolders();

        juce::Array<juce::File> arrived;
        for (const auto& source : sources)
        {
            if (! source.existsAsFile() || ! FileLibrary::fits (source, kind))
                continue;

            const auto copy = FileLibrary::importFile (source, kind);
            if (copy.existsAsFile())
                arrived.add (copy);
        }

        if (! arrived.isEmpty())
        {
            rescan();

            if (onAdded)
                onAdded (arrived);
        }

        return arrived.size();
    }

    /** Renames a file of the list (keeps its extension). */
    juce::Result renameFile (const juce::File& file, const juce::String& newBaseName)
    {
        juce::File renamed;
        const auto result = FileLibrary::renameFile (file, newBaseName, renamed);

        if (result.wasOk() && renamed != file)
        {
            if (selected == file)
                selected = renamed;

            rescan();

            if (onRenamed)
                onRenamed (file, renamed);
        }

        return result;
    }

    /** Removes a file of the list (Recycle Bin / Trash). */
    bool deleteFile (const juce::File& file)
    {
        if (! FileLibrary::removeFile (file))
            return false;

        if (selected == file)
            selected = {};

        rescan();

        if (onDeleted)
            onDeleted (file);

        return true;
    }

    /** The context menu of a row (the tests drive the actions directly). */
    void showMenu (int row)
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        const auto file = files[row];

        juce::PopupMenu menu;
        if (primaryLabel.isNotEmpty())
        {
            menu.addItem (1, primaryLabel);
            menu.addSeparator();
        }
        menu.addItem (2, kind == FileLibrary::Kind::cabs ? "Rename..." : "Edit info...");
        menu.addItem (3, "Show in file manager");
        menu.addItem (4, "Delete...");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ModelList> (this), file] (int choice)
        {
            if (safe == nullptr)
                return;

            switch (choice)
            {
                case 1: safe->pick (file); break;
                case 2: safe->askEdit (file); break;
                case 3: file.revealToUser(); break;
                case 4: safe->askDelete (file); break;
                default: break;
            }
        });
    }

    /** Models: a dialog for the name and for what the file says about itself (make, model, type, tone).
        Impulse responses have nothing to say about themselves: they only get renamed. */
    void askEdit (const juce::File& file)
    {
        if (kind == FileLibrary::Kind::cabs)
        {
            askRename (file);
            return;
        }

        const auto info = processor.metadata.getNow (file);

        auto dialog = std::make_unique<FormDialog> ("Edit info", file.getFileName(), "Save");
        dialog->addText ("name", "Name", file.getFileNameWithoutExtension());
        dialog->addText ("make", "Make", info.make, "e.g. Mesa/Boogie");
        dialog->addText ("model", "Model", info.model, "e.g. Dual Rectifier");

        std::vector<ChipRow::Choice> types, tones;
        for (const auto& c : NamMetadata::gearTypes) types.push_back ({ c.value, c.label });
        for (const auto& c : NamMetadata::toneTypes) tones.push_back ({ c.value, c.label });

        dialog->addChoices ("type", "Type", std::move (types), info.gearType);
        dialog->addChoices ("tone", "Tone", std::move (tones), info.toneType);

        // the file can be renamed even when the metadata then fails: the dialog follows it
        auto current = std::make_shared<juce::File> (file);

        dialog->onSubmit = [safe = juce::Component::SafePointer<ModelList> (this), current] (FormDialog& d)
        {
            return safe != nullptr ? safe->saveEdit (*current, d.getText ("name"), d.getText ("make"), d.getText ("model"),
                                                      d.getChoice ("type"), d.getChoice ("tone"))
                                   : juce::Result::ok();
        };

        FormDialog::show (std::move (dialog), this);
    }

    /** Renames the file and writes the make, model, type and tone into its metadata. */
    juce::Result saveEdit (juce::File& file, const juce::String& newName, const juce::String& make,
                           const juce::String& model, const juce::String& gearType, const juce::String& toneType)
    {
        juce::File target = file;

        // the name first: a name that is refused changes nothing
        if (newName.trim() != file.getFileNameWithoutExtension())
        {
            const auto renamed = FileLibrary::renameFile (file, newName, target);
            if (renamed.failed())
                return renamed;
        }

        NamMetadata::Info info;
        info.make = make;
        info.model = model;
        info.gearType = gearType;
        info.toneType = toneType;

        // the file is only rewritten if the metadata changed (which also changes its time stamp)
        const auto written = NamMetadata::write (target, info);
        const auto before = file;
        file = target;

        if (target != before)
        {
            if (selected == before)
                selected = target;

            if (onRenamed)
                onRenamed (before, target);
        }

        processor.metadata.getNow (target);   // read it again now: the lists and the details show it at once
        rescan();

        if (onEdited)
            onEdited (target);

        return written;
    }

    void askRename (const juce::File& file)
    {
        FileActions::askRename (this, file, [safe = juce::Component::SafePointer<ModelList> (this), file] (const juce::String& name)
        {
            return safe != nullptr ? safe->renameFile (file, name) : juce::Result::ok();
        });
    }

    void askDelete (const juce::File& file)
    {
        FileActions::askDelete (this, file, [safe = juce::Component::SafePointer<ModelList> (this), file]
        {
            return safe == nullptr || safe->deleteFile (file);
        });
    }

    //==========================================================================
    void visibilityChanged() override
    {
        if (isVisible())
        {
            rescan();
            startTimer (1000);
        }
        else
        {
            stopTimer();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (files.isEmpty())
        {
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.0f));

            juce::String text = emptyText;

            if (! all.isEmpty())
            {
                if (filter.isActive())
                    text = search.isEmpty() ? "Nothing matches the filters." : juce::String ("Nothing matches \"") + search + "\" and the filters.";
                else
                    text = juce::String ("Nothing matches \"") + search + "\".";
            }

            g.drawFittedText (text, getLocalBounds().reduced (6, 8), juce::Justification::topLeft, 6);
        }

        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f, 1.5f);
        }
    }

    void resized() override { list.setBounds (getLocalBounds()); }

    //==========================================================================
    bool isInterestedInFileDrag (const juce::StringArray& dragged) override { return FileActions::anyFits (dragged, kind); }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override            { setDropHighlight (false); }

    void filesDropped (const juce::StringArray& dropped, int, int) override
    {
        setDropHighlight (false);

        juce::Array<juce::File> sources;
        for (const auto& path : dropped)
            sources.add (juce::File (path));

        addFiles (sources);
    }

    /** The second line of a row. */
    juce::String detailOf (const juce::File& file)
    {
        if (kind == FileLibrary::Kind::cabs)
            return audioInfo (file);

        return processor.metadata.get (file).detailLine();
    }

private:
    void timerCallback() override
    {
        if (isShowing())
            refreshIfChanged();
    }

    /** Metadata arrived: lines can change their text and the search may match more. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // the filters and the order read the metadata too: files move between "Unknown" and their values
        if (search.isNotEmpty() || filter.isActive() || filter.sort == Sort::make)
        {
            rebuildVisible();
        }
        else
        {
            list.repaint();

            if (onFiltersChanged)
                onFiltersChanged();
        }
    }

    bool inUse (const juce::File& file) const { return isLoaded && isLoaded (file); }

    /** What the filters know about a file: its metadata (models) or its header (impulse responses). */
    LibraryFilter::Facts factsOf (const juce::File& file)
    {
        LibraryFilter::Facts facts;

        if (kind == FileLibrary::Kind::cabs)
            facts.ir = irFacts (file);
        else
            facts.info = processor.metadata.get (file);

        return facts;
    }

    void filterEdited (Facet facet)
    {
        if (facet == Facet::make)
            pruneModels();

        rebuildVisible();
    }

    /** Models that belong to none of the wanted makes cannot match any more: they are un-wanted. */
    void pruneModels()
    {
        const auto wanted = filter.selected.find (Facet::model);
        if (wanted == filter.selected.end())
            return;

        LibraryFilter::State makesOnly;
        if (filter.selected.count (Facet::make) > 0)
            makesOnly.selected[Facet::make] = filter.selected[Facet::make];

        std::set<juce::String> present;
        for (const auto& file : all)
        {
            const auto facts = factsOf (file);
            if (LibraryFilter::matches (makesOnly, facts, false, { Facet::make }))
                present.insert (LibraryFilter::valueOf (Facet::model, facts).key);
        }

        std::set<juce::String> kept;
        for (const auto& key : wanted->second)
            if (present.count (key) > 0)
                kept.insert (key);

        filter.set (Facet::model, std::move (kept));
    }

    /** Puts `shown` in the order of the filter. Name order needs no facts; the others read them once per file. */
    void sortShown (juce::Array<juce::File>& shown)
    {
        struct Item
        {
            juce::File file;
            juce::String make, model;
            juce::int64 modified = 0;
            double length = 0.0;
        };

        std::vector<Item> items;
        items.reserve ((size_t) shown.size());

        for (const auto& file : shown)
        {
            Item item { file, {}, {}, 0, 0.0 };

            if (filter.sort == Sort::make)
            {
                const auto info = processor.metadata.get (file);
                item.make = info.make.trim();
                item.model = info.model.trim();
            }
            else if (filter.sort == Sort::newest)
            {
                item.modified = file.getLastModificationTime().toMilliseconds();
            }
            else if (filter.sort == Sort::length)
            {
                const auto ir = irFacts (file);
                item.length = ir.found ? ir.lengthMs : 1.0e12;   // unknown last
            }

            items.push_back (std::move (item));
        }

        auto byName = [] (const Item& a, const Item& b)
        {
            return a.file.getFileNameWithoutExtension().compareNatural (b.file.getFileNameWithoutExtension()) < 0;
        };

        std::stable_sort (items.begin(), items.end(), [&] (const Item& a, const Item& b)
        {
            switch (filter.sort)
            {
                case Sort::make:
                {
                    if (a.make.isEmpty() != b.make.isEmpty())
                        return b.make.isEmpty();   // without a make last

                    if (const int c = a.make.compareIgnoreCase (b.make))   return c < 0;
                    if (const int c = a.model.compareIgnoreCase (b.model)) return c < 0;
                    return byName (a, b);
                }
                case Sort::newest: return a.modified != b.modified ? a.modified > b.modified : byName (a, b);
                case Sort::length: return a.length != b.length ? a.length < b.length : byName (a, b);
                case Sort::name:   break;
            }
            return byName (a, b);
        });

        shown.clearQuick();
        for (const auto& item : items)
            shown.add (item.file);
    }

    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint();
        }
    }

    bool matchesSearch (const juce::File& file)
    {
        if (search.isEmpty())
            return true;

        return file.getFileNameWithoutExtension().containsIgnoreCase (search)
            || detailOf (file).containsIgnoreCase (search);
    }

    void rebuildVisible()
    {
        juce::Array<juce::File> shown;
        const auto offered = facets();

        for (const auto& file : all)
            if (matchesSearch (file)
                && (! filter.isActive() || LibraryFilter::matches (filter, factsOf (file), inUse (file), offered)))
                shown.add (file);

        sortShown (shown);

        const bool same = shown == files;
        files = std::move (shown);

        if (! files.contains (selected))
            selected = {};

        if (! same)
            list.updateContent();

        const int row = files.indexOf (selected);
        if (row >= 0)
            list.selectRow (row, true, false);
        else
            list.deselectAllRows();

        list.repaint();
        repaint();

        if (onFiltersChanged)
            onFiltersChanged();
    }

    /** The header of an impulse response, read once per file version. */
    LibraryFilter::IrFacts irFacts (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        const auto stamp = file.getLastModificationTime().toMilliseconds();
        const auto it = irInfos.find (key);

        if (it != irInfos.end() && it->second.first == stamp)
            return it->second.second;

        LibraryFilter::IrFacts facts;
        std::unique_ptr<juce::AudioFormatReader> reader (processor.formatManager.createReaderFor (file));

        if (reader != nullptr && reader->sampleRate > 0.0)
        {
            facts.found = true;
            facts.sampleRate = reader->sampleRate;
            facts.lengthMs = 1000.0 * (double) reader->lengthInSamples / reader->sampleRate;
            facts.channels = (int) reader->numChannels;
        }

        irInfos[key] = { stamp, facts };
        return facts;
    }

    /** "48.0 kHz · 85 ms" of an impulse response. */
    juce::String audioInfo (const juce::File& file)
    {
        const auto facts = irFacts (file);
        if (! facts.found)
            return {};

        const double ms = facts.lengthMs;
        return juce::String (facts.sampleRate / 1000.0, 1) + " kHz" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
             + (ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms");
    }

    void pick (const juce::File& file)
    {
        if (onPick)
            onPick (file);
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowSelected) override
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        const auto file = files[row];
        const bool loaded = isLoaded && isLoaded (file);
        auto r = juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f);

        if (rowSelected)
        {
            g.setColour (Theme::surfaceHi);
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto text = r.reduced (12.0f, 5.0f);
        auto dotSlot = text.removeFromLeft (16.0f);

        if (loaded)
        {
            g.setColour (Theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ dotSlot.getX() + 3.0f, text.getY() + 10.0f }));
        }

        g.setColour (loaded || rowSelected ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.0f, loaded));
        g.drawText (file.getFileNameWithoutExtension(), text.removeFromTop (20.0f), juce::Justification::centredLeft, true);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (detailOf (file), text, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        const auto now = juce::isPositiveAndBelow (lastRowSelected, files.size()) && list.getNumSelectedRows() > 0
                             ? files[lastRowSelected] : juce::File();

        if (now == selected)
            return;

        selected = now;

        if (onSelected)
            onSelected (selected);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        if (e.mods.isPopupMenu())
            showMenu (row);
        else if (pickOnClick)
            pick (files[row]);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, files.size()))
            pick (files[row]);
    }

    void returnKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, files.size()))
            pick (files[row]);
    }

    void deleteKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, files.size()))
            askDelete (files[row]);
    }

    NeuralFlowAmpAudioProcessor& processor;
    FileLibrary::Kind kind;
    juce::String primaryLabel, emptyText = "Nothing here yet.\nDrop files onto this list, or copy them into the folder.", search;
    juce::ListBox list;
    juce::Array<juce::File> all, files;   // all = the folder, files = what matches the search
    juce::File selected;
    std::map<juce::String, std::pair<juce::int64, LibraryFilter::IrFacts>> irInfos;
    LibraryFilter::State filter;
    bool pickOnClick = false, dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelList)
};
