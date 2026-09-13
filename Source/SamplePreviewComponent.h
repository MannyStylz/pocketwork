#pragma once
#include <JuceHeader.h>

// A small, self-contained audio player used only for previewing files in
// the browser dialog below. It owns its own audio device connection —
// completely separate from the host's audio engine — purely so you can
// click a file and hear it before deciding to load it into the plugin.
class SamplePreviewPlayer
{
public:
    SamplePreviewPlayer()
    {
        formatManager.registerBasicFormats();
        deviceManager.initialiseWithDefaultDevices(0, 2);

        // Prefer WASAPI "Windows Audio" (shared mode) over whatever the
        // default device type is — ASIO drivers typically claim the
        // audio device EXCLUSIVELY, which can silently block this
        // separate preview connection from opening at all.
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

    void loadFile(const juce::File& file)
    {
        transportSource.stop();

        // CRITICAL ORDER: detach the old source from the transport
        // BEFORE destroying it (safely synchronized via JUCE's internal
        // locking), never after — the reverse order caused an
        // intermittent use-after-free crash.
        transportSource.setSource(nullptr);
        readerSource.reset();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file));

        if (reader == nullptr)
            return; // Honest: unreadable file, nothing to preview.

        totalLengthSeconds = reader->lengthInSamples / juce::jmax(1.0, reader->sampleRate);

        readerSource = std::make_unique<juce::AudioFormatReaderSource>(
            reader.release(), true);

        transportSource.setSource(readerSource.get(), 0, nullptr,
                                  readerSource->getAudioFormatReader() != nullptr
                                      ? readerSource->getAudioFormatReader()->sampleRate
                                      : 44100.0);
    }

    void play() { transportSource.start(); }
    void stop() { transportSource.stop(); }
    void setPositionSeconds(double s) { transportSource.setPosition(s); }
    double getCurrentPositionSeconds() const { return transportSource.getCurrentPosition(); }
    double getTotalLengthSeconds() const { return totalLengthSeconds; }

    bool isDeviceAvailable() const { return deviceManager.getCurrentAudioDevice() != nullptr; }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    double totalLengthSeconds = 0.0;
};

