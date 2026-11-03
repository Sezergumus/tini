// Renders the editor to PNG files without opening a window, so the UI can be checked
// (and reviewed in pull requests) on a machine without touching the desktop.
//
//   NeuralFlowAmpTests --snapshots <output-dir>
#include "../Source/PluginProcessor.h"
#include "../Source/UI/MainView.h"

namespace
{
    void setParam (NeuralFlowAmpAudioProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    void feedSine (NeuralFlowAmpAudioProcessor& p)
    {
        juce::MidiBuffer midi;
        for (int block = 0; block < 4; ++block)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.25f * std::sin (juce::MathConstants<float>::twoPi * 196.0f * (float) (block * 512 + i) / 48000.0f);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            p.processBlock (buf, midi);
        }
    }

    void save (juce::AudioProcessorEditor& editor, const juce::File& file)
    {
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    juce::Component* findIf (juce::Component& parent, const std::function<bool (juce::Component&)>& test)
    {
        for (auto* child : parent.getChildren())
        {
            if (test (*child))
                return child;
            if (auto* found = findIf (*child, test))
                return found;
        }
        return nullptr;
    }

    template <typename T>
    T* findFirst (juce::Component& parent)
    {
        return dynamic_cast<T*> (findIf (parent, [] (juce::Component& c) { return dynamic_cast<T*> (&c) != nullptr; }));
    }

    void report (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    }

    juce::String orderToString (const PedalCatalog::ChainOrder& order)
    {
        juce::StringArray parts;
        for (int id : order)
            parts.add (id == PedalCatalog::ampItem ? juce::String ("AMP") : PedalCatalog::displayName (id));
        return parts.joinIntoString (" > ");
    }
}

void renderUiSnapshots (const juce::File& dir)
{
    dir.createDirectory();

    NeuralFlowAmpAudioProcessor p;
    const auto presetFolder = dir.getChildFile ("presets_tmp");   // never touch the user's real presets
    presetFolder.deleteRecursively();
    p.presets.setUserFolder (presetFolder);
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());

    // The editor refreshes itself from a timer, so give the message loop time to run.
    auto settle = [&]
    {
        feedSine (p);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
    };

    settle();
    save (*editor, dir.getChildFile ("ui_default.png"));

    for (auto id : { "GATE", "BOOST", "CHORUS", "DELAY", "REVERB" })
        setParam (p, juce::String (id) + "_ADDED", 1.0f);
    setParam (p, "DELAY_ON", 0.0f);
    settle();
    save (*editor, dir.getChildFile ("ui_all_pedals.png"));

    for (auto id : { "GATE", "BOOST", "CHORUS" })
        setParam (p, juce::String (id) + "_ADDED", 0.0f);
    settle();
    save (*editor, dir.getChildFile ("ui_post_amp_only.png"));

    // Interaction: clicking a pedal's "x" must take it out of the chain.
    for (auto id : { "GATE", "BOOST" })
        setParam (p, juce::String (id) + "_ADDED", 1.0f);
    settle();

