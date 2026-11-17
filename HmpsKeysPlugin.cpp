#include "KeyboardInstrument.h"

class HmpsKeysProcessor : public juce::AudioProcessor
{
public:
    HmpsKeysProcessor() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
        parameters (*this, nullptr, "HMPSKeys", createLayout())
    {
        sound = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter ("sound"));
        for (auto& gain : gains) gain.store (1);
        for (auto& choice : choices) choice.store (0);
        for (int i = 0; i < 16; ++i) synth.addVoice (new hmps::KeyboardVoice (gains, choices));
        synth.addSound (new hmps::KeyboardSound (true));
        synth.setNoteStealingEnabled (true);
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "sound", 1 }, "Sound", juce::StringArray { "Piano", "Synth Lead" }, 0));
        return layout;
    }
    const juce::String getName() const override { return "HMPS Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return .2; }
    int getNumPrograms() override { return 2; }
    int getCurrentProgram() override { return sound->getIndex(); }
    void setCurrentProgram (int index) override
    {
        sound->beginChangeGesture();
        sound->setValueNotifyingHost (static_cast<float> (juce::jlimit (0, 1, index)));
        sound->endChangeGesture();
    }
    const juce::String getProgramName (int index) override { return index == 1 ? "Synth Lead" : "Piano"; }
    void changeProgramName (int, const juce::String&) override {}
    void prepareToPlay (double rate, int) override { synth.setCurrentPlaybackSampleRate (rate); generatedMidi.ensureSize (4096); }
    void releaseResources() override { synth.allNotesOff (0, false); keys.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet().isDisabled()
            && (layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
                || layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
    }
    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        audio.clear();
        const int selected = sound->getIndex();
        if (selected != previous)
        {
            synth.allNotesOff (0, false);
            previous = selected;
        }
        for (auto& choice : choices) choice.store (selected);
        // Drain pointer-generated notes separately so host input is not echoed back.
        generatedMidi.clear();
        keys.processNextMidiBuffer (generatedMidi, 0, audio.getNumSamples(), true);
        keys.processNextMidiBuffer (midi, 0, audio.getNumSamples(), false);
        midi.addEvents (generatedMidi, 0, audio.getNumSamples(), 0);
        synth.renderNextBlock (audio, midi, 0, audio.getNumSamples());
        midi.swapWith (generatedMidi);
    }
    void getStateInformation (juce::MemoryBlock& state) override
    {
        if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, state);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size); xml && xml->hasTagName ("HMPSKeys"))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }
    juce::MidiKeyboardState keys;
    juce::AudioProcessorValueTreeState parameters;
private:
    juce::AudioParameterChoice* sound = nullptr;
    std::array<std::atomic<float>, 16> gains;
    std::array<std::atomic<int>, 16> choices;
    juce::Synthesiser synth;
    juce::MidiBuffer generatedMidi;
    int previous = 0;
};

class HmpsKeysEditor : public juce::AudioProcessorEditor
{
public:
    explicit HmpsKeysEditor (HmpsKeysProcessor& processor)
        : AudioProcessorEditor (processor), keyboard (processor.keys, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        sounds.addItem ("Piano", 1);
        sounds.addItem ("Synth Lead", 2);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, "sound", sounds);
        title.setText ("HMPS Keys", juce::dontSendNotification);
        title.setFont (juce::FontOptions (24));
        credit.setText (juce::String::fromUTF8 (hmps::pianoCredit), juce::dontSendNotification);
        credit.setFont (juce::FontOptions (11));
        for (auto* component : std::initializer_list<juce::Component*> { &title, &sounds, &keyboard, &credit })
            addAndMakeVisible (*component);
        keyboard.setAvailableRange (60, 72);
        keyboard.setLowestVisibleKey (60);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setOctaveForMiddleC (4);
        keyboard.clearKeyMappings();
        setResizable (true, true);
        setResizeLimits (480, 240, 1920, 1200);
        setSize (760, 380);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff18252f)); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto top = area.removeFromTop (40);
        title.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth() / 2)));
        sounds.setBounds (top);
        credit.setBounds (area.removeFromBottom (48));
        keyboard.setBounds (area.reduced (0, 8));
        keyboard.setKeyWidth (keyboard.getWidth() / 8.0f);
    }
private:
    juce::Label title, credit;
    juce::ComboBox sounds;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};
juce::AudioProcessorEditor* HmpsKeysProcessor::createEditor() { return new HmpsKeysEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HmpsKeysProcessor(); }
