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

    // Called once when the host sets up audio (sample rate known) —
    // sizes the internal swing-delay window and clears any pending
    // held notes. Not real-time audio-thread work, safe to do here.
    void prepare(double sampleRate);

    // --- Real audio groove extraction (Step 2) -----------------------
    // Analyzes a loaded audio buffer for onsets (transients) and
    // replaces the current hits with what it finds — real timing and
    // velocity from the actual audio, not faked. Runs on the message
    // thread (triggered by a button), never on the audio thread.
    //
    // HONEST LIMITATIONS, stated plainly rather than hidden:
    // - Detects WHEN a hit happens and HOW LOUD it is, but does NOT
    //   yet classify what kind of drum it is (kick vs snare vs hat) —
    //   every detected hit uses the same placeholder note for now.
    // - Assumes the loaded file represents exactly one bar at the
    //   current grid resolution. A file of a different length will
    //   produce a distorted-sounding timing map. Tempo-aware analysis
    //   is a future refinement, not implemented here.
    void analyzeAudioForGroove(const juce::AudioBuffer<float>& audio,
                               double sourceSampleRate);

    int getExtractedHitCount() const { return hits.size(); }

    // Live MIDI passthrough with real-time swing/pocket applied to
    // note-ons that land on an off-beat grid step, plus immediate
    // Dynamics velocity scaling on every note-on. Requires host
    // playback position (ppq/tempo) to know where the grid actually
    // is — without it (host not playing / no playhead), notes pass
    // through with Dynamics applied but no swing, which is the
    // honest behavior rather than guessing at timing.
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

    // --- Real-time swing engine -----------------------------------
    // A fixed-capacity, pre-allocated holding buffer for notes that
    // are being delayed slightly to create the swing feel. No heap
    // allocation happens here during processBlock — every slot
    // already exists, we just flip "active" on and off.
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
