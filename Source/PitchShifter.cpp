/*
  ==============================================================================
    super-autotune — Pitch shifter implementation

    Period-synchronous resampling. See PitchShifter.h for the method and for why
    the previous time-domain attempts (PSOLA and WSOLA) produced a harmonic comb.
  ==============================================================================
*/

#include "PitchShifter.h"
#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Rising half of a raised cosine over one fragment. The weights (1-w) and w
    // sum to exactly 1 at every sample, which is what keeps the output gain
    // constant across a crossfade without any per-sample normalisation.
    float xfadeAt (int i, int fragment)
    {
        return 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                        * static_cast<float> (i) / static_cast<float> (fragment)));
    }

    // Catmull-Rom cubic interpolation between v[1] and v[2] (v[0] and v[3] are
    // the neighbouring samples that set the slope). Written as the standard
    // polynomial form. Reading at a fractional index is what gives the shift
    // ratio its resolution: quantising the read to whole samples would round the
    // output pitch to SR/round(step), which is several cents at high frequencies.
    float interpolate (const float* v, float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;

        return 0.5f * (2.0f * v[1]
                     + (-v[0] + v[2]) * t
                     + (2.0f * v[0] - 5.0f * v[1] + 4.0f * v[2] - v[3]) * t2
                     + (-v[0] + 3.0f * v[1] - 3.0f * v[2] + v[3]) * t3);
    }

    int nextPow2Up (int v)
    {
        int p = 1;
        while (p < v) p <<= 1;
        return p;
    }
}

//==============================================================================
PitchShifter::PitchShifter()  = default;
PitchShifter::~PitchShifter() = default;

//==============================================================================
void PitchShifter::prepare (double sr, int /*maxBlockSize*/, int numChannels)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    numChans   = std::max (1, numChannels);

    maxPeriod = sampleRate / static_cast<double> (kMinF0);
    minPeriod = sampleRate / static_cast<double> (kMaxF0);

    // Constant latency of ~23 ms. It does not depend on the pitch or on the
    // shift ratio, so the host's delay compensation is exact.
    latency = static_cast<int> (std::lround (0.023 * sampleRate));

    // The ring has to cover the read window the jump logic can reach:
    //   [writeHead - latency - maxJump, writeHead - latency + maxJump]
    // with maxJump <= fragment + maxPeriod, plus room for the detector window.
    const int maxJump = fragment * 2 + static_cast<int> (maxPeriod) * 2;
    ringSize = nextPow2Up (2 * latency + 2 * maxJump + 2 * fragment);
    ringMask = ringSize - 1;

    xfadeFunc.resize (static_cast<size_t> (fragment));
    for (int i = 0; i < fragment; ++i)
        xfadeFunc[static_cast<size_t> (i)] = xfadeAt (i, fragment);

    // The detector window must span at least two periods of the lowest note we
    // accept, or low pitches cannot be measured at all.
    const int detectWindow = std::clamp (nextPow2Up (static_cast<int> (2.2 * maxPeriod)), 1024, 4096);

    channels.clear();
    channels.resize (static_cast<size_t> (numChans));
    for (auto& c : channels)
    {
        c.ring.assign (static_cast<size_t> (ringSize), 0.0f);
        c.written = 0;
        c.period  = 0.0f;
        c.f0      = 0.0f;
        c.detectWindow = std::min (detectWindow, ringSize);
        c.detectWin.assign (static_cast<size_t> (c.detectWindow), 0.0f);
        c.detector = std::make_unique<PitchDetector>();
        c.detector->prepare (c.detectWindow, sampleRate);
    }

    reset();
}

//==============================================================================
void PitchShifter::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.ring.begin(), c.ring.end(), 0.0f);
        c.written = 0;
        c.period  = 0.0f;
        c.f0      = 0.0f;
        if (c.detector != nullptr)
            c.detector->reset();
    }
}

PitchShifter::State PitchShifter::makeState() const
{
    return State{};
}

