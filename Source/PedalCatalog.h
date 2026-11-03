#pragma once

#include <JuceHeader.h>

/** Describes every pedal the signal chain knows about. The UI builds its pedals from this list,
    so adding a new pedal type means adding an entry here (plus its parameters and DSP).

    postAmp only says where a pedal starts out: the user can drag pedals anywhere in the chain
    (before or after the amp), and that order is kept by the processor (see ChainOrder). */
namespace PedalCatalog
{
    /** Number of pedal types in all(). */
    constexpr int count = 7;

    /** How many copies of each pedal type can be in the chain at once. Every copy has its own
        parameters, created up front (copy 0 keeps the plain ids, copy n >= 1 gets a "_<n+1>"
        suffix), so host automation keeps working while the chain itself stays dynamic. */
    constexpr int maxCopies = 3;

    /** Chain items are numbered copy * count + type. The amp is the last item. */
    constexpr int ampItem = count * maxCopies;
    constexpr int numItems = ampItem + 1;

    /** A chain order lists every item once. Items whose pedal is not "added" are parked in it
        and skipped by the processor and the UI. */
    using ChainOrder = std::array<int, numItems>;

    inline bool isAmp (int item)          { return item == ampItem; }
    inline int  typeOf (int item)         { return item % count; }
    inline int  copyOf (int item)         { return item / count; }
    inline int  makeItem (int type, int copy) { return copy * count + type; }

    /** True if every item 0..numItems-1 appears exactly once. */
    inline bool isValidOrder (const ChainOrder& order)
    {
        juce::uint32 seen = 0;
        for (int item : order)
        {
            if (item < 0 || item >= numItems)
                return false;
            seen |= 1u << item;
        }
        return seen == (1u << numItems) - 1u;
    }

    /** "3,0,21,..." form used in saved state and preset files. */
    inline juce::String orderToString (const ChainOrder& order)
    {
        juce::StringArray parts;
        for (int item : order)
            parts.add (juce::String (item));
        return parts.joinIntoString (",");
    }

    /** Parses orderToString output. Returns false (and leaves `out` alone) if it is not a valid order. */
    inline bool orderFromString (const juce::String& text, ChainOrder& out)
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");
        parts.removeEmptyStrings();

        if (parts.size() != numItems)
            return false;

        ChainOrder parsed {};
        for (int i = 0; i < numItems; ++i)
            parsed[(size_t) i] = parts[i].getIntValue();

        if (! isValidOrder (parsed))
            return false;

        out = parsed;
        return true;
    }

    /** Orders saved by version 1 packed 8 items (7 pedals + the amp as item 7) at 4 bits each into
        one integer. Converts such a value; items that did not exist back then are parked at the end. */
    inline bool orderFromLegacy (juce::uint32 packed, ChainOrder& out)
    {
        constexpr int legacyItems = count + 1;
        ChainOrder converted {};
        juce::uint32 seen = 0;
        size_t n = 0;

        for (int i = 0; i < legacyItems; ++i)
        {
            const int old = (int) ((packed >> (4 * i)) & 0xfu);
            if (old >= legacyItems || (seen & (1u << old)) != 0)
                return false;

            seen |= 1u << old;
            converted[n++] = old == count ? ampItem : old;
        }

        for (int item = 0; item < numItems; ++item)
            if (std::find (converted.begin(), converted.begin() + (long) n, item) == converted.begin() + (long) n)
                converted[n++] = item;

        out = converted;
        return isValidOrder (out);
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
        };
        jassert ((int) pedals.size() == count);
        return pedals;
    }

    /** Where the chain starts out: catalog order, with the amp just before the first post-amp pedal. */
    inline ChainOrder defaultOrder()
    {
        ChainOrder order {};
        int n = 0;
        bool ampPlaced = false;

        for (int i = 0; i < count; ++i)
        {
            if (! ampPlaced && all()[(size_t) i].postAmp)
            {
                order[(size_t) n++] = ampItem;
                ampPlaced = true;
            }
            order[(size_t) n++] = i;
        }
        if (! ampPlaced)
            order[(size_t) n++] = ampItem;

        // extra copies start out parked at the end
        for (int copy = 1; copy < maxCopies; ++copy)
            for (int i = 0; i < count; ++i)
                order[(size_t) n++] = makeItem (i, copy);

        return order;
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
