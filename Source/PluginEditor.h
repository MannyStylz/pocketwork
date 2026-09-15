#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SamplePreviewComponent.h"

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

    // These bind the sliders directly to the plugin's parameters, so
    // moving a slider updates the real value AND stays in sync if the
    // value changes from automation or a saved preset. This is what
    // was missing in v0.1.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment;
    std::unique_ptr<SliderAttachment> pocketAttachment;
    std::unique_ptr<SliderAttachment> dynamicsAttachment;

    // Tells the analyzer the loaded breakbeat's actual musical length
    // (e.g. "2 bars at 95 BPM") so extraction maps onto the right grid
    // step instead of assuming exactly one bar.
    juce::Slider breakbeatBpmSlider;
    juce::Slider breakbeatBarsSlider;
    juce::Label breakbeatBpmLabel { {}, "Source BPM" };
    juce::Label breakbeatBarsLabel { {}, "Bars" };
    std::unique_ptr<SliderAttachment> breakbeatBpmAttachment;
    std::unique_ptr<SliderAttachment> breakbeatBarsAttachment;

    juce::TextButton playMidi {"Play/Stop MIDI"};
    juce::TextButton playBreakbeat {"Play/Stop Breakbeat"};
    juce::TextButton playBoth {"Play/Stop Both"};
    juce::TextButton exportButton {"\xE2\x87\xA9 EXPORT .MID"};
    juce::TextButton loadBreakbeatButton {"Load Breakbeat..."};
    juce::Label loadedBreakbeatLabel;
    juce::TextButton detectGrooveButton {"Detect Groove"};
    juce::Label detectResultLabel;

    // Your own drum samples — loaded directly into the plugin so it
    // makes real sound on its own, no MIDI-out/Patcher routing needed.
    juce::TextButton loadKickButton {"Load Kick..."};
    juce::TextButton loadSnareButton {"Load Snare..."};
    juce::TextButton loadHatButton {"Load Hat..."};
    juce::TextButton clearKickButton {"Clear"};
    juce::TextButton clearSnareButton {"Clear"};
    juce::TextButton clearHatButton {"Clear"};
    juce::TextButton exportKickButton {"Export..."};
    juce::TextButton exportSnareButton {"Export..."};
    juce::TextButton exportHatButton {"Export..."};
    juce::Label kickSampleLabel, snareSampleLabel, hatSampleLabel;

    void exportSampleToFile(GrooveEngine::DrumClass cls);

    void loadSample(GrooveEngine::DrumClass cls, juce::Label& targetLabel);

    void loadBreakbeat();

    // Remembers each slot's OWN last-used folder AND exact file
    // independently — initialized from the processor's saved state in
    // the constructor, and written back to it after every successful
    // browse. Kept as members (not locals) since the browse dialog is
    // asynchronous and needs these to stay alive until the user actually
    // picks a file.
    // One shared audio-preview connection for the whole editor session —
    // repeatedly creating/destroying one per dialog open was a real
    // source of instability, since it meant opening and closing an
    // audio device connection every single time you loaded a file.
    SamplePreviewPlayer sharedPreviewPlayer;

    juce::File kickBrowseFolder, snareBrowseFolder, hatBrowseFolder,
              breakbeatBrowseFolder, exportBrowseFolder;

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
