#pragma once
#include <JuceHeader.h>

// A small, self-contained audio player used only for previewing files in
// the browser dialog below. It owns its own audio device connection —
// completely separate from the host's audio engine — purely so you can
// click a file and hear it before deciding to load it into the plugin.
// Lives only as long as the browse dialog is open; cleans up fully when
// it closes.
class SamplePreviewPlayer
{
public:
    SamplePreviewPlayer()
    {
        formatManager.registerBasicFormats();
        deviceManager.initialiseWithDefaultDevices(0, 2);
        deviceManager.addAudioCallback(&sourcePlayer);
        sourcePlayer.setSource(&transportSource);
    }

    ~SamplePreviewPlayer()
    {
        transportSource.stop();
        transportSource.setSource(nullptr);
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);
        deviceManager.closeAudioDevice();
    }

    void playFile(const juce::File& file)
    {
        transportSource.stop();
        readerSource.reset();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file));

        if (reader == nullptr)
            return; // Honest: unreadable file, nothing to preview.

        readerSource = std::make_unique<juce::AudioFormatReaderSource>(
            reader.release(), true);

        transportSource.setSource(readerSource.get(), 0, nullptr,
                                  readerSource->getAudioFormatReader() != nullptr
                                      ? readerSource->getAudioFormatReader()->sampleRate
                                      : 44100.0);
        transportSource.start();
    }

    void stop() { transportSource.stop(); }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
};

// The actual preview panel shown alongside the file browser. Selecting a
// file (single click, or arrow-key navigation) auto-plays it immediately
// — the point is fast, no-commitment auditioning, not an extra click per
// file.
class SamplePreviewComponent : public juce::FilePreviewComponent
{
public:
    SamplePreviewComponent()
    {
        stopButton.setButtonText("Stop Preview");
        stopButton.onClick = [this] { player.stop(); };
        addAndMakeVisible(stopButton);

        nowPreviewingLabel.setJustificationType(juce::Justification::centredLeft);
        nowPreviewingLabel.setText("Click a file to hear it",
                                   juce::dontSendNotification);
        addAndMakeVisible(nowPreviewingLabel);

        setSize(300, 70);
    }

    void selectedFileChanged(const juce::File& newFile) override
    {
        if (newFile == juce::File{} || newFile.isDirectory())
            return;

        player.playFile(newFile);
        nowPreviewingLabel.setText("Previewing: " + newFile.getFileName(),
                                   juce::dontSendNotification);
    }

    void resized() override
    {
        nowPreviewingLabel.setBounds(4, 4, getWidth() - 8, 24);
        stopButton.setBounds(4, 32, 140, 28);
    }

private:
    SamplePreviewPlayer player;
    juce::TextButton stopButton;
    juce::Label nowPreviewingLabel;
};

// Shows a file-browse dialog with live audio preview, remembers the last
// folder used (so the next browse starts right where you left off instead
// of resetting to the Music folder), and calls `onFileChosen` with the
// selected file (or an empty File if cancelled).
inline void browseForSampleWithPreview(
    juce::File& lastFolder,
    const juce::String& dialogTitle,
    const juce::String& wildcardPatterns,
    std::function<void(const juce::File&)> onFileChosen)
{
    juce::WildcardFileFilter filter(wildcardPatterns, "", "Audio files");

    // Owned locally for the duration of this function — the browser only
    // borrows a pointer to it, so we keep it alive here and it cleans up
    // automatically when this function returns.
    auto previewComponent = std::make_unique<SamplePreviewComponent>();

    juce::FileBrowserComponent browser(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        lastFolder.isDirectory() ? lastFolder
                                  : juce::File::getSpecialLocation(
                                        juce::File::userMusicDirectory),
        &filter, previewComponent.get());

    juce::FileChooserDialogBox dialogBox(
        dialogTitle, juce::String(), browser, false,
        juce::Colours::darkgrey);

    bool chosenOk = dialogBox.show();

    if (chosenOk && browser.getSelectedFile(0) != juce::File{})
    {
        auto chosen = browser.getSelectedFile(0);
        lastFolder = chosen.getParentDirectory();
        onFileChosen(chosen);
    }
    else
    {
        onFileChosen(juce::File{});
    }
}
