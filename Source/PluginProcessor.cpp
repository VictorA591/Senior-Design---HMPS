#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../TracktionSession.h"
#include "../ExternalMidiInput.h"
#include "../KeyboardInstrument.h"
#include <cmath>

namespace
{
class VoiceGain
{
public:
    explicit VoiceGain (std::array<std::atomic<float>, 16>& values) : levels (values) {}
    void beginGain (juce::SynthesiserVoice& voice)
    {
        for (int i = 1; i <= 16; ++i)
            if (voice.isPlayingChannel (i)) { channel = i - 1; break; }
        gain = target = levels[channel].load();
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (voice.getSampleRate() * .005)));
    }
    void updateGain() { target = levels[channel].load(); }
    float nextGain() { gain += (target - gain) * smoothing; return gain; }
private:
    std::array<std::atomic<float>, 16>& levels;
    int channel = 0;
    float gain = 1, target = 1, smoothing = .004f;
};
class DrumSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int channel) override { return (channel >= 10 && channel <= 12); }
};
class DrumVoice : public juce::SynthesiserVoice, private VoiceGain
{
public:
    explicit DrumVoice (std::array<std::atomic<float>, 16>& levels) : VoiceGain (levels) {}
    bool canPlaySound (juce::SynthesiserSound* sound) override { return dynamic_cast<DrumSound*> (sound) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        beginGain (*this);
        pitch = note; level = velocity * .12f; phase = 0; age = 0;
    }
    void stopNote (float, bool tail) override { if (! tail) clearCurrentNote(); }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int start, int count) override
    {
        if (! isVoiceActive()) return;
        updateGain();
        for (int i = start; i < start + count; ++i)
        {
            const double t = age++ / getSampleRate();
            const bool noise = pitch == 38 || pitch == 42;
            const double decay = pitch == 42 ? 45 : (pitch == 38 ? 22 : 12);
            const double envelope = std::exp (-t * decay);
            const double frequency = (pitch == 36 ? 55 : 130) + 90 * std::exp (-t * 35);
            phase += juce::MathConstants<double>::twoPi * frequency / getSampleRate();
            const float wave = noise ? random.nextFloat() * 2 - 1 : static_cast<float> (std::sin (phase));
            const float value = wave * level * static_cast<float> (envelope) * nextGain();
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample (channel, i, value);
            if (envelope < .001) { clearCurrentNote(); break; }
        }
    }
private:
    juce::Random random { 12345 };
    int pitch = 36;
    int64_t age = 0;
    double phase = 0;
    float level = 0;
};

}

EducationalDawEngine::EducationalDawEngine()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    timeline = std::make_unique<TracktionSession>();
    midiInput = std::make_unique<ExternalMidiInput> (*this);
    for (auto& level : channelLevels) level.store (1.0f);
    for (auto& sound : keyboardSounds) sound.store (0);
    session.reserve (3 * (maxEvents + 2048));
    backing.reserve (3 * (maxEvents + 2048));
    for (int voice = 0; voice < 16; ++voice)
        synthesiser.addVoice (new hmps::KeyboardVoice (channelLevels, keyboardSounds));
    synthesiser.addSound (new hmps::KeyboardSound());
    synthesiser.setNoteStealingEnabled (true);
    for (int voice = 0; voice < 12; ++voice) synthesiser.addVoice (new DrumVoice (channelLevels));
    synthesiser.addSound (new DrumSound());
}

EducationalDawEngine::~EducationalDawEngine() { midiInput.reset(); clearInstruments(); }

void EducationalDawEngine::prepareToPlay (double sampleRate, int blockSize)
{
    midiReady.store (false);
    externalCollector.reset (sampleRate);
    externalBuffer.ensureSize (65536);
    midiReady.store (true);
    stopTransportLocked();
    sessionRate = sampleRate;
    audioReady = sampleRate > 0.0;
    synthesiser.setCurrentPlaybackSampleRate (sampleRate);
    preparedBlockSize = juce::jmax (1, blockSize);
    builtInMidi.ensureSize (65536);
    timelineMidi.ensureSize (65536);
    timeline->prepare (sampleRate, preparedBlockSize);
    for (auto& slot : instruments)
    {
        slot.audio.setSize (2, preparedBlockSize);
        slot.midi.ensureSize (65536);
        slot.gain.reset (sampleRate, .005);
        slot.gain.setCurrentAndTargetValue (1);
        if (slot.instance)
        {
            slot.instance->setRateAndBufferSizeDetails (sampleRate, preparedBlockSize);
            slot.instance->prepareToPlay (sampleRate, preparedBlockSize);
        }
    }
}

