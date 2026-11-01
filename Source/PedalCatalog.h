#pragma once

#include <JuceHeader.h>

/** Describes every pedal the signal chain knows about. The UI builds its pedals from this list,
    so adding a new pedal type means adding an entry here (plus its parameters and DSP).

    The order of the list is the signal order: entries with postAmp == false run before the amp,
    the others run after the cab. */
namespace PedalCatalog
{
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

        bool isRemovable() const { return addedID != nullptr; }
    };

    inline const std::vector<Pedal>& all()
    {
        static const std::vector<Pedal> pedals =
        {
            { "NOISE GATE", "TIGHT GATE",     0xffb28cff, "GATE_ON",   "GATE_ADDED",   false,
              { { "THRESH",  "GATE_THRESH", 0, " dB" }, { "RELEASE", "GATE_REL", 0, " ms" } } },

            { "COMPRESSOR", "STUDIO FET",     0xff3dd3ee, "COMP_ON",   nullptr,        false,
              { { "SUSTAIN", "COMP_SUST", 0, "" },      { "ATTACK",  "COMP_ATT",  0, " ms" },
                { "BLEND",   "COMP_MIX",  0, " %" },    { "LEVEL",   "COMP_LVL",  1, " dB" } } },

            { "BOOST",      "CLEAN BOOST",    0xff6fdc8c, "BOOST_ON",  "BOOST_ADDED",  false,
              { { "GAIN",    "BOOST_GAIN", 1, " dB" } } },

            { "OVERDRIVE",  "TS-MOD CIRCUIT", 0xffffa43f, "OD_ON",     nullptr,        false,
              { { "GAIN",    "OD_GAIN",  1, "" },      { "TONE",    "OD_TONE",   1, "" },
                { "LEVEL",   "OD_LVL",   1, "" } } },

            { "CHORUS",     "ANALOG BBD",     0xff5aa8ff, "CHORUS_ON", "CHORUS_ADDED", true,
              { { "RATE",    "CHORUS_RATE", 1, " Hz" }, { "DEPTH",   "CHORUS_DEPTH", 0, " %" },
                { "MIX",     "CHORUS_MIX",  0, " %" } } },

            { "DELAY",      "TAPE ECHO",      0xffff7ab8, "DELAY_ON",  "DELAY_ADDED",  true,
              { { "TIME",    "DELAY_TIME", 0, " ms" },  { "FEEDBACK", "DELAY_FB",    0, " %" },
                { "MIX",     "DELAY_MIX",  0, " %" } } },

            { "REVERB",     "ROOM / HALL",    0xff4be3c1, "REVERB_ON", "REVERB_ADDED", true,
              { { "SIZE",    "REVERB_SIZE", 0, " %" },  { "DAMP",    "REVERB_DAMP",  0, " %" },
                { "MIX",     "REVERB_MIX",  0, " %" } } },
        };
        return pedals;
    }
}
