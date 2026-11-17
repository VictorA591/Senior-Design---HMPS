#pragma once
#include "Source/PluginProcessor.h"
#include <algorithm>
#include <cmath>

// Message-thread project data. The audio engine receives a flattened snapshot for playback.
struct SessionArrangement
{
    using Event = EducationalDawEngine::NoteEvent;
    struct Track
    {
        juce::String name;
        bool drums = false;
        float volume = 1.0f;
        juce::String pluginXml, pluginState;
        juce::String sound = "piano";
        std::vector<Event> notes;
        std::array<bool, 32> pattern {};
    };
    std::vector<Track> tracks;
    int selected = -1;
    int bpm = 120;

    bool add (bool drums)
    {
        if (tracks.size() >= 3) return false;
        Track track;
        track.drums = drums;
        track.name = (drums ? "Step Sequencer " : "Keyboard ") + juce::String (tracks.size() + 1);
        tracks.push_back (std::move (track));
        selected = static_cast<int> (tracks.size()) - 1;
        return true;
    }
    Track* current()
    {
        return selected >= 0 && selected < static_cast<int> (tracks.size()) ? &tracks[selected] : nullptr;
    }
    static constexpr int drumNotes[4] { 36, 38, 42, 45 };
    std::vector<Event> eventsFor (int index) const
    {
        const auto& track = tracks[index];
        if (! track.drums)
        {
            auto events = track.notes;
            for (auto& event : events) event.channel = index + 1;
            return events;
        }
        std::vector<Event> events;
        // Eight eighth notes make one four-beat bar.
        for (int step = 0; step < 8; ++step)
            for (int row = 0; row < 4; ++row)
                if (track.pattern[row * 8 + step])
                {
                    const double time = step * 30.0 / bpm;
                    events.push_back ({ time, 10 + index, drumNotes[row], 100, true });
                    events.push_back ({ time + 0.08, 10 + index, drumNotes[row], 0, false });
                }
        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.seconds < b.seconds; });
        return events;
    }
    std::vector<Event> mix() const
    {
        std::vector<Event> events;
        for (int i = 0; i < static_cast<int> (tracks.size()); ++i)
        {
            auto part = eventsFor (i);
            events.insert (events.end(), part.begin(), part.end());
        }
        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.seconds < b.seconds; });
        return events;
    }
    double duration() const
    {
        const double bar = 240.0 / bpm;
        double end = bar;
        for (const auto& track : tracks)
            for (const auto& event : track.notes)
                end = juce::jmax (end, event.seconds + (event.on ? .001 : 0.0));
        return std::ceil (end / bar - 1.0e-9) * bar;
    }
    bool hasNotes() const
    {
        for (const auto& track : tracks)
            if (! track.notes.empty() || std::any_of (track.pattern.begin(), track.pattern.end(), [] (bool on) { return on; }))
                return true;
        return false;
    }
    void setTempo (int value)
    {
        value = juce::jlimit (60, 180, value);
        const double scale = static_cast<double> (bpm) / value;
        for (auto& track : tracks)
            for (auto& event : track.notes) event.seconds *= scale;
        bpm = value;
    }
    juce::var toJson() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 1);
        root->setProperty ("bpm", bpm);
        juce::Array<juce::var> list;
        for (const auto& track : tracks)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("name", track.name);
            object->setProperty ("drums", track.drums);
            object->setProperty ("volume", track.volume);
            object->setProperty ("sound", track.sound);
            object->setProperty ("plugin", track.pluginXml);
            object->setProperty ("pluginState", track.pluginState);
            juce::Array<juce::var> pattern, notes;
            for (bool on : track.pattern) pattern.add (on);
            for (const auto& event : track.notes)
            {
                juce::Array<juce::var> values;
                values.add (event.seconds); values.add (event.note);
                values.add (event.velocity); values.add (event.on);
                notes.add (juce::var (values));
            }
            object->setProperty ("pattern", juce::var (pattern));
            object->setProperty ("notes", juce::var (notes));
            list.add (juce::var (object));
        }
        root->setProperty ("tracks", juce::var (list));
        return juce::var (root);
    }
    static bool fromJson (const juce::var& value, SessionArrangement& result)
    {
        if (! value.isObject() || static_cast<int> (value["version"]) != 1) return false;
        auto* list = value["tracks"].getArray();
        const int tempo = value["bpm"];
        if (list == nullptr || list->size() > 3 || tempo < 60 || tempo > 180) return false;
        SessionArrangement candidate;
        candidate.bpm = tempo;
        int count = 0;
        for (const auto& item : *list)
        {
            if (! item.isObject() || ! item["drums"].isBool()) return false;
            candidate.add (static_cast<bool> (item["drums"]));
            auto& track = candidate.tracks.back();
            if (item.hasProperty ("volume"))
            {
                double volume = item["volume"];
                if (! std::isfinite (volume) || volume < 0 || volume > 1) return false;
                track.volume = static_cast<float> (volume);
            }
            if (item.hasProperty ("sound"))
            {
                track.sound = item["sound"].toString();
                if (track.sound != "piano" && track.sound != "synthLead") return false;
            }
            track.pluginXml = item["plugin"].toString();
            track.pluginState = item["pluginState"].toString();
            if (track.pluginXml.length() > 65536 || track.pluginState.length() > 16 * 1024 * 1024) return false;
            if (track.pluginXml.isNotEmpty())
            {
                auto xml = juce::XmlDocument::parse (track.pluginXml);
                juce::PluginDescription description;
                juce::MemoryBlock state;
                if (xml == nullptr || ! description.loadFromXml (*xml) || description.pluginFormatName != "VST3"
                    || ! EducationalDawEngine::decodeInstrumentState (track.pluginState, state)) return false;
            }
            track.name = item["name"].toString().substring (0, 80);
            auto* pattern = item["pattern"].getArray();
            auto* notes = item["notes"].getArray();
            if (pattern == nullptr || pattern->size() != 32 || notes == nullptr) return false;
            for (int i = 0; i < 32; ++i)
            {
                if (! (*pattern)[i].isBool()) return false;
                track.pattern[i] = (*pattern)[i];
            }
            double previous = 0;
            for (const auto& note : *notes)
            {
                auto* fields = note.getArray();
                if (fields == nullptr || fields->size() != 4 || ++count > 3 * (EducationalDawEngine::maxEvents + 2048)) return false;
                Event event { static_cast<double> ((*fields)[0]), 1, static_cast<int> ((*fields)[1]),
                              static_cast<int> ((*fields)[2]), static_cast<bool> ((*fields)[3]) };
                if (! std::isfinite (event.seconds) || event.seconds < previous || event.seconds > 86400
                    || event.note < 0 || event.note > 127 || event.velocity < 0 || event.velocity > 127) return false;
                previous = event.seconds;
                track.notes.push_back (event);
            }
        }
        candidate.selected = candidate.tracks.empty() ? -1 : 0;
        result = std::move (candidate);
        return true;
    }
};

