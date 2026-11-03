#pragma once

#include <JuceHeader.h>
#include "PedalCatalog.h"

/** Presets: a named snapshot of every parameter, the pedal / amp order and (for user presets)
    the cab impulse response. Factory presets are built in; user presets are small XML files in
    the user's application-data folder.

    This class only knows about data and files. The processor applies a preset to itself
    (it owns the chain order and the IR), see NeuralFlowAmpAudioProcessor::loadPreset.
    Use it from the message thread only. */
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        bool factory = false;
        std::map<juce::String, float> values;   // parameter id -> plain value; missing = parameter default
        PedalCatalog::ChainOrder order = PedalCatalog::defaultOrder();
        juce::String irPath;                    // empty = leave the current IR alone
        juce::String namPath;                   // empty = leave the current NAM model alone
        std::array<juce::String, PedalCatalog::namCopies> capturePaths;   // NAM pedal models, per copy; empty = leave alone
        juce::File file;                        // user presets only
    };

    static constexpr int maxNameLength = 40;
    static inline const char* fileExtension = ".nfpreset";

    explicit PresetManager (juce::File userFolderToUse = defaultUserFolder())
        : userFolder (std::move (userFolderToUse))
    {
        factoryPresets = makeFactoryPresets();
        rescan();
    }

    static juce::File defaultUserFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("NeuralFlowAmp").getChildFile ("Presets");
    }

    /** Points the manager at another folder (used by tests) and reads it. */
    void setUserFolder (const juce::File& folder)
    {
        userFolder = folder;
        rescan();
    }

    const juce::File& getUserFolder() const { return userFolder; }

    //==========================================================================
    int getNumFactory() const { return (int) factoryPresets.size(); }
    int size() const          { return (int) factoryPresets.size() + (int) userPresets.size(); }

    const Preset& get (int index) const
    {
        jassert (juce::isPositiveAndBelow (index, size()));
        return index < getNumFactory() ? factoryPresets[(size_t) index]
                                       : userPresets[(size_t) (index - getNumFactory())];
    }

    /** Index of the preset with this name (case-insensitive), or -1. */
    int indexOf (const juce::String& name) const
    {
        for (int i = 0; i < size(); ++i)
            if (get (i).name.equalsIgnoreCase (name))
                return i;
        return -1;
    }

    /** Re-reads the user folder. User presets are listed alphabetically after the factory ones. */
    void rescan()
    {
        userPresets.clear();

        for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
            if (auto preset = readFile (file))
                if (indexOf (preset->name) < 0)   // a file must not shadow a factory preset
                    userPresets.push_back (std::move (*preset));

        std::sort (userPresets.begin(), userPresets.end(), [] (const Preset& a, const Preset& b)
        {
            return a.name.compareNatural (b.name, false) < 0;
        });
    }

    //==========================================================================
    /** Why a name cannot be used for a user preset (ok = usable). */
    juce::Result validateName (const juce::String& rawName) const
    {
        const auto name = rawName.trim();

        if (name.isEmpty())
            return juce::Result::fail ("Type a name");
        if (name.length() > maxNameLength)
            return juce::Result::fail ("Name is too long");
        if (name.containsAnyOf ("\\/:*?\"<>|"))
            return juce::Result::fail ("Name can't contain  \\ / : * ? \" < > |");

        for (const auto& f : factoryPresets)
            if (f.name.equalsIgnoreCase (name))
                return juce::Result::fail ("That name is used by a factory preset");

        return juce::Result::ok();
    }

    /** True if saving under this name would replace an existing user preset. */
    bool wouldOverwrite (const juce::String& name) const
    {
        const int i = indexOf (name.trim());
        return i >= getNumFactory();
    }

    /** Writes `preset` (name taken from the preset) to the user folder and rescans.
        Returns the new index of the saved preset, or -1 on failure. */
    int saveUserPreset (Preset preset)
    {
        preset.name = preset.name.trim();
        if (validateName (preset.name).failed())
            return -1;

        if (! userFolder.createDirectory())
            return -1;

        const auto file = userFolder.getChildFile (juce::File::createLegalFileName (preset.name) + fileExtension);
        if (! writeFile (preset, file))
            return -1;

        // The same name might previously have lived in a differently spelled file.
        const int old = indexOf (preset.name);
        if (old >= getNumFactory() && userPresets[(size_t) (old - getNumFactory())].file != file)
            userPresets[(size_t) (old - getNumFactory())].file.deleteFile();

        rescan();
        return indexOf (preset.name);
    }

    /** Like validateName, for renaming the user preset at `index` (keeping its own name is fine). */
    juce::Result validateRename (int index, const juce::String& rawName) const
    {
        if (index < getNumFactory() || index >= size())
            return juce::Result::fail ("Only your own presets can be renamed");

        auto result = validateName (rawName);
        if (result.failed())
            return result;

        const int clash = indexOf (rawName.trim());
        if (clash >= 0 && clash != index)
            return juce::Result::fail ("A preset with that name already exists");

        return juce::Result::ok();
    }

    /** Gives the user preset at `index` a new name (and file). */
    juce::Result renameUserPreset (int index, const juce::String& rawName)
    {
        auto valid = validateRename (index, rawName);
        if (valid.failed())
            return valid;

        auto preset = get (index);
        const auto oldFile = preset.file;
        preset.name = rawName.trim();

        const auto newFile = userFolder.getChildFile (juce::File::createLegalFileName (preset.name) + fileExtension);
        if (! writeFile (preset, newFile))
            return juce::Result::fail ("Could not write the preset file");

        if (oldFile != newFile)
            oldFile.deleteFile();

        rescan();
        return juce::Result::ok();
    }

    /** Deletes the user preset at `index` from disk. */
    bool deleteUserPreset (int index)
    {
        if (index < getNumFactory() || index >= size())
            return false;

        const bool ok = get (index).file.deleteFile();
        rescan();
        return ok;
    }

    //==========================================================================
    /** The footer's input / output levels belong to the rig (guitar, interface), not to a tone: a
        preset neither stores them nor changes them. */
    static bool isRigSetting (const juce::String& paramID)
    {
        return paramID == "IN_GAIN" || paramID == "OUT_GAIN";
    }

    /** Reads every tone parameter of `state` as plain values (the rig settings are left out). */
    static std::map<juce::String, float> captureValues (juce::AudioProcessorValueTreeState& state)
    {
        std::map<juce::String, float> values;
        for (auto* p : state.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isRigSetting (r->paramID))
                    values[r->paramID] = r->convertFrom0to1 (r->getValue());
        return values;
    }

    /** Sets every parameter: the preset's value, or the parameter's default if it has none. */
    static void applyValues (const Preset& preset, juce::AudioProcessorValueTreeState& state)
    {
        for (auto* p : state.processor.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (r == nullptr || isRigSetting (r->paramID))
                continue;

            const auto it = preset.values.find (r->paramID);
            const float normalised = it != preset.values.end() ? r->convertTo0to1 (it->second)
                                                               : r->getDefaultValue();
            r->setValueNotifyingHost (normalised);
        }
    }

    /** True if the current parameters and order still match the preset (the IR is not compared). */
    static bool matches (const Preset& preset, const juce::AudioProcessorValueTreeState& state,
                         const PedalCatalog::ChainOrder& currentOrder)
    {
        for (auto* p : state.processor.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (r == nullptr || isRigSetting (r->paramID))
                continue;

            const auto it = preset.values.find (r->paramID);
            const float expected = it != preset.values.end() ? r->convertTo0to1 (it->second)
                                                             : r->getDefaultValue();
            if (std::abs (r->getValue() - expected) > 0.002f)
                return false;
        }

        return currentOrder == preset.order;
    }

