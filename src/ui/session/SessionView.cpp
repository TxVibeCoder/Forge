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
    // ONE scroll seam drives both pinned surfaces: the scene column follows the VERTICAL offset, the
    // mixer band follows the HORIZONTAL offset (twins, rotated 90 deg — both ride visibleAreaChanged).
    viewport.onScroll = [this] { syncSceneColumnToScroll(); syncMixerBandToScroll(); };
    addAndMakeVisible (viewport);

    // W08 mixer band: a fixed bottom strip (direct child of SessionView, sibling of the viewport) that
    // clips its scrolling holder. mixerHolder is the contentW-wide row of strips, translated in x to
    // mirror the horizontal pad scroll. addChildComponent (mixerBand)/addAndMakeVisible ordering keeps
    // the band under nothing else (it never overlaps the viewport — resized() carves disjoint bounds).
    mixerBand.addAndMakeVisible (mixerHolder);
    addAndMakeVisible (mixerBand);

    setWantsKeyboardFocus (true);
}

SessionView::~SessionView()
{
    // R4 teardown order, mirrored here: stop the poll FIRST, then drop children, then clear state.
    // The mixer strips carry their OWN ~12 Hz timers; clearing the array destroys each strip, and
    // ~SessionMixerStrip stops its timer FIRST — so no strip tick can land during teardown either.
    stopTimer();
    columns.clear();
    mixerStrips.clear();
    addTrackColumn.reset();
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
        // R4 — STRICT teardown order; NO engine read afterward. Mixer strips (each with its own
        // timer, stopped first in ~SessionMixerStrip) are cleared alongside the columns.
        stopTimer();
        columns.clear();
        mixerStrips.clear();
        addTrackColumn.reset();
        scenes.reset();
        focusTrack = focusScene = 0;
        edit = nullptr;
    }
}

