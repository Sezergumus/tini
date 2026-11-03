#pragma once

#include <JuceHeader.h>

/** Describes every pedal the signal chain knows about. The UI builds its pedals from this list,
    so adding a new pedal type means adding an entry here (plus its parameters and DSP).

    postAmp only says where a pedal starts out: the user can drag pedals anywhere in the chain
    (before or after the amp), and that order is kept by the processor (see ChainOrder). */
namespace PedalCatalog
{
    /** Number of pedal types in all(). New types are only ever appended, so item numbers of older types
        keep their meaning inside one copy; a new type does change numItems (see orderFromString). */
    constexpr int count = 8;

    /** Pedal types that existed when chain orders were first saved (before the Capture pedal). */
    constexpr int firstGenerationTypes = 7;

    /** Type number of the Capture pedal (the last entry of all()). */
    constexpr int captureType = 7;

    /** How many copies of each built-in pedal type can be in the chain at once. Every copy has its own
        parameters, created up front (copy 0 keeps the plain ids, copy n >= 1 gets a "_<n+1>"
        suffix), so host automation keeps working while the chain itself stays dynamic. */
    constexpr int maxCopies = 3;

    /** NAM pedals are what a library is full of, so they get a much bigger pool than the built-in
        pedals (a host needs its parameters up front, so the pool cannot grow without end). */
    constexpr int namCopies = 12;

    /** Copies of one pedal type. */
    constexpr int copiesOfType (int type) { return type == captureType ? namCopies : maxCopies; }

    /** Chain items: copy * count + type for the first maxCopies copies of every type, then the amp
        (the item after them), then the extra copies of the NAM pedal. */
    constexpr int ampItem = count * maxCopies;
    constexpr int firstExtraItem = ampItem + 1;
    constexpr int numItems = firstExtraItem + (namCopies - maxCopies);

    /** The most pedals of any kind that fit in a chain order. */
    static_assert (numItems <= 64, "chain orders are checked with a 64 bit mask");

    /** A chain order lists every item once. Items whose pedal is not "added" are parked in it
        and skipped by the processor and the UI. */
    using ChainOrder = std::array<int, numItems>;

    inline bool isAmp (int item)          { return item == ampItem; }
    inline int  typeOf (int item)         { return item >= firstExtraItem ? captureType : item % count; }
    inline int  copyOf (int item)         { return item >= firstExtraItem ? maxCopies + (item - firstExtraItem) : item / count; }
    inline int  makeItem (int type, int copy)
    {
        return copy < maxCopies ? copy * count + type : firstExtraItem + (copy - maxCopies);   // copies past maxCopies are NAM pedals
    }

    /** True if every item 0..numItems-1 appears exactly once. */
    inline bool isValidOrder (const ChainOrder& order)
    {
        juce::uint64 seen = 0;
        for (int item : order)
        {
            if (item < 0 || item >= numItems)
                return false;
            seen |= (juce::uint64) 1 << item;
        }
        return seen == ((juce::uint64) 1 << numItems) - 1u;
    }

    /** "3,0,21,..." form used in saved state and preset files. */
    inline juce::String orderToString (const ChainOrder& order)
    {
        juce::StringArray parts;
        for (int item : order)
            parts.add (juce::String (item));
        return parts.joinIntoString (",");
    }

    struct Knob
    {
        const char* label;
        const char* paramID;
        int decimals;
        const char* suffix;
    };

    struct Pedal
    {
        const char* name;
        const char* subtitle;
        juce::uint32 colour;
        const char* onID;      // bypass switch (true = pedal active)
        const char* addedID;   // nullptr = permanent pedal that cannot be removed
        bool postAmp;
        std::vector<Knob> knobs;
        const char* category;  // shown in the add-pedal menu

        bool isRemovable() const { return addedID != nullptr; }
    };

