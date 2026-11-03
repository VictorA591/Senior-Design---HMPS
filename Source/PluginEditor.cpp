#include "PluginEditor.h"
#include <algorithm>

EducationalDawEditor::EducationalDawEditor (EducationalDawEngine& p)
    : AudioProcessorEditor (&p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    for (auto* label : { &title, &instructions, &feedback, &audioStatus })
    {
        addAndMakeVisible (*label);
        label->setColour (juce::Label::textColourId, juce::Colours::white);
        label->setJustificationType (juce::Justification::centred);
    }
    title.setFont (juce::FontOptions (35.0f));
    instructions.setFont (juce::FontOptions (20.0f));
    feedback.setFont (juce::FontOptions (20.0f));
    audioStatus.setText (processor.audioStatus, juce::dontSendNotification);

    for (auto* button : { &sandboxButton, &learningButton, &backButton, &restartButton, &refreshButton, &keyboardButton, &openButton,
                          &recordButton, &stopButton, &playButton, &saveButton, &deleteButton })
    {
        addAndMakeVisible (*button);
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff315b75));
    }

    addAndMakeVisible (keyboard);
    addAndMakeVisible (tutorial);
    keyboard.setAvailableRange (60, 72);
    keyboard.setLowestVisibleKey (60);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setVelocity (0.7f, false);
    // This prototype uses pointer/touch input so typing shortcuts cannot leave hidden notes held.
    keyboard.clearKeyMappings();

    musicDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Music").getChildFile ("HMPS");
    addAndMakeVisible (fileList);
    addAndMakeVisible (fileStatus);
    fileStatus.setColour (juce::Label::textColourId, juce::Colours::white);
    fileStatus.setJustificationType (juce::Justification::centredLeft);
    fileList.setRowHeight (52);
    fileList.addListener (this);
    musicFiles.addChangeListener (this);
    fileThread.startThread();

    refreshButton.onClick = [this] { refreshFiles(); };
    keyboardButton.onClick = [this]
    {
        if (unsaved)
            showScreen (Screen::sandbox);
        else
            createSession();
    };
    openButton.onClick = [this] { openSelectedSession(); };
    deleteButton.onClick = [this] { deleteSelectedSession(); };
    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff843c43));
    addAndMakeVisible (sessionName);
    sessionName.setTextToShowWhenEmpty ("Session name", juce::Colours::grey);
    sessionName.setInputRestrictions (80);
    sessionName.setText ("Session " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
    recordButton.onClick = [this]
    {
        processor.keyboardState.allNotesOff (0);
        if (processor.startRecording())
        {
            unsaved = true;
            sessionMessage.clear();
        }
        else
            sessionMessage = "Cannot record: check audio output or create a new session.";
        updateTransportControls();
    };
    stopButton.onClick = [this]
    {
        processor.stopTransport();
        processor.keyboardState.allNotesOff (0);
        updateTransportControls();
    };
    playButton.onClick = [this]
    {
        processor.keyboardState.allNotesOff (0);
        sessionMessage = processor.startPlayback() ? "" : "Cannot play: check audio output.";
        updateTransportControls();
    };
    saveButton.onClick = [this] { saveSession(); };
    sandboxButton.onClick = [this] { showScreen (Screen::files); };
    learningButton.onClick = [this] { showScreen (Screen::learning); };
    backButton.onClick = [this]
    {
        showScreen (screen == Screen::sandbox ? Screen::files : Screen::menu);
    };
    restartButton.onClick = [this]
    {
        if (screen == Screen::sandbox)
            createSession();
        else
            showScreen (Screen::learning);
    };
    setSize (800, 480);
    showScreen (Screen::menu);
}

EducationalDawEditor::~EducationalDawEditor()
{
    stopTimer();
    fileList.removeListener (this);
    musicFiles.removeChangeListener (this);
    musicFiles.clear();
    fileThread.stopThread (-1);
    processor.stopTransport();
    processor.keyboardState.allNotesOff (0);
    processor.lessonState.store (EducationalDawEngine::LessonState::inactive);
}

