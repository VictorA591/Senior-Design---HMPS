#pragma once
#include <JuceHeader.h>

// Vector transport controls stay crisp on the Pi screen, without font-dependent symbols.
class TransportButton : public juce::TextButton
{
public:
    enum class Symbol { play, stop, record, loop };
    TransportButton (juce::String name, Symbol kind) : juce::TextButton (name), symbol (kind)
    {
        setTitle (name);
        setTooltip (name);
        setSize (44, 36);
    }
    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (3);
        const bool active = getToggleState();
        if (hover || down || active)
        {
            g.setColour (juce::Colour (0xff315b75).withAlpha (down ? 1.0f : .65f));
            g.fillRoundedRectangle (bounds, 6);
        }
        g.setColour ((symbol == Symbol::record ? juce::Colour (0xffff6b78)
                      : active ? juce::Colour (0xff50c9b0) : juce::Colours::white)
                         .withAlpha (isEnabled() ? 1.0f : .3f));
        auto box = bounds.withSizeKeepingCentre (22, 22);
        if (symbol == Symbol::record) g.fillEllipse (box.reduced (2));
        else if (symbol == Symbol::stop) g.fillRoundedRectangle (box.reduced (2), 2);
        else if (symbol == Symbol::play)
        {
            juce::Path path;
            path.addTriangle (box.getX() + 4, box.getY(), box.getRight(), box.getCentreY(),
                              box.getX() + 4, box.getBottom());
            g.fillPath (path);
        }
        else
        {
            juce::Path path;
            path.startNewSubPath (box.getX(), box.getCentreY());
            path.lineTo (box.getX(), box.getY() + 4);
            path.lineTo (box.getRight(), box.getY() + 4);
            path.startNewSubPath (box.getRight(), box.getCentreY());
            path.lineTo (box.getRight(), box.getBottom() - 4);
            path.lineTo (box.getX(), box.getBottom() - 4);
            g.strokePath (path, juce::PathStrokeType (2));
            path.clear();
            path.addTriangle (box.getRight(), box.getY() + 4, box.getRight() - 5, box.getY(),
                              box.getRight() - 5, box.getY() + 8);
            path.addTriangle (box.getX(), box.getBottom() - 4, box.getX() + 5, box.getBottom(),
                              box.getX() + 5, box.getBottom() - 8);
            g.fillPath (path);
        }
    }
private:
    Symbol symbol;
};
