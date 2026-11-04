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
    energyPrefix.resize(static_cast<size_t>(maxWindowSize) + 1, 0.0f);

    // FFT size for the accelerated difference function: enough to hold the
    // integration window plus the longest lag we search, so the correlation does
    // not wrap around.
    const int window   = maxWindowSize / 2;
    const int tauMax   = static_cast<int>(sampleRate / minFrequency);
    int       size     = 1;
    while (size < window + tauMax + 1)
        size <<= 1;

    fftSize  = size;
    fftOrder = static_cast<int>(std::log2(size));
    fft      = std::make_unique<juce::dsp::FFT>(fftOrder);

    const std::complex<float> zero {};
    xa.assign(static_cast<size_t>(fftSize), zero);
    xb.assign(static_cast<size_t>(fftSize), zero);
    xc.assign(static_cast<size_t>(fftSize), zero);
    xr.assign(static_cast<size_t>(fftSize), zero);
}

void PitchDetector::reset()
{
    lastPeriod = 0.0f;
}

//==============================================================================
void PitchDetector::computeDifferenceFunction(const float* samples, int numSamples,
                                              int tauMax, std::vector<float>& diff)
{
    // The YIN difference function, expanded so it can be computed with FFTs:
    //
    //   d(tau) = sum_j (x[j] - x[j+tau])^2      over a fixed window W
    //          = e0 + e1(tau) - 2 r(tau)
    //
    // e0 is constant, e1(tau) is the windowed energy at offset tau (a running sum),
    // and r(tau) is the cross-correlation, which one FFT pair gives for every lag
    // at once.
    //
    // The direct sum is O(numSamples * tauMax) — over a million multiply-adds at
    // 44.1 kHz — and this runs about 86 times a second per channel, which was
    // enough to overrun the audio callback and cause dropouts. This is
    // O(fftSize log fftSize) instead.
    const int W = numSamples / 2;

    // Start the comparison as late as the window allows, so the estimate uses the
    // newest audio rather than the oldest. The correlation reads up to
    // start + W + tauMax - 1, and placing that at the end of the buffer is what
    // removes the lag an early window would add on a gliding pitch.
    const int start = std::max(0, numSamples - 1 - tauMax - W + 1);

    // Running sum of squares, so e1(tau) costs a subtraction.
    energyPrefix[0] = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        energyPrefix[static_cast<size_t>(i) + 1] =
            energyPrefix[static_cast<size_t>(i)] + samples[i] * samples[i];

    const float e0 = energyPrefix[static_cast<size_t>(start + W)]
                   - energyPrefix[static_cast<size_t>(start)];

    std::fill(xa.begin(), xa.end(), std::complex<float>());
    std::fill(xb.begin(), xb.end(), std::complex<float>());

    // a = x[start, start+W), b = x[start, start+W+tauMax), so lag tau reads b[j+tau].
    for (int i = 0; i < W; ++i)
        xa[static_cast<size_t>(i)] = samples[start + i];
    for (int i = 0; i < W + tauMax && start + i < numSamples; ++i)
        xb[static_cast<size_t>(i)] = samples[start + i];

    fft->perform(xa.data(), xc.data(), false);   // A
    fft->perform(xb.data(), xr.data(), false);   // B
    for (int i = 0; i < fftSize; ++i)
        xc[static_cast<size_t>(i)] = std::conj(xc[static_cast<size_t>(i)])
                                   * xr[static_cast<size_t>(i)];
    fft->perform(xc.data(), xr.data(), true);    // r(tau) in the real part

    diff[0] = 0.0f;
    for (int tau = 1; tau < tauMax; ++tau)
    {
        const float e1 = energyPrefix[static_cast<size_t>(start + tau + W)]
                       - energyPrefix[static_cast<size_t>(start + tau)];
        diff[static_cast<size_t>(tau)] = e0 + e1
                                       - 2.0f * xr[static_cast<size_t>(tau)].real();
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

    // Step 5: Light smoothing of the period estimate.
    //
    // This used to be 0.3, which on a gliding pitch adds about 38 ms of lag — and
    // lag on a glide is a constant pitch error, not a tracking failure: at
    // 600 cents/s it is ~23 cents all by itself. The shifter already low-passes the
    // ratio with the retune setting, so heavy smoothing here only costs accuracy.
    constexpr float smoothFactor = 0.85f;  // lower = more smoothing
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
