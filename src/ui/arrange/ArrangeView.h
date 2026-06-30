/*
    ArrangeView — the arrangement surface: a vertical stack of track lanes, each showing its
    clips as rectangles with waveform thumbnails, plus a moving playhead overlay.

    Coordinate model: a shared TimelineView maps edit-time <-> pixels across the clip area
    (everything right of the fixed-width track header). Manual rebuild() is called by the
    owner after import / new / open (no ValueTree-listener storms in Phase 1).

    Phase 2 polish adds: a bars|beats ruler across the top of the clip area, per-lane
    mute/solo/arm controls + colour swatch, clip/track selection with accent outlines,
    and right-click context menus for structural edits. Optional std::function callbacks
    let the shell drive an Inspector and persist structural mutations.

    Snap: clip drag-to-move snaps to a selectable grid (SnapDivision: off / bar / 1\2 / 1\4 /
    1\8 / 1\16) chosen from a ComboBox in the headerW x rulerH corner box at the top-left
    (above the lane headers, left of the ruler). snapToGrid() converts via the TempoSequence;
    bar keeps the original logic, sub-bar divisions snap to beat-fraction grid lines. Holding
    Ctrl/Cmd during a drag bypasses snap. setSnapEnabled/isSnapEnabled remain as thin wrappers
    over the division model for the existing toolbar seam.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
namespace ArrangeLayout
{
    constexpr int headerW = 150;   // track header strip width
    constexpr int laneH   = 76;    // track lane height
    constexpr int gap     = 4;     // vertical gap between lanes
    constexpr int rulerH  = 22;    // bars|beats ruler strip height (top of clip area)
    constexpr int hintH   = 20;    // one-line info/help strip across the very bottom
}

//==============================================================================
/** Linear edit-time <-> pixel mapping over a visible time window. */
struct TimelineView
{
    te::TimePosition viewStart { te::TimePosition::fromSeconds (0.0) };
    te::TimePosition viewEnd   { te::TimePosition::fromSeconds (60.0) };

    int timeToX (te::TimePosition t, int width) const
    {
        const double span = (viewEnd - viewStart).inSeconds();
        if (width <= 0 || span <= 0.0)
            return 0;

        return juce::roundToInt ((t - viewStart).inSeconds() / span * (double) width);
    }

    te::TimePosition xToTime (int x, int width) const
    {
        const double span = (viewEnd - viewStart).inSeconds();
        if (width <= 0 || span <= 0.0)
            return viewStart;

        return viewStart + te::TimeDuration::fromSeconds (span * (double) x / (double) width);
    }
};

//==============================================================================
/** Bars|beats ruler strip drawn across the clip area (right of the track headers). */
class TimeRulerComponent : public juce::Component
{
public:
    explicit TimeRulerComponent (TimelineView&);

    void setEdit (te::Edit*);

    /** Number of sub-beat ticks to draw between whole beats (0 = beats only). Set by ArrangeView
        from the active snap division so finer grids show subdivision marks. Stored as a plain int
        to keep the ruler decoupled from ArrangeView's SnapDivision enum. */
    void setSubBeatTicks (int ticksPerBeat);

    void paint (juce::Graphics&) override;

private:
    TimelineView& view;
    te::Edit* edit = nullptr;
    int subBeatTicks = 0;     // 0 = none, 1 = 1/8 (one tick per beat half), 3 = 1/16, etc.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeRulerComponent)
};

//==============================================================================
/** One clip rectangle: the type-agnostic base owning selection, drag-to-move and the shared
    callbacks. Subclasses override paint() to draw their type-specific body (waveform / notes). */
class ClipComponent : public juce::Component
{
public:
    ClipComponent (TimelineView&, te::Clip&);

