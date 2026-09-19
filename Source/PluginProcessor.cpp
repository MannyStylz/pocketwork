#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Embeds a loaded sample's actual audio data directly into the
    // plugin's saved state (as base64-encoded WAV), rather than just a
    // file path. This is what makes loaded samples survive a state
    // save/restore cycle even if the original file moves, is deleted,
    // or the host refreshes plugin state without a full project save.
    juce::String bufferToBase64Wav(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        if (buffer.getNumSamples() <= 0)
            return {};

        // Safety cap: never embed something huge into the project's
        // saved state. A one-shot kick/snare/hat is always tiny; if
        // something absurdly long ever ends up here, skip embedding it
        // rather than risk a giant state blob crashing on save.
        constexpr double maxSecondsToEmbed = 30.0;
        if (buffer.getNumSamples() / juce::jmax(1.0, sampleRate) > maxSecondsToEmbed)
            return {};

        juce::MemoryBlock mb;

        // CRITICAL FIX: AudioFormatWriter takes OWNERSHIP of the stream
        // pointer passed to createWriterFor() and deletes it internally
        // when the writer is destroyed. The previous code passed the
        // address of a STACK-allocated MemoryOutputStream — meaning the
        // writer eventually tried to delete a non-heap pointer, which is
        // undefined behavior and a real crash. This is heap-allocated
        // now so that ownership transfer is actually safe.
        auto* mos = new juce::MemoryOutputStream(mb, false);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(mos, sampleRate,
                                      (unsigned int) buffer.getNumChannels(),
                                      16, {}, 0));

        if (writer == nullptr)
        {
            delete mos; // createWriterFor failed and never took ownership
            return {};
        }

        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        writer.reset(); // flushes the WAV data, then safely deletes mos too

        return juce::Base64::toBase64(mb.getData(), mb.getSize());
    }

    bool base64WavToBuffer(const juce::String& base64,
                           juce::AudioBuffer<float>& outBuffer, double& outSampleRate)
    {
        if (base64.isEmpty())
            return false;

        juce::MemoryOutputStream decoded;
        if (!juce::Base64::convertFromBase64(decoded, base64))
            return false;

        // CRITICAL FIX (v2.1): same ownership issue the writer side had
        // in v2.0 — AudioFormatReader takes OWNERSHIP of the stream
        // pointer passed to createReaderFor() and deletes it internally
        // when the reader is destroyed. The previous code passed the
        // address of a STACK-allocated MemoryInputStream — meaning the
        // reader eventually tried to delete a non-heap pointer, which is
        // undefined behavior. This only ran when embedded audio was
        // decoded back out of a saved project (i.e. on reopen), which is
        // exactly why it worked fine at save time but crashed on reload,
        // identically across hosts. Heap-allocated now so ownership
        // transfer is actually safe.
        auto* mis = new juce::MemoryInputStream(decoded.getData(), decoded.getDataSize(), false);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader(
            wavFormat.createReaderFor(mis, false));

        if (reader == nullptr)
        {
            delete mis; // createReaderFor failed and never took ownership
            return false;
        }

        outBuffer.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&outBuffer, 0, (int) reader->lengthInSamples, 0, true, true);
        outSampleRate = reader->sampleRate;
        return true;
    }

    // REAL FIX for playback being slower/faster than the original: if a
    // loaded file's native sample rate doesn't match the project's, it
    // must be resampled — otherwise playing it back sample-for-sample at
    // the project's rate genuinely does change its speed and pitch.
    // Uses JUCE's LagrangeInterpolator, a standard, real resampling
    // technique (not a placeholder).
    void resampleBufferIfNeeded(juce::AudioBuffer<float>& buffer,
                                double sourceRate, double targetRate)
    {
        if (sourceRate <= 0.0 || targetRate <= 0.0
            || std::abs(sourceRate - targetRate) < 0.5
            || buffer.getNumSamples() <= 0)
            return; // already matching (or nothing to do) — no change needed

        double ratio = sourceRate / targetRate;
        int numChannels = buffer.getNumChannels();
        int oldNumSamples = buffer.getNumSamples();
        int newNumSamples = juce::jmax(1, (int) std::ceil(oldNumSamples / ratio));

        juce::AudioBuffer<float> resampled(numChannels, newNumSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process(ratio, buffer.getReadPointer(ch),
                                resampled.getWritePointer(ch), newNumSamples);
        }

        buffer.makeCopyOf(resampled);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
PocketWorkAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamIDs::sensitivity, "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamIDs::pocket, "Pocket",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamIDs::dynamics, "Dynamics",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamIDs::breakbeatBpm, "Source BPM",
        juce::NormalisableRange<float>(40.0f, 240.0f), 120.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        ParamIDs::breakbeatBars, "Source Bars", 1, 8, 1));

    return { params.begin(), params.end() };
}

