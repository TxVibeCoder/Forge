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
/*  ResizerBar — a thin draggable handle sitting at the edge of a collapsible panel.
    Dragging it reports the desired new panel size (in pixels) back through onResize,
    clamped to [minSize, maxSize]. Vertical=true gives a left/right (width) handle for
    the Browser; vertical=false gives an up/down (height) handle for the Detail-drawer.

    Message-thread only. Owns no layout state itself: the shell holds the size members,
    re-reads them in resized(), and this bar just nudges them via the callback.            */
class ResizerBar : public Component
{
public:
    ResizerBar (bool isVertical, int minPx, int maxPx)
        : vertical (isVertical), minSize (minPx), maxSize (maxPx)
    {
        setMouseCursor (vertical ? MouseCursor::LeftRightResizeCursor
                                 : MouseCursor::UpDownResizeCursor);
    }

    // Called on each drag with the clamped desired size; the shell stores it and re-lays-out.
    std::function<void (int)> onResize;

    // The shell tells us the panel's current size when a drag begins (via mouseDown reading it).
    std::function<int()> getCurrentSize;

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (ForgeLookAndFeel::hairline));
        if (isMouseOverOrDragging())
        {
            g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (0.6f));
            g.fillRect (getLocalBounds());
        }
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit  (const MouseEvent&) override { repaint(); }

    void mouseDown (const MouseEvent&) override
    {
        sizeAtDragStart = getCurrentSize ? getCurrentSize() : 0;
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        // A left/right (vertical) handle grows the panel as the mouse moves right; an
        // up/down handle for a bottom drawer grows the panel as the mouse moves UP, hence
        // the negated Y delta.
        const int delta = vertical ? e.getDistanceFromDragStartX()
                                   : -e.getDistanceFromDragStartY();
        const int wanted = jlimit (minSize, maxSize, sizeAtDragStart + delta);
        if (onResize)
            onResize (wanted);
    }

    void mouseUp (const MouseEvent&) override { repaint(); }

private:
    bool vertical = true;
    int  minSize = 0, maxSize = 0;
    int  sizeAtDragStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResizerBar)
};

