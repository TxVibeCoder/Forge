/*
    Forge — a native DAW on JUCE + Tracktion Engine.

    Phase 1: the spine — a real project (Edit) on disk, audio import, a transport with a
    moving playhead and an arrange view, and recording from a live input.

    Ownership: ForgeApplication owns the single te::Engine (so it outlives windows and
    project reloads). MainComponent owns a ProjectSession (the open project) and the UI.

    Run modes:
      Forge                  -> opens/creates Documents/Forge/Untitled.tracktionedit.
      Forge --selftest        -> headless playback check (import a tone, play it).
      Forge --selftest-record -> headless recording check (arm, record ~1s, stop, verify clip).
    Both selftests write a PASS/FAIL report to %TEMP%/forge_phase0_selftest.log then quit.
*/

#include <JuceHeader.h>
#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "engine/RecordController.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/transport/TransportBar.h"

namespace te = tracktion;
using namespace juce;

enum class SelfTest { none, playback, record };

//==============================================================================
static File createSineWaveFile (double sampleRate, double seconds, double frequencyHz, float gain)
{
    auto file = File::createTempFile (".wav");

    if (auto outStream = file.createOutputStream())
    {
        WavAudioFormat wavFormat;

        if (auto* writer = wavFormat.createWriterFor (outStream.get(), sampleRate, 1, 16, {}, 0))
        {
            outStream.release();
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
    MainComponent (te::Engine& e, SelfTest testMode)
        : engine (e), session (e), recorder (e), mode (testMode)
    {
        // Open input channels too (not just output), so recording has wave-in devices.
        engine.getDeviceManager().initialise();

        editLoaded = session.openOrCreate (defaultProjectFile());

        setupToolbar();

        addAndMakeVisible (transportBar);
        transportBar.onRecord = [this] { toggleRecordTake(); };

        addAndMakeVisible (arrangeView);

        statusLabel.setJustificationType (Justification::centredLeft);
        addAndMakeVisible (statusLabel);

        if (mode == SelfTest::playback)
            importTestToneAndPlay();

        transportBar.setEdit (session.getEdit());
        arrangeView.setEdit (session.getEdit());

        setSize (980, 560);

        if (mode == SelfTest::record)
        {
            MessageManager::callAsync ([this] { beginSelfTestRecording(); });
            startTimer (1600);
        }
        else if (mode == SelfTest::playback)
        {
            startTimer (3000);
        }
        else
        {
            startTimerHz (5);
        }
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
        r.removeFromTop (40);                                 // title strip

        auto toolbar = r.removeFromTop (36).reduced (4, 2);
        const int bw = 86, gap = 4;
        for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &importButton, &audioButton })
        {
            b->setBounds (toolbar.removeFromLeft (bw));
            toolbar.removeFromLeft (gap);
        }

        transportBar.setBounds (r.removeFromTop (40));
        statusLabel.setBounds (r.removeFromBottom (40).reduced (8, 4));
        arrangeView.setBounds (r);
    }

private:
    te::Engine& engine;
    ProjectSession session;
    RecordController recorder;
    SelfTest mode = SelfTest::none;
    bool editLoaded = false;

    te::WaveAudioClip::Ptr clip;     // most recently imported clip
    File sineFile;                   // selftest tone source (temp)

    // record-selftest captured state
    int  rcInputDeviceCount = 0;
    bool rcTrackArmed = false;
    bool rcRecordingStarted = false;
    String rcAvailableInputs;
    int  rcCurrentDeviceInputs = 0;
    String rcDiagTypes;

    TextButton newButton  { "New" },  openButton   { "Open" },
               saveButton { "Save" }, saveAsButton { "Save As" },
               importButton { "Import" }, audioButton { "Audio" };
    Label statusLabel { {}, "Empty project" };
    std::unique_ptr<FileChooser> fileChooser;

    TransportBar transportBar;
    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };

    //==============================================================================
    void setupToolbar()
    {
        for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &importButton, &audioButton })
            addAndMakeVisible (b);

        newButton.onClick    = [this] { session.newProject (defaultProjectFile()); clip = nullptr; rebind(); };
        openButton.onClick   = [this] { openDialog(); };
        saveButton.onClick   = [this] { session.save(); };
        saveAsButton.onClick = [this] { saveAsDialog(); };
        importButton.onClick = [this] { importDialog(); };
        audioButton.onClick  = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
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

    //==============================================================================
    // Recording (used by the Rec button and the record selftest).
    void toggleRecordTake()
    {
        auto* edit = session.getEdit();
        auto* t = session.getTransport();
        if (edit == nullptr || t == nullptr)
            return;

        if (t->isRecording())
        {
            t->stop (false, false);
            session.save();
            rebind();
        }
        else
        {
            recorder.enableInputs();
            if (auto* track = te::getAudioTracks (*edit)[0])
                recorder.armFirstInputToTrack (*edit, *track);
            t->record (false);
        }
    }

    void beginSelfTestRecording()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr)
            return;

        // Diagnostic: what does the device layer actually expose?
        auto& jdm = engine.getDeviceManager().deviceManager;
        if (auto* type = jdm.getCurrentDeviceTypeObject())
            rcAvailableInputs = type->getDeviceNames (true).joinIntoString ("|");
        if (auto* dev = jdm.getCurrentAudioDevice())
            rcCurrentDeviceInputs = dev->getActiveInputChannels().countNumberOfSetBits();

        for (auto* type : jdm.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            rcDiagTypes += type->getTypeName() + "=" + String (type->getDeviceNames (true).size()) + "in ";
        }

        recorder.enableInputs();
        rcInputDeviceCount = recorder.getInputDeviceCount();

        if (auto* track = te::getAudioTracks (*edit)[0])
            rcTrackArmed = recorder.armFirstInputToTrack (*edit, *track);

        if (auto* t = session.getTransport())
        {
            t->record (false);
            rcRecordingStarted = t->isRecording();
        }
    }

    void finishSelfTestRecording()
    {
        if (auto* t = session.getTransport())
            t->stop (false, false);
        session.save();
        rebind();

        int recordedClips = 0;
        bool recFileExists = false;
        double recLen = -1.0;

        if (auto* edit = session.getEdit())
            if (auto* track = te::getAudioTracks (*edit)[0])
                for (auto* c : track->getClips())
                    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                    {
                        ++recordedClips;
                        recFileExists = wc->getSourceFileReference().getFile().existsAsFile();
                        recLen = wc->getEditTimeRange().getLength().inSeconds();
                    }

        const bool pass = rcInputDeviceCount > 0 && rcTrackArmed && rcRecordingStarted
                          && recordedClips >= 1 && recFileExists && recLen > 0.0;

        String report;
        report << "mode=record" << newLine
               << "availableInputDevices="  << (rcAvailableInputs.isEmpty() ? String ("(none)") : rcAvailableInputs) << newLine
               << "currentDeviceInputChans=" << rcCurrentDeviceInputs << newLine
               << "deviceTypesInputs="      << rcDiagTypes << newLine
               << "inputDeviceCount="      << rcInputDeviceCount << newLine
               << "trackArmed="            << (rcTrackArmed ? 1 : 0) << newLine
               << "recordingStarted="      << (rcRecordingStarted ? 1 : 0) << newLine
               << "recordedClipCount="     << recordedClips << newLine
               << "recordedFileExists="    << (recFileExists ? 1 : 0) << newLine
               << "recordedClipLengthSecs="<< String (recLen, 3) << newLine
               << "recordError="           << recorder.getLastError() << newLine
               << "result="                << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    void rebind()
    {
        transportBar.setEdit (session.getEdit());
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
        if (mode == SelfTest::record)
        {
            finishSelfTestRecording();
            return;
        }

        auto* t = session.getTransport();
        const bool playing = t != nullptr && t->isPlaying();

        statusLabel.setText (String ("Project: ") + session.getEditFile().getFileName()
                                 + "   (" + String (session.getNumAudioTracks()) + " track"
                                 + (session.getNumAudioTracks() == 1 ? "" : "s") + ")"
                                 + "   " + describeAudioState(),
                             dontSendNotification);

        if (mode == SelfTest::playback)
        {
            writePlaybackReport (playing);
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }

    void writePlaybackReport (bool playing) const
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

        const double clipLen   = clip != nullptr ? clip->getEditTimeRange().getLength().inSeconds() : -1.0;
        const int numClipComps = arrangeView.getNumClipComponentsOnTrack0();
        const int playheadX    = arrangeView.getPlayheadX();

        const bool pass = device != nullptr
                          && session.getNumAudioTracks() >= 1
                          && clip != nullptr
                          && clipLen > 0.0
                          && (playing || posSecs > 0.05)
                          && numClipComps >= 1
                          && transportBar.readoutIsNonEmpty();

        String report;
        report << "mode=playback" << newLine
               << "device="            << (device != nullptr ? device->getName() : String ("none")) << newLine
               << "sampleRate="        << (device != nullptr ? String (device->getCurrentSampleRate(), 0) : String ("0")) << newLine
               << "editFile="          << session.getEditFile().getFullPathName() << newLine
               << "editLoaded="        << (editLoaded ? 1 : 0) << newLine
               << "numTracks="         << session.getNumAudioTracks() << newLine
               << "importedClip="      << (clip != nullptr ? 1 : 0) << newLine
               << "clipLengthSecs="    << String (clipLen, 3) << newLine
               << "numClipComponents=" << numClipComps << newLine
               << "playheadX="         << playheadX << newLine
               << "transportReadout="  << (transportBar.readoutIsNonEmpty() ? 1 : 0) << newLine
               << "hasContext="        << (hasContext ? 1 : 0) << newLine
               << "playing="           << (playing ? 1 : 0) << newLine
               << "position="          << String (posSecs, 3) << newLine
               << "result="            << (pass ? "PASS" : "FAIL") << newLine;

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
        const auto mode = commandLine.contains ("--selftest-record") ? SelfTest::record
                        : commandLine.contains ("--selftest")        ? SelfTest::playback
                                                                     : SelfTest::none;
        mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, mode), *this));
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

    te::Engine engine { "Forge" };
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
