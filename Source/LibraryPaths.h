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

    /** The first folder level under the library folder of `kind` is a pack (one download, one amp, ...).
        Returns the pack of `file` ("" for a file that sits directly in the folder or outside it). */
    inline juce::String packOf (const juce::File& file, Kind kind)
    {
        const auto base = libraryFolder (kind);
        if (! file.isAChildOf (base))
            return {};

        auto folder = file.getParentDirectory();
        while (folder != base && folder.getParentDirectory() != base && folder != folder.getParentDirectory())
            folder = folder.getParentDirectory();

        return folder == base ? juce::String() : folder.getFileName();
    }

    /** The library kind whose folder holds `file`; false if it is not in the library. */
    inline bool kindHolding (const juce::File& file, Kind& kind)
    {
        for (auto k : allKinds)
            if (file.isAChildOf (libraryFolder (k)))
            {
                kind = k;
                return true;
            }
        return false;
    }

    /** A pack name that is safe as a folder name; "" if nothing is left of it. */
    inline juce::String legalPackName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).trim();
    }

    /** The folder of a pack in `kind` ("" = the library folder itself). */
    inline juce::File packFolder (Kind kind, const juce::String& pack)
    {
        const auto name = legalPackName (pack);
        return name.isEmpty() ? libraryFolder (kind) : libraryFolder (kind).getChildFile (name);
    }

    /** Puts `source` into the library folder of `kind` (into the subfolder `pack`, if given) and returns the
        copy; empty on failure. A file that already sits in the library folder of that kind is returned as it is.
        If a file of the same name exists, the same content is reused, different content gets a numbered name
        ("name (2).nam"). */
    inline juce::File importFile (const juce::File& source, Kind kind, const juce::String& pack = {})
    {
        if (! source.existsAsFile())
            return {};

        ensureFolders();
        const auto folder = packFolder (kind, pack);

        if (source.getParentDirectory() == folder || source.isAChildOf (libraryFolder (kind)))
            return source;

        if (! folder.createDirectory())
            return {};

        auto target = folder.getChildFile (source.getFileName());

        if (target.existsAsFile())
        {
            if (target.hasIdenticalContentTo (source))
                return target;

            target = target.getNonexistentSibling (true);
        }

        return source.copyFileTo (target) ? target : juce::File();
    }

    /** Deletes a pack folder that has nothing left in it (never the library folders themselves). */
    inline void removeIfEmpty (const juce::File& folder)
    {
        for (auto k : allKinds)
            if (folder == libraryFolder (k))
                return;

        if (folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
            folder.deleteFile();
    }

    /** Moves a library file to the folder of `kind` / `pack` (a numbered name if the name is taken). A model's
        gear type is set to match the new section, unless it already belongs there. Returns the new file; empty
        on failure. */
    inline juce::File moveTo (const juce::File& file, Kind kind, const juce::String& pack)
    {
        if (! file.existsAsFile() || ! fits (file, kind))
            return {};

        ensureFolders();
        const auto folder = packFolder (kind, pack);
        auto target = folder.getChildFile (file.getFileName());

        if (target == file)
            return file;

        if (! folder.createDirectory())
            return {};

        if (target.existsAsFile())
            target = target.getNonexistentSibling (true);

        const auto oldFolder = file.getParentDirectory();

        if (! file.moveFileTo (target))
            return {};

        removeIfEmpty (oldFolder);

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

    /** Moves a model to another section; its pack (if any) comes along. */
    inline juce::File moveToKind (const juce::File& file, Kind kind)
    {
        Kind from = Kind::amps;
        return moveTo (file, kind, kindHolding (file, from) ? packOf (file, from) : juce::String());
    }

    /** Removes a library file: to the Recycle Bin / Trash (or for good when useTrash() is off). */
    inline bool removeFile (const juce::File& file)
    {
        const auto folder = file.getParentDirectory();
        const bool gone = useTrash() ? file.moveToTrash() : file.deleteFile();

        if (gone)
            removeIfEmpty (folder);

        return gone;
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

    //==========================================================================
    // Packs: a download from tone3000 is a zip with dozens of captures. Dropping or adding the zip (or the
    // folder it was unpacked to) keeps the captures together in a pack folder named after it.

    constexpr int maxTreeEntries = 5000;

    /** Every matching file under `folder`, packs included, by path. */
    inline juce::Array<juce::File> listTree (const juce::File& folder, const juce::String& wildcard)
    {
        juce::Array<juce::File> files;

        if (folder.isDirectory())
        {
            files = folder.findChildFiles (juce::File::findFiles, true, wildcard);

            struct ByPath
            {
                static int compareElements (const juce::File& a, const juce::File& b)
                {
                    return a.getFullPathName().compareNatural (b.getFullPathName());
                }
            } byPath;
            files.sort (byPath);

            if (files.size() > maxTreeEntries)
                files.removeRange (maxTreeEntries, files.size() - maxTreeEntries);
        }

        return files;
    }

    inline bool isUsable (const juce::File& file) { return file.hasFileExtension ("nam;wav;aif;aiff;flac"); }
    inline bool isArchive (const juce::File& file) { return file.existsAsFile() && file.hasFileExtension ("zip"); }

    /** True if dragging these paths could add something: a model, an impulse response, a zip or a folder. */
    inline bool anythingToAdd (const juce::StringArray& paths)
    {
        for (const auto& path : paths)
        {
            const juce::File f (path);
            if (f.isDirectory() || isArchive (f) || isUsable (f))
                return true;
        }
        return false;
    }

    /** "Pack Name (1)" -> "Pack Name": browsers add that to a second download of the same file. */
    inline juce::String cleanPackName (juce::String name)
    {
        name = name.trim();
        if (name.endsWithChar (')'))
        {
            const int open = name.lastIndexOfChar ('(');
            if (open > 0 && name.substring (open + 1, name.length() - 1).containsOnly ("0123456789"))
                name = name.substring (0, open).trim();
        }
        return legalPackName (name);
    }

    /** The files behind a selection: zips are unpacked into a temporary folder (removed again with the batch),
        folders are walked, and every model / impulse response is paired with the pack it belongs to.

        A zip or a folder is a pack by its name. Loose files are one when two or more come from the same
        folder (an unpacked download) - not for the Downloads folder or other everyday places. */
    struct Batch
    {
        struct Item { juce::File file; juce::String pack; };

        explicit Batch (const juce::Array<juce::File>& inputs)
        {
            std::map<juce::String, juce::Array<juce::File>> loose;

            for (const auto& input : inputs)
            {
                if (input.isDirectory())
                {
                    addTree (input, cleanPackName (input.getFileName()));
                }
                else if (isArchive (input))
                {
                    if (temp == juce::File())
                        temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("tini-import-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

                    const auto target = temp.getChildFile (juce::String (++archives));
                    juce::ZipFile zip (input);

                    if (zip.getNumEntries() > 0 && target.createDirectory() && zip.uncompressTo (target).wasOk())
                        addTree (target, cleanPackName (input.getFileNameWithoutExtension()));
                }
                else if (isUsable (input))
                {
                    loose[input.getParentDirectory().getFullPathName() + (input.hasFileExtension ("nam") ? "|m" : "|a")].add (input);
                }
            }

            for (const auto& entry : loose)
            {
                const juce::File folder (entry.first.upToLastOccurrenceOf ("|", false, false));
                const auto pack = entry.second.size() >= 2 && ! isEverydayFolder (folder)
                                      ? cleanPackName (folder.getFileName()) : juce::String();

                for (const auto& f : entry.second)
                    items.push_back ({ f, pack });
            }
        }

        ~Batch()
        {
            if (temp != juce::File())
                temp.deleteRecursively();
        }

        std::vector<Item> items;

    private:
        static bool isEverydayFolder (const juce::File& folder)
        {
            for (auto place : { juce::File::userHomeDirectory, juce::File::userDocumentsDirectory,
                                juce::File::userDesktopDirectory, juce::File::userMusicDirectory })
                if (folder == juce::File::getSpecialLocation (place))
                    return true;

            return folder == downloadsFolder() || folder == folder.getParentDirectory();
        }

        void addTree (const juce::File& folder, const juce::String& pack)
        {
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*"))
                if (isUsable (f) && ! f.getFileName().startsWith ("._"))   // ._ files: macOS metadata inside zips
                    items.push_back ({ f, pack });
        }

        juce::File temp;
        int archives = 0;

        JUCE_DECLARE_NON_COPYABLE (Batch)
    };

    struct Imported { juce::File file; Kind kind; };

    /** Copies a batch into the library. `forced` puts every model / IR that fits it into that kind (a list that
        belongs to one folder); without it each file goes where it belongs (see kindOfFile; a model without
        metadata goes to `namDefault`). Anything that cannot be used is skipped. */
    inline std::vector<Imported> importBatch (const Batch& batch, const Kind* forced, Kind namDefault = Kind::amps)
    {
        std::vector<Imported> done;

        for (const auto& item : batch.items)
        {
            Kind kind = namDefault;

            if (forced != nullptr)
            {
                if (! fits (item.file, *forced))
                    continue;
                kind = *forced;
            }
            else if (! kindOfFile (item.file, kind, namDefault))
            {
                continue;
            }

            const auto copy = importFile (item.file, kind, item.pack);
            if (copy.existsAsFile())
                done.push_back ({ copy, kind });
        }

        return done;
    }

    //==========================================================================
    /** Tidy up: models in the wrong section go where their metadata says, and loose models that share a make
        and model (three or more) are put into a pack folder of their own. */
    struct TidyMove { juce::File file; Kind kind; juce::String pack; bool changesSection; };

    inline std::vector<TidyMove> planTidy()
    {
        struct Candidate { TidyMove move; bool loose; };
        std::vector<Candidate> all;
        std::map<juce::String, std::vector<size_t>> groups;   // "kind|pack" -> candidates that would make it up

        for (auto kind : { Kind::amps, Kind::pedals, Kind::other })
        {
            for (const auto& file : listTree (libraryFolder (kind), modelWildcard))
            {
                const auto info = NamMetadata::read (file);
                Kind target = kind;
                kindOfGearType (info.gearType, target);

                Candidate c { { file, target, packOf (file, kind), target != kind }, false };
                c.loose = c.move.pack.isEmpty();

                if (c.loose)
                {
                    const auto make = info.make.trim(), model = info.model.trim();
                    juce::String name = model.isEmpty() ? make
                                      : (make.isEmpty() || make == model || model.startsWithIgnoreCase (make)) ? model
                                      : make + " " + model;

                    name = legalPackName (name);
                    if (name.isNotEmpty())
                    {
                        groups[juce::String ((int) target) + "|" + name].push_back (all.size());
                        c.move.pack = name;
                    }
                }

                all.push_back (c);
            }
        }

        for (const auto& group : groups)
            if (group.second.size() < 3)
                for (auto i : group.second)
                    all[i].move.pack = {};

        std::vector<TidyMove> needed;
        for (const auto& c : all)
            if (c.move.changesSection || (c.loose && c.move.pack.isNotEmpty()))
                needed.push_back (c.move);

        return needed;
    }
}
