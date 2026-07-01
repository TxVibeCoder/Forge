/*
    ProjectSession — owns the active project (a Tracktion Edit) and its file on disk.

    The single source of truth for "the open project". Wraps create / open / save with the
    real engine APIs and enforces the load-bearing ordering gotcha: a new Edit is written to
    disk *before* any clip is inserted, so clip source files serialize as relative paths.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class ProjectSession
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

    /** Imports an audio file as a clip on track 0 starting at `start`. Returns the clip. */
    te::WaveAudioClip::Ptr importAudioFile (const juce::File&, te::TimePosition start);

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

    // The single active record slot (W7). {-1,-1} == none. Set by recordArmSlot, cleared by
    // commitSlotRecord / recordDisarmSlot. Tracks which cell beginSlotRecord may roll for.
    int activeRecordTrack = -1;
    int activeRecordScene = -1;

    // Markers: linear-scan getMarkers() for the clip whose itemID matches (getMarkerByID matches on
    // the reassignable NUMBER, not the stable itemID). The raw ptr is safe — the marker track owns it.
    te::MarkerClip* findMarkerById (te::EditItemID id) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSession)
};
