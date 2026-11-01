// Renders the editor to PNG files without opening a window, so the UI can be checked
// (and reviewed in pull requests) on a machine without touching the desktop.
//
//   NeuralFlowAmpTests --snapshots <output-dir>
#include "../Source/PluginProcessor.h"

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

    editor.reset();
}
