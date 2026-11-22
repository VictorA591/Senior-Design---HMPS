#include "PluginEditor.h"
#include "../SoundPicker.h"
#include "../SessionNamePanel.h"
#include <algorithm>
#include <iostream>

void TouchMenuRow::mouseDown (const juce::MouseEvent&)
{
    owner.chooseTouchItem (itemId);
}

TouchMenuPopup::TouchMenuPopup (TouchComboBox& owner)
{
    setAlwaysOnTop (true);

    for (int i = 0; i < owner.getNumItems(); ++i)
    {
        auto* row = rows.add (new TouchMenuRow (owner, owner.getItemId (i), owner.getItemText (i)));
        addAndMakeVisible (*row);
    }

    setSize (owner.getWidth(), juce::jmax (44, rows.size() * 44));
}

void TouchComboBox::showTouchPopup()
{
    if (touchPopup != nullptr)
    {
        hideTouchPopup();
        return;
    }

    auto* topLevel = getTopLevelComponent();
    if (topLevel == nullptr)
        return;

    touchPopup = std::make_unique<TouchMenuPopup> (*this);
    topLevel->addAndMakeVisible (*touchPopup);

    const int width = juce::jlimit (180, topLevel->getWidth() - 8, juce::jmax (getWidth(), 180));
    const int height = juce::jlimit (44,
                                     juce::jmax (44, topLevel->getHeight() - 8),
                                     touchPopup->getHeight());
    const auto topLevelBounds = topLevel->getLocalBounds();
    const auto comboBounds = topLevel->getLocalArea (nullptr, getScreenBounds());
    int x = juce::jlimit (topLevelBounds.getX() + 4,
                          topLevelBounds.getRight() - width - 4,
                          comboBounds.getX());
    int y = comboBounds.getBottom();
    if (y + height > topLevelBounds.getBottom() - 4)
        y = juce::jmax (topLevelBounds.getY() + 4, comboBounds.getY() - height);

    touchPopup->setBounds (x, y, width, height);
    touchPopup->toFront (false);

    for (int i = 0; i < touchPopup->rows.size(); ++i)
        touchPopup->rows[i]->setBounds (0, i * 44, width, 44);
}

void TouchComboBox::hideTouchPopup()
{
    touchPopup.reset();
}

void TouchComboBox::chooseTouchItem (int itemId)
{
    setSelectedId (itemId, juce::sendNotification);
    hideTouchPopup();
}

