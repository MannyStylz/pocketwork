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
    pendingPlaybackTriggerNoteOns.reserve(32);

    // Tell the host we introduce a small, fixed delay so it can
    // compensate — this is what makes the swing/pocket technique
    // honest rather than something that silently throws playback
    // out of sync with other tracks.
    setLatencySamples(groove.getLatencySamples());
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

    const int numChannels = 2; // always store as stereo internally
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

void PocketWorkAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();
    pendingPlaybackTriggerNoteOns.clear();

    // --- Breakbeat playback + synced groove-trigger MIDI -----------
    // KNOWN LIMITATION: no sample-rate conversion yet — if the loaded
    // file's sample rate doesn't match the project's, playback speed/
    // pitch will be slightly off. Real-time-safe (just reading a
    // preloaded buffer, no disk access here), but not yet a fully
    // polished implementation. Noted honestly rather than hidden.
    //
    // While the breakbeat plays, we ALSO emit a short MIDI note at
    // each detected hit's REAL position in the audio (from
    // getPlaybackHits()) — this is what lets the extracted groove
    // trigger your own instrument in sync with the original break,
    // per the "play both together" goal. Every hit currently uses the
    // same placeholder note (38) until per-drum-type classification
    // exists.
    if (breakbeatPlaying.load() && breakbeatBuffer.getNumSamples() > 0)
    {
        const int totalSamples = breakbeatBuffer.getNumSamples();
        int pos = breakbeatPlayPosition.load();
        const int blockSize = buffer.getNumSamples();
        const int outChannels = buffer.getNumChannels();

        int samplesToCopy = juce::jmin(blockSize, totalSamples - pos);

        if (samplesToCopy > 0)
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                int sourceCh = juce::jmin(ch, breakbeatBuffer.getNumChannels() - 1);
                buffer.copyFrom(ch, 0, breakbeatBuffer, sourceCh, pos, samplesToCopy);
            }
        }

        // Collect this block's playback-trigger events for later —
        // added directly to the FINAL output after the swing engine
        // runs, so these precisely-scheduled notes never get shifted
        // by the live swing/pocket logic below.
        const auto& playbackHits = groove.getPlaybackHits();

        for (const auto& hit : playbackHits)
        {
            if (hit.samplePosition >= pos && hit.samplePosition < pos + blockSize)
            {
                int offsetInBlock = hit.samplePosition - pos;
                pendingPlaybackTriggerNoteOns.push_back(
                    { offsetInBlock, hit.note,
                      juce::jlimit(1, 127, (int) hit.velocity) });
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

    // Pull the current knob values every block. This is what was
    // missing in v0.1 — the sliders now actually reach the engine.
    groove.setSensitivity(sensitivityParam->load());
    groove.setPocket(pocketParam->load());
    groove.setDynamics(dynamicsParam->load());

    // Read the host's playback position and tempo, if it's available.
    // Without this, we can't know where the musical grid actually is
    // — in that case the engine honestly skips swing rather than
    // guessing, while Dynamics scaling still applies regardless.
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
                        buffer.getNumSamples(),
                        hostIsPlaying, ppqAtBlockStart, bpm);

    // Add breakbeat-playback trigger notes directly to the final
    // output, untouched by the swing engine above — these already
    // have their exact, correct sample position from the real audio.
    const int blockSizeForClamp = buffer.getNumSamples();
    for (const auto& t : pendingPlaybackTriggerNoteOns)
    {
        processed.addEvent(
            juce::MidiMessage::noteOn(10, t.note, (juce::uint8) t.velocity),
            t.sampleOffset);

        int noteOffOffset = juce::jmin(t.sampleOffset + 200, blockSizeForClamp - 1);
        processed.addEvent(
            juce::MidiMessage::noteOff(10, t.note),
            juce::jmax(t.sampleOffset, noteOffOffset));
    }

    midi.swapWith(processed);
}

void PocketWorkAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData)
{
    // Saves ALL current parameter values via the standard JUCE
    // mechanism, replacing v0.1's fragile hand-rolled float writes.
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

// Every JUCE plugin needs this function — it's how the VST3 wrapper
// knows how to create an instance of your processor. This was missing
// entirely from the v0.1 files, which is why the linker couldn't find it.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PocketWorkAudioProcessor();
}
