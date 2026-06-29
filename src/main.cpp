/*
    Forge — a native DAW on JUCE + Tracktion Engine.

    Phase 1 (in progress): the spine — a real project (Edit) on disk, audio import,
    transport with a moving playhead, an arrange view, and recording.

    Ownership: ForgeApplication owns the single te::Engine (so it outlives windows and
    project reloads). MainComponent owns a ProjectSession (the open project) and the UI.

    Run modes:
      Forge              -> opens/creates Documents/Forge/Untitled.tracktionedit; empty
                            project + toolbar (New/Open/Save/Save As/Import).
      Forge --selftest   -> drives the app headless (imports a generated tone and plays it),
                            writes a PASS/FAIL report to %TEMP%/forge_phase0_selftest.log,
                            then quits.
*/

#include <JuceHeader.h>
#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "ui/arrange/ArrangeView.h"

namespace te = tracktion;
using namespace juce;

//==============================================================================
/** Writes `seconds` of a mono sine wave to a temp .wav file and returns it. */
static File createSineWaveFile (double sampleRate, double seconds, double frequencyHz, float gain)
{
    auto file = File::createTempFile (".wav");

    if (auto outStream = file.createOutputStream())
    {
        WavAudioFormat wavFormat;

        if (auto* writer = wavFormat.createWriterFor (outStream.get(), sampleRate, 1, 16, {}, 0))
        {
            outStream.release();                                  // writer now owns the stream
            std::unique_ptr<AudioFormatWriter> writerOwner (writer);

            const int numSamples = (int) (seconds * sampleRate);
            AudioBuffer<float> buffer (1, numSamples);
            auto* samples = buffer.getWritePointer (0);

            const double phaseDelta = MathConstants<double>::twoPi * frequencyHz / sampleRate;
            double phase = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                samples[i] = gain * (float) std::sin (phase);
                phase += phaseDelta;
            }

            writerOwner->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        }
    }

    return file;
}

static File defaultProjectFile()
{
    return File::getSpecialLocation (File::userDocumentsDirectory)
               .getChildFile ("Forge")
               .getChildFile ("Untitled" + String (te::editFileSuffix));
}

//==============================================================================
class MainComponent : public Component,
                      private Timer
{
public:
    MainComponent (te::Engine& e, bool runSelfTest)
        : engine (e), session (e), selfTest (runSelfTest)
    {
        editLoaded = session.openOrCreate (defaultProjectFile());

        setupToolbar();

        addAndMakeVisible (arrangeView);

        statusLabel.setJustificationType (Justification::centredLeft);
        addAndMakeVisible (statusLabel);

        if (selfTest)
            importTestToneAndPlay();

        arrangeView.setEdit (session.getEdit());

        setSize (980, 520);

        if (selfTest)
            startTimer (3000);
        else
            startTimerHz (5);
    }

    ~MainComponent() override
    {
        if (auto* t = session.getTransport())
            t->stop (false, false);

        sineFile.deleteFile();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
        g.setColour (Colours::white);
        g.setFont (22.0f);
        g.drawText ("FORGE", getLocalBounds().removeFromTop (40).reduced (8), Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (40);                              // title strip

        auto toolbar = r.removeFromTop (36).reduced (4, 2);
        const int bw = 86, gap = 4;
        for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &importButton, &playStopButton })
        {
            b->setBounds (toolbar.removeFromLeft (bw));
            toolbar.removeFromLeft (gap);
        }

        statusLabel.setBounds (r.removeFromBottom (44).reduced (8, 4));
        arrangeView.setBounds (r);
    }

