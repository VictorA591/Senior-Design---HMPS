#pragma once
#include <JuceHeader.h>

// A modal naming form keeps validation errors visible without losing typed text.
class SessionNamePanel : public juce::Component
{
public:
    explicit SessionNamePanel (std::function<juce::String(const juce::String&)> create)
        : onCreate (std::move (create))
    {
        title.setText ("Write Session Name", juce::dontSendNotification);
        title.setFont (juce::FontOptions (24));
        name.setTextToShowWhenEmpty ("My Awesome Beat", juce::Colours::grey);
        name.setInputRestrictions (80);
        error.setColour (juce::Label::textColourId, juce::Colour (0xffffb5b5));
        for (auto* c : std::initializer_list<juce::Component*> { &title, &name, &error, &createButton, &cancelButton })
            addAndMakeVisible (*c);
        createButton.onClick = [this]
        {
            const auto message = onCreate (name.getText());
            error.setText (message, juce::dontSendNotification);
            if (message.isEmpty()) dismiss();
        };
        cancelButton.onClick = [this] { dismiss(); };
        name.onReturnKey = [this] { createButton.onClick(); };
        name.onEscapeKey = [this] { dismiss(); };
        setSize (460, 240);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff18252f)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (20);
        title.setBounds (area.removeFromTop (40));
        name.setBounds (area.removeFromTop (44));
        error.setBounds (area.removeFromTop (52));
        auto row = area.removeFromBottom (44);
        cancelButton.setBounds (row.removeFromRight (120));
        row.removeFromRight (12);
        createButton.setBounds (row.removeFromRight (120));
    }
private:
    void dismiss() { if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0); }
    juce::Label title, error;
    juce::TextEditor name;
    juce::TextButton createButton { "Create" }, cancelButton { "Cancel" };
    std::function<juce::String(const juce::String&)> onCreate;
};
