/*
    ProjectSession — owns the active project (a Tracktion Edit) and its file on disk.

    The single source of truth for "the open project". Wraps create / open / save with the
    real engine APIs and enforces the load-bearing ordering gotcha: a new Edit is written to
    disk *before* any clip is inserted, so clip source files serialize as relative paths.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>
#include <map>
#include <utility>
#include <vector>

namespace te = tracktion;

/** Per-clip launch mode (Wave 1). Trigger = today's one-shot launch; Gate = plays while the pad is held and
    stops on release; Toggle = each click toggles launch/stop. Stored as an int on the clip's ValueTree under
    "forgeLaunchMode"; ABSENCE of the property reads as Trigger (Trigger == 0), so every pre-Wave-1 edit and
    selftest gate is unchanged. Repeat/retrigger is deferred to v2. */
enum class LaunchMode { Trigger = 0, Gate = 1, Toggle = 2 };

class ProjectSession : private juce::Timer
{
public:
    explicit ProjectSession (te::Engine&);

    /** Opens editFile if it exists (else creates a new project there).
        Returns true if an existing project was loaded, false if a new one was created. */
    bool openOrCreate (const juce::File& editFile);

    /** Creates a fresh empty 1-track project at editFile and writes it to disk immediately. */
    void newProject (const juce::File& editFile);

    /** Loads an existing .tracktionedit. Returns false (and leaves the current edit) on failure. */
    bool openProject (const juce::File& editFile);

    /** Persists the current edit to its file. */
    bool save();

    /** Persists the current edit to a new file and adopts it as the current file. */
    bool saveAs (const juce::File&);

    /** Imports an audio file as a clip on track `trackIndex` (default 0) starting at `start`.
        Returns the clip. Used by the browser double-click (track 0) and the Arrange timeline
        file-drop (the dropped lane's index). getOrInsertAudioTrackAt grows the track list if the
        index is past the end, so a drop onto an existing lane always targets that lane. */
    te::WaveAudioClip::Ptr importAudioFile (const juce::File&, te::TimePosition start, int trackIndex = 0);

    /** Creates an empty MIDI clip spanning `range` (in SECONDS) on the track at `trackIndex`,
        ensuring the track exists and is born audible (a default 4OSC instrument is added if the
        track has none). Returns the new clip, or {} if there is no open edit / the insert failed. */
    te::MidiClip::Ptr createMidiClip (int trackIndex, te::TimeRange range, const juce::String& name);

    //==============================================================================
    // SessionView clip-launch grid seam (tracks x scenes on Scene / ClipSlot / LaunchHandle).
    //
    // All message-thread only. These are the ONLY path the SessionView grid uses to touch the
    // engine: the view never makes raw te:: calls and never caches a ClipSlot*/Clip* across a
    // poll/repaint (it re-resolves via the const getClipSlot every tick — see R1/R2). LaunchHandle
    // play/queue state is *read* for display via the engine's message-readable getters; we never
    // call LaunchHandle::advance() (audio-thread only, R5).

    /** Grows the scene/slot grid to at least n scenes (rows). ONLY grows — early-returns when
        getNumScenes() >= n, so existing scenes/clips are preserved and legacy 0-scene edits are
        safe. Runs OFF the user undo stack (the grow can't be Ctrl-Z'd away) and does NOT
        markAsChanged for a pure grow-to-default (R3). Newly-created, still-unnamed scenes are
        seeded with the sheet-00 default names (Intro, Verse A, Pre, Chorus, Verse B, Bridge,
        Drop, Outro) for the first 8 rows; a user-renamed scene is never overwritten. No-op when
        there is no open edit. */
    void ensureScenes (int n);

    /** Number of scenes (grid rows) in the current edit, 0 if no edit. */
    int  getNumScenes() const;

    // ── Scene lifecycle (W15) ─────────────────────────────────────────────────────────────────
    // rename / delete / reorder a scene row. UNLIKE grow-only ensureScenes (R3, off-stack), all
    // three ride the Edit UndoManager so one Ctrl-Z reverts one gesture — the shell owns the
    // per-gesture transaction boundary (onEditMutated seals; these seams never beginNewTransaction
    // themselves). Scenes are resolved fresh via edit->getSceneList().getScenes()[index]; NO
    // te::Scene* is ever cached (R1). Message-thread only; null-edit / out-of-range paths WARN.