EducationalDawEditor::EducationalDawEditor (EducationalDawEngine& p)
    : AudioProcessorEditor (&p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&touchMenuStyle);

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

    for (auto* button : std::initializer_list<juce::TextButton*> { &sandboxButton, &learningButton, &backButton, &restartButton, &refreshButton, &keyboardButton, &openButton,
                          &recordButton, &stopButton, &playButton, &loopButton, &saveButton, &deleteButton })
    {
        addAndMakeVisible (*button);
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff315b75));
    }

    addAndMakeVisible (midiInputChoice);
    //midiInputChoice.setTooltip ("Choose your ESP32-S3 USB MIDI device or MIDI keyboard");
    midiInputChoice.onChange = [this]
    {
        std::cerr << "[MIDI INPUT] selection changed: id="
                  << midiInputChoice.getSelectedId()
                  << " text="
                  << midiInputChoice.getText().toStdString()
                  << std::endl;

        const int index = midiInputChoice.getSelectedId() - 2;
        processor.selectMidiInput (juce::isPositiveAndBelow (index, midiInputs.size())
            ? midiInputs[index].identifier : juce::String());
    };
    refreshMidiInputs();
    addAndMakeVisible (midiOutputChoice);

    //midiOutputChoice.setTooltip ("Send keyboard MIDI to the ESP32-S3 USB MIDI device");

    midiOutputChoice.onChange = [this]
    {
        std::cerr << "[MIDI OUTPUT] selection changed: id="
                  << midiOutputChoice.getSelectedId()
                  << " text="
                  << midiOutputChoice.getText().toStdString()
                  << std::endl;

        const int index = midiOutputChoice.getSelectedId() - 2;

        const auto identifier =
            juce::isPositiveAndBelow (index, midiOutputs.size())
                ? midiOutputs[index].identifier
                : juce::String();

        if (! processor.selectMidiOutput (identifier))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "MIDI output unavailable",
                "The selected MIDI output could not be opened.");
        }

        refreshMidiOutputs();
    };

    refreshMidiOutputs();
    addAndMakeVisible (keyboard);
    addAndMakeVisible (tutorial);
    for (auto* component : std::initializer_list<juce::Component*> { &arranger, &stepGrid, &addKeyboard, &addDrums, &tempo })
        addAndMakeVisible (*component);
    for (auto* c : std::initializer_list<juce::Component*> { &addTrackButton, &guideText, &guideNext, &sessionBoxTitle, &savePattern, &loadPattern, &previewPattern })
        addAndMakeVisible (*c);
    sessionBoxTitle.setText ("Session Name", juce::dontSendNotification);
    sessionBoxTitle.setFont (juce::FontOptions (20));
    sessionBoxTitle.setColour (juce::Label::textColourId, juce::Colours::white);
    guideText.setColour (juce::Label::backgroundColourId, juce::Colour (0xff294455));
    guideText.setColour (juce::Label::textColourId, juce::Colours::white);
    guideText.setFont (juce::FontOptions (16));
    addTrackButton.onClick = [this] { chooseTrack(); };
    guideNext.onClick = [this]
    {
        if (guideStep == 1) { guideStep = 2; tutorial.reset(); }
        else if (guideStep == 5) { showScreen (Screen::menu); return; }
        updateGuide(); refreshArrangement(); resized();
    };
    savePattern.onClick = [this] { patternDialog (true); };
    loadPattern.onClick = [this] { patternDialog (false); };
    previewPattern.onClick = [this]
    {
        if (previewingPattern) { stopButton.onClick(); previewingPattern = false; }
        else if (arrangement.current() != nullptr)
        {
            processor.stopTransport();
            previewingPattern = processor.loadSession (arrangement.eventsFor (arrangement.selected))
                && processor.startPlayback (240.0 / arrangement.bpm);
        }
        updateTransportControls();
    };
    pluginFormats->addFormat (std::make_unique<juce::VST3PluginFormat>());
    for (auto* component : std::initializer_list<juce::Component*> { &instrumentChoice, &loadSoundButton, &scanButton, &folderButton, &pluginEditorButton })
        addAndMakeVisible (*component);
    instrumentChoice.setTooltip ("Instrument for the selected track");
    instrumentChoice.onChange = [this]
    {
        if (pluginBusy || pluginScan || arrangement.current() == nullptr) return;
        const int selected = arrangement.selected;
        const int choice = instrumentChoice.getSelectedId();
        if (choice == 1 || choice == 2)
        {
            pluginWindow.reset();
            processor.removeInstrument (selected);
            arrangement.tracks[selected].pluginXml.clear();
            arrangement.tracks[selected].pluginState.clear();
            arrangement.tracks[selected].sound = choice == 2 ? "synthLead" : "piano";
            unsaved = true;
            sessionMessage = choice == 2 ? "Loaded Synth Lead." : "Loaded Piano.";
            refreshArrangement();
        }
        else if (choice >= 3 && choice - 3 < knownPlugins.getTypes().size())
            loadInstrument (selected, knownPlugins.getTypes()[choice - 3], {}, false);
    };
    loadSoundButton.onClick = [this] { showSoundChooser(); };
    scanButton.onClick = [this]
    {
        if (pluginScan) pluginScan->signalThreadShouldExit();
        else scanInstruments();
    };
    folderButton.onClick = [this]
    {
        pluginFolderChooser = std::make_unique<juce::FileChooser> ("Choose a folder containing VST3 instruments");
        juce::Component::SafePointer<EducationalDawEditor> safe (this);
        pluginFolderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe] (const juce::FileChooser& chooser)
            {
                if (safe != nullptr && chooser.getResult().isDirectory()) safe->scanInstruments (chooser.getResult());
            });
    };
    pluginEditorButton.onClick = [this]
    {
        if (auto* plugin = processor.instrument (arrangement.selected))
        {
            pluginWindow.reset();
            pluginWindow = std::make_unique<InstrumentWindow> (*plugin);
            unsaved = true;
        }
    };
    tempo.setRange (60, 180, 1);
    tempo.setValue (120, juce::dontSendNotification);
    tempo.setSliderStyle (juce::Slider::IncDecButtons);
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 85, 30);
    tempo.setTextValueSuffix (" BPM");
    tempo.onValueChange = [this]
    {
        arrangement.setTempo (static_cast<int> (tempo.getValue()));
        updateTrackLevels();
        unsaved = true;
        arranger.refresh();
    };
    auto addTrack = [this] (bool drums)
    {
        if (arrangement.add (drums))
        {
            unsaved = true; sessionMessage.clear();
            if (screen == Screen::learning)
            {
                if (guideStep == 0 && ! drums) guideStep = 1;
                else if (guideStep == 3 && drums) guideStep = 4;
            }
            refreshArrangement();
        }
    };
    addKeyboard.onClick = [addTrack] { addTrack (false); };
    addDrums.onClick = [addTrack] { addTrack (true); };
    arranger.onSelect = [this] { pluginWindow.reset(); refreshArrangement(); };
    arranger.onVolumeChange = [this] { unsaved = true; updateTrackLevels(); };
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { processor.setLooping (loopButton.getToggleState()); };
    stepGrid.onChange = [this]
    {
        if (auto* track = arrangement.current()) track->pattern = stepGrid.pattern;
        unsaved = true;
        arranger.refresh();
    };
    sessionName.onTextChange = [this] { if (screen == Screen::sandbox) unsaved = true; };
    keyboard.setAvailableRange (60, 72);
    keyboard.setLowestVisibleKey (60);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setVelocity (0.7f, false);
    // This prototype uses pointer/touch input so typing shortcuts cannot leave hidden notes held.
    keyboard.clearKeyMappings();

    musicDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Music").getChildFile ("HMPS");
    musicDirectory.createDirectory();
    pluginCache = musicDirectory.getChildFile ("vst3-cache.xml");
    if (auto xml = juce::XmlDocument::parse (pluginCache)) knownPlugins.recreateFromXml (*xml);
    addAndMakeVisible (fileList);
    addAndMakeVisible (fileStatus);
    fileStatus.setColour (juce::Label::textColourId, juce::Colours::white);
    fileStatus.setJustificationType (juce::Justification::centredLeft);
    fileList.setLookAndFeel (&sessionListStyle);
    fileList.setRowHeight (52);
    fileList.addListener (this);
    musicFiles.addChangeListener (this);
    fileThread.startThread();

    refreshButton.onClick = [this] { refreshFiles(); };
    keyboardButton.onClick = [this] { createSession(); };
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
        auto* track = arrangement.current();
        if (track == nullptr || track->drums || ! track->notes.empty()) return;
        processor.newSession();
        auto accompaniment = arrangement;
        accompaniment.tracks[arrangement.selected].notes.clear();
        auto backing = accompaniment.mix();
        updateTrackLevels();
        if (processor.startRecording (backing))
        {
            capturing = true;
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
        finishCapture();
        updateTransportControls();
    };
    playButton.onClick = [this]
    {
        processor.keyboardState.allNotesOff (0);
        sessionMessage = processor.loadSession (arrangement.mix()) && processor.startPlayback (arrangement.duration())
            ? "" : "Add notes or a drum pattern before playing.";
        updateTransportControls();
    };
    saveButton.onClick = [this] { saveSession(); };
    sandboxButton.onClick = [this] { showScreen (Screen::files); };
    learningButton.onClick = [this] { showScreen (Screen::learningMenu); };
    addAndMakeVisible (beginTutorial);
    addAndMakeVisible (resumeButton);
    beginTutorial.onClick = [this] { resumeTutorial = false; showScreen (Screen::learning); };
    resumeButton.onClick = [this] { resumeTutorial = true; showScreen (Screen::learning); };
    backButton.onClick = [this]
    {
        if (! workspace()) { showScreen (Screen::menu); return; }
        juce::Component::SafePointer<EducationalDawEditor> safe (this);
        const bool learning = screen == Screen::learning;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
            learning ? "Exit Tutorial?" : "Exit Sandbox?",
            learning ? "Your current tutorial section will be saved." : "Your session will be saved.",
            "Yes", "No", this, juce::ModalCallbackFunction::create ([safe, learning] (int answer)
            {
                if (safe == nullptr || answer != 1) return;
                safe->processor.stopTransport(); safe->finishCapture();
                if (! learning && ! safe->saveSession()) return;
                safe->showScreen (learning ? Screen::menu : Screen::files);
            }));
    };
    restartButton.onClick = [this]
    {
        if (screen == Screen::sandbox)
        {
            confirmDiscard ([this]
            {
                if (auto* track = arrangement.current())
                {
                    track->notes.clear();
                    track->pattern = {};
                    unsaved = true;
                    sessionMessage.clear();
                    refreshArrangement();
                }
            });
        }
        else
            showScreen (Screen::learning);
    };
    setSize (800, 480);
    showScreen (Screen::menu);
}

