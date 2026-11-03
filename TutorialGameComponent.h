#pragma once
#include <JuceHeader.h>
#include "PianoLesson.h"
#include <algorithm>

class TutorialGameComponent : public juce::Component, private juce::Timer
{
public:
    explicit TutorialGameComponent (juce::MidiKeyboardState& state) : keys (state)
    {
        for (auto* button : { &start, &listen })
            addAndMakeVisible (*button);
        start.onClick = [this] { begin (false); };
        listen.onClick = [this] { begin (true); };
        setOpaque (true);
        updateButtons();
    }

    ~TutorialGameComponent() override { deactivate(); }

    void reset()
    {
        deactivate();
        lesson = {};
        demo = false;
        elapsed = 0.0;
        message = "Tap each tile at the gold line. Reach 980 / 1400 points (70%) to pass.";
        updateButtons();
        resized();
        repaint();
    }

    void deactivate()
    {
        stopTimer();
        silence();
        lesson.running = false;
        demo = false;
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (8);
        auto controls = bounds.removeFromBottom (44);
        const int width = controls.getWidth() / 2;
        start.setBounds (controls.removeFromLeft (width).reduced (4, 0));
        listen.setBounds (controls.removeFromLeft (width).reduced (4, 0));
        bounds.removeFromBottom (8);
        statusArea = bounds.removeFromTop (44);
        board = bounds.toFloat();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff18252f));
        g.setFont (juce::FontOptions (16.0f));
        g.setColour (juce::Colours::white);
        g.drawFittedText (message, statusArea, juce::Justification::centred, 2);

        const auto laneWidth = board.getWidth() / 8.0f;
        for (int lane = 0; lane < 8; ++lane)
        {
            auto rect = juce::Rectangle<float> (board.getX() + lane * laneWidth, board.getY(),
                                                laneWidth, board.getHeight());
            g.setColour (lane % 2 ? juce::Colour (0xff223744) : juce::Colour (0xff1d303c));
            g.fillRect (rect);
            auto key = rect.removeFromBottom (keyboardHeight).reduced (2, 1);
            g.setColour (juce::Colours::ivory);
            g.fillRect (key);
            g.setColour (juce::Colours::black);
            g.drawText (whiteNoteNames[lane], key.removeFromBottom (20), juce::Justification::centred);
        }
        // Black keys sit between C/D, D/E, F/G, G/A and A/B.
        // There is deliberately no black key between E/F or B/C.
        for (const int note : { 61, 63, 66, 68, 70 })
        {
            const float width = laneWidth * 0.62f;
            auto key = juce::Rectangle<float> (noteCentre (note) - width / 2.0f,
                                               board.getBottom() - keyboardHeight,
                                               width, keyboardHeight * 0.62f);
            g.setColour (juce::Colour (0xff10151b));
            g.fillRoundedRectangle (key, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (chromaticNames[note - 60], key, juce::Justification::centred);
        }
        g.setFont (juce::FontOptions (16.0f));
        g.setColour (juce::Colour (0xffffcc66).withAlpha (0.18f));
        g.fillRect (board.getX(), hitY() - 24.0f, board.getWidth(), 48.0f);
        g.setColour (juce::Colour (0xffffcc66));
        g.drawHorizontalLine (static_cast<int> (hitY()), board.getX(), board.getRight());

        if (lesson.running || demo)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (board.withTrimmedBottom (keyboardHeight).toNearestInt());
            for (size_t i = 0; i < PianoLesson::melody.size(); ++i)
            {
                if (! demo && lesson.results[i] != 0) continue;
                const auto tile = tileBounds (static_cast<int> (i), elapsed);
                if (! tile.intersects (board)) continue;
                g.setColour (demo ? juce::Colour (0xffa694ed) : juce::Colour (0xff50c9b0));
                g.fillRoundedRectangle (tile, 6.0f);
                g.setColour (juce::Colours::black);
                g.drawText (chromaticNames[PianoLesson::melody[i].midi - 60], tile,
                            juce::Justification::centred);
            }
        }
        g.setColour (juce::Colours::white);
        g.drawText ("Score " + juce::String (lesson.score()) + "/1400   Missed "
                    + juce::String (lesson.misses), board.withHeight (24), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! lesson.running || demo || ! board.contains (event.position)) return;
        elapsed = now() - started;
        int tapped = -1;
        for (int i = 0; i < static_cast<int> (PianoLesson::melody.size()); ++i)
            if (lesson.results[i] == 0 && tileBounds (i, elapsed).contains (event.position))
            { tapped = i; break; }
        const int earned = lesson.hit (tapped, elapsed);
        if (earned > 0)
        {
            const auto& note = PianoLesson::melody[tapped];
            sound (note.midi, note.duration);
            message = earned == 100 ? "Perfect! +100" : "Good! +70";
        }
        else
            message = "Wrong tile or timing. -25. Tap at the gold line.";
        repaint();
    }

