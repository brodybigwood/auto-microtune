/*
  ==============================================================================
    super-autotune — Spectrum Analyzer implementation
  ==============================================================================
*/

#include "SpectrumAnalyzer.h"

void SpectrumAnalyzer::prepare(int fftSizeIn, double sampleRateIn)
{
    fftSize    = fftSizeIn;
    sampleRate = sampleRateIn;
    fftOrder   = static_cast<int>(std::log2(fftSize));
    numBins    = fftSize / 2;

    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftBuffer.resize(2 * fftSize, 0.0f);
    smoothedMags.resize(numBins, 0.0f);
}

void SpectrumAnalyzer::feed(const float* windowedSamples, int size)
{
    if (size > fftSize)
        size = fftSize;

    // Prepare FFT buffer
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    std::copy(windowedSamples, windowedSamples + size, fftBuffer.begin());

    // Forward FFT (magnitude only is more efficient)
    fft->performFrequencyOnlyForwardTransform(fftBuffer.data());

    // Smooth the magnitude spectrum (exponential moving average)
    juce::ScopedLock lock(mutex);
    constexpr float smoothFactor = 0.3f;  // new data weight
    for (int i = 0; i < numBins; ++i)
    {
        smoothedMags[i] = (1.0f - smoothFactor) * smoothedMags[i]
                        + smoothFactor * fftBuffer[i];
    }
}

void SpectrumAnalyzer::getMagnitudes(float* buffer, int numOutputBins)
{
    juce::ScopedLock lock(mutex);
    int n = std::min(numOutputBins, numBins);
    std::copy(smoothedMags.begin(), smoothedMags.begin() + n, buffer);
}
