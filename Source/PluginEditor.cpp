#include "PluginEditor.h"

PocketWorkAudioProcessorEditor::
PocketWorkAudioProcessorEditor(PocketWorkAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    using Slot = PocketWorkAudioProcessor::BrowseSlot;
    kickBrowseFolder      = juce::File(processor.getFolderForSlot(Slot::Kick));
    snareBrowseFolder     = juce::File(processor.getFolderForSlot(Slot::Snare));
    hatBrowseFolder       = juce::File(processor.getFolderForSlot(Slot::Hat));
    breakbeatBrowseFolder = juce::File(processor.getFolderForSlot(Slot::Breakbeat));
    exportBrowseFolder    = juce::File(processor.getFolderForSlot(Slot::Export));

    setSize(1300, 720);

    auto configure = [](juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 22);
    };

    configure(sensitivity);
    configure(pocket);
    configure(dynamics);

    addAndMakeVisible(sensitivity);
    addAndMakeVisible(pocket);
    addAndMakeVisible(dynamics);

    sensitivityAttachment = std::make_unique<SliderAttachment>(
        processor.apvts, ParamIDs::sensitivity, sensitivity);
    pocketAttachment = std::make_unique<SliderAttachment>(
        processor.apvts, ParamIDs::pocket, pocket);
    dynamicsAttachment = std::make_unique<SliderAttachment>(
        processor.apvts, ParamIDs::dynamics, dynamics);

    configure(breakbeatBpmSlider);
    configure(breakbeatBarsSlider);
    addAndMakeVisible(breakbeatBpmSlider);
    addAndMakeVisible(breakbeatBarsSlider);
    addAndMakeVisible(breakbeatBpmLabel);
    addAndMakeVisible(breakbeatBarsLabel);
    breakbeatBpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc18a35));
    breakbeatBarsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc18a35));
    breakbeatBpmLabel.setFont(juce::Font(12.0f));
    breakbeatBarsLabel.setFont(juce::Font(12.0f));

    breakbeatBpmAttachment = std::make_unique<SliderAttachment>(
        processor.apvts, ParamIDs::breakbeatBpm, breakbeatBpmSlider);
    breakbeatBarsAttachment = std::make_unique<SliderAttachment>(
        processor.apvts, ParamIDs::breakbeatBars, breakbeatBarsSlider);

    exportMap.addItem("General MIDI", 1);
    exportMap.setSelectedId(1);
    addAndMakeVisible(exportMap);

    addAndMakeVisible(playMidi);
    addAndMakeVisible(playBreakbeat);
    addAndMakeVisible(playBoth);
    addAndMakeVisible(exportButton);

    addAndMakeVisible(loadBreakbeatButton);
    loadBreakbeatButton.onClick = [this] { loadBreakbeat(); };

    loadedBreakbeatLabel.setColour(juce::Label::textColourId,
                                   juce::Colours::lightgrey);
    loadedBreakbeatLabel.setFont(juce::Font(12.0f));
    loadedBreakbeatLabel.setText("No breakbeat loaded",
                                 juce::dontSendNotification);
    addAndMakeVisible(loadedBreakbeatLabel);

    // Three mutually exclusive, looping playback modes. Starting any one
    // of them automatically stops the other two, since they all share
    // the same underlying playback position.
    using Mode = PocketWorkAudioProcessor::PlaybackMode;

    playMidi.setClickingTogglesState(true);
    playBreakbeat.setClickingTogglesState(true);
    playBoth.setClickingTogglesState(true);

    playMidi.onClick = [this]
    {
        bool nowOn = playMidi.getToggleState();
        if (nowOn)
        {
            playBreakbeat.setToggleState(false, juce::dontSendNotification);
            playBoth.setToggleState(false, juce::dontSendNotification);
        }
        processor.setPlaybackState(nowOn, Mode::Midi);
    };

    playBreakbeat.onClick = [this]
    {
        bool nowOn = playBreakbeat.getToggleState();
        if (nowOn)
        {
            playMidi.setToggleState(false, juce::dontSendNotification);
            playBoth.setToggleState(false, juce::dontSendNotification);
        }
        processor.setPlaybackState(nowOn, Mode::Breakbeat);
    };

    playBoth.onClick = [this]
    {
        bool nowOn = playBoth.getToggleState();
        if (nowOn)
        {
            playMidi.setToggleState(false, juce::dontSendNotification);
            playBreakbeat.setToggleState(false, juce::dontSendNotification);
        }
        processor.setPlaybackState(nowOn, Mode::Both);
    };

    addAndMakeVisible(detectGrooveButton);
    detectResultLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
    detectResultLabel.setFont(juce::Font(12.0f));
    detectResultLabel.setText("No groove detected yet",
                              juce::dontSendNotification);
    addAndMakeVisible(detectResultLabel);

    detectGrooveButton.onClick = [this]
    {
        const bool ok = processor.analyzeLoadedBreakbeat();

        if (!ok)
        {
            detectResultLabel.setText("Load a breakbeat first",
                                      juce::dontSendNotification);
            return;
        }

        int hitCount = processor.getGrooveEngine().getExtractedHitCount();

        detectResultLabel.setText(
            hitCount > 0
                ? ("Detected " + juce::String(hitCount) + " hits")
                : "No hits detected — try raising Sensitivity",
            juce::dontSendNotification);
    };

    // --- Your own drum samples --------------------------------------
    auto setupSampleLabel = [](juce::Label& l, const juce::String& text)
    {
        l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        l.setFont(juce::Font(12.0f));
        l.setText(text, juce::dontSendNotification);
    };

    addAndMakeVisible(loadKickButton);
    addAndMakeVisible(loadSnareButton);
    addAndMakeVisible(loadHatButton);
    addAndMakeVisible(clearKickButton);
    addAndMakeVisible(clearSnareButton);
    addAndMakeVisible(clearHatButton);
    setupSampleLabel(kickSampleLabel, "No kick sample loaded");
    setupSampleLabel(snareSampleLabel, "No snare sample loaded");
    setupSampleLabel(hatSampleLabel, "No hat sample loaded");
    addAndMakeVisible(kickSampleLabel);
    addAndMakeVisible(snareSampleLabel);
    addAndMakeVisible(hatSampleLabel);

    loadKickButton.onClick  = [this] { loadSample(GrooveEngine::DrumClass::Kick,  kickSampleLabel); };
    loadSnareButton.onClick = [this] { loadSample(GrooveEngine::DrumClass::Snare, snareSampleLabel); };
    loadHatButton.onClick   = [this] { loadSample(GrooveEngine::DrumClass::Hat,   hatSampleLabel); };

    clearKickButton.onClick = [this]
    {
        processor.clearSample(GrooveEngine::DrumClass::Kick);
        kickSampleLabel.setText("No kick sample loaded", juce::dontSendNotification);
    };
    clearSnareButton.onClick = [this]
    {
        processor.clearSample(GrooveEngine::DrumClass::Snare);
        snareSampleLabel.setText("No snare sample loaded", juce::dontSendNotification);
    };
    clearHatButton.onClick = [this]
    {
        processor.clearSample(GrooveEngine::DrumClass::Hat);
        hatSampleLabel.setText("No hat sample loaded", juce::dontSendNotification);
    };


    hostInfoLabel.setColour(juce::Label::textColourId, juce::Colours::lime);
    hostInfoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(hostInfoLabel);

    exportButton.onClick = [this] { exportMidi(); };

    startTimerHz(30);
}

void PocketWorkAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff110e0a));

    g.setColour(juce::Colour(0xffe4a52a));
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("POCKETWORK", 24, 16, 320, 32,
               juce::Justification::left);

    g.setFont(juce::Font(12.0f));
    g.setColour(juce::Colours::lightgrey);
    g.drawText("MIDI OUT", 26, 62, 120, 20,
               juce::Justification::left);

    g.drawText("GM channel 10   kick 36 / snare 38 / closed hat 42",
               110, 62, 650, 20, juce::Justification::left);

    auto box = [this, &g](juce::Rectangle<int> r, const char* title)
    {
        g.setColour(juce::Colour(0xff1b1712));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff4b3a23));
        g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colour(0xffe4a52a));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(title, r.getX()+12, r.getY()+10, 220, 22,
                   juce::Justification::left);
    };

    box({20, 105, 1260, 120}, "MIDI OUT");
    box({20, 240, 1260, 130}, "THE POCKET");
    box({20, 385, 520, 150}, "YOUR SAMPLES");
    box({560, 385, 520, 150}, "GROOVE DNA");

    g.setColour(juce::Colour(0xffc18a35));
    g.setFont(juce::Font(12.0f));
    g.drawText("Sensitivity", 30, 570, 90, 20, juce::Justification::left);
    g.drawText("Grid", 390, 570, 50, 20, juce::Justification::left);
    g.drawText("Pocket", 650, 570, 55, 20, juce::Justification::left);
    g.drawText("Dynamics", 850, 570, 65, 20, juce::Justification::left);

    g.drawText("Export Map", 30, 625, 90, 20, juce::Justification::left);
}

