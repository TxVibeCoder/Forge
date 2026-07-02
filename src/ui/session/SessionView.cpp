#include "ui/session/SessionView.h"
#include "ui/ForgeLookAndFeel.h"
#include "engine/EngineHelpers.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
SessionView::SessionView (ProjectSession& s)
    : session (s)
{
    viewport.setViewedComponent (&columnHolder, false);   // we own columnHolder ourselves
    viewport.setScrollBarsShown (true, true);             // (vertical, horizontal): BOTH scrollbars enabled
    viewport.onScroll = [this] { syncSceneColumnToScroll(); };
    addAndMakeVisible (viewport);

    setWantsKeyboardFocus (true);
}

SessionView::~SessionView()
{
    // R4 teardown order, mirrored here: stop the poll FIRST, then drop children, then clear state.
    stopTimer();
    columns.clear();
    scenes.reset();
    focusTrack = focusScene = 0;
    edit = nullptr;
}

//==============================================================================
void SessionView::setEdit (te::Edit* e)
{
    if (e != nullptr)
    {
        edit = e;
        session.ensureScenes (SessionLayout::numScenes);   // grow-only, off undo stack (R3)

        if (session.getNumScenes() < SessionLayout::numScenes)
            FORGE_LOG_ERROR ("Failed to ensure " + juce::String (SessionLayout::numScenes) + " scenes in edit");

        rebuild();
        startTimerHz (25);                                 // §e poll cadence
    }
    else
    {
        // R4 — STRICT teardown order; NO engine read afterward.
        stopTimer();
        columns.clear();
        scenes.reset();
        focusTrack = focusScene = 0;
        edit = nullptr;
    }
}

//==============================================================================
void SessionView::rebuild()
{
    columns.clear();
    scenes.reset();

    lastSlotState.clear();
    lastSceneState.clear();

    lastLoggedTrackCount = -1;   // fresh column set: re-arm the one-shot track-count-mismatch WARN gate

    if (edit != nullptr)
    {
        int trackIndex = 0;

        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* column = columns.add (new TrackColumnComponent (*track, trackIndex));
            wireColumn (*column);
            columnHolder.addAndMakeVisible (column);
            ++trackIndex;
        }

        // Pinned scene column. Scene names are read as PLAIN VALUES for display (R1) — the
        // SceneColumnComponent never caches a te::Scene*.
        StringArray sceneNames;
        for (auto* scene : edit->getSceneList().getScenes())
            sceneNames.add (scene != nullptr ? scene->name.get() : String());

        scenes = std::make_unique<SceneColumnComponent>();
        scenes->setScenes (sceneNames, SessionLayout::numScenes);
        wireScenes();
        addAndMakeVisible (*scenes);

        // Per-pad / per-scene state diff buffers, primed to a value the first poll will overwrite.
        lastSlotState.insertMultiple (0, SlotVisualState::empty, columns.size() * SessionLayout::numScenes);
        lastSceneState.insertMultiple (0, SceneLaunchState::idle, SessionLayout::numScenes);

        focusTrack = jlimit (0, jmax (0, columns.size() - 1), focusTrack);
        focusScene = jlimit (0, SessionLayout::numScenes - 1, focusScene);
    }

    viewport.setViewPosition (0, 0);   // new/rebuilt edit always starts at the top
    resized();
    syncSceneColumnToScroll();          // re-glue the fresh scene column to the (now-reset) scroll offset
    repaint();

    if (edit != nullptr)
    {
        refreshArmStates();
        refreshSlotStates();

        // Re-apply the keyboard-focus / selection highlight. rebuild() recreated every pad (each
        // defaulting unselected), so without this the cursor highlight vanishes after a create/import
        // and the initial (0,0) cursor is never drawn (QC fixes).
        if (auto* col = columns[focusTrack])
            col->setSlotSelected (focusScene, true);
    }
}