//==============================================================================
void SessionView::rebuild()
{
    columns.clear();
    mixerStrips.clear();
    addTrackColumn.reset();
    scenes.reset();

    lastSlotState.clear();
    lastSceneState.clear();

    lastLoggedTrackCount = -1;   // fresh column set: re-arm the one-shot track-count-mismatch WARN gate

    if (edit != nullptr)
    {
        // RUNTIME scene count (W07 +Scene): honour any live count above the SessionLayout floor.
        // Computed ONCE here and threaded through every FIXED-BOUND site below (pad ctor, scene
        // column, diff buffers, focus clamp) AND the 25 Hz poll (loop bound + flat index stride),
        // so the four coupled sites can never drift out of lock-step.
        gridScenes = jmax (SessionLayout::numScenes, session.getNumScenes());

        int trackIndex = 0;

        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* column = columns.add (new TrackColumnComponent (*track, trackIndex, gridScenes));
            wireColumn (*column);
            columnHolder.addAndMakeVisible (column);

            // W08: one compact mixer strip per column, bound to the SAME absolute track index the
            // column + pads use (R1 — the strip caches only (edit, index) and re-resolves live). A
            // return renders in-place with a subtle tint (INV-4: no grid filtering — filtering would
            // break ~9 absolute-index sites incl. the hot poll). isAuxReturnTrack is a localized,
            // additive cosmetic read on the message thread here in rebuild(), never in a poll.
            auto* strip = mixerStrips.add (new SessionMixerStrip());
            strip->setIsReturn (session.isAuxReturnTrack (*track));
            strip->setTrack (edit, trackIndex);
            mixerHolder.addAndMakeVisible (strip);

            ++trackIndex;
        }

        // W07 +Track: trailing "+" stub column after the last real column, inside columnHolder so it
        // scrolls with the grid. Click -> appendAudioTrack(); onTracksChanged (shell-wired) rebuilds.
        addTrackColumn = std::make_unique<AddTrackColumnComponent>();
        addTrackColumn->onAddTrack = [this] { addTrack(); };
        columnHolder.addAndMakeVisible (*addTrackColumn);

        // Pinned scene column. Scene names are read as PLAIN VALUES for display (R1) — the
        // SceneColumnComponent never caches a te::Scene*.
        StringArray sceneNames;
        for (auto* scene : edit->getSceneList().getScenes())
            sceneNames.add (scene != nullptr ? scene->name.get() : String());

        scenes = std::make_unique<SceneColumnComponent>();
        scenes->setScenes (sceneNames, gridScenes);
        scenes->onAddScene = [this] { addScene(); };   // W07 +Scene affordance (bottom of the scene column)
        wireScenes();
        addAndMakeVisible (*scenes);

        // Per-pad / per-scene state diff buffers, primed to a value the first poll will overwrite.
        // Sized by the RUNTIME gridScenes so the poll's flat stride (t * gridScenes + s) matches.
        lastSlotState.insertMultiple (0, SlotVisualState::empty, columns.size() * gridScenes);
        lastSceneState.insertMultiple (0, SceneLaunchState::idle, gridScenes);

        focusTrack = jlimit (0, jmax (0, columns.size() - 1), focusTrack);
        focusScene = jlimit (0, gridScenes - 1, focusScene);
    }

    viewport.setViewPosition (0, 0);   // new/rebuilt edit always starts at the top
    resized();
    syncSceneColumnToScroll();          // re-glue the fresh scene column to the (now-reset) scroll offset
    syncMixerBandToScroll();            // re-glue the fresh mixer band to the (now-reset) H-scroll offset
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
    column.onSlotReleased      = [this] (int t, int s)                    { handleSlotReleased (t, s); };
    column.onSlotDoubleClicked = [this] (int t, int s)                    { handleSlotDoubleClicked (t, s); };
    column.onSlotRightClicked  = [this] (int t, int s, const MouseEvent& e) { handleSlotRightClicked (t, s, e); };
    column.onSlotFilesDropped  = [this] (int t, int s, const File& file)   { handleSlotFilesDropped (t, s, file); };

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

    // W15 scene lifecycle — rename / delete / reorder. Each routes through a ProjectSession seam (all
    // undoable, unlike grow-only +Scene) then afterSceneMutation() to seal + rebuild. Move up/down map
    // to moveScene(s, s∓1); the row disables Move-up on index 0 and Move-down on the last row, so the
    // s-1 / s+1 targets are always in range when these fire.
    scenes->onSceneRenamed   = [this] (int s, const juce::String& n) { session.setSceneName (s, n); afterSceneMutation(); };
    scenes->onSceneDeleted   = [this] (int s) { if (session.deleteScene (s))     afterSceneMutation(); };
    scenes->onSceneMovedUp   = [this] (int s) { if (session.moveScene (s, s - 1)) afterSceneMutation(); };
    scenes->onSceneMovedDown = [this] (int s) { if (session.moveScene (s, s + 1)) afterSceneMutation(); };

    // Wave 7 fast-follow: "Send scene to Arrangement" needs arrangeView.rebuild(), which SessionView
    // can't reach — forward to the shell via SessionView's own public seam (same discipline as the
    // single-slot onSendToArrangement below).
    scenes->onSceneSentToArrangement = [this] (int s) { if (onSceneSentToArrangement != nullptr) onSceneSentToArrangement (s); };
}

void SessionView::afterSceneMutation()
{
    // Seal the W05 undo transaction + save synchronously (mirrors the slot-mutation gestures — the
    // seam already wrote through the Edit UndoManager, so this makes the gesture Ctrl+Z-reversible).
    if (onEditMutated != nullptr)
        onEditMutated();

    // Defer the grid rebuild (see the header note): a rename commit fires from inside the scene row's
    // TextEditor callback, and rebuild() destroys that row + editor — deleting a component whose method
    // is live on the stack is a UAF. One message-loop hop lets the callback unwind first; the
    // SafePointer guards against the view dying in the interim.
    juce::Component::SafePointer<SessionView> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr)
            safe->rebuild();
    });
}

