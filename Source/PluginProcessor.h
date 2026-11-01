#pragma once

#include <JuceHeader.h>
#include "Crunch.h"

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

    static constexpr int scopeSize = 512;
    std::array<float, scopeSize> scopeData;
    std::atomic<bool> isNextFrameReady{ false };

    // Block peaks (linear gain) for the UI meters. The UI reads them with exchange(0).
    std::atomic<float> inputPeak{ 0.0f };
    std::atomic<float> outputPeak{ 0.0f };

    void loadImpulseResponse(const juce::File& file);

    juce::File loadedIrFile; // last IR chosen by the user (message thread only)

    juce::dsp::Convolution cabSimulator;
    juce::AudioFormatManager formatManager;
private:
    static void storePeakMax(std::atomic<float>& target, float value)
    {
        if (value > target.load(std::memory_order_relaxed))
            target.store(value, std::memory_order_relaxed);
    }

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

    juce::dsp::Compressor<float> compressor;
    juce::dsp::WaveShaper<float> overdrive;
    juce::dsp::Gain<float> overdriveGain;
    
    int scopeDataIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralFlowAmpAudioProcessor)
};
