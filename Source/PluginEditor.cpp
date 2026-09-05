#include "PluginEditor.h"

PocketWorkAudioProcessorEditor::
PocketWorkAudioProcessorEditor(PocketWorkAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1100, 720);

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

    exportMap.addItem("General MIDI", 1);
    exportMap.setSelectedId(1);
    addAndMakeVisible(exportMap);

    addAndMakeVisible(playMidi);
    addAndMakeVisible(playBoth);
    addAndMakeVisible(exportButton);

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

    box({20, 105, 1060, 120}, "MIDI OUT");
    box({20, 240, 1060, 130}, "THE POCKET");
    box({20, 385, 520, 150}, "VELOCITY");
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

    playMidi.setBounds(25, 190, 130, 35);
    playBoth.setBounds(165, 190, 130, 35);

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

void PocketWorkAudioProcessorEditor::exportMidi()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export POCKETWORK MIDI",
        juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory),
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
