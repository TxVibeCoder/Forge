#include "ui/pianoroll/PianoRollView.h"
#include "ui/pianoroll/MidiNoteComponent.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

namespace
{
    using namespace PianoRollLayout;

    // Full canvas height: every MIDI pitch gets a row.
    constexpr int kCanvasHeight = numKeys * keyHeight;

    // Default pitch window centred on bind when the clip has no notes (~C3..C5). MIDI note 60 = C4.
    constexpr int kDefaultLowPitch  = 48;   // C3
    constexpr int kDefaultHighPitch = 72;   // C5

    // True for the black keys of the chromatic scale (pitch % 12 lookup).
    bool isBlackKey (int pitch)
    {
        switch (pitch % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default:                                  return false;
        }
    }
}

//==============================================================================
PianoRollView::PianoRollView (TimelineView& sharedTimeline)
    : timeline (sharedTimeline)
{
    canvas.setSize (1, kCanvasHeight);
    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, false);   // vertical only; the time axis never scrolls here
    addAndMakeVisible (viewport);
}

PianoRollView::~PianoRollView() = default;

//==============================================================================
void PianoRollView::setMidiClip (te::MidiClip* c)
{
    clip = c;
    rebuildNotes();
    scrollToClipPitchRange();
    repaint();
}

void PianoRollView::resized()
{
    viewport.setBounds (getLocalBounds());

    // The canvas spans the gutter + the full visible time axis, and the full pitch height.
    const int w = jmax (1, viewport.getMaximumVisibleWidth());
    canvas.setSize (w, kCanvasHeight);
    layoutNotes();
}

void PianoRollView::paint (Graphics& g)
{
    // The canvas paints the grid; this only fills behind the (possibly shorter) viewport.
    g.fillAll (Colour (ForgeLookAndFeel::shellBg));
}

//==============================================================================
int PianoRollView::timeAxisWidth() const
{
    // Mirror AudioClipComponent::clipAreaWidth: subtract the gutter (the vertical analogue of the
    // arrange track header) from the canvas width so time maps over the playable strip only.
    return jmax (0, canvas.getWidth() - PianoRollLayout::gutterW);
}

double PianoRollView::clipStartBeat() const
{
    if (clip == nullptr)
        return 0.0;

    auto& ts = clip->edit.tempoSequence;
    return ts.toBeats (clip->getPosition().getStart()).inBeats();
}

int PianoRollView::beatToX (double contentBeat) const
{
    if (clip == nullptr)
        return PianoRollLayout::gutterW;

    auto& ts = clip->edit.tempoSequence;
    const auto editTime = ts.toTime (te::BeatPosition::fromBeats (clipStartBeat() + contentBeat));
    return timeline.timeToX (editTime, timeAxisWidth()) + PianoRollLayout::gutterW;
}

double PianoRollView::xToBeat (int x) const
{
    if (clip == nullptr)
        return 0.0;

    auto& ts = clip->edit.tempoSequence;
    const auto editTime = timeline.xToTime (x - PianoRollLayout::gutterW, timeAxisWidth());
    return ts.toBeats (editTime).inBeats() - clipStartBeat();
}

double PianoRollView::snapBeat (double contentBeat) const
{
    if (contentBeat < 0.0)
        contentBeat = 0.0;

    // If the orchestrator supplied an edit-time snapper, round-trip the candidate through it in the
    // time domain so the editor honours the app's grid; otherwise fall back to our own beat grid.
    if (snapStartTime != nullptr && clip != nullptr)
    {
        auto& ts = clip->edit.tempoSequence;
        const auto editTime = ts.toTime (te::BeatPosition::fromBeats (clipStartBeat() + contentBeat));
        const auto snapped  = snapStartTime (editTime);
        double b = ts.toBeats (snapped).inBeats() - clipStartBeat();
        return jmax (0.0, b);
    }

    if (gridBeats <= 0.0)
        return contentBeat;

    return jmax (0.0, std::round (contentBeat / gridBeats) * gridBeats);
}

