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

    /** IR = direct spike + an echo (half amplitude) `delay` samples later. A leading gap would be
        removed by Trim::yes, so the delay has to sit between two non-silent samples. */
    juce::File writeEchoIr (int delay)
    {
        auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nfa_test_ir.wav");
        irFile.deleteFile();

        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> ir (1, 256);
        ir.clear();
        ir.setSample (0, 0, 1.0f);
        ir.setSample (0, delay, 0.5f);

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (new juce::FileOutputStream (irFile), sampleRate, 1, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        return irFile;
    }

    /** juce::dsp::Convolution starts with a 1-sample unity IR, so "loaded" means size > 1. */
    bool waitForIr (NeuralFlowAmpAudioProcessor& p)
    {
        for (int i = 0; i < 200 && p.cabSimulator.getCurrentIRSize() <= 1; ++i)
        {
            run (p, 1, [] (int, juce::AudioBuffer<float>&) {});
            juce::Thread::sleep (10);
        }
        run (p, 8, [] (int, juce::AudioBuffer<float>&) {});   // finish the engine cross-fade
        return p.cabSimulator.getCurrentIRSize() > 1;
    }

    //==========================================================================
    void testIrIsApplied()
    {
        const int delay = 100;
        auto irFile = writeEchoIr (delay);

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

        check (waitForIr (p), "IR loaded into convolution engine");

        const auto wet = run (p, 6, impulse);
        check (std::abs (echoRatio (wet) - 0.5f) < 0.15f,
               "IR applied: echo at +" + juce::String (delay) + " samples is ~0.5 of the direct hit (ratio "
               + juce::String (echoRatio (wet), 2) + ")");

        irFile.deleteFile();
    }
    void testStateRoundTrip()
    {
        const int delay = 100;
        auto irFile = writeEchoIr (delay);

        juce::MemoryBlock saved;
        {
            Prepared a;
            setParam (a.proc, "DRIVE", 77.0f);
            setParam (a.proc, "LOW", 2.0f);
            setParam (a.proc, "COMP_ON", 0.0f);
            setParam (a.proc, "CAB_HIGHCUT", 8000.0f);
            a.proc.loadImpulseResponse (irFile);
            a.proc.getStateInformation (saved);
        }
        check (saved.getSize() > 0, "getStateInformation writes data");

        Prepared b;
        b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

        auto get = [&] (const char* id) { return b.proc.apvts.getRawParameterValue (id)->load(); };
        check (std::abs (get ("DRIVE") - 77.0f) < 0.01f, "state restores DRIVE");
        check (std::abs (get ("LOW") - 2.0f) < 0.01f, "state restores LOW");
        check (get ("COMP_ON") < 0.5f, "state restores the compressor bypass");
        check (std::abs (get ("CAB_HIGHCUT") - 8000.0f) < 1.0f, "state restores CAB_HIGHCUT");
        check (b.proc.loadedIrFile == irFile, "state remembers the IR file");
        check (waitForIr (b.proc), "state reloads the IR into the convolution engine");

        irFile.deleteFile();
    }
    float rms (const juce::AudioBuffer<float>& buf, int channel, int from)
    {
        double sum = 0.0;
        for (int i = from; i < buf.getNumSamples(); ++i)
            sum += (double) buf.getSample (channel, i) * buf.getSample (channel, i);
        return (float) std::sqrt (sum / (buf.getNumSamples() - from));
    }

    void testEqProcessesBothChannels()
    {
        Prepared prepared;
        auto& p = prepared.proc;
        makeTransparent (p);
        setParam (p, "LOW", 10.0f);   // +15 dB low shelf

        auto sine = [] (int block, juce::AudioBuffer<float>& buf)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = 0.02f * std::sin (juce::MathConstants<float>::twoPi * 100.0f
                                                  * (float) (block * blockSize + i) / (float) sampleRate);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
        };

        const auto out = run (p, 20, sine);
        const float left = rms (out, 0, 4 * blockSize), right = rms (out, 1, 4 * blockSize);

        check (left > 0.0f && std::abs (left - right) / left < 0.05f,
               "EQ treats both channels equally (L rms " + juce::String (left, 4) + ", R rms " + juce::String (right, 4) + ")");
    }
    void testCompressor()
    {
        auto sine = [] (int block, juce::AudioBuffer<float>& buf)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                 * (float) (block * blockSize + i) / (float) sampleRate);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
        };

        auto level = [&] (bool on, float sustain, float mix, float levelDb)
        {
            Prepared prepared;
            auto& p = prepared.proc;
            makeTransparent (p);
            setParam (p, "COMP_ON", on ? 1.0f : 0.0f);
            setParam (p, "COMP_SUST", sustain);
            setParam (p, "COMP_ATT", 1.0f);
            setParam (p, "COMP_MIX", mix);
            setParam (p, "COMP_LVL", levelDb);
            return rms (run (p, 40, sine), 0, 20 * blockSize);
        };

        const float bypassed = level (false, 100.0f, 100.0f, 0.0f);
        const float squashed = level (true, 100.0f, 100.0f, 0.0f);
        check (squashed < bypassed * 0.5f,
               "compressor reduces level of a loud signal (" + juce::String (bypassed, 4) + " -> " + juce::String (squashed, 4) + ")");

        const float boosted = level (true, 100.0f, 100.0f, 12.0f);
        check (std::abs (boosted / squashed - 3.98f) < 0.4f,
               "COMP_LVL +12 dB raises output ~4x (ratio " + juce::String (boosted / squashed, 2) + ")");

        const float dryBlend = level (true, 100.0f, 0.0f, 0.0f);
        check (std::abs (dryBlend / bypassed - 1.0f) < 0.02f,
               "COMP_MIX 0 % leaves the dry signal untouched (ratio " + juce::String (dryBlend / bypassed, 3) + ")");

        const float halfBlend = level (true, 100.0f, 50.0f, 0.0f);
        check (halfBlend > squashed * 1.1f && halfBlend < bypassed * 0.95f,
               "COMP_MIX 50 % lies between compressed and dry");
    }

    //==========================================================================
    auto sineInput (float amplitude, float hz = 220.0f)
    {
        return [=] (int block, juce::AudioBuffer<float>& buf)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = amplitude * std::sin (juce::MathConstants<float>::twoPi * hz
                                                      * (float) (block * blockSize + i) / (float) sampleRate);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
        };
    }

    /** Output of the whole chain for a sine, after `setup` configured the processor. */
    template <typename Setup>
    juce::AudioBuffer<float> sineThrough (float amplitude, Setup setup, int numBlocks = 40)
    {
        Prepared prepared;
        makeTransparent (prepared.proc);
        setup (prepared.proc);
        return run (prepared.proc, numBlocks, sineInput (amplitude));
    }

    float differenceRms (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from)
    {
        juce::AudioBuffer<float> d (1, a.getNumSamples());
        for (int i = 0; i < a.getNumSamples(); ++i)
            d.setSample (0, i, a.getSample (0, i) - b.getSample (0, i));
        return rms (d, 0, from);
    }

    void addPedal (NeuralFlowAmpAudioProcessor& p, const juce::String& id, bool on = true)
    {
        setParam (p, id + "_ADDED", 1.0f);
        setParam (p, id + "_ON", on ? 1.0f : 0.0f);
    }

    void testAddOnPedals()
    {
        const int from = 20 * blockSize;
        const auto ref = sineThrough (0.05f, [] (auto&) {});

        // A pedal that is switched on but was never added to the chain must do nothing.
        {
            const auto out = sineThrough (0.05f, [] (auto& p)
            {
                setParam (p, "DELAY_ON", 1.0f);  setParam (p, "DELAY_MIX", 100.0f);
                setParam (p, "CHORUS_ON", 1.0f); setParam (p, "CHORUS_MIX", 100.0f);
                setParam (p, "REVERB_ON", 1.0f); setParam (p, "REVERB_MIX", 100.0f);
                setParam (p, "BOOST_ON", 1.0f);  setParam (p, "GATE_ON", 1.0f);
            });
            check (differenceRms (out, ref, from) < 1.0e-6f, "pedals that are not in the chain are ignored");
        }

        // An added pedal that is bypassed must do nothing either.
        {
            const auto out = sineThrough (0.05f, [] (auto& p)
            {
                setParam (p, "BOOST_GAIN", 24.0f);
                addPedal (p, "BOOST", false);
                setParam (p, "CHORUS_MIX", 100.0f);
                addPedal (p, "CHORUS", false);
            });
            check (differenceRms (out, ref, from) < 1.0e-6f, "bypassed pedals are ignored");
        }

        // Boost
        {
            const auto quiet = rms (sineThrough (0.01f, [] (auto&) {}), 0, from);
            const auto loud = rms (sineThrough (0.01f, [] (auto& p) { setParam (p, "BOOST_GAIN", 12.0f); addPedal (p, "BOOST"); }), 0, from);
            check (std::abs (loud / quiet - 3.98f) < 0.4f, "boost +12 dB raises level ~4x (ratio " + juce::String (loud / quiet, 2) + ")");
        }

        // Noise gate: mutes a signal far below the threshold, lets a loud one through.
        {
            auto gated = [] (auto& p) { setParam (p, "GATE_THRESH", -40.0f); addPedal (p, "GATE"); };

            const auto hissOff = rms (sineThrough (0.0005f, [] (auto&) {}), 0, from);
            const auto hissOn = rms (sineThrough (0.0005f, gated), 0, from);
            check (hissOn < hissOff * 0.2f, "gate mutes a signal below the threshold (" + juce::String (hissOff, 6) + " -> " + juce::String (hissOn, 6) + ")");

            const auto loudOff = rms (sineThrough (0.2f, [] (auto&) {}), 0, from);
            const auto loudOn = rms (sineThrough (0.2f, gated), 0, from);
            check (std::abs (loudOn / loudOff - 1.0f) < 0.1f, "gate passes a signal above the threshold (ratio " + juce::String (loudOn / loudOff, 3) + ")");
        }

        // Chorus changes the waveform
        {
            const auto out = sineThrough (0.05f, [] (auto& p) { setParam (p, "CHORUS_MIX", 100.0f); addPedal (p, "CHORUS"); });
            check (differenceRms (out, ref, from) > 0.2f * rms (ref, 0, from), "chorus modulates the signal");
        }

        // Delay: an echo of the right size at the right time
        {
            Prepared prepared;
            auto& p = prepared.proc;
            makeTransparent (p);
            setParam (p, "DELAY_TIME", 100.0f);
            setParam (p, "DELAY_FB", 0.0f);
            setParam (p, "DELAY_MIX", 50.0f);
            addPedal (p, "DELAY");

            run (p, 12, [] (int, juce::AudioBuffer<float>&) {});   // let the time smoothing settle

            const auto out = run (p, 20, [] (int block, juce::AudioBuffer<float>& buf)
            {
                if (block == 1)
                    for (int ch = 0; ch < 2; ++ch)
                        buf.setSample (ch, 10, 0.05f);
            });

            const int at = blockSize + 10, echoAt = at + 4800;
            float echo = 0.0f;
            for (int i = echoAt - 3; i <= echoAt + 3; ++i)
                echo = juce::jmax (echo, std::abs (out.getSample (0, i)));

            const float ratio = echo / std::abs (out.getSample (0, at));
            check (std::abs (ratio - 0.5f) < 0.1f, "delay: echo arrives after 100 ms at the mix level (ratio " + juce::String (ratio, 2) + ")");
        }

        // Reverb leaves a tail; with the mix at 0 it is transparent
        {
            auto tailEnergy = [&] (float mix)
            {
                Prepared prepared;
                auto& p = prepared.proc;
                makeTransparent (p);
                setParam (p, "REVERB_SIZE", 80.0f);
                setParam (p, "REVERB_MIX", mix);
                addPedal (p, "REVERB");

                const auto out = run (p, 20, [] (int block, juce::AudioBuffer<float>& buf)
                {
                    if (block == 1)
                        for (int ch = 0; ch < 2; ++ch)
                            buf.setSample (ch, 10, 0.05f);
                });

                double sum = 0.0;
                for (int i = 4000; i < out.getNumSamples(); ++i)
                    sum += std::abs (out.getSample (0, i));
                return (float) sum;
            };

            const float dry = tailEnergy (0.0f), wet = tailEnergy (100.0f);
            check (wet > 50.0f * juce::jmax (dry, 1.0e-6f), "reverb leaves a tail (" + juce::String (dry, 5) + " -> " + juce::String (wet, 3) + ")");

            const auto out = sineThrough (0.05f, [] (auto& p) { setParam (p, "REVERB_MIX", 0.0f); addPedal (p, "REVERB"); });
            check (std::abs (rms (out, 0, from) / rms (ref, 0, from) - 1.0f) < 0.03f, "reverb mix 0 % is transparent");
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testIrIsApplied();
    testStateRoundTrip();
    testEqProcessesBothChannels();
    testCompressor();
    testAddOnPedals();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