    /** Renames scene `index`. A blank name persists (the scene row falls back to its 1-based
        number). No-op + WARN on no edit / out-of-range index. Undoable (te::Scene::name is bound
        to the Edit UndoManager). Does NOT markAsChanged — the shell seals + saves via onEditMutated. */
    void setSceneName (int index, const juce::String& newName);

    /** The name of scene `index`, or "" if unset / out-of-range / no edit. Const, non-mutating. */
    juce::String getSceneName (int index) const;

    /** Deletes scene `index`: the SCENE row AND every audio track's slot at that index (with any
        clip in it) — all on the Edit UndoManager (one transaction ⇒ one Ctrl-Z restores scene +
        slots + clips). The engine's SceneList::deleteScene handles the per-track slot removal.
        No-op + WARN (returns false) on no edit / out-of-range index. Returns true on delete. */
    bool deleteScene (int index);

    /** Reorders scene `from` to index `to`, moving the SCENES tree AND every track's CLIPSLOTS tree
        in lockstep (the desync guard — a scene row and its per-track slots must move together), on
        the Edit UndoManager. No engine moveScene seam exists — this is a raw ValueTree::moveChild on
        each list's public state (both are ValueTreeObjectLists that auto-resync on a child move).
        No-op + WARN (returns false) on no edit / equal indices / either index out-of-range.
        Returns true on move. */
    bool moveScene (int from, int to);

    /** CONST, NON-MUTATING resolve of the ClipSlot at cell (trackIndex, sceneIndex). Walks
        te::getAudioTracks(*edit), bounds-checks sceneIndex against the track's slot count, and
        returns nullptr past the end (or for an out-of-range track / no edit). MUST be the only
        resolve path used by the 25 Hz poll and paint: it NEVER inserts a track or a slot (R2),
        so it is safe to call at 25 Hz. The returned pointer is live only for the calling tick —
        never store it (R1). */
    te::ClipSlot* getClipSlot (int trackIndex, int sceneIndex) const;

    /** True if cell (trackIndex, sceneIndex) holds a clip. Const, non-mutating. */
    bool isSlotFilled (int trackIndex, int sceneIndex) const;

    /** Creates an empty, born-audible MIDI clip in the slot at (trackIndex, sceneIndex). Ensures
        the track exists and the grid has enough scenes, inserts a default ~4-bar / 16-beat MIDI
        clip into the slot (free te::insertMIDIClip(owner, name, range) — name before range),
        ensures the owning track has a default 4OSC instrument (born audible), and markAsChanged.
        Returns the new clip, or {} if there is no edit / the slot could not be resolved. */
    te::MidiClip::Ptr createMidiClipInSlot (int trackIndex, int sceneIndex, const juce::String& name);

    /** Imports `file` as a wave clip into the slot at (trackIndex, sceneIndex), replacing any
        existing clip in that slot. Guards portability: ensures the edit file exists on disk
        (calls save() first if not) so the source serialises relative (Sf), then markAsChanged.
        Returns the new clip, or {} on failure / no edit. */
    te::WaveAudioClip::Ptr importAudioIntoSlot (int trackIndex, int sceneIndex, const juce::File& file);

    /** Removes the clip in cell (trackIndex, sceneIndex): stops a live/queued launch first (via the
        clip's LaunchHandle so nothing dangles), then detaches the clip with te::Clip::removeFromParent().
        The removal routes through the Edit's UndoManager — the same one W05 global Undo/Redo runs over —
        so callers should fire onEditMutated() after, letting the shell seal the transaction + refresh.
        markAsChanged. Returns false (no-op) if there is no edit, the slot can't be resolved, or the slot
        is already empty. */
    bool clearSlot (int trackIndex, int sceneIndex);

    /** DUPLICATE the clip in slot (trackIndex, srcScene) into the first EMPTY slot BELOW it on the SAME
        track — auto-growing one new row (ensureScenes + ensureNumberOfSlots, OFF the user undo stack) when
        none is empty below. A COPY (source stays filled); the clone carries the source's launcher metadata
        (follow-action / launch-mode / launch-Q) and the engine re-imposes slot normalization. Only the clip
        insert rides the UndoManager, so one Ctrl+Z removes just the duplicate; fire onEditMutated() + a grid
        rebuild after. Returns the destination scene index, or -1 (logged) on no edit / empty source / failed
        insert. */
    int duplicateSlotClip (int trackIndex, int srcScene);

