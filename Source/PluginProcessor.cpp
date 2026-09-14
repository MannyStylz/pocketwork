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

        juce::MemoryBlock mb;
        {
            juce::MemoryOutputStream mos(mb, false);
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(&mos, sampleRate,
                                          (unsigned int) buffer.getNumChannels(),
                                          16, {}, 0));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            // writer's destructor (end of scope) flushes the WAV data.
        }
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

        juce::MemoryInputStream mis(decoded.getData(), decoded.getDataSize(), false);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader(
            wavFormat.createReaderFor(&mis, false));

        if (reader == nullptr)
            return false;

        outBuffer.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&outBuffer, 0, (int) reader->lengthInSamples, 0, true, true);
        outSampleRate = reader->sampleRate;
        return true;
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
          .withOutput("Main", juce::AudioChannelSet::stereo(), true)
          .withOutput("Kick", juce::AudioChannelSet::stereo(), false)
          .withOutput("Snare", juce::AudioChannelSet::stereo(), false)
          .withOutput("Hat", juce::AudioChannelSet::stereo(), false)),
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
    // Main output must be stereo; the Kick/Snare/Hat outputs can be
    // stereo (if the host/user has enabled and routed them) or disabled
    // entirely — both are fine.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int busIndex = 1; busIndex < layouts.outputBuses.size(); ++busIndex)
    {
        auto set = layouts.outputBuses.getReference(busIndex);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
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

    breakbeatSourceSampleRate = sr;
    loadedFileName = file.getFileName();
    breakbeatPlayPosition.store(0);
    mostRecentLoadedFilePath = file.getFullPathName();

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
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->setAttribute("folderKick", kickFolderPath);
    xml->setAttribute("folderSnare", snareFolderPath);
    xml->setAttribute("folderHat", hatFolderPath);
    xml->setAttribute("folderBreakbeat", breakbeatFolderPath);
    xml->setAttribute("folderExport", exportFolderPath);
    xml->setAttribute("fileKick", kickFilePath);
    xml->setAttribute("fileSnare", snareFilePath);
    xml->setAttribute("fileHat", hatFilePath);
    xml->setAttribute("fileBreakbeat", breakbeatFilePath);
    xml->setAttribute("mostRecentFile", mostRecentLoadedFilePath);

    // CRITICAL: embed the actual trimmed audio data, not just a path —
    // this is what makes loaded samples survive a state refresh even if
    // the original file has moved, been deleted, or the host reloads
    // plugin state without a full project save.
    xml->setAttribute("kickAudio", bufferToBase64Wav(kickSample, kickSampleRate));
    xml->setAttribute("snareAudio", bufferToBase64Wav(snareSample, snareSampleRate));
    xml->setAttribute("hatAudio", bufferToBase64Wav(hatSample, hatSampleRate));
    xml->setAttribute("breakbeatAudio", bufferToBase64Wav(breakbeatBuffer, breakbeatSourceSampleRate));

    copyXmlToBinary(*xml, destData);
}

