# super-autotune

A real-time vocal tuner with microtonal tuning support, built with JUCE as a VST3
and standalone application.

Pick notes from a 128-note list, load a Scala (`.scl`) or Anamark (`.tun`) tuning
file to define what each note is worth, and the incoming monophonic audio is
snapped to the nearest one.

## Features

- **Any of the 128 MIDI notes**, not just pitch classes. Notes are toggled
  individually from a scrollable list that shows each note's pitch in cents.
- **Tuning files** — `.scl` (periodic scales) and `.tun` (explicit note → cents
  tables). Loading a file changes the pitch mapping only; it never changes which
  notes you have selected.
- **Octave extend** — a selected note is available at every octave, and the
  nearest one to the input is used. Turning it off pins each note to its own
  absolute pitch.
- **MIDI input** — when armed, held MIDI notes are the sole tuning source, so a
  keyboard can drive the tuning. MPE-aware, including per-note pitch bend.
- **Snap and Multiply modes** — snap to the nearest allowed note, or generate a
  harmony voice per selected note (capped at four).
- **Constant, exact latency**, reported to the host: 1014 samples at 44.1 kHz,
  about 23 ms. It does not vary with pitch or with the shift ratio.
- **Saves with the project** — the full tuning table and note selection are
  stored in the plugin state, so a reload does not depend on the tuning file
  still being on disk.

## How the pitch shifting works

Period-synchronous resampling, in the time domain.

The read position advances through the input at the pitch ratio — that read rate
*is* the pitch shift, so there is no spectral processing and no bin resolution to
limit the result.

Reading at a different rate than the input arrives means the read cursor slowly
drifts away from where it belongs, so at each fragment boundary (128 samples) the
drift is measured and the cursor is jumped by **a whole number of measured pitch
periods**, crossfading across one fragment.

Jumping by a whole period is the important part. The waveform at the jumped-to
position is in phase with the waveform at the jumped-from position, so the
crossfade blends two copies of the same signal: it stays at unity gain and cannot
comb-filter. The jump corrects the *duration*; the pitch comes from the read rate
and nothing else.

Earlier revisions used an STFT phase vocoder. It could not shift voiced audio at
all — at a 1024-point FFT the bins are 43 Hz apart and a Hann main lobe is four
bins wide, so a voice at 100–300 Hz has harmonics that are simply not resolved.
Pure sines shifted correctly, which is why it looked like it worked.

## Tuning files

**`.scl` (Scala)** — a periodic scale: a count, then one pitch per step in cents
or as a ratio. Steps are mapped to consecutive MIDI notes from note 60 upward,
and the scale is tiled at its own period, which is read from the file rather than
assumed to be an octave. Scales that do not repeat at 2/1 (Bohlen-Pierce, for
example) therefore tile correctly.

**`.tun` (Anamark)** — an INI-like file whose `[Tuning]` section holds **absolute
cents**, not hertz. A 12-TET table reads `note 0 = 0`, `note 1 = 100`, … and a
24-TET table steps by 50. Cents are converted with

```
freq = 8.1757989156 * 2 ^ (cents / 1200)
```

where 8.1757989156 Hz is MIDI note 0 in 12-TET at A440 — so a 12-TET table comes
out at exactly 440 Hz for note 69.

## Building

Requires JUCE 8.0.6 and CMake 3.22+.

```
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release --target super-autotune_VST3
```

### Windows

`install.ps1` does the whole job: it installs the Visual Studio build tools if
needed, fetches CMake and JUCE, downloads the source, builds the VST3 and copies
it into `C:\Program Files\Common Files\VST3`.

```powershell
irm https://brodybigwood.com/pages/autotune/install.ps1 | iex
```

It is incremental — source files are compared by hash and only changed ones are
rebuilt — and it falls back to a clean build if an incremental one fails.

## Testing

`Source/DspTest.cpp` is an offline harness that is not part of the plugin build.
It drives the shifter and detector with known signals and asserts the properties
a tuner has to have, measured rather than assumed:

```
cmake --build build --target super-autotune_dsptest
./build/super-autotune_dsptest_artefacts/super-autotune_dsptest
```

What it checks: tuning accuracy in cents, exact constant latency, transparency at
ratio 1, absence of grain-rate sidebands, flat gain across ratios, click-freeness,
stereo identity, pitch detection across a continuous glide, and `.tun` parsing
against the format's own 12-TET example.

## Controls

| Control | Purpose |
| --- | --- |
| RETUNE | How fast the pitch moves to the target. Low is the hard-tuned effect; high glides. |
| MODE | Snap, or Multiply for one voice per selected note. |
| MIDI | When on, held MIDI notes drive the tuning and the list selection is ignored. |
| OCTAVE EXTEND | Make selected notes available at every octave, or pin them to their own pitch. |
| MIX | Wet/dry. The dry path is delay-matched to the shifter, so this does not comb. |
| FFT | Spectrum display resolution only; the shifter is time-domain. |
| LOAD TUNING FILE | Load a `.scl` or `.tun`. Does not change the note selection. |

## Limitations

- **Monophonic.** It tracks one pitch at a time.
- **Formants shift with the pitch.** Because the shift is a resampling, the
  spectral envelope moves with it — corrections of a semitone or two are
  transparent, but large shifts will sound like a chipmunk. Formant preservation
  would need an extra stage.
- **Detection covers 70 Hz to 1200 Hz** (roughly D2 to D6, most of the singing
  range). Outside that, no pitch is reported and the plugin passes audio through.
- **Multiply is capped at four voices**, since each one is a separate copy of the
  shifting chain.
