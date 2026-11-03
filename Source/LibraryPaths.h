#pragma once

#include <JuceHeader.h>

//==============================================================================
/** The local library: three folders under Documents/tini that tını watches and the user can fill
    from Explorer or from inside the app.

        Amps     NAM captures of whole amps (the amp slot)
        Pedals   NAM captures of pedals (the Capture pedal)
        IRs      cabinet impulse responses

    Nothing here touches the UI or the audio thread. */
namespace FileLibrary
{
    enum class Kind { amps, pedals, irs };

    inline const juce::String modelWildcard = "*.nam";
    inline const juce::String irWildcard    = "*.wav;*.aif;*.aiff;*.flac";

    /** Documents/tini. Tests point it at a temporary folder so they never touch the real library. */
    inline juce::File& rootOverride()
    {
        static juce::File folder;
        return folder;
    }

    inline juce::File root()
    {
        if (rootOverride() != juce::File())
            return rootOverride();

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("tini");
    }

    /** Deleting from the app moves the file to the Recycle Bin / Trash. Tests switch this off. */
    inline bool& useTrash()
    {
        static bool on = true;
        return on;
    }

    /** Where downloads usually land: the file chooser starts here when there is nothing better. */
    inline juce::File downloadsFolder()
    {
        const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
        return downloads.isDirectory() ? downloads : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    inline bool isModelKind (Kind kind)                 { return kind != Kind::irs; }

    /** True if `file` is one of the files `kind` can hold (by extension). */
    inline bool fits (const juce::File& file, Kind kind)
    {
        return isModelKind (kind) ? file.hasFileExtension ("nam") : file.hasFileExtension ("wav;aif;aiff;flac");
    }

    inline const juce::String& wildcardFor (Kind kind)  { return isModelKind (kind) ? modelWildcard : irWildcard; }

    inline juce::String folderName (Kind kind)
    {
        switch (kind)
        {
            case Kind::amps:   return "Amps";
            case Kind::pedals: return "Pedals";
            case Kind::irs:    return "IRs";
        }
        return {};
    }

    /** What the browser calls the kind in its header. */
    inline juce::String title (Kind kind)
    {
        switch (kind)
        {
            case Kind::amps:   return "Amp models";
            case Kind::pedals: return "Pedal models";
            case Kind::irs:    return "Impulse responses";
        }
        return {};
    }

    inline juce::File libraryFolder (Kind kind) { return root().getChildFile (folderName (kind)); }

    /** Creates the three folders. Older versions kept amp models in "Models": that folder is renamed to
        "Amps" once (only if "Amps" does not exist yet). */
    inline void ensureFolders()
    {
        const auto legacy = root().getChildFile ("Models");
        const auto amps = libraryFolder (Kind::amps);

        if (legacy.isDirectory() && ! amps.exists())
            legacy.moveFileTo (amps);

        for (auto kind : { Kind::amps, Kind::pedals, Kind::irs })
            libraryFolder (kind).createDirectory();
    }

    /** A path saved by an older version may point into the old "Models" folder: follow it to "Amps". */
    inline juce::File resolveSaved (const juce::File& saved)
    {
        if (saved.existsAsFile() || saved == juce::File())
            return saved;

        if (saved.getParentDirectory() == root().getChildFile ("Models"))
        {
            const auto moved = libraryFolder (Kind::amps).getChildFile (saved.getFileName());
            if (moved.existsAsFile())
                return moved;
        }

        return saved;
    }

    /** Which library a file belongs in by its extension; false for anything tini cannot use. */
    inline bool kindOfFile (const juce::File& file, Kind& kind)
    {
        if (file.hasFileExtension ("nam"))
        {
            kind = Kind::amps;
            return true;
        }

        if (file.hasFileExtension ("wav;aif;aiff;flac"))
        {
            kind = Kind::irs;
            return true;
        }

        return false;
    }

    /** Puts `source` into the library folder of `kind` and returns the copy; empty on failure. A file that
        already sits in that folder is returned as it is. If a file of the same name exists, the same
        content is reused, different content gets a numbered name ("name (2).nam"). */
    inline juce::File importFile (const juce::File& source, Kind kind)
    {
        if (! source.existsAsFile())
            return {};

        ensureFolders();
        const auto folder = libraryFolder (kind);

        if (source.getParentDirectory() == folder)
            return source;

        auto target = folder.getChildFile (source.getFileName());

        if (target.existsAsFile())
        {
            if (target.hasIdenticalContentTo (source))
                return target;

            target = target.getNonexistentSibling (true);
        }

        return source.copyFileTo (target) ? target : juce::File();
    }

    /** Removes a library file: to the Recycle Bin / Trash (or for good when useTrash() is off). */
    inline bool removeFile (const juce::File& file)
    {
        return useTrash() ? file.moveToTrash() : file.deleteFile();
    }

    /** Renames `file` inside its folder, keeping the extension. Refuses empty names and names in use. */
    inline juce::Result renameFile (const juce::File& file, const juce::String& newBaseName, juce::File& renamed)
    {
        const auto name = juce::File::createLegalFileName (newBaseName.trim()).trim();
        if (name.isEmpty())
            return juce::Result::fail ("Type a name.");

        const auto target = file.getParentDirectory().getChildFile (name + file.getFileExtension());
        if (target == file)
        {
            renamed = file;
            return juce::Result::ok();
        }

        if (target.exists())
            return juce::Result::fail ("A file with that name already exists.");

        if (! file.moveFileTo (target))
            return juce::Result::fail ("The file could not be renamed.");

        renamed = target;
        return juce::Result::ok();
    }
}
