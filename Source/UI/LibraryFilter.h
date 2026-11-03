#pragma once

#include <JuceHeader.h>
#include "../LibraryPaths.h"

//==============================================================================
/** Filtering and sorting of the library lists, without any UI.

    A file is described by its "facts" (what a .nam file says about itself, or the header of an impulse
    response). Each facet (make, model, tone, ...) turns the facts into one value; a filter keeps a set of
    wanted values per facet. Inside one facet the values are OR-ed (Make = Marshall or Fender), between
    facets they are AND-ed (Make = Marshall and Tone = crunch). A file without a value for a facet has the
    value "Unknown", so no file becomes unreachable. */
namespace LibraryFilter
{
    enum class Facet { make, model, tone, type, rate, length, channels };
    enum class Sort  { name, make, newest, length };

    /** What the header of an impulse response says. */
    struct IrFacts
    {
        bool found = false;
        double sampleRate = 0.0;
        double lengthMs = 0.0;
        int channels = 0;
    };

    struct Facts
    {
        NamMetadata::Info info;
        IrFacts ir;
    };

    /** The value of a file for a facet: `key` is what filters compare (lower case), `label` is shown. */
    struct Value
    {
        juce::String key, label;
    };

    /** One choice of a facet in the list of choices: how many files have it (among the files that pass
        all the other filters). */
    struct Option
    {
        juce::String key, label;
        int count = 0;
    };

    inline constexpr const char* unknownLabel = "Unknown";

    /** The line of a facet in the filter bar. */
    inline juce::String facetName (Facet f)
    {
        switch (f)
        {
            case Facet::make:     return "Make";
            case Facet::model:    return "Model";
            case Facet::tone:     return "Tone";
            case Facet::type:     return "Type";
            case Facet::rate:     return "Rate";
            case Facet::length:   return "Length";
            case Facet::channels: return "Channels";
        }
        return {};
    }

    inline juce::String sortName (Sort s)
    {
        switch (s)
        {
            case Sort::name:   return "Name";
            case Sort::make:   return "Make";
            case Sort::newest: return "Newest";
            case Sort::length: return "Length";
        }
        return {};
    }

    /** "hi_gain" -> "Hi gain", "amp_cab" -> "Amp cab". */
    inline juce::String prettify (const juce::String& raw)
    {
        const auto text = raw.replaceCharacter ('_', ' ').trim();
        return text.substring (0, 1).toUpperCase() + text.substring (1);
    }

    //==========================================================================
    /** The facets worth offering for a section of the library, in the order of the bar. */
    inline std::vector<Facet> facetsFor (FileLibrary::Kind kind)
    {
        switch (kind)
        {
            case FileLibrary::Kind::amps:   return { Facet::make, Facet::model, Facet::tone, Facet::type };
            case FileLibrary::Kind::pedals: return { Facet::make, Facet::model, Facet::tone };
            case FileLibrary::Kind::other:  return { Facet::type, Facet::make, Facet::model, Facet::tone };
            case FileLibrary::Kind::cabs:   return { Facet::rate, Facet::length, Facet::channels };
        }
        return {};
    }

    inline std::vector<Sort> sortsFor (FileLibrary::Kind kind)
    {
        if (kind == FileLibrary::Kind::cabs)
            return { Sort::name, Sort::length, Sort::newest };

        return { Sort::name, Sort::make, Sort::newest };
    }

    //==========================================================================
    inline Value unknownValue() { return { {}, unknownLabel }; }

    /** Impulse responses up to 100 ms are short (a cab), up to 400 ms medium, longer ones are long (room, spring). */
    inline Value lengthValue (double ms)
    {
        if (ms < 100.0)  return { "short",  "Short (under 100 ms)" };
        if (ms <= 400.0) return { "medium", "Medium (100-400 ms)" };
        return { "long", "Long (over 400 ms)" };
    }

