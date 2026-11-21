#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class ExternalMidiOutput : private juce::Thread
{
public:
    ExternalMidiOutput() : juce::Thread ("HMPS MIDI output") {}

    ~ExternalMidiOutput() override
    {
        close();
    }

    // Call on the message thread while the engine callback lock is held.
    bool select (const juce::String& identifier)
    {
        close();

        fifo.reset();
        selectedIdentifier.clear();

        if (identifier.isEmpty())
            return true;

        output = juce::MidiOutput::openDevice (identifier);

        if (! output)
            return false;

        selectedIdentifier = identifier;

        if (! startThread())
        {
            output.reset();
            selectedIdentifier.clear();
            return false;
        }

        enabled.store (true);
        return true;
    }

    // Message-thread access only.
    juce::String identifier() const
    {
        return selectedIdentifier;
    }

    // Audio thread only: one producer for the FIFO.
    // Source 0 = built-in keyboard; sources 1–3 = plugin slots.
    void enqueue (const juce::MidiMessage& message, int source)
    {
        if (! enabled.load()
            || ! message.isNoteOnOrOff()
            || source < 0 || source > 3)
            return;

        const int note = message.getNoteNumber();

        if (note < 60 || note > 72)
            return;

        int first, count, second, count2;
        fifo.prepareToWrite (1, first, count, second, count2);

        if (count == 0)
        {
            // Recover from overflow instead of leaving LEDs stuck.
            panic();
            return;
        }

        events[first] = {
            note,
            static_cast<int> (message.getVelocity()),
            source,
            message.isNoteOn(),
            generation.load()
        };

        fifo.finishedWrite (1);
    }

    // May be called from either the message or audio thread.
    void panic()
    {
        generation.fetch_add (1);
    }

private:
    struct Event
    {
        int note = 60;
        int velocity = 0;
        int source = 0;
        bool on = false;
        unsigned generation = 0;
    };

    void close()
    {
        enabled.store (false);
        signalThreadShouldExit();
        notify();

        // Lifecycle operation, never called from the audio callback.
        stopThread (-1);

        output.reset();
        selectedIdentifier.clear();
    }

    void clearNotes()
    {
        activeSources.fill (0);

        if (! output)
            return;

        // Explicit Note Off messages support simple ESP32 receivers.
        for (int note = 60; note <= 72; ++note)
            output->sendMessageNow (
                juce::MidiMessage::noteOff (1, note));

        output->sendMessageNow (
            juce::MidiMessage::allNotesOff (1));
    }

    void run() override
    {
        auto currentGeneration = generation.load();
        clearNotes();

        while (! threadShouldExit())
        {
            const auto requestedGeneration = generation.load();

            if (requestedGeneration != currentGeneration)
            {
                clearNotes();
                currentGeneration = requestedGeneration;
            }

            int first, count, second, count2;
            fifo.prepareToRead (1, first, count, second, count2);

            if (count == 0)
            {
                wait (2);
                continue;
            }

            const auto event = events[first];
            fifo.finishedRead (1);

            // Ignore events queued before Stop or another reset.
            if (event.generation != currentGeneration
                || event.generation != generation.load())
                continue;

            const auto index = static_cast<size_t> (event.note - 60);
            const unsigned bit = 1u << event.source;
            const unsigned before = activeSources[index];

            const unsigned after = event.on
                ? before | bit
                : before & ~bit;

            activeSources[index] = after;

            if (before == 0 && after != 0)
            {
                output->sendMessageNow (
                    juce::MidiMessage::noteOn (
                        1, event.note,
                        static_cast<juce::uint8> (event.velocity)));
            }
            else if (before != 0 && after == 0)
            {
                output->sendMessageNow (
                    juce::MidiMessage::noteOff (1, event.note));
            }
        }

        clearNotes();
    }

    std::unique_ptr<juce::MidiOutput> output;
    juce::String selectedIdentifier;

    juce::AbstractFifo fifo { 1024 };
    std::array<Event, 1024> events {};
    std::array<unsigned, 13> activeSources {};

    std::atomic<bool> enabled { false };
    std::atomic<unsigned> generation { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExternalMidiOutput)
};
