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

    /** The text of the first "metadata" object in `text` (braces matched, strings respected); empty if none. */
    inline juce::String findMetadataObject (const juce::String& text)
    {
        int from = 0;

        for (;;)
        {
            const int key = text.indexOf (from, "\"metadata\"");
            if (key < 0)
                return {};

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
                    return text.substring (pos, i + 1);
            }

            return {};   // cut off
        }
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
