#pragma once
#include <JuceHeader.h>
#include "GrooveEngine.h"
#include <vector>

// Parameter ID constants — used by both the processor and the editor
// so the two never get out of sync with mismatched string names.
namespace ParamIDs
{
    static const juce::String sensitivity { "sensitivity" };
    static const juce::String pocket      { "pocket" };
    static const juce::String dynamics    { "dynamics" };
    static const juce::String breakbeatBpm  { "breakbeatBpm" };
    static const juce::String breakbeatBars { "breakbeatBars" };
}

class PocketWorkAudioProcessor : public juce::AudioProcessor
{
public:
    PocketWorkAudioProcessor();
    ~PocketWorkAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "POCKETWORK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // getStateInformation/setStateInformation now save the FULL apvts
    // state (not just three raw floats), so presets survive properly
    // and every parameter is DAW-automatable.
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    GrooveEngine& getGrooveEngine() { return groove; }

    // --- Breakbeat playback (Step 1) --------------------------------
    // Loads an audio file fully into memory (simple and real-time-safe
    // for short breakbeat-length samples; not built for long files).
    // Returns false if the file couldn't be read.
    bool loadBreakbeatFile(const juce::File& file);

    void setBreakbeatPlaying(bool shouldPlay);
    bool isBreakbeatLoaded() const { return breakbeatBuffer.getNumSamples() > 0; }
    juce::String getLoadedBreakbeatName() const { return loadedFileName; }

    // Runs onset detection on the currently loaded breakbeat and
    // replaces the groove engine's hits with what it finds. Message-
    // thread only (button click), never called from processBlock.
    // Returns false if no breakbeat is loaded yet.
    bool analyzeLoadedBreakbeat();

    // Exposed so the editor can attach sliders directly to parameters
    // instead of the editor and engine drifting out of sync.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GrooveEngine groove;

    // Cached atomic pointers to the live parameter values, read safely
    // from the audio thread on every processBlock call.
    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* pocketParam      = nullptr;
    std::atomic<float>* dynamicsParam    = nullptr;
    std::atomic<float>* breakbeatBpmParam  = nullptr;
    std::atomic<float>* breakbeatBarsParam = nullptr;

    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> breakbeatBuffer;
    juce::String loadedFileName;
    double breakbeatSourceSampleRate = 44100.0;
    std::atomic<int> breakbeatPlayPosition { 0 };
    std::atomic<bool> breakbeatPlaying { false };

    // Collected each block by processBlock's breakbeat-playback logic,
    // then applied directly to the final MIDI output — kept separate
    // from the live swing engine so these precisely-timed trigger
    // notes are never shifted by it.
    struct PlaybackTriggerNote { int sampleOffset; int note; int velocity; };
    std::vector<PlaybackTriggerNote> pendingPlaybackTriggerNoteOns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PocketWorkAudioProcessor)
};
