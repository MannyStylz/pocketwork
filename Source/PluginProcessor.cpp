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
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sensitivityParam = apvts.getRawParameterValue(ParamIDs::sensitivity);
    pocketParam      = apvts.getRawParameterValue(ParamIDs::pocket);
    dynamicsParam    = apvts.getRawParameterValue(ParamIDs::dynamics);
}

void PocketWorkAudioProcessor::prepareToPlay(double sampleRate,
                                               int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void PocketWorkAudioProcessor::releaseResources()
{
}

bool PocketWorkAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    juce::ignoreUnused(layouts);
    return true;
}

void PocketWorkAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();

    // Pull the current knob values every block. This is what was
    // missing in v0.1 — the sliders now actually reach the engine.
    groove.setSensitivity(sensitivityParam->load());
    groove.setPocket(pocketParam->load());
    groove.setDynamics(dynamicsParam->load());

    // HONEST STATUS: live MIDI passthrough only, for now. Pocket and
    // Dynamics are fully applied when you export a .mid file (see
    // GrooveEngine::exportMidi), but real-time humanizing of incoming
    // MIDI as it plays requires tracking host tempo/position (PPQ) to
    // know where each note falls on the grid — that's a real feature,
    // not implemented yet, and it will be built in a later milestone
    // rather than faked here.
    juce::MidiBuffer processed;
    groove.processMidi(midi, processed, getSampleRate(),
                        buffer.getNumSamples());
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
