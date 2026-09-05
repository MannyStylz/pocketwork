#pragma once
#include <JuceHeader.h>
#include "GrooveEngine.h"

namespace ParamIDs
{
    static const juce::String sensitivity { "sensitivity" };
    static const juce::String pocket      { "pocket" };
    static const juce::String dynamics    { "dynamics" };
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

    // --- Breakbeat playback (Step 1) --------------------------------
    bool loadBreakbeatFile(const juce::File& file);

    void setBreakbeatPlaying(bool shouldPlay);
    bool isBreakbeatLoaded() const { return breakbeatBuffer.getNumSamples() > 0; }
    juce::String getLoadedBreakbeatName() const { return loadedFileName; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GrooveEngine groove;

    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* pocketParam      = nullptr;
    std::atomic<float>* dynamicsParam    = nullptr;

    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> breakbeatBuffer;
    juce::String loadedFileName;
    double breakbeatSourceSampleRate = 44100.0;
    std::atomic<int> breakbeatPlayPosition { 0 };
    std::atomic<bool> breakbeatPlaying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PocketWorkAudioProcessor)
};
