#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../TutorialGameComponent.h"
#include "../SessionArrangement.h"
#include "../TransportButton.h"
#include "../PluginSupport.h"

class SessionListStyle : public juce::LookAndFeel_V4
{
public:
    void drawFileBrowserRow (juce::Graphics& g, int width, int height,
        const juce::File& file, const juce::String&, juce::Image*,
        const juce::String&, const juce::String&, bool, bool selected, int,
        juce::DirectoryContentsDisplayComponent&) override
    {
        g.setColour (juce::Colour (selected ? 0xff315b75 : 0xff22343f));
        g.fillRoundedRectangle (4, 3, static_cast<float> (width - 8), static_cast<float> (height - 6), 6);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (20));
        g.drawFittedText (file.getFileNameWithoutExtension(), 16, 0, width - 32, height,
                         juce::Justification::centredLeft, 1);
    }
};

class TouchMenuStyle : public juce::LookAndFeel_V4
{
public:
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (
        juce::ComboBox& box, juce::Label& label) override
    {
        auto options =
            juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);

#if JUCE_LINUX
        // Keep the popup in the existing application window.
        options = options.withParentComponent (box.getTopLevelComponent());
#endif

        return options.withStandardItemHeight (44);
    }
};

class EducationalDawEditor : public juce::AudioProcessorEditor, private juce::Timer,
                             private juce::FileBrowserListener,
                             private juce::ChangeListener
{
    
public:
    explicit EducationalDawEditor (EducationalDawEngine&);
    ~EducationalDawEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TouchMenuStyle touchMenuStyle;
    enum class Screen { menu, files, sandbox, learningMenu, learning };
    void showScreen (Screen);
    void timerCallback() override;
    void refreshFiles();
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File&) override { openSelectedSession(); }
    void browserRootChanged (const juce::File&) override {}
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    // Order matters: the list must be destroyed before its filter and worker.
    juce::TimeSliceThread fileThread { "HMPS file scanning" };
    juce::WildcardFileFilter musicFilter { "*.hmps;*.mid;*.midi", "", "Music files" };
    juce::DirectoryContentsList musicFiles { &musicFilter, fileThread };
    SessionListStyle sessionListStyle;
    juce::FileListComponent fileList { musicFiles };
    juce::File musicDirectory;
    juce::String storageError;
    juce::Label fileStatus;
    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton keyboardButton { "New Session" };
    juce::TextButton openButton { "Load Session" }, saveButton { "Save Session" };
    TransportButton recordButton { "Record", TransportButton::Symbol::record },
                    stopButton { "Stop", TransportButton::Symbol::stop },
                    playButton { "Play", TransportButton::Symbol::play },
                    loopButton { "Loop playback", TransportButton::Symbol::loop };
    juce::TooltipWindow tooltips { this, 500 };
    void updateTrackLevels();
    void refreshMidiInputs();
    juce::ComboBox midiInputChoice;
    juce::Array<juce::MidiDeviceInfo> midiInputs;
    void refreshMidiOutputs();
    juce::ComboBox midiOutputChoice;
    juce::Array<juce::MidiDeviceInfo> midiOutputs;
    int midiRefreshTicks = 0;
    void refreshInstrumentChoices();
    void showSoundChooser();
    juce::TextButton loadSoundButton { "Load Sound" };
    void scanInstruments (const juce::File& extraFolder = {});
    void loadInstrument (int track, juce::PluginDescription, juce::String state, bool restoring);
    void restoreInstruments (int track = 0);
    void captureInstrumentStates();
    void resetInstruments();
    juce::ComboBox instrumentChoice;
    juce::TextButton scanButton { "Scan VST3" }, folderButton { "Add Folder" }, pluginEditorButton { "Sound Controls" };
    std::shared_ptr<juce::AudioPluginFormatManager> pluginFormats = std::make_shared<juce::AudioPluginFormatManager>();
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<InstrumentScan> pluginScan;
    std::unique_ptr<InstrumentWindow> pluginWindow;
    std::unique_ptr<juce::FileChooser> pluginFolderChooser;
    juce::File pluginCache;
    juce::StringArray pluginFailures;
    bool pluginBusy = false;
    int pluginGeneration = 0;
    juce::TextButton deleteButton { "Delete Session" };
    juce::TextEditor sessionName;
    SessionArrangement arrangement;
    ArrangerView arranger { arrangement };
    StepGrid stepGrid;
    juce::TextButton addKeyboard { "+ Keyboard" }, addDrums { "+ Drum Track" };
    juce::Slider tempo;
    bool capturing = false;
    void finishCapture();
    void refreshArrangement();
    bool unsaved = false;
    juce::String sessionMessage;
    void confirmDiscard (std::function<void()> action);
    void createSession();
    juce::String createNamedSession (const juce::String&);
    void chooseTrack();
    void updateGuide();
    void saveTutorialProgress();
    void patternDialog (bool saving);
    bool workspace() const { return screen == Screen::sandbox || screen == Screen::learning; }
    juce::TextButton addTrackButton { "Add Track" }, guideNext { "Next" };
    juce::Label sessionBoxTitle, guideText;
    juce::TextButton savePattern { "Save Pattern" }, loadPattern { "Load Pattern" }, previewPattern { "Preview Pattern" };
    bool previewingPattern = false;
    int guideStep = 0;
    SessionArrangement sandboxBackup;
    juce::String sandboxName;
    bool sandboxUnsaved = false;
    juce::File pendingSessionSelection;
    juce::File currentSessionFile;
    void openSelectedSession();
    bool saveSession();
    void deleteSelectedSession();
    void updateTransportControls();
    EducationalDawEngine& processor;
    Screen screen = Screen::menu;
    juce::MidiKeyboardComponent keyboard;
    TutorialGameComponent tutorial { processor.keyboardState };
    juce::Label title, instructions, feedback, audioStatus;
    juce::TextButton sandboxButton { "Sandbox" };
    juce::TextButton learningButton { "Tutorial" };
    juce::TextButton beginTutorial { "Begin New Tutorial" }, resumeButton { "Resume Tutorial" };
    bool resumeTutorial = false;
    juce::TextButton backButton { "Main Menu" };
    juce::TextButton restartButton { "Try Lesson Again" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EducationalDawEditor)
};