EducationalDawEditor::~EducationalDawEditor()
{
    stopTimer();
    midiInputChoice.hidePopup();
    midiOutputChoice.hidePopup();
    instrumentChoice.hidePopup();
    midiInputChoice.hideTouchPopup();
    midiOutputChoice.hideTouchPopup();
    instrumentChoice.hideTouchPopup();
    setLookAndFeel (nullptr);
    if (screen == Screen::learning) saveTutorialProgress();
    ++pluginGeneration;
    pluginScan.reset();
    pluginWindow.reset();
    fileList.setLookAndFeel (nullptr);
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
    pluginWindow.reset();
    const bool leavingTutorial = screen == Screen::learning && next != Screen::learning;
    if (leavingTutorial)
    {
        saveTutorialProgress();
        resetInstruments();
        arrangement = sandboxBackup;
        sessionName.setText (sandboxName, false);
        unsaved = sandboxUnsaved;
    }
    if (next == Screen::learning && screen != Screen::learning)
    {
        captureInstrumentStates();
        sandboxBackup = arrangement; sandboxName = sessionName.getText(); sandboxUnsaved = unsaved;
        resetInstruments();
        arrangement = {}; guideStep = 0; tutorial.reset();
        if (resumeTutorial)
        {
            const auto progress = juce::JSON::parse (musicDirectory.getChildFile ("tutorial-progress.json"));
            if (progress.hasProperty ("guideStep"))
            {
                guideStep = juce::jlimit (0, 5, static_cast<int> (progress["guideStep"]));
                SessionArrangement::fromJson (progress["arrangement"], arrangement);
                tutorial.restoreProgress (progress["lesson"]);
                if (arrangement.tracks.empty()) guideStep = 0;
            }
            else { guideStep = 2; arrangement.add (false); tutorial.restoreProgress (progress); }
        }
        sessionName.setText ("Guided Tutorial", false); unsaved = false;
    }
    tutorial.deactivate();
    processor.keyboardState.allNotesOff (0);
    processor.stopTransport(); finishCapture();
    previewingPattern = false;
    screen = next;
    const bool menu = screen == Screen::menu, files = screen == Screen::files;
    const bool learning = screen == Screen::learning, work = workspace();
    startTimerHz (30);
    title.setVisible (! work);
    title.setText (menu ? "Welcome to HMPS!" : (files ? "Sandbox" : "Tutorial"), juce::dontSendNotification);
    instructions.setVisible (screen == Screen::learningMenu);
    instructions.setText ("Begin a new tutorial or pick up where you left off.", juce::dontSendNotification);
    learningButton.setVisible (menu); sandboxButton.setVisible (menu);
    midiInputChoice.setVisible (menu); midiOutputChoice.setVisible(menu); audioStatus.setVisible (menu || work);
    beginTutorial.setVisible (screen == Screen::learningMenu);
    resumeButton.setVisible (screen == Screen::learningMenu);
    resumeButton.setEnabled (musicDirectory.getChildFile ("tutorial-progress.json").existsAsFile());
    fileList.setVisible (files); fileStatus.setVisible (files); sessionBoxTitle.setVisible (files);
    refreshButton.setVisible (files); keyboardButton.setVisible (files);
    keyboardButton.setButtonText ("Create Session");
    openButton.setVisible (files); deleteButton.setVisible (files);
    backButton.setVisible (! menu); backButton.setButtonText (work ? "Exit" : "Main Menu");
    arranger.setVisible (work); addTrackButton.setVisible (work); tempo.setVisible (work);
    addKeyboard.setVisible (false); addDrums.setVisible (false);
    sessionName.setVisible (work); sessionName.setReadOnly (learning);
    guideText.setVisible (learning); guideNext.setVisible (learning);
    feedback.setVisible (work && ! learning);
    restartButton.setVisible (work && ! learning);
    for (auto* c : std::initializer_list<juce::Component*> { &stopButton, &playButton, &recordButton, &loopButton })
        c->setVisible (work);
    saveButton.setVisible (work && ! learning);
    processor.lessonState.store (EducationalDawEngine::LessonState::inactive);
    if (files) refreshFiles();
    refreshArrangement();
    if (leavingTutorial && next == Screen::sandbox) restoreInstruments();
    resized();
}

void EducationalDawEditor::timerCallback()
{
    processor.serviceTimeline();
    if (screen == Screen::learning) tutorial.repaint();
    if (++midiRefreshTicks >= 30) { midiRefreshTicks = 0; refreshMidiInputs(); refreshMidiOutputs();}
    audioStatus.setText (processor.audioStatus + " | " + processor.midiInputStatus(), juce::dontSendNotification);
    EducationalDawEngine::ExternalNote externalNote;
    while (processor.popExternalNote (externalNote))
        if (screen == Screen::learning) tutorial.playPhysicalKey (externalNote.note, externalNote.seconds);
    if (screen == Screen::learning)
    {
        if (guideStep == 2 && tutorial.isStepMode()) { guideStep = 3; refreshArrangement(); }
        if (guideStep == 4 && tutorial.rhythmPassed()) { guideStep = 5; refreshArrangement(); }
    }
    if (workspace())
    {
        if (processor.takeInstrumentChanges()) unsaved = true;
        if (pluginScan)
        {
            sessionMessage = pluginScan->message();
            if (pluginScan->finished.load())
            {
                if (! pluginScan->threadShouldExit()) knownPlugins.clear();
                for (const auto& description : pluginScan->results()) knownPlugins.addType (description);
                pluginScan.reset();
                scanButton.setButtonText ("Scan VST3");
                refreshInstrumentChoices();
            }
        }
        if (capturing && processor.getSessionStatus().transport == EducationalDawEngine::Transport::stopped)
            finishCapture();
        updateTransportControls();
        return;
    }

}

void EducationalDawEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff18252f));
    if (screen == Screen::files)
    {
        const auto card = sessionBoxTitle.getBounds().getUnion (openButton.getBounds())
            .getUnion (keyboardButton.getBounds()).expanded (10, 8);
        g.setColour (juce::Colour (0xff243640));
        g.fillRoundedRectangle (card.toFloat(), 14);
        g.setColour (juce::Colour (0xff537181));
        g.drawRoundedRectangle (card.toFloat(), 14, 1);
    }
}

void EducationalDawEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    if (workspace())
    {
        auto top = area.removeFromTop (44);
        addTrackButton.setBounds (top.removeFromLeft (128).reduced (0, 2));
        top.removeFromLeft (10);
        tempo.setBounds (top.removeFromRight (150));
        sessionName.setBounds (top.reduced (8, 2));
        area.removeFromTop (6);
        audioStatus.setBounds (area.removeFromBottom (18));
        auto bottom = area.removeFromBottom (44);
        backButton.setBounds (bottom.removeFromRight (72));
        if (screen == Screen::sandbox)
        {
            saveButton.setBounds (bottom.removeFromLeft (115));
            restartButton.setBounds (bottom.removeFromRight (100));
        }
        auto transport = bottom.withSizeKeepingCentre (192, 44);
        for (auto* c : std::initializer_list<juce::Component*> { &stopButton, &playButton, &recordButton, &loopButton })
            c->setBounds (transport.removeFromLeft (48).reduced (2));
        area.removeFromBottom (6);
        if (screen == Screen::learning)
        {
            auto guide = area.removeFromTop (56);
            guideNext.setBounds (guide.removeFromRight (100).reduced (4, 10));
            guideText.setBounds (guide.reduced (0, 2));
            area.removeFromTop (4);
        }
        else feedback.setBounds (area.removeFromBottom (24));
        const bool game = screen == Screen::learning && (guideStep == 2 || guideStep == 4 || guideStep == 5);
        if (game)
        {
            // Keep track lanes visible beside the lesson, like the reference's instrument overlay.
            tutorial.setBounds (area.withTrimmedLeft (juce::jmin (210, area.getWidth() / 3)).reduced (4));
            tutorial.toFront (false);
        }
        else if (auto* track = arrangement.current())
        {
            auto instrument = area.removeFromBottom (juce::jmin (160, area.getHeight() / 2));
            auto controls = instrument.removeFromTop (40);
            if (track->drums)
            {
                const int width = controls.getWidth() / 3;
                savePattern.setBounds (controls.removeFromLeft (width).reduced (2));
                loadPattern.setBounds (controls.removeFromLeft (width).reduced (2));
                previewPattern.setBounds (controls.reduced (2));
            }
            else
            {
                loadSoundButton.setBounds (controls.removeFromLeft (104).reduced (2));
                pluginEditorButton.setBounds (controls.removeFromRight (110).reduced (2));
                folderButton.setBounds (controls.removeFromRight (85).reduced (2));
                scanButton.setBounds (controls.removeFromRight (85).reduced (2));
                instrumentChoice.setBounds (controls.reduced (2));
            }
            keyboard.setBounds (instrument.reduced (0, 4));
            keyboard.setKeyWidth (keyboard.getWidth() / 8.0f);
            stepGrid.setBounds (instrument.reduced (0, 4));
        }
        arranger.setBounds (area.reduced (0, 2));
        return;
    }
    title.setBounds (area.removeFromTop (76));
    if (screen == Screen::menu)
    {
        audioStatus.setBounds (area.removeFromBottom (28));
        auto midiRow = area.removeFromBottom (44);
        const int halfWidth = midiRow.getWidth() / 2;
        midiInputChoice.setBounds (midiRow.removeFromLeft (halfWidth).reduced (3, 0));
        midiOutputChoice.setBounds (midiRow.reduced (3, 0));
        auto buttons = area.withSizeKeepingCentre (juce::jmin (400, area.getWidth()), 172);
        learningButton.setBounds (buttons.removeFromTop (76));
        buttons.removeFromTop (20); sandboxButton.setBounds (buttons);
    }
    else if (screen == Screen::learningMenu)
    {
        instructions.setBounds (area.removeFromTop (48));
        backButton.setBounds (area.removeFromBottom (44).removeFromLeft (130));
        auto buttons = area.withSizeKeepingCentre (juce::jmin (400, area.getWidth()), 164);
        beginTutorial.setBounds (buttons.removeFromTop (72));
        buttons.removeFromTop (20); resumeButton.setBounds (buttons);
    }
    else if (screen == Screen::files)
    {
        auto footer = area.removeFromBottom (44);
        backButton.setBounds (footer.removeFromLeft (130));
        refreshButton.setBounds (footer.removeFromRight (100));
        deleteButton.setBounds (footer.removeFromRight (150).reduced (4, 0));
        auto box = area.reduced (juce::jmax (0, (area.getWidth() - 600) / 2), 12);
        sessionBoxTitle.setBounds (box.removeFromTop (36));
        auto actions = box.removeFromBottom (48);
        keyboardButton.setBounds (actions.removeFromLeft (actions.getWidth() / 2).reduced (4));
        openButton.setBounds (actions.reduced (4));
        fileStatus.setBounds (box.removeFromBottom (36));
        fileList.setBounds (box.reduced (4));
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
        fileStatus.setText ("No Sessions Created",
                            juce::dontSendNotification);
    else
    {
        if (pendingSessionSelection.existsAsFile())
        {
            fileList.setSelectedFile (pendingSessionSelection);
            pendingSessionSelection = juce::File();
        }
        selectionChanged();
    }
}

void EducationalDawEditor::selectionChanged()
{
    if (screen != Screen::files)
        return;
    const auto file = fileList.getSelectedFile();
    openButton.setEnabled (file.existsAsFile() && file.hasFileExtension ("hmps;mid;midi"));
    deleteButton.setEnabled (file.existsAsFile() && file.hasFileExtension ("hmps;mid;midi")
                             && file.getParentDirectory() == musicDirectory);
    if (file.existsAsFile())
        fileStatus.setText (file.getFileName() + "  |  "
                            + juce::File::descriptionOfSizeInBytes (file.getSize()),
                            juce::dontSendNotification);
    else
        fileStatus.setText (musicFiles.getNumFiles() == 0 ? "No Sessions Created" : juce::String (musicFiles.getNumFiles()) + " saved sessions",
                            juce::dontSendNotification);
}