void SessionView::wireColumn (TrackColumnComponent& column)
{
    column.onSlotClicked       = [this] (int t, int s)                    { handleSlotClicked (t, s); };
    column.onSlotDoubleClicked = [this] (int t, int s)                    { handleSlotDoubleClicked (t, s); };
    column.onSlotRightClicked  = [this] (int t, int s, const MouseEvent& e) { handleSlotRightClicked (t, s, e); };

    column.onTrackStopAll = [this] (int t) { session.stopTrackClips (t); };

    column.onMute = [this] (int t)
    {
        if (auto* track = getTrackAt (t))
            track->setMute (! track->isMuted (false));
    };
    column.onSolo = [this] (int t)
    {
        if (auto* track = getTrackAt (t))
            track->setSolo (! track->isSolo (false));
    };
    column.onArm = [this] (int t)
    {
        if (auto* track = getTrackAt (t))
        {
            // Branch on the track's plugin content: a MIDI/instrument track arms through the
            // MIDI record path; an audio track keeps the existing audio arm path (design §3).
            if (trackIsMidi (t))
            {
                // Invert the MIDI-arm truth (mutually exclusive with audio arm, v1), so the
                // toggle disarms an already-MIDI-armed track and arms an unarmed one.
                const bool midiArmed = (isTrackMidiArmed != nullptr && isTrackMidiArmed (*track));
                if (onMidiArmToggled != nullptr)
                    onMidiArmToggled (*track, ! midiArmed);
            }
            else
            {
                if (onArmToggled != nullptr)
                    onArmToggled (*track, ! trackArmed (t));
            }
        }
    };

    column.isTrackArmed = [this] (te::AudioTrack& tr) -> bool
    {
        return isTrackArmed != nullptr && isTrackArmed (tr);
    };
}

void SessionView::wireScenes()
{
    if (scenes == nullptr)
        return;

    scenes->onSceneLaunched = [this] (int sceneIdx) { session.launchScene (sceneIdx); };
    scenes->onSceneStopped  = [this] (int sceneIdx)
    {
        // Stop that row across all tracks (no engine "stop scene" call — mirror the per-track stop).
        for (int t = 0; t < columns.size(); ++t)
            session.stopSlot (t, sceneIdx);
    };
    scenes->onStopAll = [this] { session.stopAllSlots(); };
}

//==============================================================================
void SessionView::resized()
{
    auto area = getLocalBounds();

    // Reserve the pinned scene column on the right (OUTSIDE the scrolling viewport, twin of MasterStrip).
    auto sceneArea = area.removeFromRight (SessionLayout::sceneColW);

    viewport.setBounds (area);

    const int nTracks  = columns.size();
    const int columnH  = SessionLayout::headerH
                       + SessionLayout::numScenes * SessionLayout::slotH
                       + SessionLayout::stopRowH;                       // FIXED 844: 78 + 16*46 + 30

    const int contentW = jmax (viewport.getMaximumVisibleWidth(),
                               nTracks * SessionLayout::trackColW);
    const int contentH = columnH;   // fixed height => pads stay slotH tall; short window scrolls,
                                    // tall window shows empty space below (no stretch)

    // Size ONLY — never setBounds(0,0,...). columnHolder is the viewport's viewed component, so its
    // top-left position IS the scroll offset (scrolled-to-bottom sits at y = -getViewPositionY()).
    // Forcing it to (0,0) here would yank the grid back to the top on every relayout (e.g. a window
    // resize while scrolled). setSize lets the Viewport keep ownership of the scroll position.
    columnHolder.setSize (contentW, contentH);

    int x = 0;
    for (auto* column : columns)
    {
        column->setBounds (x, 0, SessionLayout::trackColW, contentH);
        x += SessionLayout::trackColW + SessionLayout::gap;
    }

    // The scene column uses the SAME content height as the track columns (not the raw viewport height),
    // so its shared rowBand partition matches the columns' exactly and the scene rows never drift.
    if (scenes != nullptr)
    {
        // Same contentH as the track columns (shared rowBand partition), translated up by the
        // vertical scroll offset so scene launch row N stays glued to pad row N.
        // Reserve the horizontal-scrollbar band (when shown) so the scene column's bottom stop
        // band lines up with the H-bar-occluded track footers instead of overhanging them.
        const int hBar = viewport.isHorizontalScrollBarShown()
                       ? viewport.getScrollBarThickness() : 0;
        scenes->setBounds (sceneArea.getX(),
                           -viewport.getViewPositionY(),
                           sceneArea.getWidth(),
                           contentH - hBar);
    }
}

void SessionView::syncSceneColumnToScroll()
{
    // Message-thread only (called from the viewport's visibleAreaChanged). Cheap: one move.
    if (scenes != nullptr)
        scenes->setTopLeftPosition (scenes->getX(), -viewport.getViewPositionY());
}

