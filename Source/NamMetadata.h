#pragma once

#include <JuceHeader.h>

//==============================================================================
/** What a .nam file says about itself: the "metadata" object (gear type, make, model, tone type, name).
    Only that object is read and parsed, never the weights, so asking is cheap even for big files. */
namespace NamMetadata
{
    struct Info
    {
        bool found = false;          // the file has a readable metadata object
        juce::String name;           // "name" of the capture
        juce::String gearType;       // amp, pedal, pedal_amp, amp_cab, amp_pedal_cab, preamp, studio
        juce::String make, model;    // gear_make, gear_model
        juce::String toneType;       // clean, overdrive, crunch, hi_gain, fx

        /** "Make · Model" (either part may be missing). */
        juce::String gearLine() const
        {
            juce::StringArray parts;
            if (make.isNotEmpty())  parts.add (make);
            if (model.isNotEmpty()) parts.add (model);
            return parts.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));
        }

        /** "Make · Model · tone type": the line under a file's name in the library. */
        juce::String detailLine() const
        {
            auto line = gearLine();
            const auto tone = toneType.replaceCharacter ('_', ' ');

            if (tone.isNotEmpty())
                line += (line.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) + tone;

            return line;
        }

        /** The short line of a tile: the tone type ("hi gain"), else the gear's model, else its make. */
        juce::String shortLine() const
        {
            const auto tone = toneType.replaceCharacter ('_', ' ');
            const auto line = tone.isNotEmpty() ? tone : model.isNotEmpty() ? model : make;
            return line.substring (0, 1).toUpperCase() + line.substring (1);
        }

        bool operator== (const Info& other) const
        {
            return found == other.found && name == other.name && gearType == other.gearType
                && make == other.make && model == other.model && toneType == other.toneType;
        }
        bool operator!= (const Info& other) const { return ! (*this == other); }
    };

    /** Where the first "metadata" object sits in `text`: [start, end) covers "{ ... }" (braces matched,
        strings respected). False if there is none (or it is cut off). */
    inline bool findMetadataRange (const juce::String& text, int& start, int& end)
    {
        int from = 0;

        for (;;)
        {
            const int key = text.indexOf (from, "\"metadata\"");
            if (key < 0)
                return false;

            int pos = key + 10;
            while (pos < text.length() && juce::CharacterFunctions::isWhitespace (text[pos]))
                ++pos;

            if (pos >= text.length() || text[pos] != ':')
            {
                from = key + 10;
                continue;
            }

            ++pos;
            while (pos < text.length() && juce::CharacterFunctions::isWhitespace (text[pos]))
                ++pos;

            if (pos >= text.length() || text[pos] != '{')
            {
                from = key + 10;
                continue;
            }

            int depth = 0;
            bool inString = false;

            for (int i = pos; i < text.length(); ++i)
            {
                const auto c = text[i];

                if (inString)
                {
                    if (c == '\\')      ++i;
                    else if (c == '"')  inString = false;
                    continue;
                }

                if (c == '"')       inString = true;
                else if (c == '{')  ++depth;
                else if (c == '}' && --depth == 0)
                {
                    start = pos;
                    end = i + 1;
                    return true;
                }
            }

            return false;   // cut off
        }
    }

    /** The text of the first "metadata" object in `text`; empty if none. */
    inline juce::String findMetadataObject (const juce::String& text)
    {
        int start = 0, end = 0;
        return findMetadataRange (text, start, end) ? text.substring (start, end) : juce::String();
    }

    /** Reads the metadata of already loaded file text. */
    inline Info parse (const juce::String& fileText)
    {
        Info info;
        const auto object = findMetadataObject (fileText);
        if (object.isEmpty())
            return info;

        const auto json = juce::JSON::parse (object);
        if (! json.isObject())
            return info;

        auto text = [&json] (const char* key) { return json.getProperty (key, juce::var()).toString().trim(); };

        info.found = true;
        info.name = text ("name");
        info.gearType = text ("gear_type").toLowerCase();
        info.make = text ("gear_make");
        info.model = text ("gear_model");
        info.toneType = text ("tone_type").toLowerCase();
        return info;
    }

    /** Reads the metadata of a .nam file. The metadata normally sits at the start of the file: only the
        first 256 KB are read unless it is not there. Returns an Info with found == false for a file
        without metadata (or one that cannot be read). */
    inline Info read (const juce::File& file)
    {
        juce::FileInputStream stream (file);
        if (stream.failedToOpen())
            return {};

        constexpr int head = 256 * 1024;
        juce::MemoryBlock block;
        stream.readIntoMemoryBlock (block, head);

        auto info = parse (juce::String::createStringFromData (block.getData(), (int) block.getSize()));

        if (! info.found && stream.getTotalLength() > head)
        {
            stream.setPosition (0);
            info = parse (stream.readEntireStreamAsString());
        }

        return info;
    }

    //==========================================================================
    // Editing. Only the text of the "metadata" object is touched: the weights and every other key of the
    // file stay byte for byte as they were.

    /** The gear types and tone types a capture usually carries (value in the file, label shown). */
    struct Choice { const char* value; const char* label; };

    inline constexpr Choice gearTypes[] = { { "amp", "Amp" }, { "preamp", "Pre-amp" }, { "pedal", "Pedal" },
                                            { "pedal_amp", "Pedal + amp" }, { "amp_cab", "Amp + cab" },
                                            { "amp_pedal_cab", "Amp + pedal + cab" }, { "studio", "Studio" } };

    inline constexpr Choice toneTypes[] = { { "clean", "Clean" }, { "overdrive", "Overdrive" }, { "crunch", "Crunch" },
                                            { "hi_gain", "Hi gain" }, { "fx", "FX" } };

    namespace detail
    {
        /** End of the JSON value that starts at `pos` in `text` (a string, an object / array or a bare token). */
        inline int valueEnd (const juce::String& text, int pos)
        {
            const int n = text.length();
            if (pos >= n)
                return n;

            if (text[pos] == '"' || text[pos] == '{' || text[pos] == '[')
            {
                int depth = 0;
                bool inString = false;

                for (int i = pos; i < n; ++i)
                {
                    const auto c = text[i];

                    if (inString)
                    {
                        if (c == '\\')      ++i;
                        else if (c == '"')  { inString = false; if (depth == 0) return i + 1; }
                        continue;
                    }

                    if (c == '"')                    inString = true;
                    else if (c == '{' || c == '[')   ++depth;
                    else if ((c == '}' || c == ']') && --depth == 0)
                        return i + 1;
                }

                return n;
            }

            int i = pos;
            while (i < n && text[i] != ',' && text[i] != '}' && text[i] != ']' && ! juce::CharacterFunctions::isWhitespace (text[i]))
                ++i;
            return i;
        }

        /** Sets `key` of the object `object` ("{ ... }") to `token` (a JSON value): the old value is replaced,
            or the key is added at the end. Everything else stays as it was. */
        inline juce::String setMember (const juce::String& object, const juce::String& key, const juce::String& token)
        {
            const int n = object.length();
            int depth = 0;
            bool expectKey = false;

            for (int i = 0; i < n; ++i)
            {
                const auto c = object[i];

                if (c == '"')
                {
                    const int begin = i;
                    for (++i; i < n && object[i] != '"'; ++i)
                        if (object[i] == '\\')
                            ++i;

                    if (depth == 1 && expectKey)
                    {
                        expectKey = false;

                        if (object.substring (begin + 1, i) == key)
                        {
                            int pos = i + 1;
                            while (pos < n && juce::CharacterFunctions::isWhitespace (object[pos]))
                                ++pos;

                            if (pos < n && object[pos] == ':')
                            {
                                ++pos;
                                while (pos < n && juce::CharacterFunctions::isWhitespace (object[pos]))
                                    ++pos;

                                return object.substring (0, pos) + token + object.substring (valueEnd (object, pos));
                            }
                        }
                    }
                }
                else if (c == '{' || c == '[')
                {
                    ++depth;
                    if (c == '{' && depth == 1)
                        expectKey = true;
                }
                else if (c == '}' || c == ']')
                {
                    --depth;
                }
                else if (c == ',' && depth == 1)
                {
                    expectKey = true;
                }
            }

            // not there: add it before the closing brace
            int close = object.lastIndexOfChar ('}');
            if (close < 0)
                return object;

            int last = close;
            while (last > 0 && juce::CharacterFunctions::isWhitespace (object[last - 1]))
                --last;

            const bool empty = object[last - 1] == '{';
            return object.substring (0, last) + (empty ? "" : ", ") + "\"" + key + "\": " + token + object.substring (last);
        }

        inline juce::String tokenFor (const juce::String& value)
        {
            const auto v = value.trim();
            return v.isEmpty() ? juce::String ("null") : juce::JSON::toString (juce::var (v), true);
        }
    }

    /** Writes the make, model, gear type and tone type of `info` into the metadata of the .nam file `file`
        (the keys that differ from what the file says now; "name" is not touched). An empty value becomes null.
        The file is replaced in one step, so an error leaves it as it was. */
    inline juce::Result write (const juce::File& file, const Info& info)
    {
        auto text = file.loadFileAsString();
        if (text.isEmpty())
            return juce::Result::fail ("The file could not be read.");

        const auto before = parse (text);
        struct Edit { const char* key; const juce::String* was; const juce::String* now; };
        const Edit edits[] = { { "gear_make", &before.make, &info.make },
                               { "gear_model", &before.model, &info.model },
                               { "gear_type", &before.gearType, &info.gearType },
                               { "tone_type", &before.toneType, &info.toneType } };

        int start = 0, end = 0;
        const bool hasObject = findMetadataRange (text, start, end);
        juce::String object = hasObject ? text.substring (start, end) : juce::String ("{}");
        bool changed = false;

        for (const auto& edit : edits)
        {
            if (edit.was->trim() == edit.now->trim() && hasObject)
                continue;

            if (edit.now->trim().isEmpty() && edit.was->isEmpty())
                continue;

            object = detail::setMember (object, edit.key, detail::tokenFor (*edit.now));
            changed = true;
        }

        if (! changed)
            return juce::Result::ok();

        juce::String result;

        if (hasObject)
        {
            result = text.substring (0, start) + object + text.substring (end);
        }
        else
        {
            // no metadata yet: add the object as the last member of the top-level object
            const int close = text.lastIndexOfChar ('}');
            if (close < 0)
                return juce::Result::fail ("This is not a model file.");

            int last = close;
            while (last > 0 && juce::CharacterFunctions::isWhitespace (text[last - 1]))
                --last;

            result = text.substring (0, last) + (text[last - 1] == '{' ? "" : ", ") + "\"metadata\": " + object + text.substring (last);
        }

        // never write something that does not parse back to what was asked for
        const auto check = parse (result);
        if (! check.found || check.make.trim() != info.make.trim() || check.model.trim() != info.model.trim()
            || check.gearType != info.gearType.trim().toLowerCase() || check.toneType != info.toneType.trim().toLowerCase())
            return juce::Result::fail ("The metadata could not be changed in this file.");

        juce::TemporaryFile temp (file);
        if (! temp.getFile().replaceWithText (result, false, false) || ! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("The file could not be written (is it read-only?).");

        return juce::Result::ok();
    }

    /** Which kind of file a gear type is (see FileLibrary::Kind): amps and pre-amps are amps, pedals are
        pedals, everything else (full rigs with a cab, studio chains, unknown) is "other".
        An empty gear type (no metadata) is Section::none: the caller decides. */
    enum class Section { none, amps, pedals, other };

    inline Section sectionFor (const juce::String& gearType)
    {
        if (gearType.isEmpty())
            return Section::none;

        if (gearType == "amp" || gearType == "preamp")
            return Section::amps;

        if (gearType == "pedal")
            return Section::pedals;

        return Section::other;
    }

    /** Reads metadata once per file version: the cache key is the path plus the modification time.
        get() never blocks: a file that is not known yet is read on a background thread, and the cache
        sends a change message (message thread) when new entries arrived. */
    class Cache : public juce::ChangeBroadcaster,
                  private juce::AsyncUpdater
    {
    public:
        Cache() : pool (1) {}

        ~Cache() override
        {
            pool.removeAllJobs (true, 2000);
            cancelPendingUpdate();
        }

        /** What is known about `file` now; found == false until its metadata has been read. */
        Info get (const juce::File& file)
        {
            const auto key = file.getFullPathName();
            const auto stamp = file.getLastModificationTime().toMilliseconds();

            const juce::ScopedLock lock (mutex);
            const auto it = entries.find (key);

            if (it != entries.end() && it->second.stamp == stamp)
                return it->second.info;

            if (pending.count (key) == 0)
            {
                pending.insert (key);
                pool.addJob ([this, file, key, stamp]
                {
                    auto info = read (file);

                    {
                        const juce::ScopedLock inner (mutex);
                        entries[key] = { stamp, std::move (info) };
                        pending.erase (key);
                    }

                    triggerAsyncUpdate();
                });
            }

            return {};
        }

        /** True once every requested file has been read. */
        bool isIdle() const
        {
            const juce::ScopedLock lock (mutex);
            return pending.empty();
        }

        /** Reads `file` right now, on the calling thread (where an answer is needed at once). */
        Info getNow (const juce::File& file)
        {
            const auto key = file.getFullPathName();
            const auto stamp = file.getLastModificationTime().toMilliseconds();

            {
                const juce::ScopedLock lock (mutex);
                const auto it = entries.find (key);
                if (it != entries.end() && it->second.stamp == stamp)
                    return it->second.info;
            }

            auto info = read (file);
            const juce::ScopedLock lock (mutex);
            entries[key] = { stamp, info };
            return info;
        }

    private:
        struct Entry
        {
            juce::int64 stamp = 0;
            Info info;
        };

        void handleAsyncUpdate() override { sendChangeMessage(); }

        mutable juce::CriticalSection mutex;
        std::map<juce::String, Entry> entries;
        std::set<juce::String> pending;
        juce::ThreadPool pool;
    };
}
