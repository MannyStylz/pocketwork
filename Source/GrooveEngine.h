#pragma once
#include <JuceHeader.h>

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
    void processMidi(const juce::MidiBuffer& input, juce::MidiBuffer& output,
                     double sampleRate, int blockSize);

    bool exportMidi(const juce::File& file, double bpm = 90.0) const;

    const juce::Array<Hit>& getHits() const { return hits; }

private:
    int gridSteps = 16;
    float pocketAmount = 0.50f;
    float dynamicsAmount = 1.0f;
    float sensitivity = 0.50f;

    juce::Array<Hit> hits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveEngine)
};
