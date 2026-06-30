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

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/arrange/ArrangeView.h"   // shared TimelineView (time axis)

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

    void resized() override;
    void paint (juce::Graphics&) override;

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
    // --- Edit operations the note components and grid call back into -----------------------------

    /** Commits a note's new start + pitch to the engine, then persists. */
    void commitNoteMove (te::MidiNote&, double newStartBeat, int newPitch);
    /** Commits a note's new length (right-edge resize) to the engine, then persists. */
    void commitNoteResize (te::MidiNote&, double newLengthBeats);
    /** Removes a note from the clip, persists, and rebuilds the components. */
    void deleteNote (te::MidiNote&);

private:
    //==============================================================================
    /** Inner scrollable canvas: owns the note components and paints grid + keybed. Sized
        (timeAxisWidth + gutter) x (128 * keyHeight) and viewed through the Viewport. */
    class GridCanvas : public juce::Component
    {
    public:
        explicit GridCanvas (PianoRollView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        PianoRollView& owner;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridCanvas)
    };

    /** Re-enumerates the clip's notes and (re)creates one MidiNoteComponent each, positioned. */
    void rebuildNotes();

    /** Positions every existing note component from its note's current beats/pitch. */
    void layoutNotes();

    /** Auto-scrolls the Viewport to centre the clip's note pitch range (or ~C3..C5 if empty). */
    void scrollToClipPitchRange();

    /** Clip start expressed in edit beats (content beat 0). Returns 0 when there is no clip. */
    double clipStartBeat() const;

    void notifyMutated();

    //==============================================================================
    TimelineView& timeline;                 // shared horizontal axis (not owned)

    // The bound MIDI clip, held by the engine's reference-counted handle so it can't dangle.
    te::MidiClip::Ptr clip;

    juce::Viewport viewport;                 // provides the mandatory vertical scroll
    GridCanvas canvas { *this };             // viewed component (grid + keybed + notes)
    juce::OwnedArray<MidiNoteComponent> noteComps;

    // Internal snap grid step in beats (1/16 note). Used when snapStartTime is not set.
    double gridBeats = 0.25;
    // Default length in beats for a freshly drawn note (one beat).
    double defaultNoteLenBeats = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