    /** COPY the clip in (srcTrack, srcScene) into (dstTrack, dstScene), materialising the destination slot
        on demand. A FILLED destination is replaced (the engine auto-removes the existing clip; removal + add
        both ride the UndoManager). The source is untouched. Returns false (logged, no-op) on no edit / empty
        source / src==dst / an unresolved-or-failed insert. */
    bool copySlotClip (int srcTrack, int srcScene, int dstTrack, int dstScene);

    /** MOVE = copySlotClip(...) THEN clearSlot(srcTrack, srcScene) with NO beginNewTransaction between, so
        both halves land in ONE undo transaction (one Ctrl+Z reverses the whole move). The copy runs first;
        the source is cleared only after it lands (a failed copy leaves the source intact). Returns true on
        success. */
    bool moveSlotClip (int srcTrack, int srcScene, int dstTrack, int dstScene);

    /** Copies the clip in slot (trackIndex, sceneIndex) onto the SAME track's LINEAR (Arrange)
        timeline — the explicit, one-directional "Send to Arrangement" bridge (W5). The copy is
        APPENDED after that track's existing arrange content (track->getTotalRange().getEnd(), == 0
        for an empty lane), so repeated sends never overlap and build an arrangement left-to-right.
        Targets the clip's OWN track, so it keeps its instrument / mixer routing.

        This is a COPY, never a move: we clone the source clip's ValueTree state (which carries the
        wave source / loop / gain / fades OR the MIDI note sequence) and hand it to the multi-arg
        insertClipWithState, which re-IDs it and stamps the append position in one call — the engine's
        own duplication idiom (mirrors te::split). te::Clip::moveTo was REJECTED: it re-parents the
        live clip and would EMPTY the source slot. Session and Arrange stay separate; nothing
        auto-mirrors (a locked product decision) — this runs only on explicit user action.

        The copy is normalized to a plain linear one-shot — the slot's inherited auto-tempo + full-length
        loop range are cleared, so a later edge-drag reveals source instead of re-tiling — and the target
        track is switched to arrange playback (playSlotClips=false) so the copy is actually AUDIBLE (the
        engine gates arranger output on !playSlotClips, and a track that has launched a slot latches the
        flag true with nothing clearing it). That flip is the engine's Session->Arrange handoff: it stops
        any still-playing slot on that track.

        The insert routes through the Edit's UndoManager (the same stack as W05 global Undo), so the
        caller should seal the transaction and fire the Arrange refresh (arrangeView.rebuild() —
        ArrangeView has no clip-add listener) afterwards. markAsChanged on success. Returns the new
        arrange clip, or nullptr (logged) if there is no edit / the slot is empty / the track can't be
        resolved / the insert failed. */
    te::Clip* sendSlotToArrangement (int trackIndex, int sceneIndex);

    //==============================================================================
    // Performance capture (Wave 7) — the REAL Session -> Arrange bridge. Records which clips launched,
    // WHEN, and for how long during a live performance, and stamps them onto each track's linear timeline
    // at their absolute captured Edit beat. Unlike sendSlotToArrangement (a single static clip appended at
    // the end), this preserves the TIMING of a performance.
    //
    // Mechanism (load-bearing): LaunchHandle::getPlayedRange() is a SINGLE current span (Edit beats,
    // audio-write / message-read), not a history buffer — it reads [startBeat, startBeat+duration] while a
    // clip plays and nullopt once it stops. So capture SAMPLES-AND-ACCUMULATES on the message thread: an
    // owned ~30 Hz Timer polls every filled slot each tick, opening a span on a fresh play, growing it, and
    // SEALING it on each launch->stop transition (or when the launch startBeat jumps = a re-launch between
    // ticks — a changed start means a new play GIVEN Forge only ever calls LaunchHandle::play(), never
    // nudge()/setLooping()/playSynced(), none of which Forge wires today; any of those would also mutate
    // startBeat mid-play and this heuristic would need revisiting alongside them — QC-flagged, not a live bug).
    // R1-safe: cells are (track,scene) INDICES, re-resolved via the const getClipSlot each tick; NO
    // te::LaunchHandle*/Clip* is ever cached ACROSS ticks. Each span also carries the te::EditItemID of the
    // clip that was ACTUALLY PLAYING when the span opened (captured then, never re-derived later) — commit
    // resolves the source clip by that IDENTITY (te::findClipForID), never by re-reading "whatever occupies
    // this cell now", so clearing/replacing a clip in a cell mid-capture-session can't stamp the replacement
    // clip's content at the original clip's captured beat (QC-caught + fixed). Message-thread only.