    /** Draws the shared chrome: clip-colour fill, border, name fallback, selection outline.
        Subclasses override and draw their body on top of (or instead of) this. */
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    te::Clip& getClip() { return clip; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const { return selected; }

    /** Invoked on left-click (clip selection). */
    std::function<void (ClipComponent&)> onClicked;
    /** Invoked on right-click; param is the screen-space event for menu placement. */
    std::function<void (ClipComponent&, const juce::MouseEvent&)> onRightClicked;

    /** Invoked at the START of a horizontal drag-to-move (after the move threshold is crossed),
        so the owner can select this clip and surface the drag hint. */
    std::function<void (ClipComponent&)> onDragStarted;
    /** Invoked once on mouseUp after a drag committed a new start time to the engine clip; the
        owner persists (onEditMutated) and re-lays-out. Not fired for a plain click. */
    std::function<void (ClipComponent&)> onDragCommitted;
    /** Maps a candidate clip-start time to a snapped time. Set by the owner; returns the input
        unchanged when snapping is disabled. Only consulted when the drag is NOT bypassing snap.
        Returns a time >= 0 (the owner clamps); never round-trips a negative time. */
    std::function<te::TimePosition (te::TimePosition)> snapStartTime;

protected:
    /** Width in px of the clip area this clip is positioned within (parent width minus the track
        header). Falls back to the parent width when no parent is attached. */
    int clipAreaWidth() const;

    TimelineView& view;
    te::Clip& clip;
    bool selected = false;

private:
    // Drag-to-move state. dragging stays false until the pointer moves past a small threshold so a
    // plain click never nudges the clip. dragOriginStart is the clip's start at mouseDown; the live
    // bounds are recomputed from it + the pixel delta each mouseDrag.
    bool dragging = false;
    int  dragAnchorX = 0;                                  // mouseDown x in PARENT (lane) space (stable as we move)
    te::TimePosition dragOriginStart {};                  // clip start captured at mouseDown

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipComponent)
};

//==============================================================================
/** A wave clip: owns a SmartThumbnail and draws the waveform over the shared chrome. */
class AudioClipComponent : public ClipComponent
{
public:
    AudioClipComponent (TimelineView&, te::Clip&);

    void paint (juce::Graphics&) override;

private:
    std::unique_ptr<te::SmartThumbnail> thumbnail;

    void drawWaveformWindow (juce::Graphics&, juce::Rectangle<int> area,
                             double sourceStartSecs, double sourceLenSecs, double speed);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipComponent)
};

//==============================================================================
/** A MIDI clip: draws the shared chrome with a distinct tint plus a mini note-preview, so a
    te::MidiClip reads visibly differently from a wave clip. */
class MidiClipComponent : public ClipComponent
{
public:
    MidiClipComponent (TimelineView&, te::Clip&);

    void paint (juce::Graphics&) override;

private:
    // Fixed pitch window for the note-preview y-scale; keeps the plot cheap and robust regardless
    // of the actual notes' range (notes outside the window are simply clamped/skipped).
    static constexpr int pitchLo = 36;
    static constexpr int pitchHi = 96;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClipComponent)
};

//==============================================================================
/** One audio-track lane: a header strip (name + M/S/R + colour swatch) + clip rects. */
class TrackLaneComponent : public juce::Component
{
public:
    TrackLaneComponent (TimelineView&, te::AudioTrack&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void rebuildClips();
    int  getNumClipComponents() const { return clipComps.size(); }

    te::AudioTrack& getTrack() { return track; }

    /** Highlights/clears the selected outline on a clip owned by this lane. */
    void setSelectedClip (te::Clip* clip);
    /** Sets the track-selected visual highlight on the header. */
    void setTrackSelected (bool shouldBeSelected);

    void refreshControlStates();

    //==============================================================================
    // Callbacks bubbled up to ArrangeView (set during rebuild()).
    std::function<void (ClipComponent&)> onClipClicked;
    std::function<void (ClipComponent&, const juce::MouseEvent&)> onClipRightClicked;
    /** A clip on this lane began a drag-to-move. */
    std::function<void (ClipComponent&)> onClipDragStarted;
    /** A clip on this lane committed a new start time (drag finished). */
    std::function<void (ClipComponent&)> onClipDragCommitted;
    /** Right-click on the empty clip area of this lane (not a clip, not the header), with the
        clip-area-relative x and the area width so the owner can offer a "New MIDI Clip" affordance
        at the clicked time. */
    std::function<void (TrackLaneComponent&, int clipAreaX, int clipAreaWidth, const juce::MouseEvent&)> onClipAreaRightClicked;
    /** Snap a candidate clip-start time (set by ArrangeView; identity when snap is off). */
    std::function<te::TimePosition (te::TimePosition)> snapStartTime;
    std::function<void (TrackLaneComponent&)> onHeaderClicked;
    /** Left-click on the empty clip area of this lane (not a clip, not the header). */
    std::function<void (TrackLaneComponent&)> onLaneAreaClicked;
    /** Left-click on the empty clip area, with the clip-area-relative x in px and the area width, so
        the owner can scrub the transport there. Lets the lanes sit ABOVE the playhead overlay (so
        clips are clickable/draggable) while empty-area clicks still move the playhead. */
    std::function<void (int clipAreaX, int clipAreaWidth)> onLaneAreaScrub;
    std::function<void (TrackLaneComponent&, const juce::MouseEvent&)> onHeaderRightClicked;
    /** Reflects/toggles the visual arm state; real input arming wired by the record path. */
    std::function<void (te::AudioTrack&, bool)> onArmToggled;
    /** Authoritative arm state for this track, queried from the engine on every refresh so the
        R button never relies on a stale local flag (set by ArrangeView during rebuild). */
    std::function<bool (te::AudioTrack&)> queryArmed;
    /** Invoked after a control (mute/solo/colour) mutates the Edit, so the shell can save. */
    std::function<void()> onEditMutated;

private:
    TimelineView& view;
    te::AudioTrack& track;
    juce::OwnedArray<ClipComponent> clipComps;

    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" };
    bool armed = false;
    bool trackSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackLaneComponent)
};

