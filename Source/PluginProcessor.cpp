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

void applyHammingWindow(float* data, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Apply the Hamming window
        data[i] *= 0.54f - 0.46f * cosf(2.0f * juce::MathConstants<float>::pi * i / (numSamples - 1));
    }
}

void applyBlackmanHarrisWindow(float* data, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Apply the Blackman-Harris window
        float a0 = 0.35875f;
        float a1 = 0.48829f;
        float a2 = 0.14128f;
        float a3 = 0.01168f;
        
        data[i] *= a0 - a1 * cosf(2.0f * juce::MathConstants<float>::pi * i / (numSamples - 1))
                      + a2 * cosf(4.0f * juce::MathConstants<float>::pi * i / (numSamples - 1))
                      - a3 * cosf(6.0f * juce::MathConstants<float>::pi * i / (numSamples - 1));
    }
}


float lastphase = 0;
float lastFreq;

class Scale {
    public:
    //first arg: an array of frequency ratios for the scale
    //2nd arg: home frequency of the scale
    Scale(const std::vector<float>& values, float freq) : scale(values), baseFrequency(reduceF(freq)) {}

    float findNote(float inputFreq)
    {
        if(inputFreq == 0)
        {
            return 0.0f;
        }
        float minDifference = 120000000000;
        int scaleIndex;
        int power;
        float relativeFrequency;
        float octave;
        for (size_t i = 0; i < scale.size(); ++i)
        {
            for(int p = 0; p < 10; ++p){
                octave = baseFrequency*std::pow(2, p);
                float difference = std::abs(1200*log2(inputFreq/(octave*scale[i])));
                if (difference<minDifference)
                {
                    minDifference = difference;
                    scaleIndex = i;
                    relativeFrequency = baseFrequency*std::pow(2, p);

                }
            }
        }
        float outputNote = relativeFrequency * scale[scaleIndex];
        return outputNote;
    }
    private:
        float reduceF(float freq)
    {
        while(freq > 30)
        {
            freq/=2;
        }
        return freq;
    }
    float baseFrequency;
    std::vector<float> scale;
};

std::vector<float> _5lim = {9.0f/8.0f, 5.0f/4.0f, 4.0f/3.0f, 3.0f/2.0f, 5.0f/3.0f, 15.0f/8.0f, 2.0f/1.0f};
Scale _5lim_500hz(_5lim, 500);  


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
    int order = 3+std::ceil(std::log2(buffer.getNumSamples()));
    juce::dsp::FFT fft(order);

    if (buffer.getNumChannels() != 0 && buffer.getNumSamples() != 0)
    {
        for (int channel = 1; channel < totalNumInputChannels; ++channel)
        {


            auto* channelData = buffer.getWritePointer(channel);


            if (channelData == nullptr) {
                DBG("Error: channelData pointer is null!");
                return;
            }
            else {

                //applyHammingWindow(channelData, buffer.getNumSamples());
                
                std::vector<float> fftData;
                
                fftData.resize(std::pow(2, order+1), 0.0f);
                
                std::memcpy(fftData.data(), channelData, buffer.getNumSamples() * sizeof(float));

                applyHammingWindow(fftData.data(), std::pow(2, order+1));
 
               
                
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

                // Calculate frequency and print it
                float frequency = (maxBin * sampleRate) / fft.getSize();
                
                std::cout << "Frequency before scale: " << frequency << " Hz" << std::endl;




                //idk why but sometimes frequency is negative on startup
                if(frequency < 0){
                    frequency = 0;
                }

                //clear the buffer
                std::fill(channelData, channelData + buffer.getNumSamples(), 0.0f);
 
                //map to scale
                frequency = _5lim_500hz.findNote(frequency);
                std::cout << "Frequency after scale: " << frequency << " Hz" << std::endl;

                float phase = lastphase;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    //create sine wave of strongest frequency
                    phase += juce::MathConstants<float>::pi * frequency / sampleRate;
                    phase = std::fmod(phase, 2.0f * juce::MathConstants<float>::pi);
                    channelData[i] = sin(phase);
                }
                lastphase = phase;

                lastFreq = frequency;


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
