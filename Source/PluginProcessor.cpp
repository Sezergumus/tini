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

    ampTypeParam = apvts.getRawParameterValue("AMP_TYPE");
    namInParam   = apvts.getRawParameterValue("NAM_IN");
    namOutParam  = apvts.getRawParameterValue("NAM_OUT");
    namNormParam = apvts.getRawParameterValue("NAM_NORM");

    for (int item = 0; item < PedalCatalog::ampItem; ++item)
    {
        auto& cache = pedalParams[(size_t) item];
        cache.on = apvts.getRawParameterValue(PedalCatalog::onId(item));

        const auto added = PedalCatalog::addedId(item);
        cache.added = added.isNotEmpty() ? apvts.getRawParameterValue(added) : nullptr;

        const auto& knobs = PedalCatalog::pedalOf(item).knobs;
        for (size_t k = 0; k < knobs.size(); ++k)
            cache.knob[k] = apvts.getRawParameterValue(PedalCatalog::knobId(item, (int) k));
    }
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

// The host sees the factory presets as its programs (the list of user presets can change while
// the host is reading it, so it stays out of here).
int NeuralFlowAmpAudioProcessor::getNumPrograms()
{
    return juce::jmax (1, presets.getNumFactory());
}

int NeuralFlowAmpAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void NeuralFlowAmpAudioProcessor::setCurrentProgram (int index)
{
    // Some hosts echo the current program back after restoring a session: that must not
    // throw away the restored settings.
    if (index != currentProgram && juce::isPositiveAndBelow (index, presets.getNumFactory()))
        loadPreset (index);
}

const juce::String NeuralFlowAmpAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, presets.getNumFactory()) ? presets.get (index).name : juce::String();
}

void NeuralFlowAmpAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NeuralFlowAmpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	crunchEffect.prepare(sampleRate, samplesPerBlock);
	nam.prepare(sampleRate, samplesPerBlock);
   
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

	compressorDry.setSize((int)spec.numChannels, samplesPerBlock);

	for (size_t c = 0; c < (size_t) copies; ++c)
	{
		compressor[c].prepare(spec);
		compressor[c].setRatio(4.0f);
		compressor[c].setRelease(120.0f);

		overdrive[c].prepare(spec);
		overdriveGain[c].prepare(spec);
		// Tube overdrive using tanh function
		overdrive[c].functionToUse = [](float x) { return std::tanh(x); };

		noiseGate[c].prepare(spec);
		noiseGate[c].setRatio(10.0f);
		noiseGate[c].setAttack(1.0f);

		boost[c].prepare(spec);
		boost[c].setRampDurationSeconds(0.02);

		chorus[c].prepare(spec);
		chorus[c].setCentreDelay(7.0f);
		chorus[c].setFeedback(0.0f);

		delayFx[c].prepare(spec);
		reverb[c].prepare(spec);
	}
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
    {
        const juce::SpinLock::ScopedTryLockType lock(chainLock);
        if (lock.isLocked())
            audioOrder = chainOrder;
    }

    for (int item : audioOrder)
    {
        if (PedalCatalog::isAmp(item))
            processAmp(buffer, context);
        else if (isPedalActive(item))
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
bool NeuralFlowAmpAudioProcessor::isPedalActive (int item) const
{
    const auto& cache = pedalParams[(size_t) item];
    return cache.on->load() > 0.5f && (cache.added == nullptr || cache.added->load() > 0.5f);
}

void NeuralFlowAmpAudioProcessor::processPedal (int item, juce::AudioBuffer<float>& buffer,
                                                juce::dsp::ProcessContextReplacing<float>& context)
{
    const auto& params = pedalParams[(size_t) item];
    const auto c = (size_t) PedalCatalog::copyOf(item);

    switch (PedalCatalog::typeOf(item))
    {
    case 0: // Noise gate
        noiseGate[c].setThreshold(params.value(0));
        noiseGate[c].setRelease(params.value(1));
        noiseGate[c].process(context);
        break;

    case 1: // Compressor
    {
        const auto sustain = params.value(0);
        const auto attack = params.value(1);
        const auto blend = params.value(2) / 100.0f;
        const auto makeup = juce::Decibels::decibelsToGain(params.value(3));

        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // keep a dry copy for parallel compression (no reallocation once prepared)
        compressorDry.setSize(numCh, numSamples, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            compressorDry.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        compressor[c].setThreshold(juce::jmap(sustain, 0.0f, 100.0f, 0.0f, -40.0f));
        compressor[c].setAttack(attack);
        compressor[c].process(context);

        // blend wet/dry, then apply the output level
        buffer.applyGain(blend);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.addFrom(ch, 0, compressorDry, ch, 0, numSamples, 1.0f - blend);
        buffer.applyGain(makeup);
        break;
    }

    case 2: // Boost
        boost[c].setGainDecibels(params.value(0));
        boost[c].process(context);
        break;

    case 3: // Overdrive
    {
        const auto odGain = params.value(0);
        const auto odLvl = params.value(2);
        overdriveGain[c].setGainLinear(juce::Decibels::decibelsToGain(odGain * 2.0f));
        const float odOutputMultiplier = juce::jmap(odLvl, 0.0f, 10.0f, 0.0f, 2.0f);

        overdriveGain[c].process(context);
        overdrive[c].process(context);
        buffer.applyGain(odOutputMultiplier);
        break;
    }

    case 4: // Chorus
        chorus[c].setRate(params.value(0));
        chorus[c].setDepth(params.value(1) / 100.0f);
        chorus[c].setMix(params.value(2) / 100.0f);
        chorus[c].process(context);
        break;

    case 5: // Delay
        delayFx[c].setParameters(params.value(0), params.value(1) / 100.0f, params.value(2) / 100.0f);
        delayFx[c].process(buffer);
        break;

    case 6: // Reverb
    {
        const float mix = params.value(2) / 100.0f;

        juce::dsp::Reverb::Parameters reverbParams;
        reverbParams.roomSize = params.value(0) / 100.0f;
        reverbParams.damping = params.value(1) / 100.0f;
        reverbParams.width = 1.0f;
        reverbParams.dryLevel = 0.5f;          // juce::Reverb scales dry by 2, so 0.5 = unity
        reverbParams.wetLevel = mix * 0.33f;   // wet is scaled by 3
        reverb[c].setParameters(reverbParams);
        reverb[c].process(context);
        break;
    }

    default:
        jassertfalse;
        break;
    }
}

void NeuralFlowAmpAudioProcessor::processAmp (juce::AudioBuffer<float>& buffer,
                                              juce::dsp::ProcessContextReplacing<float>& context)
{
    // Preamp: a Neural Amp Modeler model if one is selected and ready, otherwise the built-in amp
    const bool namUsed = ampTypeParam->load() > 0.5f
                      && nam.process(buffer, namInParam->load(), namOutParam->load(), namNormParam->load() > 0.5f);

    if (! namUsed)
    {
        crunchEffect.setDrive(apvts.getRawParameterValue("DRIVE")->load());
        crunchEffect.process(buffer);
    }

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
    const juce::SpinLock::ScopedLockType lock(chainLock);
    return chainOrder;
}

void NeuralFlowAmpAudioProcessor::setChainOrder (const PedalCatalog::ChainOrder& order)
{
    if (! PedalCatalog::isValidOrder(order))
        return;

    const juce::SpinLock::ScopedLockType lock(chainLock);
    chainOrder = order;
}

//==============================================================================
bool NeuralFlowAmpAudioProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.size()))
        return false;

    const auto preset = presets.get (index);   // copy: loading the IR etc. must not depend on the list staying put

    PresetManager::applyValues (preset, apvts);
    setChainOrder (preset.order);

    if (preset.irPath.isNotEmpty())
        loadImpulseResponse (juce::File (preset.irPath));   // ignored if the file is gone

    if (preset.namPath.isNotEmpty() && juce::File (preset.namPath).existsAsFile())
        loadNamModel (juce::File (preset.namPath));

    currentPresetName = preset.name;
    if (index < presets.getNumFactory())
        currentProgram = index;

    return true;
}

