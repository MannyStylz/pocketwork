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

void GrooveEngine::prepare(double sampleRate)
{
    // 40ms max swing window — enough to create an audible, musical
    // pocket feel without introducing latency a producer would notice
    // or find annoying when playing live.
    latencySamples = static_cast<int>(std::round(sampleRate * 0.04));

    for (auto& p : pending)
        p.active = false;
}

void GrooveEngine::processMidi(const juce::MidiBuffer& input,
                               juce::MidiBuffer& output,
                               double sampleRate,
                               int blockSize,
                               bool hostIsPlaying,
                               double ppqAtBlockStart,
                               double bpm)
{
    output.clear();

    // 1) Release any held-back (swung) notes whose delay has elapsed.
    for (auto& p : pending)
    {
        if (!p.active)
            continue;

        if (p.samplesUntilFire < blockSize)
        {
            output.addEvent(p.message, juce::jmax(0, p.samplesUntilFire));
            p.active = false;
        }
        else
        {
            p.samplesUntilFire -= blockSize;
        }
    }

    const double samplesPerQuarter =
        (bpm > 0.0) ? (sampleRate * 60.0 / bpm) : 0.0;
    const double stepsPerQuarter = gridSteps / 4.0;

    // 2) Handle this block's incoming events.
    for (const auto metadata : input)
    {
        auto msg = metadata.getMessage();

        // Dynamics applies immediately — no timing info needed.
        if (msg.isNoteOn())
        {
            int scaledVel = juce::jlimit(1, 127,
                static_cast<int>(std::round(
                    msg.getVelocity() * dynamicsAmount)));

            msg = juce::MidiMessage::noteOn(
                msg.getChannel(), msg.getNoteNumber(),
                (juce::uint8) scaledVel);
        }

        int delaySamples = 0;

        // Swing/Pocket only applies to note-ons, and only when we
        // genuinely know where we are in the bar (host playing, valid
        // tempo). Without that, we honestly pass notes through
        // untouched rather than guessing at timing.
        if (hostIsPlaying && samplesPerQuarter > 0.0 && msg.isNoteOn())
        {
            double eventPpq = ppqAtBlockStart +
                (static_cast<double>(metadata.samplePosition) /
                 sampleRate) * (bpm / 60.0);

            double stepPosition = eventPpq * stepsPerQuarter;
            int stepIndex = static_cast<int>(std::floor(stepPosition + 0.5));
            bool isOffbeatStep = (stepIndex % 2) != 0;

            if (isOffbeatStep)
                delaySamples = static_cast<int>(
                    std::round(pocketAmount * latencySamples));
        }

        if (delaySamples <= 0)
        {
            output.addEvent(msg, metadata.samplePosition);
            continue;
        }

        bool scheduled = false;
        for (auto& p : pending)
        {
            if (!p.active)
            {
                p.active = true;
                p.message = msg;
                p.samplesUntilFire = delaySamples;
                scheduled = true;
                break;
            }
        }

        // Extremely unlikely at normal note densities, but if the
        // pending buffer is ever full, fire immediately rather than
        // silently dropping a note.
        if (!scheduled)
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

        // Guard against a negative timestamp (possible on step 0 with
        // an "early" pocket offset) — a negative time can produce a
        // corrupt/unreadable MIDI file.
        tick = juce::jmax(0.0, tick);

        int velocity = juce::jlimit(
            1, 127,
            static_cast<int>(std::round(
                hit.velocity * dynamicsAmount)));

        sequence.addEvent(
            juce::MidiMessage::noteOn(10, hit.note, (juce::uint8) velocity),
            tick);

        sequence.addEvent(
            juce::MidiMessage::noteOff(10, hit.note),
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
