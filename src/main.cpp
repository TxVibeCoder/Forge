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
#include "services/export/Exporter.h"
#include "engine/EngineHelpers.h"
#include "engine/RecordController.h"
#include "engine/PluginScanner.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/mixer/MixerView.h"
#include "ui/plugins/PluginWindow.h"
#include "ui/browser/BrowserView.h"
#include "ui/detail/DetailView.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/session/SessionView.h"
#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"

namespace te = tracktion;
using namespace juce;

enum class SelfTest { none, playback, record, session, screenshot };
enum class ViewMode { Session, Arrange, Mixer };
enum class BottomMode { Detail, PianoRoll };   // which editor fills the bottom drawer region

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
        // No audio init here: the engine was constructed output-only (ForgeEngineBehaviour::
        // shouldOpenAudioInputByDefault() == false), so startup never opens a capture device.
        // Recording inputs open lazily, on the first arm/record, via ensureRecordingInputOpen().

        // Self-tests use a fresh, isolated temp project so clip/track counts are deterministic
        // and never polluted by leftovers in the persistent Untitled project.
        const auto projectFile = (mode == SelfTest::none) ? defaultProjectFile()
                                                          : File::createTempFile (te::editFileSuffix);
        editLoaded = session.openOrCreate (projectFile);

        setupControlBar();
        setupStatusStrip();

        addAndMakeVisible (controlBar);
        addAndMakeVisible (arrangeView);
        addAndMakeVisible (mixerView);
        addAndMakeVisible (sessionView);
        addAndMakeVisible (browserPanel);
        addAndMakeVisible (detailView);
        addAndMakeVisible (pianoRoll);
        addAndMakeVisible (browserResizer);
        addAndMakeVisible (drawerResizer);
        addAndMakeVisible (statusLabel);

        setupResizers();

        // The shell handles app-wide shortcuts. Child buttons/views can still grab focus;
        // keyPressed returns false for keys it doesn't consume so they keep propagating.
        setWantsKeyboardFocus (true);

        // (The playback selftest's import + play is kicked off from the message loop below, NOT here in
        // the ctor, so the audio graph is ready before it plays — see the SelfTest::playback dispatch.)

        controlBar.setEdit (session.getEdit());

        // Wire the arrange-view callbacks BEFORE setEdit, so the first rebuild() can already query
        // engine arm state when it builds the lanes.

        // Persist structural edits made via the arrange view's context menus / lane controls
        // (add/delete/rename track, delete clip, colour, mute/solo). ArrangeView fires this after
        // it has already rebuilt itself, so we only need to save. (onClipSelected/onTrackSelected
        // -> Inspector are left unwired until that feature exists — see docs/devlog/integration.md.)
        arrangeView.onEditMutated = [this]
        {
            session.save();

            // A structural change in the Arrange view (e.g. delete track) can leave the Session grid's
            // columns holding stale te::AudioTrack& refs. Rebuild it synchronously when the track count
            // changed so a TrackColumnComponent never outlives its track (QC blocker fix; the 25 Hz poll
            // also guards against any path that doesn't route through here).
            if (sessionViewBinds() && session.getNumAudioTracks() != sessionView.getNumColumns())
                sessionView.rebuild();
        };

        // Authoritative arm state lives in the engine (the InputDeviceInstance targets), so the
        // lanes re-derive their R indicator from this on every rebuild and after each arm/disarm
        // rather than trusting a transient local flag that resets on rebuild.
        arrangeView.isTrackArmed = [this] (te::AudioTrack& t)
        {
            auto* ed = session.getEdit();
            return ed != nullptr && recorder.isTrackArmed (*ed, t);
        };

        // The lane R button toggles optimistically, then asks us to arm/disarm the real input.
        // Afterwards we re-derive EVERY lane's indicator from engine truth: that corrects a
        // rejected toggle (e.g. no capture device on this box), and clears a previously-armed lane
        // whose single physical input was just stolen by arming another track.
        arrangeView.onArmToggled = [this] (te::AudioTrack& track, bool arm)
        {
            if (auto* ed = session.getEdit())
            {
                if (arm)
                {
                    // Open + enable a capture input on demand (kept off the startup path). May
                    // briefly block on the first arm if the OS is slow to open the device.
                    EngineHelpers::ensureRecordingInputOpen (engine);
                    recorder.enableInputs();
                }

                const bool ok = arm ? recorder.armFirstInputToTrack (*ed, track)
                                    : recorder.disarmTrack (*ed, track);

                if (! ok)
                    setStatusMessage ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());

                arrangeView.refreshArmStates();
            }
        };

        // Selecting a clip in the arrange view drives the bottom drawer and pops it open: a MIDI clip
        // opens the PianoRoll editor, any other clip opens the DetailView inspector. resized() is
        // called unconditionally so switching between an audio and a MIDI clip swaps which editor
        // fills the drawer even when it is already open. Edits in either are persisted.
        arrangeView.onClipSelected = [this] (te::Clip* c)
        {
            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            {
                pianoRoll.setMidiClip (mc);
                bottomMode = BottomMode::PianoRoll;
            }
            else
            {
                detailView.setClip (c);
                bottomMode = BottomMode::Detail;
            }

            if (c != nullptr && ! drawerVisible)
                drawerVisible = true;

            resized();
        };
        detailView.onEditMutated = [this] { session.save(); };
        pianoRoll.onEditMutated  = [this] { session.save(); };

        // "New MIDI Clip" from a lane context menu: create the clip (born audible via a default 4OSC),
        // rebuild the arrange surface so its MidiClipComponent appears, then open the piano-roll on it
        // ready to draw. The lane menu supplies the track index and a (snapped) start time; we give the
        // clip a default 4-bar length built from the tempo sequence.
        arrangeView.onCreateMidiClipRequested = [this] (int trackIndex, te::TimePosition start)
        {
            auto* ed = session.getEdit();
            if (ed == nullptr)
                return;

            const auto startBeat = ed->tempoSequence.toBeats (start);
            const auto endTime   = ed->tempoSequence.toTime (startBeat + te::BeatDuration::fromBeats (16.0));

            if (auto mc = session.createMidiClip (trackIndex, { start, endTime }, "MIDI"))
            {
                session.save();
                arrangeView.rebuild();

                pianoRoll.setMidiClip (mc.get());
                bottomMode    = BottomMode::PianoRoll;
                drawerVisible = true;
                resized();
            }
        };

        // Double-clicking an audio file in the Browser imports it onto the project.
        browserPanel.onImportFile = [this] (const File& f)
        {
            clip = session.importAudioFile (f, te::TimePosition());
            session.save();
            rebind();
        };

        // Session grid — mirror the arrange wiring. The view delegates all engine ops to ProjectSession;
        // here we only route selection/creation to the shared piano-roll drawer and the record arm path.
        sessionView.onEditMutated = [this] { session.save(); };

        sessionView.onSlotSelected = [this] (te::Clip* c)
        {
            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            {
                pianoRoll.setMidiClip (mc);
                bottomMode = BottomMode::PianoRoll;
            }
            else
            {
                detailView.setClip (c);
                bottomMode = BottomMode::Detail;
            }

            if (c != nullptr && ! drawerVisible)
                drawerVisible = true;

            resized();
        };

        // The view already created + saved the new clip and rebuilt itself; we only open it for editing.
        sessionView.onMidiClipCreated = [this] (te::MidiClip::Ptr clip)
        {
            if (clip != nullptr)
            {
                pianoRoll.setMidiClip (clip.get());
                bottomMode    = BottomMode::PianoRoll;
                drawerVisible = true;
                resized();
            }
        };

        sessionView.isTrackArmed = [this] (te::AudioTrack& t)
        {
            auto* ed = session.getEdit();
            return ed != nullptr && recorder.isTrackArmed (*ed, t);
        };

        sessionView.onArmToggled = [this] (te::AudioTrack& track, bool arm)
        {
            if (auto* ed = session.getEdit())
            {
                if (arm)
                {
                    EngineHelpers::ensureRecordingInputOpen (engine);
                    recorder.enableInputs();
                }

                const bool ok = arm ? recorder.armFirstInputToTrack (*ed, track)
                                    : recorder.disarmTrack (*ed, track);

                if (! ok)
                    setStatusMessage ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());

                sessionView.refreshArmStates();
                arrangeView.refreshArmStates();
            }
        };

        arrangeView.setEdit (session.getEdit());
        mixerView.setEdit (session.getEdit());

        // The Session grid is the default view. Keep it OFF the headless playback/record selftest path so
        // the throwaway selftest edit stays pristine (no ensureScenes slot scaffolding perturbs the gates).
        if (sessionViewBinds())
            sessionView.setEdit (session.getEdit());

        setViewMode (ViewMode::Session);

        if (mode == SelfTest::screenshot)
            setSize (1480, 940);   // tall enough to render all 16 scene rows for the snapshot
        else
            setSize (1040, 620);

        if (mode == SelfTest::record)
        {
            // Event-driven so it mirrors the real app: open the input, then YIELD to the message
            // loop (timer below) so Tracktion's async wave-device-list rebuild actually delivers
            // before we arm. A single blocking callback would never let that async run.
            MessageManager::callAsync ([this] { beginSelfTestRecording(); });
        }
        else if (mode == SelfTest::session)
        {
            // Yield to the message loop, then create + launch a clip in a slot and confirm it
            // becomes audible (the launcher playback path engages). See finishSessionSelftest().
            MessageManager::callAsync ([this] { beginSessionSelftest(); });
        }
        else if (mode == SelfTest::screenshot)
        {
            // Build a populated demo session, then snapshot each view to a PNG (see captureScreenshots).
            MessageManager::callAsync ([this] { beginScreenshot(); });
        }
        else if (mode == SelfTest::playback)
        {
            // Run the import + play AFTER yielding to the message loop (mirrors the record/session
            // selftests), then poll for playback to actually engage. Front-loading this in the ctor and
            // sampling at a blind fixed time raced a hot-swapped default output device — the IO callback
            // stays suspended until the device-change async cascade drains, so the playhead never moved.
            MessageManager::callAsync ([this] { importTestToneAndPlay(); playbackPollTicks = 0; startTimerHz (10); });
        }
        else
            startTimerHz (5);
    }

    ~MainComponent() override
    {
        PluginWindow::closeAll();   // close any floating plugin editors before the Edit tears down

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

        // Bottom drawer: either the audio-clip DetailView or the MIDI PianoRoll fills it, chosen by
        // bottomMode (the arrange selection routes to one or the other). A resizer hugs its top edge;
        // the inactive editor is hidden. The piano-roll scrolls its 128 pitch rows internally via its
        // own Viewport, so the drawer keeps the same 90..420px clamp regardless of mode.
        const bool showDetail    = drawerVisible && bottomMode == BottomMode::Detail;
        const bool showPianoRoll = drawerVisible && bottomMode == BottomMode::PianoRoll;
        detailView.setVisible (showDetail);
        pianoRoll.setVisible  (showPianoRoll);
        drawerResizer.setVisible (drawerVisible);
        if (drawerVisible)
        {
            const int h = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);
            const auto drawerArea = centre.removeFromBottom (h);
            if (showPianoRoll) pianoRoll.setBounds (drawerArea);
            else               detailView.setBounds (drawerArea);
            drawerResizer.setBounds (centre.removeFromBottom (resizerThickness));
        }

        sessionView.setVisible (viewMode == ViewMode::Session);
        arrangeView.setVisible (viewMode == ViewMode::Arrange);
        mixerView.setVisible (viewMode == ViewMode::Mixer);
        sessionView.setBounds (centre);
        arrangeView.setBounds (centre);
        mixerView.setBounds (centre);
    }