//==============================================================================
void PianoRollView::rebuildNotes()
{
    noteComps.clear();

    if (clip != nullptr)
    {
        // Always edit the live sequence (NEVER the looped copy — its edits are discarded).
        for (auto* note : clip->getSequence().getNotes())
        {
            auto* nc = noteComps.add (new MidiNoteComponent (*this, *note));
            canvas.addAndMakeVisible (nc);
        }
    }

    layoutNotes();
    canvas.repaint();
}

void PianoRollView::layoutNotes()
{
    for (auto* nc : noteComps)
    {
        auto& note = nc->getNote();
        const double startBeat = note.getStartBeat().inBeats();
        const double lenBeats  = note.getLengthBeats().inBeats();
        const int pitch        = note.getNoteNumber();

        const int x1 = beatToX (startBeat);
        const int x2 = beatToX (startBeat + lenBeats);
        const int y  = pitchToY (pitch);

        nc->setBounds (x1, y, jmax (2, x2 - x1), PianoRollLayout::keyHeight - 1);
    }
}

void PianoRollView::scrollToClipPitchRange()
{
    int lowPitch = kDefaultLowPitch, highPitch = kDefaultHighPitch;

    if (clip != nullptr && clip->getSequence().getNumNotes() > 0)
    {
        const auto range = clip->getSequence().getNoteNumberRange();
        lowPitch  = range.getStart();
        highPitch = range.getEnd();
    }

    // Centre the range: the lower pitch sits lower on screen (larger y), so the visible band runs
    // from pitchToY(highPitch) down to pitchToY(lowPitch). Scroll so its midpoint is centred.
    const int yTop    = pitchToY (highPitch);
    const int yBottom = pitchToY (lowPitch) + PianoRollLayout::keyHeight;
    const int mid     = (yTop + yBottom) / 2;
    const int targetY = jlimit (0, jmax (0, kCanvasHeight - viewport.getMaximumVisibleHeight()),
                                mid - viewport.getMaximumVisibleHeight() / 2);

    viewport.setViewPosition (0, targetY);
}

//==============================================================================
void PianoRollView::commitNoteMove (te::MidiNote& note, double newStartBeat, int newPitch)
{
    if (clip == nullptr)
        return;

    auto* undo = &clip->edit.getUndoManager();
    const double snapped = snapBeat (newStartBeat);
    const int pitch = jlimit (0, 127, newPitch);

    note.setStartAndLength (te::BeatPosition::fromBeats (snapped), note.getLengthBeats(), undo);
    note.setNoteNumber (pitch, undo);

    layoutNotes();
    notifyMutated();
}

void PianoRollView::commitNoteResize (te::MidiNote& note, double newLengthBeats)
{
    if (clip == nullptr)
        return;

    auto* undo = &clip->edit.getUndoManager();

    // Snap the END of the note to the grid, then derive the (>= one grid step) length.
    const double startBeat = note.getStartBeat().inBeats();
    const double snappedEnd = snapBeat (startBeat + newLengthBeats);
    const double minLen = (gridBeats > 0.0 && snapStartTime == nullptr) ? gridBeats : 0.0625;
    const double len = jmax (minLen, snappedEnd - startBeat);

    note.setStartAndLength (note.getStartBeat(), te::BeatDuration::fromBeats (len), undo);

    layoutNotes();
    notifyMutated();
}

void PianoRollView::deleteNote (te::MidiNote& note)
{
    if (clip == nullptr)
        return;

    auto* undo = &clip->edit.getUndoManager();
    clip->getSequence().removeNote (note, undo);

    // Rebuild so the raw MidiNote* in the removed component is never dereferenced again.
    rebuildNotes();
    notifyMutated();
}

void PianoRollView::notifyMutated()
{
    if (onEditMutated != nullptr)
        onEditMutated();
}

