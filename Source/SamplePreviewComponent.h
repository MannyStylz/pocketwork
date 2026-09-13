#pragma once
#include <JuceHeader.h>

// A small, self-contained audio player used only for previewing files in
// the browser dialog below. It owns its own audio device connection —
// completely separate from the host's audio engine.
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
        // BEFORE destroying it — the reverse order caused an
        // intermittent use-after-free crash.
        transportSource.setSource(nullptr);
        readerSource.reset();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file));

        if (reader == nullptr)
            return;

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
    bool isPlaying() const { return transportSource.isPlaying(); }
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

// The preview panel: waveform with draggable trim handles, mouse-wheel
// zoom for precise trim placement, and a Play/Stop toggle that plays
// ONLY the selected range once, then reverts to "Preview" — press again
// to replay, no need to reselect the file.
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

        playStopButton.setButtonText("\xE2\x96\xB6 Preview");
        playStopButton.onClick = [this] { togglePlayStop(); };
        addAndMakeVisible(playStopButton);

        deleteButton.setButtonText("Delete File...");
        deleteButton.setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff5a2020));
        deleteButton.onClick = [this] { confirmDelete(); };
        addAndMakeVisible(deleteButton);

        nowPreviewingLabel.setJustificationType(juce::Justification::centredLeft);
        nowPreviewingLabel.setText("Click a file to hear it",
                                   juce::dontSendNotification);
        addAndMakeVisible(nowPreviewingLabel);

        trimInfoLabel.setJustificationType(juce::Justification::centredLeft);
        trimInfoLabel.setFont(juce::Font(11.0f));
        trimInfoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(trimInfoLabel);

        setSize(420, 320);
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

        trimStartSeconds = 0.0;
        trimEndSeconds = -1.0; // resolved once thumbnail reports real length
        viewStartSeconds = 0.0;
        viewLengthSeconds = -1.0; // -1 = "show whole file", resolved below too

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
        startPreviewPlayback();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(4);
        nowPreviewingLabel.setBounds(r.removeFromTop(20));
        r.removeFromTop(4);
        waveformArea = r.removeFromTop(190);
        r.removeFromTop(4);
        trimInfoLabel.setBounds(r.removeFromTop(18));
        r.removeFromTop(4);
        auto buttonRow = r.removeFromTop(28);
        playStopButton.setBounds(buttonRow.removeFromLeft(160));
        buttonRow.removeFromLeft(8);
        deleteButton.setBounds(buttonRow.removeFromLeft(140));
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

        if (trimEndSeconds < 0.0)
            trimEndSeconds = total;
        if (viewLengthSeconds < 0.0)
            viewLengthSeconds = total;

        g.setColour(juce::Colour(0xffe4a52a));
        thumbnail.drawChannels(g, waveformArea, viewStartSeconds,
                              viewStartSeconds + viewLengthSeconds, 1.0f);

        auto xForTime = [&](double t) -> int
        {
            double frac = (t - viewStartSeconds) / juce::jmax(0.0001, viewLengthSeconds);
            return waveformArea.getX() + (int) (frac * waveformArea.getWidth());
        };

        int startX = xForTime(trimStartSeconds);
        int endX = xForTime(trimEndSeconds);

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        if (startX > waveformArea.getX())
            g.fillRect(waveformArea.getX(), waveformArea.getY(),
                      juce::jlimit(0, waveformArea.getWidth(), startX - waveformArea.getX()),
                      waveformArea.getHeight());
        if (endX < waveformArea.getRight())
            g.fillRect(juce::jmax(waveformArea.getX(), endX), waveformArea.getY(),
                      juce::jmax(0, waveformArea.getRight() - juce::jmax(waveformArea.getX(), endX)),
                      waveformArea.getHeight());

        g.setColour(juce::Colours::lime);
        if (startX >= waveformArea.getX() && startX <= waveformArea.getRight())
            g.fillRect(startX - 2, waveformArea.getY(), 4, waveformArea.getHeight());
        if (endX >= waveformArea.getX() && endX <= waveformArea.getRight())
            g.fillRect(endX - 2, waveformArea.getY(), 4, waveformArea.getHeight());

        double pos = player.getCurrentPositionSeconds();
        int playX = xForTime(pos);
        if (playX >= waveformArea.getX() && playX <= waveformArea.getRight())
        {
            g.setColour(juce::Colours::white);
            g.drawVerticalLine(playX, (float) waveformArea.getY(),
                              (float) waveformArea.getBottom());
        }

        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(10.0f));
        g.drawText("Scroll wheel to zoom for precise trimming",
                  waveformArea.getX(), waveformArea.getBottom() - 14,
                  waveformArea.getWidth(), 14, juce::Justification::left);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!waveformArea.contains(e.getPosition()) || thumbnail.getTotalLength() <= 0.0)
        {
            draggingHandle = 0;
            return;
        }

        int startX = xForTimeInternal(trimStartSeconds);
        int endX = xForTimeInternal(trimEndSeconds);

        const int grabRadius = 8;

        if (std::abs(e.x - startX) <= grabRadius)
            draggingHandle = 1;
        else if (std::abs(e.x - endX) <= grabRadius)
            draggingHandle = 2;
        else
            draggingHandle = 0;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingHandle == 0 || thumbnail.getTotalLength() <= 0.0)
            return;

        double t = timeForXInternal(e.x);

        if (draggingHandle == 1)
            trimStartSeconds = juce::jmin(t, trimEndSeconds - 0.02);
        else if (draggingHandle == 2)
            trimEndSeconds = juce::jmax(t, trimStartSeconds + 0.02);

        trimStartSeconds = juce::jmax(0.0, trimStartSeconds);
        trimEndSeconds = juce::jmin(thumbnail.getTotalLength(), trimEndSeconds);

        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { draggingHandle = 0; }

    // Mouse-wheel zoom, centered on the cursor position, for precise
    // trim-handle placement on longer files.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        double total = thumbnail.getTotalLength();
        if (total <= 0.0 || !waveformArea.contains(e.getPosition()))
            return;

        double cursorTime = timeForXInternal(e.x);
        double zoomFactor = wheel.deltaY > 0 ? 0.8 : 1.25;

        double newLength = juce::jlimit(0.05, total, viewLengthSeconds * zoomFactor);
        double frac = (cursorTime - viewStartSeconds) / juce::jmax(0.0001, viewLengthSeconds);

        viewStartSeconds = juce::jlimit(0.0, juce::jmax(0.0, total - newLength),
                                        cursorTime - frac * newLength);
        viewLengthSeconds = newLength;

        repaint();
    }

    double getTrimStartSeconds() const { return trimStartSeconds; }
    double getTrimEndSeconds() const { return trimEndSeconds; }
    juce::File getCurrentFile() const { return currentFile; }

