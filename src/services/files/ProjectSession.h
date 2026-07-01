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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSession)
};