//==============================================================================
void SessionView::resized()
{
    auto area = getLocalBounds();

    // Reserve the pinned scene column on the right (OUTSIDE the scrolling viewport, twin of MasterStrip).
    // Taken from the FULL-height area FIRST, so the scene column still runs the full window height (the
    // mixer band tucks under the tracks only — INV-1 default; no scene-column change).
    auto sceneArea = area.removeFromRight (SessionLayout::sceneColW);

    // W08: reserve the fixed mixer band along the BOTTOM of the (non-scene) area, BEFORE sizing the
    // viewport. The viewport therefore shrinks by mixerBandH — but contentH below is UNCHANGED, so the
    // pad content height and the scene-column height stay EQUAL (the anti-drift invariant). A shorter
    // viewport only means the grid starts scrolling at a smaller track count; rowBand inputs are
    // untouched, so no new drift (the W07 scene-drift class is not reintroduced).
    auto mixerArea = area.removeFromBottom (SessionLayout::mixerBandH);
    mixerBand.setBounds (mixerArea);

    viewport.setBounds (area);

    const int nTracks  = columns.size();
    const int columnH  = SessionLayout::headerH
                       + gridScenes * SessionLayout::slotH
                       + SessionLayout::stopRowH;                       // runtime height: 78 + N*46 + 30
                                                                         // (>16 scenes -> taller content, scrolls)

    // W07 +Track: the trailing "+" stub occupies its own narrow lane after the last track column.
    const int addColW = (addTrackColumn != nullptr) ? SessionLayout::addTrackColW : 0;

    const int tracksW  = nTracks * SessionLayout::trackColW + addColW;   // real columns + the "+" stub
    const int contentW = jmax (viewport.getMaximumVisibleWidth(), tracksW);
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

    // The "+" stub immediately follows the last real column (flush, like the columns themselves),
    // full content height so its "+" glyph can centre on the grid's vertical span.
    if (addTrackColumn != nullptr)
        addTrackColumn->setBounds (x, 0, SessionLayout::addTrackColW, contentH);

    // W08 mixer band: the holder is contentW wide (the SAME width as columnHolder, so strip N shares
    // column N's x-pitch exactly) and mixerBandH tall. It lives INSIDE mixerBand (a fixed clip region
    // sized to the bottom strip above), so strips clip at the band's left/right edges. syncMixerBand-
    // ToScroll() then translates the holder by -viewport.getViewPositionX() to keep strips glued under
    // their columns while scrolling horizontally (the twin of the scene column's vertical translation).
    mixerHolder.setSize (contentW, SessionLayout::mixerBandH);
    {
        int mx = 0;
        for (auto* strip : mixerStrips)
        {
            strip->setBounds (mx, 0, SessionLayout::trackColW, SessionLayout::mixerBandH);
            mx += SessionLayout::trackColW + SessionLayout::gap;
        }
    }
    syncMixerBandToScroll();

    // The scene column MUST use the SAME content height as the track columns so its shared rowBand
    // partition matches theirs EXACTLY. rowBand divides `height` into N rows by INTEGER floor, so any
    // height difference changes the per-row pitch and scene launch row N drifts progressively from pad
    // row N (e.g. 46 vs 45 px/row at N=20 -> ~19 px at the bottom row). It is translated up by the
    // vertical scroll offset so scene launch row N stays glued to pad row N while scrolling.
    // (An earlier version subtracted the H-scrollbar thickness here to line the bottom stop band up with
    // the H-bar-occluded track footers, but that broke rowBand's equal-height invariant — the far worse
    // bug. The minor bottom-edge overhang when scrolled fully down under an H-scrollbar is accepted.)
    if (scenes != nullptr)
        scenes->setBounds (sceneArea.getX(),
                           -viewport.getViewPositionY(),
                           sceneArea.getWidth(),
                           contentH);
}

void SessionView::syncSceneColumnToScroll()
{
    // Message-thread only (called from the viewport's visibleAreaChanged). Cheap: one move.
    if (scenes != nullptr)
        scenes->setTopLeftPosition (scenes->getX(), -viewport.getViewPositionY());
}

