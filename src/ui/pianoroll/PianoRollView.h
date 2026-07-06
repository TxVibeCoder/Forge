/*
    PianoRollView — the drawer-hosted piano-roll editor for a single te::MidiClip, the MIDI
    analogue of the DetailView Inspector. When a MIDI clip is selected the shell calls
    setMidiClip(c); this binds the clip and rebuilds one MidiNoteComponent per note, laid out over
    a scrolling grid canvas. Drawing / moving / resizing / deleting a note pushes the change
    straight to the engine clip's MidiList and fires onEditMutated() so the shell persists the Edit.

    Coordinate model: the HORIZONTAL (time) axis is shared with the rest of the app via the same
    TimelineView used by the arrange surface — a left keybed gutter is excluded from the time-axis
    width (the vertical analogue of the arrange track header). The VERTICAL (pitch) axis is local:
    128 semitones at kKeyHeight px each, pitch 127 at the top, 0 at the bottom. Because that is far
    taller than the drawer, the grid lives inside a juce::Viewport and scrolls vertically; on bind
    the Viewport auto-scrolls to centre the clip's note range.

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

#include "ui/arrange/ArrangeView.h"   // shared TimelineView (time axis)
#include "ui/pianoroll/VelocityLane.h"

namespace te = tracktion;

class MidiNoteComponent;

//==============================================================================
namespace PianoRollLayout
{
    constexpr int gutterW   = 28;    // left keybed strip width (excluded from the time axis)
    constexpr int keyHeight = 12;    // px per semitone row
    constexpr int numKeys   = 128;   // MIDI pitches 0..127
}

//==============================================================================
class PianoRollView : public juce::Component
{
public:
    /** Constructed with the app's SHARED TimelineView (the same instance the arrange surface uses)
        so the horizontal time axis stays locked to the rest of the app. Does NOT own it. */
    explicit PianoRollView (TimelineView& sharedTimeline);
    ~PianoRollView() override;

    /** Binds the editor to a MIDI clip (or nullptr to show the empty hint). Rebuilds the note
        components and auto-scrolls to centre the clip's pitch range. Safe to call repeatedly. */
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

    /** Optional: maps a candidate note-start time to a snapped time. The orchestrator MAY set this;
        when null (the default) PianoRollView snaps internally to its own beat grid (gridBeats). */
    std::function<te::TimePosition (te::TimePosition)> snapStartTime;

    //==============================================================================
    // --- Coordinate mapping (used by MidiNoteComponent) ----------------------------------------

    /** The width of the time axis: the grid-canvas width minus the left keybed gutter. */
    int timeAxisWidth() const;

    /** Forward map: a note's content-relative beat -> pixel x on the grid canvas (incl. gutter). */
    int beatToX (double contentBeat) const;
    /** Inverse map: a pixel x on the grid canvas -> content-relative beat. */
    double xToBeat (int x) const;

    /** Vertical map: pitch -> top y of its row, and a canvas y -> the pitch under it. */
    static int  pitchToY (int note)  { return (127 - note) * PianoRollLayout::keyHeight; }
    static int  yToPitch (int y)     { return 127 - (y / PianoRollLayout::keyHeight); }

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

private:
    //==============================================================================
    /** Inner scrollable canvas: owns the note components and paints grid + keybed. Sized
        (timeAxisWidth + gutter) x (128 * keyHeight) and viewed through the Viewport.

        Empty-grid interaction (W6): mouseDown only records the anchor; a drag past the threshold
        starts a marquee (rubber-band) selection painted as an overlay; a release WITHOUT a drag
        draws a note as before. So a plain click still draws; a click-drag selects. */
    class GridCanvas : public juce::Component
    {
    public:
        explicit GridCanvas (PianoRollView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
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

    void notifyMutated();

    /** Quantises note STARTS to the grid (gridBeats) at 100%: the selection, or the whole clip when the
        selection is empty. Preserves each note's length; one undoable step; non-structural (layoutNotes,
        NOT rebuildNotes — keeps the selection). Bound to the 'q' key; routes through the MidiEditHelpers
        seam so no raw te::QuantisationType call lives in the view. */
    void quantiseSelectionOrClip();

    //==============================================================================
    TimelineView& timeline;                 // shared horizontal axis (not owned)

    // The bound MIDI clip, held by the engine's reference-counted handle so it can't dangle.
    te::MidiClip::Ptr clip;

    juce::Viewport viewport;                 // provides the mandatory vertical scroll
    GridCanvas canvas { *this };             // viewed component (grid + keybed + notes)
    juce::OwnedArray<MidiNoteComponent> noteComps;
    VelocityLane velocityLane { *this };     // fixed bottom strip, OUTSIDE the viewport

    // The current selection, held as raw te::MidiNote* (valid only between structural rebuilds; the
    // set is cleared on rebuildNotes, mirroring the component lifetime). Order is not significant.
    std::vector<te::MidiNote*> selection;

    // Copy/paste clipboard: each entry is start-relative to the earliest copied note (so a paste
    // can be re-anchored anywhere) plus its length, pitch and velocity.
    struct ClipboardNote { int pitch; double startBeatOffset; double lenBeats; int velocity; };
    std::vector<ClipboardNote> clipboard;

    // Internal snap grid step in beats (1/16 note). Used when snapStartTime is not set.
    double gridBeats = 0.25;
    // Default length in beats for a freshly drawn note (one beat).
    double defaultNoteLenBeats = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
