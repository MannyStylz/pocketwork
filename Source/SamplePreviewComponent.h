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

        // Prefer WASAPI "Windows Audio" (shared mode) over whatever the
        // default device type is. ASIO drivers typically claim the audio
        // device EXCLUSIVELY, which can silently block this separate
        // preview connection from opening at all. WASAPI shared mode is
        // specifically designed to let multiple apps use the output at
        // once, so this avoids the conflict rather than papering over it.
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "Windows Audio"
                && deviceManager.getCurrentAudioDeviceType() != "Windows Audio")
            {
                deviceManager.setCurrentAudioDeviceType(type->getTypeName(), true);
                deviceManager.initialiseWithDefaultDevices(0, 2);
                break;
            }
        }

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

    bool isDeviceAvailable() const { return deviceManager.getCurrentAudioDevice() != nullptr; }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
};

// The actual preview panel shown alongside the file browser. Selecting a
// file (single click, or arrow-key navigation) auto-plays it immediately.
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

        if (!player.isDeviceAvailable())
        {
            nowPreviewingLabel.setText(
                "Preview unavailable (audio device busy) - "
                "loading the file will still work fine",
                juce::dontSendNotification);
            return;
        }

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

// The full dialog content: a file browser + preview panel + OK/Cancel —
// built as a plain Component launched via an ASYNC DialogWindow (never a
// blocking modal loop, which is disallowed for plugins since it can
// freeze the host). Double-clicking a file confirms immediately, same as
// a native file dialog.
class SampleBrowserDialogContent : public juce::Component,
                                   private juce::FileBrowserListener
{
public:
    SampleBrowserDialogContent(const juce::File& startFolder,
                               const juce::String& wildcardPatterns,
                               std::function<void(const juce::File&)> callback)
        : onFileChosen(std::move(callback)),
          filter(wildcardPatterns, "", "Audio files"),
          previewOwner(std::make_unique<SamplePreviewComponent>()),
          browser(juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles,
                  startFolder, &filter, previewOwner.get())
    {
        addAndMakeVisible(browser);
        browser.addListener(this);

        okButton.setButtonText("Load");
        cancelButton.setButtonText("Cancel");
        addAndMakeVisible(okButton);
        addAndMakeVisible(cancelButton);

        okButton.onClick = [this] { confirmAndClose(); };
        cancelButton.onClick = [this] { cancelAndClose(); };

        setSize(760, 520);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(8);
        auto buttonRow = r.removeFromBottom(36);
        okButton.setBounds(buttonRow.removeFromRight(100));
        buttonRow.removeFromRight(8);
        cancelButton.setBounds(buttonRow.removeFromRight(100));
        r.removeFromBottom(8);
        browser.setBounds(r);
    }

private:
    void confirmAndClose()
    {
        auto file = browser.getSelectedFile(0);
        onFileChosen(file);
        closeDialog();
    }

    void cancelAndClose()
    {
        onFileChosen(juce::File{});
        closeDialog();
    }

    void closeDialog()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    }

    // FileBrowserListener — double-click confirms immediately.
    void selectionChanged() override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override { confirmAndClose(); }
    void browserRootChanged(const juce::File&) override {}

    std::function<void(const juce::File&)> onFileChosen;
    juce::WildcardFileFilter filter;
    std::unique_ptr<SamplePreviewComponent> previewOwner;
    juce::FileBrowserComponent browser;
    juce::TextButton okButton, cancelButton;
};

// Shows an ASYNC (plugin-safe) file-browse dialog with live audio preview,
// remembers the last folder used, and calls `onFileChosen` with the
// selected file (or an empty File if cancelled). Never blocks the message
// thread — safe to call from inside a plugin editor.
inline void browseForSampleWithPreview(
    juce::File& lastFolder,
    const juce::String& dialogTitle,
    const juce::String& wildcardPatterns,
    std::function<void(const juce::File&)> onFileChosen)
{
    auto startFolder = lastFolder.isDirectory()
        ? lastFolder
        : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    // lastFolder is captured by reference into the callback so it updates
    // the moment a file is actually chosen.
    auto wrappedCallback = [&lastFolder, onFileChosen](const juce::File& chosen)
    {
        if (chosen != juce::File{})
            lastFolder = chosen.getParentDirectory();

        onFileChosen(chosen);
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = dialogTitle;
    options.dialogBackgroundColour = juce::Colours::darkgrey;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.content.setOwned(
        new SampleBrowserDialogContent(startFolder, wildcardPatterns, wrappedCallback));

    options.launchAsync();
}