void EducationalDawEditor::showScreen (Screen next)
{
    tutorial.deactivate();
    processor.keyboardState.allNotesOff (0);
    processor.stopTransport();
    screen = next;
    const auto isMenu = screen == Screen::menu;
    const auto isLearning = screen == Screen::learning;
    const auto isFiles = screen == Screen::files;
    const auto isSandbox = screen == Screen::sandbox;
    if (isSandbox)
        startTimerHz (30);
    else
        stopTimer();
    audioStatus.setVisible (isSandbox);
    feedback.setVisible (isSandbox);
    sandboxButton.setVisible (isMenu);
    learningButton.setVisible (isMenu);
    keyboard.setVisible (isSandbox);
    tutorial.setVisible (isLearning);
    if (isLearning) tutorial.reset();
    keyboard.setEnabled (true);
    fileList.setVisible (isFiles);
    fileStatus.setVisible (isFiles);
    refreshButton.setVisible (isFiles);
    keyboardButton.setVisible (isFiles);
    keyboardButton.setButtonText (unsaved ? "Resume Session" : "New Session");
    openButton.setVisible (isFiles);
    deleteButton.setVisible (isFiles);
    for (auto* button : { &recordButton, &stopButton, &playButton, &saveButton })
        button->setVisible (isSandbox);
    sessionName.setVisible (isSandbox);
    backButton.setButtonText (screen == Screen::sandbox ? "File Manager" : "Main Menu");
    backButton.setVisible (! isMenu);
    restartButton.setVisible (isSandbox);
    restartButton.setButtonText (isSandbox ? "New Take" : "Try Lesson Again");
    processor.lessonState.store (EducationalDawEngine::LessonState::inactive);
    title.setText (isMenu ? "HMPS" : (isLearning ? "Learning Mode" : "Sandbox: Keyboard"),
                   juce::dontSendNotification);
    instructions.setText (isMenu ? "What would you like to do?"
        : (isLearning ? "Piano Tiles: Twinkle, Twinkle, Little Star"
                      : "Record a take, stop, then play it back or save it."),
        juce::dontSendNotification);
    feedback.setText (isMenu ? ""
                            : "Press and hold a key to hear its sound.", juce::dontSendNotification);
    if (isFiles)
    {
        title.setText ("File Manager", juce::dontSendNotification);
        instructions.setText ("Select a MIDI session to open, or start a new session.", juce::dontSendNotification);
        refreshFiles();
    }
    timerCallback();
    resized();
}

void EducationalDawEditor::timerCallback()
{
    if (screen == Screen::sandbox)
    {
        updateTransportControls();
        return;
    }

}

void EducationalDawEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff18252f));
}

void EducationalDawEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    const bool isTutorial = screen == Screen::learning;
    title.setBounds (area.removeFromTop (isTutorial ? 36 : 48));
    instructions.setBounds (area.removeFromTop (isTutorial ? 40 : 64));
    audioStatus.setBounds (area.removeFromBottom (isTutorial ? 0 : 32));

    if (screen == Screen::menu)
    {
        constexpr int buttonHeight = 80;
        constexpr int buttonSpacing = 20;
        auto buttons = area.withSizeKeepingCentre (juce::jmin (420, area.getWidth()),
                                                    2 * buttonHeight + buttonSpacing);
        sandboxButton.setBounds (buttons.removeFromTop (buttonHeight));
        buttons.removeFromTop (buttonSpacing);
        learningButton.setBounds (buttons.removeFromTop (buttonHeight));
    }
    else if (screen == Screen::files)
    {
        auto navigation = area.removeFromBottom (48);
        const auto width = navigation.getWidth() / 4;
        backButton.setBounds (navigation.removeFromLeft (width).reduced (4, 0));
        refreshButton.setBounds (navigation.removeFromLeft (width).reduced (4, 0));
        openButton.setBounds (navigation.removeFromLeft (width).reduced (4, 0));
        keyboardButton.setBounds (navigation.reduced (4, 0));
        area.removeFromBottom (12);
        fileStatus.setBounds (area.removeFromBottom (48));
        auto fileActions = area.removeFromTop (44);
        deleteButton.setBounds (fileActions.removeFromRight (160));
        area.removeFromTop (8);
        fileList.setBounds (area);
    }
    else if (screen == Screen::learning)
    {
        auto navigation = area.removeFromBottom (44);
        backButton.setBounds (navigation.removeFromLeft (150));
        tutorial.setBounds (area);
    }
    else
    {
        auto navigation = area.removeFromBottom (48);
        backButton.setBounds (navigation.removeFromLeft (160));
        restartButton.setBounds (navigation.removeFromRight (180));
        area.removeFromBottom (16);
        if (screen == Screen::sandbox)
        {
            sessionName.setBounds (area.removeFromTop (32));
            area.removeFromTop (6);
            auto controls = area.removeFromTop (44);
            const auto width = controls.getWidth() / 4;
            for (auto* button : { &recordButton, &stopButton, &playButton, &saveButton })
                button->setBounds (controls.removeFromLeft (width).reduced (3, 0));
            feedback.setBounds (area.removeFromTop (36));
        }
        else
            feedback.setBounds (area.removeFromTop (64));
        keyboard.setBounds (area);
        keyboard.setKeyWidth (static_cast<float> (area.getWidth()) / 8.0f);
    }
}