void SessionView::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::shellBg));

    if (columns.isEmpty())
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("No tracks — add a track to begin", getLocalBounds(), Justification::centred);
    }
}

void SessionView::visibilityChanged()
{
    // The shell switches views by toggling visibility, not focus; take keyboard focus when shown so the
    // arrow / Enter / Shift+Enter launch keys actually reach keyPressed (QC fix for dead keyboard play).
    if (isVisible())
        grabKeyboardFocus();
}

//==============================================================================
te::AudioTrack* SessionView::getTrackAt (int trackIdx) const
{
    if (edit == nullptr)
        return nullptr;

    const auto tracks = te::getAudioTracks (*edit);
    return isPositiveAndBelow (trackIdx, tracks.size()) ? tracks[trackIdx] : nullptr;
}

bool SessionView::trackIsMidi (int trackIdx) const
{
    if (auto* track = getTrackAt (trackIdx))
        for (auto* p : track->pluginList)
            if (p != nullptr && (p->isSynth() || p->takesMidiInput()))
                return true;

    return false;
}

bool SessionView::trackArmed (int trackIdx) const
{
    if (isTrackArmed == nullptr)
        return false;

    if (auto* track = getTrackAt (trackIdx))
        return isTrackArmed (*track);

    return false;
}

//==============================================================================
// Interaction — every engine op is delegated to ProjectSession (§c). The view knows the resolved
// state (filled vs empty, MIDI vs audio track) by re-resolving via the const getClipSlot ON DEMAND;
// it never caches the pointer (R1/R2).

void SessionView::handleSlotClicked (int trackIdx, int sceneIdx)
{
    grabKeyboardFocus();   // clicking the grid arms keyboard navigation (arrows / Enter / Shift+Enter)
    setFocus (trackIdx, sceneIdx);

    if (session.isSlotFilled (trackIdx, sceneIdx))
    {
        session.launchSlot (trackIdx, sceneIdx);
        return;
    }

    // Empty slot. On a MIDI track create a born-audible MIDI clip and open the drawer; on an audio
    // track open a file chooser to import a wave.
    if (trackIsMidi (trackIdx))
    {
        if (auto mc = session.createMidiClipInSlot (trackIdx, sceneIdx, "MIDI"))
        {
            if (onMidiClipCreated != nullptr)
                onMidiClipCreated (mc);
            if (onEditMutated != nullptr)
                onEditMutated();

            rebuild();
        }
        else
        {
            FORGE_LOG_ERROR ("Failed to create MIDI clip in slot (" + juce::String (trackIdx) + "," + juce::String (sceneIdx) + ")");
        }
    }
    else
    {
        importAudioInto (trackIdx, sceneIdx);
    }
}

void SessionView::handleSlotDoubleClicked (int trackIdx, int sceneIdx)
{
    setFocus (trackIdx, sceneIdx);
    openSlotForEdit (trackIdx, sceneIdx);   // convenience edit gesture (right-click "Edit clip" is the other)
}

void SessionView::openSlotForEdit (int trackIdx, int sceneIdx)
{
    // Resolve the clip ON DEMAND (never cached, R1) and route it to the shell's drawer (the piano-roll
    // for a MIDI clip, the inspector otherwise — onSlotSelected decides). Shared by the double-click and
    // the right-click "Edit clip" item so the two edit paths behave identically.
    if (auto* slot = session.getClipSlot (trackIdx, sceneIdx))
        if (auto* clip = slot->getClip())
            if (onSlotSelected != nullptr)
                onSlotSelected (clip);
}

