#include "PluginProcessor.h"
#include "PluginEditor.h"

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

bool PocketWorkAudioProcessor::loadBreakbeatFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return false;

    breakbeatPlaying.store(false);

    const int numChannels = 2;
    const int numSamples = static_cast<int>(reader->lengthInSamples);

    breakbeatBuffer.setSize(numChannels, numSamples);
    reader->read(&breakbeatBuffer, 0, numSamples, 0, true, true);

    breakbeatSourceSampleRate = reader->sampleRate;
    loadedFileName = file.getFileName();
    breakbeatPlayPosition.store(0);

    return true;
}

void PocketWorkAudioProcessor::setBreakbeatPlaying(bool shouldPlay)
{
    if (shouldPlay)
        breakbeatPlayPosition.store(0);

    breakbeatPlaying.store(shouldPlay);
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
    GrooveEngine::DrumClass drumClass, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return false;

    const int numChannels = 2;
    const int numSamples = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float>* target = nullptr;
    juce::String* nameTarget = nullptr;

    switch (drumClass)
    {
        case GrooveEngine::DrumClass::Kick:  target = &kickSample;  nameTarget = &kickFileName;  break;
        case GrooveEngine::DrumClass::Snare: target = &snareSample; nameTarget = &snareFileName; break;
        case GrooveEngine::DrumClass::Hat:   target = &hatSample;   nameTarget = &hatFileName;   break;
    }

    target->setSize(numChannels, numSamples);
    reader->read(target, 0, numSamples, 0, true, true);
    *nameTarget = file.getFileName();

    return true;
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

    const int blockSize = buffer.getNumSamples();
    const int outChannels = buffer.getNumChannels();

    // --- Breakbeat playback -----------------------------------------
    // KNOWN LIMITATION: no sample-rate conversion yet — if the loaded
    // file's sample rate doesn't match the project's, playback speed/
    // pitch will be slightly off.
    //
    // While the breakbeat plays, each detected hit (from
    // getPlaybackHits()) ALSO triggers the corresponding loaded
    // kick/snare/hat sample directly — real internal sound, no MIDI-
    // out or external instrument/Patcher routing required. This is
    // the actual fix for the FL Studio routing friction: everything
    // happens inside one self-contained plugin.
    if (breakbeatPlaying.load() && breakbeatBuffer.getNumSamples() > 0)
    {
        const int totalSamples = breakbeatBuffer.getNumSamples();
        int pos = breakbeatPlayPosition.load();

        int samplesToCopy = juce::jmin(blockSize, totalSamples - pos);

        if (samplesToCopy > 0)
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                int sourceCh = juce::jmin(ch, breakbeatBuffer.getNumChannels() - 1);
                buffer.copyFrom(ch, 0, breakbeatBuffer, sourceCh, pos, samplesToCopy);
            }
        }

        const auto& playbackHits = groove.getPlaybackHits();

        for (const auto& hit : playbackHits)
        {
            if (hit.samplePosition >= pos && hit.samplePosition < pos + blockSize)
            {
                int offsetInBlock = hit.samplePosition - pos;
                triggerSampleVoice(hit.drumClass, hit.velocity, offsetInBlock);
            }
        }

        pos += blockSize;

        if (pos >= totalSamples)
        {
            pos = 0;
            breakbeatPlaying.store(false); // play once, not looped, for now
        }

        breakbeatPlayPosition.store(pos);
    }

    // --- Render active sample voices into the output -----------------
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

void PocketWorkAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PocketWorkAudioProcessor::setStateInformation(
    const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(
        getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
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
