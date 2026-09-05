#include "GrooveEngine.h"
#include <vector>
#include <cmath>

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

    activeNoteDelay.fill(0);
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
        const int noteKey = (juce::jlimit(1, 16, msg.getChannel()) - 1) * 128
                             + juce::jlimit(0, 127, msg.getNoteNumber());

        // Swing/Pocket only applies to note-ons, and only when we
        // genuinely know where we are in the bar (host playing, valid
        // tempo). Without that, we honestly pass notes through
        // untouched rather than guessing at timing.
        if (msg.isNoteOn())
        {
            if (hostIsPlaying && samplesPerQuarter > 0.0)
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

            // Remember this note's delay so its note-off (below) gets
            // the SAME delay — otherwise a short note could end before
            // its delayed start ever plays.
            activeNoteDelay[noteKey] = delaySamples;

            lastNoteOnDelayMs.store(
                static_cast<int>(std::round(
                    1000.0 * delaySamples /
                    juce::jmax(1.0, sampleRate))));
        }
        else if (msg.isNoteOff())
        {
            delaySamples = activeNoteDelay[noteKey];
            activeNoteDelay[noteKey] = 0;
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

void GrooveEngine::analyzeAudioForGroove(const juce::AudioBuffer<float>& audio,
                                         double sourceSampleRate)
{
    hits.clear();

    const int numSamples = audio.getNumSamples();
    if (numSamples <= 0 || sourceSampleRate <= 0.0)
        return;

    // Sum to mono for analysis — we only need timing/loudness, not
    // stereo image, to detect onsets.
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    const int numChannels = audio.getNumChannels();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = audio.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] += data[i] / static_cast<float>(numChannels);
    }

    // Energy envelope: RMS per window, hopped forward each step.
    const int windowSize = 1024;
    const int hopSize = 512;
    const int numWindows = juce::jmax(1, (numSamples - windowSize) / hopSize);

    std::vector<float> energy(static_cast<size_t>(numWindows), 0.0f);

    for (int w = 0; w < numWindows; ++w)
    {
        int start = w * hopSize;
        double sumSq = 0.0;

        for (int i = 0; i < windowSize && (start + i) < numSamples; ++i)
        {
            float s = mono[static_cast<size_t>(start + i)];
            sumSq += s * s;
        }

        energy[static_cast<size_t>(w)] =
            static_cast<float>(std::sqrt(sumSq / windowSize));
    }

    // Onset detection function: positive energy increase frame-to-frame
    // ("spectral flux" simplified to broadband energy flux). A real
    // onset shows up as a sharp rise, not a gradual one.
    std::vector<float> flux(static_cast<size_t>(numWindows), 0.0f);

    for (int w = 1; w < numWindows; ++w)
    {
        float diff = energy[static_cast<size_t>(w)] -
                     energy[static_cast<size_t>(w - 1)];
        flux[static_cast<size_t>(w)] = juce::jmax(0.0f, diff);
    }

    // Adaptive threshold from the flux statistics — Sensitivity moves
    // this between "only the strongest hits" and "catch more hits,
    // including softer ones."
    double sum = 0.0, sumSq = 0.0;
    for (float f : flux) { sum += f; sumSq += f * f; }

    double mean = sum / juce::jmax(1, numWindows);
    double variance = (sumSq / juce::jmax(1, numWindows)) - (mean * mean);
    double stdDev = std::sqrt(juce::jmax(0.0, variance));

    // sensitivity: 0 = least sensitive (fewer, only-strongest hits),
    // 1 = most sensitive (more hits, including softer ones).
    double threshold = mean + (1.0 - sensitivity) * 2.0 * stdDev;

    // Peak-pick: local maxima above threshold, with a minimum spacing
    // so one transient doesn't get counted twice.
    const int minSpacingWindows =
        juce::jmax(1, static_cast<int>((0.05 * sourceSampleRate) / hopSize));

    struct RawHit { int windowIndex; float strength; };
    std::vector<RawHit> rawHits;

    int lastHitWindow = -minSpacingWindows;

    for (int w = 1; w < numWindows - 1; ++w)
    {
        float f = flux[static_cast<size_t>(w)];

        bool isLocalPeak = f > threshold &&
                           f >= flux[static_cast<size_t>(w - 1)] &&
                           f >= flux[static_cast<size_t>(w + 1)];

        if (isLocalPeak && (w - lastHitWindow) >= minSpacingWindows)
        {
            rawHits.push_back({ w, f });
            lastHitWindow = w;
        }
    }

    if (rawHits.empty())
        return; // Honest outcome: nothing detected, not a fake result.

    float maxStrength = 0.0f;
    for (const auto& rh : rawHits)
        maxStrength = juce::jmax(maxStrength, rh.strength);

    const double totalDurationSeconds =
        static_cast<double>(numSamples) / sourceSampleRate;
    const double stepsPerBar = static_cast<double>(gridSteps);

    for (const auto& rh : rawHits)
    {
        double timeSeconds =
            (static_cast<double>(rh.windowIndex) * hopSize) / sourceSampleRate;

        // KNOWN LIMITATION: assumes the file is exactly one bar long.
        double stepPosition = (timeSeconds / totalDurationSeconds) * stepsPerBar;

        int stepIndex = static_cast<int>(std::floor(stepPosition + 0.5));
        double pocketOffset = stepPosition - stepIndex;

        stepIndex = juce::jlimit(0, gridSteps - 1, stepIndex);
        pocketOffset = juce::jlimit(-1.0, 1.0, pocketOffset);

        float normalizedStrength = maxStrength > 0.0f
            ? (rh.strength / maxStrength) : 0.0f;
        float velocity = juce::jlimit(1.0f, 127.0f, 60.0f + normalizedStrength * 67.0f);

        // Placeholder note — real per-drum-type classification
        // (kick/snare/hat) is a future refinement, not implemented
        // here. Every detected hit currently uses the same note.
        Hit hit;
        hit.step = stepIndex;
        hit.note = 38;
        hit.velocity = velocity;
        hit.pocket = static_cast<float>(pocketOffset);
        hit.muted = false;

        hits.add(hit);
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