PocketWorkAudioProcessor::PocketWorkAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sensitivityParam   = apvts.getRawParameterValue(ParamIDs::sensitivity);
    pocketParam        = apvts.getRawParameterValue(ParamIDs::pocket);
    dynamicsParam      = apvts.getRawParameterValue(ParamIDs::dynamics);
    breakbeatBpmParam  = apvts.getRawParameterValue(ParamIDs::breakbeatBpm);
    breakbeatBarsParam = apvts.getRawParameterValue(ParamIDs::breakbeatBars);

    formatManager.registerBasicFormats();
}

void PocketWorkAudioProcessor::prepareToPlay(double sampleRate,
                                               int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    groove.prepare(sampleRate);
    setLatencySamples(groove.getLatencySamples());

    for (auto& v : voices)
        v.active = false;
}

void PocketWorkAudioProcessor::releaseResources()
{
}

bool PocketWorkAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

bool PocketWorkAudioProcessor::loadBreakbeatFile(const juce::File& file,
                                                 double trimStartSeconds,
                                                 double trimEndSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return false;

    breakbeatPlaying.store(false);

    const int numChannels = 2;
    const double sr = reader->sampleRate;
    const juce::int64 totalSamples = reader->lengthInSamples;

    juce::int64 startSample = 0;
    juce::int64 numSamples = totalSamples;

    // A negative trim value means "use the whole file" — the default
    // when no trim was set. Otherwise, only read the trimmed range.
    if (trimStartSeconds >= 0.0 && trimEndSeconds > trimStartSeconds)
    {
        startSample = juce::jlimit((juce::int64) 0, totalSamples,
                                   (juce::int64) (trimStartSeconds * sr));
        juce::int64 endSample = juce::jlimit((juce::int64) 0, totalSamples,
                                            (juce::int64) (trimEndSeconds * sr));
        numSamples = juce::jmax((juce::int64) 0, endSample - startSample);
    }

    breakbeatBuffer.setSize(numChannels, (int) numSamples);
    reader->read(&breakbeatBuffer, 0, (int) numSamples, startSample, true, true);

    // Convert to the project's actual sample rate, if known and
    // different — this is the real fix for playback speed being off.
    if (getSampleRate() > 0.0)
    {
        resampleBufferIfNeeded(breakbeatBuffer, sr, getSampleRate());
        breakbeatSourceSampleRate = getSampleRate();
    }
    else
    {
        breakbeatSourceSampleRate = sr;
    }

    loadedFileName = file.getFileName();
    breakbeatPlayPosition.store(0);
    mostRecentLoadedFilePath = file.getFullPathName();

    // FIX: loading a breakbeat changes what needs to be saved, but it
    // isn't an apvts parameter change, so the host has no other way of
    // knowing the plugin's state is now dirty. Without this, some hosts
    // (FL Studio included) may save a stale/earlier state snapshot
    // instead of freshly querying getStateInformation() — meaning the
    // load works perfectly all session, but doesn't survive a project
    // save/reopen. This is the real fix for that.
    updateHostDisplay();

    return true;
}

