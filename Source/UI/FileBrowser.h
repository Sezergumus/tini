#pragma once

#include "../LibraryPaths.h"
#include "Dialog.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** Browsing helpers on top of LibraryPaths.h: which folder a list shows, the sorted files in it and how
    to step through them. */
namespace FileLibrary
{
    constexpr int maxEntries = 500;

    /** The folder of the loaded file; without one, the library folder (if it exists). */
    inline juce::File folderFor (const juce::File& loaded, Kind kind)
    {
        if (loaded.existsAsFile())
            return loaded.getParentDirectory();

        const auto library = libraryFolder (kind);
        return library.isDirectory() ? library : juce::File();
    }

    /** The files of `folder` that match `wildcard`, sorted by name (at most maxEntries). */
    inline juce::Array<juce::File> list (const juce::File& folder, const juce::String& wildcard)
    {
        juce::Array<juce::File> files;

        if (folder.isDirectory())
        {
            files = folder.findChildFiles (juce::File::findFiles, false, wildcard);

            struct ByName
            {
                static int compareElements (const juce::File& a, const juce::File& b)
                {
                    return a.getFileName().compareNatural (b.getFileName());
                }
            } byName;
            files.sort (byName);

            if (files.size() > maxEntries)
                files.removeRange (maxEntries, files.size() - maxEntries);
        }

        return files;
    }

    /** The file `delta` places after (or before) `current` in its folder, wrapping around. With no current
        file the first (or last) file of the library folder. Returns an empty File if there is none. */
    inline juce::File neighbour (const juce::File& current, Kind kind, int delta)
    {
        const auto files = list (folderFor (current, kind), wildcardFor (kind));
        if (files.isEmpty())
            return {};

        const int index = files.indexOf (current);
        if (index < 0)
            return files[delta > 0 ? 0 : files.size() - 1];

        return files[(index + delta + files.size()) % files.size()];
    }
}

//==============================================================================
/** The dialogs for the things a user can do with a library file. They only ask and report: the work
    itself is done by the caller's function, so every list (the amp / cab browsers, the Library page,
    the pedal picker) shares one wording and one behaviour. */
namespace FileActions
{
    /** Asks for a new name (the extension stays) and calls `rename` with it. An error it returns is shown
        inside the dialog, which stays open. */
    inline void askRename (juce::Component* owner, const juce::File& file,
                           std::function<juce::Result (const juce::String&)> rename)
    {
        auto dialog = std::make_unique<FormDialog> ("Rename", file.getFileName(), "Rename");
        dialog->addText ("name", "Name", file.getFileNameWithoutExtension());
        dialog->onSubmit = [rename = std::move (rename)] (FormDialog& d) { return rename (d.getText ("name")); };
        FormDialog::show (std::move (dialog), owner);
    }

    /** Asks before moving a file to the Recycle Bin, then calls `remove`; false from it is shown as an error. */
    inline void askDelete (juce::Component* owner, const juce::File& file, std::function<bool()> remove)
    {
        const auto where = FileLibrary::useTrash() ? juce::String ("the Recycle Bin") : juce::String ("nowhere");

        auto dialog = std::make_unique<FormDialog> ("Delete", "Move " + file.getFileName() + " to " + where + "?", "Delete",
                                                    DialogButton::Style::danger);
        dialog->onSubmit = [file, remove = std::move (remove)] (FormDialog&)
        {
            return remove() ? juce::Result::ok() : juce::Result::fail (file.getFileName() + " is in use or protected.");
        };
        FormDialog::show (std::move (dialog), owner);
    }

