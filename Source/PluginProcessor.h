#pragma once
#include <JuceHeader.h>
#include "GrooveEngine.h"
#include <vector>
#include <array>

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

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    GrooveEngine& getGrooveEngine() { return groove; }

    // --- Breakbeat playback ------------------------------------------
    bool loadBreakbeatFile(const juce::File& file);
    void setBreakbeatPlaying(bool shouldPlay);
    bool isBreakbeatLoaded() const { return breakbeatBuffer.getNumSamples() > 0; }
    juce::String getLoadedBreakbeatName() const { return loadedFileName; }

    bool analyzeLoadedBreakbeat();

    // --- Your own drum samples (Step 3, sample-based version) --------
    // Loads a one-shot sample directly into the plugin for a given
    // drum class. This is what actually makes sound now — no MIDI-out,
    // no external instrument or Patcher routing needed, matching how
    // the reference tool ("Pick Pocket") most likely works.
    bool loadSampleForClass(GrooveEngine::DrumClass drumClass, const juce::File& file);
    juce::String getLoadedSampleName(GrooveEngine::DrumClass drumClass) const;

    // Exposed so the editor can attach sliders directly to parameters
    // instead of the editor and engine drifting out of sync.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GrooveEngine groove;

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

    // One loaded one-shot sample buffer per drum class.
    juce::AudioBuffer<float> kickSample, snareSample, hatSample;
    juce::String kickFileName, snareFileName, hatFileName;

    const juce::AudioBuffer<float>* getSampleBufferForClass(
        GrooveEngine::DrumClass cls) const;

    // A small fixed pool of playback "voices" so overlapping hits
    // (e.g. fast hi-hats) can play simultaneously without cutting each
    // other off. No heap allocation here — pool is fixed-size and
    // pre-allocated, real-time-safe.
    struct SampleVoice
    {
        GrooveEngine::DrumClass drumClass = GrooveEngine::DrumClass::Kick;
        int position = 0;
        bool active = false;
        float gain = 1.0f;
        int triggerOffsetThisBlock = -1;
    };

    static constexpr int kNumVoices = 8;
    std::array<SampleVoice, kNumVoices> voices;

    void triggerSampleVoice(GrooveEngine::DrumClass cls, float velocity,
                            int offsetInBlock);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PocketWorkAudioProcessor)
};