int NeuralFlowAmpAudioProcessor::saveCurrentAsPreset (const juce::String& name)
{
    PresetManager::Preset preset;
    preset.name = name;
    preset.values = PresetManager::captureValues (apvts);
    preset.order = getChainOrder();
    preset.irPath = loadedIrFile.existsAsFile() ? loadedIrFile.getFullPathName() : juce::String();
    preset.namPath = nam.getFile().existsAsFile() ? nam.getFile().getFullPathName() : juce::String();

    const int index = presets.saveUserPreset (std::move (preset));
    if (index >= 0)
        currentPresetName = presets.get (index).name;

    return index;
}

juce::Result NeuralFlowAmpAudioProcessor::renamePreset (int index, const juce::String& newName)
{
    if (! juce::isPositiveAndBelow (index, presets.size()))
        return juce::Result::fail ("No such preset");

    const bool wasCurrent = index == getCurrentPresetIndex();
    auto result = presets.renameUserPreset (index, newName);

    if (result.wasOk() && wasCurrent)
        currentPresetName = newName.trim();

    return result;
}

bool NeuralFlowAmpAudioProcessor::deletePreset (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.size()))
        return false;

    const bool wasCurrent = index == getCurrentPresetIndex();
    const bool ok = presets.deleteUserPreset (index);

    if (ok && wasCurrent)
        currentPresetName = {};   // the settings stay, they are just untitled now

    return ok;
}

int NeuralFlowAmpAudioProcessor::getCurrentPresetIndex() const
{
    return currentPresetName.isEmpty() ? -1 : presets.indexOf (currentPresetName);
}