void PocketWorkAudioProcessor::setPlaybackState(bool shouldPlay, PlaybackMode mode)
{
    if (shouldPlay)
        breakbeatPlayPosition.store(0);

    playbackMode.store(mode);
    breakbeatPlaying.store(shouldPlay);
}

juce::String PocketWorkAudioProcessor::getFolderForSlot(BrowseSlot slot) const
{
    switch (slot)
    {
        case BrowseSlot::Kick:      return kickFolderPath;
        case BrowseSlot::Snare:     return snareFolderPath;
        case BrowseSlot::Hat:       return hatFolderPath;
        case BrowseSlot::Breakbeat: return breakbeatFolderPath;
        case BrowseSlot::Export:    return exportFolderPath;
    }
    return {};
}

void PocketWorkAudioProcessor::setFolderForSlot(BrowseSlot slot, const juce::String& path)
{
    switch (slot)
    {
        case BrowseSlot::Kick:      kickFolderPath = path; break;
        case BrowseSlot::Snare:     snareFolderPath = path; break;
        case BrowseSlot::Hat:       hatFolderPath = path; break;
        case BrowseSlot::Breakbeat: breakbeatFolderPath = path; break;
        case BrowseSlot::Export:    exportFolderPath = path; break;
    }
}

juce::String PocketWorkAudioProcessor::getFileForSlot(BrowseSlot slot) const
{
    switch (slot)
    {
        case BrowseSlot::Kick:      return kickFilePath;
        case BrowseSlot::Snare:     return snareFilePath;
        case BrowseSlot::Hat:       return hatFilePath;
        case BrowseSlot::Breakbeat: return breakbeatFilePath;
        case BrowseSlot::Export:    return exportFilePath;
    }
    return {};
}

void PocketWorkAudioProcessor::setFileForSlot(BrowseSlot slot, const juce::String& path)
{
    switch (slot)
    {
        case BrowseSlot::Kick:      kickFilePath = path; break;
        case BrowseSlot::Snare:     snareFilePath = path; break;
        case BrowseSlot::Hat:       hatFilePath = path; break;
        case BrowseSlot::Breakbeat: breakbeatFilePath = path; break;
        case BrowseSlot::Export:    exportFilePath = path; break;
    }
}

void PocketWorkAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData)
{
    // FIX: our custom data (folder/file paths and, critically, the
    // multi-megabyte base64-encoded audio blobs) used to be written as
    // attributes directly onto the SAME XmlElement that also got fed
    // straight into apvts.replaceState() on load. That meant every
    // save/load cycle stuffed megabytes of raw audio data into
    // AudioProcessorValueTreeState's own internal ValueTree, which it
    // was never designed to hold — and only became a real problem once
    // actual audio was being embedded, which is exactly when the VST3
    // load failures started. Our data now lives in a completely
    // separate child element, so it can never leak into APVTS's state.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> paramsXml(state.createXml());

    juce::XmlElement root("POCKETWORK_STATE");
    root.addChildElement(paramsXml.release());

    auto* dataXml = root.createNewChildElement("PluginData");
    dataXml->setAttribute("folderKick", kickFolderPath);
    dataXml->setAttribute("folderSnare", snareFolderPath);
    dataXml->setAttribute("folderHat", hatFolderPath);
    dataXml->setAttribute("folderBreakbeat", breakbeatFolderPath);
    dataXml->setAttribute("folderExport", exportFolderPath);
    dataXml->setAttribute("fileKick", kickFilePath);
    dataXml->setAttribute("fileSnare", snareFilePath);
    dataXml->setAttribute("fileHat", hatFilePath);
    dataXml->setAttribute("fileBreakbeat", breakbeatFilePath);
    dataXml->setAttribute("mostRecentFile", mostRecentLoadedFilePath);

    // CRITICAL: embed the actual trimmed audio data, not just a path —
    // this is what makes loaded samples survive a state refresh even if
    // the original file has moved, been deleted, or the host reloads
    // plugin state without a full project save.
    dataXml->setAttribute("kickAudio", bufferToBase64Wav(kickSample, kickSampleRate));
    dataXml->setAttribute("snareAudio", bufferToBase64Wav(snareSample, snareSampleRate));
    dataXml->setAttribute("hatAudio", bufferToBase64Wav(hatSample, hatSampleRate));
    dataXml->setAttribute("breakbeatAudio", bufferToBase64Wav(breakbeatBuffer, breakbeatSourceSampleRate));

    copyXmlToBinary(root, destData);
}