private:
    te::Engine& engine;
    ProjectSession session;
    bool selfTest = false;
    bool editLoaded = false;

    te::WaveAudioClip::Ptr clip;     // the most recently imported clip
    File sineFile;                   // selftest tone source (temp)

    TextButton newButton    { "New" },  openButton   { "Open" },
               saveButton   { "Save" }, saveAsButton { "Save As" },
               importButton { "Import" }, playStopButton { "Play" };
    Label statusLabel { {}, "Empty project" };
    std::unique_ptr<FileChooser> fileChooser;

    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };

    //==============================================================================
    void setupToolbar()
    {
        for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &importButton, &playStopButton })
            addAndMakeVisible (b);

        newButton.onClick    = [this] { session.newProject (defaultProjectFile()); clip = nullptr; rebind(); };
        openButton.onClick   = [this] { openDialog(); };
        saveButton.onClick   = [this] { session.save(); };
        saveAsButton.onClick = [this] { saveAsDialog(); };
        importButton.onClick = [this] { importDialog(); };
        playStopButton.onClick = [this]
        {
            if (auto* ed = session.getEdit())
                EngineHelpers::togglePlay (*ed);
        };
    }

    void openDialog()
    {
        fileChooser = std::make_unique<FileChooser> ("Open project...", File(),
                                                     "*" + String (te::editFileSuffix));
        fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f.existsAsFile())
                                      {
                                          session.openProject (f);
                                          clip = nullptr;
                                          rebind();
                                      }
                                  });
    }

    void saveAsDialog()
    {
        fileChooser = std::make_unique<FileChooser> ("Save project as...", File(),
                                                     "*" + String (te::editFileSuffix));
        fileChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == File())
                                          return;
                                      if (f.getFileExtension().isEmpty())
                                          f = f.withFileExtension (te::editFileSuffix);
                                      session.saveAs (f);
                                      rebind();
                                  });
    }

    void importDialog()
    {
        EngineHelpers::browseForAudioFile (engine, [this] (const File& f)
        {
            clip = session.importAudioFile (f, te::TimePosition());
            session.save();
            rebind();
        });
    }

    void importTestToneAndPlay()
    {
        sineFile = createSineWaveFile (44100.0, 30.0, 440.0, 0.2f);
        clip = session.importAudioFile (sineFile, te::TimePosition());

        if (clip != nullptr)
        {
            auto& transport = session.getEdit()->getTransport();
            transport.setLoopRange (clip->getEditTimeRange());
            transport.looping = true;
            transport.ensureContextAllocated();
            transport.setPosition (te::TimePosition());

            MessageManager::callAsync ([this]
            {
                if (auto* ed = session.getEdit())
                    ed->getTransport().play (false);
            });
        }
    }

    /** Re-reads project state into the UI after a project swap/import. */
    void rebind()
    {
        arrangeView.setEdit (session.getEdit());
        repaint();
    }

    String describeAudioState() const
    {
        if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
            return device->getName()
                 + " @ " + String (device->getCurrentSampleRate(), 0) + " Hz"
                 + ", buffer " + String (device->getCurrentBufferSizeSamples());

        return "No audio device open";
    }

    void timerCallback() override
    {
        auto* t = session.getTransport();
        const bool playing = t != nullptr && t->isPlaying();
        playStopButton.setButtonText (playing ? "Stop" : "Play");

        statusLabel.setText (String ("Project: ") + session.getEditFile().getFileName()
                                 + "   (" + String (session.getNumAudioTracks()) + " track"
                                 + (session.getNumAudioTracks() == 1 ? "" : "s") + ")"
                                 + "\n" + describeAudioState()
                                 + "\n" + (playing ? "Playing" : "Stopped"),
                             dontSendNotification);

        if (selfTest)
        {
            writeSelfTestReport (playing);
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }

    void writeSelfTestReport (bool playing) const
    {
        auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        auto* t = session.getTransport();

        double posSecs = -1.0;
        bool hasContext = false;
        if (t != nullptr)
        {
            posSecs    = t->getPosition().inSeconds();
            hasContext = t->getCurrentPlaybackContext() != nullptr;
        }

        const double clipLen = clip != nullptr ? clip->getEditTimeRange().getLength().inSeconds() : -1.0;
        const int numClipComps = arrangeView.getNumClipComponentsOnTrack0();
        const int playheadX = arrangeView.getPlayheadX();

        const bool pass = device != nullptr
                          && session.getNumAudioTracks() >= 1
                          && clip != nullptr
                          && clipLen > 0.0
                          && (playing || posSecs > 0.05)
                          && numClipComps >= 1;

        String report;
        report << "device="          << (device != nullptr ? device->getName() : String ("none")) << newLine
               << "sampleRate="      << (device != nullptr ? String (device->getCurrentSampleRate(), 0) : String ("0")) << newLine
               << "editFile="        << session.getEditFile().getFullPathName() << newLine
               << "editLoaded="      << (editLoaded ? 1 : 0) << newLine
               << "numTracks="       << session.getNumAudioTracks() << newLine
               << "importedClip="    << (clip != nullptr ? 1 : 0) << newLine
               << "clipLengthSecs="  << String (clipLen, 3) << newLine
               << "numClipComponents=" << numClipComps << newLine
               << "playheadX="       << playheadX << newLine
               << "hasContext="      << (hasContext ? 1 : 0) << newLine
               << "playing="         << (playing ? 1 : 0) << newLine
               << "position="        << String (posSecs, 3) << newLine
               << "result="          << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

//==============================================================================
class ForgeApplication : public JUCEApplication
{
public:
    const String getApplicationName() override     { return "Forge"; }
    const String getApplicationVersion() override  { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override      { return true; }

    void initialise (const String& commandLine) override
    {
        const bool selfTest = commandLine.contains ("--selftest");
        mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, selfTest), *this));
    }

    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow (const String& name, Component* c, JUCEApplication& a)
            : DocumentWindow (name,
                              Desktop::getInstance().getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons),
              app (a)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (c, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    // Engine is owned by the app so it outlives windows and project reloads.
    te::Engine engine { "Forge" };
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
