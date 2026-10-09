/*
  ==============================================================================
    super-autotune — Spectrum Analyzer for GUI display
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

//==============================================================================
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer() = default;
    ~SpectrumAnalyzer() = default;

    void prepare(int fftSizeIn, double sampleRateIn);

    // Feed a windowed buffer of audio for analysis.
    void feed(const float* windowedSamples, int size);

    // Copy the smoothed magnitude spectrum. Buffer must be at least numBins long.
    // numOutputBins should be <= fftSize/2.
    void getMagnitudes(float* buffer, int numOutputBins);

    int getNumBins() const { return fftSize / 2; }

private:
    int fftSize    = 1024;
    int fftOrder   = 10;
    int numBins    = 512;
    double sampleRate = 44100.0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuffer;        // 2*fftSize for real-only FFT
    std::vector<float> smoothedMags;     // smoothed magnitude spectrum

    juce::CriticalSection mutex;         // thread safety for GUI reads

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
