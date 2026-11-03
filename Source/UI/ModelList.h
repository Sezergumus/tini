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

    /** One line on screen: a file, or the header of a pack. */
    struct Row
    {
        juce::File file;
        juce::String pack;
        int count = 0;
        bool isPack = false, open = false;

        Row (juce::File f, juce::String p, int n, bool header) : file (std::move (f)), pack (std::move (p)), count (n), isPack (header) {}
    };

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

    /** Called after a file was moved to another section (old file, new file, the section it went to). */
    std::function<void (const juce::File&, const juce::File&, FileLibrary::Kind)> onMoved;

    /** Called once after one or more files were moved (a single file or a whole pack). */
    std::function<void()> onMovedAll;

    /** Called after files were copied in (with the files in the library); a drop or Add packs. */
    std::function<void (const juce::Array<juce::File>&)> onAdded;

    /** When set, files dropped on the list are handed to this instead of being copied into the list's own
        folder (the Library sorts them by itself). */
    std::function<void (const juce::Array<juce::File>&)> onFilesDropped;

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
        fresh.clear();
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
    int getNumRows() override { return (int) rows.size(); }
    int getNumFiles() const { return files.size(); }
    int getNumInFolder() const { return all.size(); }
    juce::File getFile (int row) const { return files[row]; }
    juce::File getSelectedFile() const { return selected; }

    /** Row of `file` in the shown list, or -1 (also while its pack is closed). */
    int rowOf (const juce::File& file) const
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (! rows[i].isPack && rows[i].file == file)
                return (int) i;
        return -1;
    }

    /** Selects `file`; its pack opens if it was closed. */
    void selectFile (const juce::File& file)
    {
        if (rowOf (file) < 0 && files.contains (file))
        {
            openPacks.insert (FileLibrary::packOf (file, kind));
            buildRows();
            list.updateContent();
        }

        const int row = rowOf (file);
        if (row >= 0)
            list.selectRow (row);
    }

    /** Makes the files that were just added stand out: their packs open, the first one is selected and in view,
        and all of them are tinted until the next click (or about eight seconds). */
    void highlightFiles (const juce::Array<juce::File>& added)
    {
        fresh.clear();
        juce::File first;

        for (const auto& f : added)
        {
            if (! files.contains (f))
                continue;

            fresh.insert (f.getFullPathName());
            openPacks.insert (FileLibrary::packOf (f, kind));

            if (first == juce::File())
                first = f;
        }

        freshUntil = juce::Time::getMillisecondCounter() + 8000;
        buildRows();
        list.updateContent();

        if (first != juce::File())
            selectFile (first);

        list.repaint();
    }

    int getNumHighlighted() const { return (int) fresh.size(); }

    /** Opens or closes a pack. */
    void togglePack (const juce::String& pack)
    {
        if (openPacks.erase (pack) == 0)
            openPacks.insert (pack);

        buildRows();
        list.updateContent();
        reselect();
        list.repaint();
    }

    /** Reads the folder again. */
    void rescan()
    {
        FileLibrary::ensureFolders();
        all = FileLibrary::listTree (getFolder(), FileLibrary::wildcardFor (kind));

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
        if (FileLibrary::listTree (getFolder(), FileLibrary::wildcardFor (kind)) != all)
            rescan();
    }

    /** Copies `sources` into the folder of this list: models / IRs that fit it, and the ones inside zips and
        folders (those become packs). Returns how many were added. */
    int addFiles (const juce::Array<juce::File>& sources)
    {
        const FileLibrary::Batch batch (sources);
        return addBatch (batch, false);
    }

    /** Like addFiles, but asks first when something is in the library already or looks like it belongs to
        another section. This is what a drop or the Add packs button of a list runs. */
    void addFilesChecked (const juce::Array<juce::File>& sources)
    {
        auto batch = std::make_shared<FileLibrary::Batch> (sources);
        const auto report = FileLibrary::inspect (*batch, &kind);

        FileActions::confirmImport (this, report, FileLibrary::folderName (kind),
                                    [safe = juce::Component::SafePointer<ModelList> (this), batch] (bool again)
        {
            if (safe != nullptr)
                safe->addBatch (*batch, again);
        });
    }

    int addBatch (const FileLibrary::Batch& batch, bool again)
    {
        FileLibrary::ensureFolders();

        const auto done = FileLibrary::importBatch (batch, &kind, FileLibrary::Kind::amps, again);

        juce::Array<juce::File> arrived;
        for (const auto& item : done)
            arrived.add (item.file);

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
        if (! isFileRow (row))
            return;

        const auto file = rows[(size_t) row].file;

        juce::PopupMenu menu;
        if (primaryLabel.isNotEmpty())
        {
            menu.addItem (1, primaryLabel);
            menu.addSeparator();
        }
        menu.addItem (2, kind == FileLibrary::Kind::cabs ? "Rename..." : "Edit info...");

        // wrongly sorted? models can move between Amps, Pedals and Other
        if (kind != FileLibrary::Kind::cabs)
        {
            juce::PopupMenu move;
            for (auto k : { FileLibrary::Kind::amps, FileLibrary::Kind::pedals, FileLibrary::Kind::other })
                if (k != kind)
                    move.addItem (10 + (int) k, FileLibrary::folderName (k));

            menu.addSubMenu ("Move to", move);
        }

        menu.addItem (3, "Show in file manager");
        menu.addItem (4, "Delete...");
        Theme::styleMenu (menu, *this);

        menu.showMenuAsync (Theme::menuAtMouse (*this),
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
                default:
                    if (choice >= 10)
                        safe->moveFile (file, (FileLibrary::Kind) (choice - 10));
                    break;
            }
        });
    }

    /** Moves a model to another section: the file goes to that folder and its gear type follows, so it
        stays there when it is classified again. */
    bool moveFile (const juce::File& file, FileLibrary::Kind target)
    {
        const auto moved = FileLibrary::moveToKind (file, target);
        if (! moved.existsAsFile())
            return false;

        if (selected == file)
            selected = {};

        rescan();

        if (onMoved)
            onMoved (file, moved, target);

        if (onMovedAll)
            onMovedAll();

        return true;
    }

    /** Moves a whole pack to another section (all of its files, also the ones a filter hides). Returns how many moved. */
    int movePack (const juce::String& pack, FileLibrary::Kind target)
    {
        juce::Array<juce::File> members;
        for (const auto& file : all)
            if (FileLibrary::packOf (file, kind) == pack)
                members.add (file);

        juce::MouseCursor::showWaitCursor();

        std::vector<std::pair<juce::File, juce::File>> moved;
        for (const auto& file : members)
        {
            const auto now = FileLibrary::moveTo (file, target, pack);
            if (now.existsAsFile() && now != file)
                moved.emplace_back (file, now);
        }

        juce::MouseCursor::hideWaitCursor();

        if (! moved.empty())
        {
            if (selected != juce::File() && selected.isAChildOf (FileLibrary::packFolder (kind, pack)))
                selected = {};

            rescan();

            for (const auto& m : moved)
                if (onMoved)
                    onMoved (m.first, m.second, target);

            if (onMovedAll)
                onMovedAll();
        }

        return (int) moved.size();
    }

    /** Moves every file of a pack to the Recycle Bin. Returns how many could not be removed (in use or protected). */
    int deletePack (const juce::String& pack)
    {
        juce::Array<juce::File> members;
        for (const auto& file : all)
            if (FileLibrary::packOf (file, kind) == pack)
                members.add (file);

        juce::MouseCursor::showWaitCursor();

        int failed = 0;
        for (const auto& file : members)
        {
            if (FileLibrary::removeFile (file))
            {
                if (selected == file)
                    selected = {};
            }
            else
            {
                ++failed;
            }
        }

        FileLibrary::removeIfEmpty (FileLibrary::packFolder (kind, pack));
        juce::MouseCursor::hideWaitCursor();

        rescan();

        for (const auto& file : members)
            if (! file.existsAsFile() && onDeleted)
                onDeleted (file);

        if (onMovedAll)
            onMovedAll();

        return failed;
    }

    void askDeletePack (const juce::String& pack)
    {
        int count = 0;
        for (const auto& file : all)
            count += FileLibrary::packOf (file, kind) == pack ? 1 : 0;

        const auto where = FileLibrary::useTrash() ? juce::String ("the Recycle Bin") : juce::String ("nowhere");
        auto dialog = std::make_unique<FormDialog> ("Delete pack", "Move the pack \"" + pack + "\" with its " + juce::String (count)
                                                        + (count == 1 ? " file" : " files") + " to " + where + "?",
                                                    "Delete pack", DialogButton::Style::danger);

        dialog->onSubmit = [safe = juce::Component::SafePointer<ModelList> (this), pack] (FormDialog&)
        {
            const int failed = safe != nullptr ? safe->deletePack (pack) : 0;
            return failed == 0 ? juce::Result::ok()
                               : juce::Result::fail (juce::String (failed) + (failed == 1 ? " file is" : " files are") + " in use or protected and stayed.");
        };
        FormDialog::show (std::move (dialog), this);
    }

    /** The right click menu of a pack line. */
    void showPackMenu (const juce::String& pack)
    {
        if (kind == FileLibrary::Kind::cabs)
            return;

        juce::PopupMenu menu;

        for (auto k : { FileLibrary::Kind::amps, FileLibrary::Kind::pedals, FileLibrary::Kind::other })
            if (k != kind)
                menu.addItem (10 + (int) k, "Move the whole pack to " + FileLibrary::folderName (k));

        menu.addSeparator();
        menu.addItem (3, "Show in file manager");
        menu.addItem (4, "Delete pack...");
        Theme::styleMenu (menu, *this);

        menu.showMenuAsync (Theme::menuAtMouse (*this),
                            [safe = juce::Component::SafePointer<ModelList> (this), pack] (int choice)
        {
            if (safe == nullptr || choice == 0)
                return;

            if (choice == 3)
                FileLibrary::packFolder (safe->kind, pack).revealToUser();
            else if (choice == 4)
                safe->askDeletePack (pack);
            else if (choice >= 10)
                safe->movePack (pack, (FileLibrary::Kind) (choice - 10));
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
    bool isInterestedInFileDrag (const juce::StringArray& dragged) override
    {
        if (onFilesDropped)
            return FileLibrary::anythingToAdd (dragged);   // the owner sorts them

        for (const auto& path : dragged)
        {
            const juce::File f (path);
            if (f.isDirectory() || FileLibrary::isArchive (f) || FileLibrary::fits (f, kind))
                return true;
        }
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override            { setDropHighlight (false); }

    void filesDropped (const juce::StringArray& dropped, int, int) override
    {
        setDropHighlight (false);

        juce::Array<juce::File> sources;
        for (const auto& path : dropped)
            sources.add (juce::File (path));

        if (onFilesDropped)
            onFilesDropped (sources);
        else
            addFilesChecked (sources);
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
        if (! fresh.empty() && (juce::int32) (juce::Time::getMillisecondCounter() - freshUntil) > 0)
        {
            fresh.clear();
            list.repaint();
        }

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

    /** The lines on screen: files that sit loose in the folder first, then every pack as a header line followed
        by its files. A pack is closed until it is clicked - unless a search or a filter narrows the list, which
        opens the packs that still have something in them. */
    void buildRows()
    {
        std::vector<Row> built;
        std::map<juce::String, juce::Array<juce::File>> byPack;

        for (const auto& file : files)
            byPack[FileLibrary::packOf (file, kind)].add (file);

        for (const auto& file : byPack[juce::String()])
            built.push_back ({ file, {}, 0, false });

        for (const auto& entry : byPack)
        {
            if (entry.first.isEmpty())
                continue;

            const bool open = isNarrowed() || openPacks.count (entry.first) > 0;
            built.push_back ({ {}, entry.first, entry.second.size(), true });
            built.back().open = open;

            if (open)
                for (const auto& file : entry.second)
                    built.push_back ({ file, entry.first, 0, false });
        }

        rowsChanged = built.size() != rows.size();
        if (! rowsChanged)
            for (size_t i = 0; i < built.size(); ++i)
                if (built[i].file != rows[i].file || built[i].pack != rows[i].pack || built[i].open != rows[i].open
                    || built[i].count != rows[i].count)
                {
                    rowsChanged = true;
                    break;
                }

        rows = std::move (built);
    }

    void reselect()
    {
        const int row = rowOf (selected);
        if (row >= 0)
            list.selectRow (row, true, false);
        else
            list.deselectAllRows();
    }

    bool isFileRow (int row) const { return juce::isPositiveAndBelow (row, (int) rows.size()) && ! rows[(size_t) row].isPack; }

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

        buildRows();

        if (! same || rowsChanged)
            list.updateContent();

        reselect();

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
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;

        const auto& line = rows[(size_t) row];

        if (line.isPack)
        {
            paintPack (g, line, width, height, rowSelected);
            return;
        }

        const auto file = line.file;
        const bool loaded = isLoaded && isLoaded (file);
        auto r = juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f);
        const float indent = line.pack.isEmpty() ? 0.0f : 16.0f;   // the files of a pack sit under their header
        r.removeFromLeft (indent);

        if (rowSelected)
        {
            g.setColour (Theme::surfaceHi);
            g.fillRoundedRectangle (r, 6.0f);
        }

        if (fresh.count (file.getFullPathName()) > 0)
        {
            g.setColour (Theme::accent.withAlpha (rowSelected ? 0.22f : 0.14f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 6.0f), 1.5f);
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

    /** A pack header: a chevron, the name and how many files it holds (and how many of them are in use). */
    void paintPack (juce::Graphics& g, const Row& line, int width, int height, bool hot)
    {
        auto r = juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f);

        g.setColour (juce::Colours::white.withAlpha (hot ? 0.08f : 0.04f));
        g.fillRoundedRectangle (r, 6.0f);

        const auto c = juce::Point<float> (r.getX() + 18.0f, r.getCentreY());
        juce::Path chevron;
        if (line.open)
        {
            chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
            chevron.lineTo (c.x, c.y + 2.5f);
            chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        }
        else
        {
            chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
            chevron.lineTo (c.x + 2.5f, c.y);
            chevron.lineTo (c.x - 2.0f, c.y + 4.0f);
        }
        g.setColour (Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto count = juce::String (line.count) + (line.count == 1 ? " file" : " files");
        auto text = r.reduced (34.0f, 0.0f);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.5f));
        g.drawText (count, text.removeFromRight (70.0f), juce::Justification::centredRight, false);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (line.pack, text, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        if (juce::isPositiveAndBelow (lastRowSelected, (int) rows.size()) && rows[(size_t) lastRowSelected].isPack
            && list.getNumSelectedRows() > 0)
            return;   // a pack header is not a file: clicking it opens or closes it (see listBoxItemClicked)

        const auto now = isFileRow (lastRowSelected) && list.getNumSelectedRows() > 0
                             ? rows[(size_t) lastRowSelected].file : juce::File();

        if (now == selected)
            return;

        selected = now;

        if (onSelected)
            onSelected (selected);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;

        if (rows[(size_t) row].isPack)
        {
            if (e.mods.isPopupMenu())
                showPackMenu (rows[(size_t) row].pack);
            else if (! isNarrowed())
                togglePack (rows[(size_t) row].pack);
            return;
        }

        fresh.clear();
        list.repaint();

        if (e.mods.isPopupMenu())
            showMenu (row);
        else if (pickOnClick)
            pick (rows[(size_t) row].file);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (isFileRow (row))
            pick (rows[(size_t) row].file);
    }

    void returnKeyPressed (int row) override
    {
        if (isFileRow (row))
            pick (rows[(size_t) row].file);
        else if (juce::isPositiveAndBelow (row, (int) rows.size()) && ! isNarrowed())
            togglePack (rows[(size_t) row].pack);
    }

    void deleteKeyPressed (int row) override
    {
        if (isFileRow (row))
            askDelete (rows[(size_t) row].file);
    }

    NeuralFlowAmpAudioProcessor& processor;
    FileLibrary::Kind kind;
    juce::String primaryLabel, emptyText = "Nothing here yet.\nDrop files onto this list, or copy them into the folder.", search;
    juce::ListBox list;
    juce::Array<juce::File> all, files;   // all = the folder (packs included), files = what matches the search

    std::vector<Row> rows;
    std::set<juce::String> fresh;   // paths of the files that were just added: they stand out for a while
    juce::uint32 freshUntil = 0;
    std::set<juce::String> openPacks;
    bool rowsChanged = false;
    juce::File selected;
    std::map<juce::String, std::pair<juce::int64, LibraryFilter::IrFacts>> irInfos;
    LibraryFilter::State filter;
    bool pickOnClick = false, dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelList)
};