private:
    int xForTimeInternal(double t) const
    {
        double frac = (t - viewStartSeconds) / juce::jmax(0.0001, viewLengthSeconds);
        return waveformArea.getX() + (int) (frac * waveformArea.getWidth());
    }

    double timeForXInternal(int x) const
    {
        double frac = (double) (x - waveformArea.getX()) / juce::jmax(1, waveformArea.getWidth());
        return juce::jlimit(0.0, thumbnail.getTotalLength(),
                            viewStartSeconds + frac * viewLengthSeconds);
    }

    void startPreviewPlayback()
    {
        player.setPositionSeconds(trimStartSeconds);
        player.play();
        playStopButton.setButtonText("\xE2\x96\xA0 Stop");
    }

    void togglePlayStop()
    {
        if (player.isPlaying())
        {
            player.stop();
            playStopButton.setButtonText("\xE2\x96\xB6 Preview");
        }
        else
        {
            startPreviewPlayback();
        }
    }

    void confirmDelete()
    {
        if (currentFile == juce::File{})
            return;

        auto fileToDelete = currentFile;

        juce::NativeMessageBox::showYesNoBox(
            juce::MessageBoxIconType::WarningIcon,
            "Delete file?",
            "Permanently delete this file from your hard drive?\n\n"
                + fileToDelete.getFullPathName()
                + "\n\nThis cannot be undone.",
            nullptr,
            juce::ModalCallbackFunction::create([this, fileToDelete](int result)
            {
                if (result != 1) // 1 = Yes
                    return;

                player.stop();
                playStopButton.setButtonText("\xE2\x96\xB6 Preview");

                if (fileToDelete.deleteFile())
                {
                    nowPreviewingLabel.setText(
                        "Deleted: " + fileToDelete.getFileName(),
                        juce::dontSendNotification);
                    currentFile = juce::File{};
                    thumbnail.clear();
                    if (onFileDeleted)
                        onFileDeleted();
                }
                else
                {
                    nowPreviewingLabel.setText(
                        "Could not delete that file (in use, or permissions?)",
                        juce::dontSendNotification);
                }
            }));
    }

    void timerCallback() override
    {
        // Natural stop at the end of the selected range — no auto-loop.
        // Once stopped (by reaching the end OR by pressing Stop), the
        // button reverts to "Preview" so pressing it again replays the
        // SAME file without needing to reselect it.
        if (player.isPlaying() && player.getCurrentPositionSeconds() >= trimEndSeconds)
        {
            player.stop();
            playStopButton.setButtonText("\xE2\x96\xB6 Preview");
        }

        juce::String info = "Selection: " + juce::String(trimStartSeconds, 2)
                           + "s to " + juce::String(trimEndSeconds, 2) + "s";
        trimInfoLabel.setText(info, juce::dontSendNotification);

        repaint(waveformArea);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (trimEndSeconds < 0.0 && thumbnail.getTotalLength() > 0.0)
            trimEndSeconds = thumbnail.getTotalLength();
        if (viewLengthSeconds < 0.0 && thumbnail.getTotalLength() > 0.0)
            viewLengthSeconds = thumbnail.getTotalLength();

        repaint();
    }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;
    SamplePreviewPlayer player;

    juce::TextButton playStopButton;
    juce::TextButton deleteButton;
    juce::Label nowPreviewingLabel;
    juce::Label trimInfoLabel;
    juce::Rectangle<int> waveformArea;

    juce::File currentFile;
    double trimStartSeconds = 0.0;
    double trimEndSeconds = -1.0;
    double viewStartSeconds = 0.0;
    double viewLengthSeconds = -1.0;
    int draggingHandle = 0; // 0 = none, 1 = start, 2 = end