// The preview panel: shows a waveform of the selected file with two
// draggable trim handles (start/end), loops playback of just the
// selected range so you can audition exactly the section you want before
// committing, and reports that trim range back when you click Load.
class SamplePreviewComponent : public juce::FilePreviewComponent,
                               private juce::Timer,
                               private juce::ChangeListener
{
public:
    SamplePreviewComponent()
        : thumbnailCache(4),
          thumbnail(512, formatManager, thumbnailCache)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener(this);

        stopButton.setButtonText("Stop Preview");
        stopButton.onClick = [this] { player.stop(); };
        addAndMakeVisible(stopButton);

        nowPreviewingLabel.setJustificationType(juce::Justification::centredLeft);
        nowPreviewingLabel.setText("Click a file to hear it",
                                   juce::dontSendNotification);
        addAndMakeVisible(nowPreviewingLabel);

        trimInfoLabel.setJustificationType(juce::Justification::centredLeft);
        trimInfoLabel.setFont(juce::Font(11.0f));
        trimInfoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(trimInfoLabel);

        setSize(360, 220);
        startTimerHz(20);
    }

    ~SamplePreviewComponent() override
    {
        thumbnail.removeChangeListener(this);
    }

    void selectedFileChanged(const juce::File& newFile) override
    {
        if (newFile == juce::File{} || newFile.isDirectory())
            return;

        currentFile = newFile;
        player.loadFile(newFile);
        thumbnail.setSource(new juce::FileInputSource(newFile));

        // Default trim range: the whole file. Waits for the thumbnail
        // to report its real length (see changeListenerCallback) since
        // that happens asynchronously.
        trimStartSeconds = 0.0;
        trimEndSeconds = -1.0; // -1 = "not known yet"

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
        player.setPositionSeconds(0.0);
        player.play();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(4);
        nowPreviewingLabel.setBounds(r.removeFromTop(20));
        r.removeFromTop(4);
        waveformArea = r.removeFromTop(120);
        r.removeFromTop(4);
        trimInfoLabel.setBounds(r.removeFromTop(18));
        r.removeFromTop(4);
        stopButton.setBounds(r.removeFromTop(28).removeFromLeft(140));
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff1b1712));
        g.fillRect(waveformArea);

        double total = thumbnail.getTotalLength();

        if (total <= 0.0)
        {
            g.setColour(juce::Colours::grey);
            g.drawText("Loading waveform...", waveformArea,
                      juce::Justification::centred);
            return;
        }

        // Resolve trimEnd now that we know the real length, if it was
        // still pending (-1 = "not known yet" from selectedFileChanged).
        if (trimEndSeconds < 0.0)
            trimEndSeconds = total;

        g.setColour(juce::Colour(0xffe4a52a));
        thumbnail.drawChannels(g, waveformArea, 0.0, total, 1.0f);

        // Shade the non-selected (trimmed-away) regions.
        auto xForTime = [&](double t) -> int
        {
            return waveformArea.getX() +
                   (int) ((t / total) * waveformArea.getWidth());
        };

        int startX = xForTime(trimStartSeconds);
        int endX = xForTime(trimEndSeconds);

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        if (startX > waveformArea.getX())
            g.fillRect(waveformArea.getX(), waveformArea.getY(),
                      startX - waveformArea.getX(), waveformArea.getHeight());
        if (endX < waveformArea.getRight())
            g.fillRect(endX, waveformArea.getY(),
                      waveformArea.getRight() - endX, waveformArea.getHeight());

        // Draw the two drag handles.
        g.setColour(juce::Colours::lime);
        g.fillRect(startX - 2, waveformArea.getY(), 4, waveformArea.getHeight());
        g.fillRect(endX - 2, waveformArea.getY(), 4, waveformArea.getHeight());

        // Playhead, while previewing.
        double pos = player.getCurrentPositionSeconds();
        int playX = xForTime(pos);
        g.setColour(juce::Colours::white);
        g.drawVerticalLine(playX, (float) waveformArea.getY(),
                          (float) waveformArea.getBottom());
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!waveformArea.contains(e.getPosition()) || thumbnail.getTotalLength() <= 0.0)
        {
            draggingHandle = 0;
            return;
        }

        double total = thumbnail.getTotalLength();
        int startX = waveformArea.getX() +
                    (int) ((trimStartSeconds / total) * waveformArea.getWidth());
        int endX = waveformArea.getX() +
                  (int) ((trimEndSeconds / total) * waveformArea.getWidth());

        int mouseX = e.x;
        const int grabRadius = 8;

        if (std::abs(mouseX - startX) <= grabRadius)
            draggingHandle = 1;
        else if (std::abs(mouseX - endX) <= grabRadius)
            draggingHandle = 2;
        else
            draggingHandle = 0;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingHandle == 0 || thumbnail.getTotalLength() <= 0.0)
            return;

        double total = thumbnail.getTotalLength();
        double t = juce::jlimit(0.0, total,
            ((double) (e.x - waveformArea.getX()) / waveformArea.getWidth()) * total);

        if (draggingHandle == 1)
            trimStartSeconds = juce::jmin(t, trimEndSeconds - 0.02);
        else if (draggingHandle == 2)
            trimEndSeconds = juce::jmax(t, trimStartSeconds + 0.02);

        trimStartSeconds = juce::jmax(0.0, trimStartSeconds);
        trimEndSeconds = juce::jmin(total, trimEndSeconds);

        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { draggingHandle = 0; }

    double getTrimStartSeconds() const { return trimStartSeconds; }
    double getTrimEndSeconds() const { return trimEndSeconds; }
    juce::File getCurrentFile() const { return currentFile; }

private:
    void timerCallback() override
    {
        // Loop playback of just the selected trim range.
        if (trimEndSeconds > trimStartSeconds
            && player.getCurrentPositionSeconds() >= trimEndSeconds)
        {
            player.setPositionSeconds(trimStartSeconds);
        }

        juce::String info = "Selection: " + juce::String(trimStartSeconds, 2)
                           + "s to " + juce::String(trimEndSeconds, 2) + "s   "
                           + "(drag the green handles to trim)";
        trimInfoLabel.setText(info, juce::dontSendNotification);

        repaint(waveformArea);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        // Thumbnail finished loading (or updated) — resolve the pending
        // trim-end-unknown state and start looping playback now that we
        // know the real length.
        if (trimEndSeconds < 0.0 && thumbnail.getTotalLength() > 0.0)
            trimEndSeconds = thumbnail.getTotalLength();

        repaint();
    }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;
    SamplePreviewPlayer player;

    juce::TextButton stopButton;
    juce::Label nowPreviewingLabel;
    juce::Label trimInfoLabel;
    juce::Rectangle<int> waveformArea;

    juce::File currentFile;
    double trimStartSeconds = 0.0;
    double trimEndSeconds = -1.0;
    int draggingHandle = 0; // 0 = none, 1 = start, 2 = end
};