private:
    //==========================================================================
    static bool writeFile (const Preset& preset, const juce::File& file)
    {
        juce::XmlElement xml ("NeuralFlowPreset");
        xml.setAttribute ("version", 2);
        xml.setAttribute ("name", preset.name);
        xml.setAttribute ("chain", PedalCatalog::orderToString (preset.order));
        xml.setAttribute ("irPath", preset.irPath);
        xml.setAttribute ("namPath", preset.namPath);

        for (size_t copy = 0; copy < preset.capturePaths.size(); ++copy)
            if (preset.capturePaths[copy].isNotEmpty())
                xml.setAttribute ("capturePath" + juce::String ((int) copy), preset.capturePaths[copy]);

        for (const auto& [id, value] : preset.values)
        {
            auto* p = xml.createNewChildElement ("P");
            p->setAttribute ("id", id);
            p->setAttribute ("v", (double) value);
        }

        return xml.writeTo (file);
    }

    static std::optional<Preset> readFile (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("NeuralFlowPreset"))
            return std::nullopt;

        Preset preset;
        preset.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension()).trim();
        preset.file = file;
        preset.irPath = xml->getStringAttribute ("irPath");
        preset.namPath = xml->getStringAttribute ("namPath");

        for (size_t copy = 0; copy < preset.capturePaths.size(); ++copy)
            preset.capturePaths[copy] = xml->getStringAttribute ("capturePath" + juce::String ((int) copy));

        if (preset.name.isEmpty())
            return std::nullopt;

        preset.order = PedalCatalog::defaultOrder();
        if (xml->hasAttribute ("chain"))
            PedalCatalog::orderFromString (xml->getStringAttribute ("chain"), preset.order);
        else   // version 1 files packed 8 items into one integer
            PedalCatalog::orderFromLegacy ((juce::uint32) xml->getIntAttribute ("chainOrder"), preset.order);

        for (auto* p : xml->getChildWithTagNameIterator ("P"))
            preset.values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("v");

        return preset;
    }

    //==========================================================================
    /** Builds a factory preset. `chain` lists what is in the signal path, in order; use
        PedalCatalog::ampItem for the amp. Add-on pedals that are listed are switched into the
        chain, the ones that are not listed are left out (and parked at the end of the order). */
    static Preset factory (const char* name, std::initializer_list<int> chain,
                           std::initializer_list<std::pair<const char*, float>> values)
    {
        Preset preset;
        preset.name = name;
        preset.factory = true;

        std::vector<int> items;

        for (int item : chain)
        {
            items.push_back (item);

            if (item != PedalCatalog::ampItem && PedalCatalog::isRemovable (item))
                preset.values[PedalCatalog::addedId (item)] = 1.0f;
        }

        // what is not listed is parked: pedal types that are new since the first version go right
        // before the amp, everything else at the end
        PedalCatalog::ChainOrder order {};
        [[maybe_unused]] const bool ok = PedalCatalog::finishOrder (std::move (items), order);
        jassert (ok);
        preset.order = order;

        for (const auto& [id, v] : values)
            preset.values[id] = v;

        return preset;
    }

    static std::vector<Preset> makeFactoryPresets()
    {
        enum { gate, comp, boost, od, chorus, delay, reverb, amp = PedalCatalog::ampItem };

        std::vector<Preset> list;

        // Everything at its starting position.
        {
            Preset init;
            init.name = "Default";
            init.factory = true;
            list.push_back (std::move (init));   // no overrides, default chain order
        }

        list.push_back (factory ("Clean Sparkle", { comp, amp, chorus, reverb },
        {
            { "OD_ON", 0.0f },
            { "COMP_SUST", 35.0f }, { "COMP_ATT", 25.0f }, { "COMP_MIX", 70.0f }, { "COMP_LVL", 1.0f },
            { "DRIVE", 15.0f }, { "LOW", 5.0f }, { "MID", 4.5f }, { "HIGH", 6.5f }, { "MASTER", 5.0f },
            { "CHORUS_RATE", 0.9f }, { "CHORUS_DEPTH", 35.0f }, { "CHORUS_MIX", 40.0f },
            { "REVERB_SIZE", 40.0f }, { "REVERB_DAMP", 55.0f }, { "REVERB_MIX", 20.0f },
        }));

        list.push_back (factory ("Funk Clean", { comp, amp },
        {
            { "OD_ON", 0.0f },
            { "COMP_SUST", 65.0f }, { "COMP_ATT", 6.0f }, { "COMP_MIX", 100.0f }, { "COMP_LVL", 2.0f },
            { "DRIVE", 22.0f }, { "LOW", 4.0f }, { "MID", 6.0f }, { "HIGH", 7.0f }, { "MASTER", 5.0f },
            { "CAB_LOWCUT", 90.0f },
        }));

        list.push_back (factory ("Warm Crunch", { gate, od, amp, delay },
        {
            { "COMP_ON", 0.0f },
            { "GATE_THRESH", -58.0f }, { "GATE_REL", 150.0f },
            { "OD_GAIN", 3.5f }, { "OD_TONE", 5.5f }, { "OD_LVL", 5.0f },
            { "DRIVE", 42.0f }, { "LOW", 6.0f }, { "MID", 5.5f }, { "HIGH", 5.0f }, { "MASTER", 5.0f },
            { "DELAY_TIME", 320.0f }, { "DELAY_FB", 22.0f }, { "DELAY_MIX", 18.0f },
        }));

        list.push_back (factory ("Classic Lead", { gate, comp, boost, od, amp, delay, reverb },
        {
            { "GATE_THRESH", -55.0f },
            { "COMP_SUST", 55.0f }, { "COMP_ATT", 12.0f }, { "COMP_MIX", 80.0f },
            { "BOOST_GAIN", 7.0f },
            { "OD_GAIN", 5.0f }, { "OD_TONE", 6.0f }, { "OD_LVL", 5.0f },
            { "DRIVE", 68.0f }, { "LOW", 5.5f }, { "MID", 6.5f }, { "HIGH", 5.5f }, { "MASTER", 5.0f },
            { "DELAY_TIME", 420.0f }, { "DELAY_FB", 35.0f }, { "DELAY_MIX", 28.0f },
            { "REVERB_SIZE", 45.0f }, { "REVERB_DAMP", 50.0f }, { "REVERB_MIX", 18.0f },
        }));

        list.push_back (factory ("High Gain Rhythm", { gate, boost, od, amp },
        {
            { "COMP_ON", 0.0f },
            { "GATE_THRESH", -48.0f }, { "GATE_REL", 80.0f },
            { "BOOST_GAIN", 10.0f },
            { "OD_GAIN", 7.0f }, { "OD_TONE", 5.0f }, { "OD_LVL", 4.0f },
            { "DRIVE", 85.0f }, { "LOW", 6.5f }, { "MID", 3.5f }, { "HIGH", 6.0f }, { "MASTER", 4.5f },
            { "CAB_LOWCUT", 90.0f }, { "CAB_HIGHCUT", 7500.0f },
        }));

        list.push_back (factory ("Ambient Wash", { comp, amp, chorus, delay, reverb },
        {
            { "OD_ON", 0.0f },
            { "COMP_SUST", 60.0f }, { "COMP_ATT", 30.0f }, { "COMP_MIX", 100.0f },
            { "DRIVE", 20.0f }, { "LOW", 5.0f }, { "MID", 4.0f }, { "HIGH", 6.0f }, { "MASTER", 5.0f },
            { "CHORUS_RATE", 0.6f }, { "CHORUS_DEPTH", 55.0f }, { "CHORUS_MIX", 45.0f },
            { "DELAY_TIME", 520.0f }, { "DELAY_FB", 55.0f }, { "DELAY_MIX", 38.0f },
            { "REVERB_SIZE", 85.0f }, { "REVERB_DAMP", 40.0f }, { "REVERB_MIX", 45.0f },
        }));

        return list;
    }

    juce::File userFolder;
    std::vector<Preset> factoryPresets, userPresets;
};
