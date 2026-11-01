/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
NeuralFlowAmpAudioProcessor::NeuralFlowAmpAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
	 ), apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
	formatManager.registerBasicFormats();
}

NeuralFlowAmpAudioProcessor::~NeuralFlowAmpAudioProcessor()
{
}

//==============================================================================
const juce::String NeuralFlowAmpAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NeuralFlowAmpAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NeuralFlowAmpAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NeuralFlowAmpAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NeuralFlowAmpAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NeuralFlowAmpAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NeuralFlowAmpAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NeuralFlowAmpAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NeuralFlowAmpAudioProcessor::getProgramName (int index)
{
    return {};
}

void NeuralFlowAmpAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NeuralFlowAmpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	crunchEffect.prepare(sampleRate, samplesPerBlock);
   
	juce::dsp::ProcessSpec spec;
	spec.sampleRate = sampleRate;
	spec.maximumBlockSize = samplesPerBlock;
	spec.numChannels = getTotalNumOutputChannels();

	eqChain.prepare(spec);
	lastLowDb = lastMidDb = lastHighDb = 1000.0f; // force coefficient refresh

	cabSimulator.prepare(spec);
	cabLowCut.prepare(spec);
	cabHighCut.prepare(spec);
	lastLowCutHz = lastHighCutHz = -1.0f; // force coefficient refresh

	compressor.prepare(spec);
	compressor.setRatio(4.0f);
	compressor.setRelease(120.0f);
	compressorDry.setSize((int)spec.numChannels, samplesPerBlock);
	overdrive.prepare(spec);
	overdriveGain.prepare(spec);

	noiseGate.prepare(spec);
	noiseGate.setRatio(10.0f);
	noiseGate.setAttack(1.0f);

	boost.prepare(spec);
	boost.setRampDurationSeconds(0.02);

	chorus.prepare(spec);
	chorus.setCentreDelay(7.0f);
	chorus.setFeedback(0.0f);

	delayFx.prepare(spec);
	reverb.prepare(spec);

	// Tube overdrive using tanh function
	overdrive.functionToUse = [](float x) { return std::tanh(x); };
}

void NeuralFlowAmpAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NeuralFlowAmpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NeuralFlowAmpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

	// 1. CLEAR BUFFER
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

	storePeakMax(inputPeak, buffer.getMagnitude(0, buffer.getNumSamples()));

	// 2. JUCE DSP AUDIO BLOCK
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    // 3. THE CHAIN: pedals and the amp, in the order the user arranged them
    const auto order = PedalCatalog::unpackOrder(chainOrder.load());
    for (int item : order)
    {
        if (item == PedalCatalog::ampItem)
            processAmp(buffer, context);
        else
            processPedal(item, buffer, context);
    }

	auto currentMaster = apvts.getRawParameterValue("MASTER")->load();
	float masterGain = currentMaster / 10.0f;
	buffer.applyGain(masterGain);

	storePeakMax(outputPeak, buffer.getMagnitude(0, buffer.getNumSamples()));

    // 7. OSCILLOSCOPE
    auto* channelData = buffer.getReadPointer(0);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // Turn soundwaves into array
        if (scopeDataIndex < scopeSize)
        {
            scopeData[scopeDataIndex] = channelData[sample];
            scopeDataIndex++;
        }

		// When the array is full, set the flag to true and reset the index to 0
        if (scopeDataIndex >= scopeSize)
        {
            isNextFrameReady = true;
            scopeDataIndex = 0; 
        }
    }
}

