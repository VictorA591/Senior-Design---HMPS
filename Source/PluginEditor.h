#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../TutorialGameComponent.h"

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
    enum class Screen { menu, files, sandbox, learning };
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
    juce::WildcardFileFilter musicFilter { "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi", "", "Music files" };
    juce::DirectoryContentsList musicFiles { &musicFilter, fileThread };
    juce::FileListComponent fileList { musicFiles };
    juce::File musicDirectory;
    juce::String storageError;
    juce::Label fileStatus;
    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton keyboardButton { "New Session" };
    juce::TextButton openButton { "Open" }, recordButton { "Record" }, stopButton { "Stop" };
    juce::TextButton playButton { "Play" }, saveButton { "Save Session" };
    juce::TextButton deleteButton { "Delete Session" };
    juce::TextEditor sessionName;
    bool unsaved = false;
    juce::String sessionMessage;
    void confirmDiscard (std::function<void()> action);
    void createSession();
    void openSelectedSession();
    void saveSession();
    void deleteSelectedSession();
    void updateTransportControls();
    EducationalDawEngine& processor;
    Screen screen = Screen::menu;
    juce::MidiKeyboardComponent keyboard;
    TutorialGameComponent tutorial { processor.keyboardState };
    juce::Label title, instructions, feedback, audioStatus;
    juce::TextButton sandboxButton { "Sandbox Mode" };
    juce::TextButton learningButton { "Learning Mode" };
    juce::TextButton backButton { "Main Menu" };
    juce::TextButton restartButton { "Try Lesson Again" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EducationalDawEditor)
};