void SessionView::handleSlotRightClicked (int trackIdx, int sceneIdx, const MouseEvent& e)
{
    setFocus (trackIdx, sceneIdx);

    PopupMenu menu;
    const bool filled = session.isSlotFilled (trackIdx, sceneIdx);

    // New W7 ids come AFTER the existing set so they never collide with {idLaunch=1,idStop,idEdit,idImport}.
    enum { idLaunch = 1, idStop, idEdit, idImport, idRecordSlot, idStopRecord };

    // Re-derive record eligibility from engine truth on demand (never cached, R1).
    auto* track = getTrackAt (trackIdx);
    const bool trackMidiArmed = (track != nullptr && isTrackMidiArmed != nullptr && isTrackMidiArmed (*track));
    const bool recordingHere  = (isSlotRecording != nullptr && isSlotRecording (trackIdx, sceneIdx));

    if (filled)
    {
        menu.addItem (idLaunch, "Launch");
        menu.addItem (idStop,   "Stop");
        menu.addItem (idEdit,   "Edit clip");   // launch-free edit path (mirrors double-click, without launching)
        menu.addSeparator();
    }

    // Record gestures: offer "Record into slot" on an empty slot of a MIDI-armed track, and
    // "Stop recording" while this slot is the one capturing (design §3).
    if (recordingHere)
        menu.addItem (idStopRecord, "Stop recording");
    else if (! filled && trackMidiArmed)
        menu.addItem (idRecordSlot, "Record into slot");

    menu.addItem (idImport, "Import audio...");

    Component::SafePointer<SessionView> safeThis (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (
                            Rectangle<int> (e.getScreenX(), e.getScreenY(), 1, 1)),
                        [safeThis, trackIdx, sceneIdx] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            switch (result)
                            {
                                case idLaunch: safeThis->session.launchSlot (trackIdx, sceneIdx); break;
                                case idStop:   safeThis->session.stopSlot   (trackIdx, sceneIdx); break;
                                case idEdit:   safeThis->openSlotForEdit    (trackIdx, sceneIdx); break;
                                case idImport: safeThis->importAudioInto    (trackIdx, sceneIdx); break;
                                case idRecordSlot:
                                    if (safeThis->onSlotRecord != nullptr)
                                        safeThis->onSlotRecord (trackIdx, sceneIdx);
                                    break;
                                case idStopRecord:
                                    if (safeThis->onSlotRecordStop != nullptr)
                                        safeThis->onSlotRecordStop (trackIdx, sceneIdx);
                                    break;
                                default: break;
                            }
                        });
}

void SessionView::importAudioInto (int trackIdx, int sceneIdx)
{
    if (edit == nullptr)
        return;

    // EngineHelpers::browseForAudioFile owns its own shared FileChooser, so we don't keep a chooser
    // member; a SafePointer guards the async callback against teardown (setEdit(nullptr)).
    Component::SafePointer<SessionView> safeThis (this);

    EngineHelpers::browseForAudioFile (edit->engine,
                                       [safeThis, trackIdx, sceneIdx] (const File& file)
                                       {
                                           if (safeThis == nullptr || safeThis->edit == nullptr)
                                               return;

                                           if (safeThis->session.importAudioIntoSlot (trackIdx, sceneIdx, file))
                                           {
                                               if (safeThis->onEditMutated != nullptr)
                                                   safeThis->onEditMutated();

                                               safeThis->rebuild();
                                           }
                                           else
                                           {
                                               FORGE_LOG_ERROR ("Failed to import audio into slot (" + juce::String (trackIdx) + "," + juce::String (sceneIdx) + ")");
                                           }
                                       });
}

//==============================================================================
// Keyboard (§c): arrows move the focus cursor, Enter launches the focused slot, Shift+Enter the
// focused scene. All clamped to grid bounds.

bool SessionView::keyPressed (const KeyPress& key)
{
    if (edit == nullptr || columns.isEmpty())
        return false;

    if (key.isKeyCode (KeyPress::leftKey))   { setFocus (focusTrack - 1, focusScene); return true; }
    if (key.isKeyCode (KeyPress::rightKey))  { setFocus (focusTrack + 1, focusScene); return true; }
    if (key.isKeyCode (KeyPress::upKey))     { setFocus (focusTrack, focusScene - 1); return true; }
    if (key.isKeyCode (KeyPress::downKey))   { setFocus (focusTrack, focusScene + 1); return true; }

    // Ctrl+Enter on the focused empty slot of a MIDI-armed track begins a slot record (design §3).
    // MUST come BEFORE the plain-returnKey handler below so the modifier combo is consumed first.
    if (key.isKeyCode (KeyPress::returnKey) && key.getModifiers().isCommandDown())
    {
        if (auto* track = getTrackAt (focusTrack))
        {
            const bool midiArmed = (isTrackMidiArmed != nullptr && isTrackMidiArmed (*track));
            const bool slotEmpty = ! session.isSlotFilled (focusTrack, focusScene);

            if (midiArmed && slotEmpty && onSlotRecord != nullptr)
                onSlotRecord (focusTrack, focusScene);
        }

        return true;
    }

    if (key.isKeyCode (KeyPress::returnKey))
    {
        if (key.getModifiers().isShiftDown())
        {
            // A scene launch with no clips on that row across any track is a silent no-op; surface it.
            if (focusScene < 0 || focusScene >= SessionLayout::numScenes)
                FORGE_LOG_ERROR ("Failed to launch scene " + juce::String (focusScene) + " via keyboard");

            session.launchScene (focusScene);
        }
        else
        {
            // Launching an empty focused slot is a silent no-op; surface the failed keyboard launch.
            if (! session.isSlotFilled (focusTrack, focusScene))
                FORGE_LOG_ERROR ("Failed to launch slot (" + juce::String (focusTrack) + "," + juce::String (focusScene) + ") via keyboard");

            session.launchSlot (focusTrack, focusScene);
        }

        return true;
    }

    return false;
}

