// Minimal headless checks for the audio engine. Exit code 0 = all passed.
#include "../Source/PluginProcessor.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void setParam (NeuralFlowAmpAudioProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    /** The processor reads getSampleRate(), which a real host sets before prepareToPlay(). */
    struct Prepared
    {
        Prepared()
        {
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);
        }
        NeuralFlowAmpAudioProcessor proc;
    };

    /** Runs `numBlocks` blocks. `fill` writes the input for each block (block index, buffer). */
    template <typename Fill>
    juce::AudioBuffer<float> run (NeuralFlowAmpAudioProcessor& p, int numBlocks, Fill fill)
    {
        juce::AudioBuffer<float> all (2, numBlocks * blockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            block.clear();
            fill (b, block);
            p.processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
        }
        return all;
    }

    int peakIndex (const juce::AudioBuffer<float>& buf, int channel)
    {
        int best = 0;
        float bestVal = 0.0f;
        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (std::abs (buf.getSample (channel, i)) > bestVal)
            {
                bestVal = std::abs (buf.getSample (channel, i));
                best = i;
            }
        return best;
    }

    /** Neutral settings so the signal path is easy to reason about. */
    void makeTransparent (NeuralFlowAmpAudioProcessor& p)
    {
        setParam (p, "COMP_ON", 0.0f);
        setParam (p, "OD_ON", 0.0f);
        setParam (p, "DRIVE", 1.0f);
        setParam (p, "MASTER", 10.0f);
    }

    //==========================================================================
    void testIrIsApplied()
    {
        // IR = direct spike + an echo (half amplitude) 100 samples later. A leading gap would be
        // removed by Trim::yes, so the delay has to sit between two non-silent samples.
        const int delay = 100;
        auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nfa_test_ir.wav");
        irFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            juce::AudioBuffer<float> ir (1, 256);
            ir.clear();
            ir.setSample (0, 0, 1.0f);
            ir.setSample (0, delay, 0.5f);

            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (new juce::FileOutputStream (irFile), sampleRate, 1, 24, {}, 0));
            writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        }

        auto impulse = [] (int block, juce::AudioBuffer<float>& buf)
        {
            if (block == 1)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, 10, 0.05f);   // impulse at absolute sample blockSize + 10
        };

        Prepared prepared;
        auto& p = prepared.proc;
        makeTransparent (p);

        const int at = blockSize + 10;   // where the impulse lands in the output
        auto echoRatio = [&] (const juce::AudioBuffer<float>& out)
        {
            return std::abs (out.getSample (0, at + delay)) / juce::jmax (1.0e-9f, std::abs (out.getSample (0, at)));
        };

        const auto dry = run (p, 6, impulse);
        check (std::abs (peakIndex (dry, 0) - at) < 8, "no IR: impulse passes straight through");
        check (echoRatio (dry) < 0.05f, "no IR: no echo after " + juce::String (delay) + " samples");

        p.loadImpulseResponse (irFile);

        // let the background loader finish and the engine swap in
        for (int i = 0; i < 200 && p.cabSimulator.getCurrentIRSize() <= 1; ++i)
        {
            run (p, 1, [] (int, juce::AudioBuffer<float>&) {});
            juce::Thread::sleep (10);
        }
        check (p.cabSimulator.getCurrentIRSize() > 1, "IR loaded into convolution engine");
        run (p, 8, [] (int, juce::AudioBuffer<float>&) {});   // finish the engine cross-fade

        const auto wet = run (p, 6, impulse);
        check (std::abs (echoRatio (wet) - 0.5f) < 0.15f,
               "IR applied: echo at +" + juce::String (delay) + " samples is ~0.5 of the direct hit (ratio "
               + juce::String (echoRatio (wet), 2) + ")");

        irFile.deleteFile();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testIrIsApplied();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
