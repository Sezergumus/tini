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
            parts.add (id == PedalCatalog::ampItem ? juce::String ("AMP") : juce::String (PedalCatalog::all()[(size_t) id].name));
        return parts.joinIntoString (" > ");
    }
}

void renderUiSnapshots (const juce::File& dir)
{
    dir.createDirectory();

    NeuralFlowAmpAudioProcessor p;
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
        report (gateRow != nullptr && ! gateRow->isEnabled(), "a pedal that is already in the chain is greyed out in the popover");
        report (delayRow != nullptr && delayRow->isEnabled(), "a pedal that is available can be picked");

        if (delayRow != nullptr)
        {
            delayRow->triggerClick();
            settleLoop (400);
            report (p.apvts.getRawParameterValue ("DELAY_ADDED")->load() > 0.5f, "picking DELAY in the popover adds it to the chain");
            report (findFirst<AddPedalMenu> (*editor) == nullptr, "the popover closes after picking");
        }
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
                if (&v->getInfo() == &PedalCatalog::all()[(size_t) id])
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

    editor.reset();
}