void EducationalDawEditor::updateTransportControls()
{
    const auto status = processor.getSessionStatus();
    const bool stopped = status.transport == EducationalDawEngine::Transport::stopped;
    const bool editing = stopped && ! pluginBusy && ! pluginScan;
    const auto* track = arrangement.current();
    recordButton.setEnabled (editing && track != nullptr && ! track->drums && track->notes.empty());
    playButton.setEnabled (editing && arrangement.hasNotes());
    saveButton.setEnabled (editing);
    addTrackButton.setEnabled (editing && arrangement.tracks.size() < 3);
    savePattern.setEnabled (editing);
    loadPattern.setEnabled (editing);
    if (stopped) previewingPattern = false;
    previewPattern.setEnabled (editing || previewingPattern);
    previewPattern.setButtonText (previewingPattern ? "Stop Pattern" : "Preview Pattern");
    stopButton.setEnabled (! stopped);
    loopButton.setEnabled (status.transport != EducationalDawEngine::Transport::recording);
    playButton.setToggleState (status.transport == EducationalDawEngine::Transport::playing, juce::dontSendNotification);
    recordButton.setToggleState (status.transport == EducationalDawEngine::Transport::recording, juce::dontSendNotification);
    sessionName.setEnabled (stopped);
    tempo.setEnabled (stopped);
    arranger.selectionEnabled = editing;
    instrumentChoice.setEnabled (editing && arrangement.current() != nullptr);
    loadSoundButton.setEnabled (editing && arrangement.current() != nullptr && ! arrangement.current()->drums);
    scanButton.setEnabled (stopped && ! pluginBusy);
    folderButton.setEnabled (editing);
    pluginEditorButton.setEnabled (! pluginBusy && ! pluginScan && processor.instrument (arrangement.selected) != nullptr);
    backButton.setEnabled (! pluginBusy && ! pluginScan);
    stepGrid.setEnabled (stopped);
    restartButton.setEnabled (editing && track != nullptr);
    restartButton.setButtonText ("Clear Track");
    addKeyboard.setEnabled (editing && arrangement.tracks.size() < 3);
    addDrums.setEnabled (editing && arrangement.tracks.size() < 3);
    keyboard.setEnabled (! pluginBusy && ! pluginScan && status.transport != EducationalDawEngine::Transport::playing);
    arranger.position = stopped ? 0 : status.seconds;
    arranger.moving = ! stopped;
    arranger.repaint();
    stepGrid.activeStep = ! stopped ? static_cast<int> (status.seconds * arrangement.bpm / 30) % 8 : -1;
    stepGrid.repaint();
    juce::String text = sessionMessage;
    if (! stopped)
        text = (status.transport == EducationalDawEngine::Transport::recording ? "Recording: " : "Playing: ")
             + juce::String (status.seconds, 1) + " s";
    else if (status.full)
        text = "Recording limit reached. You can save this take.";
    else if (text.isEmpty())
        text = track == nullptr ? "Add a track to begin."
             : (track->drums ? "Toggle drum steps. Play previews the full arrangement."
                : (track->notes.empty() ? "Record this keyboard track, then Stop." : "Track recorded. Play all tracks or save."));
    feedback.setText (text, juce::dontSendNotification);
    if (screen == Screen::learning)
    {
        addTrackButton.setEnabled (editing && (guideStep == 0 || guideStep == 3));
        recordButton.setEnabled (false); playButton.setEnabled (false); loopButton.setEnabled (false);
        tempo.setEnabled (false); arranger.selectionEnabled = guideStep == 1;
        scanButton.setEnabled (false); folderButton.setEnabled (false);
    }
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
        "Confirm change", "Discard or replace the selected unsaved work?", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, action] (int result)
        {
            if (safe != nullptr && result == 1)
                action();
        }));
}

void EducationalDawEditor::createSession()
{
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Create Session";
    options.content.setOwned (new SessionNamePanel ([safe] (const juce::String& name)
    {
        return safe != nullptr ? safe->createNamedSession (name) : juce::String ("Window closed.");
    }));
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true; options.resizable = false;
    options.launchAsync();
}

juce::String EducationalDawEditor::createNamedSession (const juce::String& input)
{
    const auto name = input.trim();
    if (name.isEmpty()) return "Please enter a session name.";
    if (name != juce::File::createLegalFileName (name) || name == "." || name == "..")
        return "Please use a name without file-path characters.";
    if (musicDirectory.createDirectory().failed()) return "Cannot access session storage.";
    const auto file = musicDirectory.getChildFile (name + ".hmps");
    if (file.exists()) return "That name already exists. Choose another name.";
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (juce::JSON::toString (SessionArrangement().toJson()))
        || ! temporary.overwriteTargetFileWithTemporary()) return "Could not create the session.";
    pendingSessionSelection = file;
    refreshFiles();
    return {};
}

bool EducationalDawEditor::saveSession()
{
    const auto name = juce::File::createLegalFileName (sessionName.getText().trim()).substring (0, 80);
    if (name.isEmpty() || name == "." || name == "..")
    {
        sessionMessage = "Enter a session name before saving.";
        updateTransportControls();
        return false;
    }
    if (musicDirectory.createDirectory().failed())
    {
        sessionMessage = "Cannot create the sessions folder.";
        updateTransportControls();
        return false;
    }

    captureInstrumentStates();
    for (const auto& track : arrangement.tracks)
        if (track.pluginState.length() > 16 * 1024 * 1024)
        {
            sessionMessage = "Instrument settings exceed the 16 MB per-track limit. Session was not saved.";
            updateTransportControls();
            return false;
        }
    // Project files preserve separate tracks, tempo, editable patterns, and plug-in state.
    const auto target = currentSessionFile.existsAsFile() && currentSessionFile.getFileNameWithoutExtension() == name
        ? currentSessionFile : musicDirectory.getNonexistentChildFile (name, ".hmps", true);
    juce::TemporaryFile temporary (target);
    const bool written = temporary.getFile().replaceWithText (juce::JSON::toString (arrangement.toJson()));
    if (written && temporary.overwriteTargetFileWithTemporary())
    {
        unsaved = false;
        currentSessionFile = target;
        sessionMessage = "Saved: " + target.getFileName();
        sessionName.setText (target.getFileNameWithoutExtension(), false);
    }
    else
        sessionMessage = "Could not save. Check free space and folder permissions.";
    updateTransportControls();
    return ! unsaved;
}