    /** Arms performance capture: clears prior history and starts the message-thread sampler. Does NOT
        start the transport — the user launches clips/scenes as usual and capture accumulates whatever
        plays while armed. Idempotent. No-op if no edit. */
    void startPerformanceCapture();

    /** Disarms capture. Seals any still-open spans first. When `commit`, STAMPS one one-shot clip per
        captured span onto its track's linear timeline at the span's ABSOLUTE captured Edit beat (via the
        same clone/normalize path sendSlotToArrangement uses). All inserts land with NO intervening
        beginNewTransaction, so the CALLER brackets them into one undo transaction (the shell does — one
        Ctrl+Z removes the whole take). markAsChanged on a non-empty commit. Returns the number of clips
        stamped (0 if nothing captured, !commit, or no edit). */
    int  stopPerformanceCapture (bool commit);

    /** True while performance capture is armed. Const. */
    bool isPerformanceCaptureArmed() const;

    /** Count of captured spans so far (sealed + currently open). Const — for the gate + a HUD readout. */
    int  getCapturedSpanCount() const;

    /** Samples every filled slot's LaunchHandle::getPlayedRange ONCE and accumulates. Called by the owned
        Timer in production; exposed so a headless gate can pump it deterministically in lockstep with the
        audio graph (blockUntilSyncPointChange). No-op if not armed / no edit. Message-thread only; R1-safe;
        never logs (poll path). */
    void performanceCaptureTick();

    /** Appends a new EMPTY audio track at the END of the track list (te::insertNewAudioTrack at
        getEndOfTracks). LOAD-BEARING: the end-append keeps every existing absolute track index stable
        (mixer sends + Session-grid slot addressing depend on it — same invariant as ensureAuxBus).
        Names it when `name` is non-empty, markAsChanged, then fires onTracksChanged so the shell
        rebuilds track-ref-caching views (SessionView columns / Arrange lanes / channel tray) and
        persists. NO instrument is added — createMidiClipInSlot inserts the default 4OSC lazily when a
        MIDI clip is first born, so a fresh audio track only gains a synth if it actually receives one
        (pre-adding one would wrongly make every +Track a MIDI track). Returns the new track, or nullptr
        on failure (logs). */
    te::AudioTrack* appendAudioTrack (const juce::String& name = {});

    /** Queues the clip in cell (trackIndex, sceneIndex) to launch, with per-track exclusivity
        (sibling clips on the same track are stopped). Honours Edit::getLaunchQuantisation() when
        the transport is running (queues to the next quantise boundary); when stopped it starts
        the transport so the clip is AUDIBLE and launches immediately. No-op if the slot is empty
        / has no launch handle. */
    void launchSlot (int trackIndex, int sceneIndex);

    /** Stops (queues to stop) the clip in cell (trackIndex, sceneIndex). No-op if empty. */
    void stopSlot (int trackIndex, int sceneIndex);

    /** Stops all clips on the track at trackIndex (the per-track clip-stop ■). */
    void stopTrackClips (int trackIndex);

    /** Stops every launched clip in the edit (master "stop all"). */
    void stopAllSlots();

    /** Launches scene `sceneIndex` across all audio tracks: for each track, launch the clip in
        that scene's slot and stop the track's other launched clips. Starts the transport so the
        scene is audible. App logic (no engine "launch scene" call) — mirrors the demo. */
    void launchScene (int sceneIndex);