//==============================================================================
float PitchShifter::readCubic (int channel, double pos) const
{
    if (channel < 0 || channel >= static_cast<int> (channels.size()))
        return 0.0f;

    // Before the first sample ever pushed: silence, not a wrapped read.
    if (pos < 0.0)
        return 0.0f;

    const auto& ring = channels[static_cast<size_t> (channel)].ring;

    const auto   i    = static_cast<long long> (std::floor (pos));
    const float  a    = static_cast<float> (pos - static_cast<double> (i));
    const size_t base = static_cast<size_t> (i) & static_cast<size_t> (ringMask);

    float v[4];
    for (int k = 0; k < 4; ++k)
        v[k] = ring[(base + static_cast<size_t> (k) + static_cast<size_t> (ringSize) - 1u)
                    & static_cast<size_t> (ringMask)];

    return interpolate (v, a);
}

//==============================================================================
void PitchShifter::pushChannel (int channel, const float* input, int numSamples)
{
    if (channel < 0 || channel >= static_cast<int> (channels.size()))
        return;

    auto& c = channels[static_cast<size_t> (channel)];

    for (int i = 0; i < numSamples; ++i)
        c.ring[static_cast<size_t> (c.written + i) & static_cast<size_t> (ringMask)] = input[i];

    c.written += numSamples;
}

//==============================================================================
void PitchShifter::runDetector (int channel)
{
    auto& c = channels[static_cast<size_t> (channel)];
    const int n = c.detectWindow;

    if (c.written < n)
    {
        c.period = 0.0f;
        c.f0     = 0.0f;
        return;
    }

    // Most recent n samples, in chronological order.
    const long long start = c.written - n;
    for (int i = 0; i < n; ++i)
        c.detectWin[static_cast<size_t> (i)] =
            c.ring[static_cast<size_t> (start + i) & static_cast<size_t> (ringMask)];

    const float f = c.detector->detectPitch (c.detectWin.data(), n);

    c.f0     = f;
    c.period = (f > 0.0f) ? static_cast<float> (sampleRate / f) : 0.0f;
    if (c.period > 0.0f)
        c.periodLast = c.period;
}