void EducationalDawEditor::openSelectedSession()
{
    const auto file = fileList.getSelectedFile();
    if (! file.existsAsFile() || ! file.hasFileExtension ("hmps;mid;midi"))
    {
        fileStatus.setText ("Choose a MIDI session. Audio-file playback is not supported yet.",
                            juce::dontSendNotification);
        return;
    }
    if (file.getSize() > 64 * 1024 * 1024)
    {
        fileStatus.setText ("This MIDI file is too large (maximum 64 MB).", juce::dontSendNotification);
        return;
    }
    if (file.hasFileExtension ("hmps"))
    {
        SessionArrangement loaded;
        if (! SessionArrangement::fromJson (juce::JSON::parse (file), loaded))
        {
            fileStatus.setText ("Invalid or unsupported HMPS project.", juce::dontSendNotification);
            return;
        }
        confirmDiscard ([this, loaded = std::move (loaded), file]() mutable
        {
            processor.newSession();
            resetInstruments();
            arrangement = std::move (loaded);
            currentSessionFile = file;
            tempo.setValue (arrangement.bpm, juce::dontSendNotification);
            sessionName.setText (file.getFileNameWithoutExtension(), false);
            unsaved = false;
            sessionMessage = "Loaded: " + file.getFileName();
            showScreen (Screen::sandbox);
            restoreInstruments();
        });
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
        resetInstruments();
        arrangement = {};
        currentSessionFile = juce::File();
        arrangement.add (false);
        arrangement.tracks[0].notes = events;
        tempo.setValue (120, juce::dontSendNotification);
        unsaved = false;
        sessionMessage = "Imported MIDI into keyboard track: " + file.getFileName();
        sessionName.setText (file.getFileNameWithoutExtension(), false);
        showScreen (Screen::sandbox);
    });
}