void PocketWorkAudioProcessor::setStateInformation(
    const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> root(
        getXmlFromBinary(data, sizeInBytes));

    if (root == nullptr)
        return;

    // Restore APVTS parameters from their own child element ONLY — this
    // is what keeps our custom data from ever being able to leak into
    // APVTS's internal ValueTree again.
    if (auto* paramsXml = root->getChildByName(apvts.state.getType().toString()))
        apvts.replaceState(juce::ValueTree::fromXml(*paramsXml));

    if (auto* dataXml = root->getChildByName("PluginData"))
    {
        kickFolderPath = dataXml->getStringAttribute("folderKick", kickFolderPath);
        snareFolderPath = dataXml->getStringAttribute("folderSnare", snareFolderPath);
        hatFolderPath = dataXml->getStringAttribute("folderHat", hatFolderPath);
        breakbeatFolderPath = dataXml->getStringAttribute("folderBreakbeat", breakbeatFolderPath);
        exportFolderPath = dataXml->getStringAttribute("folderExport", exportFolderPath);
        kickFilePath = dataXml->getStringAttribute("fileKick", kickFilePath);
        snareFilePath = dataXml->getStringAttribute("fileSnare", snareFilePath);
        hatFilePath = dataXml->getStringAttribute("fileHat", hatFilePath);
        breakbeatFilePath = dataXml->getStringAttribute("fileBreakbeat", breakbeatFilePath);
        mostRecentLoadedFilePath = dataXml->getStringAttribute("mostRecentFile", mostRecentLoadedFilePath);

        double restoredRate = 44100.0;
        if (base64WavToBuffer(dataXml->getStringAttribute("kickAudio"), kickSample, restoredRate) && getSampleRate() > 0.0)
        { resampleBufferIfNeeded(kickSample, restoredRate, getSampleRate()); kickSampleRate = getSampleRate(); }
        if (base64WavToBuffer(dataXml->getStringAttribute("snareAudio"), snareSample, restoredRate) && getSampleRate() > 0.0)
        { resampleBufferIfNeeded(snareSample, restoredRate, getSampleRate()); snareSampleRate = getSampleRate(); }
        if (base64WavToBuffer(dataXml->getStringAttribute("hatAudio"), hatSample, restoredRate) && getSampleRate() > 0.0)
        { resampleBufferIfNeeded(hatSample, restoredRate, getSampleRate()); hatSampleRate = getSampleRate(); }
        if (base64WavToBuffer(dataXml->getStringAttribute("breakbeatAudio"), breakbeatBuffer, restoredRate) && getSampleRate() > 0.0)
        { resampleBufferIfNeeded(breakbeatBuffer, restoredRate, getSampleRate()); breakbeatSourceSampleRate = getSampleRate(); }
    }
}

bool PocketWorkAudioProcessor::analyzeLoadedBreakbeat()
{
    if (breakbeatBuffer.getNumSamples() <= 0)
        return false;

    double bpm = breakbeatBpmParam->load();
    int bars = static_cast<int>(std::round(breakbeatBarsParam->load()));

    groove.analyzeAudioForGroove(breakbeatBuffer, breakbeatSourceSampleRate,
                                 bpm, bars);

    // FIX: same reasoning as loadBreakbeatFile — Detect Groove changes
    // saveable state without touching an apvts parameter, so the host
    // needs to be told explicitly or it may not persist this on save.
    updateHostDisplay();

    return true;
}

