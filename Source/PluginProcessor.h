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
    bool loadBreakbeatFile(const juce::File& file,
                           double trimStartSeconds = -1.0, double trimEndSeconds = -1.0);
    bool isBreakbeatLoaded() const { return breakbeatBuffer.getNumSamples() > 0; }
    juce::String getLoadedBreakbeatName() const { return loadedFileName; }

    bool analyzeLoadedBreakbeat();

    // --- Your own drum samples (Step 3, sample-based version) --------
    // Loads a one-shot sample directly into the plugin for a given
    // drum class. This is what actually makes sound now — no MIDI-out,
    // no external instrument or Patcher routing needed, matching how
    // the reference tool ("Pick Pocket") most likely works.
    bool loadSampleForClass(GrooveEngine::DrumClass drumClass, const juce::File& file,
                            double trimStartSeconds = -1.0, double trimEndSeconds = -1.0);
    juce::String getLoadedSampleName(GrooveEngine::DrumClass drumClass) const;
    void clearSample(GrooveEngine::DrumClass drumClass);

    // Three playback modes, all looping until stopped:
    // - Midi: triggers ONLY your loaded samples (no breakbeat audio)
    // - Breakbeat: plays ONLY the original breakbeat audio (no samples)
    // - Both: plays the breakbeat audio AND triggers your samples
    enum class PlaybackMode { Midi, Breakbeat, Both };
    void setPlaybackState(bool shouldPlay, PlaybackMode mode);

    // Each of Kick/Snare/Hat/Breakbeat/Export remembers ITS OWN last-used
    // folder AND exact file independently — so re-opening "Load Snare"
    // goes back to wherever your snare came from (folder AND file
    // highlighted), even if you loaded a kick from a totally different
    // folder in between. Saved with the plugin's state so it survives
    // project reload too.
    enum class BrowseSlot { Kick, Snare, Hat, Breakbeat, Export };
    juce::String getFolderForSlot(BrowseSlot slot) const;
    void setFolderForSlot(BrowseSlot slot, const juce::String& path);
    juce::String getFileForSlot(BrowseSlot slot) const;
    void setFileForSlot(BrowseSlot slot, const juce::String& path);

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
    std::atomic<PlaybackMode> playbackMode { PlaybackMode::Both };

    juce::String kickFolderPath =
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
    juce::String snareFolderPath =
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
    juce::String hatFolderPath =
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
    juce::String breakbeatFolderPath =
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
    juce::String exportFolderPath =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName();

    juce::String kickFilePath, snareFilePath, hatFilePath, breakbeatFilePath, exportFilePath;

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