private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    static constexpr float keyboardHeight = 56.0f;
    float noteCentre (int note) const
    {
        // Centres measured in white-key widths for MIDI C4 (60) through C5 (72).
        static constexpr float centres[13] {
            .5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.5f, 4.0f,
            4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.5f
        };
        return board.getX() + centres[juce::jlimit (0, 12, note - 60)] * board.getWidth() / 8.0f;
    }
    float hitY() const { return board.getBottom() - keyboardHeight - 28.0f; }
    juce::Rectangle<float> tileBounds (int index, double seconds) const
    {
        const auto& note = PianoLesson::melody[index];
        const float width = board.getWidth() / 8.0f;
        const float speed = (hitY() - board.getY()) / 2.0f;
        const float centre = hitY() - static_cast<float> (note.at - seconds) * speed;
        const float height = juce::jmin (48.0f, speed * 0.65f);
        const bool black = note.midi == 61 || note.midi == 63 || note.midi == 66
                        || note.midi == 68 || note.midi == 70;
        const float tileWidth = black ? width * 0.62f : width - 8.0f;
        return { noteCentre (note.midi) - tileWidth / 2, centre - height / 2, tileWidth, height };
    }
    void silence()
    {
        for (int note = 0; note < 128; ++note)
            if (offAt[note] > 0.0) keys.noteOff (1, note, 0.0f);
        offAt.fill (0.0);
    }
    void sound (int note, double duration)
    {
        if (offAt[note] > 0.0) keys.noteOff (1, note, 0.0f);
        keys.noteOn (1, note, 0.7f);
        offAt[note] = now() + duration;
    }
    void begin (bool preview)
    {
        silence();
        lesson.start();
        demo = preview;
        if (demo) lesson.running = false;
        demoIndex = 0;
        started = now();
        elapsed = 0.0;
        message = preview ? "Listen to the melody. This preview is not scored." : "Get ready! Tap the tiles at the gold line.";
        updateButtons();
        startTimerHz (60);
        repaint();
    }
    void updateButtons()
    {
        listen.setEnabled (! lesson.running && ! demo);
        start.setButtonText (lesson.running || lesson.complete || demo ? "Restart Game" : "Start Game");
    }
    void timerCallback() override
    {
        elapsed = now() - started;
        for (int note = 0; note < 128; ++note)
            if (offAt[note] > 0.0 && now() >= offAt[note])
            { keys.noteOff (1, note, 0.0f); offAt[note] = 0.0; }

        if (demo)
        {
            while (demoIndex < PianoLesson::melody.size() && elapsed >= PianoLesson::melody[demoIndex].at)
            {
                const auto& note = PianoLesson::melody[demoIndex++];
                // Skip obsolete notes after a delayed frame instead of playing a burst.
                if (elapsed < note.at + note.duration) sound (note.midi, note.at + note.duration - elapsed);
            }
            if (elapsed >= PianoLesson::melody.back().at + PianoLesson::melody.back().duration + .1)
            {
                demo = false;
                message = "Your turn! Press Start Game.";
                updateButtons();
            }
        }
        else if (lesson.running || lesson.complete)
        {
            lesson.advance (elapsed);
            if (lesson.complete)
            {
                message = lesson.passed() ? "Passed! Piano tutorial complete."
                                         : "Score " + juce::String (lesson.score()) + "/1400. Need 980. Press Restart Game.";
                updateButtons();
            }
        }
        if (! lesson.running && ! demo
            && std::all_of (offAt.begin(), offAt.end(), [] (double value) { return value == 0.0; }))
            stopTimer();
        repaint();
    }
    juce::MidiKeyboardState& keys;
    PianoLesson lesson;
    bool demo = false;
    double started = 0.0, elapsed = 0.0;
    size_t demoIndex = 0;
    std::array<double, 128> offAt {};
    const char* whiteNoteNames[8] { "C4", "D", "E", "F", "G", "A", "B", "C5" };
    const char* chromaticNames[13] { "C4", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C5" };
    juce::TextButton start { "Start Game" }, listen { "Listen" };
    juce::String message;
    juce::Rectangle<int> statusArea;
    juce::Rectangle<float> board;
};
