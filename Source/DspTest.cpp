/*
  ==============================================================================
    super-autotune — DSP self-test harness (NOT part of the plugin build)

    Drives PitchShifter/PitchDetector and asserts the properties a tuner must
    have. The checks are deliberately quantitative (cents, dB) because the two
    previous shifter implementations both passed "does it produce SOME
    periodicity" while being audibly wrong: a harmonic-rich source shifted by
    x1.5 came out at 2x the input, with ZERO energy at the requested pitch.

    Build:  cmake --build build-native --target super-autotune_dsptest
    Run:    ./build-native/super-autotune_dsptest_artefacts/super-autotune_dsptest
  ==============================================================================
*/

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "ScaleFileParser.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static constexpr double SR    = 44100.0;
static constexpr int    BLOCK = 256;
static constexpr int    FFT_N = 32768;   // order 15 -> 1.35 Hz bins

static int failures = 0;
static int checks   = 0;

static void check (bool cond, const std::string& msg, double got = 0.0, double expected = 0.0)
{
    ++checks;
    if (cond)
        std::printf ("  PASS: %s\n", msg.c_str());
    else
    {
        ++failures;
        std::printf ("  FAIL: %s  (got %.6f, expected %.6f)\n", msg.c_str(), got, expected);
    }
}

static double cents (double measured, double target)
{
    if (measured <= 0.0 || target <= 0.0) return 1e9;
    return 1200.0 * std::log2 (measured / target);
}

//==============================================================================
// Frequency by windowed FFT + log-magnitude parabolic interpolation. Accurate to
// well under a cent, which the old +/-10 Hz checks were far too coarse to catch
// an octave or a 12-cent quantisation.
static double measureF0 (const float* data, int n, double sr, double lo, double hi)
{
    constexpr int order = 15;
    constexpr int N     = 1 << order;

    std::vector<float> buf (2 * N, 0.0f);
    const int use = std::min (n, N);
    if (use < 1024) return 0.0;

    for (int i = 0; i < use; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / use);   // Hann
        buf[static_cast<size_t> (i)] = static_cast<float> (data[i] * w);
    }

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);

    const int kLo = std::max (1, static_cast<int> (std::floor (lo * N / sr)));
    const int kHi = std::min (N / 2 - 2, static_cast<int> (std::ceil (hi * N / sr)));

    int    kBest = kLo;
    double mBest = -1.0;
    for (int k = kLo; k <= kHi; ++k)
        if (buf[static_cast<size_t> (k)] > mBest) { mBest = buf[static_cast<size_t> (k)]; kBest = k; }

    if (mBest <= 0.0) return 0.0;

    const double y0 = std::log (static_cast<double> (buf[static_cast<size_t> (kBest - 1)]) + 1e-12);
    const double y1 = std::log (static_cast<double> (buf[static_cast<size_t> (kBest)])     + 1e-12);
    const double y2 = std::log (static_cast<double> (buf[static_cast<size_t> (kBest + 1)]) + 1e-12);
    const double denom = y0 - 2.0 * y1 + y2;
    const double d = (std::fabs (denom) > 1e-12) ? 0.5 * (y0 - y2) / denom : 0.0;

    return (static_cast<double> (kBest) + d) * sr / static_cast<double> (N);
}

// Normalised autocorrelation at the lag of expectHz: ~1 means coherently periodic.
static double pitchClarity (const float* data, int n, double sr, double expectHz)
{
    if (expectHz < 1.0) return 0.0;
    const int lag = static_cast<int> (std::lround (sr / expectHz));
    if (lag < 2 || lag >= n / 2) return 0.0;

    double r0 = 0.0, rl = 0.0;
    for (int i = 0; i + lag < n; ++i)
    {
        r0 += static_cast<double> (data[i]) * data[i];
        rl += static_cast<double> (data[i]) * data[i + lag];
    }
    if (r0 < 1e-12) return 0.0;
    return std::fabs (rl / r0);
}

// Energy at one frequency, normalised by total energy.
static double binPower (const float* data, int n, double sr, double f)
{
    double re = 0.0, im = 0.0, e = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = 2.0 * M_PI * f * i / sr;
        re += data[i] * std::cos (a);
        im += data[i] * std::sin (a);
        e  += static_cast<double> (data[i]) * data[i];
    }
    if (e < 1e-12) return 0.0;
    return (re * re + im * im) / (static_cast<double> (n) * e);
}

