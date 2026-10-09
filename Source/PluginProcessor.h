/*
  ==============================================================================
    super-autotune — Audio Processor
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "PitchShifter.h"  // for PitchShifter::PhaseState (per-channel/per-voice state)

// Forward declarations for components (implemented in later phases)
class PitchDetector;
class TuningManager;
class MidiManager;
class SpectrumAnalyzer;

//==============================================================================
// Small per-channel FIFO that delays a signal by a fixed number of samples.
// Used to time-align the dry path with the phase-vocoder wet path (which is
// delayed by latency = fftSize - hopSize) before the wet/dry mix.
struct DryDelayLine
{
    void prepare(int latencySamples)
    {
        buf.assign(static_cast<size_t>(latencySamples), 0.0f);
        wptr = 0;
    }

    // Read the sample written `latency` ago, THEN store the incoming one.
    //
    // The order matters: writing first and reading the same slot back returns the
    // sample just written, i.e. no delay at all, which silently disabled the dry
    // delay and made the MIX control comb.
    float writeThenRead(float x)
    {
        const int n = static_cast<int>(buf.size());
        if (n == 0)
            return x;                     // zero latency -> passthrough
        const float delayed = buf[static_cast<size_t>(wptr % n)];
        buf[static_cast<size_t>(wptr % n)] = x;
        ++wptr;
        return delayed;
    }

    std::vector<float> buf;
    long long wptr = 0;
};

//==============================================================================
class SuperautotuneAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SuperautotuneAudioProcessor();
    ~SuperautotuneAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // MPE support
    bool supportsMPE() const override { return true; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Accessors for GUI
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    float getDetectedPitch() const { return detectedPitch.load(); }
    float getTargetPitch()   const { return targetPitch.load(); }

    // MIDI note (0-127) the input is currently being snapped to, or -1 if none.
    int getTargetNote() const { return targetNoteIndex.load(); }

    // Tuning file path (managed separately from APVTS)
    void setTuningFilePath(const juce::String& path);
    juce::String getTuningFilePath() const { return tuningFilePath; }

    // Spectrum + tuning data for the GUI (thread-safe, polled at 30Hz)
    int getNumSpectrumBins() const;
    void getSpectrumMagnitudes(float* buffer, int numBins);
    juce::StringArray getActiveNoteNames();
    double getSampleRateForGUI() const { return getSampleRate(); }

    // Note selection (the editor's note list)
    void toggleNote(int midiNote);
    bool isNoteEnabled(int midiNote) const;

    // What the loaded tuning file maps a note to, or 12-TET if none is loaded.
    // Used to show the actual frequency next to each note in the list.
    float getNoteFrequency(int midiNote) const;

    // The cents this note is tuned to under the loaded file (see TuningManager).
    float getNoteCents(int midiNote) const;
    bool  hasTuningFile() const;
    juce::String getScaleDescription() const;

private:
    //==============================================================================
    void handleParameterChanges();

    // (Re)allocate per-voice phase states + per-channel dry delay lines.
    // Call after prepareToPlay and after any FFT/hop reconfiguration.
    void rebuildVoiceResources();

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // DSP components (created in prepareToPlay, or lazily)
    std::unique_ptr<PitchDetector>    pitchDetector;
    std::unique_ptr<PitchShifter>     pitchShifter;
    std::unique_ptr<TuningManager>    tuningManager;
    std::unique_ptr<MidiManager>      midiManager;
    std::unique_ptr<SpectrumAnalyzer> spectrumAnalyzer;

    // One shifter *engine* (the input rings and detector live in it) plus one
    // State per independent voice. Snap mode uses state[ch*maxVoices]; Multiply
    // mode uses state[ch*maxVoices + v] for harmony voice v. Each voice needs its
    // own read cursor and ratio, which is what State holds.
    static constexpr int maxMultiplyVoices = 4;
    std::vector<PitchShifter::State> voiceStates;
    int stateStride = 1;   // number of voices per channel (== maxMultiplyVoices)

    // Per-channel dry delay lines (align dry with the delayed wet path).
    std::vector<DryDelayLine> dryDelays;

    // Cached parameters (checked each block for changes)
    int    currentFftSize   = 1024;
    int    currentHopSize   = 256;      // fftSize/4 for 75% overlap
    int    currentHopIndex  = 1;        // default 75%
    int    currentFftIndex  = 2;        // default 1024
    float  currentRetuneSpeed = 0.5f;
    int    currentTuningMode   = 0;     // 0=Snap, 1=Multiply
    bool   currentMidiEnabled  = false;
    bool   currentOctaveExtend = true;
    float  currentWetDry       = 1.0f;
    bool   currentBypass       = false;

    // Flags for parameter-triggered reallocation
    std::atomic<bool> fftConfigChanged{false};
    std::atomic<bool> tuningConfigChanged{false};

    // Scratch buffers
    juce::AudioBuffer<float> dryBuffer;        // holds dry input for wet/dry mix
    juce::AudioBuffer<float> processedBuffer;  // holds processed output
    std::vector<float> multiplyFrameSum;        // accumulate multiply-mode voices
    std::vector<float> multiplyFrame;           // per-voice output, block sized

    // Tuning file path
    juce::String tuningFilePath;

    // Detection state (shared with GUI)
    std::atomic<float> detectedPitch{0.0f};
    std::atomic<float> targetPitch{0.0f};
    std::atomic<int>   targetNoteIndex{-1};

    // True once prepareToPlay has been called
    bool prepared = false;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperautotuneAudioProcessor)
};