    /** Sets the Edit-global launch quantisation (free-trigger selector, W7 P#1.3). A single
        Edit-level setting (te::Edit::getLaunchQuantisation().type) — governs every slot/scene
        launch that doesn't override it per-clip. LaunchQType::none == free trigger (no snap).
        No-op if there is no open edit. */
    void setGlobalLaunchQuantisation (te::LaunchQType t);

    /** Current Edit-global launch quantisation, or te::LaunchQType::bar (the engine default) if
        there is no open edit. Const, non-mutating. */
    te::LaunchQType getGlobalLaunchQuantisation() const;

    //==============================================================================
    // Launcher expressiveness seam (Wave 1). Message-thread only; each op resolves the clip via the const
    // getClipSlot(...)->getClip() (never cached, R1) and FORGE_LOG_WARNs a null-edit / empty-slot path.

    /** Sets clip (trackIndex, sceneIndex)'s single follow action — what the launcher does after the clip has
        played for its follow-action duration (chain to the next clip, stop, replay, etc.). Keeps EXACTLY one
        Action and sets its type EXPLICITLY, which defeats the engine footgun where writing a follow-action
        duration on an empty action list auto-plants a currentGroupRoundRobin action. markAsChanged. No-op
        (logged) if the slot has no clip. */
    void setFollowAction (int trackIndex, int sceneIndex, te::FollowAction action);

    /** The clip's current follow action, or FollowAction::none if unset / no clip. CONST, NON-MUTATING:
        guards on the FOLLOWACTIONS child existing before touching getFollowActions() (which would lazily
        grow the tree), so a pure read never dirties the edit. */
    te::FollowAction getFollowAction (int trackIndex, int sceneIndex) const;

    /** Sets WHEN the follow action fires: after `amount` loops (FollowActionDurationType::loops) or `amount`
        beats (::beats). Ensures an Action exists first (so the duration write doesn't auto-plant a default
        action) but does NOT set the action type — the UI always pairs this with setFollowAction, whose
        explicit type-set follows and wins. "after N loops" is honoured only while the clip isLooping().
        markAsChanged. No-op (logged) if the slot has no clip. */
    void setFollowActionDuration (int trackIndex, int sceneIndex,
                                  te::Clip::FollowActionDurationType type, double amount);

    /** Toggles clip (trackIndex, sceneIndex) between looping and one-shot. Looping sets a REAL full-clip loop
        range [0, getLengthInBeats()] (auto-tempo follows for audio clips — the beat-locked loop); one-shot
        calls disableLooping(). NEVER setLoopRangeBeats({}) (an empty range re-asserts auto-tempo — the W5/W10
        gotcha). markAsChanged. No-op (logged) if the slot has no clip. */
    void setSlotClipLooping (int trackIndex, int sceneIndex, bool shouldLoop);

    /** True if clip (trackIndex, sceneIndex) is looping. Const, non-mutating. */
    bool isSlotClipLooping (int trackIndex, int sceneIndex) const;

    /** Sets clip (trackIndex, sceneIndex)'s launch mode (Trigger / Gate / Toggle) — stored as an int on the
        clip's ValueTree ("forgeLaunchMode"), undoable. markAsChanged. No-op (logged) if the slot has no clip. */
    void setLaunchMode (int trackIndex, int sceneIndex, LaunchMode mode);

    /** The clip's launch mode, or LaunchMode::Trigger if unset / no clip (absence reads as Trigger, so every
        pre-Wave-1 clip is Trigger). Const, non-mutating. */
    LaunchMode getLaunchMode (int trackIndex, int sceneIndex) const;

    /** True if clip (trackIndex, sceneIndex) is ACTIVE — currently playing OR queued-to-play. Const,
        non-mutating (reads the message-safe getPlayingStatus / getQueuedStatus; never advance(), R5). Drives
        Toggle launch-mode: a click stops an active-or-pending clip, so a click during the launch-quantise
        pre-roll toggles the pending launch OFF rather than re-queuing it. */
    bool isSlotActive (int trackIndex, int sceneIndex) const;

    /** Sets clip (trackIndex, sceneIndex)'s per-clip launch quantisation, ACTIVATING the override:
        calls setUsesGlobalLaunchQuatisation(false) (engine typo verbatim; the flag is INVERTED — false
        enables the clip's own Q) THEN writes the type. The resolver gates on the flag first, so BOTH are
        required. markAsChanged. No-op (logged) if the slot has no clip. */
    void setClipLaunchQuantisation (int trackIndex, int sceneIndex, te::LaunchQType t);