void PocketWorkAudioProcessorEditor::resized()
{
    sensitivity.setBounds(110, 565, 250, 28);
    pocket.setBounds(705, 565, 125, 28);
    dynamics.setBounds(915, 565, 125, 28);

    playMidi.setBounds(25, 190, 140, 35);
    playBreakbeat.setBounds(175, 190, 160, 35);
    playBoth.setBounds(345, 190, 130, 35);

    loadBreakbeatButton.setBounds(485, 190, 160, 35);
    loadedBreakbeatLabel.setBounds(655, 195, 230, 25);

    detectGrooveButton.setBounds(895, 190, 140, 35);
    detectResultLabel.setBounds(1045, 195, 230, 25);

    breakbeatBpmLabel.setBounds(60, 275, 150, 20);
    breakbeatBpmSlider.setBounds(60, 300, 250, 28);
    breakbeatBarsLabel.setBounds(400, 275, 100, 20);
    breakbeatBarsSlider.setBounds(400, 300, 150, 28);

    loadKickButton.setBounds(40, 420, 100, 32);
    clearKickButton.setBounds(148, 420, 60, 32);
    kickSampleLabel.setBounds(216, 425, 300, 22);

    loadSnareButton.setBounds(40, 460, 100, 32);
    clearSnareButton.setBounds(148, 460, 60, 32);
    snareSampleLabel.setBounds(216, 465, 300, 22);

    loadHatButton.setBounds(40, 500, 100, 32);
    clearHatButton.setBounds(148, 500, 60, 32);
    hatSampleLabel.setBounds(216, 505, 300, 22);

    exportMap.setBounds(110, 620, 280, 30);
    exportButton.setBounds(850, 615, 205, 38);

    hostInfoLabel.setBounds(20, 675, 1040, 24);
}

void PocketWorkAudioProcessorEditor::timerCallback()
{
    juce::String info = "Host info: ";

    if (auto* playHead = processor.getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            bool isPlaying = position->getIsPlaying();
            auto ppq = position->getPpqPosition();
            auto tempo = position->getBpm();

            info << "playing=" << (isPlaying ? "YES" : "no")
                 << "   ppq=" << (ppq.hasValue() ? juce::String(*ppq, 2) : "unavailable")
                 << "   bpm=" << (tempo.hasValue() ? juce::String(*tempo, 1) : "unavailable");
        }
        else
        {
            info << "getPosition() returned nothing (host gave no position info)";
        }
    }
    else
    {
        info << "no playhead object at all (getPlayHead() returned null)";
    }

    int lastDelayMs = processor.getGrooveEngine().getLastNoteOnDelayMs();
    juce::String delayInfo = (lastDelayMs < 0)
        ? "Last note-on delay: none processed yet"
        : "Last note-on delay: " + juce::String(lastDelayMs) + " ms";

    hostInfoLabel.setText(info + "     |     " + delayInfo,
                          juce::dontSendNotification);

    repaint();
}

