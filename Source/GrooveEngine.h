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

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveEngine)
};