public:
    // Set by the owning dialog so the browser's file list can be
    // refreshed after a deletion.
    std::function<void()> onFileDeleted;
};

// The full dialog content: file browser + trim/preview panel + OK/Cancel
// — an async, plugin-safe dialog (never a blocking modal loop).
class SampleBrowserDialogContent : public juce::Component,
                                   private juce::FileBrowserListener
{
public:
    // Callback receives: chosen file, trim start (seconds), trim end
    // (seconds). trimEnd <= trimStart means "no trim, use the whole file."
    using ChosenCallback = std::function<void(const juce::File&, double, double)>;

    SampleBrowserDialogContent(const juce::File& startFolder,
                               const juce::File& preselectFile,
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

        previewOwner->onFileDeleted = [this] { browser.refresh(); };

        okButton.setButtonText("Load Trimmed Selection");
        cancelButton.setButtonText("Cancel");
        addAndMakeVisible(okButton);
        addAndMakeVisible(cancelButton);

        okButton.onClick = [this] { confirmWithTrim(); };
        cancelButton.onClick = [this] { cancelAndClose(); };

        setSize(1000, 700);

        // Pre-select the file this slot was last loaded from, if any,
        // so you don't have to hunt for it again to swap it out.
        if (preselectFile != juce::File{} && preselectFile.existsAsFile())
            browser.setSelectedFile(preselectFile);
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

// Shows an ASYNC (plugin-safe) file-browse dialog with waveform trim
// preview. preselectFile (optional) highlights a specific file on open —
// e.g. the one currently loaded in this slot, so swapping it is easy.
inline void browseForSampleWithPreview(
    juce::File& lastFolder,
    const juce::File& preselectFile,
    const juce::String& dialogTitle,
    const juce::String& wildcardPatterns,
    std::function<void(const juce::File&, double trimStartSeconds,
                       double trimEndSeconds)> onFileChosen)
{
    auto startFolder = preselectFile != juce::File{} && preselectFile.getParentDirectory().isDirectory()
        ? preselectFile.getParentDirectory()
        : (lastFolder.isDirectory()
            ? lastFolder
            : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

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
        new SampleBrowserDialogContent(startFolder, preselectFile,
                                       wildcardPatterns, wrappedCallback));

    if (auto* dw = options.launchAsync())
    {
        // Use a full border resizer (all four edges), not just the
        // bottom-right corner, so the window can be resized vertically
        // too, not just horizontally.
        dw->setResizable(true, false);
        dw->setResizeLimits(500, 400, 1600, 1200);
    }
}