//==============================================================================
void NeuralFlowAmpAudioProcessor::processPedal (int item, juce::AudioBuffer<float>& buffer,
                                                juce::dsp::ProcessContextReplacing<float>& context)
{
    auto raw = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    auto pedalActive = [&](const char* added, const char* on) { return raw(added) > 0.5f && raw(on) > 0.5f; };

    switch (item)
    {
    case 0: // Noise gate
        if (pedalActive("GATE_ADDED", "GATE_ON"))
        {
            noiseGate.setThreshold(raw("GATE_THRESH"));
            noiseGate.setRelease(raw("GATE_REL"));
            noiseGate.process(context);
        }
        break;

    case 1: // Compressor
        if (raw("COMP_ON") > 0.5f)
        {
            const auto sustain = raw("COMP_SUST");
            const auto attack = raw("COMP_ATT");
            const auto blend = raw("COMP_MIX") / 100.0f;
            const auto makeup = juce::Decibels::decibelsToGain(raw("COMP_LVL"));

            const int numCh = buffer.getNumChannels();
            const int numSamples = buffer.getNumSamples();

            // keep a dry copy for parallel compression (no reallocation once prepared)
            compressorDry.setSize(numCh, numSamples, false, false, true);
            for (int ch = 0; ch < numCh; ++ch)
                compressorDry.copyFrom(ch, 0, buffer, ch, 0, numSamples);

            compressor.setThreshold(juce::jmap(sustain, 0.0f, 100.0f, 0.0f, -40.0f));
            compressor.setAttack(attack);
            compressor.process(context);

            // blend wet/dry, then apply the output level
            buffer.applyGain(blend);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.addFrom(ch, 0, compressorDry, ch, 0, numSamples, 1.0f - blend);
            buffer.applyGain(makeup);
        }
        break;

    case 2: // Boost
        if (pedalActive("BOOST_ADDED", "BOOST_ON"))
        {
            boost.setGainDecibels(raw("BOOST_GAIN"));
            boost.process(context);
        }
        break;

    case 3: // Overdrive
        if (raw("OD_ON") > 0.5f)
        {
            const auto odGain = raw("OD_GAIN");
            const auto odLvl = raw("OD_LVL");
            overdriveGain.setGainLinear(juce::Decibels::decibelsToGain(odGain * 2.0f));
            const float odOutputMultiplier = juce::jmap(odLvl, 0.0f, 10.0f, 0.0f, 2.0f);

            overdriveGain.process(context);
            overdrive.process(context);
            buffer.applyGain(odOutputMultiplier);
        }
        break;

    case 4: // Chorus
        if (pedalActive("CHORUS_ADDED", "CHORUS_ON"))
        {
            chorus.setRate(raw("CHORUS_RATE"));
            chorus.setDepth(raw("CHORUS_DEPTH") / 100.0f);
            chorus.setMix(raw("CHORUS_MIX") / 100.0f);
            chorus.process(context);
        }
        break;

    case 5: // Delay
        if (pedalActive("DELAY_ADDED", "DELAY_ON"))
        {
            delayFx.setParameters(raw("DELAY_TIME"), raw("DELAY_FB") / 100.0f, raw("DELAY_MIX") / 100.0f);
            delayFx.process(buffer);
        }
        break;

    case 6: // Reverb
        if (pedalActive("REVERB_ADDED", "REVERB_ON"))
        {
            const float mix = raw("REVERB_MIX") / 100.0f;

            juce::dsp::Reverb::Parameters reverbParams;
            reverbParams.roomSize = raw("REVERB_SIZE") / 100.0f;
            reverbParams.damping = raw("REVERB_DAMP") / 100.0f;
            reverbParams.width = 1.0f;
            reverbParams.dryLevel = 0.5f;          // juce::Reverb scales dry by 2, so 0.5 = unity
            reverbParams.wetLevel = mix * 0.33f;   // wet is scaled by 3
            reverb.setParameters(reverbParams);
            reverb.process(context);
        }
        break;

    default:
        jassertfalse;
        break;
    }
}

void NeuralFlowAmpAudioProcessor::processAmp (juce::AudioBuffer<float>& buffer,
                                              juce::dsp::ProcessContextReplacing<float>& context)
{
    // Preamp Drive
    crunchEffect.setDrive(apvts.getRawParameterValue("DRIVE")->load());
    crunchEffect.process(buffer);

    // EQ
    const float lowDb  = juce::jmap(apvts.getRawParameterValue("LOW")->load(),  0.0f, 10.0f, -15.0f, 15.0f);
    const float midDb  = juce::jmap(apvts.getRawParameterValue("MID")->load(),  0.0f, 10.0f, -15.0f, 15.0f);
    const float highDb = juce::jmap(apvts.getRawParameterValue("HIGH")->load(), 0.0f, 10.0f, -15.0f, 15.0f);

    const auto sr = getSampleRate();

    if (lowDb != lastLowDb)
    {
        *eqChain.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 250.0f, 0.707f, juce::Decibels::decibelsToGain(lowDb));
        lastLowDb = lowDb;
    }
    if (midDb != lastMidDb)
    {
        *eqChain.get<1>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 1000.0f, 0.707f, juce::Decibels::decibelsToGain(midDb));
        lastMidDb = midDb;
    }
    if (highDb != lastHighDb)
    {
        *eqChain.get<2>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 4000.0f, 0.707f, juce::Decibels::decibelsToGain(highDb));
        lastHighDb = highDb;
    }

    eqChain.process(context);

    // Cab sim (IR) and its low / high cut. Coefficients are only rebuilt when the knob moves.
    cabSimulator.process(context);

    const float lowHz = apvts.getRawParameterValue("CAB_LOWCUT")->load();
    const float highHz = juce::jmin(apvts.getRawParameterValue("CAB_HIGHCUT")->load(), (float)(sr * 0.49));

    if (lowHz != lastLowCutHz)
    {
        *cabLowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, lowHz);
        lastLowCutHz = lowHz;
    }
    if (highHz != lastHighCutHz)
    {
        *cabHighCut.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, highHz);
        lastHighCutHz = highHz;
    }

    cabLowCut.process(context);
    cabHighCut.process(context);
}

//==============================================================================
PedalCatalog::ChainOrder NeuralFlowAmpAudioProcessor::getChainOrder() const
{
    return PedalCatalog::unpackOrder(chainOrder.load());
}

void NeuralFlowAmpAudioProcessor::setChainOrder (const PedalCatalog::ChainOrder& order)
{
    if (PedalCatalog::isValidOrder(order))
        chainOrder.store(PedalCatalog::packOrder(order));
}