void PocketWorkAudioProcessor::setStateInformation(
    const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(
        getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        kickFolderPath = xmlState->getStringAttribute("folderKick", kickFolderPath);
        snareFolderPath = xmlState->getStringAttribute("folderSnare", snareFolderPath);
        hatFolderPath = xmlState->getStringAttribute("folderHat", hatFolderPath);
        breakbeatFolderPath = xmlState->getStringAttribute("folderBreakbeat", breakbeatFolderPath);
        exportFolderPath = xmlState->getStringAttribute("folderExport", exportFolderPath);
        kickFilePath = xmlState->getStringAttribute("fileKick", kickFilePath);
        snareFilePath = xmlState->getStringAttribute("fileSnare", snareFilePath);
        hatFilePath = xmlState->getStringAttribute("fileHat", hatFilePath);
        breakbeatFilePath = xmlState->getStringAttribute("fileBreakbeat", breakbeatFilePath);
        mostRecentLoadedFilePath = xmlState->getStringAttribute("mostRecentFile", mostRecentLoadedFilePath);

        base64WavToBuffer(xmlState->getStringAttribute("kickAudio"), kickSample, kickSampleRate);
        base64WavToBuffer(xmlState->getStringAttribute("snareAudio"), snareSample, snareSampleRate);
        base64WavToBuffer(xmlState->getStringAttribute("hatAudio"), hatSample, hatSampleRate);
        base64WavToBuffer(xmlState->getStringAttribute("breakbeatAudio"), breakbeatBuffer, breakbeatSourceSampleRate);

        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
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
    *nameTarget = file.getFileName();
    *sampleRateTarget = sr;
    mostRecentLoadedFilePath = file.getFullPathName();

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

    // Separate output buses — Main always carries everything (so nothing
    // changes if you never touch FL Studio's output routing), while
    // Kick/Snare/Hat carry an ADDITIONAL independent copy of just that
    // sound, for anyone who enables and routes them to their own mixer
    // channel. Any bus not enabled/routed by the host simply has 0
    // channels here, so writes to it are skipped automatically.
    auto mainBus  = getBusBuffer(buffer, false, 0);
    auto kickBus  = getBusBuffer(buffer, false, 1);
    auto snareBus = getBusBuffer(buffer, false, 2);
    auto hatBus   = getBusBuffer(buffer, false, 3);

    auto getExtraBusForClass = [&](GrooveEngine::DrumClass cls) -> juce::AudioBuffer<float>*
    {
        switch (cls)
        {
            case GrooveEngine::DrumClass::Kick:  return kickBus.getNumChannels()  > 0 ? &kickBus  : nullptr;
            case GrooveEngine::DrumClass::Snare: return snareBus.getNumChannels() > 0 ? &snareBus : nullptr;
            case GrooveEngine::DrumClass::Hat:   return hatBus.getNumChannels()   > 0 ? &hatBus   : nullptr;
        }
        return nullptr;
    };

    const int blockSize = buffer.getNumSamples();
    const int outChannels = mainBus.getNumChannels();

    // --- Breakbeat/groove playback -----------------------------------
    // KNOWN LIMITATION: no sample-rate conversion yet — if the loaded
    // file's sample rate doesn't match the project's, playback speed/
    // pitch will be slightly off.
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
                mainBus.copyFrom(ch, 0, breakbeatBuffer, sourceCh, pos, samplesToCopy);
            }
        }

        if (includeSamples)
        {
            const auto& playbackHits = groove.getPlaybackHits();

            for (const auto& hit : playbackHits)
            {
                if (hit.samplePosition >= pos && hit.samplePosition < pos + blockSize)
                {
                    int offsetInBlock = hit.samplePosition - pos;
                    triggerSampleVoice(hit.drumClass, hit.velocity, offsetInBlock);
                }
            }
        }

        pos += blockSize;

        if (pos >= totalSamples)
            pos = 0; // loop continuously — click the button again to stop

        breakbeatPlayPosition.store(pos);
    }

    // --- Render active sample voices ----------------------------------
    // Each voice writes into Main (always) AND its own dedicated bus
    // (only if that bus is enabled/routed by the host).
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
                mainBus.addFrom(ch, startInBlock, *src, sourceCh, v.position,
                                samplesToRender, v.gain);
            }

            if (auto* extraBus = getExtraBusForClass(v.drumClass))
            {
                for (int ch = 0; ch < extraBus->getNumChannels(); ++ch)
                {
                    int sourceCh = juce::jmin(ch, src->getNumChannels() - 1);
                    extraBus->addFrom(ch, startInBlock, *src, sourceCh, v.position,
                                     samplesToRender, v.gain);
                }
            }
        }

        v.position += samplesToRender;
        v.triggerOffsetThisBlock = -1;

        if (v.position >= src->getNumSamples())
            v.active = false;
    }

    // Pull the current knob values every block.
    groove.setSensitivity(sensitivityParam->load());
    groove.setPocket(pocketParam->load());
    groove.setDynamics(dynamicsParam->load());

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
    // breakbeat-extraction tool. Rough note-to-class mapping: GM
    // kick/snare/hat numbers map directly; anything else defaults to
    // Snare rather than being silently ignored.
    for (const auto metadata : processed)
    {
        auto msg = metadata.getMessage();
        if (!msg.isNoteOn())
            continue;

        GrooveEngine::DrumClass cls = GrooveEngine::DrumClass::Snare;
        int note = msg.getNoteNumber();
        if (note == 36) cls = GrooveEngine::DrumClass::Kick;
        else if (note == 42 || note == 46) cls = GrooveEngine::DrumClass::Hat;
        else if (note == 38 || note == 40) cls = GrooveEngine::DrumClass::Snare;

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