    /** Before files are added: if some of them are in the library already, or look like they belong to another
        section than the list they were dropped on, asks first. Without a reason to ask it just calls `proceed`
        (with true when copies of files that are there already are wanted). */
    inline void confirmImport (juce::Component* owner, const FileLibrary::ImportReport& report,
                               const juce::String& sectionName, std::function<void (bool again)> proceed)
    {
        if (report.duplicates == 0 && report.mismatched == 0)
        {
            proceed (false);
            return;
        }

        auto count = [] (int n, const juce::String& what) { return juce::String (n) + " " + what + (n == 1 ? "" : "s"); };
        juce::StringArray lines;

        if (report.duplicates > 0)
            lines.add (report.duplicates == report.total
                           ? (report.total == 1 ? juce::String ("This file is already in the library.")
                                                : "All " + count (report.total, "file") + " are already in the library.")
                           : count (report.duplicates, "file") + " of " + juce::String (report.total) + " are already in the library.");

        if (report.mismatched > 0)
        {
            const char* looks = report.looksLike == FileLibrary::Kind::pedals ? "pedals"
                              : report.looksLike == FileLibrary::Kind::amps   ? "amps" : "something else than an amp or a pedal";
            lines.add (count (report.mismatched, "file") + (report.mismatched == 1 ? " looks" : " look") + " like "
                       + juce::String (looks) + " (by what " + (report.mismatched == 1 ? "it says" : "they say") + " about itself), not like \""
                       + sectionName + "\".");
        }

        const bool again = report.duplicates > 0;
        auto dialog = std::make_unique<FormDialog> (again ? "Add again?" : "Add to this section?",
                                                    lines.joinIntoString (" ") + (again ? " Add them again anyway?" : " Add them here anyway?"),
                                                    "Add anyway");
        dialog->onSubmit = [proceed = std::move (proceed), again] (FormDialog&)
        {
            proceed (again);
            return juce::Result::ok();
        };
        FormDialog::show (std::move (dialog), owner);
    }

    /** Copies `sources` that fit `kind` into the library folder of `kind`; returns how many arrived. */
    inline int importAll (const juce::Array<juce::File>& sources, FileLibrary::Kind kind)
    {
        int added = 0;
        for (const auto& source : sources)
            if (source.existsAsFile() && FileLibrary::fits (source, kind)
                && FileLibrary::importFile (source, kind).existsAsFile())
                ++added;
        return added;
    }

    /** True if any of the dragged paths is a file `kind` can hold. */
    inline bool anyFits (const juce::StringArray& dragged, FileLibrary::Kind kind)
    {
        for (const auto& path : dragged)
            if (FileLibrary::fits (juce::File (path), kind))
                return true;
        return false;
    }
}


//==============================================================================
/** The pack selector of a model list: what the list shows ("All amp models", a pack, a folder) and a
    chevron. A click opens the menu of packs (see FileBrowserList::showPackMenu). */
class PackButton : public juce::Button
{
public:
    PackButton() : juce::Button ("pack")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Choose a pack, or all models");
    }

    void setText (const juce::String& newText)
    {
        if (newText != text)
        {
            text = newText;
            repaint();
        }
    }

    const juce::String& getText() const { return text; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::inset.brighter (down ? 0.1f : highlighted ? 0.05f : 0.0f));
        g.fillRoundedRectangle (r, 8.0f);

        auto inner = r.reduced (12.0f, 0.0f);
        const auto chevronArea = inner.removeFromRight (12.0f);

        g.setColour (Theme::textHi);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (text, inner.withTrimmedRight (8.0f), juce::Justification::centredLeft, true);

        const auto c = chevronArea.getCentre();
        juce::Path chevron;
        chevron.startNewSubPath (c.x - 4.5f, c.y - 2.0f);
        chevron.lineTo (c.x, c.y + 2.5f);
        chevron.lineTo (c.x + 4.5f, c.y - 2.0f);
        g.setColour (highlighted ? Theme::textHi : Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    juce::String text;
};

//==============================================================================
/** The list of models (or impulse responses) next to a model slot. It has everything for picking one:

      title             search  +  folder  hide
      [ pack          v ]       what the list shows: all models, one pack, or a folder
      path of that folder
      the files ...

    The list shows the folder of the loaded file until the pack menu says otherwise, and it stays on the
    pack (or on "All") the user chose while the loaded file is in it. A click (or Enter on the selected
    line) calls onPick. The search filters what the list shows.

    The list follows the folder: while it is visible it looks at it about once a second, so files added
    or removed in Explorer show up without leaving the app. It also manages the files: a right click
    offers Rename, Show in file manager and Delete (to the Recycle Bin), + copies files and packs in,
    the folder icon opens the folder, and files can be dropped onto the list. */
