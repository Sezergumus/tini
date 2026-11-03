#pragma once

#include "../PluginProcessor.h"
#include "FileBrowser.h"

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

    /** Called after files were copied in (with the files in the library); a drop or Add files. */
    std::function<void (const juce::Array<juce::File>&)> onAdded;

    void setPickOnClick (bool on) { pickOnClick = on; }
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
        rescan();

        if (onSelected)
            onSelected ({});
    }

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
        menu.addItem (2, "Rename...");
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
                case 2: safe->askRename (file); break;
                case 3: file.revealToUser(); break;
                case 4: safe->askDelete (file); break;
                default: break;
            }
        });
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
        FileActions::askDelete (file, [safe = juce::Component::SafePointer<ModelList> (this), file]
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

            const auto text = all.isEmpty() ? emptyText : juce::String ("Nothing matches \"") + search + "\".";
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
    void timerCallback() override { refreshIfChanged(); }

    /** Metadata arrived: lines can change their text and the search may match more. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (search.isNotEmpty())
            rebuildVisible();
        else
            list.repaint();
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
        for (const auto& file : all)
            if (matchesSearch (file))
                shown.add (file);

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
    }

    /** "48.0 kHz · 85 ms" of an impulse response, read once per file version. */
    juce::String audioInfo (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        const auto stamp = file.getLastModificationTime().toMilliseconds();
        const auto it = audioInfos.find (key);

        if (it != audioInfos.end() && it->second.first == stamp)
            return it->second.second;

        juce::String text;
        std::unique_ptr<juce::AudioFormatReader> reader (processor.formatManager.createReaderFor (file));

        if (reader != nullptr && reader->sampleRate > 0.0)
        {
            const double ms = 1000.0 * (double) reader->lengthInSamples / reader->sampleRate;
            text = juce::String (reader->sampleRate / 1000.0, 1) + " kHz" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                 + (ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms");
        }

        audioInfos[key] = { stamp, text };
        return text;
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
    std::map<juce::String, std::pair<juce::int64, juce::String>> audioInfos;
    bool pickOnClick = false, dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelList)
};