class StepGrid : public juce::Component
{
public:
    std::array<bool, 32> pattern {}, target {};
    bool hints = false;
    int activeStep = -1;
    std::function<void()> onChange;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff18252f));
        const char* names[] { "Kick", "Snare", "Hi-hat", "Tom" };
        g.setFont (juce::FontOptions (14.0f));
        for (int i = 0; i < 32; ++i)
        {
            auto rect = cell (i);
            if (i % 8 == 0)
            {
                g.setColour (juce::Colours::white);
                g.drawText (names[i / 8], 0, rect.getY(), 72, rect.getHeight(), juce::Justification::centredLeft);
            }
            g.setColour (pattern[i] ? juce::Colour (0xff50c9b0) : juce::Colour (0xff315b75));
            g.fillRoundedRectangle (rect.toFloat().reduced (2), 4);
            g.setColour (pattern[i] ? juce::Colours::black : juce::Colours::white);
            g.drawText (juce::String (i % 8 + 1), rect, juce::Justification::centred);
            if (i % 8 == activeStep)
            {
                g.setColour (juce::Colours::yellow);
                g.drawRect (rect.reduced (2), 2);
            }
            if (hints && target[i])
            {
                g.setColour (juce::Colour (0xffffcc66));
                g.fillEllipse (static_cast<float> (rect.getRight() - 10), static_cast<float> (rect.getY() + 4), 6, 6);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! isEnabled()) return;
        for (int i = 0; i < 32; ++i)
            if (cell (i).contains (event.getPosition()))
            {
                pattern[i] = ! pattern[i];
                if (onChange) onChange();
                repaint();
                break;
            }
    }
private:
    juce::Rectangle<int> cell (int i) const
    {
        const float width = (getWidth() - 76.0f) / 8;
        const float height = getHeight() / 4.0f;
        return juce::Rectangle<float> (76 + (i % 8) * width, (i / 8) * height, width, height).toNearestInt();
    }
};

