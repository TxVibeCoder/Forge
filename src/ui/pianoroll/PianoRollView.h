/*
    PianoRollView — the drawer-hosted piano-roll editor for a single te::MidiClip, the MIDI
    analogue of the DetailView Inspector. When a MIDI clip is selected the shell calls
    setMidiClip(c); this binds the clip and rebuilds one MidiNoteComponent per note, laid out over
    a scrolling grid canvas. Drawing / moving / resizing / deleting a note pushes the change
    straight to the engine clip's MidiList and fires onEditMutated() so the shell persists the Edit.

    Coordinate model (W23 — independent, zoomable): the HORIZONTAL (time) axis is the roll's OWN
    linear beat->pixel scale (pxPerBeat) with a horizontal scroll offset (hOffsetBeats) — it is NO
    LONGER tied to the arrange TimelineView, so the user can zoom/scroll the roll to a single clip
    without disturbing the arrange surface. beat 0 == the clip start (content-relative, as the engine
    stores note beats). The VERTICAL (pitch) axis is 128 semitones at keyHeight px each (also
    zoomable), pitch 127 at the top, 0 at the bottom; because that is far taller than the drawer the
    grid lives inside a juce::Viewport and scrolls vertically. A left keybed gutter (pinned — the
    canvas never scrolls horizontally) carries octave labels. On bind the horizontal scale fits the
    clip and the Viewport auto-scrolls to centre its note range.

    Navigation: Ctrl+wheel zooms time, Ctrl+Shift+wheel zooms pitch, Shift+wheel pans time, plain
    wheel scrolls pitch (the Viewport) — the routing is decoded by the public handleWheel() seam so
    it is testable headlessly. A compact bottom nav strip carries the same zoom via buttons (time
    -/+, pitch -/+, Fit, Trim) plus a horizontal scrollbar. A moving playhead overlay (30 Hz, drawn
    in the timeTempo clock colour, mouse-transparent) sweeps across while the transport runs.

    Global shortcuts: keys the roll doesn't consume locally (Ctrl+Z/Y, save, etc.) are forwarded to
    the shell via onUnhandledKey — the same escape hatch PopoutWindow uses — so undo/redo and every
    other app-wide command fire from inside the roll (docked OR torn off) regardless of which
    sub-component happens to hold keyboard focus.

    Lifetime: the clip is held as a te::MidiClip::Ptr (the engine's reference-counted handle) so the
    editor never dereferences a clip deleted underneath it; on any structural change the note
    components are rebuilt so a raw te::MidiNote* is never dereferenced after removal.

    Editing (W6): a selection model drives multi-select (click / Shift- or Ctrl-click / marquee),
    Delete, multi-move (drag one selected note -> moves the whole set), copy/paste (Ctrl/Cmd+C/V),
    and a velocity lane docked in a fixed strip at the bottom (outside the Viewport). The selection
    is tracked as raw te::MidiNote* keyed to their components; it is dropped/rebuilt on any
    structural change just like the components, so a selected pointer never outlives its note.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/pianoroll/VelocityLane.h"

namespace te = tracktion;

class MidiNoteComponent;

//==============================================================================
namespace PianoRollLayout
{
    constexpr int gutterW      = 28;    // left keybed strip width (excluded from the time axis)
    constexpr int numKeys      = 128;   // MIDI pitches 0..127

    constexpr int defaultKeyH  = 12;    // px per semitone row at 1x pitch zoom
    constexpr int minKeyH      = 5;     // pitch-zoom clamps
    constexpr int maxKeyH      = 34;

    constexpr double defaultPxPerBeat = 40.0;   // horizontal scale at bind before the fit
    constexpr double minPxPerBeat     = 4.0;    // time-zoom clamps
    constexpr double maxPxPerBeat     = 800.0;

    constexpr int navStripH    = 20;    // bottom nav strip (zoom buttons + horizontal scrollbar)
}

//==============================================================================
class PianoRollView : public juce::Component,
                      private juce::ScrollBar::Listener
{
public:
    PianoRollView();
    ~PianoRollView() override;

    /** Binds the editor to a MIDI clip (or nullptr to show the empty hint). Rebuilds the note
        components, fits the horizontal scale to the clip and auto-scrolls to centre its pitch
        range. Safe to call repeatedly. */
    void setMidiClip (te::MidiClip*);

    /** Re-syncs the view against the CURRENTLY bound clip's live note set, for a caller that
        doesn't know whether an external edit (e.g. an app-wide undo/redo unrelated to this clip)
        actually touched it. Distinguishes a STRUCTURAL change (a note added/removed — the held
        te::MidiNote* set differs from the engine's current one) from a pure position/content
        change or no change at all: only a structural change rebuilds (dropping the selection, per
        rebuildNotes()'s contract); everything else just re-lays-out the existing components, which
        touches neither the selection nor the scroll position. No-op if no clip is bound. Cheap for
        realistic clip sizes (one Array::contains scan per held note). */
    void refreshAfterExternalEdit();

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Fired after every note edit (draw / move / resize / delete) so the shell saves. */
    std::function<void()> onEditMutated;

    /** Escape hatch for keys the roll does not consume locally: the shell wires this to its own
        keyPressed so app-wide shortcuts (Ctrl+Z/Y undo-redo, save, view toggles, transport) fire
        from inside the roll regardless of focus. Mirrors PopoutWindow::onUnhandledKey. Returns true
        if the shell handled the key. */
    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    /** Fired when the user asks to trim the bound clip's silent lead-in (nav-strip Trim button). The SHELL
        performs the trim (forge::midiedit::trimLeadingSilence), seals the undo transaction, saves, rebuilds
        the arrange surface (the clip's timeline position changes) and re-fits this roll. Left to the shell
        because a trim is a CLIP-level (arrange) edit, unlike the roll's note-level edits. No-op if unset. */
    std::function<void()> onTrimClipRequested;

    /** Optional: maps a candidate note-start time to a snapped time. The orchestrator MAY set this;
        when null (the default) PianoRollView snaps internally to its own beat grid (gridBeats). */
    std::function<te::TimePosition (te::TimePosition)> snapStartTime;

    //==============================================================================
    // --- Coordinate mapping (used by MidiNoteComponent + VelocityLane) --------------------------

    /** The width of the time axis: the grid-canvas width minus the left keybed gutter. */
    int timeAxisWidth() const;

    /** Forward map: a note's content-relative beat -> pixel x on the grid canvas (incl. gutter). */
    int beatToX (double contentBeat) const;
    /** Inverse map: a pixel x on the grid canvas -> content-relative beat. */
    double xToBeat (int x) const;

    /** Vertical map: pitch -> top y of its row, and a canvas y -> the pitch under it. Instance
        methods (not static) since keyHeight is now a live, pitch-zoomable value. */
    int pitchToY (int note) const { return (127 - note) * keyHeight; }
    int yToPitch (int y)     const { return 127 - (y / juce::jmax (1, keyHeight)); }

    /** Snaps a candidate content-beat to the editor's grid (snapStartTime first if set, else the
        internal gridBeats grid). Clamped to >= 0. */
    double snapBeat (double contentBeat) const;

    te::MidiClip* getClip() const { return clip.get(); }

    //==============================================================================
    // --- Selection model (W6) --------------------------------------------------------------------

    /** Selects ONLY this note, clearing any other selection. */
    void selectOnly (te::MidiNote&);
    /** Toggles this note in / out of the current selection (Shift / Ctrl-click, marquee add). */
    void toggleSelect (te::MidiNote&);
    /** Clears the whole selection. */
    void clearSelection();
    /** True if the note is currently selected. */
    bool isSelected (te::MidiNote&) const;

    //==============================================================================
    // --- Edit operations the note components and grid call back into -----------------------------

    /** Commits a note's new start + pitch to the engine, then persists. (Single-note; retained for
        API stability — the move path now routes through commitMoveSelection.) */
    void commitNoteMove (te::MidiNote&, double newStartBeat, int newPitch);
    /** Commits a note's new length (right-edge resize) to the engine, then persists. */
    void commitNoteResize (te::MidiNote&, double newLengthBeats);
    /** Removes a note from the clip, persists, and rebuilds the components. */
    void deleteNote (te::MidiNote&);
    /** Right-click delete: removes the whole selection if `note` is part of it, else just `note`. */
    void deleteNoteOrSelection (te::MidiNote&);

    /** Live (uncommitted) preview of a selection move: repositions every selected component by the
        dragged note's (beat, pitch) delta. No engine mutation — bounds only, restored on next
        layoutNotes(). The dragged note is expected to be in the selection. */
    void previewMoveSelection (te::MidiNote& dragged, double beatDelta, int pitchDelta);
    /** Commits a selection move: snaps the dragged note's new start, then applies the SAME beat
        delta and the pitch delta to every selected note (clamping starts >= 0, pitches 0..127),
        persists, and re-lays-out. */
    void commitMoveSelection (te::MidiNote& dragged, double beatDelta, int pitchDelta);

    /** Sets one note's velocity (1..127, clamped) on the engine and persists; called by the
        velocity lane. Non-structural -> repaints, keeps references. */
    void setNoteVelocity (te::MidiNote&, int velocity);

    //==============================================================================
    // --- Zoom / navigation (W23) -----------------------------------------------------------------

    /** Multiplies the horizontal scale (pxPerBeat) by `factor`, keeping the content-beat under
        `focusX` (a canvas x) fixed. Clamped to [minPxPerBeat, maxPxPerBeat]. */
    void zoomTime (double factor, int focusX);
    /** Multiplies the vertical scale (keyHeight) by `factor`, keeping the pitch under `focusY` (a
        VIEWPORT-local y) fixed. Clamped to [minKeyH, maxKeyH]. */
    void zoomPitch (double factor, int focusY);
    /** Pans the time axis by `deltaBeats` (clamped to the content range). */
    void panTime (double deltaBeats);
    /** Fits the whole clip's beat span across the time axis and centres its pitch range. */
    void fitClipToView();

    /** Applies a wheel gesture to the roll's zoom/pan, given the modifiers, wheel deltas and the pointer in
        CANVAS coordinates. Returns true when the gesture was consumed (a zoom or a time pan); false for a
        plain wheel, which the caller must forward to the Viewport for native vertical (pitch) scrolling.
        Public so the headless gate can drive the exact routing the mouse takes. */
    bool handleWheel (const juce::ModifierKeys& mods, float deltaX, float deltaY, int canvasX, int canvasY);

    /** The current playhead x on the grid canvas, or -1 when there is no clip or the playhead sits
        outside the visible time window. Read by the playhead overlay each paint. */
    int playheadX() const;