void EducationalDawEngine::releaseResources()
{
    stopTransportLocked();
    audioReady = false;
    midiReady.store (false);
    for (auto& slot : instruments) if (slot.instance) slot.instance->releaseResources();
}

void EducationalDawEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (isSuspended()) return;
    blockStart = transportSamples;
    collectExternalMidi (midiMessages, buffer.getNumSamples());
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

    hostPlayHead.position.setBpm (static_cast<double> (hostTempo.load()));
    hostPlayHead.position.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    hostPlayHead.position.setIsPlaying (transport != Transport::stopped);
    hostPlayHead.position.setIsRecording (transport == Transport::recording);
    hostPlayHead.position.setIsLooping (looping.load() && transport == Transport::playing);
    hostPlayHead.position.setLoopPoints (juce::AudioPlayHead::LoopPoints {
        0.0, playbackLength / sessionRate * hostTempo.load() / 60.0 });
    const bool wasRunning = transport != Transport::stopped;
    processSession (midiMessages, buffer.getNumSamples());
    if (wasRunning && ! timelineStopPending.load())
    {
        timelineMidi.clear();
        timeline->process (buffer, timelineMidi);
        buffer.clear();
        // Live input is captured first; Tracktion accompaniment is never recorded twice.
        midiMessages.addEvents (timelineMidi, 0, buffer.getNumSamples(), 0);
    }
    renderInstruments (buffer, midiMessages);
    if (wasRunning && transport == Transport::stopped) timelineStopPending.store (true);
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
    timelineStopPending.store (true);
    synthesiser.allNotesOff (0, false);
    for (auto& slot : instruments) slot.panic = true;
}

void EducationalDawEngine::stopTransport()
{
    const juce::ScopedLock lock (getCallbackLock());
    const bool recorded = transport == Transport::recording;
    stopTransportLocked();
    timeline->stop();
    timelineStopPending.store (false);
    if (recorded && ! session.empty())
        timeline->load (session, hostTempo.load(), session.back().seconds + .01);
}

void EducationalDawEngine::setLooping (bool enabled)
{
    const juce::ScopedLock lock (getCallbackLock());
    looping.store (enabled);
    timeline->setLooping (enabled && transport == Transport::playing);
}

int EducationalDawEngine::tracktionNoteCount() const { return timeline->noteCount(); }

void EducationalDawEngine::serviceTimeline()
{
    const juce::ScopedLock lock (getCallbackLock());
    if (timelineStopPending.exchange (false))
    {
        timeline->stop();
        if (recordingFull && ! session.empty())
            timeline->load (session, hostTempo.load(), session.back().seconds + .01);
    }
}

void EducationalDawEngine::newSession()
{
    const juce::ScopedLock lock (getCallbackLock());
    stopTransportLocked();
    timeline->stop();
    timelineStopPending.store (false);
    session.clear();
    transportSamples = 0;
    recordingFull = false;
}

bool EducationalDawEngine::startRecording (const std::vector<NoteEvent>& accompaniment)
{
    if (accompaniment.size() > 3 * (maxEvents + 2048)) return false;
    const juce::ScopedLock lock (getCallbackLock());
    if (! audioReady || ! session.empty() || transport != Transport::stopped)
        return false;
    const double length = accompaniment.empty() ? .01 : accompaniment.back().seconds + .01;
    if (! timeline->load (accompaniment, hostTempo.load(), length)) return false;
    timeline->play (length, false);
    timelineStopPending.store (false);
    held = {};
    transportSamples = 0;
    recordingFull = false;
    synthesiser.allNotesOff (0, false);
    backing.assign (accompaniment.begin(), accompaniment.end());
    backingIndex = 0;
    transport = Transport::recording;
    return true;
}

