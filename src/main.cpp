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
#include "engine/AutomationHelpers.h"
#include "engine/RecordController.h"
#include "engine/PluginScanner.h"
#include "engine/PluginHost.h"
#include "engine/Metronome.h"
#include "engine/MidiLearn.h"
#include "engine/ForgeUIBehaviour.h"
#include "engine/LaunchpadDriver.h"
#include "engine/ControlSurfaceHost.h"
#include "engine/MidiClockSync.h"
#include "engine/MidiClockProbe.h"
#include "engine/dsp/LoudnessAnalyzer.h"
#include "engine/dsp/InstrumentSamples.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/markers/MarkerBar.h"
#include "ui/mixer/MixerView.h"
#include "ui/plugins/PluginWindow.h"
#include "ui/popout/PopoutWindow.h"
#include "ui/browser/BrowserView.h"
#include "ui/detail/DetailView.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/session/SessionView.h"
#include "ui/session/SessionMixerStrip.h"
#include "ui/session/SlotVisualState.h"
#include "ui/export/ExportProgress.h"
#include "ui/ControlBar.h"
#include "ui/SplashWindow.h"
#include "ui/transport/TapTempo.h"
#include "ui/transport/LcdModel.h"
#include "ui/tray/ChannelTray.h"
#include "ui/menu/ForgeMenuModel.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

namespace te = tracktion;
using namespace juce;

enum class SelfTest { none, playback, record, session, screenshot, midi, midilearn, midiinput, controlsurface, automation, sync, livesync, tray, popout, undo, taptempo, slotdelete, addtrack, scene, dragdrop, sessionmixer, demo, sendarrange, followaction, launchmode };
enum class ViewMode { Session, Arrange, Mixer };
enum class BottomMode { Detail, PianoRoll };   // which editor fills the bottom drawer region
enum class SidebarMode { Browser, Channel };   // which panel fills the left sidebar band (W04a)

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

            if (! writerOwner->writeFromAudioSampleBuffer (buffer, 0, numSamples))
                FORGE_LOG_ERROR ("Failed to write audio samples to WAV file — I/O error or disk full");
        }
        else
        {
            FORGE_LOG_WARN ("Failed to create WAV writer for sine wave");
        }
    }
    else
    {
        FORGE_LOG_WARN ("Failed to create temp WAV file for sine wave (createOutputStream returned null)");
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
        // NOTE: openOrCreate() returns false in the NORMAL "created a new project" case, so a
        // false editLoaded is NOT a failure. The real failure is having no edit at all afterward.
        if (session.getEdit() == nullptr)
            FORGE_LOG_ERROR ("Failed to open or create project file: " + projectFile.getFullPathName());

        // W09: first-launch welcome demo. When we CREATED a fresh default project (editLoaded == false) in
        // the normal app (never a selftest), seed the audible demo — named tracks, per-track instrument
        // presets, and a 4-on-floor + bass + piano groove — so a brand-new user opens into a playable
        // session instead of a blank grid. It is IN-MEMORY only (not saved), does NOT auto-play, and File >
        // New still gives an empty project; once the user saves their own default the demo no longer appears.
        if (mode == SelfTest::none && ! editLoaded && session.getEdit() != nullptr)
            populateDemoContent();

        // Wire the engine's focused-Edit UIBehaviour to this session so real hardware CCs route to the
        // currently-open Edit's parameter mappings (ForgeUIBehaviour). getEdit() is re-queried on every
        // access, so a project swap needs no re-wiring. Cleared in ~MainComponent BEFORE the Engine
        // (which owns the behaviour) is destroyed. The static_cast is safe: the Engine was constructed
        // with a ForgeUIBehaviour as its UIBehaviour.
        static_cast<ForgeUIBehaviour&> (engine.getUIBehaviour()).setSession (&session);

        setupControlBar();
        setupMenuModel();   // shares setupControlBar's std::functions — must come after it (W04a)
        setupStatusStrip();

        addAndMakeVisible (controlBar);
        addAndMakeVisible (arrangeView);
        addAndMakeVisible (markerBar);
        addAndMakeVisible (mixerView);
        addAndMakeVisible (sessionView);
        addAndMakeVisible (browserPanel);
        addChildComponent (channelTray);   // shown by the sidebar's Channel tab (W04a)

        // Sidebar tabs (W04a): slim toggles across the top of the left band. Toggle state mirrors
        // sidebarMode in resized(); clicking just flips the mode and relays out.
        for (auto* tab : { &sidebarFilesTab, &sidebarChannelTab })
        {
            tab->setColour (TextButton::buttonColourId,   Colour (ForgeLookAndFeel::panelBg));
            tab->setColour (TextButton::buttonOnColourId, Colour (ForgeLookAndFeel::raisedBg));
            tab->setColour (TextButton::textColourOffId,  Colour (ForgeLookAndFeel::textSec));
            tab->setColour (TextButton::textColourOnId,   Colour (ForgeLookAndFeel::textPrim));
            addChildComponent (*tab);
        }
        sidebarFilesTab.setTooltip ("File browser");
        sidebarChannelTab.setTooltip ("Selected track's channel strip");
        sidebarFilesTab.onClick   = [this] { sidebarMode = SidebarMode::Browser; userPinnedBrowser = true;  resized(); };
        sidebarChannelTab.onClick = [this] { sidebarMode = SidebarMode::Channel; userPinnedBrowser = false; resized(); };

        addAndMakeVisible (detailView);
        addAndMakeVisible (pianoRoll);
        addAndMakeVisible (browserResizer);
        addAndMakeVisible (drawerResizer);
        addAndMakeVisible (statusLabel);
        addChildComponent (exportProgress);   // hidden until an async export starts (P4)

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
        // it has already rebuilt itself, so we only need to save. (onClipSelected routes the drawer
        // editors; onTrackSelected binds the W04a channel tray — both wired below.)
        arrangeView.onEditMutated = [this]
        {
            // W05: seal the finished gesture as ONE undo transaction (the engine's 350 ms timer
            // is the backstop for paths that bypass these hooks; beginNewTransaction on an empty
            // group is a harmless no-op, so double-fires are safe).
            if (auto* ed = session.getEdit())
                ed->getUndoManager().beginNewTransaction();

            if (! session.save())
                FORGE_LOG_ERROR ("Failed to save project — I/O error");

            // A structural change in the Arrange view (e.g. delete track) can leave the Session grid's
            // columns holding stale te::AudioTrack& refs. Rebuild it synchronously when the track count
            // changed so a TrackColumnComponent never outlives its track (QC blocker fix; the 25 Hz poll
            // also guards against any path that doesn't route through here).
            if (sessionViewBinds() && session.getNumAudioTracks() != sessionView.getNumColumns())
                sessionView.rebuild();

            // Re-validate the tray's bound track eagerly (it self-clears if the track was deleted);
            // the tray's own 10 Hz identity scan is the backstop for paths that bypass this hook.
            channelTray.refreshNow();
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
                {
                    setStatusMessage ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());
                    FORGE_LOG_ERROR ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());
                }

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
                revealDrawer();

            resized();
        };

        // Selecting a track in Arrange binds the channel tray (W04a). ArrangeView delivers a raw
        // te::Track pointer — deselection fires nullptr — so the bind dynamic_casts: anything
        // that isn't an AudioTrack clears the tray to its empty state. Selecting a track also
        // flips the sidebar to the Channel tab when the sidebar is open (the GarageBand reveal),
        // UNLESS the user explicitly parked it on Files (QC: never steal an explicit pane choice).
        arrangeView.onTrackSelected = [this] (te::Track* t)
        {
            auto* at = dynamic_cast<te::AudioTrack*> (t);
            channelTray.setTrack (at);

            if (at != nullptr && browserVisible && ! userPinnedBrowser
                && sidebarMode != SidebarMode::Channel)
            {
                sidebarMode = SidebarMode::Channel;
                resized();
            }
        };

        detailView.onEditMutated = [this] { sealUndoTransaction(); if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };
        pianoRoll.onEditMutated  = [this] { sealUndoTransaction(); if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };

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
                bottomMode = BottomMode::PianoRoll;
                revealDrawer();
                resized();
            }
            else
            {
                FORGE_LOG_ERROR ("Failed to create MIDI clip in track " + juce::String (trackIndex));
            }
        };

        // File drag-drop onto an Arrange lane (W07): the lane resolves its own track index and maps the
        // drop x to a snapped start time; we import the dropped audio onto THAT track (importAudioFile is
        // track-aware) and rebuild so the clip appears. Null-guarded — unwired it is a safe no-op.
        arrangeView.onFilesDropped = [this] (int trackIndex, const File& file, te::TimePosition start)
        {
            if (session.importAudioFile (file, start, trackIndex) == nullptr)
                FORGE_LOG_ERROR ("Failed to import dropped audio onto arrange track "
                                 + juce::String (trackIndex) + ": " + file.getFullPathName());
            session.save();
            arrangeView.rebuild();
        };

        // Double-clicking an audio file in the Browser imports it onto the project.
        browserPanel.onImportFile = [this] (const File& f)
        {
            clip = session.importAudioFile (f, te::TimePosition());
            if (clip == nullptr)
                FORGE_LOG_ERROR ("Failed to import audio file: " + f.getFullPathName());
            session.save();
            rebind();
        };

        // Session grid — mirror the arrange wiring. The view delegates all engine ops to ProjectSession;
        // here we only route selection/creation to the shared piano-roll drawer and the record arm path.
        sessionView.onEditMutated = [this]
        {
            sealUndoTransaction();
            if (! session.save())
                FORGE_LOG_ERROR ("Failed to save project — I/O error");
            reconcileDrawerClip();   // a slot "Delete clip" may have detached the clip the drawer is editing
        };

        // W5 "Send to Arrangement": the one-directional Session -> Arrange bridge. The seam copies the
        // slot clip onto its OWN track's linear timeline (append-at-end); the source slot is untouched
        // (a copy, not a move), so no grid rebuild is needed — but ArrangeView has no clip-add listener,
        // so we MUST rebuild its lanes for the new clip to appear. Seal + save like every mutation.
        sessionView.onSendToArrangement = [this] (int trackIdx, int sceneIdx)
        {
            if (session.sendSlotToArrangement (trackIdx, sceneIdx) == nullptr)
                return;   // empty slot / failure — already logged by the seam

            sealUndoTransaction();
            if (! session.save())
                FORGE_LOG_ERROR ("Failed to save project — I/O error");

            arrangeView.rebuild();   // the new linear clip is invisible until the lanes re-enumerate getClips()
            setStatusMessage ("Sent clip to Arrangement");
        };

        // Session focus follows to the channel tray (W04b): SessionView announces focus-TRACK
        // changes (arrow keys / pad clicks) as an INDEX — it caches no track pointers (R1) — so
        // resolve it fresh here and bind the tray, mirroring arrangeView.onTrackSelected's guards:
        // the sidebar flips to the Channel tab only when it's open and the user hasn't explicitly
        // pinned Files (QC: never steal an explicit pane choice).
        sessionView.onTrackFocusChanged = [this] (int trackIndex)
        {
            // W08: the ChannelTray is Arrange-only now (the Session grid has its own per-column mixer
            // strips), so Session focus no longer drives the tray. This handler fires only from SessionView
            // (i.e. while in Session view), so the guard makes it a documented no-op here; the tray follows
            // arrangeView.onTrackSelected instead.
            if (viewMode != ViewMode::Arrange)
                return;

            te::AudioTrack* at = nullptr;

            if (auto* e = session.getEdit())
            {
                const auto tracks = te::getAudioTracks (*e);
                if (isPositiveAndBelow (trackIndex, tracks.size()))
                    at = tracks[trackIndex];
            }

            channelTray.setTrack (at);

            if (at != nullptr && browserVisible && ! userPinnedBrowser
                && sidebarMode != SidebarMode::Channel)
            {
                sidebarMode = SidebarMode::Channel;
                resized();
            }
        };

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
                revealDrawer();

            resized();
        };

        // The view already created + saved the new clip and rebuilt itself; we only open it for editing.
        sessionView.onMidiClipCreated = [this] (te::MidiClip::Ptr clip)
        {
            if (clip != nullptr)
            {
                pianoRoll.setMidiClip (clip.get());
                bottomMode = BottomMode::PianoRoll;
                revealDrawer();
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
                    recorder.disarmMidiTrack (*ed, track);   // v1: audio/MIDI arm mutually exclusive per track
                }

                const bool ok = arm ? recorder.armFirstInputToTrack (*ed, track)
                                    : recorder.disarmTrack (*ed, track);

                if (! ok)
                {
                    setStatusMessage ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());
                    FORGE_LOG_ERROR ((arm ? "Arm failed: " : "Disarm failed: ") + recorder.getLastError());
                }

                sessionView.refreshArmStates();
                arrangeView.refreshArmStates();
            }
        };

        // ---- W7: MIDI record into Session slots (docs/devlog/midi-record-design.md) ----
        // ProjectSession delegates the RecordController MIDI calls through these injected seams, so it
        // keeps no hard RecordController dependency (both are MainComponent members, same lifetime).
        session.recorderArmSlot     = [this] (te::Edit& e, te::ClipSlot& s) { return recorder.armFirstMidiInputToSlot (e, s); };
        session.recorderDisarmSlot  = [this] (te::Edit& e, te::ClipSlot& s) { return recorder.disarmSlot (e, s); };
        session.recorderIsSlotArmed = [this] (te::Edit& e, te::ClipSlot& s) { return recorder.isSlotMidiArmed (e, s); };

        sessionView.isTrackMidiArmed = [this] (te::AudioTrack& t)
        {
            auto* ed = session.getEdit();
            return ed != nullptr && recorder.isTrackMidiArmed (*ed, t);
        };

        sessionView.onMidiArmToggled = [this] (te::AudioTrack& track, bool arm)
        {
            if (auto* ed = session.getEdit())
            {
                if (arm)
                {
                    EngineHelpers::ensureRecordingInputOpen (engine);
                    recorder.enableMidiInputs();
                    recorder.disarmTrack (*ed, track);   // v1: mutual exclusion with the audio arm
                }

                const bool ok = arm ? recorder.armFirstMidiInputToTrack (*ed, track)
                                    : recorder.disarmMidiTrack (*ed, track);

                if (! ok)
                {
                    setStatusMessage ((arm ? "MIDI arm failed: " : "MIDI disarm failed: ") + recorder.getLastError());
                    FORGE_LOG_ERROR ((arm ? "MIDI arm failed: " : "MIDI disarm failed: ") + recorder.getLastError());
                }

                sessionView.refreshArmStates();
                arrangeView.refreshArmStates();
            }
        };

        sessionView.onSlotRecord = [this] (int t, int s)
        {
            // Slot capture is slot-ONLY: drop any track-level MIDI record target first, otherwise the
            // notes would ALSO land as a linear clip on the track (both targets are recordEnabled).
            if (auto* ed = session.getEdit())
            {
                auto tracks = te::getAudioTracks (*ed);
                if (t >= 0 && t < tracks.size())
                    if (auto* track = tracks[t])
                        recorder.disarmMidiTrack (*ed, *track);
            }

            if (! session.recordArmSlot (t, s))
            {
                // recordArmSlot's engine-arm failure sets recorder.getLastError(); its internal early
                // returns (slot/track unresolvable) log their own reason and leave the recorder error
                // empty — so fall back to a generic message rather than surfacing a stale one.
                const auto err = recorder.getLastError();
                const auto msg = err.isEmpty() ? String ("Slot record-arm failed — see the log")
                                               : "Slot record-arm failed: " + err;
                setStatusMessage (msg);
                FORGE_LOG_ERROR (msg);
                return;
            }

            session.beginSlotRecord (t, s);
            sessionView.refreshArmStates();
        };

        sessionView.onSlotRecordStop = [this] (int t, int s)
        {
            if (session.commitSlotRecord (t, s) != nullptr)
                session.save();
            sessionView.rebuild();   // the captured clip now resolves in the slot; refresh grid + arm tints
        };

        sessionView.isSlotRecording = [this] (int t, int s) { return session.isSlotRecording (t, s); };

        // ---- P3: buses / sends (aux returns) ----
        // Bind the aux seam ONCE (session outlives individual Edits); the mixer degrades gracefully
        // (no send knobs / return strips) until this is set. When ensureAuxBus appends a return track,
        // rebuild the views that cache track refs (grid columns / arrange lanes) so none derefs a stale
        // column, and persist. Appending at the END keeps every existing absolute track index stable.
        mixerView.setSession (&session);

        session.onTracksChanged = [this]
        {
            if (! session.save())
                FORGE_LOG_ERROR ("Failed to save project after track-list change");

            if (sessionViewBinds() && session.getNumAudioTracks() != sessionView.getNumColumns())
                sessionView.rebuild();
            arrangeView.rebuild();
            channelTray.refreshNow();   // re-validate the tray's bound track (W04a)
        };

        // ---- P5: markers / cue points ----
        // The MarkerBar caches only value rows; it drives every mutation through these seams (no raw
        // te:: calls). Wired once; the lambdas read the edit live via `session`.
        markerBar.getMarkers = [this]
        {
            std::vector<MarkerBar::Marker> rows;
            for (auto& m : session.getMarkers())
                rows.push_back ({ m.id, m.time, m.name });
            return rows;
        };
        markerBar.onAddMarker     = [this] (te::TimePosition t, const juce::String& n) { session.addMarker (t, n); };
        markerBar.onRemoveMarker  = [this] (te::EditItemID id)                          { session.removeMarker (id); };
        markerBar.onMoveMarker    = [this] (te::EditItemID id, te::TimePosition t)      { session.moveMarker (id, t); };
        markerBar.onRenameMarker  = [this] (te::EditItemID id, const juce::String& n)   { session.renameMarker (id, n); };
        markerBar.onJumpTransport = [this] (te::TimePosition t)                         { session.jumpTransportTo (t); };
        markerBar.onEditMutated   = [this] { sealUndoTransaction(); if (! session.save()) FORGE_LOG_ERROR ("Failed to save project after marker edit"); };

        // ---- P2: MIDI-learn ----
        // A confirmation when a learn binds. (Real-hardware CC routing into handleIncomingController is a
        // deferred follow-up — ForgeUIBehaviour / a MIDI-input listener; see docs. The seam + UI trigger
        // are wired now so the mapping model is in place.)
        midiLearn.onLearnComplete = [this] (te::AutomatableParameter& p)
        {
            setStatusMessage ("MIDI Learn: bound " + p.getParameterName());
        };

        arrangeView.setEdit (session.getEdit());
        mixerView.setEdit (session.getEdit());
        midiLearn.setActiveEdit (session.getEdit());   // live-apply target (P2)
        markerBar.refresh();                            // pull markers for the freshly-opened edit (P5)

        // The Session grid is the default view. Keep it OFF the headless playback/record selftest path so
        // the throwaway selftest edit stays pristine (no ensureScenes slot scaffolding perturbs the gates).
        if (sessionViewBinds())
            sessionView.setEdit (session.getEdit());

        setViewMode (ViewMode::Session);

        if (mode == SelfTest::screenshot)
            setSize (1480, 940);   // tall enough to render all 16 scene rows for the snapshot
        else
            setSize (1200, 620);   // wide enough for all four LCD zones at first launch (W04b QC:
                                   // fixed chrome + timecodeMinWidth puts the floor at ~1174)

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
        else if (mode == SelfTest::midi)
        {
            // Headless synthetic-MIDI capture-into-slot gate (verdict-A proof). Event-driven like the
            // record selftest: create a virtual MIDI in → yield → enable → yield → arm slot (0,0) + roll →
            // yield → inject note-ons → yield → note-offs → yield → stop + verify. See §4 of
            // docs/devlog/midi-record-design.md.
            MessageManager::callAsync ([this] { beginSelfTestMidi(); });
        }
        else if (mode == SelfTest::midilearn)
        {
            // Headless MIDI-learn gate (P2): prove the seam binds a CC to a plugin param over Tracktion's
            // native store with NO focused Edit. A virtual device can't route CC to the parser, so inject
            // via the seam directly. Event-driven yield discipline: arm a learn → yield → inject a CC →
            // yield (the native bind runs on an AsyncUpdater) → assert the mapping landed. See beginSelfTestMidiLearn().
            MessageManager::callAsync ([this] { beginSelfTestMidiLearn(); });
        }
        else if (mode == SelfTest::midiinput)
        {
            // Headless HARDWARE-path gate: prove a CC arriving THROUGH a virtual device routes via the
            // engine's parser + the ForgeUIBehaviour focused-Edit to the mapped param (the live path a
            // real knob drives). Distinct from --selftest-midilearn, which injects into the seam directly.
            MessageManager::callAsync ([this] { beginSelfTestMidiInput(); });
        }
        else if (mode == SelfTest::controlsurface)
        {
            // Headless control-surface gate: a virtual pad-press launches slot (0,0) and one LED poll
            // emits the expected note-on — proving the Forge-native Launchpad driver end-to-end, no hardware.
            MessageManager::callAsync ([this] { beginControlSurfaceSelftest(); });
        }
        else if (mode == SelfTest::automation)
        {
            // Headless automation-read gate: import a tone on track 0, write a falling volume curve
            // (fader pos 0.8 @ t=0 -> 0.2 @ t=2s, linear), force the read stream live via updateStream,
            // then roll and poll volParam->getCurrentValue() to prove the curve drives the parameter
            // during playback. Event-driven like the other gates: begin after yielding to the loop.
            MessageManager::callAsync ([this] { beginAutomationSelftest(); });
        }
        else if (mode == SelfTest::sync)
        {
            // Headless MIDI-clock-out gate: freeze the periodic MIDI rescan, inject a capture probe
            // over a REAL system MIDI out, roll the transport, and assert the engine emitted SPP +
            // start/continue + ~bpm*24 clocks + a midiStop on the stop edge. Zero-MIDI-outs machines
            // take an honest SKIP-degrade path. Event-driven yield discipline like --selftest-midi.
            MessageManager::callAsync ([this] { beginSelfTestSync(); });
        }
        else if (mode == SelfTest::livesync)
        {
            // Headless cross-surface live-refresh gate (W03 P5): an engine-side write (the path a
            // MIDI-learn CC or another surface takes) must appear on the mixer fader/mute and the
            // inspector's gain slider after one forced sync tick — no re-select, no rebuild.
            MessageManager::callAsync ([this] { runLiveSyncSelftest(); });
        }
        else if (mode == SelfTest::tray)
        {
            // Headless channel-tray gate (W04a P2): bind track 0, write volume/mute engine-side (the
            // path another surface or a MIDI-learn CC takes), force one sync tick, assert the tray
            // followed; then a null re-bind must land in the empty state.
            MessageManager::callAsync ([this] { runTraySelftest(); });
        }
        else if (mode == SelfTest::popout)
        {
            // Headless tear-off gate (W04b): reparent the mixer + piano-roll into PopoutWindows
            // and back, asserting parentage each way. Restore defers each window's destruction
            // via callAsync, so the verify is a SECOND message-loop turn (same yield discipline
            // as --selftest-midilearn).
            MessageManager::callAsync ([this] { beginPopoutSelftest(); });
        }
        else if (mode == SelfTest::undo)
        {
            // Headless undo/redo gate (W05): explicit transactions (the engine's 350 ms
            // auto-seal needs loop time; explicit beginNewTransaction is deterministic) around
            // a slot-clip create + delete, then undo/redo round-trips + a note-edit leg —
            // asserting canUndo/canRedo transitions at every step. Synchronous on the message
            // thread, so a single callAsync yield suffices.
            MessageManager::callAsync ([this] { runUndoSelftest(); });
        }
        else if (mode == SelfTest::taptempo)
        {
            // Tap-tempo model + tempo-write seam gate (hands-on 1.4). Pure math + one engine write on
            // the live edit; synchronous, so one callAsync yield suffices.
            MessageManager::callAsync ([this] { runTapTempoSelftest(); });
        }
        else if (mode == SelfTest::slotdelete)
        {
            // Wave 2 (W07): the clearSlot seam (delete clip). Synchronous; one callAsync yield suffices.
            MessageManager::callAsync ([this] { runSlotDeleteSelftest(); });
        }
        else if (mode == SelfTest::addtrack)
        {
            // Wave 2 (W07): the appendAudioTrack seam (+ Track). Synchronous; one callAsync yield suffices.
            MessageManager::callAsync ([this] { runAddTrackSelftest(); });
        }
        else if (mode == SelfTest::scene)
        {
            // Wave 2 (W07): ensureScenes grows the grid past the former 16 ceiling (+ Scene). Synchronous.
            MessageManager::callAsync ([this] { runSceneSelftest(); });
        }
        else if (mode == SelfTest::dragdrop)
        {
            // Wave 2 (W07): the file-import seams both drop paths route through (session slot + arrange
            // lane). Synchronous; one callAsync yield suffices.
            MessageManager::callAsync ([this] { runDragDropSelftest(); });
        }
        else if (mode == SelfTest::sessionmixer)
        {
            // Wave 3 (W08): the per-track Session mixer strip's engine->widget sync. Synchronous; one
            // callAsync yield suffices.
            MessageManager::callAsync ([this] { runSessionMixerSelftest(); });
        }
        else if (mode == SelfTest::demo)
        {
            // Wave 4 (W09): the audible-demo gate — instrument presets applied + notes seeded. Synchronous.
            MessageManager::callAsync ([this] { runDemoSelftest(); });
        }
        else if (mode == SelfTest::sendarrange)
        {
            // Wave 5 (the last hands-on wave): the Session -> Arrange "Send to" bridge. Synchronous;
            // one callAsync yield suffices.
            MessageManager::callAsync ([this] { runSendArrangeSelftest(); });
        }
        else if (mode == SelfTest::followaction)
        {
            // Wave 1: per-clip follow-action + loop-toggle seams. Synchronous; one callAsync yield suffices.
            MessageManager::callAsync ([this] { runFollowActionSelftest(); });
        }
        else if (mode == SelfTest::launchmode)
        {
            // Wave 1: per-clip launch-mode seam. Synchronous; one callAsync yield suffices.
            MessageManager::callAsync ([this] { runLaunchModeSelftest(); });
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
        {
            // Interactive mode: bring up the grid control surface. It stays fully inert (no timer, no
            // MIDI traffic) when no controller is present — the norm until real hardware is connected.
            controlSurface = std::make_unique<ControlSurfaceHost> (session,
                                                                   std::make_unique<LaunchpadDriver>(),
                                                                   /*openNow=*/ true);
            startTimerHz (5);
        }
    }

    ~MainComponent() override
    {
        // Clear the engine-owned ForgeUIBehaviour's session pointer and stop the control surface FIRST:
        // the Engine outlives us, so a late CC callback or LED poll must never touch a dying ProjectSession.
        static_cast<ForgeUIBehaviour&> (engine.getUIBehaviour()).setSession (nullptr);
        controlSurface.reset();

        // W04b: close tear-off windows while the views they host are still alive (the dtor BODY
        // runs before member destruction; the after-the-views declaration order is the backstop).
        // W05: persist their placements first (quit-with-popout-open path).
        if (pianoRollPopout != nullptr)
            engine.getPropertyStorage().getPropertiesFile()
                .setValue ("forgePianoRollPopoutState", pianoRollPopout->getWindowStateAsString());
        if (mixerPopout != nullptr)
            engine.getPropertyStorage().getPropertiesFile()
                .setValue ("forgeMixerPopoutState", mixerPopout->getWindowStateAsString());
        pianoRollPopout.reset();
        mixerPopout.reset();

        PluginWindow::closeAll();   // close any floating plugin editors before the Edit tears down

        if (auto* t = session.getTransport())
            t->stop (false, false);

        sineFile.deleteFile();
    }

    /** The menu-bar model MainWindow installs via setMenuBar (W04a). */
    ForgeMenuModel& getMenuModel() { return menuModel; }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (ForgeLookAndFeel::shellBg));

        // W05: tearing the mixer off WHILE in Mix view leaves the centre empty (setViewMode's
        // front-the-popout guard only covers selecting Mix afterwards) — say so instead of
        // presenting a dead pane.
        if (viewMode == ViewMode::Mixer && mixerPopout != nullptr)
        {
            g.setColour (Colour (ForgeLookAndFeel::textSec));
            g.setFont (Font (FontOptions (14.0f)));
            // ASCII only: juce::String's char* ctor is ASCII-only (a raw em-dash renders mojibake
            // + jasserts — the recovered QC finding; the escaped-UTF-8 idiom is overkill here).
            g.drawText ("Mixer is popped out - press F11 or View > Mix to bring it forward",
                        getLocalBounds(), Justification::centred, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        controlBar.setBounds (r.removeFromTop (46));
        statusLabel.setBounds (r.removeFromBottom (24));

        auto work = r;

        // Left sidebar (W04a): a multi-modal band — a slim tab row (Files | Channel) on top, then
        // EITHER the file browser or the selected-track channel tray, chosen by sidebarMode. The B
        // toggle shows/hides the whole band, exactly as it always hid the browser.
        const bool showBrowser = browserVisible && sidebarMode == SidebarMode::Browser;
        const bool showChannel = browserVisible && sidebarMode == SidebarMode::Channel;
        sidebarFilesTab.setVisible (browserVisible);
        sidebarChannelTab.setVisible (browserVisible);
        browserPanel.setVisible (showBrowser);
        channelTray.setVisible (showChannel);
        browserResizer.setVisible (browserVisible && sidebarSlide < 0);   // inert while sliding (QC)
        if (browserVisible)
        {
            // While a slide is in flight the band renders at the animated width (it passes below
            // the min toward 0 — the inner layout tolerates degenerate bounds); settled layout
            // uses the persisted clamped width as before.
            const int w = (sidebarSlide >= 0) ? sidebarSlide
                                              : jlimit (browserMinWidth, browserMaxWidth, browserWidth);
            auto band = work.removeFromLeft (w);

            auto tabs = band.removeFromTop (sidebarTabH);
            const int tabW = tabs.getWidth() / 2;
            sidebarFilesTab.setBounds (tabs.removeFromLeft (tabW));
            sidebarChannelTab.setBounds (tabs);
            sidebarFilesTab.setToggleState (sidebarMode == SidebarMode::Browser, dontSendNotification);
            sidebarChannelTab.setToggleState (sidebarMode == SidebarMode::Channel, dontSendNotification);

            if (showBrowser) browserPanel.setBounds (band);
            else             channelTray.setBounds (band);

            browserResizer.setBounds (work.removeFromLeft (resizerThickness));
        }

        auto centre = work;

        // Bottom drawer: either the audio-clip DetailView or the MIDI PianoRoll fills it, chosen by
        // bottomMode (the arrange selection routes to one or the other). A resizer hugs its top edge;
        // the inactive editor is hidden. The piano-roll scrolls its 128 pitch rows internally via its
        // own Viewport, so the drawer keeps the same 90..420px clamp regardless of mode.
        //
        // W04b: a torn-off view lives in its own PopoutWindow — bounds are parent-relative and its
        // visibility belongs to that window, so the shell skips BOTH setVisible and setBounds for it
        // (a setVisible(false) alone would blank the popout). While the roll is out, the drawer falls
        // back to the DetailView; bottomMode stays PianoRoll so the roll returns there on restore.
        const bool rollTornOff   = pianoRollPopout != nullptr;
        const bool showDetail    = drawerVisible && (bottomMode == BottomMode::Detail || rollTornOff);
        const bool showPianoRoll = drawerVisible && bottomMode == BottomMode::PianoRoll && ! rollTornOff;
        detailView.setVisible (showDetail);
        if (! rollTornOff)
            pianoRoll.setVisible (showPianoRoll);
        drawerResizer.setVisible (drawerVisible && drawerSlide < 0);   // inert while sliding (QC)
        if (drawerVisible)
        {
            const int h = (drawerSlide >= 0) ? drawerSlide
                                             : jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);
            const auto drawerArea = centre.removeFromBottom (h);
            if (showPianoRoll) pianoRoll.setBounds (drawerArea);
            else               detailView.setBounds (drawerArea);
            drawerResizer.setBounds (centre.removeFromBottom (resizerThickness));
        }

        sessionView.setVisible (viewMode == ViewMode::Session);
        arrangeView.setVisible (viewMode == ViewMode::Arrange);
        if (mixerPopout == nullptr)
            mixerView.setVisible (viewMode == ViewMode::Mixer);
        sessionView.setBounds (centre);
        if (mixerPopout == nullptr)
            mixerView.setBounds (centre);

        // Markers strip (P5) rides above the arrange view, sharing its TimelineView. Carve it off the top
        // of the arrange area ONLY in Arrange mode; Session/Mixer get the full centre. Full-width bar with
        // headerInset = headerW so its time axis lines up pixel-for-pixel with the ruler below it.
        markerBar.setVisible (viewMode == ViewMode::Arrange);
        if (viewMode == ViewMode::Arrange)
        {
            auto arrangeArea = centre;
            auto strip = arrangeArea.removeFromTop (MarkerBar::preferredHeight);
            markerBar.setHeaderInset (ArrangeLayout::headerW);
            markerBar.setBounds (strip);
            arrangeView.setBounds (arrangeArea);
        }
        else
        {
            arrangeView.setBounds (centre);
        }

        // Async-export progress panel (P4): a centred transient overlay, only while an export runs.
        if (exportProgress.isVisible())
        {
            const int w = jmin (420, getWidth() - 40);
            const int h = 96;
            exportProgress.setBounds ((getWidth() - w) / 2, (getHeight() - h) / 2, w, h);
            exportProgress.toFront (false);
        }
    }

