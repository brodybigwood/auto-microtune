/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>


//==============================================================================
SuperautotuneAudioProcessor::SuperautotuneAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

SuperautotuneAudioProcessor::~SuperautotuneAudioProcessor()
{
}

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
    return 0.0;
}

int SuperautotuneAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SuperautotuneAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SuperautotuneAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SuperautotuneAudioProcessor::getProgramName (int index)
{
    return {};
}

void SuperautotuneAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SuperautotuneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void SuperautotuneAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SuperautotuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void SuperautotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    float sampleRate = getSampleRate();


    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());


    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    int order = 1 + std::ceil(std::log2(buffer.getNumSamples()));
    juce::dsp::FFT fft(order);
    if (buffer.getNumChannels() != 0 && buffer.getNumSamples() != 0)
    {
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {


            auto* channelData = buffer.getWritePointer(channel);

            for (int i = 1; i < buffer.getNumSamples(); ++i) {
                // A simple low-pass filter (e.g., simple moving average or basic FIR)
                channelData[i] = 0.9 * channelData[i - 1] + 0.1 * channelData[i];
            }

            if (channelData == nullptr) {
                DBG("Error: channelData pointer is null!");
                return;
            }
            else {
                
                std::vector<float> fftData;
                
                fftData.resize(2*fft.getSize(), 0.0f);
                
                std::memcpy(fftData.data(), channelData, buffer.getNumSamples() * sizeof(float));
 
               
                
                // transform to frequency domain
                fft.performRealOnlyForwardTransform(fftData.data());
                
                float maxMagnitude = 0.0f;
                int maxBin = -1;
                

                for (int i = 0; i < fftData.size(); ++i)
                {
                    if (fftData[i] > maxMagnitude)
                    {
                        maxMagnitude = fftData[i];
                        maxBin = i;
                    }
                }

                float nyquistFrequency = sampleRate / 2.0f;
                float cutoffFrequency = nyquistFrequency - 1000;

                float frequency = (maxBin * sampleRate) / fft.getSize();
                /**/
                //float frequency = 500;
                float bufferLengthSeconds = buffer.getNumSamples() / sampleRate;

                float freqInBuffer = frequency * bufferLengthSeconds;
                if (frequency > 20) 
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        //create sine wave of strongest frequency
                        float sample = 3.14159 * frequency * i / sampleRate;
                        channelData[i] = sin(sample);
                    }
                }
            }
            
        }
    }
}

//==============================================================================
bool SuperautotuneAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SuperautotuneAudioProcessor::createEditor()
{
    return new SuperautotuneAudioProcessorEditor (*this);
}

//==============================================================================
void SuperautotuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SuperautotuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuperautotuneAudioProcessor();
}
