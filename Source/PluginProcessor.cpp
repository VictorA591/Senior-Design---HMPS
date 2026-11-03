#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
class ToneSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class ToneVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<ToneSound*> (sound) != nullptr;
    }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        increment = juce::MathConstants<double>::twoPi
                  * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
        level = velocity * 0.06f;
        envelope.setSampleRate (getSampleRate());
        envelope.setParameters ({ 0.01f, 0.05f, 0.8f, 0.15f });
        envelope.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            envelope.noteOff();
        else
        {
            envelope.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int start, int count) override
    {
        if (! isVoiceActive())
            return;

        for (int sample = start; sample < start + count; ++sample)
        {
            auto value = static_cast<float> (std::sin (phase)) * level * envelope.getNextSample();
            phase += increment;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample (channel, sample, value);

            if (! envelope.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    double phase = 0.0, increment = 0.0;
    float level = 0.0f;
    juce::ADSR envelope;
};
}

EducationalDawEngine::EducationalDawEngine()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    session.reserve (maxEvents + 2048);
    for (int voice = 0; voice < 16; ++voice)
        synthesiser.addVoice (new ToneVoice());
    synthesiser.addSound (new ToneSound());
}

EducationalDawEngine::~EducationalDawEngine() = default;

void EducationalDawEngine::prepareToPlay (double sampleRate, int)
{
    stopTransportLocked();
    sessionRate = sampleRate;
    audioReady = sampleRate > 0.0;
    synthesiser.setCurrentPlaybackSampleRate (sampleRate);
}

void EducationalDawEngine::releaseResources()
{
    stopTransportLocked();
    audioReady = false;
}

void EducationalDawEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (! message.isNoteOn())
            continue;

        auto state = lessonState.load();
        while (state == LessonState::waiting || state == LessonState::tryAgain)
        {
            const auto result = message.getNoteNumber() == 60
                ? LessonState::complete : LessonState::tryAgain;
            // Only publish a small value from the audio thread; the UI formats feedback.
            if (lessonState.compare_exchange_weak (state, result))
                break;
        }
    }

    processSession (midiMessages, buffer.getNumSamples());
    synthesiser.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* EducationalDawEngine::createEditor()
{
    return new EducationalDawEditor (*this);
}

void EducationalDawEngine::stopTransportLocked()
{
    if (transport == Transport::recording)
    {
        const auto end = static_cast<double> (transportSamples) / sessionRate;
        for (int channel = 0; channel < 16; ++channel)
            for (int note = 0; note < 128; ++note)
                if (held[channel][note])
                    session.push_back ({ end, channel + 1, note, 0, false });
    }
    held = {};
    transport = Transport::stopped;
    synthesiser.allNotesOff (0, false);
}

void EducationalDawEngine::stopTransport()
{
    const juce::ScopedLock lock (getCallbackLock());
    stopTransportLocked();
}

void EducationalDawEngine::newSession()
{
    const juce::ScopedLock lock (getCallbackLock());
    stopTransportLocked();
    session.clear();
    transportSamples = 0;
    recordingFull = false;
}

bool EducationalDawEngine::startRecording()
{
    const juce::ScopedLock lock (getCallbackLock());
    if (! audioReady || ! session.empty() || transport != Transport::stopped)
        return false;
    held = {};
    transportSamples = 0;
    recordingFull = false;
    synthesiser.allNotesOff (0, false);
    transport = Transport::recording;
    return true;
}

bool EducationalDawEngine::startPlayback()
{
    const juce::ScopedLock lock (getCallbackLock());
    if (! audioReady || session.empty() || transport != Transport::stopped)
        return false;
    synthesiser.allNotesOff (0, false);
    transportSamples = 0;
    playbackIndex = 0;
    transport = Transport::playing;
    return true;
}

EducationalDawEngine::SessionStatus EducationalDawEngine::getSessionStatus()
{
    const juce::ScopedLock lock (getCallbackLock());
    return { transport, static_cast<int> (session.size()),
             static_cast<double> (transportSamples) / sessionRate, recordingFull };
}

std::vector<EducationalDawEngine::NoteEvent> EducationalDawEngine::copySession()
{
    std::vector<NoteEvent> copy;
    copy.reserve (maxEvents + 2048);
    const juce::ScopedLock lock (getCallbackLock());
    if (transport == Transport::stopped)
        copy.assign (session.begin(), session.end());
    return copy;
}

bool EducationalDawEngine::loadSession (const std::vector<NoteEvent>& events)
{
    if (events.empty() || events.size() > maxEvents + 2048)
        return false;
    double previous = 0.0;
    for (const auto& event : events)
    {
        if (! std::isfinite (event.seconds) || event.seconds < previous
            || event.seconds > 86400.0 || event.channel < 1 || event.channel > 16
            || event.note < 0 || event.note > 127 || event.velocity < 0 || event.velocity > 127)
            return false;
        previous = event.seconds;
    }
    const juce::ScopedLock lock (getCallbackLock());
    stopTransportLocked();
    session.assign (events.begin(), events.end());
    transportSamples = 0;
    recordingFull = false;
    return true;
}

void EducationalDawEngine::processSession (juce::MidiBuffer& midi, int numSamples)
{
    if (transport == Transport::recording)
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (! message.isNoteOnOrOff())
                continue;
            if (session.size() >= maxEvents)
            {
                recordingFull = true;
                // Close held notes at the end of this block using reserved capacity.
                transportSamples += numSamples;
                stopTransportLocked();
                return;
            }
            const int channel = message.getChannel();
            const int note = message.getNoteNumber();
            const bool on = message.isNoteOn();
            // Ignore releases for notes pressed before Record.
            if (! on && ! held[channel - 1][note])
                continue;
            session.push_back ({ (transportSamples + metadata.samplePosition) / sessionRate,
                                 channel, note, message.getVelocity(), on });
            held[channel - 1][note] = on;
        }
        transportSamples += numSamples;
    }
    else if (transport == Transport::playing)
    {
        // Live input is suppressed during playback; it cannot enter the saved take.
        midi.clear();
        const auto end = transportSamples + numSamples;
        while (playbackIndex < session.size())
        {
            const auto& event = session[playbackIndex];
            const auto sample = static_cast<int64_t> (std::llround (event.seconds * sessionRate));
            if (sample >= end)
                break;
            const auto message = event.on
                ? juce::MidiMessage::noteOn (event.channel, event.note, static_cast<juce::uint8> (event.velocity))
                : juce::MidiMessage::noteOff (event.channel, event.note);
            midi.addEvent (message, static_cast<int> (juce::jmax<int64_t> (0, sample - transportSamples)));
            ++playbackIndex;
        }
        transportSamples = end;
        if (playbackIndex == session.size())
        {
            for (int channel = 1; channel <= 16; ++channel)
                midi.addEvent (juce::MidiMessage::allNotesOff (channel), numSamples - 1);
            transport = Transport::stopped;
        }
    }
}