    inline Value valueOf (Facet facet, const Facts& facts)
    {
        auto fromText = [] (const juce::String& text, bool pretty) -> Value
        {
            const auto t = text.trim();
            return t.isEmpty() ? unknownValue() : Value { t.toLowerCase(), pretty ? prettify (t) : t };
        };

        switch (facet)
        {
            case Facet::make:  return fromText (facts.info.make, false);
            case Facet::model: return fromText (facts.info.model, false);
            case Facet::tone:  return fromText (facts.info.toneType, true);
            case Facet::type:  return fromText (facts.info.gearType, true);

            case Facet::rate:
            {
                if (! facts.ir.found || facts.ir.sampleRate <= 0.0)
                    return unknownValue();

                const int hz = juce::roundToInt (facts.ir.sampleRate);
                const double khz = hz / 1000.0;
                return { juce::String (hz), juce::String (khz, hz % 1000 == 0 ? 0 : 1) + " kHz" };
            }

            case Facet::length:
                return facts.ir.found ? lengthValue (facts.ir.lengthMs) : unknownValue();

            case Facet::channels:
            {
                if (! facts.ir.found || facts.ir.channels <= 0)
                    return unknownValue();

                const int n = facts.ir.channels;
                return { juce::String (n), n == 1 ? "Mono" : n == 2 ? "Stereo" : juce::String (n) + " channels" };
            }
        }

        return unknownValue();
    }

    //==========================================================================
    /** What is wanted: values per facet, the order, and "only what is in use". */
    struct State
    {
        std::map<Facet, std::set<juce::String>> selected;
        Sort sort = Sort::name;
        bool onlyInUse = false;

        int count (Facet f) const
        {
            const auto it = selected.find (f);
            return it == selected.end() ? 0 : (int) it->second.size();
        }

        bool has (Facet f, const juce::String& key) const
        {
            const auto it = selected.find (f);
            return it != selected.end() && it->second.count (key) > 0;
        }

        /** True if something narrows the list (the order does not). */
        bool isActive() const
        {
            if (onlyInUse)
                return true;

            for (const auto& entry : selected)
                if (! entry.second.empty())
                    return true;

            return false;
        }

        void toggle (Facet f, const juce::String& key)
        {
            auto& set = selected[f];
            if (set.erase (key) == 0)
                set.insert (key);
        }

        void set (Facet f, std::set<juce::String> keys)
        {
            if (keys.empty())
                selected.erase (f);
            else
                selected[f] = std::move (keys);
        }

        /** Drops the wanted values; the order stays. */
        void clear()
        {
            selected.clear();
            onlyInUse = false;
        }
    };

    /** True if the file passes the filter. A facet in `skip` is left out (to count its own options). */
    inline bool matches (const State& state, const Facts& facts, bool inUse, const std::vector<Facet>& facets,
                         const Facet* skip = nullptr)
    {
        if (state.onlyInUse && ! inUse)
            return false;

        for (auto facet : facets)
        {
            if (skip != nullptr && *skip == facet)
                continue;

            const auto it = state.selected.find (facet);
            if (it != state.selected.end() && ! it->second.empty() && it->second.count (valueOf (facet, facts).key) == 0)
                return false;
        }

        return true;
    }

    /** The choices of a facet from the counts per value, in a natural order: numbers ascending for the
        sample rate, short to long for the length, alphabetical for the rest; Unknown always last.
        Values that are wanted but have no file any more stay in the list (so they can be un-ticked). */
    inline std::vector<Option> orderedOptions (Facet facet, std::map<juce::String, Option> byKey, const State& state)
    {
        for (const auto& key : state.selected.count (facet) ? state.selected.at (facet) : std::set<juce::String>())
            if (byKey.count (key) == 0)
                byKey[key] = { key, key.isEmpty() ? juce::String (unknownLabel) : key, 0 };

        std::vector<Option> options;
        for (auto& entry : byKey)
            options.push_back (entry.second);

        auto rank = [facet] (const Option& o)
        {
            if (facet == Facet::length)
                return o.key == "short" ? 0 : o.key == "medium" ? 1 : 2;
            return 0;
        };

        std::stable_sort (options.begin(), options.end(), [&] (const Option& a, const Option& b)
        {
            if (a.key.isEmpty() != b.key.isEmpty())
                return b.key.isEmpty();                       // Unknown last

            if (facet == Facet::rate || facet == Facet::channels)
                return a.key.getIntValue() < b.key.getIntValue();

            if (facet == Facet::length)
                return rank (a) < rank (b);

            return a.label.compareIgnoreCase (b.label) < 0;
        });

        return options;
    }
}
