#pragma once
#include "Source/PluginProcessor.h"

// Owns one explicitly selected input. Device changes are handled on the message
// thread; MIDI callbacks only enqueue messages, never touch UI or session files.
class ExternalMidiInput : private juce::MidiInputCallback, private juce::Timer
{
public:
    explicit ExternalMidiInput (EducationalDawEngine& owner) : engine (owner) { startTimer (500); }
    ~ExternalMidiInput() override { stopTimer(); close(); }
    void select (const juce::String& identifier)
    {
        close();
        selected = identifier;
        selectedName.clear();
        refresh();
    }
    juce::String identifier() const { return selected; }
    juce::String status() const
    {
        if (selected.isEmpty()) return "MIDI: None";
        if (input) return "MIDI: " + selectedName;
        return "MIDI: disconnected / unavailable";
    }
private:
    void close()
    {
        if (input) { input->stop(); input.reset(); }
        engine.panicExternalMidi();
    }
    void refresh()
    {
        if (selected.isEmpty()) return;
        const auto devices = juce::MidiInput::getAvailableDevices();
        bool present = false;
        for (const auto& device : devices)
            if (device.identifier == selected) { present = true; selectedName = device.name; break; }
        if (! present) { if (input) close(); return; }
        if (! input)
        {
            input = juce::MidiInput::openDevice (selected, this);
            if (input) input->start();
        }
    }
    void timerCallback() override { refresh(); }
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
    {
        engine.receiveExternalMidi (message);
    }
    EducationalDawEngine& engine;
    juce::String selected, selectedName;
    std::unique_ptr<juce::MidiInput> input;
};
