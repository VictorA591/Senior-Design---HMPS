#pragma once
#include <JuceHeader.h>

// Run each candidate in a separate process: a bad scanner cannot terminate the DAW.
inline bool runHmpsPluginScan (const juce::String& commandLine)
{
    auto args = juce::StringArray::fromTokens (commandLine, true);
    args.removeEmptyStrings();
    if (args.size() != 3 || args[0] != "--hmps-scan-vst3") return false;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, args[1].unquoted());
    juce::XmlElement result ("PLUGINS");
    for (auto* description : found)
        if (description->isInstrument) result.addChildElement (description->createXml().release());
    result.writeTo (juce::File (args[2].unquoted()));
    return true;
}

class InstrumentScan : public juce::Thread
{
public:
    InstrumentScan() : Thread ("VST3 scanning") {}
    ~InstrumentScan() override
    {
        signalThreadShouldExit();
        stopThread (2000);
    }
    void begin (juce::FileSearchPath folders, const juce::File& destination)
    {
        search = folders;
        cache = destination;
        finished.store (false);
        startThread();
    }
    juce::String message()
    {
        const juce::ScopedLock lock (mutex);
        return status;
    }
    juce::Array<juce::PluginDescription> results()
    {
        const juce::ScopedLock lock (mutex);
        return found;
    }
    std::atomic<bool> finished { false };
private:
    void update (juce::String text)
    {
        const juce::ScopedLock lock (mutex);
        status = std::move (text);
    }
    void run() override
    {
        juce::VST3PluginFormat format;
        const auto candidates = format.searchPathsForPlugins (search, true, false);
        int failed = 0;
        for (const auto& file : candidates)
        {
            if (threadShouldExit()) break;
            update ("Scanning " + juce::File (file).getFileName());
            const auto output = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("hmps-vst-scan-" + juce::Uuid().toString(), ".xml", false);
            juce::ChildProcess child;
            juce::StringArray args;
            args.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
            args.add ("--hmps-scan-vst3"); args.add (file); args.add (output.getFullPathName());
            bool started = child.start (args, 0);
            const double deadline = juce::Time::getMillisecondCounterHiRes() + 30000;
            while (started && child.isRunning() && ! threadShouldExit()
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                wait (50);
            if (started && child.isRunning()) { child.kill(); child.waitForProcessToFinish (500); }
            if (auto xml = juce::XmlDocument::parse (output))
            {
                const juce::ScopedLock lock (mutex);
                for (auto* element : xml->getChildIterator())
                {
                    juce::PluginDescription description;
                    if (description.loadFromXml (*element)) found.add (description);
                }
            }
            else ++failed;
            output.deleteFile();
        }
        juce::KnownPluginList list;
        for (const auto& description : results()) list.addType (description);
        if (! threadShouldExit())
            if (auto xml = list.createXml()) xml->writeTo (cache);
        update (threadShouldExit() ? "Scan cancelled."
            : juce::String (results().size()) + " instruments found; " + juce::String (failed) + " could not be scanned.");
        finished.store (true);
    }
    juce::FileSearchPath search;
    juce::File cache;
    juce::CriticalSection mutex;
    juce::String status { "Preparing scan..." };
    juce::Array<juce::PluginDescription> found;
};

class InstrumentWindow : public juce::DocumentWindow
{
public:
    explicit InstrumentWindow (juce::AudioPluginInstance& plugin)
        : DocumentWindow (plugin.getName(), juce::Colour (0xff18252f), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        auto* editor = plugin.createEditorAndMakeActive();
        if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (plugin);
        setResizable (editor->isResizable(), false);
        setContentOwned (editor, true);
        setSize (juce::jmax (320, editor->getWidth()), juce::jmax (200, editor->getHeight()));
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        {
            const auto available = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced (24);
            if (editor->isResizable())
            {
                setResizeLimits (480, 240, juce::jmax (480, available.getWidth()), juce::jmax (240, available.getHeight()));
                setSize (juce::jmin (getWidth(), available.getWidth()), juce::jmin (getHeight(), available.getHeight()));
            }
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
    }
    void closeButtonPressed() override { setVisible (false); }
};