void EducationalDawEditor::deleteSelectedSession()
{
    const auto file = fileList.getSelectedFile();
    if (! file.existsAsFile() || ! file.hasFileExtension ("hmps;mid;midi")
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

void EducationalDawEditor::finishCapture()
{
    if (! capturing) return;
    if (auto* track = arrangement.current())
        track->notes = processor.copySession();
    capturing = false;
    unsaved = true;
    arranger.refresh();
}

void EducationalDawEditor::refreshArrangement()
{
    auto* track = arrangement.current();
    tempo.setValue (arrangement.bpm, juce::dontSendNotification);
    updateTrackLevels();
    refreshInstrumentChoices();
    const bool sandbox = workspace();
    const bool game = screen == Screen::learning && (guideStep == 2 || guideStep == 4 || guideStep == 5);
    tutorial.setVisible (game);
    keyboard.setVisible (sandbox && ! game && track != nullptr && ! track->drums);
    stepGrid.setVisible (sandbox && ! game && track != nullptr && track->drums);
    for (auto* c : std::initializer_list<juce::Component*> { &loadSoundButton, &instrumentChoice, &scanButton, &folderButton, &pluginEditorButton })
        c->setVisible (sandbox && ! game && track != nullptr && ! track->drums);
    for (auto* c : std::initializer_list<juce::Component*> { &savePattern, &loadPattern, &previewPattern })
        c->setVisible (sandbox && ! game && track != nullptr && track->drums);
    if (screen == Screen::learning) updateGuide();
    if (track != nullptr) stepGrid.pattern = track->pattern;
    arranger.refresh();
    updateTransportControls();
    resized();
}

void EducationalDawEditor::updateTrackLevels()
{
    processor.setInstrumentRouting (screen == Screen::sandbox, arrangement.selected, arrangement.bpm);
    int drumChannel = 0;
    if (screen == Screen::sandbox)
    {
        for (int i = 0; i < static_cast<int> (arrangement.tracks.size()); ++i)
            if (arrangement.tracks[i].drums) { drumChannel = 10 + i; break; }
        if (auto* track = arrangement.current(); track != nullptr && track->drums)
            drumChannel = 10 + arrangement.selected;
    }
    processor.setExternalMidiMode (screen == Screen::sandbox ? 2 : (screen == Screen::learning ? 1 : 0), drumChannel);
    std::array<float, 16> levels;
    levels.fill (1.0f);
    keyboard.setMidiChannel (screen == Screen::sandbox ? 16 : 1);
    if (screen == Screen::sandbox)
    {
        for (int i = 0; i < static_cast<int> (arrangement.tracks.size()); ++i)
        {
            const auto& track = arrangement.tracks[i];
            processor.setKeyboardSound (i + 1, track.sound == "synthLead" ? 1 : 0);
            levels[i] = track.volume;
            levels[9 + i] = track.volume;
        }
        if (auto* track = arrangement.current())
        {
            levels[15] = track->volume;
            processor.setKeyboardSound (16, track->sound == "synthLead" ? 1 : 0);
        }
    }
    if (screen != Screen::sandbox)
        processor.setKeyboardSound (1, screen == Screen::learning && guideStep == 1 && arrangement.current() != nullptr
            ? hmps::soundIndex (arrangement.current()->sound) : 0);
    for (int channel = 1; channel <= 16; ++channel)
        processor.setChannelLevel (channel, levels[channel - 1]);
}

void EducationalDawEditor::refreshInstrumentChoices()
{
    instrumentChoice.clear (juce::dontSendNotification);
    instrumentChoice.addItem ("Sound: Piano", 1);
    instrumentChoice.addItem ("Sound: Synth Lead", 2);
    const auto types = knownPlugins.getTypes();
    for (int i = 0; i < types.size(); ++i) instrumentChoice.addItem (types[i].name + " (VST3)", i + 3);
    int id = arrangement.current() != nullptr && arrangement.current()->sound == "synthLead" ? 2 : 1;
    if (auto* track = arrangement.current(); track != nullptr && track->pluginXml.isNotEmpty())
    {
        id = 0;
        juce::PluginDescription selected;
        if (auto xml = juce::XmlDocument::parse (track->pluginXml); xml && selected.loadFromXml (*xml))
        {
            for (int i = 0; i < types.size(); ++i)
                if (types[i].uniqueId == selected.uniqueId && types[i].manufacturerName == selected.manufacturerName)
                    id = i + 3;
            if (id == 0)
            {
                id = types.size() + 3;
                instrumentChoice.addItem (selected.name + (processor.instrument (arrangement.selected) ? " (VST3)" : " (unavailable)"), id);
            }
        }
    }
    instrumentChoice.setSelectedId (id, juce::dontSendNotification);
}

void EducationalDawEditor::scanInstruments (const juce::File& extraFolder)
{
    if (pluginBusy || pluginScan) return;
    processor.stopTransport();
    processor.keyboardState.allNotesOff (0);
    finishCapture();
    juce::VST3PluginFormat format;
    auto paths = format.getDefaultLocationsToSearch();
    const auto extraPathsFile = musicDirectory.getChildFile ("vst3-folders.txt");
    paths.addPath (juce::FileSearchPath (extraPathsFile.loadFileAsString()));
    if (extraFolder.isDirectory())
    {
        juce::FileSearchPath extra (extraPathsFile.loadFileAsString());
        extra.add (extraFolder);
        extraPathsFile.replaceWithText (extra.toString());
        paths.add (extraFolder);
    }
    pluginScan = std::make_unique<InstrumentScan>();
    pluginScan->begin (paths, pluginCache);
    scanButton.setButtonText ("Cancel Scan");
    sessionMessage = "Scanning installed VST3 instruments...";
    updateTransportControls();
}

void EducationalDawEditor::loadInstrument (int track, juce::PluginDescription description,
                                           juce::String state, bool restoring)
{
    if (track < 0 || track >= static_cast<int> (arrangement.tracks.size())) return;
    pluginBusy = true;
    pluginWindow.reset();
    processor.stopTransport();
    processor.keyboardState.allNotesOff (0);
    sessionMessage = "Loading " + description.name + "...";
    updateTransportControls();
    const int generation = pluginGeneration;
    auto formats = pluginFormats; // Keep the factory alive until its callback completes.
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    formats->createPluginInstanceAsync (description,
        processor.getSampleRate() > 0 ? processor.getSampleRate() : 44100.0,
        juce::jmax (1, processor.getBlockSize() > 0 ? processor.getBlockSize() : 512),
        [safe, formats, generation, track, description, state, restoring]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& failure)
        {
            if (safe == nullptr || safe->pluginGeneration != generation) return;
            juce::String error = failure;
            if (instance)
            {
                juce::MemoryBlock bytes;
                if (! EducationalDawEngine::decodeInstrumentState (state, bytes)) error = "Invalid saved instrument state.";
                else
                {
                    if (bytes.getSize() > 0) instance->setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
                    error = safe->processor.installInstrument (track, std::move (instance));
                }
            }
            else if (error.isEmpty()) error = "Could not load this instrument on this computer.";
            if (error.isEmpty())
            {
                auto& target = safe->arrangement.tracks[track];
                target.pluginXml = description.createXml()->toString();
                target.pluginState = safe->processor.instrumentState (track);
                if (! restoring) safe->unsaved = true;
                safe->sessionMessage = "Loaded " + description.name;
            }
            else
            {
                safe->sessionMessage = description.name + ": " + error;
                if (restoring) safe->pluginFailures.add (description.name);
            }
            safe->pluginBusy = false;
            safe->refreshArrangement();
            if (restoring) safe->restoreInstruments (track + 1);
        });
}

void EducationalDawEditor::restoreInstruments (int track)
{
    while (track < static_cast<int> (arrangement.tracks.size()))
    {
        const auto& saved = arrangement.tracks[track];
        if (auto xml = juce::XmlDocument::parse (saved.pluginXml))
        {
            juce::PluginDescription description;
            if (description.loadFromXml (*xml))
            {
                // Match the installed binary by identity when moving a project between computers.
                for (const auto& candidate : knownPlugins.getTypes())
                    if (candidate.uniqueId == description.uniqueId && candidate.manufacturerName == description.manufacturerName)
                        description = candidate;
                loadInstrument (track, description, saved.pluginState, true);
                return;
            }
        }
        ++track;
    }
    pluginBusy = false;
    if (! pluginFailures.isEmpty())
        sessionMessage = "Built-in fallback: " + pluginFailures.joinIntoString (", ") + ". Scan/install compatible VST3 instruments.";
    updateTransportControls();
}

void EducationalDawEditor::captureInstrumentStates()
{
    for (int i = 0; i < static_cast<int> (arrangement.tracks.size()); ++i)
        if (processor.instrument (i) != nullptr)
            arrangement.tracks[i].pluginState = processor.instrumentState (i);
    processor.takeInstrumentChanges();
}

void EducationalDawEditor::resetInstruments()
{
    ++pluginGeneration;
    pluginWindow.reset();
    processor.clearInstruments();
    processor.takeInstrumentChanges();
    pluginFailures.clear();
    pluginBusy = false;
}

void EducationalDawEditor::refreshMidiInputs()
{
    if (midiInputChoice.isPopupActive() || midiInputChoice.isTouchPopupActive())
        return;

    midiInputs = juce::MidiInput::getAvailableDevices();
    midiInputChoice.clear (juce::dontSendNotification);
    midiInputChoice.addItem ("MIDI Input: None", 1);
    int selected = 1;
    const auto identifier = processor.midiInputIdentifier();
    for (int i = 0; i < midiInputs.size(); ++i)
    {
        midiInputChoice.addItem ("MIDI: " + midiInputs[i].name, i + 2);
        if (midiInputs[i].identifier == identifier) selected = i + 2;
    }
    if (selected == 1 && identifier.isNotEmpty())
    {
        selected = midiInputs.size() + 2;
        midiInputChoice.addItem ("MIDI: disconnected (waiting to reconnect)", selected);
    }
    midiInputChoice.setSelectedId (selected, juce::dontSendNotification);
}

void EducationalDawEditor::refreshMidiOutputs()
{
    if (midiOutputChoice.isPopupActive() || midiOutputChoice.isTouchPopupActive())
        return;

    midiOutputs = juce::MidiOutput::getAvailableDevices();

    midiOutputChoice.clear (juce::dontSendNotification);
    midiOutputChoice.addItem ("MIDI Output: None", 1);

    const auto identifier = processor.midiOutputIdentifier();
    int selected = 1;
    bool found = identifier.isEmpty();

    for (int i = 0; i < midiOutputs.size(); ++i)
    {
        midiOutputChoice.addItem (
            "Output: " + midiOutputs[i].name, i + 2);

        if (midiOutputs[i].identifier == identifier)
        {
            selected = i + 2;
            found = true;
        }
    }

    if (! found)
    {
        // Close an output that has disappeared.
        processor.selectMidiOutput ({});
    }

    midiOutputChoice.setSelectedId (
        selected, juce::dontSendNotification);
}
void EducationalDawEditor::showSoundChooser()
{
    if (pluginBusy || pluginScan || arrangement.current() == nullptr) return;
    const int selectedTrack = arrangement.selected;
    juce::StringArray names { "Piano", "Synth Lead" };
    for (const auto& type : knownPlugins.getTypes()) names.add (type.name + " (VST3)");
    const int current = juce::jlimit (1, names.size(), instrumentChoice.getSelectedId());
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Load Sound";
    options.dialogBackgroundColour = juce::Colour (0xff18252f);
    options.content.setOwned (new SoundPicker (names, current, [safe, selectedTrack] (int choice)
    {
        if (safe != nullptr && safe->arrangement.selected == selectedTrack
            && safe->processor.getSessionStatus().transport == EducationalDawEngine::Transport::stopped)
            safe->instrumentChoice.setSelectedId (choice, juce::sendNotificationSync);
    }));
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void EducationalDawEditor::chooseTrack()
{
    auto* dialog = new juce::AlertWindow ("Choose Track", "Add an instrument to your arrangement.", juce::MessageBoxIconType::NoIcon);
    const bool learning = screen == Screen::learning;
    if (! learning || guideStep == 0) dialog->addButton ("Keyboard", 1);
    if (! learning || guideStep == 3) dialog->addButton ("Step Sequencer", 2);
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr) return;
        if (result == 1) safe->addKeyboard.onClick();
        if (result == 2) safe->addDrums.onClick();
    }), true);
}