    /** The clip's OWN launch-quantisation type while it overrides; the GLOBAL type when the clip inherits
        (or no clip / no edit). Pair with clipInheritsGlobalLaunchQuantisation() to tell which. Const. */
    te::LaunchQType getClipLaunchQuantisation (int trackIndex, int sceneIndex) const;

    /** Reverts clip (trackIndex, sceneIndex) to the Edit-global launch quantisation
        (setUsesGlobalLaunchQuatisation(true)); the stored clip type is left intact for a later re-enable.
        markAsChanged. No-op (logged) if the slot has no clip. */
    void clearClipLaunchQuantisation (int trackIndex, int sceneIndex);

    /** True if the clip inherits the Edit-global launch quantisation (NO per-clip override active). True
        for no clip / no edit (the engine default). Const, non-mutating (a pure in-memory flag read). */
    bool clipInheritsGlobalLaunchQuantisation (int trackIndex, int sceneIndex) const;

    /** The launch-Q type the REAL launch path would use for slot (trackIndex, sceneIndex): delegates into
        the same file-local getLaunchQuantisation(te::Clip&) resolver launchSlot feeds, so --selftest-session
        can assert override precedence through the resolver, not a mirror. Const. */
    te::LaunchQType resolveEffectiveLaunchQType (int trackIndex, int sceneIndex) const;

    //==============================================================================
    // Session MIDI-record seam (W7). Message-thread only.
    //
    // ProjectSession keeps NO hard RecordController dependency: the orchestrator wires the three
    // std::function hooks below to the recorder's slot-arm / slot-disarm / is-slot-armed calls in
    // main.cpp (each matches a frozen RecordController signature — §1a). ProjectSession orchestrates
    // the born-audible + arm recipe on top of them so callers never touch raw engine record APIs.
    //
    // Signatures mirror RecordController::armFirstMidiInputToSlot / disarmSlot / isSlotMidiArmed
    // (te::Edit&, te::ClipSlot&) — see design §1a/§1b. Left empty they are safe no-ops (arm returns
    // false, is-armed returns false) so an un-wired ProjectSession never records but never crashes.
    std::function<bool (te::Edit&, te::ClipSlot&)> recorderArmSlot;
    std::function<bool (te::Edit&, te::ClipSlot&)> recorderDisarmSlot;
    std::function<bool (te::Edit&, te::ClipSlot&)> recorderIsSlotArmed;

    /** Arm cell (trackIndex, sceneIndex) for MIDI slot recording (VERDICT A). Idempotent. Orchestrates
        the born-audible + arm recipe so callers never touch raw engine APIs: ensureScenes(sceneIndex+1);
        ensure the track exists; PluginHost::ensureDefaultInstrument(track) so the captured clip is
        born-audible (4OSC); resolve the ClipSlot (getClipSlot); hand off to the injected recorder
        arm-slot seam (RecordController::armFirstMidiInputToSlot). Does NOT pre-insert a clip — the
        engine creates it at commit. Returns true iff the slot ends armed; sets no clip. */
    bool recordArmSlot (int trackIndex, int sceneIndex);

    /** Disarms cell (trackIndex, sceneIndex). Wraps the injected recorder disarm-slot seam. Stops the
        transport FIRST if it is recording (removeTarget fails while recording). */
    void recordDisarmSlot (int trackIndex, int sceneIndex);

    /** True iff cell (trackIndex, sceneIndex) is armed for MIDI record (re-derived from engine targets
        each call via the injected is-slot-armed seam — no cached flag). Feeds SlotVisualState::recArmed
        in the 25 Hz poll. Pure read — never logs. */
    bool isSlotRecordArmed (int trackIndex, int sceneIndex) const;

    /** Begins capture: ensures the track has a default instrument, then rolls the transport with
        transport.record(false). Records because the slot's destination is recordEnabled — NOT because
        the slot is launched (there is no clip to launch yet). No-op if no slot is currently the active
        armed record slot. */
    void beginSlotRecord (int trackIndex, int sceneIndex);

