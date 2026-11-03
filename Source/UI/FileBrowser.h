#pragma once

#include "Theme.h"

//==============================================================================
/** Where models and impulse responses are looked for, and how to step through them. */
namespace FileLibrary
{
    constexpr int maxEntries = 500;

    inline const juce::String modelWildcard = "*.nam";
    inline const juce::String irWildcard    = "*.wav;*.aif;*.aiff";

    /** Documents/tini/Models or Documents/tini/IRs: the local library folders. */
    inline juce::File libraryFolder (bool models)
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("tini").getChildFile (models ? "Models" : "IRs");
    }

    /** The folder of the loaded file; without one, the library folder (if it exists). */
    inline juce::File folderFor (const juce::File& loaded, bool models)
    {
        if (loaded.existsAsFile())
            return loaded.getParentDirectory();

        const auto library = libraryFolder (models);
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
    inline juce::File neighbour (const juce::File& current, bool models, int delta)
    {
        const auto files = list (folderFor (current, models), models ? modelWildcard : irWildcard);
        if (files.isEmpty())
            return {};

        const int index = files.indexOf (current);
        if (index < 0)
            return files[delta > 0 ? 0 : files.size() - 1];

        return files[(index + delta + files.size()) % files.size()];
    }
}

//==============================================================================
/** A scrolling list of the models or impulse responses in one folder. The loaded file is marked; a
    click (or Enter on the selected line) calls onPick. The folder is scanned when it changes and when
    the list is shown, never while it is hidden. */
class FileBrowserList : public juce::Component, private juce::ListBoxModel
{
public:
    static constexpr int rowHeight = 30, headerHeight = 44;

    /** `models` selects the .nam list; otherwise impulse responses. */
    explicit FileBrowserList (bool models)
        : isModels (models), wildcard (models ? FileLibrary::modelWildcard : FileLibrary::irWildcard)
    {
        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getVerticalScrollBar().setAutoHide (true);
        addAndMakeVisible (list);
    }

    ~FileBrowserList() override { list.setModel (nullptr); }

    /** Called with the file the user picked. */
    std::function<void (const juce::File&)> onPick;

    /** Marks the loaded file; shows its folder (rescanning only if the folder changed). */
    void setCurrentFile (const juce::File& file)
    {
        const auto newFolder = FileLibrary::folderFor (file, isModels);
        const bool folderChanged = newFolder != folder;
        current = file;

        if (folderChanged)
            rescan (newFolder);
        else
            list.repaint();
    }

    /** Reads the folder again (new files, removed files). */
    void rescan() { rescan (folder); }

    int getNumRows() override { return files.size(); }
    int getNumFiles() const { return files.size(); }
    juce::File getFile (int row) const { return files[row]; }
    const juce::File& getFolder() const { return folder; }

    void visibilityChanged() override
    {
        if (isVisible())
            rescan();   // files may have been added while the list was hidden
    }

    void paint (juce::Graphics& g) override
    {
        auto head = getLocalBounds().removeFromTop (headerHeight).reduced (4, 0);

        g.setColour (Theme::textMid);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (isModels ? "Models" : "Impulse responses", head.removeFromTop (22), juce::Justification::bottomLeft, false);

        g.setColour (Theme::textLo);
        g.setFont (Theme::ui (11.0f));
        g.drawText (folder.isDirectory() ? folder.getFullPathName() : juce::String ("No folder yet"), head,
                    juce::Justification::topLeft, true);

        if (files.isEmpty())
        {
            auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (4, 8);
            g.setColour (Theme::textLo);
            g.setFont (Theme::ui (12.0f));

            const auto kind = isModels ? juce::String (".nam files") : juce::String ("impulse responses (.wav)");
            g.drawFittedText (folder.isDirectory() ? "No " + kind + " in this folder."
                                                   : "Put your " + kind + " in\n" + FileLibrary::libraryFolder (isModels).getFullPathName()
                                                         + "\nor load one with the button above.",
                              body, juce::Justification::topLeft, 6);
        }
    }

    void resized() override { list.setBounds (getLocalBounds().withTrimmedTop (headerHeight)); }

private:
    void rescan (const juce::File& newFolder)
    {
        folder = newFolder;
        files = FileLibrary::list (folder, wildcard);
        list.updateContent();

        const int row = files.indexOf (current);
        if (row >= 0)
            list.scrollToEnsureRowIsOnscreen (row);

        list.repaint();
        repaint();
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

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { pick (row); }
    void returnKeyPressed (int row) override { pick (row); }

    void pick (int row)
    {
        if (juce::isPositiveAndBelow (row, files.size()) && onPick)
            onPick (files[row]);
    }

    bool isModels;
    juce::String wildcard;
    juce::ListBox list;
    juce::Array<juce::File> files;
    juce::File folder, current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBrowserList)
};