void SessionView::setFocus (int trackIdx, int sceneIdx)
{
    if (columns.isEmpty())
        return;

    const int newTrack = jlimit (0, columns.size() - 1, trackIdx);
    const int newScene = jlimit (0, SessionLayout::numScenes - 1, sceneIdx);

    if (newTrack == focusTrack && newScene == focusScene)
        return;

    const int oldTrack = focusTrack, oldScene = focusScene;
    focusTrack = newTrack;
    focusScene = newScene;

    repaintPad (oldTrack, oldScene);     // clears the old pad's highlight
    repaintPad (focusTrack, focusScene); // sets the new pad's highlight

    // Tray-follow seam (W04b): announce the focus TRACK change (scene moves within the same
    // column are tray-irrelevant) as an INDEX the shell resolves fresh (R1). Interaction paths
    // only — rebuild()'s clamp and the R4 teardown assign focusTrack directly, never through
    // here, so this can't fire mid-rebuild or during teardown. Fired last, after the highlight
    // repaints, so the view state is settled when the shell reacts.
    if (newTrack != oldTrack && onTrackFocusChanged != nullptr)
        onTrackFocusChanged (newTrack);
}

void SessionView::repaintPad (int trackIdx, int sceneIdx)
{
    if (auto* col = columns[trackIdx])
        col->setSlotSelected (sceneIdx, trackIdx == focusTrack && sceneIdx == focusScene);
}

//==============================================================================
void SessionView::refreshArmStates()
{
    for (auto* column : columns)
        column->refreshHeader();
}

void SessionView::refreshSlotStates()
{
    timerCallback();
}

//==============================================================================
// 25 Hz poll (message thread, §e). For each visible pad re-resolve a LIVE slot FRESH via the const
// getClipSlot (R1/R2), compute its state (computeSlotState gates the spin-locked queue read), and
// push only when changed. Scene-row state is derived from the column states of that row.
// W04a: one fractional-beat read per tick additionally drives the playing/queued beat pulse — only
// those animated pads repaint per tick; everything else keeps the repaint-on-change behaviour.

