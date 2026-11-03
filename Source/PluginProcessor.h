#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

class EducationalDawEngine : public juce::AudioProcessor
{
public:
    enum class LessonState { inactive, waiting, tryAgain, complete };

    EducationalDawEngine();
    ~EducationalDawEngine() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "EducationalDawEngine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.15; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    struct NoteEvent
    {
        double seconds = 0.0;
        int channel = 1, note = 60, velocity = 0;
        bool on = false;
    };
    enum class Transport { stopped, recording, playing };
    struct SessionStatus
    {
        Transport transport;
        int eventCount;
        double seconds;
        bool full;
    };
    void newSession();
    bool startRecording();
    bool startPlayback();
    void stopTransport();
    SessionStatus getSessionStatus();
    std::vector<NoteEvent> copySession();
    bool loadSession (const std::vector<NoteEvent>&);
    static constexpr int maxEvents = 32768;

    juce::MidiKeyboardState keyboardState;
    std::atomic<LessonState> lessonState { LessonState::inactive };
    juce::String audioStatus;

private:
    void stopTransportLocked();
    void processSession (juce::MidiBuffer&, int numSamples);
    juce::Synthesiser synthesiser;
    // Reserved once; note capture does not grow storage on the audio thread.
    std::vector<NoteEvent> session;
    std::array<std::array<bool, 128>, 16> held {};
    Transport transport = Transport::stopped;
    double sessionRate = 44100.0;
    int64_t transportSamples = 0;
    size_t playbackIndex = 0;
    bool recordingFull = false;
    bool audioReady = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EducationalDawEngine)
};