    std::function<juce::Button* (juce::Component&)> findRemoveButton = [&] (juce::Component& parent) -> juce::Button*
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == "remove")
                return b;
            if (auto* found = findRemoveButton (*child))
                return found;
        }
        return nullptr;
    };

    auto* removeButton = findRemoveButton (*editor);
    const bool found = removeButton != nullptr;
    std::printf ("[%s] a remove button exists on the added pedals\n", found ? " OK " : "FAIL");

    if (found)
    {
        const bool gateBefore = p.apvts.getRawParameterValue ("GATE_ADDED")->load() > 0.5f;
        removeButton->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

        const bool gateAfter = p.apvts.getRawParameterValue ("GATE_ADDED")->load() > 0.5f;
        std::printf ("[%s] clicking the pedal's x removes it from the chain (gate: %d -> %d)\n",
                     (gateBefore && ! gateAfter) ? " OK " : "FAIL", (int) gateBefore, (int) gateAfter);
        save (*editor, dir.getChildFile ("ui_after_remove.png"));
    }


    //==========================================================================
    // Add-pedal popover: open it, look at it, pick a row.
    auto settleLoop = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    for (auto id : { "GATE", "BOOST", "CHORUS", "DELAY", "REVERB" })
        setParam (p, juce::String (id) + "_ADDED", 0.0f);
    settle();

    auto* addButton = dynamic_cast<juce::TextButton*> (findIf (*editor, [] (juce::Component& c)
    {
        auto* b = dynamic_cast<juce::TextButton*> (&c);
        return b != nullptr && b->getButtonText().contains ("ADD PEDAL");
    }));
    report (addButton != nullptr, "the chain panel has an ADD PEDAL button");

    if (addButton != nullptr)
    {
        addButton->triggerClick();
        settleLoop (300);
        save (*editor, dir.getChildFile ("ui_add_menu.png"));
        report (findFirst<AddPedalMenu> (*editor) != nullptr, "ADD PEDAL opens the popover");

        if (auto* box = findFirst<juce::CallOutBox> (*editor))
            box->dismiss();
        settleLoop (300);
        report (findFirst<AddPedalMenu> (*editor) == nullptr, "the popover can be dismissed");

        // open it again with two pedals already in the chain
        setParam (p, "GATE_ADDED", 1.0f);
        setParam (p, "BOOST_ADDED", 1.0f);
        settle();

        addButton->triggerClick();
        settleLoop (300);
        save (*editor, dir.getChildFile ("ui_add_menu_partial.png"));

        auto* menu = findFirst<AddPedalMenu> (*editor);
        auto* gateRow = menu != nullptr ? menu->findRow ("GATE_ADDED") : nullptr;
        auto* delayRow = menu != nullptr ? menu->findRow ("DELAY_ADDED") : nullptr;
        report (gateRow != nullptr && gateRow->isEnabled() && gateRow->getCopiesUsed() == 1,
                "a pedal that is already in the chain can still be picked (it counts one copy in use)");
        report (delayRow != nullptr && delayRow->isEnabled() && delayRow->getCopiesUsed() == 0, "a pedal that is available can be picked");

        if (delayRow != nullptr)
        {
            delayRow->triggerClick();
            settleLoop (400);
            report (p.apvts.getRawParameterValue ("DELAY_ADDED")->load() > 0.5f, "picking DELAY in the popover adds it to the chain");
            report (findFirst<AddPedalMenu> (*editor) == nullptr, "the popover closes after picking");
        }

        // a second gate: picking GATE again adds copy 2 with its own parameters
        addButton->triggerClick();
        settleLoop (300);
        if (auto* again = findFirst<AddPedalMenu> (*editor))
            if (auto* row = again->findRow ("GATE_ADDED"))
                row->triggerClick();
        settleLoop (400);

        report (p.apvts.getRawParameterValue ("GATE_ADDED_2")->load() > 0.5f, "picking GATE twice adds a second gate");

        int gateViews = 0;
        if (auto* panel = findFirst<SignalChainPanel> (*editor))
            for (auto* v : panel->getStrip().pedals)
                gateViews += PedalCatalog::typeOf (v->getItem()) == 0 ? 1 : 0;
        report (gateViews == 2, "both gates are shown in the chain");
        save (*editor, dir.getChildFile ("ui_two_gates.png"));

        setParam (p, "GATE_ADDED_2", 0.0f);
        settle();
    }

    //==========================================================================
    // Drag to reorder.
    for (auto id : { "GATE", "BOOST", "CHORUS", "DELAY", "REVERB" })
        setParam (p, juce::String (id) + "_ADDED", 1.0f);
    p.setChainOrder (PedalCatalog::defaultOrder());
    settle();
    settleLoop (300);

    if (auto* panel = findFirst<SignalChainPanel> (*editor))
    {
        auto& strip = panel->getStrip();

        auto pedalOf = [&] (int id) -> PedalView*
        {
            for (auto* v : strip.pedals)
                if (v->getItem() == id)
                    return v;
            return nullptr;
        };
        auto* ampNode = findFirst<AmpNode> (*editor);

        constexpr int gate = 0, comp = 1, boost = 2, od = 3, chorus = 4, delay = 5, reverb = 6, amp = PedalCatalog::ampItem;
        auto order = [&] { return p.getChainOrder(); };
        auto sameAs = [&] (std::initializer_list<int> expected)
        {
            return std::equal (expected.begin(), expected.end(), order().begin());
        };
        auto drag = [&] (juce::Component& c, int toX, int step = 20)
        {
            const auto start = c.getBounds().getCentre();
            strip.startDrag (c, start);
            for (int x = start.x; x != toX; x += (toX > x ? juce::jmin (step, toX - x) : -juce::jmin (step, x - toX)))
                strip.dragTo ({ x, start.y });
            strip.dragTo ({ toX, start.y });
        };

        report (ampNode != nullptr, "the amp is an item of the chain");
        report (sameAs ({ gate, comp, boost, od, amp, chorus, delay, reverb }), "the chain starts in the default order");

        // a click without moving must not reorder anything
        if (auto* b = pedalOf (boost))
        {
            strip.startDrag (*b, b->getBounds().getCentre());
            strip.dragTo (b->getBounds().getCentre() + juce::Point<int> (2, 0));
            strip.endDrag();
            report (sameAs ({ gate, comp, boost, od, amp, chorus, delay, reverb }), "a click on the grip does not reorder");
        }

        // carry the boost pedal over the overdrive and the amp, and set it down after the amp
        if (auto* b = pedalOf (boost))
        {
            drag (*b, pedalOf (od)->getBounds().getCentreX() + 10);
            settleLoop (250);
            save (*editor, dir.getChildFile ("ui_dragging.png"));

            const auto here = b->getBounds().getCentre();
            for (int x = here.x; x <= pedalOf (chorus)->getBounds().getCentreX() + 10; x += 20)
                strip.dragTo ({ x, here.y });
            strip.endDrag();
            settleLoop (300);

            report (sameAs ({ gate, comp, od, amp, boost, chorus, delay, reverb }),
                    "dragging the boost across the amp moves it after the amp: " + orderToString (order()));
            report (pedalOf (boost)->getX() > ampNode->getX() && pedalOf (boost)->getX() < pedalOf (chorus)->getX(),
                    "the pedals settle into their new places on screen");
            save (*editor, dir.getChildFile ("ui_after_drag.png"));
        }

        // the amp moves like a pedal
        if (ampNode != nullptr)
        {
            drag (*ampNode, pedalOf (gate)->getBounds().getCentreX() - 40, 30);
            strip.endDrag();
            settleLoop (300);
            report (sameAs ({ amp, gate, comp, od, boost, chorus, delay, reverb }),
                    "the amp can be dragged to the front: " + orderToString (order()));
        }

        // pedals that are not in the chain keep their place in the order
        setParam (p, "COMP_ON", 1.0f);
        setParam (p, "GATE_ADDED", 0.0f);
        settle();
        settleLoop (200);
        const auto beforeHidden = order();

        if (auto* c = pedalOf (comp))
        {
            drag (*c, pedalOf (od)->getBounds().getCentreX() + 10);
            strip.endDrag();
            settleLoop (250);

            const auto afterHidden = order();
            report (PedalCatalog::isValidOrder (afterHidden), "reordering with a pedal removed keeps a valid order: " + orderToString (afterHidden));
            report (afterHidden[1] == gate && beforeHidden[1] == gate, "the removed gate keeps its slot");
            report (std::find (afterHidden.begin(), afterHidden.end(), comp) > std::find (afterHidden.begin(), afterHidden.end(), od),
                    "the compressor now sits after the overdrive");
        }
    }

    //==========================================================================
    // NAM amp slot: type switch and model card.
    if (auto* lead = findFirst<LeadChannelPanel> (*editor))
    {
        const auto model = juce::File (__FILE__).getParentDirectory().getChildFile ("models").getChildFile ("test_wavenet.nam");
        auto ampType = [&] { return p.apvts.getRawParameterValue ("AMP_TYPE")->load(); };

        p.unloadNamModel();
        setParam (p, "AMP_TYPE", 0.0f);
        settle();

        report (lead->builtInButton.getToggleState() && ! lead->namButton.getToggleState(), "the built-in amp is selected at first");
        save (*editor, dir.getChildFile ("ui_amp_builtin.png"));

        lead->namButton.triggerClick();
        settleLoop (100);
        report (ampType() > 0.5f && lead->namButton.getToggleState() && ! lead->builtInButton.getToggleState(),
                "the NAM button switches the amp to NAM");
        save (*editor, dir.getChildFile ("ui_amp_nam_empty.png"));

        p.loadNamModel (model);
        for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
            settleLoop (10);
        settle();
        report (p.nam.getStatus().state == NamEngine::Status::State::ready && p.nam.hasModel(), "a model loads while the UI is running");
        save (*editor, dir.getChildFile ("ui_amp_nam_loaded.png"));

        report (! lead->liteButton.isVisible() && ! lead->fullButton.isVisible(), "a model without sizes shows no FULL / LITE switch");

        // an A2 model has two sizes
        {
            const auto a2 = juce::File (__FILE__).getParentDirectory().getChildFile ("models").getChildFile ("test_a2.nam");
            p.loadNamModel (a2);
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
            report (lead->fullButton.isVisible() && lead->liteButton.isVisible() && lead->fullButton.getToggleState(),
                    "an A2 model shows the FULL / LITE switch, Full selected");
            save (*editor, dir.getChildFile ("ui_amp_nam_a2.png"));

            lead->liteButton.triggerClick();
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
            report (p.isNamLite() && p.nam.getModelInfo().lite && lead->liteButton.getToggleState() && ! lead->fullButton.getToggleState(),
                    "LITE reloads the model at the smaller size");
            save (*editor, dir.getChildFile ("ui_amp_nam_a2_lite.png"));

            lead->fullButton.triggerClick();
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
            report (! p.isNamLite() && ! p.nam.getModelInfo().lite, "FULL goes back to the full size");

            p.loadNamModel (model);   // back to the plain model for the checks below
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
        }

        const float levelBefore = p.apvts.getRawParameterValue ("NAM_NORM")->load();
        lead->levelButton.triggerClick();
        settleLoop (50);
        report ((p.apvts.getRawParameterValue ("NAM_NORM")->load() > 0.5f) != (levelBefore > 0.5f),
                "LEVEL MATCH toggles the normalisation (" + juce::String (levelBefore) + " -> "
                    + juce::String (p.apvts.getRawParameterValue ("NAM_NORM")->load())
                    + ", button on: " + juce::String ((int) lead->levelButton.getToggleState())
                    + ", clicks toggle: " + juce::String ((int) lead->levelButton.getClickingTogglesState()) + ")");
        lead->levelButton.triggerClick();
        settleLoop (50);

        lead->clearButton.triggerClick();
        settle();
        report (p.nam.getStatus().state == NamEngine::Status::State::empty && ! p.nam.hasModel(), "CLEAR removes the model");

        auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ui_broken.nam");
        bad.replaceWithText ("not a model");
        p.loadNamModel (bad);
        for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
            settleLoop (10);
        settle();
        report (p.nam.getStatus().state == NamEngine::Status::State::failed, "a broken file shows as failed");
        save (*editor, dir.getChildFile ("ui_amp_nam_error.png"));
        bad.deleteFile();

        p.unloadNamModel();
        lead->builtInButton.triggerClick();
        settle();
        report (ampType() < 0.5f && lead->builtInButton.getToggleState(), "the BUILT-IN button switches back");
    }
    else
        report (false, "the lead channel panel exists");

    //==========================================================================
    // Presets: header bar, stepping, edited marker, save dialog.
    p.loadPreset (p.presets.indexOf ("Default"));
    settle();
    settleLoop (300);

    if (auto* bar = findFirst<PresetBar> (*editor))
    {
        auto& nameBtn = bar->getNameButton();
        auto shownName = [&] { return nameBtn.getDisplayedName(); };
        auto pedalCount = [&]
        {
            int n = 0;
            if (auto* panel = findFirst<SignalChainPanel> (*editor))
                n = panel->getStrip().pedals.size();
            return n;
        };

        report (shownName() == "Default" && nameBtn.getTag() == "FACTORY", "the preset bar shows the current preset: " + shownName());

        p.loadPreset (p.presets.indexOf ("Classic Lead"));
        settle();
        settleLoop (300);
        report (shownName() == "Classic Lead", "the bar follows a preset loaded elsewhere");
        report (pedalCount() == 6, "loading a preset rebuilds the chain on screen (" + juce::String (pedalCount()) + " pedals)");
        save (*editor, dir.getChildFile ("ui_preset_classic_lead.png"));

        bar->getNextButton().triggerClick();
        settleLoop (300);
        report (shownName() == "High Gain Rhythm", "the next arrow loads the next preset: " + shownName());
        report (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 85.0f) < 0.01f, "...and applies its settings");
        report (pedalCount() == 4, "...and rebuilds the chain (" + juce::String (pedalCount()) + " pedals)");
        save (*editor, dir.getChildFile ("ui_preset_high_gain.png"));

        bar->getPrevButton().triggerClick();
        settleLoop (200);
        report (shownName() == "Classic Lead", "the previous arrow goes back: " + shownName());

        bar->select (0);
        bar->getPrevButton().triggerClick();
        settleLoop (200);
        report (shownName() == p.presets.get (p.presets.size() - 1).name, "previous wraps around to the last preset: " + shownName());
        bar->getNextButton().triggerClick();
        settleLoop (200);
        report (shownName() == "Default", "next wraps around to the first preset");

        // tweak something: the preset shows as edited
        bar->select (p.presets.indexOf ("Warm Crunch"));
        settleLoop (200);
        report (! nameBtn.isEdited(), "a freshly loaded preset is not marked as edited");
        setParam (p, "DRIVE", 70.0f);
        settle();
        settleLoop (200);
        report (nameBtn.isEdited(), "turning a knob marks the preset as edited");
        save (*editor, dir.getChildFile ("ui_preset_edited.png"));

        // save dialog
        bar->getSaveButton().triggerClick();
        settleLoop (300);
        auto* dialog = findFirst<SavePresetPanel> (*editor);
        report (dialog != nullptr, "SAVE opens the save dialog");

        if (dialog != nullptr)
        {
            report (dialog->isSaveEnabled(), "the suggested name 'My Warm Crunch' can be saved");
            save (*editor, dir.getChildFile ("ui_save_dialog.png"));

            dialog->setName ("Warm Crunch");
            report (! dialog->isSaveEnabled(), "a factory preset's name cannot be used: " + dialog->getHint());
            dialog->setName ("");
            report (! dialog->isSaveEnabled(), "an empty name cannot be saved");
            dialog->setName ("a/b");
            report (! dialog->isSaveEnabled(), "a name with path characters cannot be saved: " + dialog->getHint());
            save (*editor, dir.getChildFile ("ui_save_dialog_invalid.png"));

            dialog->setName ("UI Test Tone");
            report (dialog->isSaveEnabled(), "a new name can be saved");
            report (dialog->save(), "saving from the dialog succeeds");
            settleLoop (300);

            report (findFirst<SavePresetPanel> (*editor) == nullptr, "the dialog closes after saving");
            report (p.presets.indexOf ("UI Test Tone") >= p.presets.getNumFactory(), "the preset was added to the list");
            report (shownName() == "UI Test Tone" && ! nameBtn.isEdited(), "the bar shows the saved preset: " + shownName());
            report (nameBtn.getTag() == "SAVED", "the bar confirms with SAVED");
            save (*editor, dir.getChildFile ("ui_after_save.png"));
        }

        // the saved preset is reachable with the arrows and brings the tweak back
        bar->select (0);
        settleLoop (200);
        bar->getPrevButton().triggerClick();
        settleLoop (300);
        report (shownName() == "UI Test Tone", "the saved preset is the last one in the list: " + shownName());
        report (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 70.0f) < 0.01f, "...and restores the saved settings");
        report (nameBtn.getTag() == "USER" || nameBtn.getTag() == "SAVED", "...and is tagged as a user preset");

        // opening the dialog on a user preset suggests its own name, and saving replaces it
        bar->getSaveButton().triggerClick();
        settleLoop (300);
        if (auto* again = findFirst<SavePresetPanel> (*editor))
        {
            report (again->isSaveEnabled() && again->getHint().containsIgnoreCase ("replaces"),
                    "re-saving a user preset offers to replace it: " + again->getHint());
            save (*editor, dir.getChildFile ("ui_save_dialog_replace.png"));
            again->save();
            settleLoop (300);
        }
        report (p.presets.size() == p.presets.getNumFactory() + 1, "replacing does not create a duplicate");

        // a second user preset, so the list has more than one
        p.saveCurrentAsPreset ("Another Tone");
        p.loadPreset (p.presets.indexOf ("UI Test Tone"));
        settleLoop (200);

        //======================================================================
        // The preset list
        nameBtn.triggerClick();
        settleLoop (300);
        auto* list = findFirst<PresetListPanel> (*editor);
        report (list != nullptr, "clicking the preset name opens the list");

        if (list != nullptr)
        {
            report (list->getNumRows() == p.presets.size(), "the list has one line per preset (" + juce::String (list->getNumRows()) + ")");
            save (*editor, dir.getChildFile ("ui_preset_list.png"));

            if (auto* factoryRow = list->findRow ("Default"))
            {
                factoryRow->beginRename();
                factoryRow->beginDelete();
                report (factoryRow->getMode() == PresetRow::Mode::normal, "factory presets offer neither rename nor delete");
            }

            // rename: a taken name is refused with a hint, a free one is applied
            auto* row = list->findRow ("UI Test Tone");
            report (row != nullptr && ! row->isFactory(), "your own preset has a line in the list");

            if (row != nullptr)
            {
                row->beginRename();
                row->setRenameText ("Another Tone");
                report (list->getHint().isNotEmpty(), "typing a taken name shows a hint: " + list->getHint());
                report (row->commitRename().failed() && row->getMode() == PresetRow::Mode::renaming, "a taken name is refused and editing continues");
                save (*editor, dir.getChildFile ("ui_preset_list_rename_error.png"));

                row->setRenameText ("Renamed Tone");
                report (list->getHint().isEmpty(), "a free name clears the hint");
                save (*editor, dir.getChildFile ("ui_preset_list_renaming.png"));
                report (row->commitRename().wasOk(), "a free name is accepted");
                settleLoop (300);

                report (p.presets.indexOf ("Renamed Tone") >= 0 && p.presets.indexOf ("UI Test Tone") < 0, "the preset has its new name");
                report (shownName() == "Renamed Tone", "the bar shows the new name of the current preset: " + shownName());
                report (list->findRow ("Renamed Tone") != nullptr && list->findRow ("UI Test Tone") == nullptr, "the list was rebuilt");
            }

            // delete: asks first, KEEP backs out, DELETE removes
            row = list->findRow ("Renamed Tone");
            if (row != nullptr)
            {
                row->beginDelete();
                report (row->getMode() == PresetRow::Mode::confirmingDelete, "the bin asks for confirmation");
                save (*editor, dir.getChildFile ("ui_preset_list_confirm_delete.png"));

                row->cancel();
                report (p.presets.indexOf ("Renamed Tone") >= 0, "KEEP leaves the preset alone");

                row->beginDelete();
                row->confirmDelete();
                settleLoop (300);

                report (p.presets.indexOf ("Renamed Tone") < 0, "DELETE removes the preset");
                report (list->findRow ("Renamed Tone") == nullptr && list->getNumRows() == p.presets.size(), "the list no longer shows it");
                report (shownName() == "Untitled", "deleting the current preset leaves the bar untitled: " + shownName());
                save (*editor, dir.getChildFile ("ui_preset_list_after_delete.png"));
            }

            // picking a line loads it and closes the list
            if (auto* pick = list->findRow ("Funk Clean"))
            {
                pick->onPick();
                settleLoop (300);
                report (shownName() == "Funk Clean", "clicking a line loads that preset: " + shownName());
                report (findFirst<PresetListPanel> (*editor) == nullptr, "the list closes after picking");
            }
        }
    }
    else
    {
        report (false, "the editor has a preset bar");
    }

    editor.reset();
    presetFolder.deleteRecursively();
}