bool PocketWorkAudioProcessor::loadSampleForClass(
    GrooveEngine::DrumClass drumClass, const juce::File& file,
    double trimStartSeconds, double trimEndSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return false;

    const int numChannels = 2;
    const double sr = reader->sampleRate;
    const juce::int64 totalSamples = reader->lengthInSamples;

    juce::int64 startSample = 0;
    juce::int64 numSamples = totalSamples;

    if (trimStartSeconds >= 0.0 && trimEndSeconds > trimStartSeconds)
    {
        startSample = juce::jlimit((juce::int64) 0, totalSamples,
                                   (juce::int64) (trimStartSeconds * sr));
        juce::int64 endSample = juce::jlimit((juce::int64) 0, totalSamples,
                                            (juce::int64) (trimEndSeconds * sr));
        numSamples = juce::jmax((juce::int64) 0, endSample - startSample);
    }

    juce::AudioBuffer<float>* target = nullptr;
    juce::String* nameTarget = nullptr;
    double* sampleRateTarget = nullptr;

    switch (drumClass)
    {
        case GrooveEngine::DrumClass::Kick:  target = &kickSample;  nameTarget = &kickFileName;  sampleRateTarget = &kickSampleRate;  break;
        case GrooveEngine::DrumClass::Snare: target = &snareSample; nameTarget = &snareFileName; sampleRateTarget = &snareSampleRate; break;
        case GrooveEngine::DrumClass::Hat:   target = &hatSample;   nameTarget = &hatFileName;   sampleRateTarget = &hatSampleRate;   break;
    }

    target->setSize(numChannels, (int) numSamples);
    reader->read(target, 0, (int) numSamples, startSample, true, true);

    if (getSampleRate() > 0.0)
    {
        resampleBufferIfNeeded(*target, sr, getSampleRate());
        *sampleRateTarget = getSampleRate();
    }
    else
    {
        *sampleRateTarget = sr;
    }

    *nameTarget = file.getFileName();
    mostRecentLoadedFilePath = file.getFullPathName();

    // FIX: same reasoning as loadBreakbeatFile — loading a sample
    // changes saveable state without touching an apvts parameter, so
    // the host needs to be told explicitly or it may not persist this
    // on save.
    updateHostDisplay();

    return true;
}

void PocketWorkAudioProcessor::clearSample(GrooveEngine::DrumClass drumClass)
{
    switch (drumClass)
    {
        case GrooveEngine::DrumClass::Kick:
            kickSample.setSize(0, 0);
            kickFileName.clear();
            break;
        case GrooveEngine::DrumClass::Snare:
            snareSample.setSize(0, 0);
            snareFileName.clear();
            break;
        case GrooveEngine::DrumClass::Hat:
            hatSample.setSize(0, 0);
            hatFileName.clear();
            break;
    }

    // FIX: clearing a sample also changes saveable state, so the host
    // needs the same notification (otherwise a cleared slot may not
    // "stick" through a save either).
    updateHostDisplay();
}

juce::String PocketWorkAudioProcessor::getLoadedSampleName(
    GrooveEngine::DrumClass drumClass) const
{
    switch (drumClass)
    {
        case GrooveEngine::DrumClass::Kick:  return kickFileName;
        case GrooveEngine::DrumClass::Snare: return snareFileName;
        case GrooveEngine::DrumClass::Hat:   return hatFileName;
    }
    return {};
}