void EducationalDawEditor::refreshFiles()
{
    fileList.deselectAllFiles();
    openButton.setEnabled (false);
    deleteButton.setEnabled (false);
    const auto result = musicDirectory.createDirectory();
    storageError = result.failed() ? "Cannot access music storage: " + result.getErrorMessage()
                                  : juce::String();
    if (result.failed())
    {
        musicFiles.clear();
        fileStatus.setText (storageError, juce::dontSendNotification);
        return;
    }

    fileStatus.setText ("Looking for saved music...", juce::dontSendNotification);
    musicFiles.setDirectory (musicDirectory, false, true);
    musicFiles.refresh();
}

void EducationalDawEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (screen != Screen::files)
        return;
    if (storageError.isNotEmpty())
        fileStatus.setText (storageError, juce::dontSendNotification);
    else if (musicFiles.isStillLoading())
        fileStatus.setText ("Looking for saved music...", juce::dontSendNotification);
    else if (musicFiles.getNumFiles() == 0)
        fileStatus.setText ("No saved music yet. Files appear here when added to Music/HMPS.",
                            juce::dontSendNotification);
    else
        selectionChanged();
}

void EducationalDawEditor::selectionChanged()
{
    if (screen != Screen::files)
        return;
    const auto file = fileList.getSelectedFile();
    openButton.setEnabled (file.existsAsFile() && file.hasFileExtension ("mid;midi"));
    deleteButton.setEnabled (file.existsAsFile() && file.hasFileExtension ("mid;midi")
                             && file.getParentDirectory() == musicDirectory);
    if (file.existsAsFile())
        fileStatus.setText (file.getFileName() + "  |  "
                            + juce::File::descriptionOfSizeInBytes (file.getSize()),
                            juce::dontSendNotification);
    else
        fileStatus.setText (juce::String (musicFiles.getNumFiles()) + " saved music files",
                            juce::dontSendNotification);
}

void EducationalDawEditor::updateTransportControls()
{
    const auto status = processor.getSessionStatus();
    const bool stopped = status.transport == EducationalDawEngine::Transport::stopped;
    recordButton.setEnabled (stopped && status.eventCount == 0);
    playButton.setEnabled (stopped && status.eventCount > 0);
    saveButton.setEnabled (stopped && status.eventCount > 0);
    stopButton.setEnabled (! stopped);
    sessionName.setEnabled (stopped);
    keyboard.setEnabled (status.transport != EducationalDawEngine::Transport::playing);
    juce::String text = sessionMessage;
    if (! stopped)
        text = (status.transport == EducationalDawEngine::Transport::recording ? "Recording: " : "Playing: ")
             + juce::String (status.seconds, 1) + " s";
    else if (status.full)
        text = "Recording limit reached. You can save this take.";
    else if (text.isEmpty())
        text = status.eventCount == 0 ? "Press Record to begin."
             : (unsaved ? "Unsaved take. Play it back or save your session." : "Session ready.");
    feedback.setText (text, juce::dontSendNotification);
}

void EducationalDawEditor::confirmDiscard (std::function<void()> action)
{
    if (! unsaved)
    {
        action();
        return;
    }
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Unsaved session", "Discard the current unsaved take?", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, action] (int result)
        {
            if (safe != nullptr && result == 1)
                action();
        }));
}