//==============================================================================
void PianoRollView::GridCanvas::paint (Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll (Colour (ForgeLookAndFeel::panelBg));

    // --- Pitch rows: alternating shading for white / black keys ----------------------------------
    for (int pitch = 0; pitch < PianoRollLayout::numKeys; ++pitch)
    {
        const int y = PianoRollView::pitchToY (pitch);

        g.setColour (isBlackKey (pitch) ? Colour (ForgeLookAndFeel::panelBg)
                                        : Colour (ForgeLookAndFeel::raisedBg));
        g.fillRect (PianoRollLayout::gutterW, y,
                    jmax (0, bounds.getWidth() - PianoRollLayout::gutterW),
                    PianoRollLayout::keyHeight);

        // A slightly stronger hairline on each octave boundary (C row) reads as the major gridline.
        if (pitch % 12 == 0)
        {
            g.setColour (Colour (ForgeLookAndFeel::hairline));
            g.fillRect (PianoRollLayout::gutterW, y, bounds.getWidth() - PianoRollLayout::gutterW, 1);
        }
    }

    // --- Vertical beat lines: walk the tempo sequence across the visible time window -------------
    if (owner.clip != nullptr && owner.timeAxisWidth() > 0)
    {
        auto& ts = owner.clip->edit.tempoSequence;
        const int w = owner.timeAxisWidth();

        // Map the visible time window to a beat range, then draw a line at each whole beat.
        const double startBeat = owner.xToBeat (PianoRollLayout::gutterW) + owner.clipStartBeat();
        const double endBeat   = owner.xToBeat (PianoRollLayout::gutterW + w) + owner.clipStartBeat();

        const int firstBeat = (int) std::floor (jmax (0.0, startBeat));
        const int lastBeat  = (int) std::ceil  (jmax (0.0, endBeat));

        for (int beat = firstBeat; beat <= lastBeat; ++beat)
        {
            const auto t = ts.toTime (te::BeatPosition::fromBeats ((double) beat));
            const int x  = owner.timeline.timeToX (t, w) + PianoRollLayout::gutterW;

            if (x < PianoRollLayout::gutterW || x > bounds.getWidth())
                continue;

            g.setColour (Colour (ForgeLookAndFeel::hairline).withAlpha (0.7f));
            g.fillRect (x, 0, 1, bounds.getHeight());
        }
    }

    // --- Left keybed gutter: octave labels (C1, C2, ...) -----------------------------------------
    g.setColour (Colour (ForgeLookAndFeel::shellBg));
    g.fillRect (0, 0, PianoRollLayout::gutterW, bounds.getHeight());
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (PianoRollLayout::gutterW - 1, 0, 1, bounds.getHeight());

    g.setFont (FontOptions (9.0f));
    for (int pitch = 0; pitch < PianoRollLayout::numKeys; pitch += 12)
    {
        const int y = PianoRollView::pitchToY (pitch);
        const int octave = pitch / 12 - 1;   // MIDI: note 0 = C-1, note 60 = C4

        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("C" + String (octave),
                    Rectangle<int> (2, y, PianoRollLayout::gutterW - 3, PianoRollLayout::keyHeight),
                    Justification::centredLeft, false);
    }

    // --- Empty-clip hint -------------------------------------------------------------------------
    if (owner.clip == nullptr || owner.clip->getSequence().getNumNotes() == 0)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.6f));
        g.setFont (FontOptions (13.0f));

        const String hint = owner.clip == nullptr ? "No MIDI clip selected" : "Draw notes here";
        g.drawText (hint,
                    Rectangle<int> (PianoRollLayout::gutterW,
                                    jmax (0, owner.viewport.getViewPositionY()),
                                    jmax (0, getWidth() - PianoRollLayout::gutterW),
                                    jmax (0, owner.viewport.getMaximumVisibleHeight())),
                    Justification::centred, false);
    }
}

void PianoRollView::GridCanvas::mouseDown (const MouseEvent& e)
{
    // A click on the empty grid draws a new note. Clicks landing on a MidiNoteComponent are handled
    // there (this only fires for the bare canvas). The gutter is non-drawing.
    if (owner.clip == nullptr || e.x < PianoRollLayout::gutterW)
        return;

    if (e.mods.isPopupMenu())
        return;

    const double startBeat = owner.snapBeat (owner.xToBeat (e.x));
    const int pitch = jlimit (0, 127, PianoRollView::yToPitch (e.y));

    auto* undo = &owner.clip->edit.getUndoManager();
    owner.clip->getSequence().addNote (pitch,
                                       te::BeatPosition::fromBeats (startBeat),
                                       te::BeatDuration::fromBeats (owner.defaultNoteLenBeats),
                                       100, 0, undo);

    owner.rebuildNotes();
    owner.notifyMutated();
}
