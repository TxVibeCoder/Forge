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
#include "engine/PluginHost.h"
#include "engine/Metronome.h"
#include "engine/MidiLearn.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/markers/MarkerBar.h"
#include "ui/mixer/MixerView.h"
#include "ui/plugins/PluginWindow.h"
#include "ui/browser/BrowserView.h"
#include "ui/detail/DetailView.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/session/SessionView.h"
#include "ui/export/ExportProgress.h"
#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

namespace te = tracktion;
using namespace juce;

enum class SelfTest { none, playback, record, session, screenshot, midi };
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

        setupControlBar();
        setupStatusStrip();

        addAndMakeVisible (controlBar);
        addAndMakeVisible (arrangeView);
        addAndMakeVisible (markerBar);
        addAndMakeVisible (mixerView);
        addAndMakeVisible (sessionView);
        addAndMakeVisible (browserPanel);
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
        // it has already rebuilt itself, so we only need to save. (onClipSelected/onTrackSelected
        // -> Inspector are left unwired until that feature exists — see docs/devlog/integration.md.)
        arrangeView.onEditMutated = [this]
        {
            if (! session.save())
                FORGE_LOG_ERROR ("Failed to save project — I/O error");

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
                drawerVisible = true;

            resized();
        };
        detailView.onEditMutated = [this] { if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };
        pianoRoll.onEditMutated  = [this] { if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };

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
            else
            {
                FORGE_LOG_ERROR ("Failed to create MIDI clip in track " + juce::String (trackIndex));
            }
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
        sessionView.onEditMutated = [this] { if (! session.save()) FORGE_LOG_ERROR ("Failed to save project — I/O error"); };

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
                FORGE_LOG_ERROR ("Failed to save project after aux-bus add");

            if (sessionViewBinds() && session.getNumAudioTracks() != sessionView.getNumColumns())
                sessionView.rebuild();
            arrangeView.rebuild();
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
        markerBar.onEditMutated   = [this] { if (! session.save()) FORGE_LOG_ERROR ("Failed to save project after marker edit"); };

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
        else if (mode == SelfTest::midi)
        {
            // Headless synthetic-MIDI capture-into-slot gate (verdict-A proof). Event-driven like the
            // record selftest: create a virtual MIDI in → yield → enable → yield → arm slot (0,0) + roll →
            // yield → inject note-ons → yield → note-offs → yield → stop + verify. See §4 of
            // docs/devlog/midi-record-design.md.
            MessageManager::callAsync ([this] { beginSelfTestMidi(); });
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

    ControlBar controlBar;
    TimelineView timelineView;
    ArrangeView arrangeView { timelineView };
    MarkerBar markerBar { timelineView };   // markers strip over the arrange timeline (shares the axis, P5)
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
        controlBar.onToggleBrowser = [this] { browserVisible = ! browserVisible; resized(); };
        controlBar.onToggleDrawer  = [this] { drawerVisible  = ! drawerVisible;  resized(); };
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

            self->setStatusMessage (ok ? (error.isEmpty() ? caption + " — done"
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
            if (session.createMidiClipInSlot (c.track, c.scene, c.name) == nullptr)
                FORGE_LOG_WARN ("Screenshot harness: failed to create demo MIDI clip at ("
                                + juce::String (c.track) + "," + juce::String (c.scene) + ")");

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
class ForgeApplication : public JUCEApplication
{
public:
    const String getApplicationName() override     { return "Forge"; }
    const String getApplicationVersion() override  { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override      { return true; }

    void initialise (const String& commandLine) override
    {
        // Logging comes up FIRST, before anything else can fail, so startup diagnostics are captured.
        const auto modeDesc = commandLine.contains ("--screenshot")       ? "screenshot"
                            : commandLine.contains ("--selftest-record")  ? "selftest-record"
                            : commandLine.contains ("--selftest-session") ? "selftest-session"
                            : commandLine.contains ("--selftest-midi")    ? "selftest-midi"
                            : commandLine.contains ("--selftest")         ? "selftest-playback"
                                                                          : "normal";
        forge::log::install (getApplicationName(), getApplicationVersion(), commandLine, modeDesc);
        FORGE_LOG_INFO ("Forge starting");

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto mode = commandLine.contains ("--screenshot")       ? SelfTest::screenshot
                        : commandLine.contains ("--selftest-record")  ? SelfTest::record
                        : commandLine.contains ("--selftest-session") ? SelfTest::session
                        : commandLine.contains ("--selftest-midi")    ? SelfTest::midi
                        : commandLine.contains ("--selftest")         ? SelfTest::playback
                                                                      : SelfTest::none;
        mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, mode), *this));

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