//==============================================================================
class MainComponent : public Component,
                      private Timer
{
public:
    MainComponent (te::Engine& e, SelfTest testMode)
        : engine (e), session (e), recorder (e), mode (testMode)
    {
        EngineHelpers::initialiseAudioForRecording (engine);   // open input channels too (for recording)

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
        addAndMakeVisible (browserResizer);
        addAndMakeVisible (drawerResizer);
        addAndMakeVisible (statusLabel);

        setupResizers();

        // The shell handles app-wide shortcuts. Child buttons/views can still grab focus;
        // keyPressed returns false for keys it doesn't consume so they keep propagating.
        setWantsKeyboardFocus (true);

        if (mode == SelfTest::playback)
            importTestToneAndPlay();

        controlBar.setEdit (session.getEdit());
        arrangeView.setEdit (session.getEdit());

        // Persist structural edits made via the arrange view's context menus / lane controls
        // (add/delete/rename track, delete clip, colour, mute/solo). ArrangeView fires this after
        // it has already rebuilt itself, so we only need to save. (onClipSelected/onTrackSelected
        // -> Inspector are left unwired until that feature exists — see docs/devlog/integration.md.)
        arrangeView.onEditMutated = [this] { session.save(); };

        // The lane R button toggles its visual state optimistically, then asks us to arm/disarm
        // the real input. If the record path rejects it (e.g. no capture device on this box), push
        // the true state back so the lane doesn't show a misleading "armed" tint, and report why.
        arrangeView.onArmToggled = [this] (te::AudioTrack& track, bool arm)
        {
            auto* ed = session.getEdit();
            if (ed == nullptr)
                return;

            const bool ok = arm ? recorder.armFirstInputToTrack (*ed, track)
                                : recorder.disarmTrack (*ed, track);

            if (! ok)
            {
                arrangeView.setTrackArmState (track, ! arm);   // revert the optimistic toggle
                statusLabel.setText ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError(),
                                     dontSendNotification);
            }
        };

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

        // Browser (left): panel + a thin draggable resizer hugging its right edge.
        browserPanel.setVisible (browserVisible);
        browserResizer.setVisible (browserVisible);
        if (browserVisible)
        {
            const int w = jlimit (browserMinWidth, browserMaxWidth, browserWidth);
            browserPanel.setBounds (work.removeFromLeft (w));
            browserResizer.setBounds (work.removeFromLeft (resizerThickness));
        }

        auto centre = work;

        // Detail-drawer (bottom): resizer hugging its top edge, then the panel below it.
        drawerPanel.setVisible (drawerVisible);
        drawerResizer.setVisible (drawerVisible);
        if (drawerVisible)
        {
            const int h = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);
            drawerPanel.setBounds (centre.removeFromBottom (h));
            drawerResizer.setBounds (centre.removeFromBottom (resizerThickness));
        }

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

    // Each async file dialog owns its own FileChooser so open/save-as can't stomp each other.
    // (Import uses EngineHelpers::browseForAudioFile, which manages its own shared chooser.)
    std::unique_ptr<FileChooser> openChooser, saveChooser;
    TooltipWindow tooltipWindow;

    ViewMode viewMode = ViewMode::Arrange;
    bool browserVisible = false;   // lean default; toggled on demand
    bool drawerVisible  = false;

    // Region sizes are mutable (drag-to-resize). Not persisted across launches yet (future work).
    static constexpr int resizerThickness = 5;
    int browserWidth = 220, browserMinWidth = 140, browserMaxWidth = 560;
    int drawerHeight = 160, drawerMinHeight = 90,  drawerMaxHeight = 420;

    ResizerBar browserResizer { true,  browserMinWidth, browserMaxWidth };   // vertical handle (width)
    ResizerBar drawerResizer  { false, drawerMinHeight, drawerMaxHeight };   // horizontal handle (height)

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

    void setupResizers()
    {
        browserResizer.getCurrentSize = [this] { return browserWidth; };
        browserResizer.onResize       = [this] (int w) { browserWidth = w; resized(); };

        drawerResizer.getCurrentSize  = [this] { return drawerHeight; };
        drawerResizer.onResize        = [this] (int h) { drawerHeight = h; resized(); };
    }

    //==============================================================================
    bool keyPressed (const KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();

        // For letter keys the keyCode is the (case-insensitive) character itself, which stays
        // reliable even with modifiers held — unlike getTextCharacter(), which becomes a control
        // char under Ctrl on some platforms. Normalise to upper case for comparison.
        const auto letter = (juce_wchar) CharacterFunctions::toUpperCase ((juce_wchar) code);

        // Ctrl/Cmd file commands first (isCommandDown() == Ctrl on Windows).
        if (mods.isCommandDown())
        {
            if (letter == 'S')
            {
                if (mods.isShiftDown())  { if (controlBar.onSaveAs) controlBar.onSaveAs(); }
                else                     { if (controlBar.onSave)   controlBar.onSave(); }
                return true;
            }
            if (letter == 'O') { if (controlBar.onOpen)   controlBar.onOpen();   return true; }
            if (letter == 'N') { if (controlBar.onNew)    controlBar.onNew();    return true; }
            if (letter == 'I') { if (controlBar.onImport) controlBar.onImport(); return true; }

            return false;   // unrecognised Ctrl combo: let it propagate
        }

        // Un-modified shortcuts.
        if (code == KeyPress::F9Key)  { setViewMode (ViewMode::Arrange); return true; }
        if (code == KeyPress::F11Key) { setViewMode (ViewMode::Mixer);   return true; }

        if (code == KeyPress::spaceKey)
        {
            if (auto* ed = session.getEdit())
                EngineHelpers::togglePlay (*ed);
            return true;
        }

        if (letter == 'B') { browserVisible = ! browserVisible; resized(); return true; }
        if (letter == 'E') { drawerVisible  = ! drawerVisible;  resized(); return true; }
        if (letter == 'R') { toggleRecordTake();                           return true; }

        return false;   // unhandled: propagate to other handlers
    }

    //==============================================================================
    void openDialog()
    {
        openChooser = std::make_unique<FileChooser> ("Open project...", File(),
                                                     "*" + String (te::editFileSuffix));
        Component::SafePointer<MainComponent> safeThis (this);
        openChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const FileChooser& fc)
                                  {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr)
                                          return;   // dialog outlived the shell: no-op

                                      const auto f = fc.getResult();
                                      if (f.existsAsFile())
                                          self->swapProject ([self, f] { self->session.openProject (f); });
                                  });
    }

    void saveAsDialog()
    {
        saveChooser = std::make_unique<FileChooser> ("Save project as...", File(),
                                                     "*" + String (te::editFileSuffix));
        Component::SafePointer<MainComponent> safeThis (this);
        saveChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const FileChooser& fc)
                                  {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr)
                                          return;   // dialog outlived the shell: no-op

                                      auto f = fc.getResult();
                                      if (f == File())
                                          return;
                                      if (f.getFileExtension().isEmpty())
                                          f = f.withFileExtension (te::editFileSuffix);

                                      // saveAs assigns its file only on success; only rebind if it worked.
                                      if (self->session.saveAs (f))
                                          self->rebind();
                                      else
                                          self->statusLabel.setText ("Save As failed: " + f.getFullPathName(),
                                                                     dontSendNotification);
                                  });
    }

    void importDialog()
    {
        // browseForAudioFile owns its own shared FileChooser, so importChooser isn't used here;
        // we still guard `this` since the callback fires asynchronously after the file is picked.
        Component::SafePointer<MainComponent> safeThis (this);
        EngineHelpers::browseForAudioFile (engine, [safeThis] (const File& f)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;   // dialog outlived the shell: no-op

            self->clip = self->session.importAudioFile (f, te::TimePosition());
            self->session.save();
            self->rebind();
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