void SessionView::syncMixerBandToScroll()
{
    // The horizontal twin of syncSceneColumnToScroll: translate the mixer holder LEFT by the pad grid's
    // horizontal scroll offset so strip N stays under column N. The band is OUTSIDE the viewport, so the
    // vertical offset never touches it — vertical pad-scroll cannot move the band (requirement satisfied
    // by construction). Message-thread only (viewport.onScroll); cheap: one move.
    mixerHolder.setTopLeftPosition (-viewport.getViewPositionX(), 0);
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
        launchOrToggle (trackIdx, sceneIdx);   // mode-aware (W1); Trigger stays the byte-identical launch path
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

void SessionView::handleSlotReleased (int trackIdx, int sceneIdx)
{
    // Gate launch-mode: the clip plays only while the pad is held — stop it on release. A no-op for
    // Trigger / Toggle and for empty slots, so the proven click path is unaffected.
    if (session.isSlotFilled (trackIdx, sceneIdx)
        && session.getLaunchMode (trackIdx, sceneIdx) == LaunchMode::Gate)
        session.stopSlot (trackIdx, sceneIdx);
}

void SessionView::launchOrToggle (int trackIdx, int sceneIdx)
{
    // Launch-mode routing (W1), shared by the mouse click and keyboard Enter. Toggle stops an ACTIVE
    // (playing OR queued-to-play) clip, else launches it — so a click during the launch-quantise pre-roll
    // toggles the pending launch off. Trigger and Gate both launch (Gate additionally stops on mouse-up via
    // handleSlotReleased; the keyboard has no release, so Gate-via-Enter is a one-shot). Trigger is the
    // proven, byte-identical launch path (getLaunchMode short-circuits before isSlotActive is called).
    if (session.getLaunchMode (trackIdx, sceneIdx) == LaunchMode::Toggle
        && session.isSlotActive (trackIdx, sceneIdx))
        session.stopSlot (trackIdx, sceneIdx);
    else
        session.launchSlot (trackIdx, sceneIdx);
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

    // New ids come AFTER the existing set so they never collide with {idLaunch=1..idSendArrange}.
    enum { idLaunch = 1, idStop, idEdit, idImport, idRecordSlot, idStopRecord, idDelete, idSendArrange };
    // W1 launcher-expressiveness ids in distinct high ranges (follow-action is a contiguous range indexing
    // kFollowActions; loop + the launch modes are single ids). No overlap with the small set above.
    enum { idFollowBase = 100, idFollowRandomV2 = 160, idLoopToggle = 200,
           idModeTrigger = 300, idModeGate, idModeToggle,
           idLaunchQInherit = 400, idLaunchQBase = 401 };   // W2: 401.. = one per LaunchQType (enum order)
    enum { idDuplicate = 50, idMoveToNext, idSendArrangeLoop };   // W3/Wave-7 structural slot ops (9..99 gap)

    // The v1 follow-action vocabulary (deterministic set; trackAny/trackOther = "Random" deferred to v2). A
    // function-local static so the async dispatch lambda can index it by (result - idFollowBase).
    static const std::pair<te::FollowAction, const char*> kFollowActions[] =
    {
        { te::FollowAction::none,            "None" },
        { te::FollowAction::globalStop,      "Stop" },
        { te::FollowAction::globalPlayAgain, "Play again" },
        { te::FollowAction::trackNext,       "Next clip" },
        { te::FollowAction::trackPrevious,   "Previous clip" },
        { te::FollowAction::trackFirst,      "First clip" },
        { te::FollowAction::trackLast,       "Last clip" },
        { te::FollowAction::trackRoundRobin, "Round robin" },
    };

    // Re-derive record eligibility from engine truth on demand (never cached, R1).
    auto* track = getTrackAt (trackIdx);
    const bool trackMidiArmed = (track != nullptr && isTrackMidiArmed != nullptr && isTrackMidiArmed (*track));
    const bool recordingHere  = (isSlotRecording != nullptr && isSlotRecording (trackIdx, sceneIdx));

    if (filled)
    {
        menu.addItem (idLaunch, "Launch");
        menu.addItem (idStop,   "Stop");
        menu.addItem (idEdit,        "Edit clip");            // launch-free edit path (mirrors double-click, without launching)
        menu.addItem (idSendArrange, "Send to Arrangement");  // W5: copy this clip onto the track's Arrange timeline (one-directional)
        if (session.isSlotClipLooping (trackIdx, sceneIdx))   // Wave 7 fast-follow: only offered when the source IS looping
            menu.addItem (idSendArrangeLoop, "Send to Arrangement (as loop)");
        menu.addItem (idDelete,      "Delete clip");          // W07: empty the slot (filled-only); undoable via W05 global Undo
        menu.addItem (idDuplicate,   "Duplicate clip");       // W3: copy to the first empty slot below (auto-grow)
        menu.addItem (idMoveToNext,  "Move to next slot");    // W3: move to the first empty slot below (auto-grow)

        // W1 launcher expressiveness: per-clip follow action / loop / launch mode.
        PopupMenu followMenu;
        const auto currentFA = session.getFollowAction (trackIdx, sceneIdx);
        for (int i = 0; i < (int) juce::numElementsInArray (kFollowActions); ++i)
            followMenu.addItem (idFollowBase + i, kFollowActions[i].second, true,
                                kFollowActions[i].first == currentFA);
        followMenu.addSeparator();
        followMenu.addItem (idFollowRandomV2, "Random (v2)", false, false);   // trackAny/trackOther deferred
        menu.addSubMenu ("Follow action", followMenu);

        menu.addItem (idLoopToggle, "Loop", true, session.isSlotClipLooping (trackIdx, sceneIdx));

        PopupMenu modeMenu;
        const auto currentMode = session.getLaunchMode (trackIdx, sceneIdx);
        modeMenu.addItem (idModeTrigger, "Trigger", true, currentMode == LaunchMode::Trigger);
        modeMenu.addItem (idModeGate,    "Gate",    true, currentMode == LaunchMode::Gate);
        modeMenu.addItem (idModeToggle,  "Toggle",  true, currentMode == LaunchMode::Toggle);
        menu.addSubMenu ("Launch mode", modeMenu);

        // W2 per-clip launch-quantise: "Global (inherit)" + one item per LaunchQType. Labels come from the
        // SAME te::getLaunchQTypeChoices() the global TransportBar combo uses; the ticked item keys off the
        // real has-override test (clipInheritsGlobalLaunchQuantisation), not a value compare.
        PopupMenu launchQMenu;
        const auto launchQChoices  = te::getLaunchQTypeChoices();
        const bool inheritsGlobalQ = session.clipInheritsGlobalLaunchQuantisation (trackIdx, sceneIdx);
        const auto globalQ         = session.getGlobalLaunchQuantisation();
        const auto clipQ           = session.getClipLaunchQuantisation (trackIdx, sceneIdx);
        launchQMenu.addItem (idLaunchQInherit,
                             "Global (inherit - " + launchQChoices[(int) globalQ] + ")",
                             true, inheritsGlobalQ);
        launchQMenu.addSeparator();
        for (int i = 0; i < launchQChoices.size(); ++i)
            launchQMenu.addItem (idLaunchQBase + i, launchQChoices[i], true,
                                 ! inheritsGlobalQ && static_cast<te::LaunchQType> (i) == clipQ);
        menu.addSubMenu ("Launch quantise", launchQMenu);

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

                            // W1 follow-action submenu: a contiguous id range -> the kFollowActions table.
                            if (result >= idFollowBase
                                && result < idFollowBase + (int) juce::numElementsInArray (kFollowActions))
                            {
                                const auto fa = kFollowActions[result - idFollowBase].first;
                                safeThis->session.setFollowAction (trackIdx, sceneIdx, fa);
                                if (fa != te::FollowAction::none)   // default duration: after 1 loop
                                    safeThis->session.setFollowActionDuration (trackIdx, sceneIdx,
                                        te::Clip::FollowActionDurationType::loops, 1.0);
                                if (safeThis->onEditMutated != nullptr)
                                    safeThis->onEditMutated();
                                return;
                            }

                            // W2 per-clip launch-quantise: "Global (inherit)" reverts to global; a type item overrides.
                            if (result == idLaunchQInherit)
                            {
                                safeThis->session.clearClipLaunchQuantisation (trackIdx, sceneIdx);
                                if (safeThis->onEditMutated != nullptr)
                                    safeThis->onEditMutated();
                                return;
                            }
                            if (result >= idLaunchQBase
                                && result < idLaunchQBase + te::getLaunchQTypeChoices().size())
                            {
                                safeThis->session.setClipLaunchQuantisation (trackIdx, sceneIdx,
                                    static_cast<te::LaunchQType> (result - idLaunchQBase));
                                if (safeThis->onEditMutated != nullptr)
                                    safeThis->onEditMutated();
                                return;
                            }

                            switch (result)
                            {
                                case idLaunch: safeThis->session.launchSlot (trackIdx, sceneIdx); break;
                                case idStop:   safeThis->session.stopSlot   (trackIdx, sceneIdx); break;
                                case idEdit:   safeThis->openSlotForEdit    (trackIdx, sceneIdx); break;
                                case idImport: safeThis->importAudioInto    (trackIdx, sceneIdx); break;
                                case idDelete:
                                    // Empty the slot. clearSlot routes removeFromParent through the
                                    // Edit's UndoManager, so firing onEditMutated() (which seals the
                                    // W05 undo transaction) makes the delete Ctrl+Z-reversible; rebuild()
                                    // re-reads the now-empty slot. Mirrors the create/import refresh path.
                                    if (safeThis->session.clearSlot (trackIdx, sceneIdx))
                                    {
                                        if (safeThis->onEditMutated != nullptr)
                                            safeThis->onEditMutated();

                                        safeThis->rebuild();
                                    }
                                    break;
                                case idDuplicate:
                                    // W3: copy the clip to the first empty slot below (auto-grow a row if
                                    // none). Occupancy changes -> rebuild() (mirrors idDelete).
                                    if (safeThis->session.duplicateSlotClip (trackIdx, sceneIdx) >= 0)
                                    {
                                        if (safeThis->onEditMutated != nullptr)
                                            safeThis->onEditMutated();
                                        safeThis->rebuild();
                                    }
                                    break;
                                case idMoveToNext:
                                {
                                    // W3: MOVE = duplicate to the auto-grow target, then clear the source —
                                    // both in ONE undo transaction (single trailing onEditMutated seals; no
                                    // beginNewTransaction between the halves).
                                    const int dst = safeThis->session.duplicateSlotClip (trackIdx, sceneIdx);
                                    if (dst >= 0 && safeThis->session.clearSlot (trackIdx, sceneIdx))
                                    {
                                        if (safeThis->onEditMutated != nullptr)
                                            safeThis->onEditMutated();
                                        safeThis->rebuild();
                                    }
                                    break;
                                }
                                case idSendArrange:
                                case idSendArrangeLoop:
                                    // W5 (+ the Wave 7 as-loop fast-follow): hand off to the shell, which
                                    // owns the seam + the Arrange rebuild + the seal/save (the source slot
                                    // is untouched, so no grid rebuild).
                                    if (safeThis->onSendToArrangement != nullptr)
                                        safeThis->onSendToArrangement (trackIdx, sceneIdx, result == idSendArrangeLoop);
                                    break;
                                case idRecordSlot:
                                    if (safeThis->onSlotRecord != nullptr)
                                        safeThis->onSlotRecord (trackIdx, sceneIdx);
                                    break;
                                case idStopRecord:
                                    if (safeThis->onSlotRecordStop != nullptr)
                                        safeThis->onSlotRecordStop (trackIdx, sceneIdx);
                                    break;
                                case idLoopToggle:   // W1: toggle looping <-> one-shot
                                    safeThis->session.setSlotClipLooping (trackIdx, sceneIdx,
                                        ! safeThis->session.isSlotClipLooping (trackIdx, sceneIdx));
                                    if (safeThis->onEditMutated != nullptr)
                                        safeThis->onEditMutated();
                                    break;
                                case idModeTrigger:
                                case idModeGate:
                                case idModeToggle:   // W1: set the per-clip launch mode
                                    safeThis->session.setLaunchMode (trackIdx, sceneIdx,
                                        result == idModeGate   ? LaunchMode::Gate
                                      : result == idModeToggle ? LaunchMode::Toggle
                                                               : LaunchMode::Trigger);
                                    if (safeThis->onEditMutated != nullptr)
                                        safeThis->onEditMutated();
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

void SessionView::handleSlotFilesDropped (int trackIdx, int sceneIdx, const File& file)
{
    // W07 OS-external audio drop. importAudioIntoSlot is the ONE import path (identical to the
    // file-chooser import and the right-click "Import audio..." item), so drop and menu-import
    // behave identically. On success fire the create/import refresh callback (onEditMutated seals
    // the undo transaction + fans the shell refresh) and rebuild() to show the new clip.
    if (edit == nullptr)
        return;

    if (session.importAudioIntoSlot (trackIdx, sceneIdx, file))
    {
        if (onEditMutated != nullptr)
            onEditMutated();

        rebuild();
    }
    else
    {
        FORGE_LOG_ERROR ("Failed to import dropped audio into slot (" + juce::String (trackIdx) + "," + juce::String (sceneIdx) + ")");
    }
}

//==============================================================================
// Keyboard (§c): arrows move the focus cursor, Enter launches the focused slot, Shift+Enter the
// focused scene. All clamped to grid bounds.

bool SessionView::keyPressed (const KeyPress& key)
{
    if (edit == nullptr || columns.isEmpty())
        return false;

    // W07 Delete clip: Delete or Backspace empties the FOCUSED filled slot. clearSlot detaches the
    // clip through the Edit's UndoManager, and firing onEditMutated() seals the W05 undo transaction,
    // so the delete is Ctrl+Z-reversible (mirrors the right-click "Delete clip" + create/import path).
    // Consume the key even on an empty slot so it never falls through to an accidental handler.
    if (key.isKeyCode (KeyPress::deleteKey) || key.isKeyCode (KeyPress::backspaceKey))
    {
        if (session.isSlotFilled (focusTrack, focusScene)
            && session.clearSlot (focusTrack, focusScene))
        {
            if (onEditMutated != nullptr)
                onEditMutated();

            rebuild();
        }

        return true;
    }

    if (key.isKeyCode (KeyPress::leftKey))   { setFocus (focusTrack - 1, focusScene); return true; }
    if (key.isKeyCode (KeyPress::rightKey))  { setFocus (focusTrack + 1, focusScene); return true; }
    if (key.isKeyCode (KeyPress::upKey))     { setFocus (focusTrack, focusScene - 1); return true; }
    if (key.isKeyCode (KeyPress::downKey))   { setFocus (focusTrack, focusScene + 1); return true; }

    // W3 duplicate/move on the FOCUSED filled slot. Ctrl+D = MOVE to the first empty slot below (the baked
    // default); Ctrl+Shift+D = COPY (keep the source). Auto-grows a row when none is empty below; focus
    // follows the clip to its (possibly grown) row. getKeyCode() (not getTextCharacter) is reliable with a
    // modifier held; accept either case. Consume even on an empty slot.
    if (key.getModifiers().isCommandDown()
        && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd'))
    {
        if (session.isSlotFilled (focusTrack, focusScene))
        {
            const bool copy = key.getModifiers().isShiftDown();
            const int  dst  = session.duplicateSlotClip (focusTrack, focusScene);
            if (dst >= 0)
            {
                if (! copy)
                    session.clearSlot (focusTrack, focusScene);   // MOVE half — same transaction (one seal below)
                if (onEditMutated != nullptr)
                    onEditMutated();
                rebuild();
                setFocus (focusTrack, dst);
            }
        }
        return true;
    }

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
            if (focusScene < 0 || focusScene >= gridScenes)
                FORGE_LOG_ERROR ("Failed to launch scene " + juce::String (focusScene) + " via keyboard");

            session.launchScene (focusScene);
        }
        else
        {
            // Launching an empty focused slot is a silent no-op; surface the failed keyboard launch.
            if (! session.isSlotFilled (focusTrack, focusScene))
                FORGE_LOG_ERROR ("Failed to launch slot (" + juce::String (focusTrack) + "," + juce::String (focusScene) + ") via keyboard");

            launchOrToggle (focusTrack, focusScene);   // W1: Enter is mode-aware too (Toggle can toggle off)
        }

        return true;
    }

    return false;
}

//==============================================================================
void SessionView::addScene()
{
    if (edit == nullptr)
        return;

    // Grow the Edit by exactly one scene. ensureScenes is grow-only (R3) and deliberately OFF the
    // undo stack + does NOT markAsChanged, so we persist directly with save() and accept that
    // +Scene is NOT undoable (intended — a scene row is grid chrome, not an editable clip op).
    session.ensureScenes (session.getNumScenes() + 1);

    if (! session.save())
        FORGE_LOG_ERROR ("Failed to save project after +Scene");

    // rebuild() re-reads getNumScenes(), recomputes gridScenes, and recreates the columns + scene
    // rows + diff buffers at the new size (the poll then lights rows 16+). The 25 Hz poll only
    // watches TRACK count, never scene count, so a direct rebuild() is the required trigger.
    rebuild();
}

void SessionView::addTrack()
{
    if (edit == nullptr)
        return;

    // Append at the END (existing absolute track indices stay stable — Session/mixer addressing
    // depends on it). appendAudioTrack fires session.onTracksChanged, which the shell has wired to
    // persist + rebuild BOTH SessionView and ArrangeView — so we deliberately do NOT rebuild() here
    // (that would double-rebuild). No 4OSC is added: a synth arrives lazily only if a MIDI clip is
    // later born in one of the new track's slots (createMidiClipInSlot -> ensureDefaultInstrument).
    if (session.appendAudioTrack() == nullptr)
        FORGE_LOG_ERROR ("Failed to append audio track via +Track");
}

//==============================================================================
void SessionView::AddTrackColumnComponent::paint (Graphics& g)
{
    // NEUTRAL add-affordance (Fable charter): a muted "+" on the lane-background tone, brightening
    // on hover — never selection-amber (amber = selection only). Reads as "the next, empty track".
    auto b = getLocalBounds();

    g.setColour (Colour (SessionLayout::laneBg).withAlpha (hovered ? 1.0f : 0.6f));
    g.fillRect (b);

    // Left-edge hairline separates the stub from the last real column (columns sit flush).
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, 1, getHeight());

    // The "+" glyph, brightening from secondary to primary text on hover.
    g.setColour (Colour (hovered ? ForgeLookAndFeel::textPrim : ForgeLookAndFeel::textSec));
    g.setFont (Font (FontOptions (22.0f)));
    g.drawText ("+", b, Justification::centred);

    // A vertical "Track" hint under the glyph so the affordance is self-describing (legibility).
    g.setColour (Colour (ForgeLookAndFeel::textSec));
    g.setFont (Font (FontOptions (10.0f)));
    g.drawText ("Track", b.withTrimmedTop (b.getHeight() / 2 + 14).withHeight (14),
                Justification::centredTop, false);
}

void SessionView::setFocus (int trackIdx, int sceneIdx)
{
    if (columns.isEmpty())
        return;

    const int newTrack = jlimit (0, columns.size() - 1, trackIdx);
    const int newScene = jlimit (0, gridScenes - 1, sceneIdx);

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
// those animated pads (and scene rows, which pulse with the same phases) repaint per tick;
// everything else keeps the repaint-on-change behaviour.

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

    for (int s = 0; s < gridScenes; ++s)
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

            const int idx = t * gridScenes + s;
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

        if (scenes != nullptr && isPositiveAndBelow (s, lastSceneState.size()))
        {
            if (lastSceneState.getReference (s) != rowState)
            {
                lastSceneState.set (s, rowState);
                scenes->setSceneState (s, rowState);
            }

            // Beat-pulse parity with the pads (W04a): reuse the SAME per-tick phases and curve
            // (padPulseAlpha) for the launched/queued row's ring; every other row is parked at
            // the no-pulse sentinel, so its change gate keeps it repaint-free (§e).
            const float rowPulse = ! havePhase                             ? -1.0f
                                 : rowState == SceneLaunchState::playing   ? padPulseAlpha (SlotVisualState::playing, beatPhase)
                                 : rowState == SceneLaunchState::queued    ? padPulseAlpha (SlotVisualState::queued,  twoBeatPhase)
                                                                           : -1.0f;
            scenes->setScenePulse (s, rowPulse);
        }
    }
}