private:
    te::Engine& engine;
    ProjectSession session;
    RecordController recorder;
    SelfTest mode = SelfTest::none;
    bool editLoaded = false;

    te::WaveAudioClip::Ptr clip;
    File sineFile;

    te::MidiClip::Ptr ssClip;       // session selftest: the MIDI clip launched in slot (0,0)
    bool ssClipCreated = false;     // session selftest: whether that clip was created

    int  rcInputDeviceCount = 0;
    bool rcTrackArmed = false;
    bool rcRecordingStarted = false;
    String rcAvailableInputs;  // OS-visible capture endpoint names (diagnostic)
    String rcOpenError;        // setAudioDeviceSetup() error from ensureRecordingInputOpen()
    String rcDeviceAfter;      // open audio device name AFTER the lazy input-open attempt
    int  rcInputChansAfter = 0;// active input channels on that device AFTER the attempt
    int  recordPhase = 0;      // record-selftest state machine: 1 = input opened, 2 = recording
    int  playbackPollTicks = 0;// playback-selftest bounded poll: 10 Hz ticks elapsed

    ControlBar controlBar;
    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };
    MixerView mixerView;
    SessionView sessionView { session };   // primary view: tracks×scenes clip-launch grid (Sheet 00)
    BrowserView browserPanel;   // left region: real file browser (name kept for layout call sites)
    DetailView  detailView;     // bottom drawer: audio-clip inspector
    PianoRollView pianoRoll { timelineView };   // bottom drawer: MIDI-clip editor (shares the time axis)
    Label statusLabel;
    juce::uint32 statusHoldUntilMs = 0;   // transient status messages survive the 5Hz refresh until this time

    // Each async file dialog owns its own FileChooser so open/save-as/export can't stomp each other.
    // (Import uses EngineHelpers::browseForAudioFile, which manages its own shared chooser.)
    std::unique_ptr<FileChooser> openChooser, saveChooser, exportChooser, stemsChooser;
    TooltipWindow tooltipWindow;

    ViewMode viewMode = ViewMode::Session;
    BottomMode bottomMode = BottomMode::Detail;   // Detail (audio) vs PianoRoll (MIDI) in the drawer
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
        controlBar.onExport        = [this] { exportDialog(); };
        controlBar.onExportStems   = [this] { exportStemsDialog(); };
        controlBar.onScanPlugins   = [this] { PluginScanner::showScanDialog (engine); };
        controlBar.onAudioSettings = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        controlBar.onToggleBrowser = [this] { browserVisible = ! browserVisible; resized(); };
        controlBar.onToggleDrawer  = [this] { drawerVisible  = ! drawerVisible;  resized(); };
        controlBar.onViewMode      = [this] (int m) { setViewMode (m == 0 ? ViewMode::Session
                                                                  : m == 1 ? ViewMode::Arrange
                                                                           : ViewMode::Mixer); };
    }

    void setupStatusStrip()
    {
        // browserPanel (BrowserView), detailView (DetailView) and mixerView are real components now
        // and paint their own empty state — no placeholder styling needed.
        statusLabel.setColour (Label::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
        statusLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
        statusLabel.setJustificationType (Justification::centredLeft);
        statusLabel.setBorderSize ({ 0, 10, 0, 10 });
    }

    // Whether this run binds the live Session grid to the edit. The headless playback/record selftests
    // skip it to keep their throwaway edit pristine; the interactive app, the session selftest, and the
    // screenshot harness all bind it. Used identically at initial bind, rebind, and on track-list change.
    bool sessionViewBinds() const
    {
        return mode == SelfTest::none || mode == SelfTest::session || mode == SelfTest::screenshot;
    }

    void setViewMode (ViewMode m)
    {
        viewMode = m;
        controlBar.setViewMode (m == ViewMode::Session ? 0 : m == ViewMode::Arrange ? 1 : 2);
        resized();

        // Give the Session grid keyboard focus when it becomes active so its arrow/Enter launch keys fire
        // (the switch only toggles visibility; SessionView::visibilityChanged also covers this) — QC fix.
        if (m == ViewMode::Session)
            sessionView.grabKeyboardFocus();
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
        if (code == KeyPress::F8Key)  { setViewMode (ViewMode::Session); return true; }
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
                                          self->setStatusMessage ("Save As failed: " + f.getFullPathName());
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

    void exportDialog()
    {
        if (session.getEdit() == nullptr)
            return;

        const auto suggested = File::getSpecialLocation (File::userMusicDirectory)
                                   .getChildFile (session.getEditFile().getFileNameWithoutExtension() + ".wav");

        exportChooser = std::make_unique<FileChooser> ("Export to WAV...", suggested, "*.wav");
        Component::SafePointer<MainComponent> safeThis (this);
        exportChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                                    [safeThis] (const FileChooser& fc)
                                    {
                                        auto* self = safeThis.getComponent();
                                        if (self == nullptr)
                                            return;

                                        auto f = fc.getResult();
                                        if (f == File())
                                            return;
                                        if (f.getFileExtension().isEmpty())
                                            f = f.withFileExtension ("wav");

                                        auto* edit = self->session.getEdit();
                                        if (edit == nullptr)
                                            return;

                                        self->setStatusMessage ("Exporting " + f.getFileName() + "...");

                                        String err;
                                        const bool ok = Exporter::renderEditToWav (*edit, f, err);

                                        self->setStatusMessage (ok ? "Exported " + f.getFileName()
                                                                   : "Export failed: " + err);
                                    });
    }

    void exportStemsDialog()
    {
        if (session.getEdit() == nullptr)
            return;

        const auto suggested = File::getSpecialLocation (File::userMusicDirectory)
                                   .getChildFile (session.getEditFile().getFileNameWithoutExtension() + " Stems");

        stemsChooser = std::make_unique<FileChooser> ("Choose a folder for stems...", suggested);
        Component::SafePointer<MainComponent> safeThis (this);
        stemsChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                   [safeThis] (const FileChooser& fc)
                                   {
                                       auto* self = safeThis.getComponent();
                                       if (self == nullptr)
                                           return;

                                       const auto dir = fc.getResult();
                                       if (dir == File())
                                           return;

                                       auto* edit = self->session.getEdit();
                                       if (edit == nullptr)
                                           return;

                                       self->setStatusMessage ("Exporting stems to " + dir.getFileName() + "...");

                                       String err;
                                       const bool ok = Exporter::renderStems (*edit, dir, err);

                                       self->setStatusMessage (ok ? (err.isEmpty() ? "Exported stems to " + dir.getFileName()
                                                                                    : "Exported stems (some failed): " + err)
                                                                  : "Stem export failed: " + err);
                                   });
    }

    void importTestToneAndPlay()
    {
        sineFile = createSineWaveFile (44100.0, 30.0, 440.0, 0.2f);
        clip = session.importAudioFile (sineFile, te::TimePosition());

        if (clip != nullptr)
        {
            // We now run on the message loop (after the initial setEdit), so rebuild the arrange view to
            // reflect the freshly-imported clip (the report reads its clip-component count).
            arrangeView.rebuild();

            auto& transport = session.getEdit()->getTransport();
            transport.setLoopRange (clip->getEditTimeRange());
            transport.looping = true;
            transport.ensureContextAllocated();

            // Drain the device-change async cascade so the output stream is actually rolling before we
            // play — the output-side analog of the input flush the record path uses. Without this, a
            // just-hot-swapped default device (a headset unplug falling back to onboard audio) leaves the
            // IO callback suspended, so the playhead never advances (position stays 0.000).
            engine.getDeviceManager().dispatchPendingUpdates();

            transport.setPosition (te::TimePosition());
            transport.play (false);

            // Wait for the stream to actually be rolling (the exact idiom the clip-launcher path uses;
            // blockUntilSyncPointChange bails after ~100ms).
            if (auto* epc = transport.getCurrentPlaybackContext())
                epc->blockUntilSyncPointChange();
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
            EngineHelpers::ensureRecordingInputOpen (engine);   // lazily open the capture input
            recorder.enableInputs();
            if (auto* track = te::getAudioTracks (*edit)[0])
                recorder.armFirstInputToTrack (*edit, *track);
            t->record (false);
        }
    }

    // Phase 1: open the capture input lazily (exactly as the real arm path does), enable inputs,
    // then hand back to the message loop. The wave-device-list rebuild Tracktion schedules here is
    // asynchronous, so we must NOT arm in this same callback — we yield (timer) and arm in phase 2.
    void beginSelfTestRecording()
    {
        if (session.getEdit() == nullptr)
            return;

        auto& jdm = engine.getDeviceManager().deviceManager;
        if (auto* type = jdm.getCurrentDeviceTypeObject())
            rcAvailableInputs = type->getDeviceNames (true).joinIntoString ("|");

        rcOpenError = EngineHelpers::ensureRecordingInputOpen (engine);   // record selftest opens input lazily too
        recorder.enableInputs();

        if (auto* dev = jdm.getCurrentAudioDevice())
        {
            rcDeviceAfter     = dev->getName();
            rcInputChansAfter = dev->getActiveInputChannels().countNumberOfSetBits();
        }

        // Yield so the posted wave-device-list rebuild is delivered before we read the input count.
        recordPhase = 1;
        startTimer (200);
    }

    // Phase 2: by now the async wave-device-list rebuild has run, so the engine exposes the wave
    // inputs. Arm track 0 and roll. Phase 3 (the capture window) is started by the caller's timer.
    void armAndStartRecording()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr)
            return;

        rcInputDeviceCount = recorder.getInputDeviceCount();

        if (auto* track = te::getAudioTracks (*edit)[0])
            rcTrackArmed = recorder.armFirstInputToTrack (*edit, *track);

        if (auto* t = session.getTransport())
        {
            t->record (false);
            rcRecordingStarted = t->isRecording();
        }
    }

    // Peak absolute sample value across all channels of an audio file, or -1 if it can't be read.
    // Used only by the record selftest to confirm whether real signal flowed through the capture path.
    float readPeakMagnitude (const File& file) const
    {
        if (! file.existsAsFile())
            return -1.0f;

        auto& fmtManager = engine.getAudioFileFormatManager().readFormatManager;
        std::unique_ptr<AudioFormatReader> reader (fmtManager.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return -1.0f;

        const int numChans = jmax (1, (int) reader->numChannels);
        HeapBlock<Range<float>> ranges (numChans);
        reader->readMaxLevels (0, reader->lengthInSamples, ranges.get(), numChans);

        float peak = 0.0f;
        for (int ch = 0; ch < numChans; ++ch)
            peak = jmax (peak, jmax (std::abs (ranges[ch].getStart()), std::abs (ranges[ch].getEnd())));

        return peak;
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
        float recPeak = -1.0f;   // peak sample magnitude of the captured file (informational)

        if (auto* edit = session.getEdit())
            if (auto* track = te::getAudioTracks (*edit)[0])
                for (auto* c : track->getClips())
                    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                    {
                        ++recordedClips;
                        const auto recFile = wc->getSourceFileReference().getFile();
                        recFileExists = recFile.existsAsFile();
                        recLen = wc->getEditTimeRange().getLength().inSeconds();
                        recPeak = readPeakMagnitude (recFile);
                    }

        // PASS proves the record PATH end-to-end (device opened, track armed, transport rolled, a
        // clip of non-zero length was written). recPeak is reported but NOT gated on, because the
        // default capture endpoint on a given box may legitimately be silent.
        const bool pass = rcInputDeviceCount > 0 && rcTrackArmed && rcRecordingStarted
                          && recordedClips >= 1 && recFileExists && recLen > 0.0;

        String report;
        report << "mode=record" << newLine
               << "availableInputDevices="  << (rcAvailableInputs.isEmpty() ? String ("(none)") : rcAvailableInputs) << newLine
               << "openInputError="         << (rcOpenError.isEmpty() ? String ("(none)") : rcOpenError) << newLine
               << "deviceAfterOpen="        << (rcDeviceAfter.isEmpty() ? String ("(no device open)") : rcDeviceAfter) << newLine
               << "inputChansAfterOpen="    << rcInputChansAfter << newLine
               << "inputDeviceCount="       << rcInputDeviceCount << newLine
               << "trackArmed="             << (rcTrackArmed ? 1 : 0) << newLine
               << "recordingStarted="       << (rcRecordingStarted ? 1 : 0) << newLine
               << "recordedClipCount="      << recordedClips << newLine
               << "recordedFileExists="     << (recFileExists ? 1 : 0) << newLine
               << "recordedClipLengthSecs=" << String (recLen, 3) << newLine
               << "recordedPeakMagnitude="  << String (recPeak, 5) << newLine
               << "recordError="            << recorder.getLastError() << newLine
               << "result="                 << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // Session-grid audibility selftest (wave-1 acceptance gate): headlessly prove that launching a
    // clip in a slot is AUDIBLE — create a born-audible MIDI clip in slot (0,0), launch it (which
    // starts the transport), and confirm its LaunchHandle reaches the playing state with the
    // transport rolling, i.e. the playback graph engaged the launcher path.
    void beginSessionSelftest()
    {
        if (session.getEdit() == nullptr)
        {
            finishSessionSelftest();
            return;
        }

        session.ensureScenes (16);
        ssClip = session.createMidiClipInSlot (0, 0, "SelfTest");
        ssClipCreated = (ssClip != nullptr);

        if (ssClipCreated)
            session.launchSlot (0, 0);   // per-track exclusivity + starts the transport (audible)

        startTimer (1500);   // let the transport roll and the launch handle advance to 'playing'
    }

    void finishSessionSelftest()
    {
        bool transportPlaying = false, slotHasClip = false, hasLaunchHandle = false, clipPlaying = false;

        if (auto* t = session.getTransport())
            transportPlaying = t->isPlaying();

        if (auto* slot = session.getClipSlot (0, 0))
            if (auto* c = slot->getClip())
            {
                slotHasClip = true;
                if (auto lh = c->getLaunchHandle())
                {
                    hasLaunchHandle = true;
                    clipPlaying = lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
                }
            }

        if (auto* t = session.getTransport())
            t->stop (false, false);

        // PASS proves the launch PATH end-to-end: a born-audible clip created in a slot, launched,
        // with the transport rolling AND the clip's launch handle actually in the playing state.
        const bool pass = ssClipCreated && slotHasClip && hasLaunchHandle
                          && transportPlaying && clipPlaying
                          && session.getNumScenes() >= 16;

        String report;
        report << "mode=session" << newLine
               << "numScenes="        << session.getNumScenes() << newLine
               << "sessionColumns="   << sessionView.getNumColumns() << newLine
               << "clipCreated="      << (ssClipCreated ? 1 : 0) << newLine
               << "slotHasClip="      << (slotHasClip ? 1 : 0) << newLine
               << "hasLaunchHandle="  << (hasLaunchHandle ? 1 : 0) << newLine
               << "transportPlaying=" << (transportPlaying ? 1 : 0) << newLine
               << "clipPlaying="      << (clipPlaying ? 1 : 0) << newLine
               << "result="           << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // Headless screenshot mode (--screenshot): build a populated demo session and render each view to a
    // PNG via createComponentSnapshot, so the UI can be inspected without a live display. Writes
    // %TEMP%\forge_shot_{session,arrange,mix}.png then quits.
    void populateDemoSession()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr)
            return;

        edit->ensureNumberOfAudioTracks (6);
        session.ensureScenes (16);

        auto tracks = te::getAudioTracks (*edit);
        const char*  trackNames[] = { "Drums", "Bass", "Keys", "Lead", "Vox", "FX" };
        const uint32 trackCols[]  = { 0xffe0625c, 0xff5c9fe0, 0xff5ce08f, 0xffe0c25c, 0xffb86ce0, 0xff5cd6e0 };

        for (int i = 0; i < tracks.size() && i < 6; ++i)
        {
            tracks[i]->setName (trackNames[i]);
            tracks[i]->setColour (Colour (trackCols[i]));
        }

        // Scatter born-audible MIDI clips across slots so the grid reads as a real session.
        struct Cell { int track, scene; const char* name; };
        const Cell cells[] = {
            { 0, 0, "Beat A" }, { 0, 1, "Beat B" }, { 0, 3, "Fill" },
            { 1, 0, "Sub" },    { 1, 2, "Walk" },
            { 2, 1, "Pad" },    { 2, 3, "Stab" },   { 2, 4, "Chord" },
            { 3, 2, "Hook" },   { 3, 5, "Solo" },
            { 4, 0, "Verse" },  { 4, 3, "Chorus" },
            { 5, 4, "Riser" }
        };

        for (auto& c : cells)
            session.createMidiClipInSlot (c.track, c.scene, c.name);

        // Launch scene 3 so the snapshot shows playing pads + an active scene row.
        session.launchScene (3);
    }

    void beginScreenshot()
    {
        populateDemoSession();
        sessionView.rebuild();    // pick up the new tracks/clips into columns + pads
        startTimer (900);         // let the launched clips reach the playing state, then capture
    }

    void captureView (const String& name)
    {
        resized();
        auto image = createComponentSnapshot (getLocalBounds());

        auto file = File::getSpecialLocation (File::tempDirectory)
                        .getChildFile ("forge_shot_" + name + ".png");
        file.deleteFile();

        if (auto out = std::unique_ptr<FileOutputStream> (file.createOutputStream()))
        {
            PNGImageFormat png;
            png.writeImageToStream (image, *out);
        }
    }

    void captureScreenshots()
    {
        sessionView.refreshSlotStates();   // push current pad states (playing/hasClip) before snapping

        // The secondary views were bound before the demo tracks were added; re-bind so the Arrange/Mix
        // snapshots reflect all demo tracks (not just the original track 0).
        arrangeView.setEdit (nullptr);  arrangeView.setEdit (session.getEdit());
        mixerView.setEdit (nullptr);    mixerView.setEdit (session.getEdit());

        setViewMode (ViewMode::Session);   captureView ("session");
        setViewMode (ViewMode::Arrange);   captureView ("arrange");
        setViewMode (ViewMode::Mixer);     captureView ("mix");

        if (auto* t = session.getTransport())
            t->stop (false, false);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    void swapProject (std::function<void()> doSwap)
    {
        PluginWindow::closeAll();    // plugin editors belong to the outgoing Edit
        detailView.setClip (nullptr);
        pianoRoll.setMidiClip (nullptr);   // drop any MIDI clip held from the outgoing Edit
        bottomMode = BottomMode::Detail;
        controlBar.setEdit (nullptr);
        arrangeView.setEdit (nullptr);
        mixerView.setEdit (nullptr);
        sessionView.setEdit (nullptr);   // R4: stop the 25 Hz poll + drop state BEFORE the Edit is destroyed
        doSwap();
        clip = nullptr;
        rebind();
    }

    void rebind()
    {
        controlBar.setEdit (session.getEdit());
        arrangeView.setEdit (session.getEdit());
        mixerView.setEdit (session.getEdit());
        if (sessionViewBinds())
            sessionView.setEdit (session.getEdit());
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

    // Show a transient message in the status strip and shield it from the periodic 5Hz refresh
    // for a few seconds, so arm/disarm/save errors are actually readable instead of being wiped
    // within one 200ms tick.
    void setStatusMessage (const String& message)
    {
        statusLabel.setText (message, dontSendNotification);
        statusHoldUntilMs = Time::getMillisecondCounter() + 4000;
    }

    void timerCallback() override
    {
        if (mode == SelfTest::screenshot)
        {
            stopTimer();
            captureScreenshots();
            return;
        }

        if (mode == SelfTest::session)
        {
            stopTimer();
            finishSessionSelftest();
            return;
        }

        if (mode == SelfTest::record)
        {
            stopTimer();
            if (recordPhase == 1)
            {
                recordPhase = 2;
                armAndStartRecording();
                startTimer (1500);   // capture window, then phase 3 finishes + verifies
            }
            else
            {
                finishSelfTestRecording();
            }
            return;
        }

        auto* t = session.getTransport();
        const bool playing = t != nullptr && t->isPlaying();

        // Don't clobber a recently-set transient message (e.g. "Arm failed: ...").
        if (Time::getMillisecondCounter() >= statusHoldUntilMs)
            statusLabel.setText (session.getEditFile().getFileName()
                                     + "   ·   " + String (session.getNumAudioTracks()) + " track"
                                     + (session.getNumAudioTracks() == 1 ? "" : "s")
                                     + "   ·   " + describeAudioState()
                                     + (playing ? "   ·   playing" : ""),
                                 dontSendNotification);

        if (mode == SelfTest::playback)
        {
            // Bounded poll (10 Hz): finish as soon as playback has demonstrably engaged, or after ~3s,
            // so a slow-but-working device isn't failed by an unlucky fixed sample time.
            const bool engaged = t != nullptr && (t->isPlaying() || t->getPosition().inSeconds() > 0.05);
            if (engaged || ++playbackPollTicks >= 30)
            {
                stopTimer();
                writePlaybackReport (t != nullptr && t->isPlaying());
                JUCEApplication::getInstance()->systemRequestedQuit();
            }
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
/*  ForgeEngineBehaviour — keeps the capture INPUT off the startup path. The default
    EngineBehaviour::shouldOpenAudioInputByDefault() returns true, so te::Engine's constructor
    would open a default input device synchronously on the message thread — which can stall
    25–77 s when the system default device has just changed (e.g. a Bluetooth headset
    disconnected). Returning false makes the engine open OUTPUT only at startup; the recording
    input is opened lazily on the first arm/record via EngineHelpers::ensureRecordingInputOpen(). */
struct ForgeEngineBehaviour : te::EngineBehaviour
{
    bool shouldOpenAudioInputByDefault() override { return false; }
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

        const auto mode = commandLine.contains ("--screenshot")       ? SelfTest::screenshot
                        : commandLine.contains ("--selftest-record")  ? SelfTest::record
                        : commandLine.contains ("--selftest-session") ? SelfTest::session
                        : commandLine.contains ("--selftest")         ? SelfTest::playback
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
    te::Engine engine { "Forge", nullptr, std::make_unique<ForgeEngineBehaviour>() };
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