bool EducationalDawEngine::startPlayback (double durationSeconds)
{
    const juce::ScopedLock lock (getCallbackLock());
    if (! audioReady || session.empty() || transport != Transport::stopped)
        return false;
    synthesiser.allNotesOff (0, false);
    transportSamples = 0;
    playbackIndex = 0;
    const double length = std::isfinite (durationSeconds) && durationSeconds > 0
        ? juce::jmax (durationSeconds, session.back().seconds)
        : session.back().seconds + 1.0 / sessionRate;
    playbackLength = juce::jmax<int64_t> (1, static_cast<int64_t> (std::llround (length * sessionRate)));
    const double tracktionLength = juce::jmax (.01, length);
    playbackLength = static_cast<int64_t> (std::llround (tracktionLength * sessionRate));
    if (! timeline->load (session, hostTempo.load(), tracktionLength)) return false;
    timeline->play (tracktionLength, looping.load());
    timelineStopPending.store (false);
    transport = Transport::playing;
    return true;
}

EducationalDawEngine::SessionStatus EducationalDawEngine::getSessionStatus()
{
    const juce::ScopedLock lock (getCallbackLock());
    return { transport, static_cast<int> (session.size()),
             static_cast<double> (transport == Transport::playing && looping.load()
                ? transportSamples % playbackLength : transportSamples) / sessionRate, recordingFull };
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
    if (events.empty() || events.size() > 3 * (maxEvents + 2048))
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
        midi.clear();
        // Tracktion schedules MIDI; this counter only bounds the UI's playback span.
        transportSamples += numSamples;
        if (looping.load()) transportSamples %= playbackLength;
        else if (transportSamples >= playbackLength)
        {
            transport = Transport::stopped;
            // Stop on the message thread after the current graph block has rendered.
            timelineStopPending.store (false);
        }
    }
}

juce::AudioPluginInstance* EducationalDawEngine::instrument (int track) const
{
    return track >= 0 && track < 3 ? instruments[track].instance.get() : nullptr;
}

juce::String EducationalDawEngine::installInstrument (int track, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (track < 0 || track >= 3 || instance == nullptr) return "Invalid instrument.";
    if (! instance->acceptsMidi()) return "Choose a MIDI instrument, not an audio effect.";
    instance->disableNonMainBuses();
    auto layout = instance->getBusesLayout();
    for (auto& input : layout.inputBuses) input = juce::AudioChannelSet::disabled();
    if (layout.outputBuses.isEmpty()) return "This instrument has no audio output.";
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! instance->setBusesLayout (layout))
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        if (! instance->setBusesLayout (layout)) return "This instrument needs an unsupported audio layout.";
    }
    if (instance->getTotalNumInputChannels() != 0 || instance->getTotalNumOutputChannels() < 1 || instance->getTotalNumOutputChannels() > 2)
        return "Only mono or stereo instruments without audio inputs are supported.";
    double rate;
    int block;
    {
        const juce::ScopedLock lock (getCallbackLock());
        rate = sessionRate;
        block = preparedBlockSize;
    }
    instance->setPlayHead (&hostPlayHead);
    instance->setRateAndBufferSizeDetails (rate, block);
    instance->prepareToPlay (rate, block);
    std::unique_ptr<juce::AudioPluginInstance> previous;
    {
        const juce::ScopedLock lock (getCallbackLock());
        if (transport != Transport::stopped) return "Stop playback before changing instruments.";
        // A device reconfiguration during creation must not install a stale format.
        if (rate != sessionRate || block != preparedBlockSize) return "Audio device changed. Please load the instrument again.";
        auto& slot = instruments[track];
        slot.audio.setSize (2, block);
        slot.midi.ensureSize (65536);
        slot.gain.reset (rate, .005);
        slot.gain.setCurrentAndTargetValue (1);
        previous = std::move (slot.instance);
        slot.instance = std::move (instance);
        slot.instance->addListener (this);
        slot.panic = true;
        synthesiser.allNotesOff (0, false);
    }
    if (previous) { previous->removeListener (this); previous->releaseResources(); }
    return {};
}

void EducationalDawEngine::removeInstrument (int track)
{
    if (track < 0 || track >= 3) return;
    std::unique_ptr<juce::AudioPluginInstance> previous;
    {
        const juce::ScopedLock lock (getCallbackLock());
        previous = std::move (instruments[track].instance);
        synthesiser.allNotesOff (0, false);
    }
    if (previous) { previous->removeListener (this); previous->releaseResources(); }
}

void EducationalDawEngine::clearInstruments()
{
    for (int i = 0; i < 3; ++i) removeInstrument (i);
}

