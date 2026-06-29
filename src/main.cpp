/*
    Forge — a native DAW on JUCE + Tracktion Engine.

    Phase 1 spine + interface-plan shell (docs/INTERFACE.md): a single-window ForgeShell with
    a top Control Bar, a center view-slot (Arrange now; Mixer reserved), collapsible Browser
    and Detail-drawer regions, and a status strip. Dark, amber-accented (ForgeLookAndFeel).

    Run modes:
      Forge                  -> opens/creates Documents/Forge/Untitled.tracktionedit.
      Forge --selftest        -> headless playback check (import a tone, play it).
      Forge --selftest-record -> headless recording check (arm, record ~1s, verify clip).
*/

#include <JuceHeader.h>
#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "engine/RecordController.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"

namespace te = tracktion;
using namespace juce;

enum class SelfTest { none, playback, record };
enum class ViewMode { Arrange, Mixer };

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

static File uniqueUntitledFile()
{
    auto dir = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Forge");
    auto f = dir.getChildFile ("Untitled" + String (te::editFileSuffix));
    for (int n = 2; f.existsAsFile(); ++n)
        f = dir.getChildFile ("Untitled-" + String (n) + String (te::editFileSuffix));
    return f;
}

//==============================================================================
class MainComponent : public Component,
                      private Timer
{
public:
    MainComponent (te::Engine& e, SelfTest testMode)
        : engine (e), session (e), recorder (e), mode (testMode)
    {
        engine.getDeviceManager().initialise();   // open input channels too (for recording)

        // Self-tests use a fresh, isolated temp project so clip/track counts are deterministic
        // and never polluted by leftovers in the persistent Untitled project.
        const auto projectFile = (mode == SelfTest::none) ? defaultProjectFile()
                                                          : File::createTempFile (te::editFileSuffix);
        editLoaded = session.openOrCreate (projectFile);

        setupControlBar();
        setupPlaceholders();

        addAndMakeVisible (controlBar);
        addAndMakeVisible (arrangeView);
        addAndMakeVisible (mixerPanel);
        addAndMakeVisible (browserPanel);
        addAndMakeVisible (drawerPanel);
        addAndMakeVisible (statusLabel);

        if (mode == SelfTest::playback)
            importTestToneAndPlay();

        controlBar.setEdit (session.getEdit());
        arrangeView.setEdit (session.getEdit());
        setViewMode (ViewMode::Arrange);

        setSize (1040, 620);

        if (mode == SelfTest::record)
        {
            MessageManager::callAsync ([this] { beginSelfTestRecording(); });
            startTimer (1600);
        }
        else if (mode == SelfTest::playback)
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
        g.fillAll (Colour (ForgeLookAndFeel::shellBg));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        controlBar.setBounds (r.removeFromTop (46));
        statusLabel.setBounds (r.removeFromBottom (24));

        auto work = r;

        browserPanel.setVisible (browserVisible);
        if (browserVisible)
            browserPanel.setBounds (work.removeFromLeft (220));

        auto centre = work;
        drawerPanel.setVisible (drawerVisible);
        if (drawerVisible)
            drawerPanel.setBounds (centre.removeFromBottom (160));

        arrangeView.setVisible (viewMode == ViewMode::Arrange);
        mixerPanel.setVisible (viewMode == ViewMode::Mixer);
        arrangeView.setBounds (centre);
        mixerPanel.setBounds (centre);
    }

private:
    te::Engine& engine;
    ProjectSession session;
    RecordController recorder;
    SelfTest mode = SelfTest::none;
    bool editLoaded = false;

    te::WaveAudioClip::Ptr clip;
    File sineFile;

    int  rcInputDeviceCount = 0;
    bool rcTrackArmed = false;
    bool rcRecordingStarted = false;
    String rcAvailableInputs;
    int  rcCurrentDeviceInputs = 0;
    String rcDiagTypes;

    ControlBar controlBar;
    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };
    Label browserPanel, drawerPanel, mixerPanel;
    Label statusLabel;
    std::unique_ptr<FileChooser> fileChooser;
    TooltipWindow tooltipWindow;

    ViewMode viewMode = ViewMode::Arrange;
    bool browserVisible = false;   // lean default; toggled on demand
    bool drawerVisible  = false;

    //==============================================================================
    void setupControlBar()
    {
        controlBar.onNew           = [this] { swapProject ([this] { session.newProject (uniqueUntitledFile()); }); };
        controlBar.onOpen          = [this] { openDialog(); };
        controlBar.onSave          = [this] { session.save(); };
        controlBar.onSaveAs        = [this] { saveAsDialog(); };
        controlBar.onImport        = [this] { importDialog(); };
        controlBar.onAudioSettings = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        controlBar.onToggleBrowser = [this] { browserVisible = ! browserVisible; resized(); };
        controlBar.onToggleDrawer  = [this] { drawerVisible  = ! drawerVisible;  resized(); };
        controlBar.onViewMode      = [this] (int m) { setViewMode (m == 1 ? ViewMode::Mixer : ViewMode::Arrange); };
    }

    void setupPlaceholders()
    {
        auto style = [] (Label& l, const String& text)
        {
            l.setText (text, dontSendNotification);
            l.setJustificationType (Justification::centred);
            l.setColour (Label::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
            l.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
        };

        style (browserPanel, "Browser\n(files, instruments, plug-ins) — Phase 3");
        style (drawerPanel,  "Detail editor — double-click a clip\n(audio editor, then piano-roll) — Phase 4");
        style (mixerPanel,   "Mixer view\n(channel strips, sends, meters) — Phase 5");

        statusLabel.setColour (Label::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
        statusLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
        statusLabel.setJustificationType (Justification::centredLeft);
        statusLabel.setBorderSize ({ 0, 10, 0, 10 });
    }

    void setViewMode (ViewMode m)
    {
        viewMode = m;
        controlBar.setViewMode (m == ViewMode::Mixer ? 1 : 0);
        resized();
    }

    //==============================================================================
    void openDialog()
    {
        fileChooser = std::make_unique<FileChooser> ("Open project...", File(),
                                                     "*" + String (te::editFileSuffix));
        fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f.existsAsFile())
                                          swapProject ([this, f] { session.openProject (f); });
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
               << "inputDeviceCount="       << rcInputDeviceCount << newLine
               << "trackArmed="             << (rcTrackArmed ? 1 : 0) << newLine
               << "recordingStarted="       << (rcRecordingStarted ? 1 : 0) << newLine
               << "recordedClipCount="      << recordedClips << newLine
               << "recordedFileExists="     << (recFileExists ? 1 : 0) << newLine
               << "recordedClipLengthSecs=" << String (recLen, 3) << newLine
               << "recordError="            << recorder.getLastError() << newLine
               << "result="                 << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    void swapProject (std::function<void()> doSwap)
    {
        controlBar.setEdit (nullptr);
        arrangeView.setEdit (nullptr);
        doSwap();
        clip = nullptr;
        rebind();
    }

    void rebind()
    {
        controlBar.setEdit (session.getEdit());
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

        statusLabel.setText (session.getEditFile().getFileName()
                                 + "   ·   " + String (session.getNumAudioTracks()) + " track"
                                 + (session.getNumAudioTracks() == 1 ? "" : "s")
                                 + "   ·   " + describeAudioState()
                                 + (playing ? "   ·   playing" : ""),
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
                          && numClipComps >= 1;

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
               << "transportReadout="  << (controlBar.getTransportBar().readoutIsNonEmpty() ? 1 : 0) << newLine
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
        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto mode = commandLine.contains ("--selftest-record") ? SelfTest::record
                        : commandLine.contains ("--selftest")        ? SelfTest::playback
                                                                     : SelfTest::none;
        mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, mode), *this));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

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
            setResizeLimits (760, 480, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    ForgeLookAndFeel lookAndFeel;
    te::Engine engine { "Forge" };
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
