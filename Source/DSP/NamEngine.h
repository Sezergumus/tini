#pragma once

#include <JuceHeader.h>
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

/** Runs a Neural Amp Modeler (.nam) model inside the audio chain.

    - Models are loaded on a background thread (parsing + prewarming can take a while) and handed
      to the audio thread without locks. The audio thread never allocates or frees a model: a
      replaced model is parked in a slot and deleted by the loading thread later.
    - NAM models are mono. Stereo input is averaged, the model output is copied to every channel.
    - Models are usually trained at 48 kHz. At another host rate the block is resampled to the
      model's rate and back (Lagrange interpolation, a few samples of latency), unless the model
      says it can run at any rate itself.

    Message thread: prepare(), loadModel(), unload(), getStatus(), getModelInfo().
    Audio thread:   process().                                                              */
class NamEngine
{
public:
    struct ModelInfo
    {
        juce::String name;                 // file name without extension
        double trainingRate = -1.0;        // Hz, -1 if the file does not say
        bool hasLoudness = false;
        double loudnessDb = 0.0;
        bool resampled = false;            // host rate differs from the training rate
    };

    struct Status
    {
        enum class State { empty, loading, ready, failed };
        State state = State::empty;
        juce::String message;              // error text when failed
    };

    /** Level every model is normalised to when "normalise" is on (what the NAM plugin uses). */
    static constexpr double targetLoudnessDb = -18.0;

    NamEngine() = default;

    ~NamEngine()
    {
        ++generation;                      // pending loads must not publish any more
        loader.removeAllJobs (true, 5000);
        collectGarbage();

        delete pending.exchange (nullptr);
        delete current;
    }

    //==========================================================================
    /** Sets the host's rate and the largest block it will send. Reloads the current model, because
        it is prepared for one specific rate. */
    void prepare (double sampleRate, int maxBlockSize)
    {
        const bool changed = sampleRate != hostRate.load() || maxBlockSize != maxBlock.load();
        hostRate = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        if (changed && currentFile.existsAsFile())
            loadModel (currentFile);
    }

    /** Starts loading a model in the background. The previous model keeps playing until the new one is ready.
        Models that hold several sizes in one file (A2: Lite + Full) always run at their full size. */
    void loadModel (const juce::File& file)
    {
        const int gen = ++generation;
        currentFile = file;
        setStatus ({ Status::State::loading, {} });

        const double rate = hostRate.load();
        const int block = maxBlock.load();

        loader.addJob ([this, file, gen, rate, block]
        {
            std::unique_ptr<Model> model;
            juce::String error;

            try
            {
                model = Model::load (file, rate, block);
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            catch (...)
            {
                error = "Unknown error";
            }

            if (gen != generation.load())
                return;                    // a newer request replaced this one

            if (model == nullptr)
            {
                setStatus ({ Status::State::failed, error.isNotEmpty() ? error : "Could not read the model" });
                return;
            }

            publish (std::move (model));
            setStatus ({ Status::State::ready, {} });
        });
    }

    /** Removes the model (the amp falls back to the built-in sound). */
    void unload()
    {
        ++generation;
        currentFile = juce::File();
        publish (std::make_unique<Model>());   // an empty model
        setStatus ({ Status::State::empty, {} });
    }

    juce::File getFile() const { return currentFile; }

    Status getStatus() const
    {
        const juce::ScopedLock lock (infoLock);
        return status;
    }

    ModelInfo getModelInfo() const
    {
        const juce::ScopedLock lock (infoLock);
        return info;
    }

    /** True once a model has been handed to the audio thread and until it is removed. */
    bool hasModel() const { return modelReady.load (std::memory_order_acquire); }

    /** Extra latency of the sample rate conversion, in host samples (0 when the rates match). */
    int getLatencySamples() const { return latency.load(); }

    //==========================================================================
    /** Audio thread. Replaces the buffer's content with the model's output. Returns false (and
        leaves the buffer alone) if there is no usable model. `inputGainDb` is applied before the model,
        `outputGainDb` after it; with `normalise` the model's own loudness is levelled to
        targetLoudnessDb first. */
    bool process (juce::AudioBuffer<float>& buffer, float inputGainDb, float outputGainDb, bool normalise)
    {
        if (auto* next = pending.exchange (nullptr))
        {
            if (current != nullptr)
                retire (current);

            current = next;
        }

        // No model, or one that was prepared for another host rate and is being reloaded.
        if (current == nullptr || current->dsp == nullptr || std::abs (current->hostRate - hostRate.load()) > 0.5)
            return false;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return true;

        const float inGain = juce::Decibels::decibelsToGain (inputGainDb);
        float outGain = juce::Decibels::decibelsToGain (outputGainDb);
        if (normalise && current->info.hasLoudness)
            outGain *= juce::Decibels::decibelsToGain ((float) (targetLoudnessDb - current->info.loudnessDb));

        auto* mono = current->hostIn.data();
        const int limit = (int) current->hostIn.size();

        for (int start = 0; start < numSamples; start += limit)
        {
            const int n = juce::jmin (limit, numSamples - start);

            // stereo is averaged to mono
            juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0) + start, n);
            if (numChannels > 1)
            {
                juce::FloatVectorOperations::add (mono, buffer.getReadPointer (1) + start, n);
                juce::FloatVectorOperations::multiply (mono, 0.5f, n);
            }

            juce::FloatVectorOperations::multiply (mono, inGain, n);
            current->run (mono, n);
            juce::FloatVectorOperations::multiply (mono, outGain, n);

            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (buffer.getWritePointer (ch) + start, mono, n);
        }