//==============================================================================
static void makeSine (std::vector<float>& buf, double freq, double sr, double amp = 0.8)
{
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = static_cast<float> (amp * std::sin (2.0 * M_PI * freq * i / sr));
}

// Band-limited sawtooth: harmonics 1/n. Closer to a voice than a sine, and the
// case that broke both previous shifter implementations.
static void makeHarmonic (std::vector<float>& buf, double f0, double sr,
                          double maxHz = 12000.0, double amp = 0.5)
{
    const int H = std::max (1, std::min (400, static_cast<int> (maxHz / f0)));
    for (size_t i = 0; i < buf.size(); ++i)
    {
        double s = 0.0;
        for (int m = 1; m <= H; ++m)
            s += std::sin (2.0 * M_PI * m * f0 * i / sr) / m;
        buf[i] = static_cast<float> (s * amp);
    }
}

//==============================================================================
// Drive the shifter over a signal. The shifter owns its pitch detector, so the
// caller passes only the frequency it wants out.
static std::vector<float> runShift (const std::vector<float>& input, double targetHz,
                                    float retuneSpeed = 0.0f)
{
    PitchShifter shifter;
    shifter.prepare (SR, BLOCK, 1);
    auto state = shifter.makeState();

    const int total = static_cast<int> (input.size());
    std::vector<float> output (static_cast<size_t> (total), 0.0f);

    for (int i = 0; i < total; i += BLOCK)
    {
        const int n = std::min (BLOCK, total - i);
        shifter.pushChannel (0, input.data() + i, n);
        shifter.render (state, 0, output.data() + i, n,
                        static_cast<float> (targetHz), retuneSpeed);
    }
    return output;
}

//==============================================================================
// [1] PitchDetector at the production configuration.
static void testDetector()
{
    std::printf ("\n[1] PitchDetector (production config: 70-1200 Hz)\n");

    const int win = 2048;
    PitchDetector det;
    det.prepare (win, SR);

    const double freqs[] = { 1200.0, 660.0, 440.0, 261.63, 220.0, 110.0, 82.41, 70.0 };
    for (double hz : freqs)
    {
        std::vector<float> buf (static_cast<size_t> (win));
        makeSine (buf, hz, SR);
        det.reset();
        const float got = det.detectPitch (buf.data(), win);
        check (std::fabs (cents (got, hz)) < 25.0,
               "detects " + std::to_string (hz) + " Hz within 25 cents", got, hz);
    }

    std::vector<float> silence (static_cast<size_t> (win), 0.0f);
    det.reset();
    check (det.detectPitch (silence.data(), win) <= 0.0f, "returns 0 for silence");
}

//==============================================================================
// [2] Latency: exact, positive, and independent of the ratio.
static void testLatency()
{
    std::printf ("\n[2] Latency is exact and constant\n");

    auto impulseLatency = [] (double ratio)
    {
        PitchShifter shifter;
        shifter.prepare (SR, BLOCK, 1);
        auto state = shifter.makeState();

        const int total = 40000;
        const int n0    = 12000;
        std::vector<float> input (static_cast<size_t> (total), 0.0f);
        input[static_cast<size_t> (n0)] = 1.0f;

        std::vector<float> output (static_cast<size_t> (total), 0.0f);
        for (int i = 0; i < total; i += BLOCK)
        {
            const int n = std::min (BLOCK, total - i);
            shifter.pushChannel (0, input.data() + i, n);
            shifter.render (state, 0, output.data() + i, n,
                            static_cast<float> (220.0 * ratio), 0.0f);
        }

        int best = -1; float bestV = 0.0f;
        for (int i = 0; i < total; ++i)
            if (std::fabs (output[static_cast<size_t> (i)]) > bestV)
            { bestV = std::fabs (output[static_cast<size_t> (i)]); best = i; }

        return std::make_pair (best - n0, shifter.getLatencySamples());
    };

    const auto a = impulseLatency (1.0);
    const auto b = impulseLatency (1.5);
    const auto c = impulseLatency (0.75);

    std::printf ("  reported latency %d; measured ratio 1.0 -> %d, 1.5 -> %d, 0.75 -> %d\n",
                 a.second, a.first, b.first, c.first);

    check (a.first == a.second, "measured latency equals the reported latency", a.first, a.second);
    check (b.first == a.first && c.first == a.first,
           "latency does not move with the ratio", b.first, a.first);
}

