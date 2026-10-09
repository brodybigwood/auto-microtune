/*
  ==============================================================================
    super-autotune — Audio Processor implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "TuningManager.h"
#include "MidiManager.h"
#include "SpectrumAnalyzer.h"

//==============================================================================
SuperautotuneAudioProcessor::SuperautotuneAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    pitchDetector    = std::make_unique<PitchDetector>();
    pitchShifter     = std::make_unique<PitchShifter>();
    tuningManager    = std::make_unique<TuningManager>();
    midiManager      = std::make_unique<MidiManager>();
    spectrumAnalyzer = std::make_unique<SpectrumAnalyzer>();
}

SuperautotuneAudioProcessor::~SuperautotuneAudioProcessor() = default;

//==============================================================================
const juce::String SuperautotuneAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SuperautotuneAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SuperautotuneAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SuperautotuneAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SuperautotuneAudioProcessor::getTailLengthSeconds() const
{
    // The shifter holds `latency` samples of history; if the host cuts us off
    // with no reported tail, those samples pop.
    return static_cast<double>(pitchShifter ? pitchShifter->getLatencySamples() : 0)
         / getSampleRate();
}

int SuperautotuneAudioProcessor::getNumPrograms()
{
    return 1;
}

int SuperautotuneAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SuperautotuneAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String SuperautotuneAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void SuperautotuneAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void SuperautotuneAudioProcessor::prepareToPlay (double sr, int blockSize)
{
    auto numChannels = getTotalNumInputChannels();

    // Cache current parameter values
    auto* fftParam     = apvts.getRawParameterValue(ParamIDs::fftSize);
    auto* hopParam     = apvts.getRawParameterValue(ParamIDs::hopPercent);
    auto* retuneParam  = apvts.getRawParameterValue(ParamIDs::retuneSpeed);
    auto* modeParam    = apvts.getRawParameterValue(ParamIDs::tuningMode);
    auto* midiParam    = apvts.getRawParameterValue(ParamIDs::midiEnabled);
    auto* octParam     = apvts.getRawParameterValue(ParamIDs::octaveExtend);
    auto* wetParam     = apvts.getRawParameterValue(ParamIDs::wetDry);
    auto* bypassParam  = apvts.getRawParameterValue(ParamIDs::bypass);

    currentFftIndex      = static_cast<int>(fftParam->load());
    currentFftSize       = FFTSizes::getValue(currentFftIndex);
    currentHopIndex      = static_cast<int>(hopParam->load());
    currentHopSize       = HopPercent::getHopSize(currentFftSize, currentHopIndex);
    currentRetuneSpeed   = retuneParam->load();
    currentTuningMode    = static_cast<int>(modeParam->load());
    currentMidiEnabled   = midiParam->load() > 0.5f;
    currentOctaveExtend  = octParam->load() > 0.5f;
    currentWetDry        = wetParam->load();
    currentBypass        = bypassParam->load() > 0.5f;

    // Prepare DSP components. The shifter owns its input rings and runs its own
    // pitch detector, so the FFT size parameter now only affects the spectrum
    // display — it no longer has anything to do with the pitch shifting, which is
    // time-domain.
    pitchDetector->prepare(currentFftSize, sr);
    pitchShifter->prepare(sr, blockSize, numChannels);
    spectrumAnalyzer->prepare(currentFftSize, sr);

    // Scratch buffers, block sized: the shifter streams in and out, so there are
    // no FFT-sized frame buffers any more.
    multiplyFrameSum.resize(static_cast<size_t> (blockSize));
    multiplyFrame.resize(static_cast<size_t> (blockSize));

    // Dry/wet buffers
    dryBuffer.setSize(numChannels, blockSize, false, true);
    processedBuffer.setSize(numChannels, blockSize, false, true);

    // Per-voice phase states + per-channel dry delay lines
    rebuildVoiceResources();

    // Report latency. The shifter's latency is a fixed ring multiple, independent
    // of pitch and of the shift ratio, so the host's compensation is exact. (The
    // old value was fftSize - hopSize, which was not the real OLA latency anyway.)
    setLatencySamples(pitchShifter->getLatencySamples());

    // Clear state
    detectedPitch.store(0.0f);
    targetPitch.store(0.0f);
    targetNoteIndex.store(-1);
    fftConfigChanged.store(false);
    tuningConfigChanged.store(false);

    prepared = true;
}

void SuperautotuneAudioProcessor::rebuildVoiceResources()
{
    int numChannels = std::max(getTotalNumInputChannels(), 1);
    stateStride = maxMultiplyVoices;

    voiceStates.clear();
    voiceStates.reserve(static_cast<size_t>(numChannels) * stateStride);
    for (int i = 0; i < numChannels * stateStride; ++i)
        voiceStates.push_back(pitchShifter->makeState());

    // The dry path must be delayed by exactly the shifter's latency, or the wet
    // and dry branches comb against each other at any MIX below 100%.
    dryDelays.clear();
    dryDelays.resize(numChannels);
    for (auto& dd : dryDelays)
        dd.prepare(pitchShifter->getLatencySamples());
}

void SuperautotuneAudioProcessor::releaseResources()
{
    pitchDetector->reset();
    pitchShifter->reset();
    midiManager->reset();
    voiceStates.clear();
    prepared = false;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SuperautotuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

//==============================================================================
void SuperautotuneAudioProcessor::handleParameterChanges()
{
    // Check for FFT/hop changes (require re-prepare of DSP components)
    auto fftIdx = static_cast<int>(apvts.getRawParameterValue(ParamIDs::fftSize)->load());
    auto hopIdx = static_cast<int>(apvts.getRawParameterValue(ParamIDs::hopPercent)->load());

    if (fftIdx != currentFftIndex || hopIdx != currentHopIndex)
    {
        currentFftIndex = fftIdx;
        currentFftSize  = FFTSizes::getValue(fftIdx);
        currentHopIndex = hopIdx;
        currentHopSize  = HopPercent::getHopSize(currentFftSize, hopIdx);

        fftConfigChanged.store(true);
    }

    // Smooth parameters
    currentRetuneSpeed   = apvts.getRawParameterValue(ParamIDs::retuneSpeed)->load();
    currentTuningMode    = static_cast<int>(apvts.getRawParameterValue(ParamIDs::tuningMode)->load());
    currentMidiEnabled   = apvts.getRawParameterValue(ParamIDs::midiEnabled)->load() > 0.5f;
    currentOctaveExtend  = apvts.getRawParameterValue(ParamIDs::octaveExtend)->load() > 0.5f;
    currentWetDry        = apvts.getRawParameterValue(ParamIDs::wetDry)->load();
    currentBypass        = apvts.getRawParameterValue(ParamIDs::bypass)->load() > 0.5f;
}

//==============================================================================
void SuperautotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto numChannels = getTotalNumInputChannels();
    auto numSamples  = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = numChannels; i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);

    // Handle parameter changes
    handleParameterChanges();

    // The FFT size parameter only drives the spectrum display now. The shifter is
    // time-domain, so there is nothing to reconfigure when it changes.
    if (fftConfigChanged.load())
    {
        pitchDetector->prepare(currentFftSize, getSampleRate());
        spectrumAnalyzer->prepare(currentFftSize, getSampleRate());
        fftConfigChanged.store(false);
    }

    // If bypassed or not prepared, passthrough
    if (currentBypass || !prepared)
        return;

    // === STEP 1: Process MIDI ===
    midiManager->process(midiMessages, getSampleRate());

    // === STEP 2: Update tuning set ===
    tuningManager->setOctaveExtend(currentOctaveExtend);
    tuningManager->setMidiEnabled(currentMidiEnabled);
    tuningManager->setMidiNotes(midiManager->getActiveNoteFrequencies());

    // === STEP 3: Save dry input for wet/dry mix ===
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    processedBuffer.clear();

    // === STEP 4: Per-channel processing ===
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* channelData = buffer.getReadPointer(ch);

        // Feed this channel once. The shifter runs its own detector on a fixed
        // internal hop, and every voice of this channel reads the same input ring
        // and the same pitch estimate — that is what keeps the stereo image stable
        // and stops voices from drifting apart.
        pitchShifter->pushChannel(ch, channelData, numSamples);

        const float currentFreq = pitchShifter->getDetectedPitch(ch);
        detectedPitch.store(currentFreq);

        // Feed the spectrum analyzer (windowed). fillWindowingTables writes
        // fftSize+1 samples, so allocate that many, then truncate to fftSize.
        if (numSamples >= currentFftSize)
        {
            std::vector<float> windowedCopy(currentFftSize);
            std::vector<float> spectrumWindow(currentFftSize + 1);
            juce::dsp::WindowingFunction<float>::fillWindowingTables(
                spectrumWindow.data(), currentFftSize + 1,
                juce::dsp::WindowingFunction<float>::hann, false);
            for (int i = 0; i < currentFftSize; ++i)
                windowedCopy[i] = channelData[numSamples - currentFftSize + i] * spectrumWindow[i];
            spectrumAnalyzer->feed(windowedCopy.data(), currentFftSize);
        }

        // Base index into the per-voice state pool for this channel.
        const int chBase = ch * stateStride;
        auto* outData = processedBuffer.getWritePointer(ch);

        if (currentTuningMode == 0)  // Snap mode: one voice at the nearest allowed note
        {
            // ONE lookup supplies both the frequency the audio snaps to and the
            // note the UI highlights, so they cannot disagree. An invalid result
            // (nothing selected, or no pitch) means no correction: the shifter
            // treats a non-positive target as ratio 1 and stays transparent.
            const TuningTarget target = tuningManager->findTarget(currentFreq);

            targetPitch.store(target.valid ? target.freqHz : 0.0f);
            targetNoteIndex.store(target.valid ? target.midiNote : -1);

            pitchShifter->render(voiceStates[chBase], ch, outData, numSamples,
                                 target.valid ? target.freqHz : 0.0f, currentRetuneSpeed);
        }
        else  // Multiply mode: one voice per active note, capped
        {
            const int numVoices = std::min(tuningManager->getVoiceCount(), maxMultiplyVoices);

            if (numVoices <= 0)
            {
                pitchShifter->render(voiceStates[chBase], ch, outData, numSamples,
                                     0.0f, currentRetuneSpeed);
            }
            else
            {
                std::fill(multiplyFrameSum.begin(), multiplyFrameSum.begin() + numSamples, 0.0f);

                for (int v = 0; v < numVoices; ++v)
                {
                    const float targetFreq = tuningManager->getMultiplyNote(currentFreq, v);

                    // Independent state per (channel, voice): each voice has its
                    // own read cursor and ratio.
                    pitchShifter->render(voiceStates[chBase + v], ch, multiplyFrame.data(),
                                         numSamples, targetFreq, currentRetuneSpeed);

                    for (int i = 0; i < numSamples; ++i)
                        multiplyFrameSum[i] += multiplyFrame[i];
                }

                // Sum of N voices, divided by count so N full-scale voices come
                // back at ~full scale instead of clipping.
                const float gain = 1.0f / static_cast<float>(numVoices);
                for (int i = 0; i < numSamples; ++i)
                    outData[i] = multiplyFrameSum[i] * gain;

                targetPitch.store(tuningManager->getActiveNotes().empty()
                    ? 0.0f : tuningManager->getActiveNotes()[0]);
            }
        }
    }

    // === STEP 5: Wet/Dry mix ===
    // The wet path is delayed by `latency = fftSize - hopSize` samples (the OLA
    // buffer), so the dry path must be delayed by the same amount before mixing
    // or the two comb/cancel at wet < 1.0. We already report setLatencySamples()
    // and the host applies one global delay; the two branches must still be
    // mutually aligned *here*.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* outData    = buffer.getWritePointer(ch);
        auto* dryData    = dryBuffer.getReadPointer(ch);
        auto* wetData    = processedBuffer.getReadPointer(ch);
        auto& delay      = dryDelays[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            float alignedDry = delay.writeThenRead(dryData[i]);
            outData[i] = (1.0f - currentWetDry) * alignedDry + currentWetDry * wetData[i];
        }
    }
}

//==============================================================================
bool SuperautotuneAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SuperautotuneAudioProcessor::createEditor()
{
    return new SuperautotuneAudioProcessorEditor (*this);
}

//==============================================================================
void SuperautotuneAudioProcessor::setTuningFilePath(const juce::String& path)
{
    tuningFilePath = path;
    if (path.isNotEmpty())
    {
        juce::File file(path);
        if (file.existsAsFile() && tuningManager)
            tuningManager->loadScaleFile(file);
    }
}

//==============================================================================
int SuperautotuneAudioProcessor::getNumSpectrumBins() const
{
    return spectrumAnalyzer ? spectrumAnalyzer->getNumBins() : 0;
}

void SuperautotuneAudioProcessor::getSpectrumMagnitudes(float* buffer, int numBins)
{
    if (spectrumAnalyzer)
        spectrumAnalyzer->getMagnitudes(buffer, numBins);
    else
        std::fill(buffer, buffer + numBins, 0.0f);
}

juce::StringArray SuperautotuneAudioProcessor::getActiveNoteNames()
{
    return tuningManager ? tuningManager->getActiveNoteNames() : juce::StringArray();
}

float SuperautotuneAudioProcessor::getNoteFrequency(int midiNote) const
{
    return tuningManager ? tuningManager->getNoteFrequency(midiNote) : 0.0f;
}

float SuperautotuneAudioProcessor::getNoteCents(int midiNote) const
{
    return tuningManager ? tuningManager->getNoteCents(midiNote) : 0.0f;
}

bool SuperautotuneAudioProcessor::hasTuningFile() const
{
    return tuningManager && tuningManager->hasTuningFile();
}

juce::String SuperautotuneAudioProcessor::getScaleDescription() const
{
    return tuningManager ? tuningManager->getScaleDescription() : juce::String();
}

void SuperautotuneAudioProcessor::toggleNote(int midiNote)
{
    if (tuningManager) tuningManager->toggleNote(midiNote);
}

bool SuperautotuneAudioProcessor::isNoteEnabled(int midiNote) const
{
    return tuningManager ? tuningManager->isNoteEnabled(midiNote) : false;
}

//==============================================================================
void SuperautotuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Store the tuning file path and the note selection. The selection lives in
    // TuningManager rather than in a parameter (128 individual parameters would be
    // absurd), so without this it was lost on every reload.
    state.setProperty("tuningFilePath", tuningFilePath, nullptr);
    if (tuningManager)
    {
        state.setProperty("noteMask", tuningManager->getNoteMask(), nullptr);

        // The mapping itself, not just where it came from: this is what makes the
        // tuning survive a reload without the file.
        state.setProperty("tuningTable", tuningManager->getTuningTable(), nullptr);
    }
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SuperautotuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xmlState);
            apvts.replaceState (tree);

            // Restore the tuning. The mapping is carried in the saved state, so this
            // does not need the file. Re-reading the file is only a fallback for
            // states saved before the table was stored, and for a file that has
            // since been edited on disk.
            tuningFilePath = tree.getProperty("tuningFilePath", "").toString();

            if (tuningManager)
            {
                if (tree.hasProperty("tuningTable"))
                {
                    tuningManager->setTuningTable(tree.getProperty("tuningTable").toString());
                }
                else if (tuningFilePath.isNotEmpty())
                {
                    juce::File file(tuningFilePath);
                    if (file.existsAsFile())
                        tuningManager->loadScaleFile(file);
                }
            }

            // Restore the note selection. After the file load, so a stale mask
            // cannot be overwritten by the load — loading a tuning file deliberately
            // leaves the selection alone, and so does this.
            if (tuningManager)
                if (tree.hasProperty("noteMask"))
                    tuningManager->setNoteMask(tree.getProperty("noteMask").toString());
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuperautotuneAudioProcessor();
}
