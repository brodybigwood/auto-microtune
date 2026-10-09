/*
  ==============================================================================
    super-autotune — Pitch shifter

    The technique is period-synchronous resampling, as described in Fons
    Adriaensen's work on zita-at1. Stated as a method rather than as code:

      * Read the input at `ratio` samples per output sample. That read rate IS
        the pitch shift, and it needs no spectral processing, so it cannot comb.
      * Work in short fragments. The read rate differs from the input rate, so
        the cursor slowly drifts away from where it belongs. At each fragment
        boundary, check the drift, and when it exceeds half a period, move the
        cursor by a whole number of pitch periods and crossfade across the next
        fragment.
      * Because the move is a whole number of periods, the waveform before and
        after the move is at the same phase, so the crossfade blends two copies
        of the same signal and stays at constant gain.
      * Re-estimate the pitch every few fragments.

    The move corrects the DURATION. The pitch comes from the read rate, and
    nothing else.

    Why this design and not PSOLA or WSOLA — both of which were tried here and
    produced a harmonic comb:

      * The output is built by reading the input at `ratio` samples per output
        sample. That rate IS the pitch shift. There is no window, no grain
        overlap, and no analysis hop, so there is no hop-vs-period relationship
        to get wrong and nothing to comb.

      * The read cursor drifts away from its target lag because the read rate
        differs from the input rate. The drift is removed by jumping the cursor
        by `period * ceil(fragment / period)` — an integer number of periods —
        so the waveform at the jumped-to position is the same phase as at the
        jumped-from position. The crossfade is therefore between two copies of
        the same waveform, and sums to unity gain by construction.

      * That is the whole trick: the jump fixes the DURATION, not the pitch. A
        naive overlap-add at a hop that is not a multiple of the period is what
        cancels every other harmonic and lands the output on the wrong octave.

    Latency is a fixed ring multiple and does not depend on the pitch or on the
    ratio, so the host's delay compensation is exact.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

// Defined in PitchDetector.h. Only the .cpp needs the definition.
class PitchDetector;

//==============================================================================
class PitchShifter
{
public:
    // Detector range. 70 Hz (~D2) is the bottom of the practical singing range;
    // the ring size, detector window and latency are all derived from it.
    static constexpr float kMinF0 = 70.0f;
    static constexpr float kMaxF0 = 1200.0f;

    // Clamp on the applied ratio, so a wild detector reading cannot drive the
    // read cursor at a silly rate.
    static constexpr float kRatioLo = 0.5f;
    static constexpr float kRatioHi = 2.0f;

    //==============================================================================
    // Per-voice state. One per (channel, voice): the read cursors and the ratio
    // are voice-specific, while the input ring is shared per channel.
    struct State
    {
        double r1 = 0.0;         // read cursor, absolute sample position
        double r2 = 0.0;         // second cursor while crossfading
        int    fi = 0;           // position within the current fragment
        bool   xfade = false;    // a jump is in progress
        bool   started = false;  // r1 has been initialised

        // Output samples produced so far. This is the clock the read cursor is
        // measured against: the input arrives in lockstep with the output, so
        // output n corresponds to input n. Using the input write head instead
        // would be a whole block ahead, because pushChannel runs before render.
        long long outCount = 0;

        int    fragsSinceEst = 0;
        int    lostEst = 0;      // consecutive fragments without a usable pitch
        float  ratioTarget = 1.0f;
        float  ratio = 1.0f;     // smoothed, applied per sample
    };

    PitchShifter();
    ~PitchShifter();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Clear all history. Keeps allocations.
    void reset();

    // Constant for a given sample rate, independent of pitch and ratio.
    int getLatencySamples() const { return latency; }

    // Fresh, correctly-sized State for one independent voice.
    State makeState() const;

    // Push one block of raw input for a channel. Call once per channel per
    // block, before rendering any voice of that channel.
    void pushChannel (int channel, const float* input, int numSamples);

    // Render numSamples into `output` for one voice. `channel` must have been
    // pushed this block. targetFreq <= 0 (nothing selected, or no note) means
    // no correction: the ratio glides back to 1 and the chain becomes a
    // delay-line passthrough rather than a switch, so there is nothing to click.
    void render (State& state, int channel, float* output, int numSamples,
                 float targetFreq, float retuneSpeed);

    // Most recent detected pitch for this channel in Hz, 0 if unvoiced.
    float getDetectedPitch (int channel) const;

    // Diagnostic: log every drift-correction jump. Off by default; the test
    // harness turns it on to see exactly what a click coincides with.
    bool traceJumps = false;

private:
    // Cubic-interpolated read from a channel's input ring.
    float readCubic (int channel, double pos) const;

    // End-of-fragment housekeeping: complete any crossfade, refresh the pitch
    // estimate if due, and decide whether the read cursor has drifted far enough
    // to need a whole-period jump.
    void onFragmentBoundary (State& state, int channel, float targetFreq, float retuneSpeed);

    void runDetector (int channel);

    //==============================================================================
    double sampleRate = 44100.0;
    int    numChans   = 2;
    int    ringSize   = 4096;
    int    ringMask   = 4095;
    int    fragment   = 128;    // output samples per fragment
    int    estEvery   = 4;      // fragments between pitch estimates
    int    latency    = 1024;   // ~23 ms at 44.1 kHz; constant for the config

    double minPeriod  = 0.0;
    double maxPeriod  = 0.0;

    // Rising half of a raised cosine, one fragment long.
    std::vector<float> xfadeFunc;

    struct Channel
    {
        std::vector<float> ring;
        long long written = 0;      // absolute samples pushed
        float     period  = 0.0f;   // measured period in samples (0 = no estimate)
        float     f0      = 0.0f;

        // Last period that was successfully measured. The jump quantum needs a
        // whole number of periods; falling back to the fragment length when the
        // estimate is momentarily lost makes jumps land at an arbitrary phase,
        // which clicks if one lands as the voice comes back. A stale period is a
        // much smaller error than a quantum that was never a period at all.
        float     periodLast = 0.0f;

        std::unique_ptr<PitchDetector> detector;
        int detectWindow = 2048;
        std::vector<float> detectWin;
    };
    std::vector<Channel> channels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchShifter)
};
