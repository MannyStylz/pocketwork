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

    return { params.begin(), params.end() };
}

PocketWorkAudioProcessor::PocketWorkAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sensitivityParam = apvts.getRawParameterValue(ParamIDs::sensitivity);
    pocketParam      = apvts.getRawParameterValue(ParamIDs::pocket);
    dynamicsParam    = apvts.getRawParameterValue(ParamIDs::dynamics);

    formatManager.registerBasicFormats();
}

void PocketWorkAudioProcessor::prepareToPlay(double sampleRate,
                                               int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    groove.prepare(sampleRate);
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

void PocketWorkAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();

    // KNOWN LIMITATION: no sample-rate conversion yet — if the loaded
    // file's sample rate doesn't match the project's, playback speed/
    // pitch will be slightly off. Noted honestly rather than hidden.
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

        pos += blockSize;

        if (pos >= totalSamples)
        {
            pos = 0;
            breakbeatPlaying.store(false);
        }

        breakbeatPlayPosition.store(pos);
    }

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
                        buffer.getNumSamples(),
                        hostIsPlaying, ppqAtBlockStart, bpm);
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
