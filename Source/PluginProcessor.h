#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

class TracktionSession;
class ExternalMidiInput;
class ExternalMidiOutput;

class EducationalDawEngine : public juce::AudioProcessor, private juce::AudioProcessorListener
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
    bool startRecording (const std::vector<NoteEvent>& accompaniment = {});
    bool startPlayback (double durationSeconds = 0.0);
    void setLooping (bool enabled);
    int tracktionNoteCount() const;
    void serviceTimeline();
    void selectMidiInput (const juce::String&);
    juce::String midiInputIdentifier() const;
    juce::String midiInputStatus() const;
    bool selectMidiOutput (const juce::String& identifier);
    juce::String midiOutputIdentifier() const;
    void receiveExternalMidi (const juce::MidiMessage&);
    void panicExternalMidi() { externalPanic.store (true); }
    // 0: ignore, 1: tutorial keyboard, 2: selected Sandbox instrument.
    void setExternalMidiMode (int mode, int drumChannel = 0);
    struct ExternalNote { int note = 0; double seconds = 0; };
    bool popExternalNote (ExternalNote&);
    void setKeyboardSound (int channel, int sound);
    void setChannelLevel (int channel, float level)
    {
        if (channel >= 1 && channel <= 16) channelLevels[channel - 1].store (juce::jlimit (0.0f, 1.0f, level));
    }
    void stopTransport();
    SessionStatus getSessionStatus();
    std::vector<NoteEvent> copySession();
    bool loadSession (const std::vector<NoteEvent>&);
    // Plug-in lifecycle and state calls run on the message thread with transport stopped.
    juce::String installInstrument (int track, std::unique_ptr<juce::AudioPluginInstance>);
    void removeInstrument (int track);
    void clearInstruments();
    juce::AudioPluginInstance* instrument (int track) const;
    juce::String instrumentState (int track);
    void setInstrumentRouting (bool enabled, int selected, int bpm)
    {
        const juce::ScopedLock lock (getCallbackLock());
        if (liveTrack.load() != selected || instrumentsEnabled.load() != enabled)
        {
            // Release held notes before the live keyboard starts addressing another instrument.
            for (auto& slot : instruments) slot.panic = true;
            synthesiser.allNotesOff (16, false);
        }
        instrumentsEnabled.store (enabled);
        liveTrack.store (selected);
        hostTempo.store (bpm);
    }
    static bool decodeInstrumentState (const juce::String& text, juce::MemoryBlock& bytes)
    {
        if (text.length() > 16 * 1024 * 1024) return false;
        bytes.reset();
        juce::MemoryOutputStream output (bytes, false);
        return juce::Base64::convertFromBase64 (output, text);
    }
    bool takeInstrumentChanges() { return instrumentChanged.exchange (false); }
    static constexpr int maxEvents = 32768;

    juce::MidiKeyboardState keyboardState;
    std::atomic<LessonState> lessonState { LessonState::inactive };
    juce::String audioStatus;

private:
    std::unique_ptr<TracktionSession> timeline;
    std::unique_ptr<ExternalMidiInput> midiInput;
    std::unique_ptr<ExternalMidiOutput> midiOutput;
    juce::MidiBuffer onscreenMidi;
    juce::MidiMessageCollector externalCollector;
    juce::MidiBuffer externalBuffer;
    std::atomic<bool> externalPanic { false }, midiReady { false };
    std::atomic<int> externalMode { 0 }, externalDrumChannel { 0 };
    juce::AbstractFifo externalNotesFifo { 256 };
    std::array<ExternalNote, 256> externalNotes;
    void collectExternalMidi (juce::MidiBuffer&, int samples);
    juce::MidiBuffer timelineMidi;
    std::atomic<bool> timelineStopPending { false };
    std::atomic<bool> instrumentChanged { false };
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { instrumentChanged.store (true); }
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override
    { instrumentChanged.store (true); }
    void stopTransportLocked();
    void processSession (juce::MidiBuffer&, int numSamples);
    void renderInstruments (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    struct HostPlayHead : juce::AudioPlayHead
    {
        PositionInfo position;
        juce::Optional<PositionInfo> getPosition() const override { return position; }
    } hostPlayHead;
    struct InstrumentSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::AudioBuffer<float> audio;
        juce::MidiBuffer midi;
        juce::SmoothedValue<float> gain;
        bool panic = false;
    };
    std::array<InstrumentSlot, 3> instruments;
    juce::MidiBuffer builtInMidi;
    std::atomic<bool> instrumentsEnabled { false };
    std::atomic<int> liveTrack { -1 }, hostTempo { 120 };
    int preparedBlockSize = 512;
    int64_t blockStart = 0;
    std::array<std::atomic<float>, 16> channelLevels;
    std::array<std::atomic<int>, 16> keyboardSounds;
    std::atomic<bool> looping { false };
    int64_t playbackLength = 1;
    juce::Synthesiser synthesiser;
    // Reserved once; note capture does not grow storage on the audio thread.
    std::vector<NoteEvent> session;
    std::vector<NoteEvent> backing;
    size_t backingIndex = 0;
    std::array<std::array<bool, 128>, 16> held {};
    Transport transport = Transport::stopped;
    double sessionRate = 44100.0;
    int64_t transportSamples = 0;
    size_t playbackIndex = 0;
    bool recordingFull = false;
    bool audioReady = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EducationalDawEngine)
};