private:
    te::Engine& engine;
    ProjectSession session;
    RecordController recorder;
    MidiLearn midiLearn { engine };   // CC → plugin-param mapping over Tracktion's native store (P2)
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

    // --selftest-automation state: prove a volume automation CURVE drives volParam during playback.
    te::AutomatableParameter::Ptr auVolParam;   // track-0 volume param carrying the test curve
    bool   auReadingAutomation = false;         // automation read enabled on the record manager
    bool   auAutomationActive  = false;         // volParam->isAutomationActive() after updateStream()
    int    auNumPoints         = 0;             // curve point count (must be 2)
    float  auStaticValueAt1p5  = -1.0f;         // curve math at 1.5 s (must be ~0.35, pre-playback)
    bool   auEarlyCaptured     = false;         // first poll tick sampled (early value)
    bool   auLateCaptured      = false;         // a poll tick landed in the 1.5..2.4 s late window
    float  auEarlyValue        = -1.0f;         // getCurrentValue() early (expect >= 0.7)
    float  auLateValue         = -1.0f;         // getCurrentValue() late  (expect <= 0.45)
    double auEarlyPos          = -1.0;          // transport position at the early sample (diagnostic)
    int    automationPollTicks = 0;             // automation bounded poll: 10 Hz ticks elapsed (cap 40 = ~4 s)

    // --selftest-midi state machine (event-driven; mirrors the record selftest's yield discipline).
    static constexpr const char* kSelfTestMidiName = "Forge SelfTest MIDI";
    int  miPhase = 0;                                   // 1=created 2=enabled 3=armed+rolling 4=on 5=off
    te::VirtualMidiInputDevice* miDevice = nullptr;     // the virtual MIDI in we inject through (engine-owned)
    String miAvailableMidiIns;                          // MIDI-in device names after create (diagnostic)
    bool miDeviceEnabled   = false;
    bool miSlotArmed       = false;                     // slot (0,0) became a MIDI record target
    bool miRecordingStarted = false;
    int  miNotesInjected   = 0;
    int  miPreExistingNotes = -1;                       // notes in slot (0,0) BEFORE capture (must be 0)
    bool miClipCreated     = false;
    int  miCapturedNotes   = 0;

    // --selftest-midilearn state machine (event-driven; the native bind runs on an AsyncUpdater, so the
    // CC-inject and the verify must be separate message-loop turns — same yield discipline as --selftest-midi).
    int  mlPhase = 0;                                   // 1 = learn armed, 2 = CC injected
    te::AutomatableParameter* mlParam = nullptr;        // the learn target (owned by its plugin; edit-lifetime)
    juce::String mlParamName;                           // the target's display name (diagnostic)
    bool mlWasMappedBefore = true;                      // was the param mapped BEFORE the gate? (must be false)
    bool mlLearnArmed      = false;                     // did beginLearn arm the native learn?
    bool mlIsMappedAfter   = false;                     // did the CC bind the param?
    int  mlCc = -1, mlCh = -1;                          // the CC number / channel the seam reports as mapped

    // --selftest-midiinput state: prove the ForgeUIBehaviour focused-Edit wiring (the necessary condition
    // for a real hardware CC to reach the mappings) + a CC->param bind. A VirtualMidiInputDevice has no
    // controllerParser, so the physical-CC end-to-end is a real-hardware smoke item, not a headless one.
    int  miiPhase = 0;                                  // 1 = verify (after the seam bind's async yield)
    te::AutomatableParameter* miiParam = nullptr;       // the CC-mapped target (plugin-owned, edit-lifetime)
    juce::String miiParamName;
    bool  miiMapped = false;                            // did CC 74 bind to miiParam?

    // --selftest-controlsurface state: Forge-native Launchpad driver end-to-end with no hardware.
    CapturingMidiSink* csSink = nullptr;                // non-owning; owned by the driver inside controlSurface
    bool csClipCreated = false, csInputLaunched = false, csLedCaptured = false;
    int  csExpectedNote = -1, csExpectedVel = -1, csExpectedChan = -1;

    // --selftest-sync (MIDI-clock-out) state. The probe is a shared_ptr because dm.midiOutputs holds
    // shared_ptr<MidiOutputDevice>; we keep our own handle so we can snapshot() + tear down deterministically.
    int  syPhase = 0;                                   // 1 = scan settled, 2 = rolled, 3 = stop edge drained
    std::shared_ptr<MidiClockProbeDevice> syProbe;      // our capture device (also lives in dm.midiOutputs)
    std::shared_ptr<te::MidiOutputDevice> syEvicted;    // the engine's own entry we evicted; re-inserted at teardown
    std::unique_ptr<juce::XmlElement> syPropsSnapshot;  // the real device's persisted props BEFORE the probe touched them
    juce::String syOutName;                             // chosen MIDI-out name (diagnostic)
    bool syDegraded      = false;                       // true => zero MIDI outs (honest SKIP-degrade path)
    bool syProbeOpen     = false;                       // probe's juce::MidiOutput actually opened (isOpen)
    bool syPropRoundTrip = false;                       // setSendingClock(true) -> isSendingClock() == true
    bool syEnabledOK     = false;                       // probe->isEnabled() after ctor+force (stale-props guard)
    bool syRolled        = false;                       // transport.isPlaying() after play()
    int  sySpp = 0, syStartCont = 0, syClock = 0, syStop = 0;   // captured message-type counts
    double syExpectedClocks = 0.0;                      // seconds * (bpm/60) * 24
    int  sySavedScanInterval = 4;                       // restore on ALL exit paths (persists to storage)

    // --selftest-popout state (W04b): parentage/visibility flags captured across the two turns.
    bool poDockedBefore      = false;   // both views were shell children before the tear-off
    bool poMixerWindowSeen   = false, poRollWindowSeen    = false;   // popouts constructed + visible
    bool poMixerOut          = false, poRollOut           = false;   // parent == the popout window
    bool poMixerBack         = false, poRollBack          = false;   // turn 2: parent == the shell, window gone
    bool poNoGhostOverlay    = false;   // turn 2, BEFORE any rescue relayout: restored views hidden, no stolen focus
    bool poMixerVisibleAfter = false, poRollVisibleAfter  = false;   // turn 2: visible after driving view state

    ControlBar controlBar;
    ForgeMenuModel menuModel;   // top menu-bar model (W04a): wired in setupMenuModel(), installed by MainWindow::setMenuBar
    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };
    MarkerBar markerBar { timelineView };   // markers strip over the arrange timeline (shares the axis, P5)
    MixerView mixerView;
    SessionView sessionView { session };   // primary view: tracks×scenes clip-launch grid (Sheet 00)
    BrowserView browserPanel;   // left region: real file browser (name kept for layout call sites)
    DetailView  detailView;     // bottom drawer: audio-clip inspector
    ChannelTray channelTray { session };   // left sidebar Channel tab: selected-track strip (W04a)
    PianoRollView pianoRoll { timelineView };   // bottom drawer: MIDI-clip editor (shares the time axis)

    // W04b tear-off windows. Declared AFTER mixerView/pianoRoll (reverse destruction kills the
    // windows before their content even if the dtor-body resets are ever removed) and reset in
    // ~MainComponent's body — the same declaration-order discipline the controlSurface member documents.
    std::unique_ptr<PopoutWindow> mixerPopout, pianoRollPopout;

    Label statusLabel;
    juce::uint32 statusHoldUntilMs = 0;   // transient status messages survive the 5Hz refresh until this time

    // Grid control surface (Launchpad, Forge-native). Interactive: constructed in the ctor's normal-mode
    // branch, inert until a controller appears. Selftest: reused by --selftest-controlsurface with a
    // capturing LED sink. Declared AFTER `session` so it destructs before it (and is reset first in ~ctor).
    std::unique_ptr<ControlSurfaceHost> controlSurface;

    // Each async file dialog owns its own FileChooser so open/save-as/export can't stomp each other.
    // (Import uses EngineHelpers::browseForAudioFile, which manages its own shared chooser.)
    std::unique_ptr<FileChooser> openChooser, saveChooser, exportChooser, stemsChooser;
    TooltipWindow tooltipWindow;

    // Async export (P4): a transient centred progress panel + the in-flight render handle it drives.
    // The handle is held for the lifetime of the export and destroyed on the message thread from inside
    // onComplete (AsyncRender::finishAll moves onComplete out before invoking, so self-destruction is safe).
    ExportProgress exportProgress;
    std::unique_ptr<Exporter::AsyncRender> activeRender;

    // MIDI-learn (P2): menu ids from showMidiLearnMenu() index into this; each holds a live param pointer
    // (valid only while its plugin lives — transient, rebuilt on every menu open).
    juce::Array<PluginHost::LearnableParam> midiLearnTargets;

    ViewMode viewMode = ViewMode::Session;
    BottomMode bottomMode = BottomMode::Detail;   // Detail (audio) vs PianoRoll (MIDI) in the drawer
    bool browserVisible = false;   // lean default; toggled on demand
    bool drawerVisible  = false;

    // Region sizes are mutable (drag-to-resize). Not persisted across launches yet (future work).
    static constexpr int resizerThickness = 5;
    int browserWidth = 220, browserMinWidth = 140, browserMaxWidth = 560;
    int drawerHeight = 160, drawerMinHeight = 90,  drawerMaxHeight = 420;

    // W04a left-sidebar mode: the band hosts the file browser or the channel tray, picked by two
    // slim tabs across its top. The B toggle still shows/hides the whole band.
    SidebarMode sidebarMode = SidebarMode::Browser;
    static constexpr int sidebarTabH = 22;
    TextButton sidebarFilesTab { "Files" }, sidebarChannelTab { "Channel" };

    // QC: track selection auto-flips the sidebar to Channel (the GarageBand reveal), but never
    // over an EXPLICIT Files choice — a user browsing samples must not lose the pane mid-browse.
    bool userPinnedBrowser = false;

    // W04b animated slide-outs. A DEDICATED timer — MainComponent's inherited Timer is load-bearing
    // selftest machinery (dossier risk). The animation lerps the width/height SCALARS and re-runs
    // resized() per step (the exact path the drag-resizers already exercise), so the whole layout
    // chain moves together; juce::ComponentAnimator would fight resized() for the child bounds.
    // The screenshot harness sets browserVisible/drawerVisible directly and never animates.
    struct SlideTimer : juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    };
    SlideTimer slideTimer;
    int sidebarSlide = -1, sidebarSlideTarget = 0;   // in-flight width  (-1 = settled)
    int drawerSlide  = -1, drawerSlideTarget  = 0;   // in-flight height (-1 = settled)

    ResizerBar browserResizer { true,  browserMinWidth, browserMaxWidth };   // vertical handle (width)
    ResizerBar drawerResizer  { false, drawerMinHeight, drawerMaxHeight };   // horizontal handle (height)

    //==============================================================================
    void setupControlBar()
    {
        controlBar.onNew           = [this] { swapProject ([this] { session.newProject (uniqueUntitledFile()); }); };
        controlBar.onOpen          = [this] { openDialog(); };
        controlBar.onSave          = [this] { if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };
        controlBar.onSaveAs        = [this] { saveAsDialog(); };
        controlBar.onImport        = [this] { importDialog(); };
        controlBar.onExport        = [this] { exportDialog(); };
        controlBar.onExportStems   = [this] { exportStemsDialog(); };
        controlBar.onScanPlugins   = [this] { PluginScanner::showScanDialog (engine); };
        controlBar.onAudioSettings = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        controlBar.onToggleBrowser = [this] { toggleSidebarAnimated(); };
        controlBar.onToggleDrawer  = [this] { toggleDrawerAnimated(); };
        controlBar.onViewMode      = [this] (int m) { setViewMode (m == 0 ? ViewMode::Session
                                                                  : m == 1 ? ViewMode::Arrange
                                                                           : ViewMode::Mixer); };

        // Metronome + count-in (P1): route the TransportBar click toggle + count-in selector through the
        // Metronome engine seam so the view makes no raw te:: click calls. Each lambda reads the edit LIVE
        // (per-invocation) so it tracks project swaps, and null-guards. Count-in is native — te's
        // transport.record() pre-rolls getNumCountInBeats() itself, so no RecordController change. Wired
        // here (before controlBar.setEdit below), so the first syncMetronomeControls() reflects engine truth.
        auto& tb = controlBar.getTransportBar();
        tb.onMetronomeToggled    = [this] (bool on)  { if (auto* e = session.getEdit()) Metronome::enableClick (*e, on); };
        tb.queryMetronomeEnabled = [this]            { auto* e = session.getEdit(); return e != nullptr && Metronome::isClickEnabled (*e); };
        tb.onCountInBarsChanged  = [this] (int bars) { if (auto* e = session.getEdit()) Metronome::setCountInBars (*e, bars); };
        tb.queryCountInBars      = [this]            { auto* e = session.getEdit(); return e != nullptr ? Metronome::getCountInBars (*e) : 0; };

        // MIDI-clock out (W03): route the Clock toggle through the MidiClockSync seam. Device-level
        // (persists per MIDI-out via the engine's props), so no edit guard — and wired here, before
        // controlBar.setEdit, so the first sync reflects engine truth including persisted state.
        // (QC caught the seam left unwired at integration — the toggle was inert.)
        tb.onMidiClockToggled    = [this] (bool on) { MidiClockSync::setSendClockToAll (engine, on); };
        tb.queryMidiClockEnabled = [this]           { return MidiClockSync::isSendingClockAny (engine); };

        // Free-trigger launch quantization (hands-on 1.3): expose the Edit-level global
        // (Edit::getLaunchQuantisation().type) as a TransportBar selector. LaunchQType::none = free
        // trigger (no bar-snap) for effects/jamming; default is 1 bar. Wired before controlBar.setEdit,
        // so the TransportBar's first sync seeds the combo from the query.
        tb.onLaunchQuantisationChanged = [this] (int idx) { session.setGlobalLaunchQuantisation (static_cast<te::LaunchQType> (idx)); };
        tb.queryLaunchQuantisation     = [this]           { return (int) session.getGlobalLaunchQuantisation(); };

        // Clickable tempo (hands-on 1.4): the LCD's tempo zone opens a CallOutBox popup (up/down, typed
        // entry, tap tempo). queryBpm reads the live curve-aware BPM per-invocation (tracks project
        // swaps); onBpmChanged writes via the clamped [20,300] EngineHelpers::setTempoAt seam. The LCD
        // self-polls at 25 Hz, so no explicit refresh call is needed.
        auto& lcd = controlBar.getLcdDisplay();
        lcd.queryBpm = [this] { auto* e = session.getEdit();
                                return e != nullptr ? e->tempoSequence.getBpmAt (e->getTransport().getPosition()) : 120.0; };
        lcd.onBpmChanged = [this] (double bpm) { if (auto* e = session.getEdit())
                                                     EngineHelpers::setTempoAt (*e, e->getTransport().getPosition(), bpm); };
    }

    // Menu-bar model (W04a): the same commands the buttons/keys already run, exposed as a
    // discoverable index with shortcut labels. File/View entries SHARE the std::functions wired
    // in setupControlBar() (call this after it), so menu and button can never drift. Transport
    // entries mirror TransportBar.cpp's button lambdas; Record routes through toggleRecordTake
    // and the SAME function is finally assigned to the TransportBar's Rec button, which was
    // never wired anywhere (silent no-op — W04a dossier bug). Engine-state mutations end with a
    // controlBar.setEdit(...) resync so the TransportBar toggles reflect the change immediately
    // (its ChangeListener only fires on transport state changes, not on click/clock/count-in).
    void setupMenuModel()
    {
        auto& cb = menuModel.callbacks;

        // File — shared with the ControlBar buttons.
        cb.onNewProject    = controlBar.onNew;
        cb.onOpenProject   = controlBar.onOpen;
        cb.onSave          = controlBar.onSave;
        cb.onSaveAs        = controlBar.onSaveAs;
        cb.onImportAudio   = controlBar.onImport;
        cb.onExportMixdown = controlBar.onExport;
        cb.onExportStems   = controlBar.onExportStems;
        cb.onAudioSettings = controlBar.onAudioSettings;
        cb.onPluginManager = controlBar.onScanPlugins;
        cb.onExit          = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };   // hands-on 1.5

        // Edit
        cb.onUndo        = [this] { doUndo(); };
        cb.onRedo        = [this] { doRedo(); };
        cb.queryCanUndo  = [this] { auto* e = session.getEdit(); return e != nullptr && e->getUndoManager().canUndo(); };
        cb.queryCanRedo  = [this] { auto* e = session.getEdit(); return e != nullptr && e->getUndoManager().canRedo(); };
        cb.onMidiLearn = [this] { showMidiLearnMenu(); };

        // View — commands shared with the ControlBar; queries read the shell's own state.
        cb.onViewMode      = controlBar.onViewMode;
        cb.onToggleBrowser = controlBar.onToggleBrowser;
        cb.onToggleDrawer  = controlBar.onToggleDrawer;
        cb.onPopOutMixer     = [this] { tearOffMixer(); };       // W04b: re-invoke fronts the window
        cb.onPopOutPianoRoll = [this] { tearOffPianoRoll(); };
        cb.queryMixerPoppedOut     = [this] { return mixerPopout != nullptr; };       // tick = torn off (QC)
        cb.queryPianoRollPoppedOut = [this] { return pianoRollPopout != nullptr; };
        cb.queryViewMode       = [this] { return viewMode == ViewMode::Session ? 0
                                               : viewMode == ViewMode::Arrange ? 1 : 2; };
        cb.queryBrowserVisible = [this] { return browserVisible; };
        cb.queryDrawerVisible  = [this] { return drawerVisible; };

        // Transport — mirrors TransportBar.cpp's button lambdas; the edit is read LIVE per invocation.
        cb.onTogglePlay   = [this] { if (auto* ed = session.getEdit()) EngineHelpers::togglePlay (*ed); };
        cb.onToggleRecord = [this] { toggleRecordTake(); };
        cb.onToggleLoop   = [this]
        {
            if (auto* t = session.getTransport())
                t->looping = ! t->looping;
            controlBar.setEdit (session.getEdit());   // resync the TransportBar toggles
        };
        cb.onToggleMetronome = [this]
        {
            if (auto* e = session.getEdit())
                Metronome::enableClick (*e, ! Metronome::isClickEnabled (*e));
            controlBar.setEdit (session.getEdit());
        };
        cb.onCountInBars = [this] (int bars)
        {
            if (auto* e = session.getEdit())
                Metronome::setCountInBars (*e, bars);
            controlBar.setEdit (session.getEdit());
        };
        cb.onToggleMidiClock = [this]
        {
            MidiClockSync::setSendClockToAll (engine, ! MidiClockSync::isSendingClockAny (engine));
            controlBar.setEdit (session.getEdit());
        };
        cb.queryMetronomeEnabled = [this] { auto* e = session.getEdit(); return e != nullptr && Metronome::isClickEnabled (*e); };
        cb.queryMidiClockEnabled = [this] { return MidiClockSync::isSendingClockAny (engine); };
        cb.queryCountInBars      = [this] { auto* e = session.getEdit(); return e != nullptr ? Metronome::getCountInBars (*e) : 0; };

        // Rec-button fix (W04a dossier): TransportBar::onRecord is fired by the Rec button but was
        // never assigned — the button was a silent no-op; only the 'R' key recorded. Menu item,
        // key, and button now all route through the one toggleRecordTake.
        controlBar.getTransportBar().onRecord = [this] { toggleRecordTake(); };

        // Help — a minimal About box. May be left unset (the model null-guards every callback).
        cb.onAbout = []
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "About Forge",
                                              "Forge " + JUCEApplication::getInstance()->getApplicationVersion()
                                                  + "\nA native, Session-first DAW on JUCE + Tracktion Engine.");
        };
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
        // W04b (QC): selecting Mix while the mixer is torn off would render a blank centre with
        // no affordance — front the popout instead, leaving the current centre view in place.
        if (m == ViewMode::Mixer && mixerPopout != nullptr)
        {
            mixerPopout->toFront (true);
            return;
        }

        viewMode = m;
        controlBar.setViewMode (m == ViewMode::Session ? 0 : m == ViewMode::Arrange ? 1 : 2);
        resized();

        // Give the Session grid keyboard focus when it becomes active so its arrow/Enter launch keys fire
        // (the switch only toggles visibility; SessionView::visibilityChanged also covers this) — QC fix.
        if (m == ViewMode::Session)
        {
            sessionView.grabKeyboardFocus();

            // W08: the ChannelTray is Arrange-only now (Session uses per-column mixer strips), so entering
            // Session view no longer seeds the tray from the grid's focus track.
        }
    }

    //==============================================================================
    // W04b tear-off panels: the mixer / piano-roll SHELL MEMBERS reparent into a floating
    // PopoutWindow and back. The window never owns a view (setContentNonOwned); restore
    // reparents home synchronously but DEFERS the window's destruction via callAsync, because
    // it may be running inside the window's own closeButtonPressed (PluginWindow discipline).
    // A project swap deliberately leaves tear-offs alone: the views rebind in place and the
    // window survives showing the rebound (or designed-empty) state.
    void tearOffMixer()
    {
        if (mixerPopout != nullptr) { mixerPopout->toFront (true); return; }   // already out: surface it

        mixerPopout = std::make_unique<PopoutWindow> ("Forge — Mixer", mixerView,
                                                      [this] { restoreMixer(); });
        mixerPopout->onUnhandledKey = [this] (const KeyPress& k) { return keyPressed (k); };

        // W05: restore the window's last position/size (saved at restore/close time). JUCE's
        // restoreWindowStateFromString includes an off-screen rescue, so a stale multi-monitor
        // state can't strand the window.
        if (const auto state = engine.getPropertyStorage().getPropertiesFile()
                                   .getValue ("forgeMixerPopoutState"); state.isNotEmpty())
            mixerPopout->restoreWindowStateFromString (state);

        resized();   // re-lay-out the shell without the torn-off view (the guards skip it)
        repaint();   // the empty-centre hint may now apply (tearing off while in Mix view)
    }

    void restoreMixer()
    {
        if (mixerPopout == nullptr)
            return;

        // Persist the window placement NOW, while the window is definitely alive — the deferred
        // lambda may run mid-close (W05; the dtor-body reset path saves too, for quit-with-open).
        engine.getPropertyStorage().getPropertiesFile()
            .setValue ("forgeMixerPopoutState", mixerPopout->getWindowStateAsString());

        // Reparent home, and hide until the post-reset relayout decides — the view arrives from
        // the window VISIBLE at stale popout bounds, topmost, and the synchronous resized() below
        // still sees a non-null popout so its guards skip this view (the W04b QC blocker: the
        // restored view overlaid the whole shell and ate every click).
        addChildComponent (mixerView);
        mixerView.setVisible (false);
        resized();

        // Deferred destruction: restore may be running inside the window's own close callback,
        // where a synchronous reset would delete the window mid-method. The deferred lambda must
        // ALSO re-run the layout — only after the pointer clears do the resized() guards re-own
        // this view's visibility/bounds. SafePointer guards a shell death before the lambda runs.
        Component::SafePointer<MainComponent> safeThis (this);
        MessageManager::callAsync ([safeThis]
        {
            if (auto* self = safeThis.getComponent())
            {
                self->mixerPopout.reset();
                self->resized();
                self->repaint();   // clear the empty-centre hint if we were in Mix view (W05)
            }
        });
    }

    void tearOffPianoRoll()
    {
        if (pianoRollPopout != nullptr) { pianoRollPopout->toFront (true); return; }

        pianoRollPopout = std::make_unique<PopoutWindow> ("Forge — Piano Roll", pianoRoll,
                                                          [this] { restorePianoRoll(); });
        pianoRollPopout->onUnhandledKey = [this] (const KeyPress& k) { return keyPressed (k); };

        if (const auto state = engine.getPropertyStorage().getPropertiesFile()
                                   .getValue ("forgePianoRollPopoutState"); state.isNotEmpty())
            pianoRollPopout->restoreWindowStateFromString (state);

        resized();   // the drawer falls back to the DetailView while the roll is out
    }

    void restorePianoRoll()
    {
        if (pianoRollPopout == nullptr)
            return;

        engine.getPropertyStorage().getPropertiesFile()
            .setValue ("forgePianoRollPopoutState", pianoRollPopout->getWindowStateAsString());

        addChildComponent (pianoRoll);
        pianoRoll.setVisible (false);   // hidden until the post-reset relayout decides (QC blocker)
        resized();

        // The relayout AND the focus grab both belong to the deferred turn: only after the popout
        // pointer clears do the resized() guards re-own the roll, and only then does isShowing()
        // reflect the true drawer/viewMode state instead of the stale visible flag carried over
        // from the window (the pre-fix code grabbed focus even restoring into a CLOSED drawer).
        Component::SafePointer<MainComponent> safeThis (this);
        MessageManager::callAsync ([safeThis]
        {
            if (auto* self = safeThis.getComponent())
            {
                self->pianoRollPopout.reset();
                self->resized();

                if (self->pianoRoll.isShowing())
                    self->pianoRoll.grabKeyboardFocus();
            }
        });
    }

    // W04b: the B/E region toggles slide instead of snapping. Opening shows the region immediately
    // and grows it from its current in-flight size (0 when settled closed); closing shrinks to 0
    // and only then flips the visible flag. A mid-flight re-toggle retargets from the CURRENT
    // in-flight value (dossier risk: restarting from the persisted size would jump). Persisted
    // sizes are untouched — only the resizer drags write them.
    void toggleSidebarAnimated()
    {
        const int shown = jlimit (browserMinWidth, browserMaxWidth, browserWidth);

        // "Open" = visible and not actively closing. A settled-visible region (slide == -1) can
        // only be fully open, WHATEVER the last target was — a direct `visible = true` write
        // (e.g. an auto-reveal) leaves the target stale at 0, and testing the target alone made
        // the first toggle bounce the region open again instead of closing it (QC major).
        if (browserVisible && ! (sidebarSlide >= 0 && sidebarSlideTarget == 0))
        {
            sidebarSlideTarget = 0;
            if (sidebarSlide < 0) sidebarSlide = shown;
        }
        else
        {
            browserVisible = true;
            sidebarSlideTarget = shown;
            if (sidebarSlide < 0) sidebarSlide = 0;
        }

        controlBar.setBrowserOpen (browserVisible);   // hands-on 1.2: sync the folder-icon tint (open edge)
        startSlide();
    }

    void toggleDrawerAnimated()
    {
        const int shown = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);

        if (drawerVisible && ! (drawerSlide >= 0 && drawerSlideTarget == 0))
        {
            drawerSlideTarget = 0;
            if (drawerSlide < 0) drawerSlide = shown;
        }
        else
        {
            drawerVisible = true;
            drawerSlideTarget = shown;
            if (drawerSlide < 0) drawerSlide = 0;
        }

        startSlide();
    }

    /** The one path for programmatic drawer opens (clip selection routing an editor into it).
        Retargets an in-flight close to open-from-current-height — a direct `drawerVisible = true`
        write raced the settle step, which flipped the flag back off and the requested editor
        never appeared (QC); it also keeps the slide target in sync so the next E closes. */
    void revealDrawer()
    {
        drawerVisible = true;
        drawerSlideTarget = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);

        if (drawerSlide >= 0)   // a close is in flight: retarget to open from the current height
            startSlide();
    }

    //==============================================================================
    // W05 undo/redo — a thin shell over the Edit's own UndoManager. Transaction granularity is
    // already per-gesture: the engine's UndoTransactionTimer seals a transaction 350 ms after
    // each change burst (deferred while the mouse is down), and the onEditMutated hooks seal
    // eagerly (sealUndoTransaction). ensureScenes stays deliberately OFF the stack (inhibitor +
    // clearUndoHistory in ProjectSession — do not "fix").
    void sealUndoTransaction()
    {
        if (auto* ed = session.getEdit())
            ed->getUndoManager().beginNewTransaction();
    }

    // A structural mutation (a slot/clip delete, or an undo/redo of one) can DETACH the clip the bottom
    // drawer is editing: the engine keeps the clip alive through the drawer's Ptr, but its state tree is
    // now parentless, so further edits write to a dead tree (silent no-op edits + undo-stack pollution —
    // not a crash). Close the drawer onto Detail whenever its held clip has lost its parent. Idempotent +
    // cheap (a live clip is a no-op), so it is safe to call after EVERY session mutation. Covers BOTH the
    // MIDI (piano-roll) and audio (DetailView) editors. Shared by undoOrRedo and the session mutation hook
    // (W07: the new Session "Delete clip" made this hazard reachable for slot clips, not just undo/redo).
    void reconcileDrawerClip()
    {
        if (auto* mc = pianoRoll.getClip())
            if (! mc->state.getParent().isValid())
            {
                pianoRoll.setMidiClip (nullptr);
                bottomMode = BottomMode::Detail;
                resized();
            }

        if (auto* c = detailView.getClip())
            if (! c->state.getParent().isValid())
                detailView.setClip (nullptr);
    }

    void doUndo()  { undoOrRedo (true); }
    void doRedo()  { undoOrRedo (false); }

    void undoOrRedo (bool isUndo)
    {
        auto* ed = session.getEdit();
        if (ed == nullptr)
            return;

        // Record gate: record-arm targets sit ON the undo stack (setTarget/removeTarget write
        // through the UM) and removeTarget fails while recording — an undo mid-take would
        // silently retarget the capture. An explicit no-op + status message beats relying on
        // Edit::undoOrRedo's force-stop.
        if (ed->getTransport().isRecording())
        {
            statusLabel.setText ("Undo is disabled while recording", dontSendNotification);
            statusHoldUntilMs = Time::getMillisecondCounter() + 4000;
            return;
        }

        auto& um = ed->getUndoManager();
        if (isUndo ? ! um.canUndo() : ! um.canRedo())
            return;

        if (isUndo) ed->undo();   // Edit::undo, not the raw UM — keeps the engine's selection refresh
        else        ed->redo();

        // Undo/redo does NOT fire onEditMutated and no Forge view listens to the state tree, so
        // fan the refresh out explicitly. SAVE first-class: Forge writes the file on every
        // mutation, so an unsaved undo leaves DISK newer than memory (a crash would "restore"
        // the pre-undo state).
        if (! session.save())
            FORGE_LOG_ERROR ("Failed to save project after undo/redo");

        arrangeView.rebuild();          // no self-heal timer — stale ClipComponents otherwise (UAF)
        if (sessionViewBinds())
            sessionView.rebuild();      // synchronous, so no column ever derefs a resurrected/deleted track
        mixerView.refreshControls();    // structural guard rebuilds strips if the track set changed
        channelTray.refreshNow();
        markerBar.refresh();

        // A redo-of-delete (or any structural undo/redo) can leave the drawer holding a detached clip —
        // reconcile it (now also covers the audio DetailView, not just the piano-roll).
        reconcileDrawerClip();

        statusLabel.setText (isUndo ? "Undo" : "Redo", dontSendNotification);
        statusHoldUntilMs = Time::getMillisecondCounter() + 1500;
    }

    void startSlide()
    {
        slideTimer.onTick = [this] { slideStep(); };
        if (! slideTimer.isTimerRunning())
            slideTimer.startTimerHz (60);
        slideStep();   // first step immediately so the toggle responds this frame
    }

    void slideStep()
    {
        // Ease-out lerp with a snap window: ~10 frames (~160 ms) for a full slide.
        auto step = [] (int current, int target)
        {
            const int next = current + roundToInt ((float) (target - current) * 0.35f);
            return std::abs (target - next) <= 2 ? target : next;
        };

        bool active = false;

        if (sidebarSlide >= 0)
        {
            sidebarSlide = step (sidebarSlide, sidebarSlideTarget);
            if (sidebarSlide == sidebarSlideTarget)
            {
                if (sidebarSlideTarget == 0)
                    browserVisible = false;
                controlBar.setBrowserOpen (browserVisible);   // hands-on 1.2: un-tint the folder icon on close-settle
                sidebarSlide = -1;   // settled
            }
            else
                active = true;
        }

        if (drawerSlide >= 0)
        {
            drawerSlide = step (drawerSlide, drawerSlideTarget);
            if (drawerSlide == drawerSlideTarget)
            {
                if (drawerSlideTarget == 0)
                    drawerVisible = false;
                drawerSlide = -1;
            }
            else
                active = true;
        }

        if (! active)
            slideTimer.stopTimer();

        resized();
    }

    void setupResizers()
    {
        // W04a: section sizes persist across launches via the engine's PropertiesFile (arbitrary
        // Forge keys can't use PropertyStorage's closed SettingID enum; the PropertiesFile is the
        // engine-sanctioned open store — PluginScanner already uses it). Loaded here (called once
        // from the ctor), saved on every completed drag — writes are coalesced by the file, so
        // per-drag saves are cheap.
        {
            auto& props = engine.getPropertyStorage().getPropertiesFile();
            browserWidth = jlimit (browserMinWidth, browserMaxWidth,
                                   props.getIntValue ("forgeBrowserWidth", browserWidth));
            drawerHeight = jlimit (drawerMinHeight, drawerMaxHeight,
                                   props.getIntValue ("forgeDrawerHeight", drawerHeight));
        }

        browserResizer.getCurrentSize = [this] { return browserWidth; };
        browserResizer.onResize       = [this] (int w)
        {
            browserWidth = w;
            resized();
            engine.getPropertyStorage().getPropertiesFile()
                .setValue ("forgeBrowserWidth", jlimit (browserMinWidth, browserMaxWidth, w));
        };

        drawerResizer.getCurrentSize  = [this] { return drawerHeight; };
        drawerResizer.onResize        = [this] (int h)
        {
            drawerHeight = h;
            resized();
            engine.getPropertyStorage().getPropertiesFile()
                .setValue ("forgeDrawerHeight", jlimit (drawerMinHeight, drawerMaxHeight, h));
        };
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
            // W05: undo/redo. Ctrl+Z / Ctrl+Shift+Z, with Ctrl+Y as the Windows-convention alias.
            if (letter == 'Z')
            {
                if (mods.isShiftDown()) doRedo();
                else                    doUndo();
                return true;
            }
            if (letter == 'Y') { doRedo(); return true; }

            if (letter == 'S')
            {
                if (mods.isShiftDown())  { if (controlBar.onSaveAs) controlBar.onSaveAs(); }
                else                     { if (controlBar.onSave)   controlBar.onSave(); }
                return true;
            }
            if (letter == 'O') { if (controlBar.onOpen)   controlBar.onOpen();   return true; }
            if (letter == 'N') { if (controlBar.onNew)    controlBar.onNew();    return true; }
            if (letter == 'I') { if (controlBar.onImport) controlBar.onImport(); return true; }
            if (letter == 'L') { showMidiLearnMenu();                            return true; }   // MIDI-learn picker (P2)

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

        if (letter == 'B') { toggleSidebarAnimated(); return true; }
        if (letter == 'E') { toggleDrawerAnimated();  return true; }
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
                                          self->swapProject ([self, f]
                                          {
                                              if (! self->session.openProject (f))
                                                  FORGE_LOG_ERROR ("Failed to open project: " + f.getFullPathName());
                                          });
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
                                      {
                                          self->setStatusMessage ("Save As failed: " + f.getFullPathName());
                                          FORGE_LOG_ERROR ("Save As failed: " + f.getFullPathName());
                                      }
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
            if (self->clip == nullptr)
                FORGE_LOG_ERROR ("Failed to import audio file: " + f.getFullPathName());
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

                                        // Async render off the message thread with a progress panel (P4).
                                        String err;
                                        auto render = Exporter::renderEditToWavAsync (*edit, f, err);

                                        if (render == nullptr)
                                        {
                                            FORGE_LOG_ERROR ("Export failed: " + err);
                                            self->setStatusMessage ("Export failed: " + err);
                                            return;
                                        }

                                        self->beginAsyncExport (std::move (render), "Exporting " + f.getFileName());
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

                                       // Async per-stem render off the message thread with progress (P4).
                                       String err;
                                       auto render = Exporter::renderStemsAsync (*edit, dir, err);

                                       if (render == nullptr)
                                       {
                                           FORGE_LOG_ERROR ("Stem export failed: " + err);
                                           self->setStatusMessage ("Stem export failed: " + err);
                                           return;
                                       }

                                       self->beginAsyncExport (std::move (render), "Exporting stems to " + dir.getFileName());
                                   });
    }

    // Takes ownership of a running AsyncRender handle (its factory already called begin()), wires the
    // progress/complete/cancel callbacks, and shows the transient progress panel. Wiring here is safe:
    // we are on the message thread and don't yield, so no deferred callAsync / 25Hz timer callback from
    // begin() can fire before the callbacks are in place. onComplete destroys the handle (message-thread).
    void beginAsyncExport (std::unique_ptr<Exporter::AsyncRender> render, const juce::String& caption)
    {
        if (render == nullptr)
            return;

        activeRender = std::move (render);   // supersede any finished-but-not-cleared handle

        exportProgress.setCaption (caption);
        exportProgress.setProgress (0.0f);

        Component::SafePointer<MainComponent> safeThis (this);

        exportProgress.onCancel = [safeThis]
        {
            if (auto* self = safeThis.getComponent())
                if (self->activeRender != nullptr)
                    self->activeRender->cancel();
        };

        activeRender->onProgress = [safeThis] (float p)
        {
            if (auto* self = safeThis.getComponent())
                self->exportProgress.setProgress (p);
        };

        activeRender->onComplete = [safeThis, caption] (bool ok, juce::String error)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            if (! ok && error.isNotEmpty())
                FORGE_LOG_ERROR ("Export failed: " + error);

            // Surface the measured integrated loudness (whole-edit exports only; the handle leaves it empty
            // for stems / failures). Read it from the still-alive handle BEFORE the reset below (item 4).
            juce::String loudnessSuffix;
            if (ok && self->activeRender != nullptr)
                if (auto l = self->activeRender->getLoudness(); l && std::isfinite (l->integratedLufs))
                    loudnessSuffix = "   ·   " + juce::String (l->integratedLufs, 1) + " LUFS";

            self->setStatusMessage (ok ? (error.isEmpty() ? caption + " — done" + loudnessSuffix
                                                           : caption + " — done (some stems failed): " + error)
                                       : "Export failed: " + error);
            self->exportProgress.setVisible (false);
            self->activeRender.reset();   // destroy the finished handle on the message thread
        };

        exportProgress.setVisible (true);
        resized();                     // position the centred overlay
        exportProgress.toFront (false);
    }

    // MIDI-learn (P2): a track ▸ plugin ▸ parameter picker. Choosing a parameter arms a learn on it; the
    // next controller delivered to MidiLearn::handleIncomingController binds the CC. (Hardware CC routing
    // into that entry point is a deferred follow-up — see docs; the mapping model + this trigger are wired.)
    void showMidiLearnMenu()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr)
            return;

        midiLearnTargets.clearQuick();
        PopupMenu menu;
        int id = 1;

        for (auto* track : te::getAudioTracks (*edit))
        {
            if (track == nullptr)
                continue;

            PopupMenu trackMenu;

            for (auto* plugin : PluginHost::getTrackInserts (*track))
            {
                if (plugin == nullptr)
                    continue;

                PopupMenu pluginMenu;

                for (auto& lp : PluginHost::getAutomatableParameters (*plugin))
                {
                    if (lp.param == nullptr)
                        continue;

                    midiLearnTargets.add (lp);
                    pluginMenu.addItem (id++, lp.name);
                }

                if (pluginMenu.getNumItems() > 0)
                    trackMenu.addSubMenu (plugin->getName(), pluginMenu);
            }

            if (trackMenu.getNumItems() > 0)
                menu.addSubMenu (track->getName(), trackMenu);
        }

        if (midiLearnTargets.isEmpty())
        {
            setStatusMessage ("MIDI Learn: no automatable plugin parameters — add a plugin first");
            return;
        }

        Component::SafePointer<MainComponent> safeThis (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
                            [safeThis] (int result)
                            {
                                auto* self = safeThis.getComponent();
                                if (self == nullptr || result <= 0)
                                    return;

                                const int idx = result - 1;
                                if (idx < 0 || idx >= self->midiLearnTargets.size())
                                    return;

                                if (auto* param = self->midiLearnTargets.getReference (idx).param)
                                {
                                    self->midiLearn.beginLearn (*param);
                                    self->setStatusMessage ("MIDI Learn armed: " + param->getParameterName()
                                                            + " — move a controller to bind");
                                }
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

            auto* ed = session.getEdit();
            if (ed == nullptr)
            {
                FORGE_LOG_ERROR ("Transport null after verifying clip import — engine state error");
                return;
            }

            auto& transport = ed->getTransport();
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
        else
        {
            FORGE_LOG_ERROR ("Failed to import test tone file for playback selftest");
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
            const auto openErr = EngineHelpers::ensureRecordingInputOpen (engine);   // lazily open the capture input
            if (openErr.isNotEmpty())
                FORGE_LOG_ERROR ("Failed to open recording input: device unavailable or hardware error");

            recorder.enableInputs();
            if (auto* track = te::getAudioTracks (*edit)[0])
                if (! recorder.armFirstInputToTrack (*edit, *track))
                    FORGE_LOG_ERROR ("Failed to arm input to track: " + recorder.getLastError());
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

        FORGE_LOG_INFO ("selftest-record: opening input");

        auto& jdm = engine.getDeviceManager().deviceManager;
        if (auto* type = jdm.getCurrentDeviceTypeObject())
            rcAvailableInputs = type->getDeviceNames (true).joinIntoString ("|");

        rcOpenError = EngineHelpers::ensureRecordingInputOpen (engine);   // record selftest opens input lazily too
        if (rcOpenError.isNotEmpty())
            FORGE_LOG_WARN ("Record selftest: failed to open recording input: " + rcOpenError);

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

        if (! rcTrackArmed)
            FORGE_LOG_WARN ("Record selftest: failed to arm input to track 0");
        else
            FORGE_LOG_INFO ("selftest-record: armed track 0");

        if (auto* t = session.getTransport())
        {
            t->record (false);
            rcRecordingStarted = t->isRecording();
            if (rcRecordingStarted)
                FORGE_LOG_INFO ("selftest-record: rolling");
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
               << "result="                 << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="                << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write record selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Record selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

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

        FORGE_LOG_INFO ("selftest-session: creating + launching clip in slot (0,0)");

        session.ensureScenes (16);
        ssClip = session.createMidiClipInSlot (0, 0, "SelfTest");
        ssClipCreated = (ssClip != nullptr);

        if (ssClipCreated)
            session.launchSlot (0, 0);   // per-track exclusivity + starts the transport (audible)
        else
            FORGE_LOG_ERROR ("Session selftest: failed to create MIDI clip in slot (0,0)");

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

        // Launch-quant seam round-trip (hands-on 1.3): prove the free-trigger global set/get. Run after
        // the launch above (which used the default 1-bar quant), so it can't affect this run; leaves the
        // default 'bar' restored.
        session.setGlobalLaunchQuantisation (te::LaunchQType::none);
        const bool launchQNone = session.getGlobalLaunchQuantisation() == te::LaunchQType::none;
        session.setGlobalLaunchQuantisation (te::LaunchQType::bar);
        const bool launchQBar  = session.getGlobalLaunchQuantisation() == te::LaunchQType::bar;
        const bool launchQRoundTrip = launchQNone && launchQBar;

        // PASS proves the launch PATH end-to-end: a born-audible clip created in a slot, launched,
        // with the transport rolling AND the clip's launch handle actually in the playing state.
        const bool pass = ssClipCreated && slotHasClip && hasLaunchHandle
                          && transportPlaying && clipPlaying
                          && launchQRoundTrip
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
               << "launchQRoundTrip=" << (launchQRoundTrip ? 1 : 0) << newLine
               << "result="           << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="          << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write session selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Session selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-taptempo (hands-on 1.4): the tap-tempo pure model + the EngineHelpers::setTempoAt
    // engine-write seam, both headless. Leg 1 asserts the TapTempo math with synthetic timestamps
    // (no engine); leg 2 writes a tempo through the clamped [20,300] seam and reads it back.
    void runTapTempoSelftest()
    {
        forge::transport::TapTempo t;
        t.tap (0.0);
        const bool oneTapNull = ! t.currentBpm().has_value();     // <2 taps -> no estimate
        t.tap (500.0); t.tap (1000.0); t.tap (1500.0);
        const bool bpm120 = t.currentBpm().has_value() && std::abs (*t.currentBpm() - 120.0) < 1e-6;
        t.tap (4000.0);                                           // gap 2500 ms > 2000 ms -> fresh sequence
        const bool gapReset = ! t.currentBpm().has_value();

        forge::transport::TapTempo fast;
        fast.tap (0.0); fast.tap (10.0);                          // 6000 BPM raw -> clamps to 300
        const bool clampHigh = fast.currentBpm().has_value() && std::abs (*fast.currentBpm() - 300.0) < 1e-6;

        bool engineWrite = false, engineClamp = false;
        if (auto* ed = session.getEdit())
        {
            EngineHelpers::setTempoAt (*ed, te::TimePosition(), 140.0);
            engineWrite = std::abs (ed->tempoSequence.getBpmAt (te::TimePosition()) - 140.0) < 1e-3;
            EngineHelpers::setTempoAt (*ed, te::TimePosition(), 5.0);   // below the 20 BPM floor -> clamps
            engineClamp = std::abs (ed->tempoSequence.getBpmAt (te::TimePosition()) - 20.0) < 1e-3;
        }

        const bool pass = oneTapNull && bpm120 && gapReset && clampHigh && engineWrite && engineClamp;

        String report;
        report << "mode=taptempo" << newLine
               << "oneTapNull="  << (oneTapNull ? 1 : 0) << newLine
               << "bpm120="      << (bpm120 ? 1 : 0) << newLine
               << "gapReset="    << (gapReset ? 1 : 0) << newLine
               << "clampHigh="   << (clampHigh ? 1 : 0) << newLine
               << "engineWrite=" << (engineWrite ? 1 : 0) << newLine
               << "engineClamp=" << (engineClamp ? 1 : 0) << newLine
               << "result="      << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="     << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write tap-tempo selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Tap-tempo selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // Automation-read selftest (W03 acceptance gate): headlessly prove that a volume automation
    // CURVE drives the track's volume during playback. Import a tone on track 0, write two points
    // to its volParam curve (slider pos 0.8 @ t=0 -> 0.2 @ t=2s, linear), force the read stream live
    // (updateStream, else activation waits on a deferred 10 ms timer), roll the transport, and poll
    // volParam->getCurrentValue(): it must read high early (>= 0.7) and low late (<= 0.45, curve is
    // 0.35 at 1.5 s). We assert on getCurrentValue() (the atomic the curve writes), NEVER the async
    // dB CachedValue. Looping is disabled so position can't wrap before the late sample.
    void beginAutomationSelftest()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr) { finishAutomationSelftest(); return; }

        FORGE_LOG_INFO ("selftest-automation: importing tone + writing a volume curve on track 0");

        // 1. Import a tone on track 0 so there's audio to drive (30 s so the 0..4 s window never runs out).
        sineFile = createSineWaveFile (44100.0, 30.0, 440.0, 0.2f);
        clip = session.importAudioFile (sineFile, te::TimePosition());
        if (clip == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-automation: failed to import test tone");
            finishAutomationSelftest();
            return;
        }
        arrangeView.rebuild();

        // 2. Grab the track-0 volume automation parameter via the seam.
        auto* track = te::getAudioTracks (*edit)[0];
        if (track == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-automation: no audio track 0");
            finishAutomationSelftest();
            return;
        }

        auVolParam = AutomationHelpers::getTrackVolumeParam (*track);
        if (auVolParam == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-automation: track 0 has no volume parameter");
            finishAutomationSelftest();
            return;
        }

        // 3. Write a falling volume curve (slider position units): 0.8 @ t=0 -> 0.2 @ t=2s, linear.
        AutomationHelpers::clearAutomation (*auVolParam);
        AutomationHelpers::addPoint (*auVolParam, 0.0, 0.8f, 0.0f);
        AutomationHelpers::addPoint (*auVolParam, 2.0, 0.2f, 0.0f);   // addPoint calls updateStream()

        // 4. Force automation read ON (defaults true; assert so a stale/toggled state can't silently
        //    gate the whole path off) and capture the static preconditions.
        edit->getAutomationRecordManager().setReadingAutomation (true);
        auReadingAutomation = edit->getAutomationRecordManager().isReadingAutomation();
        auAutomationActive  = AutomationHelpers::isActive (*auVolParam);
        auNumPoints         = AutomationHelpers::getNumPoints (*auVolParam);
        auStaticValueAt1p5  = AutomationHelpers::getValueAt (*auVolParam, 1.5);

        // Preconditions must hold before we bother rolling — fail fast with a diagnostic.
        if (! auAutomationActive || auNumPoints != 2
            || std::abs (auStaticValueAt1p5 - 0.35f) > 0.01f || ! auReadingAutomation)
        {
            FORGE_LOG_ERROR ("selftest-automation: preconditions failed (active=" + juce::String (auAutomationActive ? 1 : 0)
                             + " points=" + juce::String (auNumPoints)
                             + " valueAt1.5=" + juce::String (auStaticValueAt1p5, 4)
                             + " reading=" + juce::String (auReadingAutomation ? 1 : 0) + ")");
            finishAutomationSelftest();
            return;
        }

        // 5. Roll. Looping OFF so the playhead can't wrap before the late sample; drain the device
        //    change cascade and block until the stream is actually rolling (playback-selftest idiom).
        auto& transport = edit->getTransport();
        transport.looping = false;
        transport.ensureContextAllocated();
        engine.getDeviceManager().dispatchPendingUpdates();
        transport.setPosition (te::TimePosition());
        transport.play (false);
        if (auto* epc = transport.getCurrentPlaybackContext())
            epc->blockUntilSyncPointChange();

        // 6. Poll at 10 Hz. Capture the FIRST tick unconditionally as the early sample (don't require
        //    pos < 0.3 s); the timerCallback automation branch bounds the poll (~4 s) so a non-rolling
        //    device FAILs instead of hanging, and grabs the late sample in the 1.5..2.4 s window.
        auEarlyCaptured = false;
        auLateCaptured  = false;
        auEarlyValue    = -1.0f;
        auLateValue     = -1.0f;
        auEarlyPos      = -1.0;
        automationPollTicks = 0;
        startTimerHz (10);
    }

    void finishAutomationSelftest()
    {
        if (auto* t = session.getTransport())
            t->stop (false, false);

        double finalPos = -1.0;
        if (auto* t = session.getTransport())
            finalPos = t->getPosition().inSeconds();

        // PASS proves the automation-read PATH end-to-end: an active 2-point volume curve whose static
        // math is correct, read ON, and getCurrentValue() following the curve high->low across playback.
        const bool pass = clip != nullptr
                          && auVolParam != nullptr
                          && auReadingAutomation
                          && auAutomationActive
                          && auNumPoints == 2
                          && std::abs (auStaticValueAt1p5 - 0.35f) < 0.01f
                          && auEarlyCaptured && auLateCaptured
                          && auEarlyValue >= 0.7f
                          && auLateValue  <= 0.45f;

        String report;
        report << "mode=automation" << newLine
               << "importedClip="        << (clip != nullptr ? 1 : 0) << newLine
               << "readingAutomation="   << (auReadingAutomation ? 1 : 0) << newLine
               << "automationActive="    << (auAutomationActive ? 1 : 0) << newLine
               << "numPoints="           << auNumPoints << newLine
               << "staticValueAt1.5="    << String (auStaticValueAt1p5, 4) << newLine
               << "earlyCaptured="       << (auEarlyCaptured ? 1 : 0) << newLine
               << "earlyPosSecs="        << String (auEarlyPos, 3) << newLine
               << "earlyValue="          << String (auEarlyValue, 4) << newLine
               << "lateCaptured="        << (auLateCaptured ? 1 : 0) << newLine
               << "lateValue="           << String (auLateValue, 4) << newLine
               << "finalPosSecs="        << String (finalPos, 3) << newLine
               << "result="              << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="             << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write automation selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Automation selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-midi (docs/devlog/midi-record-design.md §4): prove MIDI capture straight into a Session
    // ClipSlot with ZERO hardware, by creating a virtual MIDI input, arming slot (0,0), rolling the
    // transport, injecting synthetic notes, then verifying the slot's committed clip captured exactly
    // those notes. Event-driven with the record selftest's yield discipline: each async device op must
    // settle before the next step, or the isEnabled()&&isAvailableToEdit() gate / record graph isn't ready.

    // Find our engine-owned virtual MIDI-in by name and downcast so we can inject through it.
    te::VirtualMidiInputDevice* findSelfTestMidiInput() const
    {
        for (auto& mi : engine.getDeviceManager().getMidiInDevices())
            if (mi != nullptr && mi->getName() == kSelfTestMidiName)
                return dynamic_cast<te::VirtualMidiInputDevice*> (mi.get());
        return nullptr;
    }

    // Phase 1: assert slot (0,0) empty, ensure a born-audible instrument, create the virtual MIDI in,
    // then YIELD so the create's async rescanMidiDeviceList delivers the device.
    void beginSelfTestMidi()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr) { finishSelfTestMidi(); return; }

        FORGE_LOG_INFO ("selftest-midi: creating virtual MIDI input + arming slot (0,0)");

        session.ensureScenes (16);

        // The target slot MUST start empty, else capturedNoteCount could count pre-seeded notes.
        miPreExistingNotes = 0;
        if (auto* slot = session.getClipSlot (0, 0))
            if (auto* c = slot->getClip())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                    miPreExistingNotes = mc->getSequence().getNumNotes();

        // Born-audible target track (4OSC); do NOT pre-insert a clip in slot (0,0).
        if (auto* track = te::getAudioTracks (*edit)[0])
            PluginHost::ensureDefaultInstrument (*track);

        // Drop any leaked device of the same name from a prior aborted run, then create fresh.
        if (auto* stale = findSelfTestMidiInput())
            engine.getDeviceManager().deleteVirtualMidiDevice (*stale);

        const auto r = engine.getDeviceManager().createVirtualMidiDevice (kSelfTestMidiName);
        if (r.failed())
            FORGE_LOG_ERROR ("selftest-midi: createVirtualMidiDevice failed: " + r.getErrorMessage());

        miPhase = 1;
        startTimer (300);   // YIELD: let the create's async rescan deliver the device
    }

    // Phase 2: find + enable the virtual device (automatic monitoring), then YIELD so the enable's async
    // rescan settles before ensureContextAllocated reads the isEnabled()&&isAvailableToEdit() gate.
    void midiSelftestEnableDevice()
    {
        miDevice = findSelfTestMidiInput();

        juce::StringArray names;
        for (auto& mi : engine.getDeviceManager().getMidiInDevices())
            if (mi != nullptr) names.add (mi->getName());
        miAvailableMidiIns = names.joinIntoString ("|");

        if (miDevice != nullptr)
        {
            miDevice->setEnabled (true);
            miDevice->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            miDeviceEnabled = miDevice->isEnabled();
        }
        else
        {
            FORGE_LOG_ERROR ("selftest-midi: virtual MIDI input not found after create");
        }

        miPhase = 2;
        startTimer (300);   // YIELD: let setEnabled's async rescan settle before arming
    }

    // Phase 3: allocate the context (AFTER the device is enabled), arm ONLY slot (0,0) as the record
    // target (never the track — two targets would double-capture and mask a wrong-target bug), roll the
    // transport, then YIELD so the record graph is live before injection.
    void midiSelftestArmAndRoll()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr || miDevice == nullptr) { finishSelfTestMidi(); return; }

        edit->getTransport().ensureContextAllocated();

        if (auto* slot = session.getClipSlot (0, 0))
        {
            for (auto* instance : edit->getAllInputDevices())
            {
                if (instance == nullptr || &instance->getInputDevice() != miDevice)
                    continue;   // arm ONLY our virtual device's instance

                auto res = instance->setTarget (slot->itemID, /*moveToTrack=*/false, &edit->getUndoManager(), 0);
                if (res && res.value() != nullptr)
                {
                    instance->setRecordingEnabled (slot->itemID, true);
                    miSlotArmed = true;
                }
            }
        }

        edit->restartPlayback();

        if (auto* t = session.getTransport())
        {
            t->record (false);
            miRecordingStarted = t->isRecording();
        }

        miPhase = 3;
        startTimer (300);   // YIELD: record graph live before we inject
    }

    // Phase 4: inject 4 note-ons; YIELD so the notes have real (non-zero) length before the note-offs
    // (a zero-length note can be dropped, which would fail the exact capturedNoteCount==injected gate).
    void midiSelftestInjectOn()
    {
        if (miDevice != nullptr)
            for (int n : { 60, 64, 67, 72 })   // C4 E4 G4 C5
            {
                miDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOn (1, n, 0.8f), miDevice->getMPESourceID());
                ++miNotesInjected;
            }

        miPhase = 4;
        startTimer (300);   // hold the notes so each has real length
    }

    // Phase 5-prep: inject the matching note-offs, then YIELD so the take finalises before stop.
    void midiSelftestInjectOff()
    {
        if (miDevice != nullptr)
            for (int n : { 60, 64, 67, 72 })
                miDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOff (1, n), miDevice->getMPESourceID());

        miPhase = 5;
        startTimer (500);   // let the note-offs + take settle before we stop
    }

    // Phase 5: stop (the engine commits the captured notes into a MidiClip in the slot via
    // addMidiAsTransaction), disarm the slot, verify the note count, delete the virtual device, report.
    void finishSelfTestMidi()
    {
        auto* edit = session.getEdit();

        if (auto* t = session.getTransport())
            t->stop (false, false);

        // Disarm slot (0,0) on our virtual device's instance(s) (transport already stopped above).
        if (edit != nullptr)
            if (auto* slot = session.getClipSlot (0, 0))
                for (auto* instance : edit->getAllInputDevices())
                {
                    if (instance == nullptr || (miDevice != nullptr && &instance->getInputDevice() != miDevice))
                        continue;
                    instance->setRecordingEnabled (slot->itemID, false);
                    [[maybe_unused]] const auto removed = instance->removeTarget (slot->itemID, &edit->getUndoManager());
                }

        // Verify the captured clip landed in the SLOT with the expected note count.
        if (auto* slot = session.getClipSlot (0, 0))
            if (auto* c = slot->getClip())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                {
                    miClipCreated  = true;
                    miCapturedNotes = mc->getSequence().getNumNotes();
                }

        // MANDATORY cleanup: the virtual device name persists in engine storage; a leak fails the NEXT
        // run's createVirtualMidiDevice ("name already in use").
        if (miDevice != nullptr)
        {
            engine.getDeviceManager().deleteVirtualMidiDevice (*miDevice);
            miDevice = nullptr;
        }

        // PASS proves end-to-end capture INTO THE SLOT: device enabled, slot (not track) armed, transport
        // rolled, notes injected, a clip materialised in the slot, and it holds EXACTLY the injected notes
        // (exact count rules out an empty clip, pre-seeded notes, or notes leaking to the wrong target).
        const bool pass = miDeviceEnabled && miSlotArmed && miRecordingStarted
                          && miPreExistingNotes == 0
                          && miNotesInjected >= 4
                          && miClipCreated
                          && miCapturedNotes == miNotesInjected;

        String report;
        report << "mode=midi" << newLine
               << "availableMidiInputs=" << (miAvailableMidiIns.isEmpty() ? String ("(none)") : miAvailableMidiIns) << newLine
               << "midiDeviceEnabled="   << (miDeviceEnabled ? 1 : 0) << newLine
               << "preExistingNotes="    << miPreExistingNotes << newLine
               << "trackArmed="          << (miSlotArmed ? 1 : 0) << newLine
               << "recordingStarted="    << (miRecordingStarted ? 1 : 0) << newLine
               << "notesInjected="       << miNotesInjected << newLine
               << "clipCreated="         << (miClipCreated ? 1 : 0) << newLine
               << "capturedNoteCount="   << miCapturedNotes << newLine
               << "result="              << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="             << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write midi selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("MIDI selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-sync (W03): prove MIDI-clock OUTPUT end-to-end, headless, no hardware analyzer, by
    // subclassing te::MidiOutputDevice (MidiClockProbeDevice) and capturing the bytes the engine sends
    // through the real graph (clock-only device inclusion) + generator (24 PPQN) + dispatcher (1ms timer).
    //
    // Three load-bearing corrections over the naive recipe (Skeptic verdicts, dossier):
    //   (1) DEVICE CONFLICT: the engine's own copy of every enabled MIDI out is opened at startup, and the
    //       GS synth is single-client under WinMM, so a naive second open of the same identifier fails ->
    //       we closeDevice() and ERASE the engine's same-identifier entry from dm.midiOutputs, then inject
    //       the probe as sole owner of that port (never via setEnabled, which triggers a rescan).
    //   (2) DEGRADE DETECTION: base openDevice() returns an empty (success-looking) string when the device
    //       is disabled -> we check probe->isOpen() (outputDevice != nullptr), never the return string.
    //   (3) SCAN ORDERING: the initial MIDI scan is a juce::Timer one-shot NOT flushed by
    //       dispatchPendingUpdates -> we rescanMidiDeviceList() then YIELD until dm.midiOutputs is
    //       populated BEFORE freezing the interval + injecting (else the wholesale swap wipes the probe).

    // Phase 0: freeze nothing yet — kick a rescan and yield so dm.midiOutputs is populated first.
    void beginSelfTestSync()
    {
        FORGE_LOG_INFO ("selftest-sync: scanning MIDI outputs before probe injection");

        auto& dm = engine.getDeviceManager();
        sySavedScanInterval = (int) engine.getPropertyStorage()
                                  .getProperty (te::SettingID::midiScanIntervalSeconds, 4);   // restore on every exit path

        // Fix (3): schedule the one-shot scan and YIELD; the periodic 4s timer would otherwise be the
        // first thing to populate dm.midiOutputs, far too late for our 300ms cadence.
        dm.rescanMidiDeviceList();

        syPhase = 1;
        startTimer (400);   // YIELD: let the 5ms one-shot applyNewMidiDeviceList() populate dm.midiOutputs
    }

    // Phase 1: MIDI scan has settled. NOW freeze the periodic rescan, resolve a real out, evict the
    // engine's same-identifier copy, inject the probe, reallocate the playback context, and roll.
    void syncSelftestInject()
    {
        auto& dm = engine.getDeviceManager();

        // Fix (3, cont.): freeze the periodic rescan only AFTER the initial population, so no wholesale
        // swap wipes the probe. NOTE: this persists to property storage (SettingID::midiScanIntervalSeconds),
        // so it MUST be restored on every exit path — see finishSelfTestSync().
        dm.setMidiDeviceScanIntervalSeconds (0);

        const auto outs = juce::MidiOutput::getAvailableDevices();
        if (outs.isEmpty())
        {
            // Honest SKIP-degrade: no MIDI outs exist on this box. Prove only the property round-trip
            // and a no-crash roll; never claim a clock-out PASS we cannot back with captured bytes.
            FORGE_LOG_WARN ("selftest-sync: no MIDI outputs present — taking SKIP-degrade path");
            syDegraded = true;

            // Property round-trip on a bare (un-injected, un-opened) probe: setSendingClock persists +
            // reflects, with no device rescan. Scoped so its dtor's final saveProps lands BEFORE we
            // remove the phantom props node it writes under its selftest-only name.
            {
                auto bare = std::make_shared<MidiClockProbeDevice> (engine, juce::MidiDeviceInfo ("Forge SelfTest Clock (degraded)", "forge-selftest-clock-degraded"));
                bare->setSendingClock (true);
                syPropRoundTrip = bare->isSendingClock();
            }
            engine.getPropertyStorage().removePropertyItem (te::SettingID::midiout, "Forge SelfTest Clock (degraded)");

            // No-crash roll with no clock device in the graph.
            if (auto* t = session.getTransport())
            {
                t->play (false);
                syRolled = t->isPlaying();
            }

            syPhase = 2;
            startTimer (500);   // brief roll, then finish (which stops + restores + reports)
            return;
        }

        // Prefer the software Microsoft GS synth if present (no hardware needed); else take the first out.
        int chosen = 0;
        for (int i = 0; i < outs.size(); ++i)
            if (outs[i].name.containsIgnoreCase ("Microsoft GS "))
            {
                chosen = i;
                break;
            }
        const auto info = outs[chosen];
        syOutName = info.name;
        FORGE_LOG_INFO ("selftest-sync: probing MIDI output '" + syOutName + "'");

        // Fix (1): free any existing playback context first (so no instance references dm.midiOutputs
        // entries while we mutate the vector), then evict the engine's own copy of this identifier so the
        // probe is the SOLE opener of the WinMM port.
        if (auto* t = session.getTransport())
            t->freePlaybackContext();

        // NOTE: the engine assigns its own device IDs (e.g. "out_81b0d7ef"), so getDeviceID() can NOT be
        // compared against the raw juce identifier — match by NAME (unique per system MIDI out). Keep the
        // evicted entry alive so teardown can restore the engine's device list exactly as it found it.
        for (int i = dm.getNumMidiOutDevices(); --i >= 0;)
            if (auto* out = dm.getMidiOutDevice (i))
                if (out->getName() == info.name || out->getDeviceID() == info.identifier)
                {
                    syEvicted = dm.midiOutputs[(size_t) i];   // hold the engine's entry for re-insertion
                    out->closeDevice();               // release the port; does NOT trigger a rescan
                    dm.midiOutputs.erase (dm.midiOutputs.begin() + i);
                }

        // Snapshot the REAL device's persisted props node BEFORE the probe touches it: MidiOutputDevice
        // props are keyed by device NAME, so the probe's saveProps calls (setSendingClock, closeDevice,
        // dtor) would otherwise permanently rewrite the real device's stored enabled/sendMidiClock —
        // and a killed run would leave clock stuck ON for normal launches. Restored at teardown.
        syPropsSnapshot = engine.getPropertyStorage().getXmlPropertyItem (te::SettingID::midiout, info.name);

        // Inject the probe. Force enabled=true directly (protected OutputDevice::enabled) — NEVER via
        // setEnabled(), which would trigger rescanMidiDeviceList() and swap our probe out.
        syProbe = std::make_shared<MidiClockProbeDevice> (engine, info);
        syProbe->forceEnabledForSelfTest();           // sets the protected enabled flag — see MidiClockProbe.h
        syProbe->setSendingClock (true);              // changed() + saveProps(), no rescan
        syPropRoundTrip = syProbe->isSendingClock();

        // Shrink the kill window: put the persisted node back immediately (the probe's in-memory state
        // is unaffected — nothing reloads props mid-run), so even a force-killed run leaves the stored
        // props untouched. Teardown repeats this restore AFTER the probe's last saveProps (its dtor).
        restoreSyncProbeProps();

        // Stale-props guard: loadProps keys by device NAME, so a saved enabled=false could have slipped in.
        // Assert both flags explicitly after construction/force.
        syEnabledOK = syProbe->isEnabled();

        const auto openErr = syProbe->openDevice();   // return string is unreliable when disabled — Fix (2)
        syProbeOpen = syProbe->isOpen();              // the HONEST gate: the underlying port actually opened
        if (! syProbeOpen)
            FORGE_LOG_WARN ("selftest-sync: probe port did not open (" + (openErr.isEmpty() ? juce::String ("no error string") : openErr)
                            + ") — clock generation is gated off; degrading");

        dm.midiOutputs.push_back (syProbe);           // public vector; scan is frozen so it survives

        if (! syProbeOpen)
        {
            // Port didn't open => clock gen is gated off. Degrade honestly rather than assert a hollow
            // PASS — but still do the no-crash roll (mirrors the zero-outs path), else the degraded
            // pass criteria (propRoundTrip && rolled) could only ever report FAIL (QC minor).
            syDegraded = true;

            if (auto* t = session.getTransport())
            {
                t->ensureContextAllocated (/*alwaysReallocate=*/true);
                t->play (false);
                syRolled = t->isPlaying();
            }

            syPhase = 2;
            startTimer (500);
            return;
        }

        // Build a FRESH playback context now that the probe is in dm.midiOutputs, so rebuildDeviceList
        // enumerates it (an instance is created for each enabled out). alwaysReallocate=true forces the
        // rebuild even if a context lingered.
        if (auto* edit = session.getEdit())
        {
            syExpectedClocks = 0.0;
            if (auto* t = session.getTransport())
            {
                t->ensureContextAllocated (/*alwaysReallocate=*/true);
                t->play (false);
                syRolled = t->isPlaying();
            }

            // Expected clock count = seconds * (bpm/60) * 24. Read bpm LIVE so a non-120 default still passes.
            const double bpm = edit->tempoSequence.getBpmAt (te::TimePosition());
            syExpectedClocks = 2.0 * (bpm / 60.0) * 24.0;   // ~2s roll below
        }

        syPhase = 2;
        startTimer (2000);   // roll ~2s so the generator emits a solid clock train, then finish
    }

    // Puts the real MIDI-out device's persisted props back exactly as we found them (or removes the
    // node if none existed) — the probe shares the device's NAME, so every probe saveProps (from
    // setSendingClock, closeDevice, and its dtor) rewrites the real device's stored state. Idempotent;
    // called immediately after the probe's first saveProps AND after its destruction at teardown.
    void restoreSyncProbeProps()
    {
        if (syOutName.isEmpty())
            return;

        if (syPropsSnapshot != nullptr)
            engine.getPropertyStorage().setXmlPropertyItem (te::SettingID::midiout, syOutName, *syPropsSnapshot);
        else
            engine.getPropertyStorage().removePropertyItem (te::SettingID::midiout, syOutName);
    }

    // Phase 2: stop with clearDevices=false so the graph processes the play->stop edge that emits
    // midiStop, then YIELD one timer turn so the MidiNoteDispatcher's 1 ms timer actually flushes it
    // through sendMessageNow before we snapshot (the first run proved snapshotting immediately after
    // stop() races that flush: 96/96 clocks captured but stopCount=0).
    void syncSelftestStop()
    {
        if (auto* t = session.getTransport())
            t->stop (/*discardRecordings=*/false, /*clearDevices=*/false);

        syPhase = 3;
        startTimer (300);   // drain window for the dispatcher's midiStop
    }

    // Phase 3: snapshot the captured messages, verify, tear down in the correct order, restore the
    // scan interval on ALL paths, report, quit.
    void finishSelfTestSync()
    {
        auto& dm = engine.getDeviceManager();

        // Belt-and-suspenders: the transport is normally stopped by phase 2, but failure paths can
        // reach here directly.
        if (auto* t = session.getTransport())
            if (t->isPlaying())
                t->stop (/*discardRecordings=*/false, /*clearDevices=*/false);

        bool pass = false;

        if (syDegraded)
        {
            // Honest degrade PASS: property round-trip held and the transport rolled without crashing.
            pass = syPropRoundTrip && syRolled;
        }
        else if (syProbe != nullptr)
        {
            const auto msgs = syProbe->snapshot();
            for (const auto& m : msgs)
            {
                if (m.isSongPositionPointer())            ++sySpp;
                if (m.isMidiStart() || m.isMidiContinue()) ++syStartCont;
                if (m.isMidiClock())                       ++syClock;
                if (m.isMidiStop())                        ++syStop;
            }

            const bool clockCountOK = syExpectedClocks > 0.0
                                       && syClock >= (int) (0.5 * syExpectedClocks)
                                       && syClock <= (int) (1.5 * syExpectedClocks);

            // Full PASS proves end-to-end clock-out: probe opened + enabled, flag round-tripped, transport
            // rolled, and the ACTUAL bytes on the wire include SPP + start/continue + ~bpm*24 clocks + a stop.
            pass = syProbeOpen && syEnabledOK && syPropRoundTrip && syRolled
                   && sySpp >= 1 && syStartCont >= 1 && clockCountOK && syStop >= 1;
        }

        // TEARDOWN ORDER (dossier step 7): free the Edit's context FIRST (its instances reference the
        // device), THEN erase the probe from dm.midiOutputs, THEN restore the scan interval. Do all of
        // this on EVERY path (including failures) so the box is left as we found it.
        if (auto* t = session.getTransport())
            t->freePlaybackContext();

        if (syProbe != nullptr)
        {
            for (int i = dm.getNumMidiOutDevices(); --i >= 0;)
                if (dm.getMidiOutDevice (i) == syProbe.get())
                    dm.midiOutputs.erase (dm.midiOutputs.begin() + i);

            syProbe->closeDevice();
            syProbe.reset();          // dtor's closeDevice/saveProps is the probe's LAST props write...
            restoreSyncProbeProps();  // ...so the lossless restore must come after it (QC minor)
        }

        // Restore the engine's own evicted device entry so the DeviceManager is structurally exactly as
        // we found it (the port is closed; the engine reopens it on demand).
        if (syEvicted != nullptr)
        {
            dm.midiOutputs.push_back (syEvicted);
            syEvicted.reset();
        }

        dm.setMidiDeviceScanIntervalSeconds (sySavedScanInterval);   // restore persisted interval

        String report;
        report << "mode=sync" << newLine
               << "midiOutChosen="   << (syOutName.isEmpty() ? String ("(none)") : syOutName) << newLine
               << "skip-degraded="   << (syDegraded ? 1 : 0) << newLine
               << "propRoundTrip="   << (syPropRoundTrip ? 1 : 0) << newLine
               << "probeEnabled="    << (syEnabledOK ? 1 : 0) << newLine
               << "probeOpen="       << (syProbeOpen ? 1 : 0) << newLine
               << "rolled="          << (syRolled ? 1 : 0) << newLine
               << "sppCount="        << sySpp << newLine
               << "startContCount="  << syStartCont << newLine
               << "clockCount="      << syClock << newLine
               << "expectedClocks="  << (int) syExpectedClocks << newLine
               << "stopCount="       << syStop << newLine
               << "result="          << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="         << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write sync selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("SYNC selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + (syDegraded ? " (degraded)" : "") + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-midilearn (P2, docs/devlog/wave-01-features.md): prove the MidiLearn seam binds a CC to a
    // plugin parameter over Tracktion's native ParameterControlMappings with NO focused Edit (Forge uses the
    // default UIBehaviour). A virtual MIDI device does NOT route CC to the engine's parser, so the CC is
    // injected through the seam's own handleIncomingController — the same entry a Forge MIDI-input listener
    // would use. The native bind lands on an AsyncUpdater, so arm / inject / verify are separate loop turns.

    // Phase 0: ensure a born-audible plugin (4OSC) on track 0, pick its first automatable param, arm a learn,
    // then YIELD so the native learn-arm settles before the CC is injected.
    void beginSelfTestMidiLearn()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr) { finishSelfTestMidiLearn(); return; }

        FORGE_LOG_INFO ("selftest-midilearn: adding an instrument, arming a learn on its first param");

        auto tracks = te::getAudioTracks (*edit);
        auto* track = tracks.isEmpty() ? nullptr : tracks[0];
        if (track == nullptr) { FORGE_LOG_ERROR ("selftest-midilearn: no audio track"); finishSelfTestMidiLearn(); return; }

        PluginHost::ensureDefaultInstrument (*track);   // 4OSC at chain head — has automatable params

        // First automatable parameter of the first insert (the 4OSC; volume/meter are excluded).
        for (auto* plugin : PluginHost::getTrackInserts (*track))
        {
            if (plugin == nullptr)
                continue;

            auto params = PluginHost::getAutomatableParameters (*plugin);
            if (! params.isEmpty()) { mlParam = params.getReference (0).param; break; }
        }

        if (mlParam == nullptr) { FORGE_LOG_ERROR ("selftest-midilearn: no automatable parameter found"); finishSelfTestMidiLearn(); return; }

        mlParamName       = mlParam->getParameterName();
        mlWasMappedBefore = MidiLearn::isMapped (*mlParam);   // must be false at the start

        midiLearn.setActiveEdit (edit);
        midiLearn.beginLearn (*mlParam);                       // arm the native learn (no focused Edit needed)
        mlLearnArmed = midiLearn.isLearning();

        mlPhase = 1;
        startTimer (300);   // YIELD before injecting the CC
    }

    // Phase 1: inject a controller change through the seam (cc 74, ch 1). The bind schedules on the engine's
    // AsyncUpdater, so YIELD again before verifying.
    void midiLearnSelftestInjectCC()
    {
        FORGE_LOG_INFO ("selftest-midilearn: injecting CC 74 / ch 1 via handleIncomingController");
        midiLearn.handleIncomingController (/*cc*/ 74, /*value0to1*/ 0.5f, /*channel*/ 1);
        mlPhase = 2;
        startTimer (300);   // YIELD: let the AsyncUpdater dispatch the bind
    }

    // Phase 2: verify the parameter is now mapped to CC 74 / ch 1, report, quit.
    void finishSelfTestMidiLearn()
    {
        if (mlParam != nullptr)
        {
            mlIsMappedAfter = MidiLearn::isMapped (*mlParam);
            MidiLearn::getMappedCC (*mlParam, mlCc, mlCh);
        }

        // PASS proves the seam completes a learn with no focused Edit: the param was unmapped, beginLearn
        // armed the native learn, a single injected CC bound it, and the store reports exactly that CC/channel.
        const bool pass = mlLearnArmed
                          && ! mlWasMappedBefore
                          && mlIsMappedAfter
                          && mlCc == 74
                          && mlCh == 1;

        String report;
        report << "mode=midilearn" << newLine
               << "param="           << (mlParamName.isEmpty() ? String ("(none)") : mlParamName) << newLine
               << "wasMappedBefore="  << (mlWasMappedBefore ? 1 : 0) << newLine
               << "learnArmed="       << (mlLearnArmed ? 1 : 0) << newLine
               << "isMappedAfter="    << (mlIsMappedAfter ? 1 : 0) << newLine
               << "mappedCc="         << mlCc << newLine
               << "mappedChannel="    << mlCh << newLine
               << "result="           << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="          << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write midilearn selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("MIDI-learn selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-midiinput: prove the ForgeUIBehaviour focused-Edit wiring that lets REAL hardware CCs
    // reach parameter mappings. Source fact (tracktion): the MidiControllerParser that routes a CC to
    // getCurrentlyFocusedMappings() lives ONLY in PhysicalMidiInputDevice (PhysicalMidiInputDevice.cpp) —
    // a VirtualMidiInputDevice has NO controller parser, so a CC injected headlessly can never drive a
    // param. What IS deterministic (and is exactly the gap we closed) is: (1) the engine now reports our
    // open Edit as the focused Edit — null under the default UIBehaviour, so getCurrentlyFocusedMappings()
    // found nothing and no physical CC could route — and (2) a CC binds to a param in that Edit's store.
    // The final mile, a physical controller's parser actually firing, is a real-hardware smoke item.

    // Phase 0: instrument + first automatable param, bind CC 74 -> it via the seam, YIELD for the async bind.
    void beginSelfTestMidiInput()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr) { finishSelfTestMidiInput(); return; }

        FORGE_LOG_INFO ("selftest-midiinput: assert the ForgeUIBehaviour focused Edit + a CC->param bind");

        auto tracks = te::getAudioTracks (*edit);
        auto* track = tracks.isEmpty() ? nullptr : tracks[0];
        if (track == nullptr) { FORGE_LOG_ERROR ("selftest-midiinput: no audio track"); finishSelfTestMidiInput(); return; }

        PluginHost::ensureDefaultInstrument (*track);

        for (auto* plugin : PluginHost::getTrackInserts (*track))
        {
            if (plugin == nullptr) continue;
            auto params = PluginHost::getAutomatableParameters (*plugin);
            if (! params.isEmpty()) { miiParam = params.getReference (0).param; break; }
        }

        if (miiParam == nullptr) { FORGE_LOG_ERROR ("selftest-midiinput: no automatable parameter"); finishSelfTestMidiInput(); return; }
        miiParamName = miiParam->getParameterName();

        midiLearn.setActiveEdit (edit);
        midiLearn.beginLearn (*miiParam);
        midiLearn.handleIncomingController (/*cc*/ 74, /*value0to1*/ 0.5f, /*channel*/ 1);   // bind CC74 -> param

        miiPhase = 1;
        startTimer (300);   // YIELD: the native bind lands on an AsyncUpdater
    }

    // Phase 1: assert the focused Edit (the ForgeUIBehaviour contribution) + the CC bind, report, quit.
    void finishSelfTestMidiInput()
    {
        auto* edit = session.getEdit();

        // THE proof of what ForgeUIBehaviour adds: the engine reports our open Edit as focused, so a real
        // controller's PhysicalMidiInputDevice controllerParser -> getCurrentlyFocusedMappings() would now
        // resolve this Edit's mappings (the old null UIBehaviour resolved nothing, so a knob drove nothing).
        const bool focusedEditOk = edit != nullptr
                                   && engine.getUIBehaviour().getCurrentlyFocusedEdit() == edit
                                   && engine.getUIBehaviour().getLastFocusedEdit()      == edit;

        miiMapped = (miiParam != nullptr) && MidiLearn::isMapped (*miiParam);

        const bool pass = focusedEditOk && miiMapped;

        String report;
        report << "mode=midiinput" << newLine
               << "param="         << (miiParamName.isEmpty() ? String ("(none)") : miiParamName) << newLine
               << "focusedEditOk=" << (focusedEditOk ? 1 : 0) << newLine
               << "mapped="        << (miiMapped ? 1 : 0) << newLine
               << "note=physical-CC routing needs real hardware (a VirtualMidiInputDevice has no controllerParser)" << newLine
               << "result="        << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="       << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write midiinput selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("MIDI-input selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-controlsurface: prove the Forge-native grid driver end-to-end with NO hardware. A virtual
    // pad-press launches the clip in slot (0,0); one LED poll emits the exact note-on the 'playing' state
    // encodes. Uses the driver's test seams (injectIncomingForTest + a CapturingMidiSink) so it never needs
    // the OS to route a real MIDI port.
    void beginControlSurfaceSelftest()
    {
        auto* edit = session.getEdit();
        if (edit == nullptr) { finishControlSurfaceSelftest(); return; }

        FORGE_LOG_INFO ("selftest-controlsurface: pad-press -> launch + LED poll (virtual, no hardware)");

        session.ensureScenes (16);

        // Born-audible clip in slot (0,0) so a launch can reach 'playing' (same recipe as --selftest-session).
        ssClip = session.createMidiClipInSlot (0, 0, "CS SelfTest");
        csClipCreated = (ssClip != nullptr);

        // Driver with a capturing LED sink; host with openNow=false (no real ports — we drive it by hand).
        auto capturing = std::make_unique<CapturingMidiSink>();
        csSink = capturing.get();
        auto driver = std::make_unique<LaunchpadDriver> (std::move (capturing));
        auto* drv = driver.get();

        controlSurface = std::make_unique<ControlSurfaceHost> (session, std::move (driver), /*openNow=*/ false);

        // INPUT: inject a grid pad-press for cell (0,0) — cellToNote(0,0) == 81, channel 1, velocity 127.
        drv->injectIncomingForTest (juce::MidiMessage::noteOn (1, LaunchpadDriver::cellToNote (0, 0), (juce::uint8) 127));
        controlSurface->drainActions();   // marshal the queued press -> session.launchSlot(0,0) on the message thread

        startTimer (1500);   // let the transport roll + the launch handle advance to 'playing'
    }

    void finishControlSurfaceSelftest()
    {
        auto* edit = session.getEdit();

        // INPUT proven: the clip in slot (0,0) reached 'playing' with the transport rolling (reuse the
        // exact SlotVisualState logic the LED poll uses, so the test can't drift from the shipped model).
        bool transportPlaying = false;
        if (auto* t = session.getTransport()) transportPlaying = t->isPlaying();
        if (auto* slot = session.getClipSlot (0, 0))
            csInputLaunched = transportPlaying
                              && computeSlotState (slot, transportPlaying, false, false) == SlotVisualState::playing;

        // OUTPUT proven: one LED poll must make the capturing sink receive the 'playing' note-on for (0,0),
        // with the EXPECTED bytes derived from the SAME shared model (no magic numbers).
        juce::Colour trackColour;
        if (edit != nullptr)
            if (auto* tr = te::getAudioTracks (*edit)[0])
                trackColour = tr->getColour();

        const PadFeedback fb = toPadFeedback (0, 0, SlotVisualState::playing, trackColour);
        csExpectedNote = LaunchpadDriver::cellToNote (0, 0);
        csExpectedVel  = LaunchpadDriver::colourIdxToPalette (fb.colourIdx);
        csExpectedChan = LaunchpadDriver::stateToChannel (fb.state);

        if (csSink != nullptr && controlSurface != nullptr)
        {
            csSink->messages.clear();
            controlSurface->pollOnce();

            for (auto& m : csSink->messages)
                if (m.isNoteOn() && m.getNoteNumber() == csExpectedNote
                    && m.getChannel() == csExpectedChan
                    && m.getVelocity() == (juce::uint8) csExpectedVel)
                { csLedCaptured = true; break; }
        }

        // Teardown the host (stops its timer + closes the driver) BEFORE stopping the transport.
        controlSurface.reset();
        csSink = nullptr;
        if (auto* t = session.getTransport()) t->stop (false, false);

        const bool pass = csClipCreated && csInputLaunched && csLedCaptured
                          && csExpectedNote == 81 && csExpectedVel > 0 && csExpectedChan == 3;

        String report;
        report << "mode=controlsurface" << newLine
               << "clipCreated="   << (csClipCreated ? 1 : 0) << newLine
               << "inputLaunched=" << (csInputLaunched ? 1 : 0) << newLine
               << "ledCaptured="   << (csLedCaptured ? 1 : 0) << newLine
               << "expectedNote="  << csExpectedNote << newLine
               << "expectedVel="   << csExpectedVel << newLine
               << "expectedChan="  << csExpectedChan << newLine
               << "result="        << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="       << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write controlsurface selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Control-surface selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-livesync (W03 P5): prove the cross-surface live-refresh loop headlessly. Write
    // engine-side values exactly as another surface would (EngineHelpers volume, AudioTrack::setMute,
    // AudioClipBase::setGainDB), force one sync tick through the views' test seams (refreshControls /
    // refreshNow — the deterministic mirrors of their poll timers), then assert the widgets moved.
    // Every op here is synchronous on the message thread, so a single callAsync yield suffices.
    void runLiveSyncSelftest()
    {
        auto* ed = session.getEdit();
        if (ed == nullptr) { finishLiveSyncSelftest (false, 0.0, false, 0.0); return; }

        FORGE_LOG_INFO ("selftest-livesync: import a clip, write engine values, force a sync tick");

        // One imported clip gives the mixer a populated track AND the inspector an AudioClipBase.
        sineFile = createSineWaveFile (44100.0, 2.0, 440.0, 0.2f);
        clip = session.importAudioFile (sineFile, te::TimePosition());
        if (clip == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-livesync: failed to import test tone");
            finishLiveSyncSelftest (false, 0.0, false, 0.0);
            return;
        }

        auto tracks = te::getAudioTracks (*ed);
        auto* track = tracks.isEmpty() ? nullptr : tracks[0];   // importAudioFile lands on track 0
        if (track == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-livesync: no audio track after import");
            finishLiveSyncSelftest (false, 0.0, false, 0.0);
            return;
        }

        // Mixer leg. The first refreshControls settles structure (the import may have changed the
        // track set since setEdit, which would make the guard rebuild-and-return); the second,
        // after the engine writes, is a pure value-sync tick — the path under test.
        mixerView.refreshControls();
        EngineHelpers::setTrackVolumeDb (*track, -12.0f);
        track->setMute (true);
        mixerView.refreshControls();

        const double faderDb  = mixerView.getStripFaderDb (0);
        const bool   muteSeen = mixerView.getStripMuted (0);

        // Detail leg: bind the clip (the slider reads the pre-write gain), write engine-side, then
        // one forced poll tick must move the slider.
        detailView.setClip (clip.get());

        bool   gainSeen = false;
        double gainDb   = 0.0;

        if (auto* acb = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            acb->setGainDB (-6.0f);
            detailView.refreshNow();
            gainDb   = detailView.getGainSliderDb();
            gainSeen = true;
        }
        else
        {
            FORGE_LOG_ERROR ("selftest-livesync: imported clip is not an AudioClipBase");
        }

        finishLiveSyncSelftest (muteSeen, faderDb, gainSeen, gainDb);
    }

    void finishLiveSyncSelftest (bool muteSeen, double faderDb, bool gainSeen, double gainDb)
    {
        // Tolerances: the mixer fader snaps to 0.1 dB and the engine's dB-to-fader-position curve
        // is not bit-exact through a round-trip, so the mixer leg allows 0.15; the clip-gain slider
        // is a direct dB value, so one snap step (0.1) suffices.
        const bool faderPass = std::abs (faderDb - (-12.0)) <= 0.15;
        const bool gainPass  = gainSeen && std::abs (gainDb - (-6.0)) <= 0.1;
        const bool pass      = faderPass && muteSeen && gainPass;

        String report;
        report << "mode=livesync" << newLine
               << "numStrips="    << mixerView.getNumStrips() << newLine
               << "faderDb="      << String (faderDb, 3) << newLine
               << "faderPass="    << (faderPass ? 1 : 0) << newLine
               << "muteSeen="     << (muteSeen ? 1 : 0) << newLine
               << "gainSliderDb=" << String (gainDb, 3) << newLine
               << "gainPass="     << (gainPass ? 1 : 0) << newLine
               << "result="       << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="      << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write livesync selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Live-sync selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-tray (W04a P2): prove the channel tray's bind -> engine-write -> forced-sync-tick
    // loop headlessly, plus the null re-bind to the empty state. Mirrors --selftest-livesync: import
    // a tone so track 0 is populated, bind the tray, write volume/mute engine-side, force one
    // refreshNow() tick (the deterministic mirror of the 10 Hz poll), and assert the widgets moved.
    // Every op here is synchronous on the message thread, so a single callAsync yield suffices.
    void runTraySelftest()
    {
        auto* ed = session.getEdit();
        if (ed == nullptr) { finishTraySelftest (false, 0.0, false, false); return; }

        FORGE_LOG_INFO ("selftest-tray: import a clip, bind track 0, engine-write vol/mute, force a sync tick");

        sineFile = createSineWaveFile (44100.0, 2.0, 440.0, 0.2f);
        clip = session.importAudioFile (sineFile, te::TimePosition());
        if (clip == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-tray: failed to import test tone");
            finishTraySelftest (false, 0.0, false, false);
            return;
        }

        auto tracks = te::getAudioTracks (*ed);
        auto* track = tracks.isEmpty() ? nullptr : tracks[0];   // importAudioFile lands on track 0
        if (track == nullptr)
        {
            FORGE_LOG_ERROR ("selftest-tray: no audio track after import");
            finishTraySelftest (false, 0.0, false, false);
            return;
        }

        // Bind, then write engine-side and force one sync tick — the path under test.
        channelTray.setTrack (track);
        const bool boundSeen = channelTray.isShowingTrack();
        const bool meterSourceSeen = channelTray.getMeterHasSource();   // W04b: meter attached to track 0's measurer

        EngineHelpers::setTrackVolumeDb (*track, -9.0f);
        track->setMute (true);
        channelTray.refreshNow();

        const double faderDb  = channelTray.getFaderDb();
        const bool   muteSeen = channelTray.getMuteShown();

        // Null re-bind must land in the empty state (the "Select a track" hint).
        channelTray.setTrack (nullptr);
        const bool clearedSeen = ! channelTray.isShowingTrack();

        finishTraySelftest (boundSeen, faderDb, muteSeen, clearedSeen, meterSourceSeen);
    }

    void finishTraySelftest (bool boundSeen, double faderDb, bool muteSeen, bool clearedSeen,
                             bool meterSourceSeen = false)
    {
        // Same tolerance as the livesync mixer leg: the fader snaps to 0.1 dB and the engine's
        // dB-to-fader-position curve is not bit-exact through a round-trip, so allow 0.15.
        const bool faderPass = std::abs (faderDb - (-9.0)) <= 0.15;
        const bool pass      = boundSeen && faderPass && muteSeen && clearedSeen && meterSourceSeen;

        String report;
        report << "mode=tray" << newLine
               << "boundSeen="   << (boundSeen ? 1 : 0) << newLine
               << "faderDb="     << String (faderDb, 3) << newLine
               << "faderPass="   << (faderPass ? 1 : 0) << newLine
               << "muteSeen="    << (muteSeen ? 1 : 0) << newLine
               << "clearedSeen=" << (clearedSeen ? 1 : 0) << newLine
               << "meterSourceSeen=" << (meterSourceSeen ? 1 : 0) << newLine
               << "result="      << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="     << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write tray selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Tray selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-popout (W04b): prove the tear-off round-trip headlessly. Turn 1: both views are
    // docked shell children -> tearOff reparents each into a visible PopoutWindow (the content is
    // a DIRECT child of the ResizableWindow) and one forced mixer sync tick proves the engine
    // binding survives the reparent -> drive the REAL close path (closeButtonPressed -> onClosed
    // -> restore). Turn 2 (after the deferred window resets ran): both windows are gone, parentage
    // is back on the shell, and — after driving viewMode/drawer state, since the Session+Detail
    // default hides both restored views — each view reads visible again (dossier verdict).
    void beginPopoutSelftest()
    {
        FORGE_LOG_INFO ("selftest-popout: tear off mixer + piano-roll, assert reparent, close, restore");

        poDockedBefore = mixerView.getParentComponent() == this
                         && pianoRoll.getParentComponent() == this;

        tearOffMixer();
        tearOffPianoRoll();

        poMixerWindowSeen = mixerPopout != nullptr && mixerPopout->isVisible();
        poRollWindowSeen  = pianoRollPopout != nullptr && pianoRollPopout->isVisible();
        poMixerOut        = mixerPopout != nullptr && mixerView.getParentComponent() == mixerPopout.get();
        poRollOut         = pianoRollPopout != nullptr && pianoRoll.getParentComponent() == pianoRollPopout.get();

        mixerView.refreshControls();   // one forced sync tick while popped out: the binding is still live

        // The real close path, not restore directly: closeButtonPressed -> onClosed -> restore
        // (reparents home now, defers each window's reset to the NEXT message-loop turn).
        if (mixerPopout != nullptr)     mixerPopout->closeButtonPressed();
        if (pianoRollPopout != nullptr) pianoRollPopout->closeButtonPressed();

        startTimer (300);   // YIELD: let the deferred window resets run, then verify
    }

    void finishPopoutSelftest()
    {
        poMixerBack = mixerPopout == nullptr && mixerView.getParentComponent() == this;
        poRollBack  = pianoRollPopout == nullptr && pianoRoll.getParentComponent() == this;

        // QC-hardening (the blocker this gate originally missed): BEFORE any rescue relayout,
        // under the Session+Detail default BOTH restored views must already be hidden (the
        // deferred lambda's post-reset resized() owns that) and the roll must not hold focus —
        // a stale-visible restored view overlays the whole shell and eats input.
        poNoGhostOverlay = ! mixerView.isVisible() && ! pianoRoll.isVisible()
                           && ! pianoRoll.hasKeyboardFocus (true);

        // Visibility only reads true under the right shell state — the Session+Detail default
        // hides both restored views — so drive the state first, then assert.
        setViewMode (ViewMode::Mixer);
        poMixerVisibleAfter = mixerView.isVisible();

        drawerVisible = true;
        bottomMode    = BottomMode::PianoRoll;
        resized();
        poRollVisibleAfter = pianoRoll.isVisible();

        const bool pass = poDockedBefore
                          && poMixerWindowSeen && poRollWindowSeen
                          && poMixerOut && poRollOut
                          && poMixerBack && poRollBack
                          && poNoGhostOverlay
                          && poMixerVisibleAfter && poRollVisibleAfter;

        String report;
        report << "mode=popout" << newLine
               << "noGhostOverlay="     << (poNoGhostOverlay ? 1 : 0) << newLine
               << "dockedBefore="       << (poDockedBefore ? 1 : 0) << newLine
               << "mixerWindowSeen="    << (poMixerWindowSeen ? 1 : 0) << newLine
               << "rollWindowSeen="     << (poRollWindowSeen ? 1 : 0) << newLine
               << "mixerReparentedOut=" << (poMixerOut ? 1 : 0) << newLine
               << "rollReparentedOut="  << (poRollOut ? 1 : 0) << newLine
               << "mixerRestored="      << (poMixerBack ? 1 : 0) << newLine
               << "rollRestored="       << (poRollBack ? 1 : 0) << newLine
               << "mixerVisibleAfter="  << (poMixerVisibleAfter ? 1 : 0) << newLine
               << "rollVisibleAfter="   << (poRollVisibleAfter ? 1 : 0) << newLine
               << "result="             << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="            << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write popout selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Popout selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // --selftest-undo (W05): prove the undo/redo round-trip headlessly against the Edit's own
    // UndoManager. Explicit beginNewTransaction calls make the gate deterministic (the engine's
    // 350 ms auto-seal timer needs message-loop time); clearUndoHistory first gives a guaranteed
    // clean baseline whatever ran before. Legs: slot-clip create -> delete -> undo -> redo (with
    // canUndo/canRedo transition asserts at every step), then a MIDI-note add undone on the
    // resurrected clip. All synchronous message-thread work.
    void runUndoSelftest()
    {
        auto* ed = session.getEdit();
        if (ed == nullptr) { finishUndoSelftest (false, false, false, false, false, false, false, false); return; }

        FORGE_LOG_INFO ("selftest-undo: slot-clip create/delete + note-edit undo/redo round-trips");

        auto& um = ed->getUndoManager();
        um.clearUndoHistory();
        const bool baselineClean = ! um.canUndo() && ! um.canRedo();

        auto slotClip = [this]() -> te::Clip*
        {
            auto* s = session.getClipSlot (0, 0);
            return s != nullptr ? s->getClip() : nullptr;
        };

        // T1: create a born-audible MIDI clip in slot (0,0).
        um.beginNewTransaction();
        const bool created = session.createMidiClipInSlot (0, 0, "UNDO") != nullptr
                             && slotClip() != nullptr;
        const bool canUndoAfterCreate = um.canUndo();

        // T2: delete it.
        um.beginNewTransaction();
        if (auto* c = slotClip())
            c->removeFromParent();
        const bool emptyAfterDelete = slotClip() == nullptr;

        // Undo T2 -> the clip is back; redo T2 -> gone again.
        ed->undo();
        const bool filledAfterUndo = slotClip() != nullptr;
        const bool canRedoAfterUndo = um.canRedo();

        ed->redo();
        const bool emptyAfterRedo = slotClip() == nullptr;

        // Note leg: bring the clip back (undo T2 again) and undo a single note add.
        ed->undo();
        bool noteLegOk = false;

        if (auto* mc = dynamic_cast<te::MidiClip*> (slotClip()))
        {
            auto& seq = mc->getSequence();
            const int before = seq.getNumNotes();

            um.beginNewTransaction();
            seq.addNote (60, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (1.0),
                         100, 0, &um);
            const bool added = seq.getNumNotes() == before + 1;

            ed->undo();
            noteLegOk = added && seq.getNumNotes() == before && um.canRedo();
        }

        finishUndoSelftest (baselineClean, created, canUndoAfterCreate, emptyAfterDelete,
                            filledAfterUndo, canRedoAfterUndo, emptyAfterRedo, noteLegOk);
    }

    void finishUndoSelftest (bool baselineClean, bool created, bool canUndoAfterCreate,
                             bool emptyAfterDelete, bool filledAfterUndo, bool canRedoAfterUndo,
                             bool emptyAfterRedo, bool noteLegOk)
    {
        const bool pass = baselineClean && created && canUndoAfterCreate && emptyAfterDelete
                          && filledAfterUndo && canRedoAfterUndo && emptyAfterRedo && noteLegOk;

        String report;
        report << "mode=undo" << newLine
               << "baselineClean="     << (baselineClean ? 1 : 0) << newLine
               << "clipCreated="       << (created ? 1 : 0) << newLine
               << "canUndoAfterCreate=" << (canUndoAfterCreate ? 1 : 0) << newLine
               << "emptyAfterDelete="  << (emptyAfterDelete ? 1 : 0) << newLine
               << "filledAfterUndo="   << (filledAfterUndo ? 1 : 0) << newLine
               << "canRedoAfterUndo="  << (canRedoAfterUndo ? 1 : 0) << newLine
               << "emptyAfterRedo="    << (emptyAfterRedo ? 1 : 0) << newLine
               << "noteLeg="           << (noteLegOk ? 1 : 0) << newLine
               << "result="            << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="           << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write undo selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Undo selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // Wave 2 (W07) grid-interaction gates — each proves ONE new ProjectSession seam headlessly and
    // synchronously (seam-level; no transport roll), then writes forge_phase0_selftest.log and quits.
    // Templates: runTapTempoSelftest / runUndoSelftest. They run in their own SelfTest modes, where the
    // Session grid is NOT bound (sessionViewBinds() is false), so they exercise the seams directly.

    // --selftest-slotdelete: the clearSlot seam. create -> filled -> clearSlot -> empty; clearSlot on an
    // already-empty slot returns false (the no-op contract); ed->undo() restores the clip (removeFromParent
    // rides the Edit UndoManager — the same stack W05 global Undo drives).
    void runSlotDeleteSelftest()
    {
        bool created = false, filledBefore = false, cleared = false, emptyAfter = false,
             clearEmptyIsNoop = false, undoRestored = false;

        if (auto* ed = session.getEdit())
        {
            auto& um = ed->getUndoManager();
            um.clearUndoHistory();

            session.ensureScenes (16);
            created      = session.createMidiClipInSlot (0, 0, "DEL") != nullptr;
            filledBefore = session.isSlotFilled (0, 0);

            um.beginNewTransaction();                        // seal a per-gesture unit like the shell does
            cleared          = session.clearSlot (0, 0);
            emptyAfter       = ! session.isSlotFilled (0, 0);
            clearEmptyIsNoop = ! session.clearSlot (0, 0);   // already empty -> false, no-op

            ed->undo();
            undoRestored = session.isSlotFilled (0, 0);
        }

        const bool pass = created && filledBefore && cleared && emptyAfter
                          && clearEmptyIsNoop && undoRestored;

        String report;
        report << "mode=slotdelete" << newLine
               << "clipCreated="      << (created ? 1 : 0) << newLine
               << "filledBefore="     << (filledBefore ? 1 : 0) << newLine
               << "cleared="          << (cleared ? 1 : 0) << newLine
               << "emptyAfter="       << (emptyAfter ? 1 : 0) << newLine
               << "clearEmptyIsNoop=" << (clearEmptyIsNoop ? 1 : 0) << newLine
               << "undoRestored="     << (undoRestored ? 1 : 0) << newLine
               << "result="           << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="          << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write slot-delete selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Slot-delete selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-sendarrange (W5, the last hands-on wave): the Session -> Arrange "Send to" bridge. Seed a
    // MIDI clip with a known note count into slot (0,0), send it to the SAME track's linear timeline, then
    // assert the faithful, one-directional COPY: the source slot still holds its clip (a copy, not a move);
    // a NEW clip appears on the track's Arrange timeline; it is a MidiClip carrying the SAME note count
    // (proves the note sequence rode along in the state clone); it landed at the append point (0 for a
    // previously-empty lane); and a SECOND send APPENDS a second clip after the first (append-at-end — no
    // overlap, no replace).
    void runSendArrangeSelftest()
    {
        // MIDI leg (track 0).
        bool clipCreated = false, notesSeeded = false, sent = false, sourceIntact = false,
             arrangeClipAppeared = false, isMidiCopy = false, noteCountPreserved = false,
             landedAtStart = false, copyNotLooping = false, arrangeAudible = false,
             secondAppended = false, undoRemovedCopy = false, sourceIntactAfterUndo = false;
        int  seededNotes = 0, copiedNotes = 0, clipsBefore = 0, clipsAfterFirst = 0,
             clipsAfterSecond = 0, clipsAfterUndo = 0;
        // Wave leg (track 1) — exercises the AudioClipBase normalization the MIDI leg can't reach.
        bool waveImported = false, waveSent = false, waveIsAudioClip = false,
             waveNotLooping = false, waveNoAutoTempo = false, waveSourceMatches = false;
        juce::File waveFile;

        if (auto* ed = session.getEdit())
        {
            auto& um = ed->getUndoManager();
            um.clearUndoHistory();

            session.ensureScenes (16);

            // Seed a known 4-note pattern into the slot clip (content-relative beats).
            if (auto mc = session.createMidiClipInSlot (0, 0, "SEND"))
            {
                clipCreated = true;
                auto& seq = mc->getSequence();
                um.beginNewTransaction();
                for (int i = 0; i < 4; ++i)
                    seq.addNote (60 + i, te::BeatPosition::fromBeats ((double) i),
                                 te::BeatDuration::fromBeats (1.0), 100, 0, &um);
                seededNotes = seq.getNumNotes();
                notesSeeded = seededNotes == 4;
            }

            const auto arrangeClipCount = [ed] (int trackIdx) -> int
            {
                auto tracks = te::getAudioTracks (*ed);
                return trackIdx < tracks.size() ? tracks[trackIdx]->getClips().size() : -1;
            };
            const auto trackAt = [ed] (int trackIdx) -> te::AudioTrack*
            {
                auto tracks = te::getAudioTracks (*ed);
                return trackIdx < tracks.size() ? tracks[trackIdx] : nullptr;
            };

            // Simulate a track whose slot has played: playSlotClips latches TRUE and nothing in the engine
            // clears it, so WITHOUT the fix the arrange copy is silent (arranger output is gated off). The
            // send must flip it back to arrange playback.
            if (auto* t0 = trackAt (0))
                t0->playSlotClips = true;

            clipsBefore = arrangeClipCount (0);   // 0: nothing on the linear timeline yet

            um.beginNewTransaction();
            auto* newClip = session.sendSlotToArrangement (0, 0);
            sent = newClip != nullptr;

            sourceIntact    = session.isSlotFilled (0, 0);   // the slot clip was COPIED, not moved
            clipsAfterFirst = arrangeClipCount (0);
            arrangeClipAppeared = clipsAfterFirst == clipsBefore + 1;

            if (auto* t0 = trackAt (0))
                arrangeAudible = ! t0->playSlotClips.get();   // the fix flipped the track back to arrange playback

            if (auto* midiCopy = dynamic_cast<te::MidiClip*> (newClip))
            {
                isMidiCopy         = true;
                copiedNotes        = midiCopy->getSequence().getNumNotes();
                noteCountPreserved = seededNotes > 0 && copiedNotes == seededNotes;
                landedAtStart      = midiCopy->getPosition().getStart().inSeconds() < 0.001;   // empty lane -> start 0
                copyNotLooping     = ! midiCopy->isLooping();   // the slot's inherited loop range was cleared
            }

            // A second send must APPEND after the first (append-at-end; not replace, not overlap-at-0).
            um.beginNewTransaction();
            auto* secondClip = session.sendSlotToArrangement (0, 0);
            clipsAfterSecond = arrangeClipCount (0);
            secondAppended = secondClip != nullptr
                             && clipsAfterSecond == clipsAfterFirst + 1
                             && secondClip->getPosition().getStart().inSeconds() > 0.001;

            // Undo leg: one Ctrl+Z removes the second copy and leaves the SOURCE slot intact (the send is
            // add-only, undoable via the same UndoManager as W05 global undo).
            ed->undo();
            clipsAfterUndo        = arrangeClipCount (0);
            undoRemovedCopy       = clipsAfterUndo == clipsAfterFirst;   // 2 -> 1
            sourceIntactAfterUndo = session.isSlotFilled (0, 0);

            // Wave leg (track 1): import a sine WAV into slot (1,0), send it, and assert the AudioClipBase
            // normalization — a NON-looping, NON-auto-tempo WaveAudioClip whose audio source survived the
            // state copy (matches the slot clip's source). This is the fix path the MIDI leg cannot reach.
            waveFile = createSineWaveFile (44100.0, 1.0, 440.0, 0.5f);
            waveImported = session.importAudioIntoSlot (1, 0, waveFile) != nullptr;

            um.beginNewTransaction();
            auto* waveCopy = session.sendSlotToArrangement (1, 0);
            waveSent = waveCopy != nullptr;

            if (auto* acb = dynamic_cast<te::AudioClipBase*> (waveCopy))
            {
                waveIsAudioClip = true;
                waveNotLooping  = ! acb->isLooping();
                waveNoAutoTempo = ! acb->getAutoTempo();

                te::Clip* slotClip = nullptr;
                if (auto* slot = session.getClipSlot (1, 0))
                    slotClip = slot->getClip();

                if (auto* slotWave = dynamic_cast<te::WaveAudioClip*> (slotClip))
                    waveSourceMatches = acb->getSourceFileReference().getFile()
                                            == slotWave->getSourceFileReference().getFile()
                                        && slotWave->getSourceFileReference().getFile() != juce::File();
            }
        }

        const bool pass = clipCreated && notesSeeded && sent && sourceIntact && arrangeClipAppeared
                          && isMidiCopy && noteCountPreserved && landedAtStart && copyNotLooping
                          && arrangeAudible && secondAppended && undoRemovedCopy && sourceIntactAfterUndo
                          && waveImported && waveSent && waveIsAudioClip && waveNotLooping
                          && waveNoAutoTempo && waveSourceMatches;

        String report;
        report << "mode=sendarrange" << newLine
               << "clipCreated="           << (clipCreated ? 1 : 0) << newLine
               << "notesSeeded="           << (notesSeeded ? 1 : 0) << newLine
               << "seededNotes="           << seededNotes << newLine
               << "sent="                  << (sent ? 1 : 0) << newLine
               << "sourceIntact="          << (sourceIntact ? 1 : 0) << newLine
               << "clipsBefore="           << clipsBefore << newLine
               << "clipsAfterFirst="       << clipsAfterFirst << newLine
               << "arrangeClipAppeared="   << (arrangeClipAppeared ? 1 : 0) << newLine
               << "isMidiCopy="            << (isMidiCopy ? 1 : 0) << newLine
               << "copiedNotes="           << copiedNotes << newLine
               << "noteCountPreserved="    << (noteCountPreserved ? 1 : 0) << newLine
               << "landedAtStart="         << (landedAtStart ? 1 : 0) << newLine
               << "copyNotLooping="        << (copyNotLooping ? 1 : 0) << newLine
               << "arrangeAudible="        << (arrangeAudible ? 1 : 0) << newLine
               << "secondAppended="        << (secondAppended ? 1 : 0) << newLine
               << "clipsAfterSecond="      << clipsAfterSecond << newLine
               << "undoRemovedCopy="       << (undoRemovedCopy ? 1 : 0) << newLine
               << "clipsAfterUndo="        << clipsAfterUndo << newLine
               << "sourceIntactAfterUndo=" << (sourceIntactAfterUndo ? 1 : 0) << newLine
               << "waveImported="          << (waveImported ? 1 : 0) << newLine
               << "waveSent="              << (waveSent ? 1 : 0) << newLine
               << "waveIsAudioClip="       << (waveIsAudioClip ? 1 : 0) << newLine
               << "waveNotLooping="        << (waveNotLooping ? 1 : 0) << newLine
               << "waveNoAutoTempo="       << (waveNoAutoTempo ? 1 : 0) << newLine
               << "waveSourceMatches="     << (waveSourceMatches ? 1 : 0) << newLine
               << "result="                << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="               << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write send-to-arrange selftest report to: " + reportFile.getFullPathName());

        if (waveFile.existsAsFile())
            waveFile.deleteFile();

        FORGE_LOG_INFO ("Send-to-arrange selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-followaction (W1): the per-clip follow-action + loop-toggle seams. Synchronous. Fills slots
    // (0,0) and (0,1) so trackNext has a valid sibling (FollowActions createFollowAction resolves non-null
    // only when a filled clip exists AFTER this one). Proves set/read-back, EXACTLY-one-action (the engine
    // auto-plant footgun defeated), duration persist WITH the action surviving (footgun re-check), a non-null
    // resolved functor for trackNext + an empty functor for none, a ValueTree round-trip (serialization), an
    // undo revert, and the loop toggle (incl. never-empty loop range). It does NOT prove the clip AUDIBLY
    // chains (needs a pumped playback loop the harness doesn't run — parked, W09/W10 convention).
    void runFollowActionSelftest()
    {
        bool bothClipsCreated = false, setReadsBack = false, actionListSingle = false,
             durationPersists = false, functorNonNull = false, noneGivesEmpty = false,
             valueTreeRoundTrip = false, undoReverts = false,
             loopToggleOn = false, loopToggleOff = false, neverEmptyLoopRange = false;

        if (auto* ed = session.getEdit())
        {
            auto& um = ed->getUndoManager();
            session.ensureScenes (8);

            bothClipsCreated = (session.createMidiClipInSlot (0, 0, "FA0") != nullptr)
                            && (session.createMidiClipInSlot (0, 1, "FA1") != nullptr);

            const auto clipAt = [this] (int t, int s) -> te::Clip*
            {
                if (auto* slot = session.getClipSlot (t, s))
                    return slot->getClip();
                return nullptr;
            };

            // Leg 2: set + read back.
            session.setFollowAction (0, 0, te::FollowAction::trackNext);
            setReadsBack = session.getFollowAction (0, 0) == te::FollowAction::trackNext;

            // Leg 3: EXACTLY one action, front == trackNext (footgun: no stray currentGroupRoundRobin auto-plant).
            if (auto* c = clipAt (0, 0))
                if (auto* fa = c->getFollowActions())
                {
                    auto actions = fa->getActions();
                    actionListSingle = actions.size() == 1 && ! actions.empty()
                                    && actions.front()->action == te::FollowAction::trackNext;
                }

            // Leg 4: duration persists AND the action type survived the duration write (footgun re-check).
            session.setFollowActionDuration (0, 0, te::Clip::FollowActionDurationType::loops, 1.0);
            if (auto* c = clipAt (0, 0))
                durationPersists = c->followActionDurationType == te::Clip::FollowActionDurationType::loops
                                && c->followActionNumLoops == 1.0
                                && session.getFollowAction (0, 0) == te::FollowAction::trackNext;

            // Leg 5: KEY PROOF — createFollowAction resolves a non-null functor for trackNext (sibling at (0,1)).
            if (auto* c = clipAt (0, 0))
                functorNonNull = (bool) te::createFollowAction (*c);

            // Leg 6: none -> empty functor.
            session.setFollowAction (0, 0, te::FollowAction::none);
            if (auto* c = clipAt (0, 0))
                noneGivesEmpty = ! (bool) te::createFollowAction (*c);

            // Leg 7: ValueTree round-trip (serialization proof, no disk I/O).
            session.setFollowAction (0, 0, te::FollowAction::trackFirst);
            session.setFollowActionDuration (0, 0, te::Clip::FollowActionDurationType::loops, 3.0);
            if (auto* c = clipAt (0, 0))
            {
                const auto copy = c->state.createCopy();
                const auto faChild = copy.getChildWithName (te::IDs::FOLLOWACTIONS);
                const auto typeStr = faChild.getChild (0).getProperty (te::IDs::type).toString();
                const auto parsed  = te::followActionFromString (typeStr);
                const double loops = (double) copy.getProperty (te::IDs::followActionNumLoops, -1.0);
                valueTreeRoundTrip = parsed.has_value() && *parsed == te::FollowAction::trackFirst && loops == 3.0;
            }

            // Leg 8: undo reverts a follow-action set on (0,1) (rides the Edit UndoManager).
            um.clearUndoHistory();
            um.beginNewTransaction();
            session.setFollowAction (0, 1, te::FollowAction::trackLast);
            const bool wasSet = session.getFollowAction (0, 1) == te::FollowAction::trackLast;
            ed->undo();
            undoReverts = wasSet && session.getFollowAction (0, 1) != te::FollowAction::trackLast;

            // Legs 9-11: loop toggle (the "after N loops" precondition), incl. the never-empty-loop-range gotcha.
            session.setSlotClipLooping (0, 0, true);
            if (auto* c = clipAt (0, 0))
            {
                loopToggleOn = session.isSlotClipLooping (0, 0) && c->isLooping();
                neverEmptyLoopRange = c->getLoopLengthBeats() > te::BeatDuration();
            }
            session.setSlotClipLooping (0, 0, false);
            if (auto* c = clipAt (0, 0))
                loopToggleOff = ! c->isLooping();
        }

        const bool pass = bothClipsCreated && setReadsBack && actionListSingle && durationPersists
                       && functorNonNull && noneGivesEmpty && valueTreeRoundTrip && undoReverts
                       && loopToggleOn && loopToggleOff && neverEmptyLoopRange;

        String report;
        report << "mode=followaction" << newLine
               << "bothClipsCreated="   << (bothClipsCreated ? 1 : 0) << newLine
               << "setReadsBack="       << (setReadsBack ? 1 : 0) << newLine
               << "actionListSingle="   << (actionListSingle ? 1 : 0) << newLine
               << "durationPersists="   << (durationPersists ? 1 : 0) << newLine
               << "functorNonNull="     << (functorNonNull ? 1 : 0) << newLine
               << "noneGivesEmpty="     << (noneGivesEmpty ? 1 : 0) << newLine
               << "valueTreeRoundTrip=" << (valueTreeRoundTrip ? 1 : 0) << newLine
               << "undoReverts="        << (undoReverts ? 1 : 0) << newLine
               << "loopToggleOn="       << (loopToggleOn ? 1 : 0) << newLine
               << "loopToggleOff="      << (loopToggleOff ? 1 : 0) << newLine
               << "neverEmptyLoopRange="<< (neverEmptyLoopRange ? 1 : 0) << newLine
               << "result="             << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="            << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write follow-action selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Follow-action selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-launchmode (W1): the per-clip launch-mode seam (Trigger / Gate / Toggle). Synchronous,
    // state-level: proves the default is Trigger, set/read-back for Toggle + Gate, ValueTree persistence, that
    // ABSENCE of the property reads as Trigger (so every pre-W1 clip + gate is unchanged), and that the
    // isSlotPlaying query the Toggle routing reads is sound. The AUDIBLE Gate-hold / Toggle-off transitions are
    // driven by the view's mouse routing (not a seam) and are parked/manual — proven-adjacent by the launch
    // path in --selftest-session; this gate never fakes a PASS for them (gate-honesty rule).
    void runLaunchModeSelftest()
    {
        bool bothClips = false, defaultIsTrigger = false, setToggleReadsBack = false,
             setGateReadsBack = false, persists = false, absenceIsTrigger = false, toggleQuerySound = false;

        if (session.getEdit() != nullptr)
        {
            session.ensureScenes (8);
            bothClips = (session.createMidiClipInSlot (0, 0, "LM0") != nullptr)
                     && (session.createMidiClipInSlot (0, 1, "LM1") != nullptr);

            defaultIsTrigger = session.getLaunchMode (0, 0) == LaunchMode::Trigger;   // fresh clip, no property

            session.setLaunchMode (0, 0, LaunchMode::Toggle);
            setToggleReadsBack = session.getLaunchMode (0, 0) == LaunchMode::Toggle;

            session.setLaunchMode (0, 0, LaunchMode::Gate);
            setGateReadsBack = session.getLaunchMode (0, 0) == LaunchMode::Gate;

            if (auto* slot = session.getClipSlot (0, 0))
                if (auto* c = slot->getClip())
                {
                    const auto copy = c->state.createCopy();
                    persists = (int) copy.getProperty (juce::Identifier ("forgeLaunchMode"), -1) == (int) LaunchMode::Gate;
                }

            absenceIsTrigger = session.getLaunchMode (0, 1) == LaunchMode::Trigger;   // (0,1) never set
            toggleQuerySound = ! session.isSlotActive (0, 0);   // nothing launched -> not active -> a Toggle click would launch
        }

        const bool pass = bothClips && defaultIsTrigger && setToggleReadsBack && setGateReadsBack
                       && persists && absenceIsTrigger && toggleQuerySound;

        String report;
        report << "mode=launchmode" << newLine
               << "bothClips="          << (bothClips ? 1 : 0) << newLine
               << "defaultIsTrigger="   << (defaultIsTrigger ? 1 : 0) << newLine
               << "setToggleReadsBack=" << (setToggleReadsBack ? 1 : 0) << newLine
               << "setGateReadsBack="   << (setGateReadsBack ? 1 : 0) << newLine
               << "persists="           << (persists ? 1 : 0) << newLine
               << "absenceIsTrigger="   << (absenceIsTrigger ? 1 : 0) << newLine
               << "toggleQuerySound="   << (toggleQuerySound ? 1 : 0) << newLine
               << "result="             << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="            << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write launch-mode selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Launch-mode selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-addtrack: the appendAudioTrack seam. Track count increments by exactly 1, and a slot on
    // the newly-appended (last) track resolves + accepts a born-audible MIDI clip (proves the new column
    // is a real, addressable track — not a phantom).
    void runAddTrackSelftest()
    {
        int  before = 0, after = 0;
        bool appended = false, newSlotResolves = false, clipOnNewTrack = false;

        if (session.getEdit() != nullptr)
        {
            before   = session.getNumAudioTracks();
            appended = session.appendAudioTrack ("SelfTest Track") != nullptr;
            after    = session.getNumAudioTracks();

            const int newIdx = after - 1;
            session.ensureScenes (16);
            clipOnNewTrack  = session.createMidiClipInSlot (newIdx, 0, "OnNew") != nullptr;
            newSlotResolves = session.getClipSlot (newIdx, 0) != nullptr;
        }

        const bool pass = appended && (after == before + 1) && newSlotResolves && clipOnNewTrack;

        String report;
        report << "mode=addtrack" << newLine
               << "tracksBefore="    << before << newLine
               << "tracksAfter="     << after << newLine
               << "appended="        << (appended ? 1 : 0) << newLine
               << "newSlotResolves=" << (newSlotResolves ? 1 : 0) << newLine
               << "clipOnNewTrack="  << (clipOnNewTrack ? 1 : 0) << newLine
               << "result="          << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="         << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write add-track selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Add-track selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-scene: the dynamic scene count (+ Scene). ensureScenes grows the grid past the former
    // constexpr=16 ceiling, and a clip created in scene 18 resolves — proving the engine + seam carry
    // N != 16 (the UI's dynamic-N rendering is proved separately by the screenshot matrix).
    void runSceneSelftest()
    {
        int  base = 0, grown = 0;
        bool grewPast16 = false, slot18Resolves = false, clipInScene18 = false;

        if (session.getEdit() != nullptr)
        {
            base = session.getNumScenes();
            session.ensureScenes (20);                 // grow past the former 16 ceiling
            grown      = session.getNumScenes();
            grewPast16 = grown >= 20;

            clipInScene18  = session.createMidiClipInSlot (0, 18, "S18") != nullptr;
            slot18Resolves = session.getClipSlot (0, 18) != nullptr;
        }

        const bool pass = grewPast16 && slot18Resolves && clipInScene18;

        String report;
        report << "mode=scene" << newLine
               << "scenesBase="     << base << newLine
               << "scenesGrown="    << grown << newLine
               << "grewPast16="     << (grewPast16 ? 1 : 0) << newLine
               << "slot18Resolves=" << (slot18Resolves ? 1 : 0) << newLine
               << "clipInScene18="  << (clipInScene18 ? 1 : 0) << newLine
               << "result="         << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="        << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write scene selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Scene selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-dragdrop: the file-import seams both drop paths route through. Session leg: a file
    // dropped on a pad -> importAudioIntoSlot fills that slot. Arrange leg: a file dropped on lane N ->
    // importAudioFile(file, time, N) lands the clip on track N (a fresh empty target track ends with
    // exactly one clip, proving the trackIndex param routes correctly and not to track 0). The pointer
    // math (xToTime) is a pure function the arrange agent left testable; this gate proves the payload path.
    void runDragDropSelftest()
    {
        bool sessionImported = false, sessionSlotFilled = false, replaceUndoRestores = false,
             arrangeImported = false, arrangeLandedOnTarget = false;

        if (auto* ed = session.getEdit())
        {
            sineFile = createSineWaveFile (44100.0, 1.0, 440.0, 0.2f);

            session.ensureScenes (16);

            auto& um = ed->getUndoManager();
            um.clearUndoHistory();

            // Session-drop path: an OS file dropped on a pad routes to importAudioIntoSlot.
            um.beginNewTransaction();
            sessionImported   = session.importAudioIntoSlot (0, 1, sineFile) != nullptr;   // clip A
            sessionSlotFilled = session.isSlotFilled (0, 1);

            // Replace-on-drop is UNDOABLE (QC4-F2): a SECOND drop onto the filled slot replaces the clip
            // (importAudioIntoSlot -> DeleteExistingClips::yes); an undo restores the prior clip. Seal the
            // replace in its own transaction so undo reverts ONLY it (clip A returns -> the slot stays filled).
            um.beginNewTransaction();
            session.importAudioIntoSlot (0, 1, sineFile);                                  // clip B replaces A
            const bool refilledAfterReplace = session.isSlotFilled (0, 1);
            um.beginNewTransaction();
            ed->undo();
            replaceUndoRestores = refilledAfterReplace && session.isSlotFilled (0, 1);

            // Arrange-drop path: a file dropped on lane N routes to importAudioFile(file, time, N).
            // Append a fresh EMPTY track as an unambiguous drop target (it starts with 0 clips).
            session.appendAudioTrack ("Drop Target");
            const int target = session.getNumAudioTracks() - 1;
            arrangeImported = session.importAudioFile (sineFile, te::TimePosition(), target) != nullptr;

            auto tracks = te::getAudioTracks (*ed);
            if (target >= 1 && target < tracks.size())
                arrangeLandedOnTarget = tracks[target]->getClips().size() == 1;   // landed on N, not track 0
        }

        const bool pass = sessionImported && sessionSlotFilled && replaceUndoRestores
                          && arrangeImported && arrangeLandedOnTarget;

        String report;
        report << "mode=dragdrop" << newLine
               << "sessionImported="       << (sessionImported ? 1 : 0) << newLine
               << "sessionSlotFilled="     << (sessionSlotFilled ? 1 : 0) << newLine
               << "replaceUndoRestores="   << (replaceUndoRestores ? 1 : 0) << newLine
               << "arrangeImported="       << (arrangeImported ? 1 : 0) << newLine
               << "arrangeLandedOnTarget=" << (arrangeLandedOnTarget ? 1 : 0) << newLine
               << "result="                << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="               << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write drag-drop selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Drag-drop selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-sessionmixer (W08): the per-track Session mixer strip's engine->widget sync. Mirrors
    // --selftest-livesync / --selftest-tray: write vol/pan/mute/solo engine-side on track 0, bind a strip
    // to it, force ONE refreshControls() tick, then read the strip's controls back through its accessors.
    void runSessionMixerSelftest()
    {
        bool bound = false, faderOk = false, panOk = false, muteOk = false, soloOk = false;

        if (auto* ed = session.getEdit())
        {
            auto tracks = te::getAudioTracks (*ed);
            if (! tracks.isEmpty())
            {
                auto* t0 = tracks[0];
                EngineHelpers::setTrackVolumeDb (*t0, -9.0f);
                EngineHelpers::setTrackPan (*t0, -0.5f);
                t0->setMute (true);
                t0->setSolo (true);

                SessionMixerStrip strip;
                strip.setTrack (ed, 0);
                strip.refreshControls();

                bound   = strip.isBound();
                faderOk = std::abs (strip.getFaderDb()  - (-9.0)) < 0.15;
                panOk   = std::abs (strip.getPanValue() - (-0.5)) < 0.02;
                muteOk  = strip.isMuteOn();
                soloOk  = strip.isSoloOn();

                t0->setMute (false);   // restore (throwaway edit, kept tidy)
                t0->setSolo (false);
            }
        }

        const bool pass = bound && faderOk && panOk && muteOk && soloOk;

        String report;
        report << "mode=sessionmixer" << newLine
               << "bound="   << (bound ? 1 : 0) << newLine
               << "faderOk="  << (faderOk ? 1 : 0) << newLine
               << "panOk="    << (panOk ? 1 : 0) << newLine
               << "muteOk="   << (muteOk ? 1 : 0) << newLine
               << "soloOk="   << (soloOk ? 1 : 0) << newLine
               << "result="   << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="  << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write session-mixer selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Session-mixer selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " - report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // --selftest-demo (W09): the audible-demo gate. Proves the instrument PRESETS insert the right plugins
    // (a 4OSC for kick, the engine Sampler for piano), that the self-rendered CC0 piano one-shot exists on
    // disk, and that the demo note-seeding actually writes notes (so a launched clip is not silent).
    // Structural + synchronous — it does NOT render audio (the Sampler loads its sample on an AsyncUpdater;
    // a render leg would have to pump the loop first). Playback engagement is covered by --selftest-session.
    void runDemoSelftest()
    {
        bool kickIsSynth = false, pianoIsSampler = false, pianoFileExists = false, clipHasNotes = false;
        int  noteCount = 0;

        if (auto* ed = session.getEdit())
        {
            ed->ensureNumberOfAudioTracks (3);
            auto tracks = te::getAudioTracks (*ed);

            if (tracks.size() >= 3)
            {
                auto kp = PluginHost::applyInstrumentPreset (*tracks[0], PluginHost::InstrumentPreset::Kick);
                kickIsSynth = kp != nullptr && kp->isSynth()
                              && dynamic_cast<te::SamplerPlugin*> (kp.get()) == nullptr;   // a 4OSC, not the sampler

                auto pp = PluginHost::applyInstrumentPreset (*tracks[2], PluginHost::InstrumentPreset::Piano);
                pianoIsSampler = dynamic_cast<te::SamplerPlugin*> (pp.get()) != nullptr;

                pianoFileExists = InstrumentSamples::ensurePianoOneShot().existsAsFile();

                session.ensureScenes (16);
                if (auto mc = session.createMidiClipInSlot (0, 0, "DemoKick"))
                {
                    seedDemoNotes (*mc, 0);
                    noteCount    = mc->getSequence().getNumNotes();
                    clipHasNotes = noteCount > 0;
                }
            }
        }

        const bool pass = kickIsSynth && pianoIsSampler && pianoFileExists && clipHasNotes;

        String report;
        report << "mode=demo" << newLine
               << "kickIsSynth="     << (kickIsSynth ? 1 : 0) << newLine
               << "pianoIsSampler="  << (pianoIsSampler ? 1 : 0) << newLine
               << "pianoFileExists=" << (pianoFileExists ? 1 : 0) << newLine
               << "noteCount="       << noteCount << newLine
               << "clipHasNotes="    << (clipHasNotes ? 1 : 0) << newLine
               << "result="          << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="         << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write demo selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Demo selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " - report: " + reportFile.getFullPathName());

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    // Headless screenshot mode (--screenshot): build a populated demo session and render each view to a
    // PNG via createComponentSnapshot, so the UI can be inspected without a live display. Writes
    // %TEMP%\forge_shot_{session,arrange,mix}.png then quits.
    // W09: seed a per-track-instrument note pattern into a demo clip so a launched clip is AUDIBLE, not
    // silent. Track 0 = 4-on-the-floor kick (MIDI 36), track 1 = C-minor root/fifth walking bass, track 2 =
    // Cm-Ab-Bb-Cm chord stabs; other tracks are left empty. Beats are content-relative (beat 0 = clip start),
    // routed through the Edit's real UndoManager like every other note-write path.
    void seedDemoNotes (te::MidiClip& mc, int trackIndex)
    {
        auto& seq  = mc.getSequence();
        auto* undo = &mc.edit.getUndoManager();
        const auto add = [&] (int pitch, double beat, double len, int vel)
        {
            seq.addNote (pitch, te::BeatPosition::fromBeats (beat),
                         te::BeatDuration::fromBeats (len), vel, 0, undo);
        };

        if (trackIndex == 0)            // Drums: four-on-the-floor kick, 16 beats
        {
            for (int b = 0; b < 16; ++b)
                add (36, (double) b, 0.25, 110);
        }
        else if (trackIndex == 1)       // Bass: C-minor root/fifth walking figure, 8-beat phrase x2
        {
            struct N { int pitch; double beat, len; int vel; };
            static const N phrase[] = {
                { 36, 0.0, 1.5, 105 }, { 43, 1.5, 0.5, 95 }, { 36, 2.0, 1.5, 105 }, { 39, 3.5, 0.5, 95 },
                { 34, 4.0, 1.5, 100 }, { 41, 5.5, 0.5,  90 }, { 34, 6.0, 1.5, 100 }, { 39, 7.5, 0.5, 90 }
            };
            for (int rep = 0; rep < 2; ++rep)
                for (auto& n : phrase)
                    add (n.pitch, n.beat + rep * 8.0, n.len, n.vel);
        }
        else if (trackIndex == 2)       // Keys: Cm - Ab - Bb - Cm chord stabs (3-note voicings), one per bar
        {
            struct Chord { double beat; int a, b, c; };
            static const Chord chords[] = {
                { 0.0,  60, 63, 67 },   // Cm  (C  Eb G)
                { 4.0,  56, 60, 63 },   // Ab  (Ab C  Eb)
                { 8.0,  58, 62, 65 },   // Bb  (Bb D  F)
                { 12.0, 60, 63, 67 }    // Cm
            };
            for (auto& ch : chords)
                for (int p : { ch.a, ch.b, ch.c })
                    add (p, ch.beat, 4.0, 85);
        }
    }

    // W09: builds the demo CONTENT (named/coloured tracks, per-track instrument PRESETS, note-seeded MIDI
    // clips) into the current edit. Shared by --screenshot (which then adds an automation curve, launches,
    // and snapshots) and the first-launch welcome demo (see initialise). Does NOT launch or persist.
    void populateDemoContent()
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

        // W09: give the three hero tracks distinct instruments BEFORE creating clips, so
        // createMidiClipInSlot's ensureDefaultInstrument no-ops (a synth already exists) instead of stacking
        // a stock 4OSC. Drums -> punchy 4OSC kick, Bass -> 4OSC bass, Keys -> Sampler + the self-rendered CC0
        // piano one-shot. Lead/Vox/FX keep the default 4OSC.
        if (tracks.size() > 0) PluginHost::applyInstrumentPreset (*tracks[0], PluginHost::InstrumentPreset::Kick);
        if (tracks.size() > 1) PluginHost::applyInstrumentPreset (*tracks[1], PluginHost::InstrumentPreset::Bass);
        if (tracks.size() > 2) PluginHost::applyInstrumentPreset (*tracks[2], PluginHost::InstrumentPreset::Piano);

        // Clips across slots so the grid reads as a real session. SCENE 0 carries the coherent
        // kick+bass+piano groove (Beat A / Sub / Chords) so launchScene(0) is an audible demo. The three
        // hero tracks (0,1,2) get seeded note patterns; the rest stay empty for now.
        struct Cell { int track, scene; const char* name; };
        const Cell cells[] = {
            { 0, 0, "Beat A" }, { 0, 1, "Beat B" }, { 0, 3, "Fill" },
            { 1, 0, "Sub" },    { 1, 2, "Walk" },
            { 2, 0, "Chords" }, { 2, 1, "Pad" },    { 2, 3, "Stab" },   { 2, 4, "Melody" },
            { 3, 2, "Hook" },   { 3, 5, "Solo" },
            { 4, 0, "Verse" },  { 4, 3, "Chorus" },
            { 5, 4, "Riser" }
        };

        for (auto& c : cells)
        {
            auto mc = session.createMidiClipInSlot (c.track, c.scene, c.name);
            if (mc == nullptr)
            {
                FORGE_LOG_WARN ("Demo: failed to create MIDI clip at ("
                                + juce::String (c.track) + "," + juce::String (c.scene) + ")");
                continue;
            }
            seedDemoNotes (*mc, c.track);   // seeds tracks 0/1/2; a no-op for the rest
        }
    }

    void populateDemoSession()
    {
        populateDemoContent();

        auto* edit = session.getEdit();
        if (edit == nullptr)
            return;

        auto tracks = te::getAudioTracks (*edit);

        // W04a: a visible volume curve on track 0 so the arrange_automation state shows a real
        // shape (rise-dip-rise) rather than an empty lane.
        if (! tracks.isEmpty())
            if (auto volParam = AutomationHelpers::getTrackVolumeParam (*tracks[0]))
            {
                AutomationHelpers::clearAutomation (*volParam);
                AutomationHelpers::addPoint (*volParam, 0.0, 0.85f);
                AutomationHelpers::addPoint (*volParam, 4.0, 0.35f);
                AutomationHelpers::addPoint (*volParam, 8.0, 0.70f);
            }

        // W09: launch scene 0 — the coherent kick+bass+piano groove — so the snapshot shows playing pads.
        session.launchScene (0);
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
        else
        {
            FORGE_LOG_ERROR ("Failed to create/write PNG snapshot: " + file.getFullPathName());
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

        // W04a state-matrix: the same Arrange frame with track 0's automation lane EXPANDED, showing
        // the demo volume curve — the visual proof the collapsed default screenshot cannot give.
        arrangeView.setAutomationLaneExpanded (0, true);
        captureView ("arrange_automation");
        arrangeView.setAutomationLaneExpanded (0, false);

        // W04a: the channel tray bound to track 0 on the sidebar's Channel tab, in Arrange — the
        // GarageBand-inspector state.
        {
            auto tracks = te::getAudioTracks (*session.getEdit());
            if (! tracks.isEmpty())
                channelTray.setTrack (tracks[0]);
        }
        browserVisible = true;
        controlBar.setBrowserOpen (true);   // hands-on 1.2: this direct-set harness path bypasses the
        sidebarMode    = SidebarMode::Channel;   // animated toggle, so sync the folder-icon tint by hand (QC)
        captureView ("arrange_tray");
        browserVisible = false;
        controlBar.setBrowserOpen (false);
        sidebarMode    = SidebarMode::Browser;
        channelTray.setTrack (nullptr);

        // W04a: the LCD's count-in face via its demo seam (digit 3 of 4, pulse near the click) —
        // a REAL count-in needs a capture device, so this state is injected, per the dossier.
        setViewMode (ViewMode::Session);
        controlBar.getLcdDisplay().setDemoState (forge::lcd::LcdState { {}, {}, {}, {}, true, 3, 4, 0.1 });
        captureView ("lcd_countin");
        controlBar.getLcdDisplay().setEdit (session.getEdit());   // resume live polling

        // W04b: ONE window-level capture — the menu bar is window chrome ABOVE the shell content,
        // so component snapshots exclude it; snapping the top-level window includes bar + shell.
        // (The OS-native title bar is peer chrome outside the component tree — expected absent.)
        // Must run BEFORE the short-window setSize mutation below.
        if (auto* top = getTopLevelComponent())
        {
            auto image = top->createComponentSnapshot (top->getLocalBounds());
            auto file  = File::getSpecialLocation (File::tempDirectory)
                             .getChildFile ("forge_shot_shell_window.png");
            file.deleteFile();
            if (auto out = std::unique_ptr<FileOutputStream> (file.createOutputStream()))
            {
                PNGImageFormat png;
                png.writeImageToStream (image, *out);
            }
            else
            {
                FORGE_LOG_ERROR ("Failed to write window-level PNG snapshot: " + file.getFullPathName());
            }
        }

        setViewMode (ViewMode::Mixer);     captureView ("mix");

        // Prove vertical scroll headlessly: force a SHORT window so the vertical scrollbar appears and the
        // bottom scene rows are reachable only by scrolling, snap the top, scroll to the true bottom, snap
        // again. Comparing session_top vs session_scrolled shows: scrollbar present, bottom rows reachable
        // (not clipped), pads stay 46px (no stretch), and the pinned scene column tracks the pads.
        setViewMode (ViewMode::Session);
        setSize (1040, 360);                                                  // ~5-6 of 16 rows visible
        sessionView.resized();                                               // re-layout at the short height
        sessionView.getViewport().setViewPosition (0, 0);                     // top reference
        captureView ("session_top");
        sessionView.getViewport().setViewPositionProportionately (0.0, 1.0);  // scroll to the true bottom
        captureView ("session_scrolled");

        // W07: prove the DYNAMIC scene count renders past the former constexpr=16 ceiling. Grow the demo
        // grid to 20 scenes (the +Scene path), rebuild, scroll to the true bottom, and snap — rows 16-19
        // must render + stay aligned (the pinned scene column tracks the pads) with no drift or clipping.
        session.ensureScenes (20);
        sessionView.rebuild();
        sessionView.resized();
        sessionView.getViewport().setViewPositionProportionately (0.0, 1.0);  // bottom: rows 16-19 in view
        captureView ("session_scenes");

        FORGE_LOG_INFO ("Screenshots written to " + File::getSpecialLocation (File::tempDirectory).getFullPathName());

        if (auto* t = session.getTransport())
            t->stop (false, false);

        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==============================================================================
    void swapProject (std::function<void()> doSwap)
    {
        PluginWindow::closeAll();    // plugin editors belong to the outgoing Edit

        // Abort any in-flight async export BEFORE the Edit is torn down (QC blocker, P4). ~AsyncRender
        // joins the render worker (stopThread) and runs its engine teardown (turnOffAllPlugins(edit),
        // ScopedRenderStatus dtor) against the Edit, so it must complete while the Edit is still ALIVE —
        // otherwise the worker keeps rendering a graph built from freed tracks/clips (cross-thread UAF).
        // reset() is a no-op when no export is running.
        activeRender.reset();
        exportProgress.setVisible (false);

        detailView.setClip (nullptr);
        channelTray.setTrack (nullptr);    // the outgoing Edit's tracks are about to die (W04a)
        pianoRoll.setMidiClip (nullptr);   // drop any MIDI clip held from the outgoing Edit
        bottomMode = BottomMode::Detail;
        controlBar.setEdit (nullptr);
        arrangeView.setEdit (nullptr);
        mixerView.setEdit (nullptr);
        midiLearn.cancelLearn();             // abandon any in-flight learn while its Edit is still alive (QC blocker, P2)
        midiLearn.setActiveEdit (nullptr);   // drop the live-apply target before the Edit is destroyed (P2)

        // Drop any MIDI record-arm before the outgoing Edit is torn down (stop the transport first —
        // removeTarget fails while recording). The Edit owns the input-device targets, so this is
        // hygiene rather than a correctness requirement, but it keeps arm state from surviving a swap.
        if (auto* ed = session.getEdit())
        {
            if (auto* t = session.getTransport())
                t->stop (false, false);
            for (auto* track : te::getAudioTracks (*ed))
                if (track != nullptr)
                    recorder.disarmMidiTrack (*ed, *track);
        }

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
        midiLearn.setActiveEdit (session.getEdit());   // live-apply target follows the open edit (P2)
        markerBar.refresh();                            // reflect the new edit's markers (P5)
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

        if (mode == SelfTest::midi)
        {
            stopTimer();
            switch (miPhase)
            {
                case 1:  midiSelftestEnableDevice(); break;   // device created → find + enable, yield
                case 2:  midiSelftestArmAndRoll();   break;   // enabled → alloc ctx, arm slot, roll, yield
                case 3:  midiSelftestInjectOn();     break;   // rolling → inject note-ons, yield
                case 4:  midiSelftestInjectOff();    break;   // → inject note-offs, yield
                default: finishSelfTestMidi();       break;   // phase 5 → stop, verify, report, quit
            }
            return;
        }

        if (mode == SelfTest::midilearn)
        {
            stopTimer();
            if (mlPhase == 1)  midiLearnSelftestInjectCC();   // learn armed → inject CC, yield for the async bind
            else               finishSelfTestMidiLearn();     // phase 2 → verify + report + quit
            return;
        }

        if (mode == SelfTest::midiinput)
        {
            stopTimer();
            finishSelfTestMidiInput();   // single yield after the seam bind → verify the focused Edit + mapping
            return;
        }

        if (mode == SelfTest::popout)
        {
            stopTimer();
            finishPopoutSelftest();   // single yield: the deferred window resets have run
            return;
        }

        if (mode == SelfTest::controlsurface)
        {
            stopTimer();
            finishControlSurfaceSelftest();   // single yield (1500 ms) after begin() launched + drained
            return;
        }

        if (mode == SelfTest::sync)
        {
            stopTimer();
            switch (syPhase)
            {
                case 1:  syncSelftestInject();  break;   // scan settled -> free ctx, swap in probe, reallocate, roll
                case 2:  syncSelftestStop();    break;   // rolled -> stop (emits midiStop), yield for the dispatcher
                default: finishSelfTestSync();  break;   // phase 3 -> stop edge drained, verify, report, quit
            }
            return;
        }

        if (mode == SelfTest::automation)
        {
            auto* t = session.getTransport();
            const double pos = (t != nullptr) ? t->getPosition().inSeconds() : -1.0;

            // Capture the FIRST observed tick unconditionally as the early sample. If that first
            // position is already past 0.3 s, still record it but the value threshold guards us
            // (a slow start reads high on a falling ramp, so >= 0.7 still holds early); a truly
            // non-rolling device is caught by the ~4 s poll cap below (position stays ~0).
            if (! auEarlyCaptured && auVolParam != nullptr)
            {
                auEarlyValue    = auVolParam->getCurrentValue();
                auEarlyPos      = pos;
                auEarlyCaptured = true;
            }

            // Late sample: first tick whose position is in the [1.5, 2.4] s window (curve 0.35 -> 0.2).
            if (! auLateCaptured && auVolParam != nullptr && pos >= 1.5 && pos <= 2.4)
            {
                auLateValue    = auVolParam->getCurrentValue();
                auLateCaptured = true;
            }

            // Finish as soon as both samples are in, or after ~4 s (40 ticks) so a non-rolling device
            // FAILs instead of stalling.
            if ((auEarlyCaptured && auLateCaptured) || ++automationPollTicks >= 40)
            {
                stopTimer();
                finishAutomationSelftest();
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
               << "transportReadout="  << (controlBar.getLcdDisplay().readoutIsNonEmpty() ? 1 : 0) << newLine
               << "hasContext="        << (hasContext ? 1 : 0) << newLine
               << "playing="           << (playing ? 1 : 0) << newLine
               << "position="          << String (posSecs, 3) << newLine
               << "result="            << (pass ? "PASS" : "FAIL") << newLine
               << "logFile="           << forge::log::getLogFile().getFullPathName() << newLine;

        const auto reportFile = File::getSpecialLocation (File::tempDirectory)
                                    .getChildFile ("forge_phase0_selftest.log");
        if (! reportFile.replaceWithText (report))
            FORGE_LOG_ERROR ("Failed to write playback selftest report to: " + reportFile.getFullPathName());

        FORGE_LOG_INFO ("Playback selftest " + juce::String (pass ? "PASS" : "FAIL")
                        + " — report: " + reportFile.getFullPathName());
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
// --selftest-lufs: prove the BS.1770-4 analyzer measures a known-loudness signal. A MONO full-scale 1 kHz
// sine at 48 kHz has integrated loudness -3.00 LUFS (model value -3.0036); PASS within ±0.5 LU. A pure
// analyzer gate — no engine / edit / device — so it runs before the window is built (see initialise()).
static void runLufsSelfTest()
{
    constexpr double sr   = 48000.0;
    constexpr int    secs = 4;                         // >= 3 s so the gating has material
    const int        N    = (int) (sr * secs);

    juce::AudioBuffer<float> buf (1, N);               // MONO full-scale 1 kHz sine
    auto* d = buf.getWritePointer (0);
    for (int i = 0; i < N; ++i)
        d[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / sr);

    forge::dsp::LoudnessAnalyzer analyzer;
    analyzer.prepare (sr, 1);
    analyzer.processBlock (buf.getArrayOfReadPointers(), 1, N);
    const auto r = analyzer.getResult();

    constexpr float expected = -3.0036f, tol = 0.5f;
    const bool bufPass = std::isfinite (r.integratedLufs) && std::abs (r.integratedLufs - expected) <= tol;

    // --- File+thread leg: run the EXACT static file path the export worker executes, off the message
    // thread, and assert it agrees with the buffer-fed result. Also exercises the abort predicate. ---
    const auto tmpWav = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("forge_lufs_selftest.wav");
    tmpWav.deleteFile();

    bool fileWritten = false;
    {
        juce::WavAudioFormat wav;
        if (auto* os = tmpWav.createOutputStream().release())
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (os, sr, 1, 24, {}, 0));

            if (writer != nullptr)
                fileWritten = writer->writeFromAudioSampleBuffer (buf, 0, N);
            else
                delete os;   // createWriterFor didn't take ownership on failure
        }
    }

    // Run analyzeFile on a spawned one-shot thread; join with a bounded wait (proves the off-thread path).
    struct FileLegThread : public juce::Thread
    {
        FileLegThread (const juce::File& f) : juce::Thread ("Forge lufs selftest"), file (f) {}
        void run() override { result = forge::dsp::LoudnessAnalyzer::analyzeFile (file); }
        juce::File file;
        forge::dsp::LoudnessAnalyzer::Result result;
    };

    float fileLufs = forge::dsp::LoudnessAnalyzer::kSilenceLufs;
    bool  fileJoined = false;

    if (fileWritten)
    {
        FileLegThread th (tmpWav);
        th.startThread();
        fileJoined = th.stopThread (10000);   // bounded join
        fileLufs   = th.result.integratedLufs;
    }

    const bool filePass = fileWritten && fileJoined
                          && std::isfinite (fileLufs)
                          && std::abs (fileLufs - r.integratedLufs) <= 0.1f;   // agrees within 0.1 LU

    // --- Abort leg: an always-true predicate must return promptly with the silence sentinel (-inf). ---
    const auto abortResult = fileWritten
        ? forge::dsp::LoudnessAnalyzer::analyzeFile (tmpWav, [] { return true; })
        : forge::dsp::LoudnessAnalyzer::Result{};
    const bool abortPass = fileWritten && ! std::isfinite (abortResult.integratedLufs);

    tmpWav.deleteFile();   // clean up the temp WAV on exit

    const bool pass = bufPass && filePass && abortPass;

    juce::String report;
    report << "mode=lufs" << juce::newLine
           << "integratedLufs=" << juce::String (r.integratedLufs, 4) << juce::newLine
           << "expectedLufs="   << juce::String (expected, 4) << juce::newLine
           << "toleranceLu="    << juce::String (tol, 2) << juce::newLine
           << "truePeakDb="     << juce::String (r.truePeakDb, 4) << juce::newLine
           << "momentaryLufs="  << juce::String (r.momentaryLufs, 4) << juce::newLine
           << "bufResult="      << (bufPass ? "PASS" : "FAIL") << juce::newLine
           << "fileLufs="       << juce::String (fileLufs, 4) << juce::newLine
           << "fileJoined="     << (fileJoined ? "1" : "0") << juce::newLine
           << "fileResult="     << (filePass ? "PASS" : "FAIL") << juce::newLine
           << "abortLufs="      << juce::String (abortResult.integratedLufs, 4) << juce::newLine
           << "abortResult="    << (abortPass ? "PASS" : "FAIL") << juce::newLine
           << "result="         << (pass ? "PASS" : "FAIL") << juce::newLine
           << "logFile="        << forge::log::getLogFile().getFullPathName() << juce::newLine;

    const auto reportFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("forge_phase0_selftest.log");
    if (! reportFile.replaceWithText (report))
        FORGE_LOG_ERROR ("Failed to write lufs selftest report to: " + reportFile.getFullPathName());

    FORGE_LOG_INFO ("LUFS selftest " + juce::String (pass ? "PASS" : "FAIL")
                    + " — integrated " + juce::String (r.integratedLufs, 3) + " LUFS"
                    + " — report: " + reportFile.getFullPathName());
}

//==============================================================================
// --selftest-lcd: prove the W04a LCD display model + the pad pulse curve, headlessly. PURE — no
// engine / edit / device (the model is plain ints/doubles/strings by design), so it runs and
// quits before the window is built, like --selftest-lufs. Expected values are the skeptic-verified
// acceptance table from the W04a dossier, hand-derived for a fresh Edit (120 BPM, 4/4, key C)
// with count-in N=4 punching at 0 s: the engine pre-rolls (N + 0.5) beats, digit k flips exactly
// on click beat k, and the first half-beat is the digit-0 "ready" lead-in.
static void runLcdSelfTest()
{
    using forge::lcd::LcdInput;
    using forge::lcd::computeLcdState;

    int failures = 0;
    juce::StringArray failed;
    juce::String report;
    report << "mode=lcd" << juce::newLine;

    auto expect = [&] (const char* name, bool ok)
    {
        report << name << "=" << (ok ? "PASS" : "FAIL") << juce::newLine;
        if (! ok) { ++failures; failed.add (name); }
    };

    // Base input: fresh-edit defaults (120 BPM, 4/4, key C).
    LcdInput base;
    base.timeSigString = "4/4";
    base.keyString     = "C";

    // -- Idle at position 0: 1|1, 120.0, C · 4/4, no count-in, phase 0.
    {
        const auto s = computeLcdState (base);
        expect ("idlePosition",  s.positionText == "1|1");
        expect ("idleTempo",     s.tempoText == "120.0");
        expect ("idleKeySig",    s.keySigText == juce::String::fromUTF8 ("C \xc2\xb7 4/4"));
        expect ("idleNoCountIn", ! s.countInActive && s.countInDigit == 0);
        expect ("idlePhase",     s.pulsePhase == 0.0);
    }

    // -- Playing at 6.5 beats -> bar 2, beat 3, phase 0.5 (raw toBarsAndBeats values: bars=1,
    //    wholeBeats=2, fractional 0.5 — skeptic-recomputed).
    {
        auto in = base;
        in.bars = 1; in.beatInBar = 2; in.fractionalBeat = 0.5;
        in.positionSeconds = 3.25;
        const auto s = computeLcdState (in);
        expect ("playPosition", s.positionText == "2|3");
        expect ("playPhase",    s.pulsePhase == 0.5);
    }

    // -- Timecode zone (W04b): absolute time from positionSeconds — "M:SS.mmm" under an hour,
    //    "H:MM:SS.mmm" from one hour up. A negative position (count-in pre-roll) clamps to
    //    "0:00.000" — the model must never emit a minus sign.
    {
        auto timecodeAt = [&base] (double posSeconds)
        {
            auto in = base;
            in.positionSeconds = posSeconds;
            return computeLcdState (in).timecodeText;
        };
        expect ("timecodeZero",     timecodeAt (0.0)    == "0:00.000");
        expect ("timecodeMinutes",  timecodeAt (83.204) == "1:23.204");
        expect ("timecodeHours",    timecodeAt (3723.5) == "1:02:03.500");
        expect ("timecodeNegative", timecodeAt (-2.25)  == "0:00.000");
    }

    // -- Count-in acceptance table (click-grid form): the digit derives from WHOLE TIMELINE
    //    BEATS (where the engine's clicks land), never from distances to the punch. Aligned
    //    case first: N=4, punch at beat 0 (0 s), record latched from stopped; the engine
    //    pre-rolls 4.5 beats and the position runs NEGATIVE up to the punch.
    auto countIn = [&base] (double posSeconds, double currentBeat, double punchBeat, double punchSeconds)
    {
        auto in = base;
        in.recording          = true;
        in.startedFromStopped = true;
        in.countInTotal       = 4;
        in.punchTimeSeconds   = punchSeconds;
        in.positionSeconds    = posSeconds;
        in.currentBeat        = currentBeat;
        in.punchBeat          = punchBeat;
        in.bars = -2; in.beatInBar = 0;   // raw bars run negative in pre-roll; must never surface
        return computeLcdState (in);
    };

    { const auto s = countIn (-2.25,  -4.5,  0.0, 0.0); expect ("countInLeadIn", s.countInActive && s.countInDigit == 0); }
    { const auto s = countIn (-2.0,   -4.0,  0.0, 0.0); expect ("countInBeat1",  s.countInActive && s.countInDigit == 1); }
    { const auto s = countIn (-1.0,   -2.0,  0.0, 0.0); expect ("countInBeat3",  s.countInActive && s.countInDigit == 3); }
    { const auto s = countIn (-0.125, -0.25, 0.0, 0.0); expect ("countInBeat4",  s.countInActive && s.countInDigit == 4); }
    { const auto s = countIn ( 0.0,    0.0,  0.0, 0.0); expect ("countInPunch", ! s.countInActive && s.countInDigit == 0); }   // pos !< punch -> punched in

    // -- NON-ALIGNED punch (the QC major): recording from a mid-beat stop at beat 2.3. Clicks
    //    land on whole beats -1, 0, 1, 2 (firstClick = ceil(2.3 - 4) = -1); each digit must
    //    flip ON its click, not 0.7 beats early as the old distance form did.
    { const auto s = countIn (-1.10, -2.2, 2.3, 1.15); expect ("countInNA_preClick", s.countInActive && s.countInDigit == 0); }   // before the first click
    { const auto s = countIn (-0.85, -1.7, 2.3, 1.15); expect ("countInNA_stillLeadIn", s.countInActive && s.countInDigit == 0); } // old form said 1 here — must be 0
    { const auto s = countIn (-0.50, -1.0, 2.3, 1.15); expect ("countInNA_beat1", s.countInActive && s.countInDigit == 1); }       // ON click beat -1
    { const auto s = countIn ( 0.15,  0.3, 2.3, 1.15); expect ("countInNA_beat2", s.countInActive && s.countInDigit == 2); }
    { const auto s = countIn ( 1.00,  2.0, 2.3, 1.15); expect ("countInNA_beat4", s.countInActive && s.countInDigit == 4); }       // ON click beat 2
    { const auto s = countIn ( 1.15,  2.3, 2.3, 1.15); expect ("countInNA_punch", ! s.countInActive); }

    // -- Skeptic guard 1: the same pre-punch shape WITHOUT the stopped-transport latch is a
    //    mid-playback punch-in (no pre-roll, stale start time after a backward seek) — the
    //    count-in face must stay off.
    {
        auto in = base;
        in.recording          = true;
        in.startedFromStopped = false;   // record() fired while already playing
        in.countInTotal       = 4;
        in.punchTimeSeconds   = 8.0;     // stale startTime ahead of the position
        in.positionSeconds    = 4.0;
        in.currentBeat        = 8.0;
        in.punchBeat          = 16.0;
        const auto s = computeLcdState (in);
        expect ("phantomCountInSuppressed", ! s.countInActive && s.countInDigit == 0);
    }

    // -- Skeptic guard 2: FP overshoot/undershoot at an exact click boundary must not glitch
    //    the digit — the epsilon inside floor()/ceil() absorbs it.
    {
        const auto s = countIn (-2.0, -4.0 - 1.0e-9, 0.0, 0.0);
        expect ("epsilonAtBoundary", s.countInActive && s.countInDigit == 1);
    }

    // -- Beat-phase leg: the pulse phase is the fractional beat passed through untouched
    //    (dossier: beatPhase at 3.25 beats == 0.25).
    {
        auto in = base;
        in.fractionalBeat = 0.25;
        expect ("beatPhase", computeLcdState (in).pulsePhase == 0.25);
    }

    // -- Pad pulse curve (SlotVisualState::padPulseAlpha — the W04a sequence-lighting leg;
    //    documented curve values, float math, 1e-4 tolerance).
    {
        auto near = [] (float a, float b) { return std::abs (a - b) <= 1.0e-4f; };
        expect ("pulsePlaying", near (padPulseAlpha (SlotVisualState::playing, 0.0),   1.0f)
                             && near (padPulseAlpha (SlotVisualState::playing, 0.5),   0.775f)
                             && near (padPulseAlpha (SlotVisualState::playing, 0.999), 0.55045f));
        expect ("pulseQueued",  near (padPulseAlpha (SlotVisualState::queued,  0.0),   0.35f)
                             && near (padPulseAlpha (SlotVisualState::queued,  0.5),   0.75f)
                             && near (padPulseAlpha (SlotVisualState::queued,  0.999), 0.3508f));
        expect ("pulseRecording", near (padPulseAlpha (SlotVisualState::recording, 0.0),   1.0f)
                               && near (padPulseAlpha (SlotVisualState::recording, 0.5),   1.0f)
                               && near (padPulseAlpha (SlotVisualState::recording, 0.999), 1.0f));
        expect ("pulseOthers",  padPulseAlpha (SlotVisualState::empty,    0.5) == 0.0f
                             && padPulseAlpha (SlotVisualState::hasClip,  0.5) == 0.0f
                             && padPulseAlpha (SlotVisualState::stopping, 0.5) == 0.0f
                             && padPulseAlpha (SlotVisualState::recArmed, 0.5) == 0.0f);
        expect ("pulseWrap",    near (padPulseAlpha (SlotVisualState::playing,  1.25),
                                      padPulseAlpha (SlotVisualState::playing,  0.25))
                             && near (padPulseAlpha (SlotVisualState::playing, -0.25),
                                      padPulseAlpha (SlotVisualState::playing,  0.75)));
    }

    const bool pass = failures == 0;

    report << "checksFailed=" << failures << juce::newLine
           << "result="       << (pass ? "PASS" : "FAIL") << juce::newLine
           << "logFile="      << forge::log::getLogFile().getFullPathName() << juce::newLine;

    const auto reportFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("forge_phase0_selftest.log");
    if (! reportFile.replaceWithText (report))
        FORGE_LOG_ERROR ("Failed to write lcd selftest report to: " + reportFile.getFullPathName());

    FORGE_LOG_INFO ("LCD selftest " + juce::String (pass ? "PASS" : "FAIL")
                    + (pass ? juce::String() : " — failing: " + failed.joinIntoString (", "))
                    + " — report: " + reportFile.getFullPathName());
}

//==============================================================================
// --selftest-menu: prove the menu-bar model is complete and safe. A PURE model gate — no engine,
// edit, device, or window. The model is first walked and dispatched BARE (every std::function
// unset: must no-op, never crash), then flag-capturing callbacks and tick queries are wired and
// asserted. Known name/shortcut pairs are pinned (labels are display-only; keyPressed owns the
// keys) so a shortcut rebind that forgets the menu table fails loudly here.
static void runMenuSelfTest()
{
    ForgeMenuModel model;   // deliberately UNWIRED first

    // Null-safety leg: dispatch through the bare model — silent no-op or the gate crashes.
    model.menuItemSelected (ForgeMenuModel::cmdSave,         (int) ForgeMenuModel::menuFile);
    model.menuItemSelected (ForgeMenuModel::cmdCountIn2Bars, (int) ForgeMenuModel::menuTransport);

    // Shape leg: 5 named menus, expected item counts (getNumItems() excludes separators; the
    // Count-In submenu counts as ONE item of the Transport menu).
    auto names = model.getMenuBarNames();
    const bool namesPass = names.size() == (int) ForgeMenuModel::numMenus
                           && names[(int) ForgeMenuModel::menuFile]      == "File"
                           && names[(int) ForgeMenuModel::menuTransport] == "Transport";

    const int expectedCounts[] = { 10, 3, 7, 6, 1 };  // File (+1 Exit, hands-on 1.5), Edit (+2 W05 undo/redo), View, Transport, Help
    bool countsPass = names.size() == (int) (sizeof (expectedCounts) / sizeof (expectedCounts[0]));
    for (int m = 0; countsPass && m < names.size(); ++m)
        countsPass = model.getMenuForIndex (m, names[m]).getNumItems() == expectedCounts[m];

    // Id + shortcut leg: every real item has a non-zero id (0 = dismissed-without-selection;
    // submenu parents legitimately carry 0), and known name/shortcut pairs hold.
    // NOTE: MenuItemIterator stores a POINTER to the menu — the menu must be a named local,
    // never a temporary, or the iterator dangles.
    bool idsPass = true;
    juce::String saveShortcut, openShortcut;

    for (int m = 0; m < names.size(); ++m)
    {
        auto menu = model.getMenuForIndex (m, names[m]);

        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            auto& item = it.getItem();

            if (! item.isSeparator && item.subMenu == nullptr && item.itemID == 0)
                idsPass = false;

            if (item.text == "Save")    saveShortcut = item.shortcutKeyDescription;
            if (item.text == "Open...") openShortcut = item.shortcutKeyDescription;
        }
    }

    const bool shortcutsPass = saveShortcut == "Ctrl+S" && openShortcut == "Ctrl+O";

    // Dispatch leg: wire two flag-capturing callbacks and invoke them via menuItemSelected.
    bool saveFired  = false;
    int  viewModeArg = -1;
    model.callbacks.onSave     = [&saveFired]           { saveFired = true; };
    model.callbacks.onViewMode = [&viewModeArg] (int m) { viewModeArg = m; };
    model.menuItemSelected (ForgeMenuModel::cmdSave,        (int) ForgeMenuModel::menuFile);
    model.menuItemSelected (ForgeMenuModel::cmdViewArrange, (int) ForgeMenuModel::menuView);
    const bool dispatchPass = saveFired && viewModeArg == 1;

    // Tick leg: wire queries, rebuild (menus are built fresh on every getMenuForIndex call),
    // and check ticks follow the queries; an UNSET query (metronome) must read unticked.
    model.callbacks.queryViewMode       = [] { return 1; };      // Arrange is current
    model.callbacks.queryBrowserVisible = [] { return true; };
    model.callbacks.queryCountInBars    = [] { return 2; };

    bool arrangeTicked = false, sessionTicked = false, browserTicked = false;
    {
        auto view = model.getMenuForIndex ((int) ForgeMenuModel::menuView, "View");
        for (juce::PopupMenu::MenuItemIterator it (view, true); it.next();)
        {
            auto& item = it.getItem();
            if (item.text == "Arrange")         arrangeTicked = item.isTicked;
            if (item.text == "Session")         sessionTicked = item.isTicked;
            if (item.text == "Browser Sidebar") browserTicked = item.isTicked;
        }
    }

    bool twoBarsTicked = false, oneBarTicked = false, metronomeTicked = false;
    {
        auto transportMenu = model.getMenuForIndex ((int) ForgeMenuModel::menuTransport, "Transport");
        for (juce::PopupMenu::MenuItemIterator it (transportMenu, true); it.next();)
        {
            auto& item = it.getItem();
            if (item.text == "2 bars")          twoBarsTicked   = item.isTicked;
            if (item.text == "1 bar")           oneBarTicked    = item.isTicked;
            if (item.text == "Metronome Click") metronomeTicked = item.isTicked;   // query unset -> unticked
        }
    }

    const bool ticksPass = arrangeTicked && ! sessionTicked && browserTicked
                           && twoBarsTicked && ! oneBarTicked && ! metronomeTicked;

    const bool pass = namesPass && countsPass && idsPass && shortcutsPass && dispatchPass && ticksPass;

    juce::String report;
    report << "mode=menu" << juce::newLine
           << "menus="        << names.size() << juce::newLine
           << "names="        << (namesPass     ? "PASS" : "FAIL") << juce::newLine
           << "itemCounts="   << (countsPass    ? "PASS" : "FAIL") << juce::newLine
           << "itemIds="      << (idsPass       ? "PASS" : "FAIL") << juce::newLine
           << "saveShortcut=" << saveShortcut << juce::newLine
           << "shortcuts="    << (shortcutsPass ? "PASS" : "FAIL") << juce::newLine
           << "dispatch="     << (dispatchPass  ? "PASS" : "FAIL") << juce::newLine
           << "ticks="        << (ticksPass     ? "PASS" : "FAIL") << juce::newLine
           << "result="       << (pass          ? "PASS" : "FAIL") << juce::newLine
           << "logFile="      << forge::log::getLogFile().getFullPathName() << juce::newLine;

    const auto reportFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("forge_phase0_selftest.log");
    if (! reportFile.replaceWithText (report))
        FORGE_LOG_ERROR ("Failed to write menu selftest report to: " + reportFile.getFullPathName());

    FORGE_LOG_INFO ("Menu selftest " + juce::String (pass ? "PASS" : "FAIL")
                    + " — report: " + reportFile.getFullPathName());
}

//==============================================================================
class ForgeApplication : public JUCEApplication
{
public:
    const String getApplicationName() override     { return "Forge"; }
    const String getApplicationVersion() override  { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override      { return true; }

    void initialise (const String& commandLine) override
    {
        // Logging comes up FIRST, before anything else can fail, so startup diagnostics are captured.
        const auto modeDesc = commandLine.contains ("--screenshot")              ? "screenshot"
                            : commandLine.contains ("--selftest-record")         ? "selftest-record"
                            : commandLine.contains ("--selftest-sessionmixer")   ? "selftest-sessionmixer"   // before -session (substring! W08)
                            : commandLine.contains ("--selftest-session")        ? "selftest-session"
                            : commandLine.contains ("--selftest-midilearn")      ? "selftest-midilearn"      // before -midi (substring)
                            : commandLine.contains ("--selftest-midiinput")      ? "selftest-midiinput"      // before -midi (substring)
                            : commandLine.contains ("--selftest-midi")           ? "selftest-midi"
                            : commandLine.contains ("--selftest-controlsurface") ? "selftest-controlsurface" // before bare --selftest
                            : commandLine.contains ("--selftest-lufs")           ? "selftest-lufs"           // before bare --selftest
                            : commandLine.contains ("--selftest-lcd")            ? "selftest-lcd"            // before bare --selftest
                            : commandLine.contains ("--selftest-menu")           ? "selftest-menu"           // before bare --selftest
                            : commandLine.contains ("--selftest-tray")           ? "selftest-tray"           // before bare --selftest
                            : commandLine.contains ("--selftest-automation")     ? "selftest-automation"     // before bare --selftest
                            : commandLine.contains ("--selftest-sync")           ? "selftest-sync"           // before bare --selftest
                            : commandLine.contains ("--selftest-livesync")       ? "selftest-livesync"       // before bare --selftest
                            : commandLine.contains ("--selftest-popout")         ? "selftest-popout"         // before bare --selftest
                            : commandLine.contains ("--selftest-undo")           ? "selftest-undo"           // before bare --selftest
                            : commandLine.contains ("--selftest-taptempo")       ? "selftest-taptempo"       // before bare --selftest
                            : commandLine.contains ("--selftest-slotdelete")     ? "selftest-slotdelete"     // before bare --selftest (W07)
                            : commandLine.contains ("--selftest-addtrack")       ? "selftest-addtrack"       // before bare --selftest (W07)
                            : commandLine.contains ("--selftest-scene")          ? "selftest-scene"          // before bare --selftest (W07)
                            : commandLine.contains ("--selftest-dragdrop")       ? "selftest-dragdrop"       // before bare --selftest (W07)
                            : commandLine.contains ("--selftest-demo")           ? "selftest-demo"           // before bare --selftest (W09)
                            : commandLine.contains ("--selftest-sendarrange")    ? "selftest-sendarrange"    // before bare --selftest (W5)
                            : commandLine.contains ("--selftest-followaction")   ? "selftest-followaction"   // before bare --selftest (W1)
                            : commandLine.contains ("--selftest-launchmode")     ? "selftest-launchmode"     // before bare --selftest (W1)
                            : commandLine.contains ("--selftest")                ? "selftest-playback"
                                                                                 : "normal";
        forge::log::install (getApplicationName(), getApplicationVersion(), commandLine, modeDesc);
        FORGE_LOG_INFO ("Forge starting");

        // --selftest-lufs is a PURE analyzer gate (no engine / edit / device) — run it and quit before the
        // window/engine machinery matters. Its report is written the same way as the other selftest gates.
        if (commandLine.contains ("--selftest-lufs"))
        {
            runLufsSelfTest();
            quit();
            return;
        }

        // --selftest-lcd is a PURE model gate (no engine / edit / device) — asserts the W04a LCD
        // display model's acceptance table and the pad pulse curve, then quits before the window
        // is built, exactly like --selftest-lufs.
        if (commandLine.contains ("--selftest-lcd"))
        {
            runLcdSelfTest();
            quit();
            return;
        }

        // --selftest-menu is likewise PURE: it walks the ForgeMenuModel command table with
        // flag-capturing callbacks and quits before any window exists.
        if (commandLine.contains ("--selftest-menu"))
        {
            runMenuSelfTest();
            quit();
            return;
        }

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto mode = commandLine.contains ("--screenshot")              ? SelfTest::screenshot
                        : commandLine.contains ("--selftest-record")         ? SelfTest::record
                        : commandLine.contains ("--selftest-sessionmixer")   ? SelfTest::sessionmixer   // before -session (substring! W08)
                        : commandLine.contains ("--selftest-session")        ? SelfTest::session
                        : commandLine.contains ("--selftest-midilearn")      ? SelfTest::midilearn      // before -midi (substring)
                        : commandLine.contains ("--selftest-midiinput")      ? SelfTest::midiinput      // before -midi (substring)
                        : commandLine.contains ("--selftest-midi")           ? SelfTest::midi
                        : commandLine.contains ("--selftest-controlsurface") ? SelfTest::controlsurface // before bare --selftest
                        : commandLine.contains ("--selftest-automation")     ? SelfTest::automation     // before bare --selftest
                        : commandLine.contains ("--selftest-sync")           ? SelfTest::sync           // before bare --selftest
                        : commandLine.contains ("--selftest-livesync")       ? SelfTest::livesync       // before bare --selftest
                        : commandLine.contains ("--selftest-tray")           ? SelfTest::tray           // before bare --selftest
                        : commandLine.contains ("--selftest-popout")         ? SelfTest::popout         // before bare --selftest
                        : commandLine.contains ("--selftest-undo")           ? SelfTest::undo           // before bare --selftest
                        : commandLine.contains ("--selftest-taptempo")       ? SelfTest::taptempo       // before bare --selftest
                        : commandLine.contains ("--selftest-slotdelete")     ? SelfTest::slotdelete     // before bare --selftest (W07)
                        : commandLine.contains ("--selftest-addtrack")       ? SelfTest::addtrack       // before bare --selftest (W07)
                        : commandLine.contains ("--selftest-scene")          ? SelfTest::scene          // before bare --selftest (W07)
                        : commandLine.contains ("--selftest-dragdrop")       ? SelfTest::dragdrop       // before bare --selftest (W07)
                        : commandLine.contains ("--selftest-demo")           ? SelfTest::demo           // before bare --selftest (W09)
                        : commandLine.contains ("--selftest-sendarrange")    ? SelfTest::sendarrange    // before bare --selftest (W5)
                        : commandLine.contains ("--selftest-followaction")   ? SelfTest::followaction   // before bare --selftest (W1)
                        : commandLine.contains ("--selftest-launchmode")     ? SelfTest::launchmode     // before bare --selftest (W1)
                        : commandLine.contains ("--selftest")                ? SelfTest::playback
                                                                             : SelfTest::none;

        // Cosmetic launch splash (hands-on 1.7) — SKIPPED for every --selftest-*/--screenshot run so the
        // headless floor never spawns a window. It cannot mask the ~8s te::Engine construction (a
        // ForgeApplication member built before initialise() ran); see SplashWindow.h's header note.
        if (mode == SelfTest::none)
        {
            splashWindow = std::make_unique<forge::SplashWindow>();
            splashWindow->centreAndShow();
        }

        mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, mode), *this));

        // MainWindow's ctor calls setVisible(true) synchronously — it is showing now, so drop the splash.
        splashWindow = nullptr;

        // Post-open device snapshot (Option A). The engine member opened its OUTPUT device during
        // ForgeApplication construction — BEFORE install() ran — so an open *failure* can't be captured,
        // but we can record the resulting device and flag a total absence of any output device.
        if (auto* dev = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
            FORGE_LOG_INFO ("Engine output device: " + dev->getName()
                            + " @ " + String (dev->getCurrentSampleRate(), 0) + " Hz"
                            + ", buffer " + String (dev->getCurrentBufferSizeSamples()));
        else
            FORGE_LOG_ERROR ("No output audio device after startup — playback will be unavailable");
    }

    void shutdown() override
    {
        FORGE_LOG_INFO ("Forge shutting down");
        mainWindow = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        forge::log::shutdown();   // restores the previous juce::Logger, clears logFile, flushes + closes
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

            // Traditional top menu bar (W04a). setMenuBar creates the MenuBarComponent and
            // reserves ~24 px above the content automatically; with the native title bar it sits
            // at the top of the client area. The model is owned by the content component — set
            // AFTER setContentOwned, cleared in the destructor BEFORE the content dies.
            setMenuBar (&static_cast<MainComponent*> (c)->getMenuModel());

            setResizable (true, false);
            setResizeLimits (760, 504, 10000, 10000);   // +24 for the menu bar so the shell keeps its floor
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        ~MainWindow() override
        {
            // Detach the bar from the model before ResizableWindow destroys the content that owns
            // it. ~DocumentWindow already does this; being explicit keeps the discipline visible
            // if window teardown is ever restructured (setContentNonOwned, early content release).
            setMenuBar (nullptr);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    ForgeLookAndFeel lookAndFeel;
    te::Engine engine { "Forge", std::make_unique<ForgeUIBehaviour>(), std::make_unique<ForgeEngineBehaviour>() };
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<forge::SplashWindow> splashWindow;   // hands-on 1.7 (cosmetic; skipped in headless)
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