class ArrangerView : public juce::Component
{
public:
    explicit ArrangerView (SessionArrangement& data) : project (data)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& slider = volumes[i];
            addChildComponent (slider);
            slider.setRange (0, 100, 1);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 20);
            slider.setTextValueSuffix ("%");
            slider.setTitle ("Track " + juce::String (i + 1) + " volume");
            slider.setTooltip ("Track volume: 0% mutes this track");
            slider.onValueChange = [this, i]
            {
                if (i >= static_cast<int> (project.tracks.size())) return;
                project.tracks[i].volume = static_cast<float> (volumes[i].getValue() / 100);
                if (onVolumeChange) onVolumeChange();
            };
        }
    }
    void resized() override
    {
        for (int i = 0; i < 3; ++i)
            volumes[i].setBounds (lane (i).withWidth (leftWidth - 8).withTrimmedTop (15).reduced (4, 0));
    }
    bool selectionEnabled = true;
    std::function<void()> onVolumeChange;
    std::function<void()> onSelect;
    struct Clip { double start, end; int note; };
    std::array<std::vector<Clip>, 3> clips;
    double contentEnd = 0;
    void refresh()
    {
        for (int i = 0; i < 3; ++i)
        {
            volumes[i].setVisible (i < static_cast<int> (project.tracks.size()));
            if (i < static_cast<int> (project.tracks.size()))
                volumes[i].setValue (project.tracks[i].volume * 100, juce::dontSendNotification);
        }
        resized();
        for (auto& lane : clips) lane.clear();
        contentEnd = 0;
        for (int i = 0; i < static_cast<int> (project.tracks.size()); ++i)
        {
            std::array<std::vector<size_t>, 128> held;
            for (const auto& event : project.eventsFor (i))
            {
                contentEnd = juce::jmax (contentEnd, event.seconds + .2);
                if (event.on)
                {
                    held[event.note].push_back (clips[i].size());
                    clips[i].push_back ({ event.seconds, event.seconds + .1, event.note });
                }
                else if (! held[event.note].empty())
                {
                    auto index = held[event.note].front();
                    held[event.note].erase (held[event.note].begin());
                    clips[i][index].end = event.seconds;
                }
            }
        }
        repaint();
    }
    double position = 0;
    bool moving = false;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101c26));
        const float left = leftWidth;
        const float width = juce::jmax (1.0f, getWidth() - left);
        double duration = juce::jmax (4.0 * 60.0 / project.bpm, position + 0.5);
        duration = juce::jmax (duration, contentEnd);
        const double beats = std::ceil (duration * project.bpm / 60.0 / 4.0) * 4;
        duration = beats * 60.0 / project.bpm;
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (project.tracks.size()) + "/3 tracks", 8, 0, 140, 24, juce::Justification::centredLeft);
        const int stride = juce::jmax (1, static_cast<int> (std::ceil (beats / 12)));
        for (int beat = 0; beat < static_cast<int> (beats); beat += stride)
        {
            float x = left + width * static_cast<float> (beat / beats);
            g.setColour (juce::Colour (0xff315b75));
            g.drawVerticalLine (static_cast<int> (x), 24, static_cast<float> (getHeight()));
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (beat / 4 + 1) + "." + juce::String (beat % 4 + 1),
                        static_cast<int> (x) + 3, 0, 48, 24, juce::Justification::centredLeft);
        }
        for (int i = 0; i < static_cast<int> (project.tracks.size()); ++i)
        {
            auto row = lane (i);
            g.setColour (i == project.selected ? juce::Colour (0xff315b75) : juce::Colour (0xff223744));
            g.fillRect (row.withWidth (leftWidth - 2).reduced (1));
            g.setColour (juce::Colours::white);
            g.drawFittedText (project.tracks[i].name, row.withWidth (leftWidth - 2).withHeight (15).reduced (6, 0), juce::Justification::centredLeft, 1);
            for (const auto& clip : clips[i])
            {
                float x = left + width * static_cast<float> (clip.start / duration);
                float length = juce::jmax (3.0f, width * static_cast<float> ((clip.end - clip.start) / duration));
                float y = row.getY() + 5.0f + (127 - clip.note) / 127.0f * juce::jmax (1, row.getHeight() - 14);
                g.setColour (project.tracks[i].drums ? juce::Colour (0xffffcc66) : juce::Colour (0xff50c9b0));
                g.fillRoundedRectangle (x, y, length, 5, 2);
            }
            g.setColour (juce::Colour (0xff315b75));
            g.drawHorizontalLine (row.getBottom() - 1, left, static_cast<float> (getWidth()));
        }
        if (project.tracks.empty())
        {
            g.setColour (juce::Colours::white);
            g.drawText ("Add a keyboard or drum track to begin.", getLocalBounds().withTrimmedTop (24), juce::Justification::centred);
        }
        if (moving)
        {
            g.setColour (juce::Colours::yellow);
            g.drawVerticalLine (static_cast<int> (left + width * position / duration), 24, static_cast<float> (getHeight()));
        }
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! isEnabled() || ! selectionEnabled) return;
        for (int i = 0; i < static_cast<int> (project.tracks.size()); ++i)
            if (lane (i).contains (event.getPosition()))
            {
                project.selected = i;
                if (onSelect) onSelect();
                repaint();
            }
    }
private:
    juce::Rectangle<int> lane (int i) const
    {
        int height = juce::jmax (1, (getHeight() - 24) / 3);
        return { 0, 24 + i * height, getWidth(), height };
    }
    static constexpr int leftWidth = 210;
    std::array<juce::Slider, 3> volumes;
    SessionArrangement& project;
};
