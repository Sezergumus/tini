#pragma once

#include "../LibraryPaths.h"
#include "Dialog.h"
#include "Theme.h"

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
/** A scrolling list of the models or impulse responses in one folder. The loaded file is marked; a
    click (or Enter on the selected line) calls onPick.

    The list follows the folder: while it is visible it looks at the folder about once a second, so files
    added or removed in Explorer show up without leaving the app. It also manages the files: a right
    click offers Rename, Show in Explorer and Delete (to the Recycle Bin), "Add files" copies files in,
    "Open folder" shows the folder, and files can be dropped onto the list. */
class FileBrowserList : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ListBoxModel,
                        private juce::Timer
{
public:
    static constexpr int rowHeight = 30, headerHeight = 54;

    explicit FileBrowserList (FileLibrary::Kind libraryKind)
        : kind (libraryKind), wildcard (FileLibrary::wildcardFor (libraryKind))
    {
        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getVerticalScrollBar().setAutoHide (true);
        addAndMakeVisible (list);

        addButton.setButtonText ("Add files");
        addButton.setTooltip ("Copy files into this library");
        addButton.onClick = [this] { chooseFilesToAdd(); };
        addAndMakeVisible (addButton);

        folderButton.setButtonText ("Open folder");
        folderButton.setTooltip ("Show this folder in the file manager");
        folderButton.onClick = [this] { openFolder(); };
        addAndMakeVisible (folderButton);
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

    /** Marks the loaded file; shows its folder (rescanning only if the folder changed). */
    void setCurrentFile (const juce::File& file)
    {
        if (file != current)
            pinned = {};   // another file was loaded: follow it again

        current = file;
        const auto newFolder = folderToShow();
        const bool folderChanged = newFolder != folder;

        if (folderChanged)
            rescan (newFolder);
        else
            list.repaint();
    }

    /** Reads the folder again (new files, removed files). */
    void rescan() { rescan (folder); }

    /** Looks at the folder once; the list only changes when its content did. Runs by itself while visible. */
    void refreshIfChanged()
    {
        const auto newFolder = folderToShow();
        if (newFolder != folder || FileLibrary::list (folder, wildcard) != files)
            rescan (newFolder);
    }

    /** Copies `sources` into this library and shows them. Returns how many were added. */
    int addFiles (const juce::Array<juce::File>& sources)
    {
        const int added = FileActions::importAll (sources, kind);

        if (added > 0)
        {
            // the new files live in the library: if the loaded file is elsewhere, show the library
            if (FileLibrary::folderFor (current, kind) != FileLibrary::libraryFolder (kind))
                pinned = FileLibrary::libraryFolder (kind);

            rescan (folderToShow());
        }

        return added;
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

    int getNumRows() override { return files.size(); }
    int getNumFiles() const { return files.size(); }
    juce::File getFile (int row) const { return files[row]; }
    const juce::File& getFolder() const { return folder; }
    FileLibrary::Kind getKind() const { return kind; }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            FileLibrary::ensureFolders();   // so there is a folder to put files in
            refreshIfChanged();
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
        auto head = getLocalBounds().removeFromTop (headerHeight).reduced (4, 0);

        auto top = head.removeFromTop (30);
        top.removeFromRight (buttonsWidth);
        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (FileLibrary::title (kind), top, juce::Justification::centredLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (folder.isDirectory() ? folder.getFullPathName() : juce::String ("No folder yet"), head,
                    juce::Justification::topLeft, true);

        if (files.isEmpty())
        {
            auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (4, 8);
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.0f));

            const auto what = FileLibrary::isModelKind (kind) ? juce::String (".nam files")
                                                              : juce::String ("impulse responses (.wav)");
            g.drawFittedText ("No " + what + " here yet.\nDrop files onto this list, use Add files,\nor copy them into the folder - the list follows it.",
                              body, juce::Justification::topLeft, 6);
        }

        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f, 1.5f);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (30).reduced (4, 0).withSizeKeepingCentre (getWidth() - 8, 24);
        folderButton.setBounds (top.removeFromRight (folderWidth));
        top.removeFromRight (6);
        addButton.setBounds (top.removeFromRight (addWidth));

        list.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    }

    //==========================================================================
    // Files dropped on the list are only added to the library; the loaded model stays.
    bool isInterestedInFileDrag (const juce::StringArray& dragged) override
    {
        return FileActions::anyFits (dragged, kind);
    }

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

    // exposed so tests can drive them
    juce::TextButton addButton, folderButton;

    /** The context menu of a row (also used by the tests through the actions below). */
    void showMenu (int row)
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        const auto file = files[row];

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
    static constexpr int addWidth = 70, folderWidth = 88, buttonsWidth = addWidth + folderWidth + 6;

    void timerCallback() override { refreshIfChanged(); }

    /** The folder of the loaded file, unless files were just added to the library. */
    juce::File folderToShow() const
    {
        return pinned != juce::File() ? pinned : FileLibrary::folderFor (current, kind);
    }

    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint();
        }
    }

    void rescan (const juce::File& newFolder)
    {
        folder = newFolder;
        auto fresh = FileLibrary::list (folder, wildcard);
        const bool same = fresh == files;
        files = std::move (fresh);

        if (! same)
        {
            list.updateContent();

            const int row = files.indexOf (current);
            if (row >= 0)
                list.scrollToEnsureRowIsOnscreen (row);
        }

        list.repaint();
        repaint();
    }

    void openFolder()
    {
        FileLibrary::ensureFolders();
        const auto target = folder.isDirectory() ? folder : FileLibrary::libraryFolder (kind);
        target.startAsProcess();
    }

    void chooseFilesToAdd()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Add files to the " + FileLibrary::title (kind).toLowerCase(),
                                                           FileLibrary::downloadsFolder(),
                                                           wildcard);

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<FileBrowserList> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->addFiles (fc.getResults());
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
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        const auto file = files[row];
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
        if (juce::isPositiveAndBelow (row, files.size()))
            askDelete (files[row]);
    }

    void pick (int row)
    {
        if (juce::isPositiveAndBelow (row, files.size()) && onPick)
            onPick (files[row]);
    }

    FileLibrary::Kind kind;
    juce::String wildcard;
    juce::ListBox list;
    juce::Array<juce::File> files;
    juce::File folder, current, pinned;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBrowserList)
};
