#pragma once

#include <JuceHeader.h>
#include "Crunch.h"
#include "DSP/StereoDelay.h"
#include "DSP/NamEngine.h"
#include "PedalCatalog.h"
#include "PresetManager.h"

//==============================================================================
/**
*/
class NeuralFlowAmpAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    NeuralFlowAmpAudioProcessor();
    ~NeuralFlowAmpAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Block peaks (linear gain) for the UI meters. The UI reads them with exchange(0).
    std::atomic<float> inputPeak{ 0.0f };
    std::atomic<float> outputPeak{ 0.0f };

    void loadImpulseResponse(const juce::File& file);

    /** Order of the pedals and the amp in the signal chain (item numbers, see PedalCatalog).
        Safe to call from the UI while audio is running. Items whose "added" switch is off are
        parked in the order and not processed. */
    PedalCatalog::ChainOrder getChainOrder() const;
    void setChainOrder(const PedalCatalog::ChainOrder& order);

    //==============================================================================
    // Presets (message thread only). Factory presets are also exposed to the host as programs.
    PresetManager presets;

    /** Applies preset `index` of `presets`: all parameters, the chain order and, if it has one, the IR. */
    bool loadPreset (int index);

    /** Saves the current pedals, settings, order and IR under `name` (overwrites a user preset of
        that name). Returns the preset's index, or -1 if the name is not allowed / writing failed. */
    int saveCurrentAsPreset (const juce::String& name);

    /** Renames / deletes one of the user's presets (factory presets cannot be changed). */
    juce::Result renamePreset (int index, const juce::String& newName);
    bool deletePreset (int index);

    /** Index in `presets` of the preset that was loaded last, or -1 for an untitled setup. */
    int getCurrentPresetIndex() const;

    /** True if something was changed since the current preset was loaded. */
    bool isPresetModified() const;

    juce::File loadedIrFile; // last IR chosen by the user (message thread only)

    /** Neural Amp Modeler model of the amp slot. AMP_TYPE selects between it and the built-in amp;
        while no model is loaded (or it is still loading) the built-in amp is used. */
    NamEngine nam;
    void loadNamModel(const juce::File& file);   // background load, see nam.getStatus()
    void unloadNamModel();

    juce::dsp::Convolution cabSimulator;
    juce::AudioFormatManager formatManager;
private:
    static void storePeakMax(std::atomic<float>& target, float value)
    {
        if (value > target.load(std::memory_order_relaxed))
            target.store(value, std::memory_order_relaxed);
    }

    void processPedal(int item, juce::AudioBuffer<float>& buffer, juce::dsp::ProcessContextReplacing<float>& context);
    bool isPedalActive(int item) const;
    void processAmp(juce::AudioBuffer<float>& buffer, juce::dsp::ProcessContextReplacing<float>& context);

    // The UI writes `chainOrder` under the lock; the audio thread copies it into `audioOrder`
    // with a try-lock once per block (it keeps the previous order if the UI holds the lock).
    juce::SpinLock chainLock;
    PedalCatalog::ChainOrder chainOrder = PedalCatalog::defaultOrder();
    PedalCatalog::ChainOrder audioOrder = PedalCatalog::defaultOrder();

    /** Cached parameter pointers of one pedal copy, so the audio thread never looks up ids by name. */
    struct PedalParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* added = nullptr;               // nullptr = permanent
        std::array<std::atomic<float>*, 4> knob {};
        float value (int i) const { return knob[(size_t) i]->load(); }
    };
    std::array<PedalParams, PedalCatalog::ampItem> pedalParams;

    std::atomic<float>* ampTypeParam = nullptr;
    std::atomic<float>* namInParam = nullptr;
    std::atomic<float>* namOutParam = nullptr;
    std::atomic<float>* namNormParam = nullptr;

    juce::String currentPresetName; // empty = untitled
    int currentProgram = 0;         // factory preset last reported to the host

    using CabFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    CabFilter cabLowCut, cabHighCut;
    float lastLowCutHz = -1.0f, lastHighCutHz = -1.0f;

    //==============================================================================    
	juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    Crunch crunchEffect;

	// ProcessorDuplicator keeps one filter state per channel (a bare IIR::Filter is mono).
	using EqBand = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
	juce::dsp::ProcessorChain<EqBand, EqBand, EqBand> eqChain;
	float lastLowDb = 1000.0f, lastMidDb = 1000.0f, lastHighDb = 1000.0f;

    // One processor per pedal copy (index = copy). A pedal only runs while its "added" and "on"
    // parameters are true.
    static constexpr int copies = PedalCatalog::maxCopies;
    std::array<juce::dsp::Compressor<float>, copies> compressor;
    juce::AudioBuffer<float> compressorDry; // untouched copy for the blend knob (shared scratch)
    std::array<juce::dsp::WaveShaper<float>, copies> overdrive;
    std::array<juce::dsp::Gain<float>, copies> overdriveGain;

    std::array<juce::dsp::NoiseGate<float>, copies> noiseGate;
    std::array<juce::dsp::Gain<float>, copies> boost;
    std::array<juce::dsp::Chorus<float>, copies> chorus;
    std::array<StereoDelay, copies> delayFx;
    std::array<juce::dsp::Reverb, copies> reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralFlowAmpAudioProcessor)
};