    inline const std::vector<Pedal>& all()
    {
        static const std::vector<Pedal> pedals =
        {
            { "NOISE GATE", "TIGHT GATE",     0xffb28cff, "GATE_ON",   "GATE_ADDED",   false,
              { { "THRESH",  "GATE_THRESH", 0, " dB" }, { "RELEASE", "GATE_REL", 0, " ms" } }, "DYNAMICS" },

            { "COMPRESSOR", "STUDIO FET",     0xff3dd3ee, "COMP_ON",   nullptr,        false,
              { { "SUSTAIN", "COMP_SUST", 0, "" },      { "ATTACK",  "COMP_ATT",  0, " ms" },
                { "BLEND",   "COMP_MIX",  0, " %" },    { "LEVEL",   "COMP_LVL",  1, " dB" } }, "DYNAMICS" },

            { "BOOST",      "CLEAN BOOST",    0xff6fdc8c, "BOOST_ON",  "BOOST_ADDED",  false,
              { { "GAIN",    "BOOST_GAIN", 1, " dB" } }, "GAIN" },

            { "OVERDRIVE",  "TS-MOD CIRCUIT", 0xffffa43f, "OD_ON",     nullptr,        false,
              { { "GAIN",    "OD_GAIN",  1, "" },      { "TONE",    "OD_TONE",   1, "" },
                { "LEVEL",   "OD_LVL",   1, "" } }, "GAIN" },

            { "CHORUS",     "ANALOG BBD",     0xff5aa8ff, "CHORUS_ON", "CHORUS_ADDED", true,
              { { "RATE",    "CHORUS_RATE", 1, " Hz" }, { "DEPTH",   "CHORUS_DEPTH", 0, " %" },
                { "MIX",     "CHORUS_MIX",  0, " %" } }, "MODULATION" },

            { "DELAY",      "TAPE ECHO",      0xffff7ab8, "DELAY_ON",  "DELAY_ADDED",  true,
              { { "TIME",    "DELAY_TIME", 0, " ms" },  { "FEEDBACK", "DELAY_FB",    0, " %" },
                { "MIX",     "DELAY_MIX",  0, " %" } }, "TIME" },

            { "REVERB",     "ROOM / HALL",    0xff4be3c1, "REVERB_ON", "REVERB_ADDED", true,
              { { "SIZE",    "REVERB_SIZE", 0, " %" },  { "DAMP",    "REVERB_DAMP",  0, " %" },
                { "MIX",     "REVERB_MIX",  0, " %" } }, "SPACE" },

            // a Neural Amp Modeler capture of a pedal; the model is chosen on its page
            { "CAPTURE",    "NAM PEDAL",      0xffe0825f, "CAP_ON",    "CAP_ADDED",    false,
              { { "INPUT",   "CAP_IN",    1, " dB" },   { "OUTPUT",  "CAP_OUT",   1, " dB" },
                { "MIX",     "CAP_MIX",   0, " %" } }, "NAM MODEL" },
        };
        jassert ((int) pedals.size() == count);
        return pedals;
    }

    /** Where the chain starts out: the pre-amp pedals in catalog order, the amp, then the post-amp pedals. */
    inline ChainOrder defaultOrder()
    {
        ChainOrder order {};
        int n = 0;

        for (int i = 0; i < count; ++i)
            if (! all()[(size_t) i].postAmp)
                order[(size_t) n++] = i;

        order[(size_t) n++] = ampItem;

        for (int i = 0; i < count; ++i)
            if (all()[(size_t) i].postAmp)
                order[(size_t) n++] = i;

        // extra copies start out parked at the end
        for (int copy = 1; copy < maxCopies; ++copy)
            for (int i = 0; i < count; ++i)
                order[(size_t) n++] = makeItem (i, copy);

        for (int copy = maxCopies; copy < namCopies; ++copy)
            order[(size_t) n++] = makeItem (captureType, copy);

        return order;
    }

    //==========================================================================
    // Reading orders written by older versions

    /** Completes an order made of valid, distinct items: pedal types the old order did not know are added.
        Copy 0 of a new pre-amp pedal goes right before the amp (so it shows up there once the user adds
        it), everything else is parked at the end. */
    inline bool finishOrder (std::vector<int> items, ChainOrder& out)
    {
        auto has = [&items] (int item) { return std::find (items.begin(), items.end(), item) != items.end(); };

        for (int type = firstGenerationTypes; type < count; ++type)
            if (! has (type) && ! all()[(size_t) type].postAmp && has (ampItem))
                items.insert (std::find (items.begin(), items.end(), ampItem), type);

        for (int item = 0; item < numItems; ++item)
            if (! has (item))
                items.push_back (item);

        if ((int) items.size() != numItems)
            return false;

        ChainOrder candidate {};
        std::copy (items.begin(), items.end(), candidate.begin());

        if (! isValidOrder (candidate))
            return false;

        out = candidate;
        return true;
    }

