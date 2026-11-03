#pragma once

#include <JuceHeader.h>
#include "NamMetadata.h"

//==============================================================================
/** The local library: four folders under Documents/tini that tını watches and the user can fill
    from Explorer or from inside the app.

        Amps     NAM captures of amps and pre-amps (the amp slot)
        Pedals   NAM captures of pedals (each one becomes a pedal of its own)
        Cabs     cabinet impulse responses
        Other    NAM captures of anything else (amp + cab, studio chains, ...)

    Nothing here touches the UI or the audio thread. */
namespace FileLibrary
{
    enum class Kind { amps, pedals, cabs, other };

    inline constexpr Kind allKinds[] = { Kind::amps, Kind::pedals, Kind::cabs, Kind::other };

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

    inline bool isModelKind (Kind kind)                 { return kind != Kind::cabs; }

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
            case Kind::cabs:   return "Cabs";
            case Kind::other:  return "Other";
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
            case Kind::cabs:   return "Cabinet IRs";
            case Kind::other:  return "Other models";
        }
        return {};
    }

    inline juce::File libraryFolder (Kind kind) { return root().getChildFile (folderName (kind)); }

    /** Creates the library folders. Older versions used other names: "Models" is renamed to "Amps" and
        "IRs" to "Cabs", once (only if the new folder does not exist yet). */
    inline void ensureFolders()
    {
        const std::pair<const char*, Kind> renamed[] = { { "Models", Kind::amps }, { "IRs", Kind::cabs } };

        for (const auto& [oldName, kind] : renamed)
        {
            const auto legacy = root().getChildFile (oldName);
            const auto target = libraryFolder (kind);

            if (legacy.isDirectory() && ! target.exists())
                legacy.moveFileTo (target);
        }

        for (auto kind : allKinds)
            libraryFolder (kind).createDirectory();
    }

    /** A path saved by an older version may point into a renamed folder ("Models" -> "Amps", "IRs" ->
        "Cabs"): follow it. */
    inline juce::File resolveSaved (const juce::File& saved)
    {
        if (saved.existsAsFile() || saved == juce::File())
            return saved;

        const std::pair<const char*, Kind> renamed[] = { { "Models", Kind::amps }, { "IRs", Kind::cabs } };

        for (const auto& [oldName, kind] : renamed)
        {
            if (saved.getParentDirectory() == root().getChildFile (oldName))
            {
                const auto moved = libraryFolder (kind).getChildFile (saved.getFileName());
                if (moved.existsAsFile())
                    return moved;
            }
        }

        return saved;
    }

    /** The library kind of a NAM gear type; false if the type is empty (the caller picks a default). */
    inline bool kindOfGearType (const juce::String& gearType, Kind& kind)
    {
        switch (NamMetadata::sectionFor (gearType))
        {
            case NamMetadata::Section::amps:    kind = Kind::amps;   return true;
            case NamMetadata::Section::pedals:  kind = Kind::pedals; return true;
            case NamMetadata::Section::other:   kind = Kind::other;  return true;
            case NamMetadata::Section::none:    break;
        }
        return false;
    }

    /** Which library a file belongs in; false for anything tını cannot use. An audio file is a cab IR.
        A .nam file goes where its metadata says (amp / pre-amp: Amps, pedal: Pedals, anything else:
        Other); without metadata it goes to `namDefault`. */
    inline bool kindOfFile (const juce::File& file, Kind& kind, Kind namDefault = Kind::amps)
    {
        if (file.hasFileExtension ("nam"))
        {
            kind = namDefault;
            kindOfGearType (NamMetadata::read (file).gearType, kind);
            return true;
        }

        if (file.hasFileExtension ("wav;aif;aiff;flac"))
        {
            kind = Kind::cabs;
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

    /** Moves a model to the folder of `kind` (a numbered name if the name is taken) and sets its gear type
        to match, unless the type already belongs to that section. Returns the new file; empty on failure. */
    inline juce::File moveToKind (const juce::File& file, Kind kind)
    {
        if (! file.existsAsFile() || ! fits (file, kind))
            return {};

        ensureFolders();
        auto target = libraryFolder (kind).getChildFile (file.getFileName());

        if (target == file)
            return file;

        if (target.existsAsFile())
            target = target.getNonexistentSibling (true);

        if (! file.moveFileTo (target))
            return {};

        if (isModelKind (kind))
        {
            auto info = NamMetadata::read (target);
            Kind now = Kind::amps;
            if (! (kindOfGearType (info.gearType, now) && now == kind))
            {
                info.gearType = kind == Kind::pedals ? "pedal" : kind == Kind::amps ? "amp" : "amp_cab";
                NamMetadata::write (target, info);
            }
        }

        return target;
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
