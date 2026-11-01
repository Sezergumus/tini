#pragma once

#include <JuceHeader.h>

/** Describes every pedal the signal chain knows about. The UI builds its pedals from this list,
    so adding a new pedal type means adding an entry here (plus its parameters and DSP).

    postAmp only says where a pedal starts out: the user can drag pedals anywhere in the chain
    (before or after the amp), and that order is kept by the processor (see ChainOrder). */
namespace PedalCatalog
{
    /** Number of pedals in all(). The amp itself is item number `count` in a chain order. */
    constexpr int count = 7;
    constexpr int ampItem = count;
    constexpr int numItems = count + 1;

    using ChainOrder = std::array<int, numItems>;

    /** Packs / unpacks a chain order (4 bits per item) so it can be handed to the audio thread atomically. */
    inline juce::uint32 packOrder (const ChainOrder& order)
    {
        juce::uint32 packed = 0;
        for (int i = 0; i < numItems; ++i)
            packed |= (juce::uint32) order[(size_t) i] << (4 * i);
        return packed;
    }

    inline ChainOrder unpackOrder (juce::uint32 packed)
    {
        ChainOrder order {};
        for (int i = 0; i < numItems; ++i)
            order[(size_t) i] = (int) ((packed >> (4 * i)) & 0xfu);
        return order;
    }

    /** True if every item 0..numItems-1 appears exactly once. */
    inline bool isValidOrder (const ChainOrder& order)
    {
        unsigned seen = 0;
        for (int item : order)
        {
            if (item < 0 || item >= numItems)
                return false;
            seen |= 1u << item;
        }
        return seen == (1u << numItems) - 1u;
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
        return order;
    }
}
