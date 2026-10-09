/*
  ==============================================================================
    super-autotune — YIN Pitch Detector implementation
  ==============================================================================
*/

#include "PitchDetector.h"

PitchDetector::PitchDetector() = default;
PitchDetector::~PitchDetector() = default;

//==============================================================================
void PitchDetector::prepare(int maxWindowSize, double sr)
{
    sampleRate = sr;

    // Pre-allocate difference-function buffers. The maximum lag we search is
    // maxWindowSize (giving f_min = sampleRate / maxWindowSize).
    diffBuffer.resize(maxWindowSize, 0.0f);
    cmndBuffer.resize(maxWindowSize, 0.0f);
}

void PitchDetector::reset()
{
    lastPeriod = 0.0f;
}

//==============================================================================
void PitchDetector::computeDifferenceFunction(const float* samples, int numSamples,
                                              int tauMax, std::vector<float>& diff)
{
    // Direct computation of the YIN difference function:
    //   d[tau] = sum_{j=0}^{numSamples-tau-1} (x[j] - x[j+tau])^2
    // This is the correct definition — it does NOT rely on the autocorrelation
    // approximation (which breaks for low frequencies / short windows).
    //
    // The sum runs to numSamples-tau-1 so that j+tau stays in range.
    diff[0] = 0.0f;
    for (int tau = 1; tau < tauMax; ++tau)
    {
        float sum = 0.0f;
        const int limit = numSamples - tau;
        for (int j = 0; j < limit; ++j)
        {
            float d = samples[j] - samples[j + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }
}

//==============================================================================
void PitchDetector::cumulativeMeanNormalized(const std::vector<float>& diff,
                                             int tauMax, std::vector<float>& cmnd)
{
    cmnd[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < tauMax; ++tau)
    {
        runningSum += diff[tau];
        if (runningSum < 1e-9f)
            cmnd[tau] = 1.0f;
        else
            cmnd[tau] = diff[tau] * static_cast<float>(tau) / runningSum;
    }
}

//==============================================================================
float PitchDetector::parabolicInterpolation(int tau, const std::vector<float>& cmnd)
{
    if (tau <= 0 || tau >= static_cast<int>(cmnd.size()) - 1)
        return static_cast<float>(tau);

    float y0 = cmnd[tau - 1];
    float y1 = cmnd[tau];
    float y2 = cmnd[tau + 1];

    float denom = y0 - 2.0f * y1 + y2;
    if (std::abs(denom) < 1e-9f)
        return static_cast<float>(tau);

    float shift = 0.5f * (y0 - y2) / denom;
    return static_cast<float>(tau) + shift;
}

//==============================================================================
float PitchDetector::detectPitch(const float* samples, int numSamples)
{
    // tauMax determines the minimum detectable frequency:
    //   f_min = sampleRate / tauMax
    int tauMax = static_cast<int>(sampleRate / minFrequency);
    if (tauMax > numSamples - 1)
        tauMax = numSamples - 1;

    // Minimum detectable lag (for max frequency)
    int tauMin = static_cast<int>(sampleRate / maxFrequency);
    if (tauMin < 2)
        tauMin = 2;

    // Step 1: Compute difference function (direct sum of squared differences)
    computeDifferenceFunction(samples, numSamples, tauMax, diffBuffer);

    // Step 2: Cumulative mean normalized difference
    cumulativeMeanNormalized(diffBuffer, tauMax, cmndBuffer);

    // Step 3: Find first dip below threshold
    int bestTau = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmndBuffer[tau] < threshold)
        {
            // Find the local minimum around this point
            bestTau = tau;
            while (tau + 1 < tauMax && cmndBuffer[tau + 1] < cmndBuffer[tau])
            {
                tau++;
                bestTau = tau;
            }
            break;
        }
    }

    if (bestTau < 0)
    {
        // No pitch detected (unvoiced)
        lastPeriod = 0.0f;
        return 0.0f;
    }

    // Step 4: Parabolic interpolation for sub-sample accuracy
    float period = parabolicInterpolation(bestTau, cmndBuffer);

    // Step 5: Smooth the period estimate (first-order IIR).
    // Large jumps are smoothed like any other change rather than passed through
    // untouched. The old "only smooth when the new value is close" gate let a
    // single octave error land in lastPeriod at full weight and poison every
    // following frame; rejecting real octave errors is the caller's job.
    constexpr float smoothFactor = 0.3f;  // lower = more smoothing
    if (lastPeriod > 0.0f)
        period = lastPeriod + smoothFactor * (period - lastPeriod);
    lastPeriod = period;

    // Step 6: Convert to frequency
    float frequency = static_cast<float>(sampleRate) / period;

    // Sanity check. The bounds are widened slightly so a signal sitting exactly
    // on the limit (70.00 Hz against a 70 Hz floor) is not discarded by a
    // fraction of a Hz of estimation error.
    if (frequency < minFrequency * 0.95f || frequency > maxFrequency * 1.05f)
        return 0.0f;

    return frequency;
}
