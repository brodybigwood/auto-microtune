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
float lastFreq = -1;

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

struct oscillator{
    float frequency;
    float phase = 0.0f;
};



void SuperautotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    float sampleRate = getSampleRate();
int order = 1+std::ceil(std::log2(buffer.getNumSamples()));
juce::dsp::FFT fft(order);
static oscillator osc1;
static oscillator osc2;
//output
std::vector<std::complex<float>> fft_in(1 << order, {0.0f, 0.0f});
std::vector<std::complex<float>> fft_out(1 << order, {0.0f, 0.0f});
std::vector<float> phases(pow(2,order), 0.0f);

bool phaseSet = false;





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



    if (buffer.getNumChannels() != 0 && buffer.getNumSamples() != 0)
    {
        for (int channel = 1; channel < totalNumInputChannels; ++channel)
        {
std::cout << "263" << std::endl;
int numSamples = buffer.getNumSamples();
auto* channelData = buffer.getWritePointer(channel);

auto nextPhaseLambda = [numSamples, sampleRate](float frequency, float phase) -> float
{
    // phase increment per sample
    float phaseIncrement = 2 * M_PI * frequency / sampleRate;
    int samplesLeft = numSamples - static_cast<int>(phase * numSamples / (2 * M_PI));
    phase += phaseIncrement * samplesLeft;
    
    if (phase >= M_PI)
        phase -= 2 * M_PI;
    else if (phase < -1 * M_PI)
        phase += 2 * M_PI;

    return phase;
};

auto sinc = [](float x) -> float
{
    if (x == 0.0f) return 1.0f;
    return sin(M_PI * x) / (M_PI * x);
};




auto oscillate = [&channelData, sampleRate, numSamples](oscillator& osc) {

    float phase = osc.phase;
    float frequency = osc.frequency;

    for (int i = 0; i < numSamples; ++i) 
    {
        // Create the sine wave of the strongest frequency
        phase += juce::MathConstants<float>::pi * frequency / sampleRate;
        phase = std::fmod(phase, 2.0f * juce::MathConstants<float>::pi);
        channelData[i] += sin(phase);  // Directly assign the sine wave value to channelData
    }
    osc.phase = phase;  // Update oscillator phase after processing
    };

            if (channelData == nullptr) {
                DBG("Error: channelData pointer is null!");
                return;
            }
            else {
                
std::cout << "306" << std::endl;
                std::vector<float> fftData(static_cast<size_t>(pow(2, order + 1)), 0.0f);
std::cout << "308" << std::endl;
                for (size_t i = 0; i < buffer.getNumSamples(); ++i) {
                    // hamming window
                    //float windowValue = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (buffer.getNumSamples() - 1));
                    //copy windowed buffer samples into fft complex array
                    fft_in[i] = std::complex<float>(channelData[i], 0.0f);
                    fftData[i] = channelData[i];
                }


                //fill output with bins
                fft.perform(fft_in.data(), fft_out.data(), false);

           std::cout << "321" << std::endl;     
                //fft.performFrequencyOnlyForwardTransform(fftData.data());
                //get bin with max magnitude

                float maxMagnitude = 0.0f;
                int maxBin = -1;
                float maxFreq = 1500;
                int maxBinIndex = static_cast<int>((2 * maxFreq / sampleRate) * fft_out.size());

                for (int i = 0; i < maxBinIndex; ++i)
                {
                    if (std::abs(fft_out[i]) > maxMagnitude)
                    {
                        maxMagnitude = std::abs(fft_out[i]);
                        maxBin = i;
                    }
                }

                // Calculate frequency and print it
                float frequency = 2 * (maxBin * sampleRate) / fft_out.size();
                
                std::cout << "Frequency before scale: " << frequency << " Hz" << std::endl;

            
                //idk why but sometimes frequency is negative on startup
                if(frequency < 0){
                    frequency = 0;
                }

                //clear the buffer
                std::fill(channelData, channelData + buffer.getNumSamples(), 0.0f);

                //map to scale
                float newFrequency = _5lim_500hz.findNote(frequency);
                float correctionRatio = newFrequency/frequency;
                //correctionRatio = 1;

                //change pitch
                std::vector<std::complex<float>> shifted_bins(fft_out.size(), {0.0f, 0.0f});

                for(int bin = 0; bin < fft_out.size(); ++bin)
                {
                    float binFrequency = static_cast<float>(bin) * (sampleRate / fft_out.size());
                    float shiftedFrequency = 2*binFrequency * correctionRatio;
                    //shifted_bins[bin] = fft_out[bin];
                        //phases[bin] = nextPhaseLambda(shiftedFrequency, phases[bin]);
                    
                    float shiftedBin = correctionRatio * static_cast<float>(bin);


                    //no interpolation
                    //int intShiftedBin = static_cast<int>(shiftedBin);
                    //intShiftedBin = bin;

                    int intShiftedBinLow = static_cast<int>(std::floor(shiftedBin));
                    int intShiftedBinHigh = intShiftedBinLow + 1;

                    float distanceLow = shiftedBin - intShiftedBinLow;
                    float distanceHigh = 1.0f - distanceLow;

                    float weightLow = sinc(distanceLow);
                    float weightHigh = sinc(distanceHigh);

                    if (intShiftedBinLow < fft_out.size()) 
                    {
                        shifted_bins[intShiftedBinLow] = fft_out[bin] * weightLow;
                        phases[intShiftedBinLow] = nextPhaseLambda(shiftedFrequency, phases[bin]);
                    }

                    if (intShiftedBinHigh < fft_out.size()) 
                    {
                        shifted_bins[intShiftedBinHigh] = fft_out[bin] * weightHigh;
                    }
                    
/*
                    if (intShiftedBin < fft_out.size())
                    {

                        shifted_bins[intShiftedBin] = std::complex<float> {fft_out[bin].real(),phases[intShiftedBin]};
                        phases[intShiftedBin] = nextPhaseLambda(shiftedFrequency, phases[bin]);
                        //shifted_bins[fftData.size() - intShiftedBin - 1] = std::conj(shifted_bins[intShiftedBin]);
                    }*/
                }
                //shifted_bins = fft_out;

                
                //test: put fft data back into buffer
                fft.perform(shifted_bins.data(), fft_out.data(), true);
                
                for (size_t i = 0; i < buffer.getNumSamples(); ++i) 
                {
                    channelData[i] = std::abs(fft_out[i]);
                }
                std::cout << channelData[buffer.getNumSamples()-1] <<std::endl;

                /* SIMPLE OSCILLATION
                //map to scale
                frequency = _5lim_500hz.findNote(frequency);
                std::cout << "Frequency after scale: " << frequency << " Hz" << std::endl;

                if (frequency != lastFreq) {
                    
                    osc1.frequency = frequency;
                }
                
                oscillate(osc1);


                lastFreq = frequency;

                */
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
