/*
    SessionView — the top-level tracks×scenes clip-launch grid (Sheet 00, the new DEFAULT
    ViewMode). The backbone the rest of the SessionView feature hangs off, mirroring MixerView's
    Viewport + holder + OwnedArray<Column> + pinned-singleton skeleton and ArrangeView's
    rebuild() teardown/recreate + null-guarded std::function shell seams.

    Layout: a horizontally-scrolling viewport of TrackColumnComponent columns (one per audio
    track) inside columnHolder, with a SceneColumnComponent pinned to the right edge (outside the
    viewport) at SessionLayout::sceneColW. Each column carries a header, numScenes clip pads, and a
    clip-stop footer; the scene column carries the MASTER stop-all band and one launch row per scene.

    Threading & lifecycle (load-bearing — docs/devlog/session-design.md R1-R5):
      - R1: caches NO te::ClipSlot* / Clip* across the poll/repaint boundary. Pads hold
        (trackIndex, sceneIndex); selection/focus are INDICES. The 25 Hz poll re-resolves a live
        slot FRESH every tick via the const ProjectSession::getClipSlot and never stores it.
      - R2: getClipSlot (const, non-mutating) is the only resolve path used by the poll/paint;
        track-inserting helpers are never called from the poll.
      - R4: setEdit(nullptr) teardown order is STRICT — stopTimer() FIRST, then clear columns,
        reset the scene column, reset selection/focus indices, edit=nullptr, with NO engine read
        afterward. The same teardown runs in ~SessionView().
      - R5: every engine op goes through ProjectSession on the message thread; LaunchHandle state
        is only READ for display (never advance()).

    All structural mutation routes through ProjectSession; the view itself makes no raw te:: calls
    except reading scene names/track count for display on the message thread.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/session/TrackColumnComponent.h"
#include "ui/session/SceneColumnComponent.h"
#include "ui/session/SlotVisualState.h"
#include "ui/session/SessionLayout.h"
#include "services/files/ProjectSession.h"

namespace te = tracktion;

//==============================================================================
class SessionView : public juce::Component,
                    private juce::Timer
{
public:
    explicit SessionView (ProjectSession& session);
    ~SessionView() override;

    /** Binds the view to an Edit (or nullptr). On a non-null edit: stores it, grows the grid to
        SessionLayout::numScenes via session.ensureScenes (grow-only, off the undo stack, R3),
        rebuilds the columns + scene column, and starts the 25 Hz poll. On nullptr: strict R4
        teardown (stopTimer FIRST, then clear, then reset indices, then edit=nullptr). */
    void setEdit (te::Edit*);

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;   // grabs keyboard focus when the Session view is shown

    //==============================================================================
    // Public helpers the shell may call (mirror ArrangeView).

    /** Tears down and recreates every column + the scene column from the current Edit's track /
        scene list, re-wiring all upward callbacks. Call after any structural mutation. No-op when
        there is no bound edit. */
    void rebuild();

    /** Re-reads each column's header (incl. the engine-truth R arm indicator). Message-thread. */
    void refreshArmStates();

    /** Forces an immediate slot-state poll pass (the same work the 25 Hz timer does), pushing the
        current state of every pad and scene row. */
    void refreshSlotStates();

    /** Track-column count (excludes the scene column), for diagnostics / self-tests. */
    int getNumColumns() const                { return columns.size(); }

    /** Exposes the scrolling viewport so the headless screenshot harness can drive setViewPosition
        to prove vertical scroll. Message-thread only; UI-geometry accessor (does NOT let callers
        cache slots — R1 is preserved). */
    juce::Viewport& getViewport() { return viewport; }

    //==============================================================================
    // Shell seam callbacks (null-guarded; mirror ArrangeView). Default null => no-op.

    /** A filled MIDI slot was double-clicked: the resolved clip opens in the piano-roll drawer
        (identical to ArrangeView::onClipSelected). The clip is resolved on demand; never cached. */
    std::function<void (te::Clip*)> onSlotSelected;

    /** Invoked after any structural Edit mutation (create/import into a slot), so the shell saves. */
    std::function<void()> onEditMutated;

    /** Invoked when an empty MIDI-track slot is clicked and a new born-audible MIDI clip is
        created, so the shell can open it in the piano-roll drawer. */
    std::function<void (te::MidiClip::Ptr)> onMidiClipCreated;

    /** Authoritative per-track arm query (engine truth), set by the shell. Drives the R indicator
        and the rec-armed pad tint. */
    std::function<bool (te::AudioTrack&)> isTrackArmed;

    /** Reflects/toggles a track's arm state; real input arming wired by the record path. */
    std::function<void (te::AudioTrack&, bool)> onArmToggled;

private:
    void timerCallback() override;

    void wireColumn (TrackColumnComponent& column);
    void wireScenes();

    // Interaction dispatch (all engine ops via session).
    void handleSlotClicked       (int trackIdx, int sceneIdx);
    void handleSlotDoubleClicked (int trackIdx, int sceneIdx);
    void handleSlotRightClicked  (int trackIdx, int sceneIdx, const juce::MouseEvent&);
    void importAudioInto         (int trackIdx, int sceneIdx);
    void openSlotForEdit         (int trackIdx, int sceneIdx);   // route the slot's clip to the drawer

    void setFocus (int trackIdx, int sceneIdx);
    void repaintPad (int trackIdx, int sceneIdx);
    void syncSceneColumnToScroll();  // translate the pinned scene column by -viewport.getViewPositionY()

    // Resolves the AudioTrack at trackIdx fresh via te::getAudioTracks (R1; never cached). Null
    // for an out-of-range index / no edit. Message-thread only.
    te::AudioTrack* getTrackAt (int trackIdx) const;

    // True if the track at trackIdx hosts a synth / MIDI-input plugin (an instrument → MIDI track).
    bool trackIsMidi (int trackIdx) const;
    // Engine-truth arm query for a track index (false if unwired / out of range).
    bool trackArmed (int trackIdx) const;

    ProjectSession& session;
    te::Edit* edit = nullptr;                 // raw, non-owning (R1)

    // Viewport subclass whose only job is to surface visibleAreaChanged as an onScroll callback,
    // so SessionView can translate the pinned scene column to match the vertical scroll offset.
    struct ScrollingViewport : public juce::Viewport
    {
        std::function<void()> onScroll;
        void visibleAreaChanged (const juce::Rectangle<int>& newArea) override
        {
            juce::Viewport::visibleAreaChanged (newArea);
            if (onScroll) onScroll();
        }
    };

    ScrollingViewport viewport;
    juce::Component columnHolder;
    juce::OwnedArray<TrackColumnComponent> columns;
    std::unique_ptr<SceneColumnComponent> scenes;

    // Keyboard focus / selection cursor tracked as INDICES only — never a cached ClipSlot* / Clip*
    // (R1). The pad's `selected` flag (set via setSlotSelected) renders the highlight.
    int focusTrack = 0, focusScene = 0;

    // Per-pad last-pushed state, so the poll only repaints pads whose state actually changed (§e).
    // Sized columns × numScenes on rebuild(); indexed [trackIdx * numScenes + sceneIdx].
    juce::Array<SlotVisualState> lastSlotState;
    juce::Array<SceneLaunchState> lastSceneState;

    // Edge-trigger gate for the 25 Hz track-count-mismatch WARN: log only when the live count
    // differs from the last value we logged (reset to -1 in rebuild()), never on every poll tick.
    int lastLoggedTrackCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionView)
};