//==============================================================================
void PitchShifter::onFragmentBoundary (State& state, int channel, float targetFreq, float retuneSpeed)
{
    auto& c = channels[static_cast<size_t> (channel)];

    // A crossfade started at the previous boundary has now run its full fragment:
    // the faded-in read position becomes the current one.
    if (state.xfade)
    {
        state.r1 = state.r2;
        state.xfade = false;
    }

    // --- Pitch estimate, every `estEvery` fragments -----------------------------
    if (++state.fragsSinceEst >= estEvery)
    {
        state.fragsSinceEst = 0;
        runDetector (channel);

        // Only the DETECTED pitch has to be inside kMinF0..kMaxF0 — that is what
        // the detector can measure. The target is allowed to be lower: shifting a
        // voice down to a note below 70 Hz is a legitimate correction, and gating
        // on the target silently disabled every downshift into that range.
        if (c.period > 0.0f && targetFreq > 0.0f)
        {
            state.ratioTarget = std::clamp (targetFreq / c.f0, kRatioLo, kRatioHi);
            state.lostEst = 0;
        }
        else if (++state.lostEst > 5)
        {
            // No usable pitch for a while: stop correcting and let the chain
            // become transparent again.
            state.lostEst = 5;
            state.ratioTarget = 1.0f;
        }
    }

    // --- Retune glide ----------------------------------------------------------
    // tau maps 0 -> instant (hard tune) .. 1 -> ~200 ms (natural). Applied per
    // fragment, which is where the ratio is allowed to change.
    const float tau = 0.005f + std::clamp (retuneSpeed, 0.0f, 1.0f) * 0.195f;
    const float fragTime = static_cast<float> (fragment) / static_cast<float> (sampleRate);
    const float k = (retuneSpeed <= 0.0f) ? 1.0f
                                          : 1.0f - std::exp (-fragTime / std::max (tau, 1.0e-4f));
    state.ratio += k * (state.ratioTarget - state.ratio);
    state.ratio = std::clamp (state.ratio, kRatioLo, kRatioHi);

    // --- Drift correction ------------------------------------------------------
    // The cursor must sit `latency` behind the write head. It will not, because
    // it advances at `ratio` per output sample while the input arrives at 1 per
    // sample; that difference is the drift, and it accumulates.
    //
    // The fix is to jump by a whole number of pitch periods. A whole period
    // matters: the waveform at the jumped-to position is then in phase with the
    // waveform at the jumped-from position, so the crossfade blends two copies
    // of the same signal and neither comb-filters nor changes the pitch. The
    // jump corrects the DURATION, not the pitch — the pitch is the read rate.
    // Prefer the current estimate, then the last good one, and only fall back to
    // the fragment length if a period has never been measured. The quantum must
    // be a whole number of periods or the crossfade is between two signals at
    // different phases, which is audible as a click.
    const double rawPeriod = (c.period > 0.0f) ? static_cast<double> (c.period)
                          : (c.periodLast > 0.0f) ? static_cast<double> (c.periodLast)
                          : static_cast<double> (fragment);
    double period = std::clamp (rawPeriod, minPeriod, maxPeriod);

    // At least one fragment, so a low pitch does not jump on every boundary.
    const double jump = period * std::ceil (static_cast<double> (fragment) / period);

    // Where the cursor should sit: `latency` behind the output sample we are
    // about to produce. See State::outCount for why the input write head is the
    // wrong clock to measure against.
    const double target = static_cast<double> (state.outCount) - static_cast<double> (latency);
    double d1 = state.r1 - target;

    // Guard band. The cursor must never overtake the newest input, because past
    // the write head there is nothing to read. Within one fragment the cursor can
    // advance by at most fragment * kRatioHi, and a crossfade spans a further
    // fragment, so that is the margin the test needs.
    const double guard = static_cast<double> (fragment) * (1.0 + static_cast<double> (kRatioHi)) + 1.0;

    double signedJump = 0.0;

    if (d1 > jump * 0.5 || d1 + guard >= static_cast<double> (latency))
    {
        // Reading too fast (or about to overtake the write head): jump back.
        state.xfade = true;
        state.r2 = state.r1 - jump;
        signedJump = -jump;
    }
    else if (d1 < -jump * 0.5)
    {
        // Reading too slowly: jump forward.
        state.xfade = true;
        state.r2 = state.r1 + jump;
        signedJump = jump;
    }

    if (traceJumps && signedJump != 0.0)
        std::printf ("  [jump] out=%lld r1=%.2f d1=%.2f period=%.3f(clamped %.3f raw %.3f) "
                     "jump=%.3f ratio=%.5f f0=%.2f\n",
                     state.outCount, state.r1, d1, period, period, rawPeriod, signedJump,
                     state.ratio, c.f0);
}

//==============================================================================
void PitchShifter::render (State& state, int channel, float* output, int numSamples,
                           float targetFreq, float retuneSpeed)
{
    if (channel < 0 || channel >= static_cast<int> (channels.size()))
    {
        std::fill (output, output + numSamples, 0.0f);
        return;
    }

    auto& c = channels[static_cast<size_t> (channel)];

    // Output sample n reads input sample n - latency, so the chain is a pure
    // delay of exactly `latency` at ratio 1 and the first samples output silence
    // covering the reported latency.
    //
    // This must be -latency, NOT written - latency. The cursor advances once per
    // output sample while `written` advances once per input sample, so starting
    // at -latency makes the two track (written - latency) exactly. Initialising
    // from the write head instead picks up however many samples the first block
    // already pushed, which makes the real latency block-size dependent.
    if (!state.started)
    {
        state.r1 = -static_cast<double> (latency);
        state.started = true;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (state.fi >= fragment)
        {
            onFragmentBoundary (state, channel, targetFreq, retuneSpeed);
            state.fi = 0;
        }

        if (state.xfade)
        {
            const float u1 = readCubic (channel, state.r1);
            const float u2 = readCubic (channel, state.r2);
            const float v  = xfadeFunc[static_cast<size_t> (state.fi)];
            output[i] = (1.0f - v) * u1 + v * u2;

            state.r1 += state.ratio;
            state.r2 += state.ratio;
        }
        else
        {
            output[i] = readCubic (channel, state.r1);
            state.r1 += state.ratio;
        }

        ++state.fi;
        ++state.outCount;
    }
}

//==============================================================================
float PitchShifter::getDetectedPitch (int channel) const
{
    if (channel < 0 || channel >= static_cast<int> (channels.size()))
        return 0.0f;
    return channels[static_cast<size_t> (channel)].f0;
}