class FileBrowserList : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ListBoxModel,
                        private juce::Timer
{
public:
    static constexpr int rowHeight = 30, titleHeight = 30, packHeight = 32, pathHeight = 24;
    static constexpr int headerHeight = titleHeight + 6 + packHeight + pathHeight;

    explicit FileBrowserList (FileLibrary::Kind libraryKind)
        : kind (libraryKind), wildcard (FileLibrary::wildcardFor (libraryKind))
    {
        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getVerticalScrollBar().setAutoHide (true);
        addAndMakeVisible (list);

        searchButton.onClick = [this] { openSearch(); };
        addButton.onClick    = [this] { chooseFilesToAdd(); };
        folderButton.onClick = [this] { openFolder(); };
        hideButton.onClick   = [this] { if (onHide) onHide(); };
        packButton.onClick   = [this] { showPackMenu(); };

        for (auto* b : std::initializer_list<juce::Component*> { &searchButton, &addButton, &folderButton, &packButton })
            addAndMakeVisible (b);
        addChildComponent (hideButton);

        searchBox.setOpaque (false);   // rounded corners
        searchBox.setTextToShowWhenEmpty ("Search " + lowerFirst (FileLibrary::title (kind)), Theme::textLo);
        searchBox.setFont (Theme::ui (13.0f));
        searchBox.setIndents (10, 6);
        searchBox.onTextChange = [this] { setSearch (searchBox.getText()); };
        searchBox.onEscapeKey  = [this] { closeSearch(); };
        searchBox.onReturnKey  = [this] { pick (0); };
        addChildComponent (searchBox);

        closeSearchButton.onClick = [this] { closeSearch(); };
        addChildComponent (closeSearchButton);
    }

    ~FileBrowserList() override
    {
        stopTimer();
        list.setModel (nullptr);
    }

    /** Called with the file the user picked. */
    std::function<void (const juce::File&)> onPick;

    /** Called after a file was renamed here (the loaded model or IR may have to follow it). */
    std::function<void (const juce::File& oldFile, const juce::File& newFile)> onRenamed;

    /** Called after a file was deleted here. */
    std::function<void (const juce::File&)> onDeleted;

    /** Called by the hide button; without it the list has no hide button. */
    std::function<void()> onHide;

    /** Marks the loaded file. The list stays on what it shows if the file is in it, and goes to the
        file's folder if not. */
    void setCurrentFile (const juce::File& file)
    {
        if (file != current)
        {
            current = file;
            if (file.existsAsFile() && ! viewContains (file))
            {
                showingAll = false;
                folder = file.getParentDirectory();
            }
        }

        rescan();
    }

    /** Shows every file of the library, all packs together. */
    void showAll()
    {
        showingAll = true;
        rescan();
    }

    /** Shows one folder: a pack, the library folder itself, or the folder of a file outside the library. */
    void showFolder (const juce::File& newFolder)
    {
        showingAll = false;
        folder = newFolder;
        rescan();
    }

    bool isShowingAll() const { return showingAll; }

    /** The folder the list shows (the library folder while it shows all of it). */
    juce::File getFolder() const { return showingAll ? FileLibrary::libraryFolder (kind) : folder; }

    /** What the pack button says. */
    const juce::String& getViewName() const { return packButton.getText(); }

    /** Filters the list: a file is shown if its name (or its pack, while all are shown) has every word. */
    void setSearch (const juce::String& text)
    {
        if (text != search)
        {
            search = text;
            rescan();
        }
    }

    /** Reads the folder again (new files, removed files). */
    void rescan()
    {
        files = scan();

        juce::Array<juce::File> fresh;
        for (const auto& f : files)
            if (matchesSearch (f))
                fresh.add (f);

        if (fresh != shown)
        {
            shown = std::move (fresh);
            list.updateContent();

            const int row = shown.indexOf (current);
            if (row >= 0)
                list.scrollToEnsureRowIsOnscreen (row);
        }

        packButton.setText (viewName());
        list.repaint();
        repaint();
    }

    /** Looks at the folder once; the list only changes when its content did. Runs by itself while visible. */
    void refreshIfChanged()
    {
        const bool wasAll = showingAll;
        const auto oldFolder = folder;

        if (scan() != files || showingAll != wasAll || folder != oldFolder)
            rescan();
    }

    /** The file `delta` places after (or before) `file` in what the list shows (with the search), wrapping
        around; for a file that is not in it, the neighbour in its folder. Used by the arrows of the slot. */
    juce::File neighbourOf (const juce::File& file, int delta) const
    {
        for (const auto* source : { &shown, &files })
        {
            const int index = source->indexOf (file);
            if (index >= 0)
                return (*source)[(index + delta + source->size()) % source->size()];
        }

        return FileLibrary::neighbour (file, kind, delta);
    }

    /** Copies `sources` into this library and shows them. Returns how many were added. */
    int addFiles (const juce::Array<juce::File>& sources)
    {
        const FileLibrary::Batch batch (sources);
        return addBatch (batch, false);
    }

    /** Like addFiles, but asks first when something is in the library already or looks like it belongs to
        another section (a pedal capture offered to the amp list). */
    void addFilesChecked (const juce::Array<juce::File>& sources)
    {
        auto batch = std::make_shared<FileLibrary::Batch> (sources);
        const auto report = FileLibrary::inspect (*batch, &kind);

        FileActions::confirmImport (this, report, FileLibrary::folderName (kind),
                                    [safe = juce::Component::SafePointer<FileBrowserList> (this), batch] (bool again)
        {
            if (safe != nullptr)
                safe->addBatch (*batch, again);
        });
    }

    int addBatch (const FileLibrary::Batch& batch, bool again)
    {
        const auto done = FileLibrary::importBatch (batch, &kind, FileLibrary::Kind::amps, again);

        if (! done.empty())
        {
            // the new files live in the library: show where they went (a pack is a folder of its own)
            const auto arrived = done.front().file;
            if (! viewContains (arrived))
            {
                showingAll = false;
                folder = arrived.getParentDirectory();
            }
            rescan();
        }

        return (int) done.size();
    }

    /** Renames a file of the list (keeps its extension). */
    juce::Result renameFile (const juce::File& file, const juce::String& newBaseName)
    {
        juce::File renamed;
        const auto result = FileLibrary::renameFile (file, newBaseName, renamed);

        if (result.wasOk() && renamed != file)
        {
            if (current == file)
                current = renamed;

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

        rescan();

        if (onDeleted)
            onDeleted (file);

        return true;
    }

    int getNumRows() override { return shown.size(); }
    int getNumFiles() const { return shown.size(); }
    juce::File getFile (int row) const { return shown[row]; }
    FileLibrary::Kind getKind() const { return kind; }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            FileLibrary::ensureFolders();   // so there is a folder to put files in
            rescan();                       // files may have been added while the list was hidden
            startTimer (1000);
        }
        else
        {
            stopTimer();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (! searching)
        {
            g.setColour (Theme::textMid);
            g.setFont (Theme::ui (13.0f, true));
            g.drawText (FileLibrary::title (kind), titleArea.withTrimmedLeft (4), juce::Justification::centredLeft, true);
        }

        const auto shownFolder = getFolder();
        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (shownFolder.isDirectory() ? shownFolder.getFullPathName() : juce::String ("No folder yet"),
                    pathArea.withTrimmedLeft (4), juce::Justification::centredLeft, true);

        if (shown.isEmpty())
        {
            auto body = list.getBounds().reduced (4, 8);
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.0f));

            if (! files.isEmpty())
            {
                g.drawFittedText ("Nothing here matches \"" + search.trim() + "\".", body, juce::Justification::topLeft, 3);
            }
            else
            {
                const auto what = FileLibrary::isModelKind (kind) ? juce::String (".nam files")
                                                                  : juce::String ("impulse responses (.wav)");
                g.drawFittedText ("No " + what + " here yet.\nDrop files onto this list, add packs with +,\nor copy them into the folder - the list follows it.",
                                  body, juce::Justification::topLeft, 6);
            }
        }

        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f, 1.5f);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto titleRow = b.removeFromTop (titleHeight);

        const int s = IconButton::size;
        auto icon = [&titleRow, s] { return titleRow.removeFromRight (s).withSizeKeepingCentre (s, s); };

        hideButton.setVisible (static_cast<bool> (onHide));
        if (onHide)
        {
            hideButton.setBounds (icon());
            titleRow.removeFromRight (2);
        }
        folderButton.setBounds (icon());
        addButton.setBounds (icon());
        titleRow.removeFromRight (2);

        // the search box takes the title's place (and the search icon's) while it is open
        const auto searchSlot = icon();
        searchButton.setBounds (searchSlot);
        titleArea = titleRow;

        const auto box = titleRow.withRight (searchSlot.getRight()).withSizeKeepingCentre (titleRow.getWidth() + s, 28);
        searchBox.setBounds (box);
        closeSearchButton.setBounds (box.withLeft (box.getRight() - 26).withSizeKeepingCentre (24, 24));
        searchButton.setVisible (! searching);

        b.removeFromTop (6);
        packButton.setBounds (b.removeFromTop (packHeight));
        pathArea = b.removeFromTop (pathHeight);

        list.setBounds (b);
    }

    //==========================================================================
    // Files dropped on the list are only added to the library; the loaded model stays.
    bool isInterestedInFileDrag (const juce::StringArray& dragged) override
    {
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

        addFilesChecked (sources);
    }

    // exposed so tests can drive them
    IconButton searchButton { "list_search", Icons::search(), "Search this list" },
               addButton    { "list_add", Icons::plus(), "Add packs: copy files, zip packs or folders into the library" },
               folderButton { "list_folder", Icons::folder(), "Show this folder in the file manager" },
               hideButton   { "list_hide", Icons::hidePanel(), "Hide the list" };
    PackButton packButton;
    juce::TextEditor searchBox;

    bool isSearching() const { return searching; }

    /** Opens the search box in the title row. */
    void openSearch()
    {
        searching = true;
        searchBox.setVisible (true);
        closeSearchButton.setVisible (true);
        resized();
        repaint();
        searchBox.grabKeyboardFocus();
    }

    /** Closes the search box; the list shows everything again. */
    void closeSearch()
    {
        searching = false;
        searchBox.setText ({}, false);
        searchBox.setVisible (false);
        closeSearchButton.setVisible (false);
        setSearch ({});
        resized();
        repaint();
    }

    /** The menu of the pack button: all models, the library folder itself (if files sit there), each pack,
        and the folder outside the library the list shows (if it does). */
    void showPackMenu()
    {
        const auto library = FileLibrary::libraryFolder (kind);
        const auto packs = packFolders();
        const bool atRoot = ! showingAll && folder == library;

        juce::PopupMenu menu;
        menu.addItem (allId, allLabel(), true, showingAll);

        if (atRoot || ! FileLibrary::list (library, wildcard).isEmpty())
            menu.addItem (rootId, rootLabel(), true, atRoot);

        if (! packs.isEmpty())
        {
            menu.addSectionHeader ("Packs");
            for (int i = 0; i < packs.size(); ++i)
                menu.addItem (firstPackId + i, packs[i].getFileName(), true, ! showingAll && folder == packs[i]);
        }

        if (! showingAll && folder.isDirectory() && folder != library && ! packs.contains (folder))
        {
            menu.addSeparator();
            menu.addItem (otherId, folder.getFileName(), true, true);
        }

        Theme::styleMenu (menu, *this);
        menu.showMenuAsync (Theme::menuBelow (packButton).withMinimumWidth (packButton.getWidth()),
                            [safe = juce::Component::SafePointer<FileBrowserList> (this), packs, library] (int choice)
        {
            if (safe == nullptr || choice == 0 || choice == otherId)
                return;

            if (choice == allId)
                safe->showAll();
            else if (choice == rootId)
                safe->showFolder (library);
            else if (juce::isPositiveAndBelow (choice - firstPackId, packs.size()))
                safe->showFolder (packs[choice - firstPackId]);
        });
    }

    /** The packs of the library: its first level of folders, by name. */
    juce::Array<juce::File> packFolders() const
    {
        auto packs = FileLibrary::libraryFolder (kind).findChildFiles (juce::File::findDirectories, false);

        struct ByName
        {
            static int compareElements (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareNatural (b.getFileName());
            }
        } byName;
        packs.sort (byName);
        return packs;
    }

    /** The context menu of a row (also used by the tests through the actions below). */
    void showMenu (int row)
    {
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;

        const auto file = shown[row];

        juce::PopupMenu menu;
        menu.addItem (1, "Load");
        menu.addSeparator();
        menu.addItem (2, "Rename...");
        menu.addItem (3, "Show in file manager");
        menu.addItem (4, "Delete...");
        Theme::styleMenu (menu, *this);

        menu.showMenuAsync (Theme::menuAtMouse (*this),
                            [safe = juce::Component::SafePointer<FileBrowserList> (this), file] (int choice)
        {
            if (safe == nullptr)
                return;

            switch (choice)
            {
                case 1: if (safe->onPick) safe->onPick (file); break;
                case 2: safe->askRename (file); break;
                case 3: file.revealToUser(); break;
                case 4: safe->askDelete (file); break;
                default: break;
            }
        });
    }