private:
    //==============================================================================
    /** Inner scrollable canvas: owns the note components and paints grid + keybed. Sized
        (visible width) x (128 * keyHeight) and viewed through the Viewport.

        Empty-grid interaction (W6): mouseDown only records the anchor; a drag past the threshold
        starts a marquee (rubber-band) selection painted as an overlay; a release WITHOUT a drag
        draws a note as before. So a plain click still draws; a click-drag selects.

        Wheel (W23): the modifier decode itself lives in the owner's handleWheel() (a public seam so
        the routing is testable headlessly) — Ctrl -> zoom time, Ctrl+Shift -> zoom pitch, Shift ->
        pan time, plain -> not consumed, and this forwards it to the Viewport's native vertical
        scroll. */
    class GridCanvas : public juce::Component
    {
    public:
        explicit GridCanvas (PianoRollView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        PianoRollView& owner;

        // Empty-grid press state. anchor is the mouseDown point (canvas space); marquee turns true
        // once the pointer passes the drag threshold; the live band is anchor..current.
        bool pressOnEmpty = false;
        bool marquee      = false;
        bool shiftAtPress = false;     // Shift held at press -> marquee adds to the selection
        juce::Point<int> anchor, current;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridCanvas)
    };

    /** Moving playhead line over the grid, mouse-transparent so it never blocks note editing. Polls
        the bound clip's transport on a 30 Hz timer and repaints only the thin band it crosses. */
    class PlayheadOverlay : public juce::Component,
                            private juce::Timer
    {
    public:
        explicit PlayheadOverlay (PianoRollView& o);
        void paint (juce::Graphics&) override;
    private:
        void timerCallback() override;
        PianoRollView& owner;
        int lastX = -1;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayheadOverlay)
    };

    /** Re-enumerates the clip's notes and (re)creates one MidiNoteComponent each, positioned. */
    void rebuildNotes();

    /** Positions every existing note component from its note's current beats/pitch. */
    void layoutNotes();

    /** Pushes the selection set onto the components' visual state and repaints the velocity lane. */
    void applySelectionVisuals();

    /** The component owning `note`, or nullptr (e.g. after a not-yet-rebuilt structural change). */
    MidiNoteComponent* componentFor (te::MidiNote&) const;

    /** Selects exactly this set of notes (used by marquee + paste); clears first unless `add`. */
    void selectNotes (const juce::Array<te::MidiNote*>&, bool add);

    /** Copies the current selection into the clipboard as start-relative records (Ctrl/Cmd+C). */
    void copySelection();
    /** Pastes the clipboard at the snapped playhead (or just past the copied block), selects the
        new notes, persists (structural -> rebuild). (Ctrl/Cmd+V). */
    void pasteClipboard();

    /** Auto-scrolls the Viewport to centre the clip's note pitch range (or ~C3..C5 if empty). */
    void scrollToClipPitchRange();

    /** Clip start expressed in edit beats (content beat 0). Returns 0 when there is no clip. */
    double clipStartBeat() const;

    /** The clip's scrollable content span in beats: max(note extent, clip length, a small floor)
        so a short clip still gives a little room and a long clip scrolls to its end. */
    double contentLengthBeats() const;

    /** Beats currently visible across the time axis (timeAxisWidth / pxPerBeat). */
    double visibleBeats() const;

    /** Full canvas height at the current pitch zoom. */
    int canvasHeight() const { return PianoRollLayout::numKeys * keyHeight; }

    /** Beats per bar at the clip start (time signature numerator; 4 as a safe fallback). */
    int beatsPerBar() const;

    /** Rebuilds the canvas size, note layout, scrollbar and repaints after a zoom/scroll change. */
    void applyViewChange();

    /** Re-syncs the horizontal scrollbar's range + thumb to the current scale/offset/clip. */
    void updateHScrollBar();

    /** Lays out the bottom nav strip (zoom buttons + horizontal scrollbar) within `strip`. */
    void layoutNavStrip (juce::Rectangle<int> strip);

    void notifyMutated();

    // juce::ScrollBar::Listener
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    /** Quantises note STARTS to the grid (gridBeats) at 100%: the selection, or the whole clip when the
        selection is empty. Preserves each note's length; one undoable step; non-structural (layoutNotes,
        NOT rebuildNotes — keeps the selection). Bound to the 'q' key; routes through the MidiEditHelpers
        seam so no raw te::QuantisationType call lives in the view. */
    void quantiseSelectionOrClip();

    /** Nudges every selected note by one grid step (gridBeats) — or, when no grid step is set, one bar
        (the local time signature's numerator) — in the given direction (-1 = left/earlier, +1 =
        right/later). Group-clamped so the whole selection moves together and never crosses beat 0; one
        undoable step; non-structural (layoutNotes, NOT rebuildNotes — keeps the selection). Bound to
        Shift+Left/Shift+Right; routes through MidiEditHelpers::shiftNoteStarts so no raw te::MidiNote
        mutation lives in the view. No-op if no clip is bound or the selection is empty. */
    void nudgeSelection (int direction);

    //==============================================================================
    // The bound MIDI clip, held by the engine's reference-counted handle so it can't dangle.
    te::MidiClip::Ptr clip;

    juce::Viewport viewport;                 // provides the mandatory vertical scroll
    GridCanvas canvas { *this };             // viewed component (grid + keybed + notes)
    PlayheadOverlay playhead { *this };      // over the grid, mouse-transparent
    juce::OwnedArray<MidiNoteComponent> noteComps;
    VelocityLane velocityLane { *this };     // fixed bottom strip, OUTSIDE the viewport

    // Bottom nav strip: zoom buttons + a horizontal scrollbar for the time axis.
    juce::TextButton zoomOutTimeBtn, zoomInTimeBtn, zoomOutPitchBtn, zoomInPitchBtn, fitBtn, trimBtn;
    juce::ScrollBar  hScrollBar { false };   // false = horizontal
    juce::Rectangle<int> navBounds;          // nav-strip rect (for the paint() background + captions)

    // The current selection, held as raw te::MidiNote* (valid only between structural rebuilds; the
    // set is cleared on rebuildNotes, mirroring the component lifetime). Order is not significant.
    std::vector<te::MidiNote*> selection;

    // Copy/paste clipboard: each entry is start-relative to the earliest copied note (so a paste
    // can be re-anchored anywhere) plus its length, pitch and velocity.
    struct ClipboardNote { int pitch; double startBeatOffset; double lenBeats; int velocity; };
    std::vector<ClipboardNote> clipboard;

    // Horizontal (time) scale + scroll. pxPerBeat is the zoom; hOffsetBeats is the leftmost visible
    // content beat. keyHeight is the vertical (pitch) zoom. All mutated only via the zoom/pan seams.
    double pxPerBeat   = PianoRollLayout::defaultPxPerBeat;
    double hOffsetBeats = 0.0;
    int    keyHeight    = PianoRollLayout::defaultKeyH;

    // Set on bind when the time axis width isn't known yet; consumed by the first resized().
    bool pendingFit = false;

    // Internal snap grid step in beats (1/16 note). Used when snapStartTime is not set, and drives
    // the sub-beat grid lines.
    double gridBeats = 0.25;
    // Default length in beats for a freshly drawn note (one beat).
    double defaultNoteLenBeats = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
