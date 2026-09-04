#pragma once
#include <JuceHeader.h>
#include "GrooveEngine.h"

// Parameter ID constants — used by both the processor and the editor
// so the two never get out of sync with mismatched string names.
namespace ParamIDs
{
    static const juce::String sensitivity { "sensitivity" };
    static const juce::String pocket      { "pocket" };
    static const juce::String dynamics    { "dynamics" };
}

class LightFingersAudioProcessor : public juce::AudioProcessor
{
public:
    LightFingersAudioProcessor();
    ~LightFingersAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LIGHTFINGERS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightFingersAudioProcessor)
};