void SessionView::timerCallback()
{
    if (edit == nullptr)
        return;

    // Catch a cross-view structural change (a track added/deleted in Arrange or Mix): if the live
    // track count no longer matches our columns, those TrackColumnComponents hold stale te::AudioTrack&
    // refs — rebuild BEFORE any of them is dereferenced (QC blocker fix). rebuild() re-runs this poll.
    auto tracks = te::getAudioTracks (*edit);
    if (tracks.size() != columns.size())
    {
        // Edge-triggered (NOT per-tick): only log when the live count differs from the last value we
        // logged, so a persistent mismatch across ticks emits a single WARN rather than 25/s.
        const int live = tracks.size();
        if (live != lastLoggedTrackCount)
        {
            FORGE_LOG_WARN ("Track count mismatch in poll: " + juce::String (live) + " live vs " + juce::String (columns.size()) + " (rebuilding)");
            lastLoggedTrackCount = live;
        }

        rebuild();
        return;
    }

    auto* transport = session.getTransport();
    const bool transportRunning = transport != nullptr && transport->isPlaying();

    // W04a sequence lighting: ONE fractional-beat read per tick, derived from the transport (never
    // a free-running animation). toBarsAndBeats is allocation-free on the message thread — a
    // CachedValue position read plus a backwards scan of the prebuilt tempo Section vector — so it
    // fits the existing 25 Hz poll budget. Phase is ABSENT while stopped: no animation.
    bool   havePhase    = false;
    double beatPhase    = 0.0;   // [0,1) within the current beat — drives the playing pulse
    double twoBeatPhase = 0.0;   // [0,1) within a TWO-beat cycle — drives the queued half-rate pulse

    if (transportRunning)
    {
        const auto bb = edit->tempoSequence.toBarsAndBeats (transport->getPosition());
        beatPhase     = bb.getFractionalBeats().inBeats();
        // Half-rate cycle from the beat-in-bar parity (getWholeBeats is >= 0 — beat-in-bar stays
        // positive even at negative pre-roll time). Parity resets at odd-numerator bar boundaries;
        // acceptable — the pulse stays beat-locked either way.
        twoBeatPhase  = ((bb.getWholeBeats() % 2) + beatPhase) * 0.5;
        havePhase     = true;
    }

    const int nTracks = columns.size();

    // Resolve each track's slot list + arm state ONCE per tick (both are scene-invariant). This
    // replaces the old per-pad session.getClipSlot()/trackArmed() calls — each of which walked the
    // whole track tree and allocated (~6,400 walks/s on an 8x16 grid). Now: one getAudioTracks walk
    // + one getClipSlots copy per track (QC perf fix).
    struct TrackPoll { juce::Array<te::ClipSlot*> slots; bool armed = false; };
    juce::Array<TrackPoll> perTrack;
    perTrack.ensureStorageAllocated (nTracks);

    for (int t = 0; t < nTracks; ++t)
    {
        auto* track = tracks[t];
        TrackPoll tp;
        if (track != nullptr)
        {
            tp.slots = track->getClipSlotList().getClipSlots();
            // OR in the MIDI arm so a MIDI-armed (audio-disarmed) track still tints its empty
            // slots recArmed (design §3). Pure engine reads — no logging on this hot path.
            tp.armed = (isTrackArmed     != nullptr && isTrackArmed     (*track))
                    || (isTrackMidiArmed != nullptr && isTrackMidiArmed (*track));
        }
        perTrack.add (std::move (tp));
    }

    for (int s = 0; s < SessionLayout::numScenes; ++s)
    {
        bool anyPlaying = false;
        bool anyQueued  = false;

        for (int t = 0; t < nTracks; ++t)
        {
            const auto& tp = perTrack.getReference (t);

            // R1/R2: live slot read fresh from the once-resolved per-track list; never stored.
            te::ClipSlot* slot = (s < tp.slots.size()) ? tp.slots.getUnchecked (s) : nullptr;

            // Resolve recording-here once per pad from the shell seam (cheap engine read; R1
            // preserved — no ClipSlot* cached). No logging on this poll path.
            const bool recordingHere = (isSlotRecording != nullptr && isSlotRecording (t, s));

            const SlotVisualState state = computeSlotState (slot, transportRunning, tp.armed, recordingHere);

            if (state == SlotVisualState::playing)  anyPlaying = true;
            if (state == SlotVisualState::queued || state == SlotVisualState::stopping) anyQueued = true;

            const int idx = t * SessionLayout::numScenes + s;
            if (isPositiveAndBelow (idx, lastSlotState.size()) && lastSlotState.getReference (idx) != state)
            {
                lastSlotState.set (idx, state);

                String label;
                if (slot != nullptr)
                    if (auto* clip = slot->getClip())
                        label = clip->getName();

                columns[t]->setSlotVisual (s, state, label);
            }

            // Beat pulse (W04a): pushed per tick ONLY for the animated states (playing / queued)
            // while a phase is flowing; every other pad is parked at the no-pulse sentinel — a
            // value that doesn't change, so the pad's change gate keeps it repaint-free (§e).
            // Pure maths on the already-computed state; no logging on this poll path.
            const float pulse = ! havePhase                          ? -1.0f
                              : state == SlotVisualState::playing    ? padPulseAlpha (state, beatPhase)
                              : state == SlotVisualState::queued     ? padPulseAlpha (state, twoBeatPhase)
                                                                     : -1.0f;
            columns[t]->setSlotPulse (s, pulse);
        }

        // Derive the scene row state from its slots: playing dominates queued dominates idle.
        const SceneLaunchState rowState = anyPlaying ? SceneLaunchState::playing
                                        : anyQueued  ? SceneLaunchState::queued
                                                     : SceneLaunchState::idle;

        if (scenes != nullptr
            && isPositiveAndBelow (s, lastSceneState.size())
            && lastSceneState.getReference (s) != rowState)
        {
            lastSceneState.set (s, rowState);
            scenes->setSceneState (s, rowState);
        }
    }
}