    /** Ends capture: transport.stop(false,false); the engine commits the captured notes into a new
        MidiClip in the slot via addMidiAsTransaction on stop. Then disarms the slot. Returns the
        resulting clip if one was captured (getClipSlot(...)->getClip() dyn_cast to MidiClip), else {}. */
    te::MidiClip::Ptr commitSlotRecord (int trackIndex, int sceneIndex);

    /** True iff cell (trackIndex, sceneIndex) is the slot currently CAPTURING: it is the armed record
        target (injected is-slot-armed seam) AND edit.getTransport().isRecording(). Pure engine read —
        NOT "LaunchHandle playing" (which never becomes true for an empty capturing slot). Drives the
        recording pad. Never logs (poll path). */
    bool isSlotRecording (int trackIndex, int sceneIndex) const;

    //==============================================================================
    // Aux buses / sends seam (mixer buses/sends — P3). Message-thread only.
    //
    // An aux bus is modelled the Tracktion way: a plain te::AudioTrack hosting an AuxReturnPlugin
    // (there is no separate "bus track" type). A per-track AuxSendPlugin with a matching busNumber
    // feeds it. These seams are the ONLY path MixerView uses to touch bus/send structure — the view
    // makes no raw te:: bus calls. busIdx is 0-based (0="A", 1="B"); trackIndex is the ABSOLUTE index
    // into te::getAudioTracks(*edit) (the same index MixerView + the Session grid address tracks by).

    /** Ensures aux bus `busIdx` exists, appending a new aux-return AudioTrack at the END of the track
        list (an AuxReturnPlugin at its chain head) and seeding a friendly name. Grow-only / idempotent.
        LOAD-BEARING: the return is appended AFTER every existing track so absolute track indices stay
        stable (mixer sends + Session-grid slot addressing depend on it). On a real add it fires
        onTracksChanged so views that cache track refs rebuild + the project persists. Returns the
        return track, or nullptr (no edit / creation failed — logs on failure). */
    te::AudioTrack* ensureAuxBus (int busIdx);

    /** The aux-return AudioTrack backing `busIdx` (hosts an AuxReturnPlugin with that busNumber), or
        nullptr if not provisioned / no edit. Const, non-mutating. */
    te::AudioTrack* getAuxReturnTrack (int busIdx) const;

    /** True if `track` hosts an AuxReturnPlugin (so the mixer renders it as a return, not a strip). */
    bool isAuxReturnTrack (const te::AudioTrack& track) const;

    /** Display name for `busIdx`: edit.getAuxBusName(busIdx) (may be ""). Const. */
    juce::String getAuxBusName (int busIdx) const;

    /** Sets the post-fader send level (dB) from track `trackIndex` to bus `busIdx`, ensuring the bus
        exists (ensureAuxBus) then find-or-creating an AuxSendPlugin(busNumber=busIdx) on the track and
        clamping to [-100, +6] dB. No-op + log on failure. */
    void setTrackSendLevel (int trackIndex, int busIdx, float gainDb);

    /** Current send level (dB) from `trackIndex` to `busIdx`, or a silence value (<= -100) if the
        track has no send for that bus. Const. */
    float getTrackSendLevel (int trackIndex, int busIdx) const;

    /** Fired (message thread) when a seam changes the TRACK LIST — currently only ensureAuxBus, which
        appends an aux-return AudioTrack. The shell wires this to rebuild views that cache track refs
        (SessionView columns / ArrangeView lanes) so a stale column never derefs a track, and to persist.
        Fires only when a track was actually added (grow-only). */
    std::function<void()> onTracksChanged;

    //==============================================================================
    // Markers / cue points seam (P5). Message-thread only.
    //
    // A marker is a te::MarkerClip on the Edit's marker track. The stable key is te::EditItemID
    // (EditItem::itemID — const, edit-wide-unique), NOT the marker NUMBER (getMarkerID(), which the
    // engine reassigns on duplicate-resolution). Callers cache only value rows keyed on EditItemID.

    struct Marker { te::EditItemID id; te::TimePosition time; juce::String name; };

    /** Adds a marker at `time` (auto-assigned unique number; `name` overrides the default "Marker N"
        when non-empty). Returns its stable EditItemID, or {} on failure (no edit / no marker track). */
    te::EditItemID       addMarker    (te::TimePosition time, const juce::String& name);