//==============================================================================
// [3] Ratio 1 must be a transparent delayed copy.
static void testTransparency()
{
    std::printf ("\n[3] Ratio 1 is transparent\n");

    PitchShifter probe;
    probe.prepare (SR, BLOCK, 1);
    const int L = probe.getLatencySamples();

    const double freqs[] = { 82.41, 110.0, 220.0, 440.0, 660.0, 1200.0 };
    for (double f0 : freqs)
    {
        const int total = static_cast<int> (SR * 2.0);
        std::vector<float> input (static_cast<size_t> (total));
        makeSine (input, f0, SR);

        PitchShifter shifter;
        shifter.prepare (SR, BLOCK, 1);
        auto state = shifter.makeState();
        std::vector<float> output (static_cast<size_t> (total), 0.0f);

        for (int i = 0; i < total; i += BLOCK)
        {
            const int n = std::min (BLOCK, total - i);
            shifter.pushChannel (0, input.data() + i, n);

            // Ask for exactly the pitch the shifter itself detected. The ratio is
            // then target/detected == 1.0 exactly, so the chain must be a pure
            // delay. Asking for the TRUE f0 instead would make a fraction of a
            // cent of detector error a real (and correct) shift, and the test
            // would be measuring detector accuracy, not transparency.
            shifter.render (state, 0, output.data() + i, n, shifter.getDetectedPitch (0), 0.0f);
        }

        const int start = static_cast<int> (SR * 1.0);
        double worst = 0.0;
        for (int i = start; i + L + 1 < total; ++i)
            worst = std::max (worst, std::fabs (static_cast<double> (output[static_cast<size_t> (i + L)])
                                                - static_cast<double> (input[static_cast<size_t> (i)])));

        check (worst < 2.0e-2, "transparent at " + std::to_string (static_cast<int> (f0 + 0.5))
                               + " Hz (max err " + std::to_string (worst) + ")", worst, 0.0);
    }
}

//==============================================================================
// [4] Tuning accuracy in cents, on sines and on a harmonic-rich source.
static void testAccuracy()
{
    std::printf ("\n[4] Tuning accuracy (must be within 1 cent)\n");

    const double freqs[]  = { 82.41, 110.0, 220.0, 440.0, 466.16, 660.0, 880.0, 1200.0 };
    const double ratios[] = { 0.667, 0.9439, 1.0595, 1.5 };

    double worst = 0.0;
    std::string worstMsg;

    for (double f0 : freqs)
    {
        for (double r : ratios)
        {
            std::vector<float> input (static_cast<size_t> (SR * 2.0));
            makeSine (input, f0, SR);

            const auto output = runShift (input, f0 * r);

            const int start = static_cast<int> (output.size()) - FFT_N;
            if (start < 0) continue;

            const double target = f0 * r;
            const double got = measureF0 (output.data() + start, FFT_N, SR,
                                          target * 0.6, target * 1.6);
            const double err = std::fabs (cents (got, target));

            if (err > worst)
            {
                worst = err;
                worstMsg = std::to_string (static_cast<int> (f0 + 0.5)) + " Hz x"
                         + std::to_string (r) + " -> " + std::to_string (target)
                         + " measured " + std::to_string (got);
            }
        }
    }

    std::printf ("  worst case: %s (%.2f cents)\n", worstMsg.c_str(), worst);
    check (worst < 1.0, "all sine shifts land within 1 cent of target", worst, 1.0);
}