//==============================================================================
/** Moving playhead overlay; polls the transport on a ~30Hz timer and supports scrub. */
class PlayheadComponent : public juce::Component,
                          private juce::Timer
{
public:
    PlayheadComponent (TimelineView&, te::TransportControl&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    /** Only the thin band around the playhead line grabs the mouse, so clicks/drags elsewhere in
        the clip area fall through to the clips (drag-to-move) and lanes (click-to-scrub) beneath.
        Without this the full-width overlay shadowed every clip and clips could never be dragged. */
    bool hitTest (int x, int y) override;

    int getCurrentX() const { return lastX; }

private:
    void timerCallback() override;
    void scrubTo (const juce::MouseEvent&);

    TimelineView& view;
    te::TransportControl& transport;
    int lastX = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayheadComponent)
};

//==============================================================================
class ArrangeView : public juce::Component
{
public:
    /** Snap granularity for clip drag-to-move. `off` disables snapping entirely; `bar` is the
        legacy default (snap to whole bars). The remaining values snap to musical sub-bar grid
        lines derived from the local bar's beats-per-bar. Numeric values are stable (they back the
        ComboBox item IDs as id = (int) value + 1), so do not reorder existing members. */
    enum class SnapDivision { off, bar, half, quarter, eighth, sixteenth };

    explicit ArrangeView (TimelineView&);

    /** Binds the view to an Edit (or nullptr) and rebuilds. */
    void setEdit (te::Edit*);

    /** Re-enumerates tracks/clips and rebuilds the lanes + playhead. */
    void rebuild();

    void resized() override;
    void paint (juce::Graphics&) override;

    int getNumClipComponentsOnTrack0() const;
    int getPlayheadX() const { return playhead != nullptr ? playhead->getCurrentX() : -1; }

    /** Enables/disables snapping for clip drag-to-move. Default ON. Implemented on top of the
        snap-division model: enabling restores the last non-off division (bar if none), disabling
        sets SnapDivision::off. The shell can wire this to a toolbar toggle (see devlog); holding
        Ctrl/Cmd during a drag always bypasses snap regardless. */
    void setSnapEnabled (bool shouldSnap);
    bool isSnapEnabled() const { return division != SnapDivision::off; }

    /** Sets the active snap division live (updates the in-surface selector and the drag-snap grid).
        SnapDivision::off disables snapping; any other value enables it. Default is SnapDivision::bar,
        which preserves the original bar-snap behaviour. */
    void setSnapDivision (SnapDivision newDivision);
    SnapDivision getSnapDivision() const { return division; }

    /** Re-derives every lane's control state (incl. the R arm indicator) from the engine via the
        queryArmed callback. Call after any arm/disarm so a stolen single input clears the other
        lane's indicator, and after rebuild() so arm survives structural edits. */
    void refreshArmStates();