//==============================================================================
bool NeuralFlowAmpAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NeuralFlowAmpAudioProcessor::createEditor()
{
    return new NeuralFlowAmpAudioProcessorEditor (*this);
}

//==============================================================================
void NeuralFlowAmpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Parameters plus the path of the loaded IR, stored as XML.
    auto state = apvts.copyState();
    state.setProperty("irPath", loadedIrFile.getFullPathName(), nullptr);
    state.setProperty("chainOrder", (int) chainOrder.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void NeuralFlowAmpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    // Pedal / amp order (older sessions have none: keep the default)
    auto order = PedalCatalog::defaultOrder();
    if (state.hasProperty("chainOrder"))
    {
        const auto loaded = PedalCatalog::unpackOrder((juce::uint32)(int) state.getProperty("chainOrder"));
        if (PedalCatalog::isValidOrder(loaded))
            order = loaded;
    }
    setChainOrder(order);

    // The IR itself is not embedded: reload it if the file is still there.
    const juce::File irFile(state.getProperty("irPath").toString());
    if (irFile.existsAsFile())
        loadImpulseResponse(irFile);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeuralFlowAmpAudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout NeuralFlowAmpAudioProcessor::createParameterLayout() {
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>("DRIVE", "Drive", 1.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LOW", "Low", 0.0f, 10.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MID", "Mid", 0.0f, 10.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HIGH", "High", 0.0f, 10.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MASTER", "Master", 0.0f, 10.0f, 5.0f));

    // Compressor
    params.push_back(std::make_unique<juce::AudioParameterFloat>("COMP_SUST", "Sustain", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("COMP_ATT", "Attack", 0.0f, 100.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("COMP_MIX", "Blend", 0.0f, 100.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("COMP_LVL", "Level", -12.0f, 12.0f, 0.0f));


    // Overdrive
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OD_GAIN", "Gain", 0.0f, 10.0f, 3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OD_TONE", "Tone", 0.0f, 10.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OD_LVL", "Level", 0.0f, 10.0f, 5.0f));

    // Pedal bypass switches (true = pedal active)
    params.push_back(std::make_unique<juce::AudioParameterBool>("COMP_ON", "Compressor On", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("OD_ON", "Overdrive On", true));

    // Add-on pedals. *_ADDED says whether the pedal is in the chain (set from the UI).
    auto addPedalSwitches = [&params](const juce::String& id, const juce::String& name)
    {
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(id + "_ADDED", 1), name + " In Chain", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        params.push_back(std::make_unique<juce::AudioParameterBool>(id + "_ON", name + " On", true));
    };
    auto addFloat = [&params](const juce::String& id, const juce::String& name, float lo, float hi, float def, float skew = 1.0f)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(id, name,
            juce::NormalisableRange<float>(lo, hi, 0.0f, skew), def));
    };

    addPedalSwitches("GATE", "Gate");
    addFloat("GATE_THRESH", "Gate Threshold", -80.0f, -20.0f, -55.0f);
    addFloat("GATE_REL", "Gate Release", 10.0f, 500.0f, 120.0f, 0.5f);

    addPedalSwitches("BOOST", "Boost");
    addFloat("BOOST_GAIN", "Boost Gain", 0.0f, 24.0f, 6.0f);

    addPedalSwitches("CHORUS", "Chorus");
    addFloat("CHORUS_RATE", "Chorus Rate", 0.1f, 8.0f, 1.2f, 0.5f);
    addFloat("CHORUS_DEPTH", "Chorus Depth", 0.0f, 100.0f, 40.0f);
    addFloat("CHORUS_MIX", "Chorus Mix", 0.0f, 100.0f, 50.0f);

    addPedalSwitches("DELAY", "Delay");
    addFloat("DELAY_TIME", "Delay Time", 40.0f, 1000.0f, 380.0f, 0.6f);
    addFloat("DELAY_FB", "Delay Feedback", 0.0f, 90.0f, 35.0f);
    addFloat("DELAY_MIX", "Delay Mix", 0.0f, 100.0f, 30.0f);

    addPedalSwitches("REVERB", "Reverb");
    addFloat("REVERB_SIZE", "Reverb Size", 0.0f, 100.0f, 50.0f);
    addFloat("REVERB_DAMP", "Reverb Damping", 0.0f, 100.0f, 50.0f);
    addFloat("REVERB_MIX", "Reverb Mix", 0.0f, 100.0f, 25.0f);

    // Cab filters (defaults are fully open, i.e. transparent)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CAB_LOWCUT", "Cab Low Cut",
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f, 0.5f), 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CAB_HIGHCUT", "Cab High Cut",
        juce::NormalisableRange<float>(4000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f));

    return { params.begin(), params.end() };
}

void NeuralFlowAmpAudioProcessor::loadImpulseResponse(const juce::File& file) {
	if (file.existsAsFile()) {
		cabSimulator.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0, juce::dsp::Convolution::Normalise::yes);
		loadedIrFile = file;
	}
}