void PocketWorkAudioProcessorEditor::loadBreakbeat()
{
    using Slot = PocketWorkAudioProcessor::BrowseSlot;
    juce::File preselect(processor.getFileForSlot(Slot::Breakbeat));

    browseForSampleWithPreview(
        breakbeatBrowseFolder,
        preselect,
        "Load Breakbeat Audio (drag the green handles to trim, then Load)",
        "*.wav;*.aif;*.aiff;*.mp3;*.flac",
        [this](const juce::File& file, double trimStart, double trimEnd)
        {
            if (file == juce::File{})
                return;

            const bool hasTrim = trimEnd > trimStart;
            const bool ok = hasTrim
                ? processor.loadBreakbeatFile(file, trimStart, trimEnd)
                : processor.loadBreakbeatFile(file);

            processor.setFolderForSlot(Slot::Breakbeat, breakbeatBrowseFolder.getFullPathName());
            processor.setFileForSlot(Slot::Breakbeat, file.getFullPathName());

            loadedBreakbeatLabel.setText(
                ok ? ("Loaded: " + processor.getLoadedBreakbeatName())
                   : "Failed to load that file",
                juce::dontSendNotification);
        });
}

void PocketWorkAudioProcessorEditor::loadSample(
    GrooveEngine::DrumClass cls, juce::Label& targetLabel)
{
    using Slot = PocketWorkAudioProcessor::BrowseSlot;

    juce::File* folderMember = &kickBrowseFolder;
    Slot slot = Slot::Kick;

    switch (cls)
    {
        case GrooveEngine::DrumClass::Kick:  folderMember = &kickBrowseFolder;  slot = Slot::Kick;  break;
        case GrooveEngine::DrumClass::Snare: folderMember = &snareBrowseFolder; slot = Slot::Snare; break;
        case GrooveEngine::DrumClass::Hat:   folderMember = &hatBrowseFolder;   slot = Slot::Hat;   break;
    }

    browseForSampleWithPreview(
        *folderMember,
        juce::File(processor.getFileForSlot(slot)),
        "Load Sample (drag the green handles to trim, then Load)",
        "*.wav;*.aif;*.aiff;*.mp3;*.flac",
        [this, cls, slot, folderMember, &targetLabel]
            (const juce::File& file, double trimStart, double trimEnd)
        {
            if (file == juce::File{})
                return;

            const bool hasTrim = trimEnd > trimStart;
            const bool ok = hasTrim
                ? processor.loadSampleForClass(cls, file, trimStart, trimEnd)
                : processor.loadSampleForClass(cls, file);

            processor.setFolderForSlot(slot, folderMember->getFullPathName());
            processor.setFileForSlot(slot, file.getFullPathName());

            targetLabel.setText(
                ok ? ("Loaded: " + processor.getLoadedSampleName(cls))
                   : "Failed to load that file",
                juce::dontSendNotification);
        });
}

void PocketWorkAudioProcessorEditor::exportMidi()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export POCKETWORK MIDI",
        exportBrowseFolder.isDirectory()
            ? exportBrowseFolder
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            const bool ok = processor.getGrooveEngine().exportMidi(file);

            if (ok)
            {
                exportBrowseFolder = file.getParentDirectory();
                processor.setFolderForSlot(
                    PocketWorkAudioProcessor::BrowseSlot::Export,
                    exportBrowseFolder.getFullPathName());
            }

            if (!ok)
            {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Export failed")
                        .withMessage("POCKETWORK could not write the MIDI "
                                     "file to:\n" + file.getFullPathName() +
                                     "\n\nTry a different folder (e.g. your "
                                     "Desktop) and try again."),
                    nullptr);
            }
        });
}