    //==============================================================================
    // Optional callbacks the shell can set to drive Inspector / persistence.
    // Default null => no-op. Selection state itself is owned here.
    std::function<void (te::Clip*)>  onClipSelected;
    std::function<void (te::Track*)> onTrackSelected;
    /** Invoked after any structural Edit mutation (add/delete/rename track, delete clip). */
    std::function<void()> onEditMutated;
    /** Reflects/toggles a lane's visual arm state; real input arming wired by record path. */
    std::function<void (te::AudioTrack&, bool)> onArmToggled;
    /** Authoritative per-track arm query (engine truth), set by the shell. Used to re-derive every
        lane's R indicator on rebuild() and after each arm/disarm. */
    std::function<bool (te::AudioTrack&)> isTrackArmed;
    /** Optional: invoked after the snap division changes (selector or setSnapDivision), so the
        shell can mirror the state elsewhere. Snap is self-contained here; this is purely additive. */
    std::function<void (SnapDivision)> onSnapDivisionChanged;
    /** Invoked when the user requests a new (empty) MIDI clip: from the clip-area or header context
        menu. trackIndex is the index of the lane's track within te::getAudioTracks(*edit);
        startTime is the (snapped) clip start. The shell binds this to ProjectSession::createMidiClip
        and then rebuild()s. Default null => no-op. */
    std::function<void (int trackIndex, te::TimePosition startTime)> onCreateMidiClipRequested;

private:
    void selectClip (ClipComponent*);
    void selectTrack (TrackLaneComponent*);
    void clearSelection();

    void showClipContextMenu (ClipComponent&, const juce::MouseEvent&);
    void showLaneContextMenu (TrackLaneComponent&, const juce::MouseEvent&);
    /** Shows the empty-clip-area context menu (a "New MIDI Clip" affordance at the clicked time). */
    void showClipAreaContextMenu (TrackLaneComponent& lane, int clipAreaX, int clipAreaW,
                                  const juce::MouseEvent&);

    void renameClip (te::Clip&);
    void renameTrack (te::AudioTrack&);
    void deleteClip (te::Clip&);
    void addTrack (te::AudioTrack* after);
    void deleteTrack (te::AudioTrack&);

    void notifyEditMutated();

    /** Snaps a candidate clip-start time to the nearest grid line for the active SnapDivision via
        edit->tempoSequence. Returns the input unchanged when the division is off or there is no Edit.
        For SnapDivision::bar this matches the original bar-snap logic exactly; sub-bar divisions snap
        to the nearest beat-fraction grid line derived from the local bar's beats-per-bar. Clamps to
        >= 0 and never round-trips a negative time (which can trip engine asserts). */
    te::TimePosition snapToGrid (te::TimePosition) const;

    /** Legacy entry point retained for source/binary stability: forwards to snapToGrid. */
    te::TimePosition snapToBar (te::TimePosition) const;

    /** Grid step in engine beats for a sub-bar division, given the local time signature's numerator
        (beats per bar) and denominator (which scales musical note divisions onto the engine-beat
        grid). Returns 0 for off/bar (handled separately by snapToGrid). */
    static double gridStepInBeats (SnapDivision, int numerator, int denominator);

    /** Builds/positions the in-surface snap-division selector ComboBox in the top-left corner box. */
    void buildSnapSelector();

    /** Sets the one-line hint text and repaints the hint strip. */
    void setHint (const juce::String&);

    TimelineView& view;
    te::Edit* edit = nullptr;
    juce::OwnedArray<TrackLaneComponent> lanes;
    std::unique_ptr<TimeRulerComponent> ruler;
    std::unique_ptr<PlayheadComponent> playhead;

    te::Clip* selectedClip = nullptr;
    te::Track* selectedTrack = nullptr;

    SnapDivision division = SnapDivision::bar;     // snap granularity; bar preserves legacy default
    SnapDivision lastEnabledDivision = SnapDivision::bar;  // restored by setSnapEnabled(true)
    juce::ComboBox snapSelector;                   // top-left corner-box division picker
    juce::String hintText;                         // current one-line hint shown in the bottom strip

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