//==============================================================================
// [5] THE comb test. A harmonic-rich source shifted by x1.5 must put its energy
// on the SHIFTED grid. Both previous implementations failed exactly here: the
// output landed on 2x the input (440 for a 220 input) with zero energy at the
// requested 330, because overlaying period-aligned grains at a hop that is not a
// whole number of periods cancels every odd harmonic.
static void testHarmonicRich()
{
    std::printf ("\n[5] Harmonic-rich source lands on the shifted grid (the comb test)\n");

    const double f0 = 220.0, ratio = 1.5, target = f0 * ratio;

    std::vector<float> input (static_cast<size_t> (SR * 2.0));
    makeHarmonic (input, f0, SR);

    const auto output = runShift (input, target);
    const int start = static_cast<int> (output.size()) - FFT_N;
    const float* seg = output.data() + start;

    const double onTarget = binPower (seg, FFT_N, SR, target);
    const double onInput  = binPower (seg, FFT_N, SR, f0);
    const double onOctave = binPower (seg, FFT_N, SR, 2.0 * f0);
    const double db = 10.0 * std::log10 ((onTarget + 1e-12) / (onInput + 1e-12));

    std::printf ("  power@%.0f(input)=%.4f  @%.0f(shifted)=%.4f  @%.0f(2x input)=%.4f\n",
                 f0, onInput, target, onTarget, 2.0 * f0, onOctave);
    std::printf ("  shifted vs input grid: %.1f dB\n", db);

    check (db >= 10.0, "shifted grid exceeds the input grid by >= 10 dB", db, 10.0);

    const double clarity = pitchClarity (seg, FFT_N, SR, target);
    check (clarity > 0.8, "output is coherent at the shifted pitch", clarity, 1.0);

    const double got = measureF0 (seg, FFT_N, SR, target * 0.6, target * 1.6);
    check (std::fabs (cents (got, target)) < 10.0, "shifted pitch within 10 cents", got, target);

    // The real use case: low male voice, where harmonics are closest together.
    std::printf ("  low-f0 cases (the actual use case):\n");
    for (double low : { 82.41, 110.0, 130.81 })
    {
        std::vector<float> in2 (static_cast<size_t> (SR * 2.0));
        makeHarmonic (in2, low, SR);
        const auto out2 = runShift (in2, low * ratio);
        const int st2 = static_cast<int> (out2.size()) - FFT_N;
        const float* s2 = out2.data() + st2;
        const double tg = low * ratio;

        const double g = measureF0 (s2, FFT_N, SR, tg * 0.6, tg * 1.6);
        const double e = std::fabs (cents (g, tg));
        const double d = 10.0 * std::log10 ((binPower (s2, FFT_N, SR, tg) + 1e-12)
                                            / (binPower (s2, FFT_N, SR, low) + 1e-12));
        std::printf ("    %.2f Hz x1.5 -> %.2f Hz target, measured %.2f Hz (%.2f cents, %.1f dB over input grid)\n",
                     low, tg, g, e, d);
        check (e < 10.0, "  low f0 " + std::to_string (static_cast<int> (low + 0.5)) + " Hz shifts accurately", g, tg);
    }
}

//==============================================================================
// [6] Click-free, including a pitch-down and a hard target change.
static void testClicks()
{
    std::printf ("\n[6] Click-free\n");

    auto worstStep = [] (const std::vector<float>& out, int from)
    {
        double worst = 0.0;
        for (int i = std::max (1, from); i < static_cast<int> (out.size()); ++i)
            worst = std::max (worst, std::fabs (static_cast<double> (out[static_cast<size_t> (i)])
                                                - static_cast<double> (out[static_cast<size_t> (i - 1)])));
        return worst;
    };

    {
        std::vector<float> input (static_cast<size_t> (SR * 2.0));
        makeSine (input, 220.0, SR);
        const auto out = runShift (input, 110.0);   // x0.5, the heaviest correction
        const double w = worstStep (out, 20000);
        check (w < 0.25, "no clicks at ratio 0.5 (worst step " + std::to_string (w) + ")", w, 0.25);
    }

    {
        // Voiced -> unvoiced -> voiced: no target means no correction, and the
        // ratio glides rather than switching.
        PitchShifter shifter;
        shifter.prepare (SR, BLOCK, 1);
        auto state = shifter.makeState();

        const int total = static_cast<int> (SR * 2.0);
        std::vector<float> input (static_cast<size_t> (total), 0.0f);
        makeSine (input, 220.0, SR);

        // Envelope into and out of the silent stretch, 10 ms each way, continuous
        // at both ends. Hard-zeroing (or a ramp whose gain does not actually
        // reach 0 where the silence starts) puts a step in the INPUT, and this
        // test is supposed to measure whether the SHIFTER clicks — measuring my
        // own envelope instead wasted a debugging cycle.
        const int ramp = static_cast<int> (SR * 0.01);
        const int a = total / 3, b = 2 * total / 3;

        for (int i = a - ramp; i < a; ++i)          // gain 1 -> 0
            if (i >= 0)
                input[static_cast<size_t> (i)] *= 0.5f * (1.0f + std::cos (M_PI * (i - (a - ramp)) / ramp));
        for (int i = a; i < b; ++i)                 // silent
            input[static_cast<size_t> (i)] = 0.0f;
        for (int i = b; i < b + ramp; ++i)          // gain 0 -> 1
            if (i < total)
                input[static_cast<size_t> (i)] *= 0.5f * (1.0f - std::cos (M_PI * (i - b) / ramp));

        std::vector<float> output (static_cast<size_t> (total), 0.0f);
        for (int i = 0; i < total; i += BLOCK)
        {
            const int n = std::min (BLOCK, total - i);
            shifter.pushChannel (0, input.data() + i, n);
            shifter.render (state, 0, output.data() + i, n, 277.18f, 0.2f);
        }

        double w = 0.0; int at = 0;
        for (int i = 20001; i < total; ++i)
        {
            const double d = std::fabs (static_cast<double> (output[static_cast<size_t> (i)])
                                        - static_cast<double> (output[static_cast<size_t> (i - 1)]));
            if (d > w) { w = d; at = i; }
        }

        // Where the worst step sits relative to the silent stretch, so a real
        // defect is not confused with the ramp edges.
        std::printf ("  worst step %.4f at sample %d (ramp down %d, silence %d..%d, ramp up %d)\n",
                     w, at, total / 3 - static_cast<int> (SR * 0.01), total / 3, 2 * total / 3,
                     2 * total / 3 + static_cast<int> (SR * 0.01));

        check (w < 0.25, "no clicks across voiced->unvoiced->voiced (worst step "
                         + std::to_string (w) + ")", w, 0.25);
    }
}