void EducationalDawEditor::createSession()
{
    confirmDiscard ([this]
    {
        processor.newSession();
        unsaved = false;
        sessionMessage.clear();
        sessionName.setText ("Session " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
        showScreen (Screen::sandbox);
    });
}

void EducationalDawEditor::saveSession()
{
    const auto events = processor.copySession();
    if (events.empty())
        return;
    const auto name = juce::File::createLegalFileName (sessionName.getText().trim()).substring (0, 80);
    if (name.isEmpty() || name == "." || name == "..")
    {
        sessionMessage = "Enter a session name before saving.";
        updateTransportControls();
        return;
    }
    if (musicDirectory.createDirectory().failed())
    {
        sessionMessage = "Cannot create the sessions folder.";
        updateTransportControls();
        return;
    }

    // MIDI uses ticks. 480 ticks/quarter at 120 BPM is 960 ticks/second.
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (500000));
    for (const auto& event : events)
    {
        auto message = event.on
            ? juce::MidiMessage::noteOn (event.channel, event.note, static_cast<juce::uint8> (event.velocity))
            : juce::MidiMessage::noteOff (event.channel, event.note);
        message.setTimeStamp (event.seconds * 960.0);
        track.addEvent (message);
    }
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (track);
    // Save a new version instead of overwriting an existing session.
    const auto target = musicDirectory.getNonexistentChildFile (name, ".mid", true);
    juce::TemporaryFile temporary (target);
    bool written = false;
    {
        juce::FileOutputStream stream (temporary.getFile());
        if (stream.openedOk())
        {
            written = midi.writeTo (stream);
            stream.flush();
            written = written && stream.getStatus().wasOk();
        }
    }
    if (written && temporary.overwriteTargetFileWithTemporary())
    {
        unsaved = false;
        sessionMessage = "Saved: " + target.getFileName();
        sessionName.setText (target.getFileNameWithoutExtension());
    }
    else
        sessionMessage = "Could not save. Check free space and folder permissions.";
    updateTransportControls();
}

void EducationalDawEditor::openSelectedSession()
{
    const auto file = fileList.getSelectedFile();
    if (! file.existsAsFile() || ! file.hasFileExtension ("mid;midi"))
    {
        fileStatus.setText ("Choose a MIDI session. Audio-file playback is not supported yet.",
                            juce::dontSendNotification);
        return;
    }
    if (file.getSize() > 4 * 1024 * 1024)
    {
        fileStatus.setText ("This MIDI file is too large (maximum 4 MB).", juce::dontSendNotification);
        return;
    }
    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream) || midi.getTimeFormat() == 0)
    {
        fileStatus.setText ("Could not read this MIDI session.", juce::dontSendNotification);
        return;
    }
    midi.convertTimestampTicksToSeconds();
    std::vector<EducationalDawEngine::NoteEvent> events;
    for (int index = 0; index < midi.getNumTracks(); ++index)
    {
        const auto* track = midi.getTrack (index);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& message = track->getEventPointer (i)->message;
            if (message.isNoteOnOrOff())
                events.push_back ({ message.getTimeStamp(), message.getChannel(),
                                    message.getNoteNumber(), message.getVelocity(), message.isNoteOn() });
        }
    }
    std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b)
    {
        return a.seconds < b.seconds;
    });
    if (events.empty() || events.size() > EducationalDawEngine::maxEvents + 2048)
    {
        fileStatus.setText ("Session has no notes or exceeds the recording capacity.", juce::dontSendNotification);
        return;
    }
    confirmDiscard ([this, events = std::move (events), file]
    {
        if (! processor.loadSession (events))
        {
            fileStatus.setText ("Invalid or unsupported MIDI note timing.", juce::dontSendNotification);
            return;
        }
        unsaved = false;
        sessionMessage = "Loaded: " + file.getFileName();
        sessionName.setText (file.getFileNameWithoutExtension());
        showScreen (Screen::sandbox);
    });
}

void EducationalDawEditor::deleteSelectedSession()
{
    const auto file = fileList.getSelectedFile();
    if (! file.existsAsFile() || ! file.hasFileExtension ("mid;midi")
        || file.getParentDirectory() != musicDirectory)
        return;

    // Capture the file, not the selection: the dialog confirms this exact session.
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
        "Delete saved session?",
        "Permanently delete \"" + file.getFileName() + "\" from storage? This cannot be undone.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, file] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            if (! file.existsAsFile())
            {
                safe->refreshFiles();
                return;
            }
            if (! file.deleteFile())
            {
                safe->fileStatus.setText ("Could not delete the session. Check folder permissions.",
                                          juce::dontSendNotification);
                return;
            }
            // Only the saved file is deleted; any in-memory take remains intact.
            safe->refreshFiles();
        }));
}