    /** Parses orderToString output. Returns false (and leaves `out` alone) if it is not a valid order.
        Two older layouts are converted:
          - 25 entries (before the NAM pedal pool grew): the same item numbers, the new items are parked;
          - 22 entries (before the Capture pedal): 7 types x 3 copies + the amp, renumbered to the current
            layout (copy * count + type), and the new pedal is added. */
    inline bool orderFromString (const juce::String& text, ChainOrder& out)
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");
        parts.removeEmptyStrings();

        std::vector<int> values;
        for (const auto& part : parts)
            values.push_back (part.getIntValue());

        if ((int) values.size() == numItems)
        {
            ChainOrder parsed {};
            std::copy (values.begin(), values.end(), parsed.begin());

            if (! isValidOrder (parsed))
                return false;

            out = parsed;
            return true;
        }

        constexpr int secondGenerationItems = ampItem + 1;   // every type x maxCopies, then the amp

        if ((int) values.size() == secondGenerationItems)
        {
            std::vector<int> items;
            juce::uint64 seen = 0;

            for (int item : values)
            {
                if (item < 0 || item > ampItem || (seen & ((juce::uint64) 1 << item)) != 0)
                    return false;

                seen |= (juce::uint64) 1 << item;
                items.push_back (item);
            }

            return finishOrder (std::move (items), out);
        }

        constexpr int oldAmp = firstGenerationTypes * maxCopies;

        if ((int) values.size() == oldAmp + 1)
        {
            std::vector<int> items;
            juce::uint32 seen = 0;

            for (int old : values)
            {
                if (old < 0 || old > oldAmp || (seen & (1u << old)) != 0)
                    return false;

                seen |= 1u << old;
                items.push_back (old == oldAmp ? ampItem
                                               : makeItem (old % firstGenerationTypes, old / firstGenerationTypes));
            }

            return finishOrder (std::move (items), out);
        }

        return false;
    }

    /** Orders saved by version 1 packed 8 items (7 pedals + the amp as item 7) at 4 bits each into
        one integer. Converts such a value; items that did not exist back then are added by finishOrder. */
    inline bool orderFromLegacy (juce::uint32 packed, ChainOrder& out)
    {
        constexpr int legacyItems = firstGenerationTypes + 1;
        std::vector<int> items;
        juce::uint32 seen = 0;

        for (int i = 0; i < legacyItems; ++i)
        {
            const int old = (int) ((packed >> (4 * i)) & 0xfu);
            if (old >= legacyItems || (seen & (1u << old)) != 0)
                return false;

            seen |= 1u << old;
            items.push_back (old == firstGenerationTypes ? ampItem : old);
        }

        return finishOrder (std::move (items), out);
    }

    //==========================================================================
    // Per-item helpers. `item` must be a pedal (not the amp).

    inline const Pedal& pedalOf (int item) { return all()[(size_t) typeOf (item)]; }

    /** "DELAY", "DELAY 2", ... */
    inline juce::String displayName (int item)
    {
        const auto name = juce::String (pedalOf (item).name);
        return copyOf (item) == 0 ? name : name + " " + juce::String (copyOf (item) + 1);
    }

    /** Parameter id of copy `copy` of a base id: unchanged for copy 0, "<id>_2", "<id>_3" ... otherwise. */
    inline juce::String suffixedId (const juce::String& base, int copy)
    {
        return copy == 0 ? base : base + "_" + juce::String (copy + 1);
    }

    inline juce::String onId (int item)       { return suffixedId (pedalOf (item).onID, copyOf (item)); }
    inline juce::String knobId (int item, int knob)
    {
        return suffixedId (pedalOf (item).knobs[(size_t) knob].paramID, copyOf (item));
    }

    /** The "<NAME>_ADDED" id of copy 0 of a pedal type (also for pedals whose first copy cannot be removed). */
    inline juce::String addedBaseId (int type)
    {
        const auto& p = all()[(size_t) type];
        return p.addedID != nullptr ? juce::String (p.addedID)
                                    : juce::String (p.onID).upToLastOccurrenceOf ("_ON", false, false) + "_ADDED";
    }

    /** Copy 0 of a permanent pedal is always in the chain; every other copy can be added and removed. */
    inline bool isRemovable (int item) { return copyOf (item) > 0 || pedalOf (item).isRemovable(); }

    /** Id of the "in chain" switch, or an empty string for a permanent pedal. */
    inline juce::String addedId (int item)
    {
        return isRemovable (item) ? suffixedId (addedBaseId (typeOf (item)), copyOf (item)) : juce::String();
    }
}