    /** Removes the marker with stable id `id`. Returns false if no edit / id not found. */
    bool                 removeMarker (te::EditItemID id);

    /** Moves marker `id` to `newTime` (engine clamps to [0, maxEditEnd]). False if no edit / not found. */
    bool                 moveMarker   (te::EditItemID id, te::TimePosition newTime);

    /** Renames marker `id`. False if no edit / not found. */
    bool                 renameMarker (te::EditItemID id, const juce::String& newName);

    /** All markers as value rows {id, time, name}, time-sorted (as getMarkers() returns them). Pure
        read — allocates engine-side each call, so NEVER call from a poll/paint. */
    std::vector<Marker>  getMarkers   () const;

    /** Immediately locates the transport to `time` (marker jump). No-op if no transport. */
    void                 jumpTransportTo (te::TimePosition time);

    te::Edit*             getEdit() const      { return edit.get(); }
    te::TransportControl* getTransport() const;
    juce::File            getEditFile() const  { return editFile; }
    int                   getNumAudioTracks() const;

    /** True if the edit has unsaved changes. Backed by te::Edit::hasChangedSinceSaved();
        returns false when there is no open edit. Use this to enable a Save action and to
        decide whether to prompt before closing. */
    bool                  isModified() const;

    /** Number of clips on the first audio track (track 0). Returns 0 if there is no edit
        or no track 0. Safe, read-only convenience accessor. */
    int                   getNumClipsOnTrack0() const;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    juce::File editFile;

    // ── Performance capture (Wave 7) ──────────────────────────────────────────────────────────────
    // Owned message-thread sampler + the accumulation state. timerCallback() delegates to
    // performanceCaptureTick(). Keyed by (track,scene); an OpenSpan is the currently-growing play, sealed
    // into capturedSpans (Edit-beat ranges) on a stop / re-launch. NO engine pointers are cached (R1).
    void timerCallback() override;

    // clipID is captured at OPEN time from the clip actually playing then (NOT re-resolved at seal/commit
    // time) — a clip can be cleared and replaced in the same cell mid-capture-session; resolving "whatever
    // occupies this cell now" at commit would stamp the REPLACEMENT clip's content at the ORIGINAL span's
    // captured beat (QC-caught). commit resolves the clip by IDENTITY (te::findClipForID), never by cell
    // index, so a since-moved-or-untouched clip is found correctly and a since-DELETED clip degrades to a
    // logged skip (same graceful-degrade posture as an empty slot).
    struct CaptureSpan { int track, scene; te::BeatRange range; te::EditItemID clipID; };
    struct OpenSpan    { double startBeat, endBeat; te::EditItemID clipID; };   // currently-growing, Edit beats

    bool capturing = false;
    std::map<std::pair<int,int>, OpenSpan> openSpans;   // per active cell
    std::vector<CaptureSpan>               capturedSpans;

    void sealCaptureSpan (int track, int scene, const OpenSpan&);   // push a CaptureSpan (guards tiny/neg len)

    // Clone helper factored out of sendSlotToArrangement (BEHAVIOUR-PRESERVING — the existing
    // --selftest-sendarrange stays byte-identical): clones `src`'s state onto `track` at `destPos`,
    // normalizes to a one-shot (unless keepAsLoop), flips playSlotClips false (audible in Arrange), and
    // strips launcher-only metadata (follow-action / launch-mode). Returns the new clip, or nullptr on a
    // failed insert. Does NOT markAsChanged (callers do). Message-thread only.
    te::Clip* insertClipCopyOnTimeline (te::AudioTrack& track, te::Clip& src,
                                        te::ClipPosition destPos, bool keepAsLoop);

    // The single active record slot (W7). {-1,-1} == none. Set by recordArmSlot, cleared by
    // commitSlotRecord / recordDisarmSlot. Tracks which cell beginSlotRecord may roll for.
    int activeRecordTrack = -1;
    int activeRecordScene = -1;

    // Markers: linear-scan getMarkers() for the clip whose itemID matches (getMarkerByID matches on
    // the reassignable NUMBER, not the stable itemID). The raw ptr is safe — the marker track owns it.
    te::MarkerClip* findMarkerById (te::EditItemID id) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSession)
};