bool PocketWorkAudioProcessor::exportSample(
    GrooveEngine::DrumClass drumClass, const juce::File& destFile)
{
    const juce::AudioBuffer<float>* src = nullptr;
    double sr = 44100.0;

    switch (drumClass)
    {
        case GrooveEngine::DrumClass::Kick:  src = &kickSample;  sr = kickSampleRate;  break;
        case GrooveEngine::DrumClass::Snare: src = &snareSample; sr = snareSampleRate; break;
        case GrooveEngine::DrumClass::Hat:   src = &hatSample;   sr = hatSampleRate;   break;
    }

    if (src == nullptr || src->getNumSamples() <= 0)
        return false; // Honest: nothing loaded for this slot to export.

    destFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(destFile.createOutputStream());
    if (stream == nullptr || !stream->openedOk())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sr,
                                  (unsigned int) src->getNumChannels(), 16, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release(); // writer now owns the stream
    writer->writeFromAudioSampleBuffer(*src, 0, src->getNumSamples());
    return true;
}

const juce::AudioBuffer<float>* PocketWorkAudioProcessor::getSampleBufferForClass(
    GrooveEngine::DrumClass cls) const
{
    switch (cls)
    {
        case GrooveEngine::DrumClass::Kick:  return &kickSample;
        case GrooveEngine::DrumClass::Snare: return &snareSample;
        case GrooveEngine::DrumClass::Hat:   return &hatSample;
    }
    return nullptr;
}

void PocketWorkAudioProcessor::triggerSampleVoice(
    GrooveEngine::DrumClass cls, float velocity, int offsetInBlock)
{
    const auto* src = getSampleBufferForClass(cls);
    if (src == nullptr || src->getNumSamples() == 0)
        return; // Honest: no sample loaded for this class, so nothing plays.

    // Find a free voice, or steal the first active one as a fallback
    // rather than silently dropping the trigger.
    int voiceIndex = -1;
    for (int i = 0; i < kNumVoices; ++i)
    {
        if (!voices[static_cast<size_t>(i)].active)
        {
            voiceIndex = i;
            break;
        }
    }
    if (voiceIndex < 0)
        voiceIndex = 0; // steal

    auto& v = voices[static_cast<size_t>(voiceIndex)];
    v.drumClass = cls;
    v.position = 0;
    v.active = true;
    v.gain = juce::jlimit(0.0f, 1.0f, velocity / 127.0f);
    v.triggerOffsetThisBlock = offsetInBlock;
}

void PocketWorkAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();

    // Pull the current knob values FIRST, before anything below uses
    // them — reading them at the end of the block (the previous order)
    // meant every block used the PREVIOUS block's values, which is why
    // moving the sliders never seemed to do anything in real time.
    groove.setSensitivity(sensitivityParam->load());
    groove.setPocket(pocketParam->load());
    groove.setDynamics(dynamicsParam->load());

    const int blockSize = buffer.getNumSamples();
    const int outChannels = buffer.getNumChannels();

    // --- Breakbeat/groove playback -----------------------------------
    // KNOWN LIMITATION: this plays the loaded audio at whatever tempo it
    // naturally has — there is no time-stretching to match your
    // project's tempo. If your breakbeat's own tempo differs from your
    // project's, it will sound faster/slower than the project — that's
    // expected given the current feature set, not a bug. Real tempo-
    // syncing (time-stretching) would be a separate, substantial feature
    // if wanted later.
    if (breakbeatPlaying.load() && breakbeatBuffer.getNumSamples() > 0)
    {
        const int totalSamples = breakbeatBuffer.getNumSamples();
        int pos = breakbeatPlayPosition.load();
        const PlaybackMode mode = playbackMode.load();
        const bool includeAudio = (mode == PlaybackMode::Breakbeat || mode == PlaybackMode::Both);
        const bool includeSamples = (mode == PlaybackMode::Midi || mode == PlaybackMode::Both);

        int samplesToCopy = juce::jmin(blockSize, totalSamples - pos);

        if (includeAudio && samplesToCopy > 0)
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                int sourceCh = juce::jmin(ch, breakbeatBuffer.getNumChannels() - 1);
                buffer.copyFrom(ch, 0, breakbeatBuffer, sourceCh, pos, samplesToCopy);
            }
        }

        if (includeSamples)
        {
            // Thread-safe read: never blocks, never races with the
            // message thread rewriting this data via Detect Groove or
            // loading a new file — the actual likely fix for the
            // recurring crashes.
            GrooveEngine::PlaybackHitsSnapshot snapshot;
            groove.getPlaybackHitsSnapshot(snapshot);

            for (int i = 0; i < snapshot.count; ++i)
            {
                const auto& hit = snapshot.hits[static_cast<size_t>(i)];
                if (hit.samplePosition >= pos && hit.samplePosition < pos + blockSize)
                {
                    int offsetInBlock = hit.samplePosition - pos;
                    float scaledVelocity = juce::jlimit(1.0f, 127.0f,
                        hit.velocity * groove.getDynamics());
                    triggerSampleVoice(hit.drumClass, scaledVelocity, offsetInBlock);
                }
            }
        }

        pos += blockSize;

        if (pos >= totalSamples)
            pos = 0; // loop continuously — click the button again to stop

        breakbeatPlayPosition.store(pos);
    }

    // --- Render active sample voices ----------------------------------
    for (auto& v : voices)
    {
        if (!v.active)
            continue;

        const auto* src = getSampleBufferForClass(v.drumClass);
        if (src == nullptr || src->getNumSamples() == 0)
        {
            v.active = false;
            continue;
        }

        int startInBlock = (v.triggerOffsetThisBlock >= 0) ? v.triggerOffsetThisBlock : 0;
        int samplesAvailableInBlock = blockSize - startInBlock;
        int samplesRemainingInSample = src->getNumSamples() - v.position;
        int samplesToRender = juce::jmin(samplesAvailableInBlock, samplesRemainingInSample);

        if (samplesToRender > 0)
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                int sourceCh = juce::jmin(ch, src->getNumChannels() - 1);
                buffer.addFrom(ch, startInBlock, *src, sourceCh, v.position,
                              samplesToRender, v.gain);
            }
        }

        v.position += samplesToRender;
        v.triggerOffsetThisBlock = -1;

        if (v.position >= src->getNumSamples())
            v.active = false;
    }

    bool hostIsPlaying = false;
    double ppqAtBlockStart = 0.0;
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            hostIsPlaying = position->getIsPlaying();

            if (auto ppq = position->getPpqPosition())
                ppqAtBlockStart = *ppq;

            if (auto tempo = position->getBpm())
                bpm = *tempo;
        }
    }

    juce::MidiBuffer processed;
    groove.processMidi(midi, processed, getSampleRate(),
                        blockSize, hostIsPlaying, ppqAtBlockStart, bpm);

    // Live-played MIDI (e.g. from a keyboard) ALSO triggers your loaded
    // samples directly, humanized by the same swing/dynamics engine —
    // so POCKETWORK is playable as a real instrument, not just a
    // breakbeat-extraction tool. Only the standard GM drum note numbers
    // trigger a sample — anything else is genuinely ignored, not
    // defaulted to Snare (that earlier default was a real bug: it made
    // almost every key on a keyboard trigger the snare).
    for (const auto metadata : processed)
    {
        auto msg = metadata.getMessage();
        if (!msg.isNoteOn())
            continue;

        int note = msg.getNoteNumber();
        bool matched = true;
        GrooveEngine::DrumClass cls = GrooveEngine::DrumClass::Kick;

        if (note == 36) cls = GrooveEngine::DrumClass::Kick;
        else if (note == 38 || note == 40) cls = GrooveEngine::DrumClass::Snare;
        else if (note == 42 || note == 46) cls = GrooveEngine::DrumClass::Hat;
        else matched = false;

        if (matched)
            triggerSampleVoice(cls, (float) msg.getVelocity(),
                              metadata.samplePosition);
    }

    midi.swapWith(processed);
}

juce::AudioProcessorEditor*
PocketWorkAudioProcessor::createEditor()
{
    return new PocketWorkAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PocketWorkAudioProcessor();
}
