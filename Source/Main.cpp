#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

class Application : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Educational DAW Engine"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    void initialise (const juce::String& commandLine) override
    {
        processor = std::make_unique<EducationalDawEngine>();
        const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
        processor->audioStatus = error.isEmpty() ? "Audio ready - built-in synth"
                                               : "Audio unavailable: " + error;
        audioPlayer.setProcessor (processor.get());
        deviceManager.addAudioCallback (&audioPlayer);
        mainWindow = std::make_unique<MainWindow> (
            getApplicationName(), std::make_unique<EducationalDawEditor> (*processor),
            juce::StringArray::fromTokens (commandLine, true).contains ("--kiosk"));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        deviceManager.removeAudioCallback (&audioPlayer);
        audioPlayer.setProcessor (nullptr);
        deviceManager.closeAudioDevice();
        processor = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, std::unique_ptr<juce::Component> ui, bool kiosk)
            : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (! kiosk);
            setContentOwned (ui.release(), true);
            setResizable (! kiosk, ! kiosk);
            if (kiosk)
                setTitleBarHeight (0);
            setResizeLimits (640, 480, 1920, 1200);
            centreWithSize (800, 480);
            setVisible (true);
            if (kiosk)
                setFullScreen (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<EducationalDawEngine> processor;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (Application)
