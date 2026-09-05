#pragma once
#include <JuceHeader.h>
#include <array>

class GrooveEngine
{
public:
    struct Hit
    {
        int step = 0;
        int note = 36;       // GM kick by default
        float velocity = 100.0f;
        float pocket = 0.0f; // -1 early, +1 late
        bool muted = false;
    };

    GrooveEngine();

    void reset();
    void setGridSteps(int steps);
    void setPocket(float amount);
    void setDynamics(float amount);
    void setSensitivity(float amount);

    void generateDemoGroove();

    void prepare(double sampleRate);

    void processMidi(const juce::MidiBuffer& input, juce::MidiBuffer& output,
                     double sampleRate, int blockSize,
                     bool hostIsPlaying, double ppqAtBlockStart, double bpm);

    int getLatencySamples() const { return latencySamples; }

    // TEMPORARY DIAGNOSTIC — the delay (in milliseconds) applied to
    // the most recent note-on, so the GUI can show it's really
    // happening. -1 means "no note-on processed yet."
    int getLastNoteOnDelayMs() const { return lastNoteOnDelayMs.load(); }

    bool exportMidi(const juce::File& file, double bpm = 90.0) const;

    const juce::Array<Hit>& getHits() const { return hits; }

private:
    int gridSteps = 16;
    float pocketAmount = 0.50f;
    float dynamicsAmount = 1.0f;
    float sensitivity = 0.50f;

    juce::Array<Hit> hits;

    struct PendingEvent
    {
        juce::MidiMessage message { juce::MidiMessage::noteOff(1, 0) };
        int samplesUntilFire = 0;
        bool active = false;
    };

    static constexpr int kMaxPendingEvents = 256;
    std::array<PendingEvent, kMaxPendingEvents> pending;

    // Remembers the swing delay applied to each active note's note-on,
    // keyed by (channel-1)*128 + noteNumber, so the matching note-off
    // gets the SAME delay — otherwise a note could end before its
    // delayed start ever plays, causing dropped or stuck notes.
    std::array<int, 16 * 128> activeNoteDelay {};

    std::atomic<int> lastNoteOnDelayMs { -1 };

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveEngine)
};
