#include "GrooveEngine.h"

GrooveEngine::GrooveEngine()
{
    generateDemoGroove();
}

void GrooveEngine::reset()
{
    hits.clear();
}

void GrooveEngine::setGridSteps(int steps)
{
    gridSteps = juce::jlimit(4, 32, steps);
}

void GrooveEngine::setPocket(float amount)
{
    pocketAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void GrooveEngine::setDynamics(float amount)
{
    dynamicsAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void GrooveEngine::setSensitivity(float amount)
{
    sensitivity = juce::jlimit(0.0f, 1.0f, amount);
}

void GrooveEngine::generateDemoGroove()
{
    hits.clear();

    // Screenshot-defined GM drum defaults:
    // Kick 36, Snare 38, Closed Hat 42.
    const int kick[]   = {0, 4, 8, 10, 12};
    const int snare[]  = {4, 12};
    const int hats[]   = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

    for (int s : kick)
        hits.add({s, 36, 112.0f, 0.0f, false});

    for (int s : snare)
        hits.add({s, 38, 108.0f, 0.0f, false});

    for (int s : hats)
    {
        float v = (s % 2 == 0) ? 92.0f : 68.0f;
        hits.add({s, 42, v, (s % 2) ? 0.10f : -0.02f, false});
    }
}

void GrooveEngine::processMidi(const juce::MidiBuffer& input,
                               juce::MidiBuffer& output,
                               double sampleRate,
                               int blockSize)
{
    juce::ignoreUnused(sampleRate, blockSize);

    output.clear();

    for (const auto metadata : input)
    {
        auto msg = metadata.getMessage();
        output.addEvent(msg, metadata.samplePosition);
    }
}

bool GrooveEngine::exportMidi(const juce::File& file, double bpm) const
{
    juce::ignoreUnused(bpm);
    juce::MidiMessageSequence sequence;

    for (const auto& hit : hits)
    {
        if (hit.muted)
            continue;

        // One bar represented as 4 beats / 16 sixteenth-note steps.
        const double ticksPerQuarter = 480.0;
        const double ticksPerStep = ticksPerQuarter / 4.0;

        double tick = hit.step * ticksPerStep;

        // Pocket: negative = early, positive = late.
        tick += static_cast<double>(hit.pocket) *
                pocketAmount * (ticksPerStep * 0.45);

        int velocity = juce::jlimit(
            1, 127,
            static_cast<int>(std::round(
                hit.velocity * dynamicsAmount)));

        sequence.addEvent(
            juce::MidiMessage::noteOn(9, hit.note, (juce::uint8) velocity),
            tick);

        sequence.addEvent(
            juce::MidiMessage::noteOff(9, hit.note),
            tick + ticksPerStep * 0.45);
    }

    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(480);
    midi.addTrack(sequence);

    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(
        file.createOutputStream());

    if (stream == nullptr || !stream->openedOk())
        return false;

    midi.writeTo(*stream);
    return true;
}