//==============================================================================
// [7] Identical mono into two channels must give identical output.
static void testStereo()
{
    std::printf ("\n[7] Stereo identity\n");

    PitchShifter shifter;
    shifter.prepare (SR, BLOCK, 2);
    auto sL = shifter.makeState();
    auto sR = shifter.makeState();

    std::vector<float> input (static_cast<size_t> (SR));
    makeHarmonic (input, 220.0, SR);

    const int total = static_cast<int> (input.size());
    std::vector<float> outL (static_cast<size_t> (total), 0.0f), outR (static_cast<size_t> (total), 0.0f);

    for (int i = 0; i < total; i += BLOCK)
    {
        const int n = std::min (BLOCK, total - i);
        shifter.pushChannel (0, input.data() + i, n);
        shifter.pushChannel (1, input.data() + i, n);
        shifter.render (sL, 0, outL.data() + i, n, 293.66f, 0.0f);
        shifter.render (sR, 1, outR.data() + i, n, 293.66f, 0.0f);
    }

    double worst = 0.0;
    for (int i = 0; i < total; ++i)
        worst = std::max (worst, std::fabs (static_cast<double> (outL[static_cast<size_t> (i)])
                                            - static_cast<double> (outR[static_cast<size_t> (i)])));
    check (worst < 1.0e-9, "identical channels produce identical output", worst, 0.0);
}

//==============================================================================
// [8] Gain stays flat at every ratio (the crossfade weights sum to 1, so nothing
// should modulate the output at the jump rate).
static void testFlatGain()
{
    std::printf ("\n[8] Flat gain across ratios (must be within 3 dB)\n");

    const double ratios[] = { 0.5, 0.667, 0.8, 1.0, 1.25, 1.5, 2.0 };
    for (double r : ratios)
    {
        std::vector<float> input (static_cast<size_t> (SR * 2.0));
        makeSine (input, 220.0, SR);

        const auto output = runShift (input, 220.0 * r);

        const int win = 8192;
        double lo = 1e30, hi = 0.0;
        for (int w = 0; w < 3; ++w)
        {
            const int start = static_cast<int> (output.size()) - (3 - w) * (win + 8192);
            if (start < 0) continue;
            double e = 0.0;
            for (int i = 0; i < win; ++i)
                e += static_cast<double> (output[static_cast<size_t> (start + i)]) * output[static_cast<size_t> (start + i)];
            const double rms = std::sqrt (e / win);
            lo = std::min (lo, rms);
            hi = std::max (hi, rms);
        }

        const double db = (lo > 0.0) ? 20.0 * std::log10 (hi / lo) : 1e9;
        check (db < 3.0, "gain flat at ratio " + std::to_string (r), db, 0.0);
    }
}