bool NeuralFlowAmpAudioProcessor::isPresetModified() const
{
    const int index = getCurrentPresetIndex();
    if (index < 0)
        return false;

    return ! PresetManager::matches (presets.get (index), apvts, getChainOrder());
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
    state.setProperty("namPath", nam.getFile().getFullPathName(), nullptr);
    state.setProperty("chain", PedalCatalog::orderToString(getChainOrder()), nullptr);
    state.setProperty("presetName", currentPresetName, nullptr);

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
    if (state.hasProperty("chain"))
        PedalCatalog::orderFromString(state.getProperty("chain").toString(), order);
    else if (state.hasProperty("chainOrder"))   // sessions saved by version 1
        PedalCatalog::orderFromLegacy((juce::uint32)(int) state.getProperty("chainOrder"), order);
    setChainOrder(order);

    // Which preset the session was based on (the values above stay as saved, edits included)
    currentPresetName = state.getProperty("presetName").toString();
    const int restored = presets.indexOf(currentPresetName);
    if (restored >= 0 && restored < presets.getNumFactory())
        currentProgram = restored;

    // The IR itself is not embedded: reload it if the file is still there.
    const juce::File irFile(state.getProperty("irPath").toString());
    if (irFile.existsAsFile())
        loadImpulseResponse(irFile);

    const juce::File namFile(state.getProperty("namPath").toString());
    if (namFile.existsAsFile())
        loadNamModel(namFile);
    else
        unloadNamModel();
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

    // Amp slot: built-in amp or a Neural Amp Modeler model
    params.push_back(std::make_unique<juce::AudioParameterChoice>("AMP_TYPE", "Amp Type", juce::StringArray { "Built-in", "NAM" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("NAM_IN", "NAM Input", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("NAM_OUT", "NAM Output", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("NAM_NORM", "NAM Normalise", true));

    // Pedals. Every pedal type exists maxCopies times; copy 0 keeps the plain ids, the others get a
    // "_2", "_3" suffix (see PedalCatalog::suffixedId). *_ADDED says whether a copy is in the chain
    // (set from the UI); copy 0 of the compressor and overdrive is permanent and has no such switch.
    for (int copy = 0; copy < PedalCatalog::maxCopies; ++copy)
    {
        auto id   = [copy](const juce::String& base) { return PedalCatalog::suffixedId(base, copy); };
        auto name = [copy](const juce::String& n) { return copy == 0 ? n : n + " " + juce::String(copy + 1); };

        auto addFloat = [&](const juce::String& base, const juce::String& n, float lo, float hi, float def, float skew = 1.0f)
        {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(id(base), name(n),
                juce::NormalisableRange<float>(lo, hi, 0.0f, skew), def));
        };
        auto addSwitches = [&](const juce::String& base, const juce::String& n, bool permanentFirstCopy)
        {
            if (! (permanentFirstCopy && copy == 0))
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID(id(base + "_ADDED"), 1), name(n) + " In Chain", false,
                    juce::AudioParameterBoolAttributes().withAutomatable(false)));
            params.push_back(std::make_unique<juce::AudioParameterBool>(id(base + "_ON"), name(n) + " On", true));
        };

        addSwitches("GATE", "Gate", false);
        addFloat("GATE_THRESH", "Gate Threshold", -80.0f, -20.0f, -55.0f);
        addFloat("GATE_REL", "Gate Release", 10.0f, 500.0f, 120.0f, 0.5f);

        addSwitches("COMP", "Compressor", true);
        addFloat("COMP_SUST", "Sustain", 0.0f, 100.0f, 50.0f);
        addFloat("COMP_ATT", "Attack", 0.0f, 100.0f, 10.0f);
        addFloat("COMP_MIX", "Blend", 0.0f, 100.0f, 100.0f);
        addFloat("COMP_LVL", "Level", -12.0f, 12.0f, 0.0f);

        addSwitches("BOOST", "Boost", false);
        addFloat("BOOST_GAIN", "Boost Gain", 0.0f, 24.0f, 6.0f);

        addSwitches("OD", "Overdrive", true);
        addFloat("OD_GAIN", "Gain", 0.0f, 10.0f, 3.0f);
        addFloat("OD_TONE", "Tone", 0.0f, 10.0f, 5.0f);
        addFloat("OD_LVL", "Level", 0.0f, 10.0f, 5.0f);

        addSwitches("CHORUS", "Chorus", false);
        addFloat("CHORUS_RATE", "Chorus Rate", 0.1f, 8.0f, 1.2f, 0.5f);
        addFloat("CHORUS_DEPTH", "Chorus Depth", 0.0f, 100.0f, 40.0f);
        addFloat("CHORUS_MIX", "Chorus Mix", 0.0f, 100.0f, 50.0f);

        addSwitches("DELAY", "Delay", false);
        addFloat("DELAY_TIME", "Delay Time", 40.0f, 1000.0f, 380.0f, 0.6f);
        addFloat("DELAY_FB", "Delay Feedback", 0.0f, 90.0f, 35.0f);
        addFloat("DELAY_MIX", "Delay Mix", 0.0f, 100.0f, 30.0f);

        addSwitches("REVERB", "Reverb", false);
        addFloat("REVERB_SIZE", "Reverb Size", 0.0f, 100.0f, 50.0f);
        addFloat("REVERB_DAMP", "Reverb Damping", 0.0f, 100.0f, 50.0f);
        addFloat("REVERB_MIX", "Reverb Mix", 0.0f, 100.0f, 25.0f);
    }

    // Cab filters (defaults are fully open, i.e. transparent)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CAB_LOWCUT", "Cab Low Cut",
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f, 0.5f), 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CAB_HIGHCUT", "Cab High Cut",
        juce::NormalisableRange<float>(4000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f));

    return { params.begin(), params.end() };
}

void NeuralFlowAmpAudioProcessor::loadNamModel(const juce::File& file)
{
    if (file.existsAsFile())
        nam.loadModel(file);
}

void NeuralFlowAmpAudioProcessor::unloadNamModel()
{
    nam.unload();
}

void NeuralFlowAmpAudioProcessor::loadImpulseResponse(const juce::File& file) {
	if (file.existsAsFile()) {
		cabSimulator.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0, juce::dsp::Convolution::Normalise::yes);
		loadedIrFile = file;
	}
}