        return true;
    }

private:
    //==========================================================================
    /** One loaded model plus everything the audio thread needs to run it. */
    struct Model
    {
        std::unique_ptr<nam::DSP> dsp;
        ModelInfo info;
        bool resample = false;
        double hostRate = 48000.0;
        double modelRate = 48000.0;
        int modelMaxBlock = 0;

        std::vector<float> hostIn, modelIn, modelOut;
        juce::LagrangeInterpolator down, up;
        Model* nextRetired = nullptr;          // intrusive list of models waiting to be deleted

        static std::unique_ptr<Model> load (const juce::File& file, double hostRate, int maxBlock)
        {
            auto model = std::make_unique<Model>();
            model->hostRate = hostRate;

           #if JUCE_WINDOWS
            model->dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toWideCharPointer()));
           #else
            model->dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
           #endif

            if (model->dsp == nullptr)
                return nullptr;

            auto& dsp = *model->dsp;
            model->info.name = file.getFileNameWithoutExtension();
            model->info.trainingRate = dsp.GetExpectedSampleRate();
            model->info.hasLoudness = dsp.HasLoudness();
            model->info.loudnessDb = model->info.hasLoudness ? dsp.GetLoudness() : 0.0;

            // A2 files hold a Lite and a Full network. Full is the container's default; asking for it
            // explicitly (before Reset(), which prepares the active size) keeps that true for other
            // slimmable models as well.
            if (auto* slim = dynamic_cast<nam::SlimmableModel*> (&dsp))
                slim->SetSlimmableSize (1.0);

            const bool knowsRate = model->info.trainingRate > 0.0;
            model->resample = knowsRate && ! dsp.SupportsArbitrarySampleRate()
                                  && std::abs (hostRate - model->info.trainingRate) > 1.0;
            model->info.resampled = model->resample;
            model->modelRate = model->resample ? model->info.trainingRate : hostRate;

            const double ratio = model->modelRate / hostRate;
            model->modelMaxBlock = (int) std::ceil ((double) maxBlock * ratio) + 8;

            model->hostIn.assign ((size_t) maxBlock, 0.0f);
            model->modelIn.assign ((size_t) model->modelMaxBlock, 0.0f);
            model->modelOut.assign ((size_t) model->modelMaxBlock, 0.0f);

            // Reset allocates and prewarms the network: fine here, we are not on the audio thread.
            dsp.Reset (model->modelRate, model->modelMaxBlock);
            return model;
        }

        /** Runs the model on `samples` (host rate, in place). Handles blocks larger than prepared. */
        void run (float* samples, int numSamples)
        {
            const int limit = (int) hostIn.size();

            for (int done = 0; done < numSamples; done += limit)
                runChunk (samples + done, juce::jmin (limit, numSamples - done));
        }

        void runChunk (float* samples, int n)
        {
            if (! resample)
            {
                float* in[] = { samples };
                float* out[] = { samples };
                dsp->process (in, out, n);
                return;
            }

            // n host samples -> m model samples -> n host samples. The ratios are exact for this
            // block, so the stream never drifts.
            const int m = juce::jlimit (1, modelMaxBlock, juce::roundToInt ((double) n * modelRate / hostRate));
            down.process ((double) n / (double) m, samples, modelIn.data(), m);

            float* in[] = { modelIn.data() };
            float* out[] = { modelOut.data() };
            dsp->process (in, out, m);

            up.process ((double) m / (double) n, modelOut.data(), samples, n);
        }
    };

    //==========================================================================
    /** Hands a model to the audio thread. The loader thread (or the message thread for unload)
        is the only caller, serialised by `publishLock`. */
    void publish (std::unique_ptr<Model> model)
    {
        const juce::ScopedLock lock (publishLock);

        collectGarbage();

        {
            const juce::ScopedLock infoGuard (infoLock);
            info = model->info;
        }

        latency = model->resample ? 4 : 0;
        modelReady.store (model->dsp != nullptr, std::memory_order_release);

        // a model the audio thread never picked up is simply replaced
        delete pending.exchange (model.release());
    }

    /** Audio thread: parks a replaced model for the loader thread to delete (lock-free push). */
    void retire (Model* model)
    {
        model->nextRetired = retiredHead.load (std::memory_order_relaxed);
        while (! retiredHead.compare_exchange_weak (model->nextRetired, model))
        {}
    }

    void collectGarbage()
    {
        for (auto* m = retiredHead.exchange (nullptr); m != nullptr;)
        {
            auto* next = m->nextRetired;
            delete m;
            m = next;
        }
    }

    void setStatus (Status newStatus)
    {
        const juce::ScopedLock lock (infoLock);
        status = std::move (newStatus);
    }

    //==========================================================================
    std::atomic<double> hostRate { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    std::atomic<int> generation { 0 };
    std::atomic<int> latency { 0 };
    std::atomic<bool> modelReady { false };

    std::atomic<Model*> pending { nullptr };   // loader -> audio
    std::atomic<Model*> retiredHead { nullptr }; // audio -> loader (to delete)
    Model* current = nullptr;                  // owned by the audio thread

    juce::File currentFile;                    // message thread

    juce::CriticalSection infoLock, publishLock;
    Status status;
    ModelInfo info;

    juce::ThreadPool loader { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamEngine)
};
