#pragma once
#include "SessionArrangement.h"

class StepLessonComponent : public juce::Component, private juce::Timer
{
public:
    explicit StepLessonComponent (juce::MidiKeyboardState& state) : keys (state)
    {
        addAndMakeVisible (grid);
        addAndMakeVisible (status);
        status.setJustificationType (juce::Justification::centred);
        status.setColour (juce::Label::textColourId, juce::Colours::white);
        for (auto* button : { &target, &preview, &hints, &submit, &retry })
            addAndMakeVisible (*button);
        target.onClick = [this] { play (grid.target); };
        preview.onClick = [this] { play (grid.pattern); };
        hints.onClick = [this]
        {
            grid.hints = ! grid.hints;
            hints.setButtonText (grid.hints ? "Hide Hints" : "Hints");
            grid.repaint();
        };
        submit.onClick = [this]
        {
            stopPreview();
            int expected = 0, correct = 0, extra = 0;
            for (int i = 0; i < 32; ++i)
            {
                if (grid.target[i]) { ++expected; if (grid.pattern[i]) ++correct; }
                else if (grid.pattern[i]) ++extra;
            }
            const int score = 100 * correct / juce::jmax (1, expected + extra);
            passed = score >= 80;
            status.setText ("You scored " + juce::String (score) + "%. "
                + (passed ? "Tutorial complete! Return to the main menu or retry."
                          : "Reach 80% to pass. Compare the rhythms and try again."),
                juce::dontSendNotification);
        };
        retry.onClick = [this] { reset(); };
        grid.onChange = [this]
        {
            passed = false;
            status.setText ("One bar: 8 eighth-note steps. Listen, match the rhythm, then Submit.",
                            juce::dontSendNotification);
        };
        reset();
    }
    ~StepLessonComponent() override { stopPreview(); }
    bool hasPassed() const { return passed; }
    void reset()
    {
        stopPreview();
        passed = false;
        grid.pattern = {};
        grid.target = {{
            true,false,false,false,true,false,false,false,
            false,false,true,false,false,false,true,false,
            true,false,true,false,true,false,true,false,
            false,false,false,false,false,false,false,false
        }};
        grid.hints = false;
        hints.setButtonText ("Hints");
        status.setText ("Listen to the target, then recreate it. Gold hints mark required hits. Pass: 80%.",
                        juce::dontSendNotification);
        grid.repaint();
    }
    juce::var progress() const
    {
        juce::Array<juce::var> values;
        for (bool on : grid.pattern) values.add (on);
        return juce::var (values);
    }
    void restoreProgress (const juce::var& value)
    {
        if (auto* values = value.getArray(); values != nullptr && values->size() == 32)
            for (int i = 0; i < 32; ++i) grid.pattern[i] = static_cast<bool> ((*values)[i]);
        grid.repaint();
    }
    void stopPreview()
    {
        stopTimer();
        for (int note : SessionArrangement::drumNotes) keys.noteOff (10, note, 0);
        offAt.fill (0);
        grid.activeStep = -1;
        grid.repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        status.setBounds (area.removeFromTop (48));
        auto buttons = area.removeFromBottom (40);
        int width = buttons.getWidth() / 5;
        for (auto* button : { &target, &preview, &hints, &submit, &retry })
            button->setBounds (buttons.removeFromLeft (width).reduced (2));
        grid.setBounds (area.reduced (0, 8));
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff18252f)); }
private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() * .001; }
    void play (const std::array<bool, 32>& pattern)
    {
        stopPreview();
        previewPattern = pattern;
        started = now();
        lastStep = -1;
        startTimerHz (60);
        timerCallback();
    }
    void timerCallback() override
    {
        const double time = now();
        const double elapsed = time - started;
        for (int row = 0; row < 4; ++row)
            if (offAt[row] > 0 && time >= offAt[row])
            {
                keys.noteOff (10, SessionArrangement::drumNotes[row], 0);
                offAt[row] = 0;
            }
        if (elapsed >= 2.0) { stopPreview(); return; }
        const int step = static_cast<int> (elapsed / .25);
        if (step != lastStep)
        {
            lastStep = step;
            grid.activeStep = step;
            // Preview timing is for the lesson only; project playback uses the audio engine.
            for (int row = 0; row < 4; ++row)
                if (previewPattern[row * 8 + step])
                {
                    keys.noteOn (10, SessionArrangement::drumNotes[row], .7f);
                    offAt[row] = time + .08;
                }
            grid.repaint();
        }
    }
    juce::MidiKeyboardState& keys;
    StepGrid grid;
    juce::Label status;
    juce::TextButton target { "Hear Target" }, preview { "Hear My Pattern" }, hints { "Hints" },
                     submit { "Submit" }, retry { "Retry" };
    bool passed = false;
    std::array<bool, 32> previewPattern {};
    std::array<double, 4> offAt {};
    double started = 0;
    int lastStep = -1;
};