juce::String EducationalDawEngine::instrumentState (int track)
{
    juce::MemoryBlock state;
    // Suspend callbacks before asking third-party code for state; it may allocate or take time.
    suspendProcessing (true);
    {
        const juce::ScopedLock lock (getCallbackLock());
        if (auto* plugin = instrument (track)) plugin->getStateInformation (state);
    }
    suspendProcessing (false);
    return juce::Base64::toBase64 (state.getData(), state.getSize());
}

void EducationalDawEngine::renderInstruments (juce::AudioBuffer<float>& output, juce::MidiBuffer& incoming)
{
    builtInMidi.clear();
    for (auto& slot : instruments) slot.midi.clear();
    const bool enabled = instrumentsEnabled.load();
    for (const auto metadata : incoming)
    {
        auto message = metadata.getMessage();
        const int channel = message.getChannel();
        int track = channel >= 1 && channel <= 3 ? channel - 1
                  : channel >= 10 && channel <= 12 ? channel - 10
                  : channel == 16 ? liveTrack.load() : -1;
        if (enabled && track >= 0 && track < 3 && instruments[track].instance)
        {
            message.setChannel (channel >= 10 && channel <= 12 ? 10 : 1);
            instruments[track].midi.addEvent (message, metadata.samplePosition);
        }
        else
            builtInMidi.addEvent (message, metadata.samplePosition);
    }
    synthesiser.renderNextBlock (output, builtInMidi, 0, output.getNumSamples());
    for (int i = 0; i < 3; ++i)
    {
        auto& slot = instruments[i];
        if (! slot.instance) continue;
        if (! enabled) { slot.panic = true; continue; }
        if (slot.panic)
        {
            slot.midi.clear();
            for (int channel = 1; channel <= 16; ++channel)
                slot.midi.addEvent (juce::MidiMessage::allSoundOff (channel), 0);
            // Preserve this block's real notes after the panic messages.
            for (const auto metadata : incoming)
            {
                auto message = metadata.getMessage();
                const int channel = message.getChannel();
                if (channel == i + 1 || channel == i + 10 || (channel == 16 && liveTrack.load() == i))
                {
                    message.setChannel (channel == i + 10 ? 10 : 1);
                    slot.midi.addEvent (message, metadata.samplePosition);
                }
            }
            slot.panic = false;
        }
        // Choose the assigned track's gain, independent of incoming MIDI velocities.
        // Both keyboard and drum channel levels are populated for each track by the editor.
        slot.gain.setTargetValue (channelLevels[i].load());
        for (int start = 0; start < output.getNumSamples();)
        {
            auto position = blockStart + start;
            int count = juce::jmin (preparedBlockSize, output.getNumSamples() - start);
            // Split at loop boundaries so tempo-synced instruments see the same wrap as MIDI.
            if (hostPlayHead.position.getIsLooping() && playbackLength > 0)
            {
                position %= playbackLength;
                count = static_cast<int> (juce::jmin<int64_t> (count, playbackLength - position));
            }
            auto audio = juce::AudioBuffer<float> (slot.audio.getArrayOfWritePointers(),
                                                   slot.instance->getTotalNumOutputChannels(), count);
            audio.clear();
            juce::MidiBuffer blockMidi;
            blockMidi.swapWith (builtInMidi);
            blockMidi.clear();
            blockMidi.addEvents (slot.midi, start, count, -start);
            const double seconds = position / sessionRate;
            hostPlayHead.position.setTimeInSamples (position);
            hostPlayHead.position.setTimeInSeconds (seconds);
            const double ppq = seconds * hostTempo.load() / 60;
            hostPlayHead.position.setPpqPosition (ppq);
            hostPlayHead.position.setPpqPositionOfLastBarStart (std::floor (ppq / 4) * 4);
            slot.instance->processBlock (audio, blockMidi);
            // Capture notes produced by an instrument's onscreen keyboard.
            // Ignore echoed input so hardware notes/accompaniment are not recorded twice.
            if (slot.instance->producesMidi() && transport == Transport::recording && liveTrack.load() == i)
                for (const auto event : blockMidi)
                {
                    const auto message = event.getMessage();
                    if (! message.isNoteOnOrOff()) continue;
                    bool echoed = false;
                    for (const auto input : slot.midi)
                        if (input.samplePosition == start + event.samplePosition
                            && input.numBytes == event.numBytes
                            && std::memcmp (input.data, event.data, static_cast<size_t> (event.numBytes)) == 0) { echoed = true; break; }
                    if (echoed) continue;
                    const int note = message.getNoteNumber();
                    const bool on = message.isNoteOn();
                    if (! on && ! held[15][note]) continue;
                    if (session.size() >= maxEvents)
                    {
                        recordingFull = true;
                        stopTransportLocked();
                        break;
                    }
                    NoteEvent captured { (blockStart + start + juce::jlimit (0, count - 1, event.samplePosition)) / sessionRate,
                                         16, note, message.getVelocity(), on };
                    // Capacity is reserved; keep UI and physical events in timestamp order.
                    const auto at = std::upper_bound (session.begin(), session.end(), captured.seconds,
                        [] (double time, const NoteEvent& existing) { return time < existing.seconds; });
                    session.insert (at, captured);
                    held[15][note] = on;
                }
            blockMidi.swapWith (builtInMidi);
            for (int sample = 0; sample < count; ++sample)
            {
                const float gain = slot.gain.getNextValue();
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                    output.addSample (channel, start + sample,
                        audio.getSample (juce::jmin (channel, audio.getNumChannels() - 1), sample) * gain);
            }
            start += count;
        }
    }
}