// The full dialog content: file browser + trim/preview panel + OK/Cancel
// — built as a plain Component launched via an ASYNC DialogWindow (never
// a blocking modal loop, which is disallowed for plugins). Double-
// clicking a file confirms with the full file (no trim), same as a
// native dialog; the "Load" button confirms with whatever trim range is
// currently selected.
class SampleBrowserDialogContent : public juce::Component,
                                   private juce::FileBrowserListener
{
public:
    // Callback receives: chosen file, trim start (seconds), trim end
    // (seconds). trimEnd <= trimStart means "no trim, use the whole
    // file" (e.g. on double-click or Cancel).
    using ChosenCallback = std::function<void(const juce::File&, double, double)>;

    SampleBrowserDialogContent(const juce::File& startFolder,
                               const juce::String& wildcardPatterns,
                               ChosenCallback callback)
        : onFileChosen(std::move(callback)),
          filter(wildcardPatterns, "", "Audio files"),
          previewOwner(std::make_unique<SamplePreviewComponent>()),
          browser(juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles,
                  startFolder, &filter, previewOwner.get())
    {
        addAndMakeVisible(browser);
        browser.addListener(this);

        okButton.setButtonText("Load Trimmed Selection");
        cancelButton.setButtonText("Cancel");
        addAndMakeVisible(okButton);
        addAndMakeVisible(cancelButton);

        okButton.onClick = [this] { confirmWithTrim(); };
        cancelButton.onClick = [this] { cancelAndClose(); };

        setSize(920, 560);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(8);
        auto buttonRow = r.removeFromBottom(36);
        okButton.setBounds(buttonRow.removeFromRight(220));
        buttonRow.removeFromRight(8);
        cancelButton.setBounds(buttonRow.removeFromRight(100));
        r.removeFromBottom(8);
        browser.setBounds(r);
    }

private:
    void confirmWithTrim()
    {
        auto file = previewOwner->getCurrentFile() != juce::File{}
            ? previewOwner->getCurrentFile()
            : browser.getSelectedFile(0);

        onFileChosen(file, previewOwner->getTrimStartSeconds(),
                    previewOwner->getTrimEndSeconds());
        closeDialog();
    }

    void cancelAndClose()
    {
        onFileChosen(juce::File{}, 0.0, 0.0);
        closeDialog();
    }

    void closeDialog()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    }

    // Double-click loads the WHOLE file (no trim) immediately, matching
    // native file-dialog expectations.
    void selectionChanged() override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& file) override
    {
        onFileChosen(file, 0.0, 0.0);
        closeDialog();
    }
    void browserRootChanged(const juce::File&) override {}

    ChosenCallback onFileChosen;
    juce::WildcardFileFilter filter;
    std::unique_ptr<SamplePreviewComponent> previewOwner;
    juce::FileBrowserComponent browser;
    juce::TextButton okButton, cancelButton;
};

// Shows an ASYNC (plugin-safe) file-browse dialog with waveform preview
// and trim selection, remembers the last folder used, and calls
// `onFileChosen` with the selected file plus trim start/end (in
// seconds). trimEnd <= trimStart means "use the whole file."
inline void browseForSampleWithPreview(
    juce::File& lastFolder,
    const juce::String& dialogTitle,
    const juce::String& wildcardPatterns,
    std::function<void(const juce::File&, double trimStartSeconds,
                       double trimEndSeconds)> onFileChosen)
{
    auto startFolder = lastFolder.isDirectory()
        ? lastFolder
        : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    auto wrappedCallback = [&lastFolder, onFileChosen]
        (const juce::File& chosen, double trimStart, double trimEnd)
    {
        if (chosen != juce::File{})
            lastFolder = chosen.getParentDirectory();

        onFileChosen(chosen, trimStart, trimEnd);
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
