#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class LightFingersAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit LightFingersAudioProcessorEditor(LightFingersAudioProcessor&);
    ~LightFingersAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void exportMidi();

    LightFingersAudioProcessor& processor;

    juce::Slider sensitivity;
    juce::Slider pocket;
    juce::Slider dynamics;

    // These bind the sliders directly to the plugin's parameters, so
    // moving a slider updates the real value AND stays in sync if the
    // value changes from automation or a saved preset. This is what
    // was missing in v0.1.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment;
    std::unique_ptr<SliderAttachment> pocketAttachment;
    std::unique_ptr<SliderAttachment> dynamicsAttachment;

    juce::TextButton playMidi {"\xE2\x99\xAA Play MIDI"};
    juce::TextButton playBoth {"\xE2\x99\xAB Play Both"};
    juce::TextButton exportButton {"\xE2\x87\xA9 EXPORT .MID"};

    juce::ComboBox exportMap;

    // NOTE: Grid selector, REC, CLICK, and count-in controls from the
    // spec are intentionally NOT in this build yet. Adding them as
    // inert placeholders would violate the "don't fake it" rule in
    // the spec, so they'll arrive in the milestone where they're
    // actually wired to real behavior.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        LightFingersAudioProcessorEditor)
};
