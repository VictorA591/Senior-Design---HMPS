#pragma once
#include "Source/PluginProcessor.h"
#include <tracktion_engine/tracktion_engine.h>

// Tracktion owns the Edit, MIDI clips, tempo and playback graph. JUCE still owns
// the physical audio device and renders our built-in/VST3 instruments downstream.
class TracktionSession : private juce::AsyncUpdater
{
    struct Behaviour : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override { return false; }
        bool addSystemAudioIODeviceTypes() override { return false; }
    };
public:
    TracktionSession()
        : engine ("HMPS", nullptr, std::make_unique<Behaviour>()),
          host (engine.getDeviceManager().getHostedAudioDeviceInterface())
    {
        tracktion::HostedAudioDeviceInterface::Parameters parameters;
        parameters.inputChannels = 0;
        host.initialise (parameters);
        host.prepareToPlay (44100, 512);
        engine.getDeviceManager().rescanMidiDeviceList();
        edit = tracktion::Edit::createSingleTrackEdit (engine);
        edit->clickTrackEnabled = false;
    }
    ~TracktionSession() override
    {
        cancelPendingUpdate();
        edit->getTransport().stop (true, true);
    }
    void prepare (double rate, int block)
    {
        requestedRate.store (rate);
        requestedBlock.store (block);
        ready.store (false);
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) handleAsyncUpdate();
        else triggerAsyncUpdate();
    }
    bool isReady() const { return ready.load() && engine.getDeviceManager().getNumMidiOutDevices() > 0; }

    bool load (const std::vector<EducationalDawEngine::NoteEvent>& events, int bpm, double duration)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (! isReady()) return false;
        auto& transport = edit->getTransport();
        transport.stop (true, true);
        edit->tempoSequence.getTempo (0)->setBpm (bpm);
        std::array<bool, 16> used {};
        for (const auto& event : events) used[event.channel - 1] = true;
        int count = 0;
        for (bool channel : used) if (channel) ++count;
        edit->ensureNumberOfAudioTracks (juce::jmax (1, count));
        auto tracks = tracktion::getAudioTracks (*edit);
        for (auto* track : tracks)
        {
            auto clips = track->getClips();
            for (auto* clip : clips) clip->removeFromParent();
            track->getOutput().setOutputToDefaultDevice (true);
        }
        for (int i = 0; i < engine.getDeviceManager().getNumMidiOutDevices(); ++i)
            engine.getDeviceManager().getMidiOutDevice (i)->setEnabled (true);
        int index = 0;
        for (int channel = 1; channel <= 16; ++channel)
        {
            if (! used[channel - 1]) continue;
            auto* track = tracks[index++];
            track->setName ("MIDI channel " + juce::String (channel));
            auto clip = track->insertMIDIClip ("HMPS sequence",
                { tracktion::TimePosition::fromSeconds (0),
                  tracktion::TimePosition::fromSeconds (juce::jmax (.01, duration)) }, nullptr);
            auto& list = clip->getSequence();
            list.setMidiChannel (tracktion::MidiChannel (channel));
            std::array<double, 128> starts;
            starts.fill (-1.0);
            std::array<int, 128> velocities {};
            const double beatsPerSecond = bpm / 60.0;
            auto add = [&] (int note, double end)
            {
                if (starts[note] < 0) return;
                list.addNote (note, tracktion::BeatPosition::fromBeats (starts[note] * beatsPerSecond),
                    tracktion::BeatDuration::fromBeats (juce::jmax (.001, end - starts[note]) * beatsPerSecond),
                    velocities[note], 0, nullptr);
                starts[note] = -1;
            };
            for (const auto& event : events)
            {
                if (event.channel != channel) continue;
                if (event.on && event.velocity > 0)
                {
                    add (event.note, event.seconds);
                    starts[event.note] = event.seconds;
                    velocities[event.note] = event.velocity;
                }
                else add (event.note, event.seconds);
            }
            for (int note = 0; note < 128; ++note) add (note, duration);
        }
        edit->restartPlayback();
        transport.ensureContextAllocated (true);
        return true;
    }
    void play (double duration, bool loop)
    {
        auto& transport = edit->getTransport();
        transport.setLoopRange ({ tracktion::TimePosition::fromSeconds (0),
                                  tracktion::TimePosition::fromSeconds (juce::jmax (.01, duration)) });
        transport.looping = loop;
        transport.setPosition (tracktion::TimePosition::fromSeconds (0));
        transport.play (false);
    }
    void setLooping (bool enabled) { edit->getTransport().looping = enabled; }
    void stop() { edit->getTransport().stop (true, false); }
    void process (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        host.processBlock (audio, midi);
    }
    double position() const
    {
        if (auto* context = edit->getTransport().getCurrentPlaybackContext())
            return context->getPosition().inSeconds();
        return 0;
    }
    int noteCount() const
    {
        int count = 0;
        for (auto* track : tracktion::getAudioTracks (*edit))
            for (auto* clip : track->getClips())
                if (auto* midi = dynamic_cast<tracktion::MidiClip*> (clip))
                    count += midi->getSequence().getNumNotes();
        return count;
    }
private:
    void handleAsyncUpdate() override
    {
        host.prepareToPlay (requestedRate.load(), requestedBlock.load());
        ready.store (true);
    }
    tracktion::Engine engine;
    tracktion::HostedAudioDeviceInterface& host;
    std::unique_ptr<tracktion::Edit> edit;
    std::atomic<double> requestedRate { 44100 };
    std::atomic<int> requestedBlock { 512 };
    std::atomic<bool> ready { false };
};