//==============================================================================
// [9] The .tun parser. The [Tuning] section holds absolute CENTS, not Hz; reading
// them as Hz made every note snap to its own cent offset (a 24-TET file produced
// notes at 50 Hz, 100 Hz, ...). The spec's own 12-TET example must come out at
// exactly 440 Hz for note 69.
static void testTunParser()
{
    std::printf ("\n[9] .tun parser (cents, not Hz)\n");

    auto makeTable = [] (int centsPerStep)
    {
        juce::String s = "; generated\n[Tuning]\n";
        for (int n = 0; n < 128; ++n)
            s << "note " << n << " = " << (n * centsPerStep) << "\n";
        return s;
    };

    {
        TuningData d;
        check (ScaleFileParser::parseTun (makeTable (100), d), "12-TET table parses");
        check (std::fabs (d.midiFreqs[69] - 440.0) < 0.01,
               "12-TET note 69 -> 440 Hz", d.midiFreqs[69], 440.0);
        check (std::fabs (d.midiFreqs[60] - 261.6256) < 0.01,
               "12-TET note 60 -> 261.626 Hz", d.midiFreqs[60], 261.6256);
    }

    {
        TuningData d;
        check (ScaleFileParser::parseTun (makeTable (50), d), "24-TET table parses");
        check (std::fabs (d.midiCents[60] - 3000.0f) < 0.01f,
               "24-TET note 60 reports 3000 cents", d.midiCents[60], 3000.0);
        check (std::fabs (d.midiFreqs[24] - 16.3516) < 0.01,
               "24-TET note 24 -> 16.352 Hz (one 24-step octave)",
               d.midiFreqs[24], 16.3516);
        check (d.midiFreqs[60] > 40.0 && d.midiFreqs[60] < 50.0,
               "24-TET note 60 is ~46 Hz, not 60 'Hz' off a cent value",
               d.midiFreqs[60], 46.249);
    }
}

//==============================================================================
// [10] Detection across a continuous glide — the case that was never tested.
// Every other signal here is stationary, so none of them exercise a pitch that is
// moving between estimate frames. The tuner's whole correction is target/detected,
// so a detected pitch that does not track a glide puts the error straight into the
// output, and makes the chosen note (and hence the note list) jump around.
static void testGlideTracking()
{
    std::printf ("\n[10] Pitch detection across a continuous glide\n");

    // One octave up, exponentially, over ~2 s, logged as it goes.
    const double startHz = 110.0;
    const double total   = SR * 2.0;
    const double octaves = 1.0;

    PitchDetector det;
    det.prepare (2048, SR);

    const int block = 512;   // one detection frame
    int    worstAt  = 0;
    double worst    = 0.0;

    std::printf ("      true Hz   detected Hz   error (cents)\n");
    for (int n = 0; n + block <= static_cast<int> (total); n += block)
    {
        // Exponential glide across the whole run.
        const double frac = static_cast<double> (n) / total;
        const double trueHz = startHz * std::pow (2.0, octaves * frac);

        std::vector<float> buf (static_cast<size_t> (block));
        for (int i = 0; i < block; ++i)
            buf[static_cast<size_t> (i)] =
                static_cast<float> (0.8 * std::sin (2.0 * M_PI * trueHz * (n + i) / SR));

        const float got = det.detectPitch (buf.data(), block);

        if (n % (block * 8) == 0)
        {
            const double err = cents (got, trueHz);
            std::printf ("    %8.2f   %9.2f   %+9.1f\n", trueHz, got, err);

            if (std::fabs (err) > std::fabs (worst)) { worst = err; worstAt = n; }
        }
    }

    std::printf ("  worst sampled error: %+.1f cents at sample %d\n", worst, worstAt);

    // A glide is a moving target, so a few cents of lag is expected. An octave is
    // 1200 cents, and anything near that means the estimator locked onto the wrong
    // period rather than simply lagging.
    check (std::fabs (worst) < 100.0,
           "detector tracks a glide within 100 cents", worst, 0.0);
}

//==============================================================================
int main()
{
    std::printf ("=== super-autotune DSP self-test ===\n");

    testDetector();
    testLatency();
    testTransparency();
    testAccuracy();
    testHarmonicRich();
    testClicks();
    testStereo();
    testFlatGain();
    testTunParser();
    testGlideTracking();

    std::printf ("\n=== %s: %d failure(s) of %d checks ===\n",
                 failures == 0 ? "ALL PASS" : "FAILED", failures, checks);
    return failures == 0 ? 0 : 1;
}
