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

    // the library lives in a temporary folder, never in the user's real Documents/tini
    FileLibrary::rootOverride() = dir.getChildFile ("tini_library_tmp");
    FileLibrary::rootOverride().deleteRecursively();
    FileLibrary::useTrash() = false;

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

    auto* addButton = findFirst<AddButton> (*editor);
    report (addButton != nullptr, "the chain bar has a + button");

    if (addButton != nullptr)
    {
        addButton->triggerClick();
        settleLoop (300);
        save (*editor, dir.getChildFile ("ui_add_menu.png"));
        report (findFirst<AddPedalMenu> (*editor) != nullptr, "the + button opens the popover");

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
        if (auto* panel = findFirst<ChainBar> (*editor))
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

    if (auto* panel = findFirst<ChainBar> (*editor))
    {
        auto& strip = panel->getStrip();

        auto pedalOf = [&] (int id) -> BlockTile*
        {
            for (auto* v : strip.pedals)
                if (v->getItem() == id)
                    return v;
            return nullptr;
        };
        auto* ampNode = &strip.getAmpTile();

        // the Capture pedal is not in the chain at first: it is parked right before the amp
        constexpr int gate = 0, comp = 1, boost = 2, od = 3, chorus = 4, delay = 5, reverb = 6, capture = 7, amp = PedalCatalog::ampItem;
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
        report (sameAs ({ gate, comp, boost, od, capture, amp, chorus, delay, reverb }), "the chain starts in the default order");

        // a click without moving must not reorder anything
        if (auto* b = pedalOf (boost))
        {
            strip.startDrag (*b, b->getBounds().getCentre());
            strip.dragTo (b->getBounds().getCentre() + juce::Point<int> (2, 0));
            strip.endDrag();
            report (sameAs ({ gate, comp, boost, od, capture, amp, chorus, delay, reverb }), "a click on the grip does not reorder");
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

            report (sameAs ({ gate, comp, od, amp, capture, boost, chorus, delay, reverb }),
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
            report (sameAs ({ amp, gate, comp, od, capture, boost, chorus, delay, reverb }),
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
    // Blocks: clicking a tile opens its page.
    auto* chainBar = findFirst<ChainBar> (*editor);
    auto* blocks = findFirst<BlockEditor> (*editor);
    report (chainBar != nullptr && blocks != nullptr, "the editor has a chain bar and a block editor");

    if (chainBar != nullptr && blocks != nullptr)
    {
        // a click is a mouse down and up on the tile without moving
        auto click = [&] (juce::Component& tile)
        {
            const auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> pos (6.0f, 6.0f);
            juce::MouseEvent down (source, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &tile, &tile, now, pos, now, 1, false);
            auto& strip = chainBar->getStrip();
            if (&tile == &chainBar->getCabTile())
            {
                tile.mouseUp (down);
            }
            else
            {
                strip.mouseDown (down.getEventRelativeTo (&strip));
                strip.mouseUp (down.getEventRelativeTo (&strip));
            }
            settleLoop (50);
        };

        for (auto id : { "GATE", "BOOST", "CHORUS", "DELAY", "REVERB" })
            setParam (p, juce::String (id) + "_ADDED", 1.0f);
        setParam (p, "GATE_ADDED_2", 0.0f);
        p.setChainOrder (PedalCatalog::defaultOrder());
        settle();
        settleLoop (300);

        report (blocks->getShownBlock() == PedalCatalog::ampItem && chainBar->getSelected() == PedalCatalog::ampItem,
                "the amp page is open at first");
        save (*editor, dir.getChildFile ("ui_page_amp.png"));

        BlockTile* delayTile = nullptr;
        BlockTile* gateTile = nullptr;
        for (auto* t : chainBar->getStrip().pedals)
        {
            if (t->getItem() == 5) delayTile = t;
            if (t->getItem() == 0) gateTile = t;
        }

        if (delayTile != nullptr)
        {
            click (*delayTile);
            report (blocks->getShownBlock() == 5 && blocks->getPedalPage() != nullptr && chainBar->getSelected() == 5,
                    "clicking a pedal tile opens that pedal's page");
            report (! blocks->getAmpPage().isVisible(), "...and hides the amp page");
            save (*editor, dir.getChildFile ("ui_page_pedal_delay.png"));
        }
        else
            report (false, "the delay pedal has a tile");

        click (chainBar->getCabTile());
        report (blocks->getShownBlock() == cabBlock && blocks->getCabPage().isVisible() && blocks->getPedalPage() == nullptr,
                "clicking the cab tile opens the cab page");
        save (*editor, dir.getChildFile ("ui_page_cab.png"));

        auto order = chainBar->getStrip().getItemOrder();
        report (std::find (order.begin(), order.end(), cabBlock) == order.end(), "the cab is not a draggable item of the chain");

        // the open pedal is removed: back to the amp
        if (delayTile != nullptr)
            click (*delayTile);
        report (blocks->getShownBlock() == 5, "the delay page is open again");
        setParam (p, "DELAY_ADDED", 0.0f);
        settle();
        settleLoop (300);
        report (blocks->getShownBlock() == PedalCatalog::ampItem && chainBar->getSelected() == PedalCatalog::ampItem,
                "removing the open pedal falls back to the amp page");

        // a pedal's bypass switch is bound to its parameter
        if (gateTile != nullptr)
        {
            click (*gateTile);
            if (auto* page = blocks->getPedalPage())
            {
                const bool before = p.apvts.getRawParameterValue ("GATE_ON")->load() > 0.5f;
                page->getBypass().triggerClick();
                settleLoop (50);
                report ((p.apvts.getRawParameterValue ("GATE_ON")->load() > 0.5f) != before, "the bypass switch on a pedal page toggles the pedal");
                page->getBypass().triggerClick();
                settleLoop (50);
            }
            else
            {
                report (false, "the gate has a page");
            }
        }

        click (chainBar->getStrip().getAmpTile());
        report (blocks->getShownBlock() == PedalCatalog::ampItem, "clicking the amp tile opens the amp page");

        // the dot of a tile is the bypass switch, and the tile shows its main value
        if (gateTile != nullptr)
        {
            const bool before = p.apvts.getRawParameterValue ("GATE_ON")->load() > 0.5f;
            gateTile->getDot().triggerClick();
            settleLoop (50);
            report ((p.apvts.getRawParameterValue ("GATE_ON")->load() > 0.5f) != before, "clicking the dot on a tile toggles the pedal");
            report (gateTile->isActive() != before, "...and the tile follows it");
            gateTile->getDot().triggerClick();
            settleLoop (50);

            report (gateTile->getDetail().startsWith ("Thresh") && gateTile->getDetail().endsWith ("dB"),
                    "a pedal tile shows its first knob: " + gateTile->getDetail());
            setParam (p, "GATE_THRESH", -33.0f);
            settleLoop (100);
            report (gateTile->getDetail().contains ("-33"), "...and follows the knob: " + gateTile->getDetail());
        }

        report (chainBar->getStrip().getAmpTile().getDetail().startsWith ("Drive"),
                "the amp tile shows Drive for the built-in amp: " + chainBar->getStrip().getAmpTile().getDetail());
        report (chainBar->getCabTile().getDetail().isNotEmpty(), "the cab tile shows its IR line: " + chainBar->getCabTile().getDetail());
        report (chainBar->getHeight() == 176 && BlockTile::width == 132 && BlockTile::height == 104, "the chain strip uses big tiles");

        // footer: meters and the audio device live here, not in the header
        if (auto* footer = findFirst<Footer> (*editor))
        {
            report (footer->getAudioInfo().contains ("kHz"), "the footer shows the audio device: " + footer->getAudioInfo());
            report (footer->getHeight() == Footer::height && footer->getBottom() <= Theme::designHeight, "the footer sits at the bottom");
            report (footer->inMeter.getX() < footer->getTunerButton().getX() && footer->outMeter.getX() > footer->getTunerButton().getX(),
                    "In is on the left, Out on the right, Tuner in the middle");

            // everything in the footer shares one centre line
            const int centre = footer->getHeight() / 2;
            report (std::abs (footer->getTunerButton().getBounds().getCentreY() - centre) <= 1
                        && std::abs (footer->inMeter.getBounds().getCentreY() - centre) <= 1
                        && std::abs (footer->outMeter.getBounds().getCentreY() - centre) <= 1,
                    "the meters and the Tuner button are centred on the same line");

            // gain lines: drag sets the parameter, double click resets it
            auto& in = footer->inMeter;
            report (! in.getGainLineBounds().isEmpty(), "the In meter has a gain line");
            const auto line = in.getGainLineBounds();
            juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(),
                                   line.getPosition().translated (line.getWidth() * 0.75f, line.getHeight() * 0.5f),
                                   juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   &in, &in, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
            in.mouseDown (down);
            in.mouseUp (down);
            report (std::abs (p.apvts.getRawParameterValue ("IN_GAIN")->load() - 12.0f) < 0.2f,
                    "clicking the gain line at 75 % sets +12 dB (" + juce::String (p.apvts.getRawParameterValue ("IN_GAIN")->load(), 1) + ")");
            in.mouseDoubleClick (down);
            report (std::abs (p.apvts.getRawParameterValue ("IN_GAIN")->load()) < 0.01f, "a double click puts the input gain back to 0 dB");

            footer->outMeter.setGainDb (-6.0f);
            settle();
            save (*editor, dir.getChildFile ("ui_footer_gain.png"));
            footer->outMeter.setGainDb (0.0f);
        }
        else
        {
            report (false, "the editor has a footer");
        }

        for (auto id : { "GATE", "BOOST", "CHORUS", "DELAY", "REVERB" })
            setParam (p, juce::String (id) + "_ADDED", 0.0f);
        settle();
    }

    //==========================================================================
    // NAM amp slot: type switch and model slot.
    if (blocks != nullptr)
    {
        auto& amp = blocks->getAmpPage();
        const auto model = juce::File (__FILE__).getParentDirectory().getChildFile ("models").getChildFile ("test_wavenet.nam");
        auto ampType = [&] { return p.apvts.getRawParameterValue ("AMP_TYPE")->load(); };

        blocks->show (PedalCatalog::ampItem);
        p.unloadNamModel();
        setParam (p, "AMP_TYPE", 0.0f);
        settle();

        report (amp.builtInButton.getToggleState() && ! amp.namButton.getToggleState(), "the built-in amp is selected at first");
        report (amp.getDriveKnob().isVisible() && ! amp.getInputKnob().isVisible() && ! amp.modelSlot.isVisible(),
                "built-in mode shows Drive and no model slot");
        save (*editor, dir.getChildFile ("ui_amp_builtin.png"));

        amp.namButton.triggerClick();
        settleLoop (100);
        report (ampType() > 0.5f && amp.namButton.getToggleState() && ! amp.builtInButton.getToggleState(),
                "the NAM button switches the amp to NAM");
        report (! amp.getDriveKnob().isVisible() && amp.getInputKnob().isVisible() && amp.modelSlot.isVisible(),
                "NAM mode shows Input instead of Drive, and the model slot");
        save (*editor, dir.getChildFile ("ui_amp_nam_empty.png"));

        p.loadNamModel (model);
        for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
            settleLoop (10);
        settle();
        report (p.nam.getStatus().state == NamEngine::Status::State::ready && p.nam.hasModel(), "a model loads while the UI is running");
        save (*editor, dir.getChildFile ("ui_amp_nam_loaded.png"));

        const float levelBefore = p.apvts.getRawParameterValue ("NAM_NORM")->load();
        amp.modelSlot.levelButton.triggerClick();
        settleLoop (50);
        report ((p.apvts.getRawParameterValue ("NAM_NORM")->load() > 0.5f) != (levelBefore > 0.5f),
                "Level match toggles the normalisation (" + juce::String (levelBefore) + " -> "
                    + juce::String (p.apvts.getRawParameterValue ("NAM_NORM")->load()) + ")");
        amp.modelSlot.levelButton.triggerClick();
        settleLoop (50);

        amp.modelSlot.clearButton.triggerClick();
        settle();
        report (p.nam.getStatus().state == NamEngine::Status::State::empty && ! p.nam.hasModel(), "Clear removes the model");

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
        amp.builtInButton.triggerClick();
        settle();
        report (ampType() < 0.5f && amp.builtInButton.getToggleState(), "the Built-in button switches back");
    }
    else
    {
        report (false, "the block editor exists");
    }

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
            if (auto* panel = findFirst<ChainBar> (*editor))
                n = panel->getStrip().pedals.size();
            return n;
        };

        report (shownName() == "Default", "the preset bar shows the current preset: " + shownName());
        report (! bar->getSaveButton().isVisible(), "an unchanged preset shows no Save button");

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
        report (bar->getSaveButton().isVisible(), "...and the Save button appears right of the name");
        report (bar->getSaveButton().getX() > bar->getNameButton().getRight(), "...to the right of the name");
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
            report (bar->getSaveButton().isVisible() && bar->getSaveButton().getButtonText() == "Saved", "the Save button confirms with Saved");
            save (*editor, dir.getChildFile ("ui_after_save.png"));
        }

        // the saved preset is reachable with the arrows and brings the tweak back
        bar->select (0);
        settleLoop (200);
        bar->getPrevButton().triggerClick();
        settleLoop (300);
        report (shownName() == "UI Test Tone", "the saved preset is the last one in the list: " + shownName());
        report (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 70.0f) < 0.01f, "...and restores the saved settings");
        settleLoop (2000);
        report (! bar->getSaveButton().isVisible(), "the Save button goes away again once nothing is changed");

        // opening the dialog on a user preset suggests its own name, and saving replaces it
        setParam (p, "DRIVE", 72.0f);
        settle();
        settleLoop (200);
        report (bar->getSaveButton().isVisible(), "changing the user preset brings the Save button back");
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

    //==========================================================================
    // Model / IR browser: the list of the loaded file's folder, the arrows and the Browse switch.
    if (blocks != nullptr)
    {
        const auto libDir = dir.getChildFile ("library_tmp");
        libDir.deleteRecursively();
        libDir.createDirectory();

        const auto testModels = juce::File (__FILE__).getParentDirectory().getChildFile ("models");
        const auto alpha = libDir.getChildFile ("Alpha Clean.nam");
        const auto beta  = libDir.getChildFile ("Beta Crunch.nam");
        testModels.getChildFile ("test_wavenet.nam").copyFileTo (alpha);
        testModels.getChildFile ("test_a2.nam").copyFileTo (beta);
        libDir.getChildFile ("notes.txt").replaceWithText ("not a model");

        auto waitForModel = [&]
        {
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
        };

        auto& amp = blocks->getAmpPage();
        blocks->show (PedalCatalog::ampItem);
        p.unloadNamModel();
        setParam (p, "AMP_TYPE", 1.0f);
        p.loadNamModel (alpha);
        waitForModel();
        settleLoop (200);

        report (amp.browser.isVisible(), "the model list is open in NAM mode");
        report (amp.browser.getFolder() == libDir, "it shows the folder of the loaded model");
        report (amp.browser.getNumFiles() == 2, "it lists the .nam files and nothing else (" + juce::String (amp.browser.getNumFiles()) + ")");
        report (amp.browser.getNumFiles() == 2 && amp.browser.getFile (0) == alpha && amp.browser.getFile (1) == beta, "...sorted by name");
        save (*editor, dir.getChildFile ("ui_amp_nam_browser.png"));

        // picking a line loads that model
        if (amp.browser.getNumFiles() == 2)
        {
            amp.browser.onPick (beta);
            waitForModel();
            report (p.nam.getFile() == beta && p.nam.hasModel(), "picking a line loads that model");

            // the arrows go to the next / previous model in the folder, wrapping around
            amp.modelSlot.nextButton.triggerClick();
            waitForModel();
            report (p.nam.getFile() == alpha, "the next arrow wraps to the first model");
            amp.modelSlot.prevButton.triggerClick();
            waitForModel();
            report (p.nam.getFile() == beta, "the previous arrow wraps to the last model");
        }

        // the Browse switch hides the list and gives the width back to the knobs
        const int widthWithList = amp.getDriveKnob().getWidth() + amp.getInputKnob().getWidth();
        amp.modelSlot.browseButton.triggerClick();
        settleLoop (100);
        report (! amp.browser.isVisible(), "Browse switches the list off");
        report (amp.getInputKnob().getWidth() + amp.getDriveKnob().getWidth() > widthWithList,
                "...and the knobs get the room");
        save (*editor, dir.getChildFile ("ui_amp_nam_browser_closed.png"));
        amp.modelSlot.browseButton.triggerClick();
        settleLoop (100);
        report (amp.browser.isVisible(), "...and on again");

        // built-in mode has no list
        setParam (p, "AMP_TYPE", 0.0f);
        settle();
        report (! amp.browser.isVisible(), "the built-in amp has no model list");
        p.unloadNamModel();

        // impulse responses
        auto writeIr = [&] (const juce::File& file, float gain)
        {
            juce::WavAudioFormat wav;
            file.deleteFile();
            std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());
            if (out == nullptr)
                return;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0));
            if (writer == nullptr)
                return;
            out.release();

            juce::AudioBuffer<float> ir (1, 256);
            ir.clear();
            ir.setSample (0, 0, gain);
            ir.setSample (0, 40, gain * 0.4f);
            writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        };

        const auto irDir = dir.getChildFile ("library_tmp").getChildFile ("IRs");
        irDir.createDirectory();
        const auto irA = irDir.getChildFile ("Cab 4x12.wav");
        const auto irB = irDir.getChildFile ("Cab 1x12.wav");
        writeIr (irA, 0.8f);
        writeIr (irB, 0.6f);

        p.loadImpulseResponse (irA);
        blocks->show (cabBlock);
        settleLoop (300);
        auto& cab = blocks->getCabPage();

        report (cab.browser.isVisible() && cab.browser.getFolder() == irDir, "the cab page lists the folder of the loaded IR");
        report (cab.browser.getNumFiles() == 2, "...with both impulse responses (" + juce::String (cab.browser.getNumFiles()) + ")");
        report (cab.browser.getNumFiles() == 2 && cab.browser.getFile (0) == irB, "...sorted by name");
        save (*editor, dir.getChildFile ("ui_cab_browser.png"));

        cab.browser.onPick (irB);
        settleLoop (200);
        report (p.loadedIrFile == irB, "picking an impulse response loads it");

        cab.irSlot.nextButton.triggerClick();
        settleLoop (200);
        report (p.loadedIrFile == irA, "the next arrow goes on to the next IR");
        cab.irSlot.prevButton.triggerClick();
        settleLoop (200);
        report (p.loadedIrFile == irB, "the previous arrow goes back");

        cab.irSlot.browseButton.triggerClick();
        settleLoop (100);
        report (! cab.browser.isVisible(), "Browse hides the IR list");
        save (*editor, dir.getChildFile ("ui_cab_browser_closed.png"));
        cab.irSlot.browseButton.triggerClick();
        settleLoop (100);
        report (cab.browser.isVisible(), "...and shows it again");

        blocks->show (PedalCatalog::ampItem);
        settle();
        libDir.deleteRecursively();
    }

    //==========================================================================
    // Library: Documents/tini/{Amps,Pedals,IRs} is watched live, files can be added, renamed and deleted
    // from the list, and files dropped on the window are copied in and loaded.
    if (auto* view = findFirst<MainView> (*editor))
    {
        auto& blocks3 = view->getBlocks();
        const auto testModels = juce::File (__FILE__).getParentDirectory().getChildFile ("models");
        const auto outside = dir.getChildFile ("outside_tmp");   // where "downloads" come from
        outside.deleteRecursively();
        outside.createDirectory();

        auto waitForModel = [&]
        {
            for (int i = 0; i < 300 && p.nam.getStatus().state == NamEngine::Status::State::loading; ++i)
                settleLoop (10);
            settle();
        };

        FileLibrary::rootOverride().deleteRecursively();
        report (! FileLibrary::libraryFolder (FileLibrary::Kind::amps).exists(), "the test library starts empty");

        // a list that shows the library folder follows it while it is visible
        {
            FileBrowserList list (FileLibrary::Kind::amps);
            list.setBounds (0, 0, 300, 400);
            list.setVisible (true);
            settleLoop (50);

            const auto amps = FileLibrary::libraryFolder (FileLibrary::Kind::amps);
            report (amps.isDirectory() && FileLibrary::libraryFolder (FileLibrary::Kind::pedals).isDirectory()
                        && FileLibrary::libraryFolder (FileLibrary::Kind::irs).isDirectory(),
                    "showing a list creates the Amps, Pedals and IRs folders");
            report (list.getFolder() == amps && list.getNumFiles() == 0, "an empty library shows an empty list");

            testModels.getChildFile ("test_wavenet.nam").copyFileTo (amps.getChildFile ("One.nam"));
            list.refreshIfChanged();
            report (list.getNumFiles() == 1, "a file copied in with the file manager shows up");

            testModels.getChildFile ("test_a2.nam").copyFileTo (amps.getChildFile ("Two.nam"));
            amps.getChildFile ("readme.txt").replaceWithText ("x");
            settleLoop (1300);   // the list polls the folder about once a second
            report (list.getNumFiles() == 2, "...and the list notices by itself (" + juce::String (list.getNumFiles()) + ")");

            amps.getChildFile ("One.nam").deleteFile();
            list.refreshIfChanged();
            report (list.getNumFiles() == 1 && list.getFile (0).getFileName() == "Two.nam", "a file removed with the file manager disappears");

            // Add files: copies from anywhere
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (outside.getChildFile ("Fresh.nam"));
            outside.getChildFile ("cover.png").replaceWithText ("x");
            const int added = list.addFiles ({ outside.getChildFile ("Fresh.nam"), outside.getChildFile ("cover.png") });
            report (added == 1 && amps.getChildFile ("Fresh.nam").existsAsFile() && list.getNumFiles() == 2,
                    "Add files copies models in and skips other files");
            report (outside.getChildFile ("Fresh.nam").existsAsFile(), "...the original stays where it was");

            // dropping on the list does the same
            testModels.getChildFile ("test_a2.nam").copyFileTo (outside.getChildFile ("Dropped.nam"));
            report (list.isInterestedInFileDrag ({ outside.getChildFile ("Dropped.nam").getFullPathName() }), "the list accepts .nam files");
            report (! list.isInterestedInFileDrag ({ outside.getChildFile ("cover.png").getFullPathName() }), "...and nothing else");
            list.filesDropped ({ outside.getChildFile ("Dropped.nam").getFullPathName() }, 0, 0);
            report (list.getNumFiles() == 3, "a file dropped on the list is added to it");

            // rename keeps the extension and refuses a name in use
            juce::File renamedTo;
            bool renamedCalled = false;
            list.onRenamed = [&] (const juce::File&, const juce::File& n) { renamedCalled = true; renamedTo = n; };
            auto renameResult = list.renameFile (amps.getChildFile ("Fresh.nam"), "Clean: Fender?");
            report (renameResult.wasOk() && renamedCalled && renamedTo.getFileExtension() == ".nam" && renamedTo.existsAsFile()
                        && ! amps.getChildFile ("Fresh.nam").exists(),
                    "Rename moves the file and keeps the extension (" + renamedTo.getFileName() + ")");
            report (list.renameFile (renamedTo, "Two").failed(), "Rename refuses a name that is in use");
            report (list.renameFile (renamedTo, "   ").failed(), "Rename refuses an empty name");

            // delete
            bool deletedCalled = false;
            list.onDeleted = [&] (const juce::File&) { deletedCalled = true; };
            report (list.deleteFile (renamedTo) && deletedCalled && ! renamedTo.exists() && list.getNumFiles() == 2,
                    "Delete removes the file and updates the list");

            // a list stays on the folder of the loaded file, until files are added to the library
            list.setCurrentFile (outside.getChildFile ("Fresh.nam"));
            report (list.getFolder() == outside, "a loaded file outside the library shows its own folder");
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (outside.getChildFile ("Late.nam"));
            list.addFiles ({ outside.getChildFile ("Late.nam") });
            report (list.getFolder() == amps, "...but adding files shows the library, where they went");
        }

        // legacy folder: "Models" is renamed to "Amps" once, saved paths follow
        {
            FileLibrary::rootOverride().deleteRecursively();
            const auto legacy = FileLibrary::root().getChildFile ("Models");
            legacy.createDirectory();
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (legacy.getChildFile ("Old.nam"));
            const auto oldPath = legacy.getChildFile ("Old.nam");

            FileLibrary::ensureFolders();
            report (! legacy.exists() && FileLibrary::libraryFolder (FileLibrary::Kind::amps).getChildFile ("Old.nam").existsAsFile(),
                    "an older Models folder becomes Amps");
            report (FileLibrary::resolveSaved (oldPath) == FileLibrary::libraryFolder (FileLibrary::Kind::amps).getChildFile ("Old.nam"),
                    "...and a path saved by the old version still finds its model");

            // the session of an older version loads its model from the new place
            juce::MemoryBlock state;
            p.getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            if (xml != nullptr)
            {
                xml->setAttribute ("namPath", oldPath.getFullPathName());
                juce::MemoryBlock patched;
                juce::AudioProcessor::copyXmlToBinary (*xml, patched);
                p.setStateInformation (patched.getData(), (int) patched.getSize());
                waitForModel();
                report (p.nam.getFile().getFileName() == "Old.nam" && p.nam.hasModel(), "a saved session finds the model in Amps");
            }
            p.unloadNamModel();
        }

        // import rules
        {
            FileLibrary::rootOverride().deleteRecursively();
            const auto src = outside.getChildFile ("Dup.nam");
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (src);

            const auto first = FileLibrary::importFile (src, FileLibrary::Kind::amps);
            report (first.existsAsFile() && first.getParentDirectory() == FileLibrary::libraryFolder (FileLibrary::Kind::amps),
                    "import copies into the library");
            report (FileLibrary::importFile (src, FileLibrary::Kind::amps) == first, "importing the same file again reuses the copy");
            report (FileLibrary::importFile (first, FileLibrary::Kind::amps) == first, "a file already in the library is left alone");

            const auto other = outside.getChildFile ("Other");
            other.createDirectory();
            const auto different = other.getChildFile ("Dup.nam");
            testModels.getChildFile ("test_a2.nam").copyFileTo (different);
            const auto second = FileLibrary::importFile (different, FileLibrary::Kind::amps);
            report (second.existsAsFile() && second != first && second.getFileName().startsWith ("Dup"),
                    "a different file with the same name gets its own copy (" + second.getFileName() + ")");

            FileLibrary::Kind kind;
            report (FileLibrary::kindOfFile (juce::File ("x.NAM"), kind) && kind == FileLibrary::Kind::amps, ".nam belongs to the amp library");
            report (FileLibrary::kindOfFile (juce::File ("x.wav"), kind) && kind == FileLibrary::Kind::irs, ".wav belongs to the IR library");
            report (! FileLibrary::kindOfFile (juce::File ("x.png"), kind), "other files belong nowhere");
        }

        // dropped on the window: copied in and loaded
        {
            FileLibrary::rootOverride().deleteRecursively();
            setParam (p, "AMP_TYPE", 0.0f);
            p.unloadNamModel();
            view->getChain().select (PedalCatalog::ampItem);

            const auto model = outside.getChildFile ("Dropped Amp.nam");
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (model);

            auto ir = outside.getChildFile ("Dropped Cab.wav");
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::FileOutputStream> out (ir.createOutputStream());
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0));
                if (writer != nullptr)
                {
                    out.release();
                    juce::AudioBuffer<float> spike (1, 64);
                    spike.clear();
                    spike.setSample (0, 0, 1.0f);
                    writer->writeFromAudioSampleBuffer (spike, 0, 64);
                }
            }

            report (view->isInterestedInFileDrag ({ model.getFullPathName() }) && view->isInterestedInFileDrag ({ ir.getFullPathName() })
                        && ! view->isInterestedInFileDrag ({ outside.getChildFile ("cover.png").getFullPathName() }),
                    "the window accepts models and impulse responses only");

            view->filesDropped ({ outside.getChildFile ("cover.png").getFullPathName(), model.getFullPathName(), ir.getFullPathName() }, 10, 10);
            waitForModel();
            settleLoop (200);

            const auto ampsFolder = FileLibrary::libraryFolder (FileLibrary::Kind::amps);
            const auto irsFolder = FileLibrary::libraryFolder (FileLibrary::Kind::irs);
            report (ampsFolder.getChildFile ("Dropped Amp.nam").existsAsFile() && irsFolder.getChildFile ("Dropped Cab.wav").existsAsFile(),
                    "dropped files are copied into Amps and IRs");
            report (p.nam.getFile() == ampsFolder.getChildFile ("Dropped Amp.nam") && p.nam.hasModel(), "the dropped model is loaded from the library");
            report (p.apvts.getRawParameterValue ("AMP_TYPE")->load() > 0.5f, "...and the amp switches to NAM");
            report (p.loadedIrFile == irsFolder.getChildFile ("Dropped Cab.wav"), "the dropped impulse response is loaded into the cab");
            report (blocks3.getShownBlock() == cabBlock, "the cab page is shown last");

            p.unloadNamModel();
            setParam (p, "AMP_TYPE", 0.0f);
            view->getChain().select (PedalCatalog::ampItem);
            settle();
        }

        // Capture pedal: its page has a model slot and the list of the Pedals folder
        {
            FileLibrary::rootOverride().deleteRecursively();
            FileLibrary::ensureFolders();
            const auto pedalsFolder = FileLibrary::libraryFolder (FileLibrary::Kind::pedals);
            testModels.getChildFile ("test_wavenet.nam").copyFileTo (pedalsFolder.getChildFile ("Green Drive.nam"));
            testModels.getChildFile ("test_a2.nam").copyFileTo (pedalsFolder.getChildFile ("Fuzz Face.nam"));

            setParam (p, "CAP_ADDED", 1.0f);
            settle();

            view->getChain().select (PedalCatalog::captureType);
            settleLoop (200);
            auto* page = blocks3.getPedalPage();
            report (page != nullptr && page->getModelSlot() != nullptr && page->getBrowser() != nullptr, "the Capture pedal's page has a model slot and a list");
            report (page != nullptr && page->getBrowser()->isVisible() && page->getBrowser()->getKind() == FileLibrary::Kind::pedals
                        && page->getBrowser()->getFolder() == pedalsFolder && page->getBrowser()->getNumFiles() == 2,
                    "...the list shows the Pedals folder");
            save (*editor, dir.getChildFile ("ui_page_capture_empty.png"));

            if (page != nullptr && page->getModelSlot() != nullptr)
            {
                page->getBrowser()->onPick (pedalsFolder.getChildFile ("Green Drive.nam"));
                for (int i = 0; i < 300 && p.pedalNam[0].getStatus().state == NamEngine::Status::State::loading; ++i)
                    settleLoop (10);
                settleLoop (300);

                report (p.pedalNam[0].getFile() == pedalsFolder.getChildFile ("Green Drive.nam") && p.pedalNam[0].hasModel(),
                        "picking a line loads the capture into this pedal");
                report (! p.nam.hasModel(), "...and not into the amp");

                bool tileShowsName = false;
                for (auto* tile : view->getChain().getStrip().pedals)
                    if (tile->getItem() == PedalCatalog::captureType)
                        tileShowsName = tile->getDetail().isNotEmpty() && tile->getDetail() != "No model";
                report (tileShowsName, "the pedal's tile shows the model name");
                save (*editor, dir.getChildFile ("ui_page_capture.png"));

                page->getModelSlot()->nextButton.triggerClick();
                settleLoop (100);   // a click is delivered by the message loop
                for (int i = 0; i < 300 && p.pedalNam[0].getStatus().state == NamEngine::Status::State::loading; ++i)
                    settleLoop (10);
                report (p.pedalNam[0].getFile() == pedalsFolder.getChildFile ("Green Drive.nam") ? false : p.pedalNam[0].getFile().existsAsFile(),
                        "the arrows step through the Pedals folder (" + p.pedalNam[0].getFile().getFileName() + ")");
            }

            setParam (p, "CAP_ADDED", 0.0f);
            p.unloadPedalModel (0);
            settle();
        }

        FileLibrary::rootOverride().deleteRecursively();
        outside.deleteRecursively();
    }

    //==========================================================================
    // Tuner: the footer button opens it, the processor collects input only while it is open.
    if (auto* view = findFirst<MainView> (*editor))
    {
        auto& blocks2 = view->getBlocks();
        auto& footer = view->getFooter();

        // feeds a sine of `freq` straight into the processor, continuing the phase between calls
        long position = 0;
        auto feedTone = [&] (double freq, int numBlocks)
        {
            juce::MidiBuffer midi;
            for (int b = 0; b < numBlocks; ++b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) (position + i) / 48000.0);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                position += 512;
                p.processBlock (buf, midi);
            }
        };

        report (! p.isTunerActive(), "the tuner is off at first");

        footer.getTunerButton().triggerClick();
        settleLoop (100);
        auto* tuner = dynamic_cast<TunerView*> (blocks2.getOverlay());
        report (tuner != nullptr && blocks2.getOverlayKind() == BlockEditor::Overlay::tuner, "the Tuner button opens the tuner");
        report (p.isTunerActive(), "...and switches the tuner input on");
        report (footer.getTunerButton().getToggleState(), "...and the button stays lit");
        report (! blocks2.getAmpPage().isVisible(), "...and the block page steps aside");

        if (tuner != nullptr)
        {
            save (*editor, dir.getChildFile ("ui_tuner_idle.png"));
            report (! tuner->hasPitch(), "without sound the tuner shows no note");

            for (int i = 0; i < 6; ++i)
            {
                feedTone (196.0, 3);
                settleLoop (40);
            }
            settleLoop (200);
            report (tuner->hasPitch() && tuner->getNoteName() == "G" && tuner->getOctave() == 3, "a 196 Hz string shows G3: " + tuner->getNoteName() + juce::String (tuner->getOctave()));
            report (std::abs (tuner->getCents()) < 2.0f && tuner->isInTune(), "...in tune (" + juce::String (tuner->getCents(), 2) + " cents)");
            report (std::abs (tuner->getFrequency() - 196.0f) < 0.5f, "...at " + juce::String (tuner->getFrequency(), 2) + " Hz");
            save (*editor, dir.getChildFile ("ui_tuner_in_tune.png"));

            // 25 cents flat of A2
            const double flatA = 110.0 * std::pow (2.0, -25.0 / 1200.0);
            for (int i = 0; i < 8; ++i)
            {
                feedTone (flatA, 3);
                settleLoop (40);
            }
            settleLoop (200);
            report (tuner->getNoteName() == "A" && tuner->getOctave() == 2 && std::abs (tuner->getCents() + 25.0f) < 3.0f && ! tuner->isInTune(),
                    "a flat string shows its note and how far off it is: " + tuner->getNoteName() + juce::String (tuner->getOctave()) + " " + juce::String (tuner->getCents(), 1));
            save (*editor, dir.getChildFile ("ui_tuner_flat.png"));

            tuner->getMuteSwitch().triggerClick();
            settleLoop (50);
            report (p.isTunerMuted(), "the Mute switch mutes the output");
            save (*editor, dir.getChildFile ("ui_tuner_muted.png"));

            // the string stops: after a moment the note goes away
            settleLoop (1800);
            report (! tuner->hasPitch(), "the note goes away once the sound has stopped");
        }

        // a click on a block closes the tuner and switches everything off
        view->getChain().select (PedalCatalog::ampItem);
        settleLoop (100);
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none && blocks2.getAmpPage().isVisible(), "clicking a block closes the tuner");
        report (! p.isTunerActive() && ! p.isTunerMuted(), "...switches the tuner input off and lifts the mute");
        report (! footer.getTunerButton().getToggleState(), "...and the Tuner button is not lit any more");

        // the button closes it too
        footer.getTunerButton().triggerClick();
        settleLoop (100);
        footer.getTunerButton().triggerClick();
        settleLoop (100);
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none && ! p.isTunerActive(), "the Tuner button toggles the tuner");
    }
    else
    {
        report (false, "the editor has a main view for the tuner");
    }

    //==========================================================================
    // Settings page: opened by the gear, closed by the gear or a block.
    if (auto* view = findFirst<MainView> (*editor))
    {
        auto& blocks2 = view->getBlocks();
        report (! view->getMinimiseButton().isVisible() && ! view->getCloseButton().isVisible(),
                "no window buttons without a standalone window (plugin in a DAW)");
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none, "no overlay page at first");

        view->getGearButton().triggerClick();
        settleLoop (100);
        auto* settings = dynamic_cast<SettingsPage*> (blocks2.getOverlay());
        report (settings != nullptr && blocks2.getOverlayKind() == BlockEditor::Overlay::settings, "the gear opens the Settings page");
        report (view->getGearButton().getToggleState(), "...and the gear stays lit");
        report (! blocks2.getAmpPage().isVisible(), "...and the block page steps aside");
        report (! view->getChain().isVisible() && blocks2.getY() == view->getChain().getY(),
                "...and the page also takes the chain strip's place (full window)");
        report (settings != nullptr && ! settings->hasAudioSelector(), "inside a plugin the host owns the audio device: no device selector");
        save (*editor, dir.getChildFile ("ui_settings_plugin.png"));

        if (settings != nullptr)
        {
            settings->getReferenceSlider().setValue (432.0, juce::sendNotificationSync);
            report (std::abs (p.getTunerReferenceHz() - 432.0f) < 0.01f, "the reference pitch slider sets the tuner's A4");

            juce::MemoryBlock state;
            p.getStateInformation (state);
            p.setTunerReferenceHz (440.0f);
            p.setStateInformation (state.getData(), (int) state.getSize());
            report (std::abs (p.getTunerReferenceHz() - 432.0f) < 0.01f, "...and it is saved with the session");
            p.setTunerReferenceHz (440.0f);
        }

        view->getGearButton().triggerClick();
        settleLoop (100);
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none && blocks2.getAmpPage().isVisible(), "the gear closes the Settings page again");
        report (view->getChain().isVisible() && blocks2.getY() > view->getChain().getY(), "...and the chain strip is back");

        // the Done button inside the page closes it too
        view->getGearButton().triggerClick();
        settleLoop (100);
        if (auto* page = dynamic_cast<SettingsPage*> (blocks2.getOverlay()))
        {
            page->getDoneButton().triggerClick();
            settleLoop (100);
        }
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none && view->getChain().isVisible(), "the Done button closes the Settings page");

        view->getGearButton().triggerClick();
        settleLoop (100);
        view->getChain().select (PedalCatalog::ampItem);
        settleLoop (100);
        report (blocks2.getOverlayKind() == BlockEditor::Overlay::none && blocks2.getAmpPage().isVisible(), "clicking a block closes the Settings page");
    }
    else
    {
        report (false, "the editor has a main view");
    }

    editor.reset();

    // The standalone window: its hooks add the window buttons and the audio device selector.
    {
        int minimised = 0, toggled = 0, closed = 0;
        juce::AudioDeviceManager deviceManager;

        auto& hooks = StandaloneHooks::controller();
        hooks.minimise       = [&] { ++minimised; };
        hooks.toggleMaximise = [&] { ++toggled; };
        hooks.close          = [&] { ++closed; };
        hooks.isMaximised    = [&] { return toggled % 2 == 1; };
        hooks.deviceManager  = [&] { return &deviceManager; };

        std::unique_ptr<juce::AudioProcessorEditor> standalone (p.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

        if (auto* view = findFirst<MainView> (*standalone))
        {
            report (view->getMinimiseButton().isVisible() && view->getMaximiseButton().isVisible() && view->getCloseButton().isVisible(),
                    "the standalone header has minimise, fit and close buttons");
            report (view->getMinimiseButton().getX() < view->getMaximiseButton().getX()
                        && view->getMaximiseButton().getX() < view->getCloseButton().getX()
                        && view->getCloseButton().getRight() <= Theme::designWidth,
                    "...in that order at the right end");
            report (view->getGearButton().getRight() < view->getMinimiseButton().getX(), "...with the gear to their left");

            view->getMinimiseButton().triggerClick();
            view->getMaximiseButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            report (minimised == 1 && toggled == 1 && closed == 0, "minimise and fit call the window");
            view->getCloseButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            report (closed == 1, "close asks the application to quit");

            // double click on the empty header fits the window
            const auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> pos (200.0f, 20.0f);
            juce::MouseEvent e (source, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, view, view, now, pos, now, 2, false);
            view->mouseDoubleClick (e);
            report (toggled == 2, "a double click on the header fits the window");

            view->getGearButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            auto* settings = dynamic_cast<SettingsPage*> (view->getBlocks().getOverlay());
            report (settings != nullptr && settings->hasAudioSelector(), "the standalone Settings page has the audio device selector");
            save (*standalone, dir.getChildFile ("ui_settings_standalone.png"));
        }
        else
        {
            report (false, "the standalone editor has a main view");
        }

        standalone.reset();
        hooks = {};
    }

    presetFolder.deleteRecursively();
}
