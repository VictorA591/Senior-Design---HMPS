#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "KeyboardInstrument.h"

class SoundPicker : public juce::Component
{
public:
    SoundPicker (const juce::StringArray& names, int selected, std::function<void(int)> apply)
        : onApply (std::move (apply))
    {
        title.setText ("Choose a sound", juce::dontSendNotification);
        title.setFont (juce::FontOptions (24));
        for (int i = 0; i < names.size(); ++i) sounds.addItem (names[i], i + 1);
        sounds.setSelectedId (selected, juce::dontSendNotification);
        credit.setText (juce::String::fromUTF8 (hmps::pianoCredit), juce::dontSendNotification);
        credit.setFont (juce::FontOptions (12));
        load.setButtonText ("Load");
        cancel.setButtonText ("Cancel");
        for (auto* component : std::initializer_list<juce::Component*> { &title, &sounds, &credit, &load, &cancel })
            addAndMakeVisible (*component);
        load.onClick = [this]
        {
            const int selection = sounds.getSelectedId();
            if (selection > 0) onApply (selection);
            dismiss();
        };
        cancel.onClick = [this] { dismiss(); };
        setSize (480, 250);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff18252f)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (20);
        title.setBounds (area.removeFromTop (38));
        area.removeFromTop (12);
        sounds.setBounds (area.removeFromTop (40));
        auto buttons = area.removeFromBottom (38);
        cancel.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (12);
        load.setBounds (buttons.removeFromRight (110));
        credit.setBounds (area.reduced (0, 8));
    }
private:
    void dismiss()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (0);
    }
    juce::Label title, credit;
    juce::ComboBox sounds;
    juce::TextButton load, cancel;
    std::function<void(int)> onApply;
};