void EducationalDawEngine::selectMidiInput (const juce::String& identifier) { midiInput->select (identifier); }
juce::String EducationalDawEngine::midiInputIdentifier() const { return midiInput->identifier(); }
juce::String EducationalDawEngine::midiInputStatus() const { return midiInput->status(); }

void EducationalDawEngine::receiveExternalMidi (const juce::MidiMessage& source)
{
    if (! midiReady.load() || source.getRawDataSize() > 3 || source.getChannel() == 0) return;
    auto message = source;
    if (message.getTimeStamp() <= 0)
        message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * .001);
    externalCollector.addMessageToQueue (message);
}

void EducationalDawEngine::setExternalMidiMode (int mode, int drumChannel)
{
    const bool modeChanged = externalMode.exchange (mode) != mode;
    const bool drumChanged = externalDrumChannel.exchange (drumChannel) != drumChannel;
    if (modeChanged || drumChanged)
        panicExternalMidi();
}

bool EducationalDawEngine::popExternalNote (ExternalNote& note)
{
    int first, count, second, count2;
    externalNotesFifo.prepareToRead (1, first, count, second, count2);
    if (count == 0) return false;
    note = externalNotes[first];
    externalNotesFifo.finishedRead (1);
    return true;
}

void EducationalDawEngine::collectExternalMidi (juce::MidiBuffer& midi, int samples)
{
    externalBuffer.clear();
    externalCollector.removeNextBlockOfMessages (externalBuffer, samples);
    if (externalPanic.exchange (false))
    {
        // A cable removal must release sustained notes and close any recorded held notes.
        for (int channel = 0; channel < 16; ++channel)
        {
            for (int note = 0; note < 128; ++note)
                if (held[channel][note])
                    midi.addEvent (juce::MidiMessage::noteOff (channel + 1, note), 0);
            midi.addEvent (juce::MidiMessage::allSoundOff (channel + 1), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (channel + 1, 64, 0), 0);
        }
        keyboardState.reset();
    }
    const int mode = externalMode.load();
    if (mode == 0) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * .001;
    for (const auto metadata : externalBuffer)
    {
        auto message = metadata.getMessage();
        const bool drum = message.getChannel() == 10;
        const int channel = drum ? externalDrumChannel.load() : (mode == 2 ? 16 : 1);
        if (channel <= 0 || (drum && mode == 1)) continue;
        message.setChannel (channel);
        midi.addEvent (message, metadata.samplePosition);
        if (mode == 1 && message.isNoteOn())
        {
            int first, count, second, count2;
            externalNotesFifo.prepareToWrite (1, first, count, second, count2);
            if (count > 0)
            {
                externalNotes[first] = { message.getNoteNumber(),
                    now - (samples - metadata.samplePosition) / sessionRate };
                externalNotesFifo.finishedWrite (1);
            }
        }
    }
}

void EducationalDawEngine::setKeyboardSound (int channel, int sound)
{
    if (channel < 1 || channel > 16) return;
    const juce::ScopedLock lock (getCallbackLock());
    sound = juce::jlimit (0, 1, sound);
    if (keyboardSounds[channel - 1].exchange (sound) != sound)
        synthesiser.allNotesOff (channel, false);
}