void EducationalDawEditor::updateGuide()
{
    const juce::String messages[] {
        "1 / 6  Welcome! This is your arranger. Tap Add Track at the top left, then choose Keyboard.",
        "2 / 6  Select Load Sound to try Piano or Synth Lead. Play the keys, then tap Next for Piano Tiles.",
        "3 / 6  Press Start in Piano Tiles. Play the matching keys at the gold line. Reach 70%, then Continue.",
        "4 / 6  Now add a rhythm track. Tap Add Track and choose Step Sequencer.",
        "5 / 6  Hear the target rhythm, fill the grid, then Submit. Hints can help. Reach 80% to finish.",
        "6 / 6  Tutorial complete! You have practiced the keyboard and built a drum pattern."
    };
    guideText.setText (messages[juce::jlimit (0, 5, guideStep)], juce::dontSendNotification);
    guideNext.setButtonText (guideStep == 5 ? "Finish" : "Next");
    guideNext.setEnabled (guideStep == 1 || guideStep == 5);
}

void EducationalDawEditor::saveTutorialProgress()
{
    if (musicDirectory.createDirectory().failed()) return;
    auto* data = new juce::DynamicObject();
    data->setProperty ("guideStep", guideStep);
    data->setProperty ("arrangement", arrangement.toJson());
    data->setProperty ("lesson", tutorial.progress());
    const auto file = musicDirectory.getChildFile ("tutorial-progress.json");
    juce::TemporaryFile temporary (file);
    if (temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (data))))
        temporary.overwriteTargetFileWithTemporary();
}

void EducationalDawEditor::patternDialog (bool saving)
{
    if (arrangement.current() == nullptr || ! arrangement.current()->drums) return;
    const int track = arrangement.selected;
    const auto folder = musicDirectory.getChildFile ("Patterns");
    if (folder.createDirectory().failed())
    {
        sessionMessage = "Cannot access pattern storage."; updateTransportControls(); return;
    }
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.hmpattern");
    files.sort();
    auto* dialog = new juce::AlertWindow (saving ? "Save Pattern" : "Load Pattern",
        saving ? "Name this drum pattern." : "Choose a saved drum pattern.", juce::MessageBoxIconType::NoIcon);
    if (saving) dialog->addTextEditor ("name", "Pattern A", "Pattern name");
    else
    {
        if (files.isEmpty()) { delete dialog; sessionMessage = "No saved patterns yet."; updateTransportControls(); return; }
        juce::StringArray names;
        for (const auto& file : files) names.add (file.getFileNameWithoutExtension());
        dialog->addComboBox ("pattern", names, "Pattern");
    }
    dialog->addButton (saving ? "Save" : "Load", 1);
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<EducationalDawEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog, files, folder, track, saving] (int result)
    {
        if (safe == nullptr || result != 1 || safe->arrangement.selected != track) return;
        auto* current = safe->arrangement.current();
        if (current == nullptr || ! current->drums) return;
        if (saving)
        {
            const auto name = juce::File::createLegalFileName (dialog->getTextEditorContents ("name").trim());
            if (name.isEmpty()) { safe->sessionMessage = "Enter a pattern name."; safe->updateTransportControls(); return; }
            juce::Array<juce::var> values;
            for (bool on : current->pattern) values.add (on);
            const auto file = folder.getNonexistentChildFile (name, ".hmpattern", true);
            juce::TemporaryFile temporary (file);
            const bool ok = temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (values)))
                && temporary.overwriteTargetFileWithTemporary();
            safe->sessionMessage = ok ? "Saved pattern: " + file.getFileNameWithoutExtension() : "Could not save pattern.";
        }
        else
        {
            const int index = dialog->getComboBoxComponent ("pattern")->getSelectedItemIndex();
            if (! juce::isPositiveAndBelow (index, files.size())) return;
            const auto value = juce::JSON::parse (files[index]);
            const auto* values = value.getArray();
            if (values == nullptr || values->size() != 32
                || std::any_of (values->begin(), values->end(), [] (const juce::var& item) { return ! item.isBool(); }))
                safe->sessionMessage = "Invalid pattern file.";
            else
            {
                for (int i = 0; i < 32; ++i) current->pattern[i] = static_cast<bool> ((*values)[i]);
                safe->unsaved = true;
                safe->sessionMessage = "Loaded pattern: " + files[index].getFileNameWithoutExtension();
                safe->refreshArrangement();
            }
        }
        safe->updateTransportControls();
    }), true);
}
