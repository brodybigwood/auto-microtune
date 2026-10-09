/*
  ==============================================================================
    super-autotune — YIN Pitch Detector
    FFT-accelerated autocorrelation-based monophonic pitch detection.
    Reference: de Cheveigné & Kawahara (2002), "YIN, a fundamental frequency
    estimator for speech and music."
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class PitchDetector
{
public:
    PitchDetector();
    ~PitchDetector();

    // Prepare for a given max window size and sample rate.
    // maxWindowSize should be the largest FFT size we'll use.
    void prepare(int maxWindowSize, double sampleRate);

    // Reset internal state (period smoothing history).
    void reset();

    // Detect the fundamental frequency in the given windowed audio.
    // Returns frequency in Hz, or 0.0f if no pitch is detected (unvoiced).
    // 'samples' must be 'numSamples' long.
    float detectPitch(const float* samples, int numSamples);

    // Set the YIN threshold. Lower = more sensitive (may produce false detections).
    // Higher = more conservative (may miss weak signals). Range: ~0.01 to 0.4.
    // Default: 0.15.
    void setThreshold(float newThreshold) { threshold = newThreshold; }

    // Get the last detected period (in samples) for debugging.
    float getLastPeriod() const { return lastPeriod; }

private:
    // Compute difference function via FFT-accelerated autocorrelation.
    // d[tau] = r[0] + r[0] - 2*r[tau] for tau in [0, tauMax]
    void computeDifferenceFunction(const float* samples, int numSamples, int tauMax,
                                   std::vector<float>& diff);

    // Cumulative mean normalized difference.
    void cumulativeMeanNormalized(const std::vector<float>& diff, int tauMax,
                                  std::vector<float>& cmnd);

    // Parabolic interpolation around a minimum for sub-sample accuracy.
    float parabolicInterpolation(int tau, const std::vector<float>& cmnd);

    // Internal state
    double sampleRate = 44100.0;
    float  threshold  = 0.15f;      // YIN absolute threshold
    float  lastPeriod = 0.0f;       // for smoothing
    // 70 Hz (~D2) is the bottom of the practical singing range. The shifter sizes
    // its ring, detector window and latency from this, so the two must agree.
    float  minFrequency = 70.0f;
    float  maxFrequency = 1200.0f;  // above this, return 0

    // Scratch buffers (pre-allocated in prepare)
    std::vector<float> diffBuffer;       // difference function values
    std::vector<float> cmndBuffer;       // cumulative mean normalized difference

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchDetector)
};
