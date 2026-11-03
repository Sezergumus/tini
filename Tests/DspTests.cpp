// Minimal headless checks for the audio engine. Exit code 0 = all passed.
#include "../Source/PluginProcessor.h"
#include "NAM/get_dsp.h"

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

    //==========================================================================
    void testChainOrder()
    {
        const int from = 20 * blockSize;
        auto order = PedalCatalog::defaultOrder();

        check (PedalCatalog::isValidOrder (order), "the default chain order is a valid permutation");
        check (order[4] == PedalCatalog::ampItem, "the amp starts between the pre-amp and post-amp pedals");

        // Same pedals, different order: a boost ahead of the overdrive gets squashed by it, a boost
        // behind the overdrive is plain gain.
        auto boostThenOverdrive = [] (auto& p)
        {
            setParam (p, "OD_ON", 1.0f);
            addPedal (p, "BOOST");
            setParam (p, "BOOST_GAIN", 24.0f);
        };

        const auto before = sineThrough (0.05f, boostThenOverdrive);

        auto swapped = order;
        std::swap (swapped[2], swapped[3]);   // boost <-> overdrive
        const auto after = sineThrough (0.05f, [&] (auto& p) { boostThenOverdrive (p); p.setChainOrder (swapped); });

        const float ratio = rms (after, 0, from) / rms (before, 0, from);
        check (ratio > 1.1f, "moving the boost behind the overdrive changes the sound (level x" + juce::String (ratio, 2) + ")");

        // Garbage orders are rejected and leave the chain as it was.
        {
            Prepared p;
            auto bad = order;
            bad[0] = bad[1];
            p.proc.setChainOrder (bad);
            check (p.proc.getChainOrder() == order, "an invalid chain order is ignored");
        }

        // The order is part of the saved state.
        {
            juce::MemoryBlock saved;
            {
                Prepared a;
                a.proc.setChainOrder (swapped);
                a.proc.getStateInformation (saved);
            }
            Prepared b;
            b.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            check (b.proc.getChainOrder() == swapped, "state restores the chain order");
        }
    }

    //==========================================================================
    void testNamCoreIsLinked()
    {
        std::printf ("\n-- NAM core --\n");

        bool threw = false;
        try
        {
            auto model = nam::get_dsp (std::filesystem::path ("this_model_does_not_exist.nam"));
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check (threw, "the NAM core is linked and refuses a missing model file");
    }

    //==========================================================================
    juce::File testModel()
    {
        return juce::File (__FILE__).getParentDirectory().getChildFile ("models").getChildFile ("test_wavenet.nam");
    }

    /** Loads happen on a background thread; the audio thread picks the model up in its next block. */
    bool waitForNam (NeuralFlowAmpAudioProcessor& p)
    {
        for (int i = 0; i < 500; ++i)
        {
            const auto state = p.nam.getStatus().state;
            if (state == NamEngine::Status::State::ready || state == NamEngine::Status::State::failed)
                break;
            juce::Thread::sleep (10);
        }
        run (p, 2, [] (int, juce::AudioBuffer<float>&) {});
        return p.nam.getStatus().state == NamEngine::Status::State::ready;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    void testNamModel()
    {
        std::printf ("\n-- NAM model --\n");
        const int from = 20 * blockSize;
        const auto model = testModel();
        check (model.existsAsFile(), "the test model file is there");

        const auto builtIn = sineThrough (0.1f, [] (auto&) {});

        // Picking NAM without a model keeps the built-in amp.
        {
            const auto out = sineThrough (0.1f, [] (auto& p) { setParam (p, "AMP_TYPE", 1.0f); });
            check (differenceRms (out, builtIn, from) < 1.0e-6f, "AMP_TYPE = NAM without a model falls back to the built-in amp");
        }

        // With a model the sound changes and stays sane.
        {
            bool loaded = false;
            const auto namOut = sineThrough (0.1f, [&] (auto& p)
            {
                setParam (p, "AMP_TYPE", 1.0f);
                p.loadNamModel (model);
                loaded = waitForNam (p);

                const auto info = p.nam.getModelInfo();
                check (info.name == "test_wavenet" && info.hasLoudness && info.trainingRate > 0.0,
                       "model info is read: " + info.name + ", " + juce::String (info.trainingRate) + " Hz, "
                           + juce::String (info.loudnessDb, 1) + " dB");
                check (! info.resampled, "no resampling at the training rate");
            });
            check (loaded, "the model loads in the background");
            check (allFinite (namOut) && rms (namOut, 0, from) > 1.0e-4f, "the model produces a finite, audible signal");
            check (differenceRms (namOut, builtIn, from) > 0.1f * rms (builtIn, 0, from), "the model sounds different from the built-in amp");
        }

        // The output trim and normalisation work.
        {
            auto withOut = [&] (float db, bool normalise)
            {
                return rms (sineThrough (0.1f, [&] (auto& p)
                {
                    setParam (p, "AMP_TYPE", 1.0f);
                    setParam (p, "NAM_OUT", db);
                    setParam (p, "NAM_NORM", normalise ? 1.0f : 0.0f);
                    p.loadNamModel (model);
                    waitForNam (p);
                }), 0, from);
            };

            const float plain = withOut (0.0f, false), louder = withOut (6.0f, false);
            check (std::abs (louder / plain - 2.0f) < 0.1f, "NAM output +6 dB doubles the level (ratio " + juce::String (louder / plain, 2) + ")");

            const float normalised = withOut (0.0f, true);
            const float expected = juce::Decibels::decibelsToGain ((float) (NamEngine::targetLoudnessDb + 20.020729));
            check (std::abs (normalised / plain - expected) < 0.05f * expected, "normalising levels the model to -18 dB loudness");
        }

        // A broken file reports an error and does not disturb the sound.
        {
            auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nfa_broken.nam");
            bad.replaceWithText ("this is not a model");

            Prepared prepared;
            auto& p = prepared.proc;
            setParam (p, "AMP_TYPE", 1.0f);
            p.loadNamModel (bad);
            const bool ready = waitForNam (p);
            check (! ready && p.nam.getStatus().state == NamEngine::Status::State::failed && p.nam.getStatus().message.isNotEmpty(),
                   "a broken model file is reported as failed: " + p.nam.getStatus().message);
            check (! p.nam.hasModel(), "...and no model is active");
            bad.deleteFile();
        }

        // Unloading goes back to the built-in amp.
        {
            const auto out = sineThrough (0.1f, [&] (auto& p)
            {
                setParam (p, "AMP_TYPE", 1.0f);
                p.loadNamModel (model);
                waitForNam (p);
                p.unloadNamModel();
                run (p, 2, [] (int, juce::AudioBuffer<float>&) {});
            });
            // (a tiny difference is left over from the model's last blocks decaying in the filters)
            check (differenceRms (out, builtIn, from) < 0.002f * rms (builtIn, 0, from), "unloading the model returns to the built-in amp");
        }

        // Another host rate: the model is resampled, blocks larger than prepared are fine.
        {
            NeuralFlowAmpAudioProcessor p;
            p.setRateAndBufferSizeDetails (44100.0, 256);
            p.prepareToPlay (44100.0, 256);
            setParam (p, "AMP_TYPE", 1.0f);
            p.loadNamModel (model);
            const bool loaded = waitForNam (p);

            check (loaded && p.nam.getModelInfo().resampled && p.nam.getLatencySamples() > 0, "at 44.1 kHz the model is resampled");

            juce::AudioBuffer<float> big (2, 1000);   // larger than the 256 samples prepared
            juce::MidiBuffer midi;
            bool finite = true;
            for (int b = 0; b < 20; ++b)
            {
                for (int i = 0; i < big.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        big.setSample (ch, i, 0.1f * std::sin (0.03f * (float) (b * 1000 + i)));
                p.processBlock (big, midi);
                finite = finite && allFinite (big);
            }
            check (finite && big.getMagnitude (0, 1000) > 1.0e-4f, "resampled output is finite and audible, also for oversized blocks");
        }

        // A change of host rate reloads the model for the new rate.
        {
            NeuralFlowAmpAudioProcessor p;
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            setParam (p, "AMP_TYPE", 1.0f);
            p.loadNamModel (model);
            waitForNam (p);
            check (! p.nam.getModelInfo().resampled, "model prepared at 48 kHz is not resampled");

            p.setRateAndBufferSizeDetails (96000.0, 512);
            p.prepareToPlay (96000.0, 512);
            check (waitForNam (p) && p.nam.getModelInfo().resampled, "preparing at 96 kHz reloads the model with resampling");
        }

        // The model is part of the saved state and of user presets.
        {
            juce::MemoryBlock saved;
            {
                Prepared a;
                a.proc.loadNamModel (model);
                waitForNam (a.proc);
                a.proc.getStateInformation (saved);
            }
            Prepared b;
            b.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            check (waitForNam (b.proc) && b.proc.nam.getFile() == model, "state restores the NAM model");
        }
        {
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nfa_nam_preset_test");
            folder.deleteRecursively();

            Prepared a;
            a.proc.presets.setUserFolder (folder);
            a.proc.loadNamModel (model);
            waitForNam (a.proc);
            const int index = a.proc.saveCurrentAsPreset ("With Model");

            Prepared b;
            b.proc.presets.setUserFolder (folder);
            const int found = b.proc.presets.indexOf ("With Model");
            check (index >= 0 && found >= 0 && b.proc.presets.get (found).namPath == model.getFullPathName(), "a user preset stores the NAM model path");

            b.proc.loadPreset (found);
            check (waitForNam (b.proc) && b.proc.nam.getFile() == model, "loading that preset loads the model");
            folder.deleteRecursively();
        }
    }

    //==========================================================================
    void testNamA2()
    {
        std::printf ("\n-- NAM A2 --\n");
        const int from = 20 * blockSize;
        const auto a2 = juce::File (__FILE__).getParentDirectory().getChildFile ("models").getChildFile ("test_a2.nam");
        check (a2.existsAsFile(), "the A2 test model file is there");

        // An A2 file (a slimmable container with a Lite and a Full network) loads and sounds sane.
        juce::AudioBuffer<float> out;
        {
            bool loaded = false;
            out = sineThrough (0.1f, [&] (auto& p)
            {
                setParam (p, "AMP_TYPE", 1.0f);
                p.loadNamModel (a2);
                loaded = waitForNam (p);

                const auto info = p.nam.getModelInfo();
                check (info.trainingRate == 48000.0 && info.hasLoudness && ! info.resampled,
                       "A2 model info is read (" + info.name + ", " + juce::String (info.loudnessDb, 1) + " dB)");
            });
            check (loaded, "an A2 model loads");
            check (allFinite (out) && rms (out, 0, from) > 1.0e-4f, "an A2 model produces a finite, audible signal");
        }

        // A2 runs at another host rate too.
        {
            NeuralFlowAmpAudioProcessor p;
            p.setRateAndBufferSizeDetails (44100.0, 512);
            p.prepareToPlay (44100.0, 512);
            setParam (p, "AMP_TYPE", 1.0f);
            p.loadNamModel (a2);
            check (waitForNam (p) && p.nam.getModelInfo().resampled, "an A2 model is resampled at 44.1 kHz");
        }

        // How much of the real-time budget a block takes. Machine dependent: only a loose limit is
        // enforced, the numbers are printed.
        {
            auto budgetUsed = [&] (const juce::File& model)
            {
                Prepared prepared;
                auto& p = prepared.proc;
                setParam (p, "AMP_TYPE", 1.0f);
                p.loadNamModel (model);
                waitForNam (p);

                const int blocks = 300;
                run (p, 20, sineInput (0.1f));   // warm up
                const double start = juce::Time::getMillisecondCounterHiRes();
                run (p, blocks, sineInput (0.1f));
                const double ms = juce::Time::getMillisecondCounterHiRes() - start;
                return ms / (1000.0 * blocks * blockSize / sampleRate);
            };

            const double a2Used = budgetUsed (a2), a1Used = budgetUsed (testModel());
            std::printf ("    whole plugin, NAM amp, 512 samples @ 48 kHz: A2 %.1f %%, small A1 example %.1f %% of real time\n",
                         a2Used * 100.0, a1Used * 100.0);
            check (a2Used < 0.5, "an A2 model uses less than half of the real-time budget");
        }
    }

    //==========================================================================
    void testPedalCopies()
    {
        std::printf ("\n-- pedal copies --\n");
        const int from = 20 * blockSize;

        // Copy 0 keeps the old parameter ids; permanent pedals only have switches on later copies.
        {
            Prepared p;
            check (p.proc.apvts.getParameter ("COMP_ADDED") == nullptr && p.proc.apvts.getParameter ("COMP_ADDED_2") != nullptr,
                   "the compressor's first copy is permanent, later copies can be added");
            check (p.proc.apvts.getParameter ("DELAY_TIME") != nullptr && p.proc.apvts.getParameter ("DELAY_TIME_3") != nullptr,
                   "copy 0 keeps the plain parameter ids, copies get a suffix");
            check (PedalCatalog::addedId (PedalCatalog::makeItem (1, 0)).isEmpty()
                       && PedalCatalog::addedId (PedalCatalog::makeItem (1, 1)) == "COMP_ADDED_2"
                       && PedalCatalog::addedId (PedalCatalog::makeItem (5, 0)) == "DELAY_ADDED",
                   "added ids are derived consistently");
        }

        // Two boosts in a row stack their gain.
        {
            auto boosts = [] (int n)
            {
                return [n] (auto& p)
                {
                    for (int copy = 0; copy < n; ++copy)
                    {
                        const auto sfx = copy == 0 ? juce::String() : "_" + juce::String (copy + 1);
                        setParam (p, "BOOST_GAIN" + sfx, 12.0f);
                        setParam (p, "BOOST_ADDED" + sfx, 1.0f);
                        setParam (p, "BOOST_ON" + sfx, 1.0f);
                    }
                };
            };

            const auto one = rms (sineThrough (0.01f, boosts (1)), 0, from);
            const auto two = rms (sineThrough (0.01f, boosts (2)), 0, from);
            check (std::abs (two / one - 3.98f) < 0.5f, "a second boost copy adds its gain (ratio " + juce::String (two / one, 2) + ")");
        }

        // A copy that is parked (not added) or bypassed does nothing, even with extreme settings.
        {
            const auto ref = sineThrough (0.05f, [] (auto&) {});
            const auto out = sineThrough (0.05f, [] (auto& p)
            {
                setParam (p, "BOOST_GAIN_2", 24.0f);
                setParam (p, "BOOST_ON_2", 1.0f);
                setParam (p, "DELAY_MIX_3", 100.0f);
                setParam (p, "DELAY_ADDED_3", 1.0f);
                setParam (p, "DELAY_ON_3", 0.0f);
            });
            check (differenceRms (out, ref, from) < 1.0e-6f, "copies that are not in the chain or are bypassed are ignored");
        }

        // The chain order round-trips through its text form and rejects garbage.
        {
            auto order = PedalCatalog::defaultOrder();
            std::swap (order[0], order[9]);
            PedalCatalog::ChainOrder parsed {};
            check (PedalCatalog::orderFromString (PedalCatalog::orderToString (order), parsed) && parsed == order,
                   "chain order survives orderToString / orderFromString");
            check (! PedalCatalog::orderFromString ("1,2,3", parsed) && parsed == order, "a chain order of the wrong size is rejected");
        }

        // Version 1 sessions and presets stored 8 items packed into an integer: they must still load.
        {
            // old default: gate comp boost od AMP(7) chorus delay reverb
            const int legacyItems[] = { 0, 1, 2, 3, 7, 4, 5, 6 };
            juce::uint32 packed = 0;
            for (int i = 0; i < 8; ++i)
                packed |= (juce::uint32) legacyItems[i] << (4 * i);

            PedalCatalog::ChainOrder converted {};
            check (PedalCatalog::orderFromLegacy (packed, converted) && converted == PedalCatalog::defaultOrder(),
                   "a version 1 chain order converts to the new default order");

            // an old session
            Prepared a;
            juce::MemoryBlock current;
            a.proc.getStateInformation (current);
            auto xml = juce::AudioProcessor::getXmlFromBinary (current.getData(), (int) current.getSize());
            auto state = juce::ValueTree::fromXml (*xml);
            state.removeProperty ("chain", nullptr);
            state.setProperty ("chainOrder", (int) packed, nullptr);

            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*state.createXml(), old);

            Prepared b;
            auto swapped = PedalCatalog::defaultOrder();
            std::swap (swapped[0], swapped[1]);
            b.proc.setChainOrder (swapped);
            b.proc.setStateInformation (old.getData(), (int) old.getSize());
            check (b.proc.getChainOrder() == PedalCatalog::defaultOrder(), "a version 1 session restores its chain order");

            // an old preset file
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nfa_legacy_preset_test");
            folder.deleteRecursively();
            folder.createDirectory();

            juce::XmlElement preset ("NeuralFlowPreset");
            preset.setAttribute ("version", 1);
            preset.setAttribute ("name", "Old Preset");
            preset.setAttribute ("chainOrder", (int) packed);
            check (preset.writeTo (folder.getChildFile ("Old Preset" + juce::String (PresetManager::fileExtension))), "wrote a version 1 preset file");

            PresetManager manager (folder);
            const int index = manager.indexOf ("Old Preset");
            check (index >= 0 && manager.get (index).order == PedalCatalog::defaultOrder(), "a version 1 preset file keeps its chain order");
            folder.deleteRecursively();
        }
    }

    //==========================================================================
    void testPresets()
    {
        std::printf ("\n-- presets --\n");

        auto tempFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nfa_preset_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));

        {
            Prepared a;
            auto& p = a.proc;
            p.presets.setUserFolder (tempFolder);

            const int numFactory = p.presets.getNumFactory();
            check (numFactory >= 5, "there are factory presets (" + juce::String (numFactory) + ")");
            check (p.getNumPrograms() == numFactory, "the host sees the factory presets as programs");

            // every factory preset only names real parameters, in range, with a valid chain order
            bool idsOk = true, orderOk = true, namesOk = true;
            for (int i = 0; i < numFactory; ++i)
            {
                const auto& preset = p.presets.get (i);
                orderOk = orderOk && PedalCatalog::isValidOrder (preset.order);
                namesOk = namesOk && preset.name.isNotEmpty() && p.presets.indexOf (preset.name) == i;
                for (const auto& [id, v] : preset.values)
                {
                    auto* r = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id));
                    idsOk = idsOk && r != nullptr && v >= r->getNormalisableRange().start && v <= r->getNormalisableRange().end;
                }
            }
            check (idsOk, "factory presets only use existing parameters inside their range");
            check (orderOk, "factory presets have valid chain orders");
            check (namesOk, "factory preset names are unique");

            // every factory preset makes sound without blowing up
            bool allFinite = true, allAudible = true;
            for (int i = 0; i < numFactory; ++i)
            {
                p.loadPreset (i);
                auto out = run (p, 40, [] (int b, juce::AudioBuffer<float>& buf)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int n = 0; n < buf.getNumSamples(); ++n)
                            buf.setSample (ch, n, 0.2f * std::sin (juce::MathConstants<float>::twoPi * 196.0f * (float) (b * blockSize + n) / (float) sampleRate));
                });
                for (int n = 0; n < out.getNumSamples(); ++n)
                    allFinite = allFinite && std::isfinite (out.getSample (0, n)) && std::abs (out.getSample (0, n)) < 8.0f;
                allAudible = allAudible && out.getMagnitude (0, 0, out.getNumSamples()) > 0.01f;
            }
            check (allFinite, "every factory preset produces finite, bounded audio");
            check (allAudible, "every factory preset is audible");

            // loading applies parameters, which pedals are in the chain, and their order
            const int hg = p.presets.indexOf ("High Gain Rhythm");
            check (hg >= 0 && p.loadPreset (hg), "a factory preset loads");
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 85.0f) < 0.01f, "preset sets the amp drive");
            check (p.apvts.getRawParameterValue ("GATE_ADDED")->load() > 0.5f, "preset puts its pedals in the chain");
            check (p.apvts.getRawParameterValue ("CHORUS_ADDED")->load() < 0.5f, "preset leaves the other pedals out");
            check (p.getCurrentPresetIndex() == hg && ! p.isPresetModified(), "the loaded preset is current and unmodified");

            // changing a knob marks it as edited; loading another preset replaces everything
            setParam (p, "DRIVE", 20.0f);
            check (p.isPresetModified(), "a tweak marks the preset as edited");
            auto swappedOrder = p.getChainOrder();
            std::swap (swappedOrder[0], swappedOrder[1]);
            p.setChainOrder (swappedOrder);
            p.loadPreset (p.presets.indexOf ("Default"));
            check (p.getChainOrder() == PedalCatalog::defaultOrder(), "loading Default restores the chain order");
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 50.0f) < 0.01f
                       && p.apvts.getRawParameterValue ("GATE_ADDED")->load() < 0.5f,
                   "loading Default resets the settings and removes extra pedals");

            // name rules
            check (p.presets.validateName ("").failed(), "an empty name is rejected");
            check (p.presets.validateName ("  ").failed(), "a blank name is rejected");
            check (p.presets.validateName ("classic LEAD").failed(), "a factory name cannot be reused");
            check (p.presets.validateName ("a/b").failed(), "a name with path characters is rejected");
            check (p.presets.validateName (juce::String::repeatedString ("x", 60)).failed(), "an over-long name is rejected");
            check (p.saveCurrentAsPreset ("Default") < 0, "saving over a factory preset is refused");

            // save, edit, reload
            p.loadPreset (hg);
            setParam (p, "DRIVE", 33.0f);
            setParam (p, "REVERB_ADDED", 1.0f);
            setParam (p, "REVERB_MIX", 61.0f);
            auto order = p.getChainOrder();
            std::swap (order[0], order[2]);
            p.setChainOrder (order);

            const int saved = p.saveCurrentAsPreset ("  My Test Tone ");
            check (saved >= numFactory, "saving creates a user preset");
            check (saved >= 0 && p.presets.get (saved).name == "My Test Tone" && ! p.presets.get (saved).factory,
                   "the name is trimmed and the preset is a user preset");
            check (p.getCurrentPresetIndex() == saved && ! p.isPresetModified(), "after saving, the new preset is current and unmodified");
            check (tempFolder.getNumberOfChildFiles (juce::File::findFiles, "*.nfpreset") == 1, "a preset file was written");

            p.loadPreset (p.presets.indexOf ("Default"));
            check (p.loadPreset (saved), "a user preset loads");
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 33.0f) < 0.01f
                       && std::abs (p.apvts.getRawParameterValue ("REVERB_MIX")->load() - 61.0f) < 0.01f
                       && p.apvts.getRawParameterValue ("REVERB_ADDED")->load() > 0.5f,
                   "loading a user preset restores the saved knobs and pedals");
            check (p.getChainOrder() == order, "loading a user preset restores the chain order");

            // saving under the same name replaces, it does not add
            setParam (p, "DRIVE", 44.0f);
            check (p.presets.wouldOverwrite ("my test tone"), "an existing user name is reported as an overwrite");
            const int again = p.saveCurrentAsPreset ("My Test Tone");
            check (again == saved && p.presets.size() == numFactory + 1, "saving under the same name replaces the preset");
            p.loadPreset (p.presets.indexOf ("Default"));
            p.loadPreset (again);
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 44.0f) < 0.01f, "the replaced preset holds the new values");

            // user presets are listed after the factory ones, alphabetically
            p.saveCurrentAsPreset ("Aardvark");
            check (p.presets.get (numFactory).name == "Aardvark" && p.presets.get (numFactory + 1).name == "My Test Tone",
                   "user presets are sorted after the factory presets");

            // the preset the session was based on survives a save / restore of the plugin state
            p.loadPreset (p.presets.indexOf ("My Test Tone"));
            juce::MemoryBlock state;
            p.getStateInformation (state);

            Prepared b;
            b.proc.presets.setUserFolder (tempFolder);
            b.proc.setStateInformation (state.getData(), (int) state.getSize());
            check (b.proc.getCurrentPresetIndex() == b.proc.presets.indexOf ("My Test Tone") && ! b.proc.isPresetModified(),
                   "the session remembers which preset it is on");

            // a host echoing the current program must not wipe the restored settings
            setParam (b.proc, "DRIVE", 12.0f);
            b.proc.setCurrentProgram (b.proc.getCurrentProgram());
            check (std::abs (b.proc.apvts.getRawParameterValue ("DRIVE")->load() - 12.0f) < 0.01f,
                   "re-selecting the current host program does not reload it");

            const int other = (b.proc.getCurrentProgram() + 1) % numFactory;
            b.proc.setCurrentProgram (other);
            check (b.proc.getCurrentProgram() == other && b.proc.getCurrentPresetIndex() == other,
                   "the host can switch programs");
        }

        // a fresh manager finds the saved files
        {
            PresetManager fresh (tempFolder);
            check (fresh.size() == fresh.getNumFactory() + 2 && fresh.indexOf ("My Test Tone") >= 0,
                   "user presets are found again from disk");
        }

        tempFolder.deleteRecursively();
    }

    void testPresetEditing()
    {
        std::printf ("\n-- renaming and deleting presets --\n");

        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nfa_preset_edit_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));

        {
            Prepared a;
            auto& p = a.proc;
            p.presets.setUserFolder (folder);
            const int numFactory = p.presets.getNumFactory();

            setParam (p, "DRIVE", 31.0f);
            p.saveCurrentAsPreset ("Alpha");
            setParam (p, "DRIVE", 62.0f);
            p.saveCurrentAsPreset ("Bravo");
            check (p.presets.size() == numFactory + 2, "two user presets exist");

            // rename
            const int bravo = p.presets.indexOf ("Bravo");
            check (p.getCurrentPresetIndex() == bravo, "the last saved preset is current");
            check (p.renamePreset (bravo, "  Zulu  ").wasOk(), "a user preset can be renamed");
            check (p.presets.indexOf ("Bravo") < 0 && p.presets.indexOf ("Zulu") >= numFactory, "the old name is gone, the new one is listed");
            check (p.getCurrentPresetIndex() == p.presets.indexOf ("Zulu"), "renaming the current preset keeps it current");
            check (! folder.getChildFile ("Bravo.nfpreset").exists() && folder.getChildFile ("Zulu.nfpreset").existsAsFile(),
                   "the file follows the name");

            p.loadPreset (p.presets.indexOf ("Alpha"));
            p.loadPreset (p.presets.indexOf ("Zulu"));
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - 62.0f) < 0.01f, "a renamed preset keeps its settings");

            check (p.renamePreset (p.presets.indexOf ("Zulu"), "alpha").failed(), "renaming onto another user preset is refused");
            check (p.renamePreset (p.presets.indexOf ("Zulu"), "Default").failed(), "renaming to a factory name is refused");
            check (p.renamePreset (p.presets.indexOf ("Zulu"), "").failed(), "renaming to nothing is refused");
            check (p.renamePreset (0, "Mine").failed(), "factory presets cannot be renamed");
            check (p.renamePreset (p.presets.indexOf ("Zulu"), "ZULU").wasOk() && p.presets.indexOf ("ZULU") >= 0,
                   "changing only the capitals works");

            // delete
            check (! p.deletePreset (0), "factory presets cannot be deleted");
            const int alpha = p.presets.indexOf ("Alpha");
            check (p.deletePreset (alpha), "a user preset can be deleted");
            check (p.presets.indexOf ("Alpha") < 0 && p.presets.size() == numFactory + 1, "the deleted preset is gone from the list");
            check (folder.getNumberOfChildFiles (juce::File::findFiles, "*.nfpreset") == 1, "...and from disk");
            check (p.getCurrentPresetIndex() >= 0, "deleting another preset leaves the current one alone");

            const float driveBefore = p.apvts.getRawParameterValue ("DRIVE")->load();
            check (p.deletePreset (p.getCurrentPresetIndex()), "the current preset can be deleted too");
            check (p.getCurrentPresetIndex() == -1 && ! p.isPresetModified(), "the setup becomes untitled");
            check (std::abs (p.apvts.getRawParameterValue ("DRIVE")->load() - driveBefore) < 0.01f, "...and keeps its settings");
        }

        folder.deleteRecursively();
    }
}

void renderUiSnapshots (const juce::File& dir);   // Tests/UiSnapshots.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String (argv[1]) == "--snapshots")
    {
        renderUiSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        return 0;
    }

    testIrIsApplied();
    testStateRoundTrip();
    testEqProcessesBothChannels();
    testCompressor();
    testAddOnPedals();
    testChainOrder();
    testNamCoreIsLinked();
    testNamModel();
    testNamA2();
    testPedalCopies();
    testPresets();
    testPresetEditing();

    std::printf ("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
