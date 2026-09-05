#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PocketWorkAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit PocketWorkAudioProcessorEditor(PocketWorkAudioProcessor&);
    ~PocketWorkAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void exportMidi();

    PocketWorkAudioProcessor& processor;

    juce::Slider sensitivity;
    juce::Slider pocket;
    juce::Slider dynamics;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment;
    std::unique_ptr<SliderAttachment> pocketAttachment;
    std::unique_ptr<SliderAttachment> dynamicsAttachment;

    juce::TextButton playMidi {"\xE2\x99\xAA Play MIDI"};
    juce::TextButton playBoth {"\xE2\x99\xAB Play Both"};
    juce::TextButton exportButton {"\xE2\x87\xA9 EXPORT .MID"};
    juce::TextButton loadBreakbeatButton {"Load Breakbeat..."};
    juce::Label loadedBreakbeatLabel;

    void loadBreakbeat();

    juce::ComboBox exportMap;

    // TEMPORARY DIAGNOSTIC — shows exactly what the host is telling us
    // about transport/tempo, so we can confirm whether FL Studio is
    // providing real playhead data to this plugin at all. This will be
    // removed once the real-time Pocket feature is confirmed working.
    juce::Label hostInfoLabel;

    // NOTE: Grid selector, REC, CLICK, and count-in controls from the
    // spec are intentionally NOT in this build yet. Adding them as
    // inert placeholders would violate the "don't fake it" rule in
    // the spec, so they'll arrive in the milestone where they're
    // actually wired to real behavior.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        PocketWorkAudioProcessorEditor)
};