private:
    static constexpr int allId = 1, rootId = 2, otherId = 3, firstPackId = 100;

    static juce::String lowerFirst (const juce::String& s) { return s.substring (0, 1).toLowerCase() + s.substring (1); }

    juce::String allLabel() const  { return "All " + lowerFirst (FileLibrary::title (kind)); }
    juce::String rootLabel() const { return FileLibrary::folderName (kind) + " (no pack)"; }

    juce::String viewName() const
    {
        if (showingAll)
            return allLabel();
        if (folder == FileLibrary::libraryFolder (kind))
            return rootLabel();
        return folder.isDirectory() ? folder.getFileName() : juce::String ("No folder yet");
    }

    void timerCallback() override { refreshIfChanged(); }

    bool viewContains (const juce::File& file) const
    {
        return showingAll ? file.isAChildOf (FileLibrary::libraryFolder (kind))
                          : folder != juce::File() && file.getParentDirectory() == folder;
    }

    /** The files of what the list shows. A folder that is gone (a pack renamed or removed elsewhere) is
        replaced by the folder of the loaded file. */
    juce::Array<juce::File> scan()
    {
        if (! showingAll && ! folder.isDirectory())
            folder = FileLibrary::folderFor (current, kind);

        return showingAll ? FileLibrary::listTree (FileLibrary::libraryFolder (kind), wildcard)
                          : FileLibrary::list (folder, wildcard);
    }

    bool matchesSearch (const juce::File& file) const
    {
        const auto words = juce::StringArray::fromTokens (search, " ", "");
        if (words.isEmpty())
            return true;

        auto text = file.getFileNameWithoutExtension();
        if (showingAll)
            text << " " << FileLibrary::packOf (file, kind);

        for (const auto& word : words)
            if (word.isNotEmpty() && ! text.containsIgnoreCase (word))
                return false;
        return true;
    }

    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint();
        }
    }

    void openFolder()
    {
        FileLibrary::ensureFolders();
        const auto shownFolder = getFolder();
        const auto target = shownFolder.isDirectory() ? shownFolder : FileLibrary::libraryFolder (kind);
        target.startAsProcess();
    }

    void chooseFilesToAdd()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Add packs and files to the " + FileLibrary::title (kind).toLowerCase(),
                                                           FileLibrary::downloadsFolder(),
                                                           wildcard + ";*.zip");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<FileBrowserList> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->addFilesChecked (fc.getResults());
        });
    }

    void askRename (const juce::File& file)
    {
        FileActions::askRename (this, file, [safe = juce::Component::SafePointer<FileBrowserList> (this), file] (const juce::String& name)
        {
            return safe != nullptr ? safe->renameFile (file, name) : juce::Result::ok();
        });
    }

    void askDelete (const juce::File& file)
    {
        FileActions::askDelete (this, file, [safe = juce::Component::SafePointer<FileBrowserList> (this), file]
        {
            return safe == nullptr || safe->deleteFile (file);
        });
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;

        const auto file = shown[row];
        const bool isCurrent = file == current;
        auto r = juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f);

        if (isCurrent || selected)
        {
            g.setColour (isCurrent ? Theme::surfaceHi : juce::Colours::white.withAlpha (selected ? 0.08f : 0.04f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto text = r.reduced (12.0f, 0.0f);
        if (isCurrent)
        {
            g.setColour (Theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ text.getX() + 3.0f, text.getCentreY() }));
        }
        text.removeFromLeft (16.0f);

        // with all packs in one list, each line says which pack it is from
        if (showingAll)
        {
            const auto pack = FileLibrary::packOf (file, kind);
            if (pack.isNotEmpty())
            {
                const auto font = Theme::ui (11.0f);
                const float w = juce::jmin (text.getWidth() * 0.4f, font.getStringWidthFloat (pack) + 2.0f);
                g.setColour (Theme::textLo);
                g.setFont (font);
                g.drawText (pack, text.removeFromRight (w), juce::Justification::centredRight, true);
                text.removeFromRight (8.0f);
            }
        }

        g.setColour (isCurrent ? Theme::textHi : Theme::textMid);
        g.setFont (Theme::ui (13.0f, isCurrent));
        g.drawText (file.getFileNameWithoutExtension(), text, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showMenu (row);
        else
            pick (row);
    }

    void returnKeyPressed (int row) override { pick (row); }

    void deleteKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, shown.size()))
            askDelete (shown[row]);
    }

    void pick (int row)
    {
        if (juce::isPositiveAndBelow (row, shown.size()) && onPick)
            onPick (shown[row]);
    }

    FileLibrary::Kind kind;
    juce::String wildcard, search;
    juce::ListBox list;
    juce::Array<juce::File> files, shown;   // what the view holds, and what of it the search lets through
    juce::File folder, current;
    bool showingAll = false, searching = false;
    IconButton closeSearchButton { "list_search_close", Icons::close(), "Close the search" };
    juce::Rectangle<int> titleArea, pathArea;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBrowserList)
};
