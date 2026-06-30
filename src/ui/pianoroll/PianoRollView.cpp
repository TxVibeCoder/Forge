#include "ui/pianoroll/PianoRollView.h"
#include "ui/pianoroll/MidiNoteComponent.h"
#include "ui/ForgeLookAndFeel.h"

#include <algorithm>   // std::find for the selection set

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

    addAndMakeVisible (velocityLane);             // fixed bottom strip, sized in resized()

    // Take keyboard focus on any click inside the roll so Delete / Copy / Paste land here. The roll
    // wants focus itself; the canvas + lane forward clicks up via the standard focus-on-click path.
    setWantsKeyboardFocus (true);
}

PianoRollView::~PianoRollView() = default;

//==============================================================================
void PianoRollView::setMidiClip (te::MidiClip* c)
{
    clip = c;
    clipboard.clear();          // a fresh clip starts with an empty clipboard
    rebuildNotes();             // clears the selection too
    scrollToClipPitchRange();
    repaint();
}

void PianoRollView::resized()
{
    // Split off a fixed-height velocity strip at the bottom (outside the Viewport so it never
    // scrolls with the pitch axis); the remainder hosts the scrolling grid Viewport.
    auto area = getLocalBounds();
    const int laneH = jmin (VelocityLaneLayout::laneHeight, area.getHeight() / 2);
    velocityLane.setBounds (area.removeFromBottom (laneH));
    viewport.setBounds (area);

    // The canvas spans the gutter + the full visible time axis, and the full pitch height.
    const int w = jmax (1, viewport.getMaximumVisibleWidth());
    canvas.setSize (w, kCanvasHeight);
    layoutNotes();
    velocityLane.repaint();
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
    // A structural change invalidates every held raw te::MidiNote*, so the selection is dropped here
    // (the same discipline as the components). Callers that want to keep a selection across a rebuild
    // (e.g. paste) re-establish it AFTER calling rebuildNotes().
    selection.clear();
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
    velocityLane.repaint();
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
    velocityLane.repaint();
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
    velocityLane.repaint();
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

void PianoRollView::deleteNoteOrSelection (te::MidiNote& note)
{
    if (clip == nullptr)
        return;

    // If the right-clicked note is part of a multi-selection, delete the whole selection; otherwise
    // just that note. Snapshot the targets first (removeNote + rebuild invalidate the selection).
    std::vector<te::MidiNote*> targets;
    if (isSelected (note) && selection.size() > 1)
        targets = selection;
    else
        targets.push_back (&note);

    auto* undo = &clip->edit.getUndoManager();
    for (auto* n : targets)
        clip->getSequence().removeNote (*n, undo);

    rebuildNotes();
    notifyMutated();
}

//==============================================================================
void PianoRollView::selectOnly (te::MidiNote& note)
{
    selection.clear();
    selection.push_back (&note);
    applySelectionVisuals();
}

void PianoRollView::toggleSelect (te::MidiNote& note)
{
    auto it = std::find (selection.begin(), selection.end(), &note);
    if (it != selection.end())
        selection.erase (it);
    else
        selection.push_back (&note);

    applySelectionVisuals();
}

void PianoRollView::clearSelection()
{
    if (selection.empty())
        return;

    selection.clear();
    applySelectionVisuals();
}

bool PianoRollView::isSelected (te::MidiNote& note) const
{
    return std::find (selection.begin(), selection.end(), &note) != selection.end();
}

void PianoRollView::selectNotes (const juce::Array<te::MidiNote*>& notes, bool add)
{
    if (! add)
        selection.clear();

    for (auto* n : notes)
        if (std::find (selection.begin(), selection.end(), n) == selection.end())
            selection.push_back (n);

    applySelectionVisuals();
}

void PianoRollView::applySelectionVisuals()
{
    for (auto* nc : noteComps)
        nc->setSelected (isSelected (nc->getNote()));

    velocityLane.repaint();
}

MidiNoteComponent* PianoRollView::componentFor (te::MidiNote& note) const
{
    for (auto* nc : noteComps)
        if (&nc->getNote() == &note)
            return nc;

    return nullptr;
}

//==============================================================================
void PianoRollView::previewMoveSelection (te::MidiNote& dragged, double beatDelta, int pitchDelta)
{
    // Pure bounds preview (no engine mutation): shift every selected component by the dragged note's
    // (beat, pitch) delta off its OWN origin. layoutNotes() restores truth on the next layout.
    if (! isSelected (dragged))
    {
        // Safety: the dragged note should already be selected (mouseDown selects it), but if not,
        // fall back to moving just it so the preview still tracks the pointer.
        if (auto* nc = componentFor (dragged))
        {
            const double s = jmax (0.0, dragged.getStartBeat().inBeats() + beatDelta);
            const int    p = jlimit (0, 127, dragged.getNoteNumber() + pitchDelta);
            const int x1 = beatToX (s), x2 = beatToX (s + dragged.getLengthBeats().inBeats());
            nc->setBounds (x1, pitchToY (p), jmax (2, x2 - x1), nc->getHeight());
        }
        return;
    }

    // Clamp the delta ONCE against the whole selection's headroom so the group keeps its shape: the
    // most-constrained member bounds the move, rather than each note clamping independently (which
    // would collapse chord intervals onto the beat-0 floor / pitch 0..127 edges).
    double minStart = selection.front()->getStartBeat().inBeats();
    int    minPitch = selection.front()->getNoteNumber(), maxPitch = minPitch;
    for (auto* note : selection)
    {
        minStart = jmin (minStart, note->getStartBeat().inBeats());
        minPitch = jmin (minPitch, note->getNoteNumber());
        maxPitch = jmax (maxPitch, note->getNoteNumber());
    }

    const double groupDelta = jmax (beatDelta,  -minStart);
    const int    groupPitch = jlimit (-minPitch, 127 - maxPitch, pitchDelta);

    for (auto* note : selection)
    {
        if (auto* nc = componentFor (*note))
        {
            const double s = note->getStartBeat().inBeats() + groupDelta;
            const int    p = note->getNoteNumber() + groupPitch;
            const int x1 = beatToX (s), x2 = beatToX (s + note->getLengthBeats().inBeats());
            nc->setBounds (x1, pitchToY (p), jmax (2, x2 - x1), nc->getHeight());
        }
    }
}

void PianoRollView::commitMoveSelection (te::MidiNote& dragged, double beatDelta, int pitchDelta)
{
    if (clip == nullptr)
        return;

    if (! isSelected (dragged))
        selectOnly (dragged);

    auto* undo = &clip->edit.getUndoManager();

    // Snap the DRAGGED note's new start, then re-derive the delta from the snapped value so every
    // note in the selection moves by exactly the same (snapped) beat amount. Pitch shifts by the
    // raw pitch delta; both are clamped per note (starts >= 0, pitches 0..127).
    const double snappedStart = snapBeat (dragged.getStartBeat().inBeats() + beatDelta);
    const double snappedDelta = snappedStart - dragged.getStartBeat().inBeats();

    // Clamp the (snapped) delta ONCE against the selection's headroom so every note moves by the
    // SAME amount and the group keeps its shape — the most-constrained member bounds the move,
    // instead of per-note clamps collapsing chord intervals at the beat-0 / pitch 0..127 edges.
    double minStart = selection.front()->getStartBeat().inBeats();
    int    minPitch = selection.front()->getNoteNumber(), maxPitch = minPitch;
    for (auto* note : selection)
    {
        minStart = jmin (minStart, note->getStartBeat().inBeats());
        minPitch = jmin (minPitch, note->getNoteNumber());
        maxPitch = jmax (maxPitch, note->getNoteNumber());
    }

    const double groupDelta = jmax (snappedDelta, -minStart);
    const int    groupPitch = jlimit (-minPitch, 127 - maxPitch, pitchDelta);

    for (auto* note : selection)
    {
        const double newStart = jmax (0.0, note->getStartBeat().inBeats() + groupDelta);
        const int    newPitch = note->getNoteNumber() + groupPitch;

        note->setStartAndLength (te::BeatPosition::fromBeats (newStart), note->getLengthBeats(), undo);
        note->setNoteNumber (newPitch, undo);
    }

    layoutNotes();
    velocityLane.repaint();
    notifyMutated();
}

void PianoRollView::setNoteVelocity (te::MidiNote& note, int velocity)
{
    if (clip == nullptr)
        return;

    auto* undo = &clip->edit.getUndoManager();
    note.setVelocity (jlimit (1, 127, velocity), undo);

    // Non-structural: reflect the new dynamic in the grid note (alpha) and the lane bar.
    if (auto* nc = componentFor (note))
        nc->repaint();
    velocityLane.repaint();
    notifyMutated();
}

//==============================================================================
void PianoRollView::copySelection()
{
    if (selection.empty())
        return;

    // Find the earliest selected start so the clipboard is anchor-relative (paste can re-anchor it).
    double minStart = selection.front()->getStartBeat().inBeats();
    for (auto* n : selection)
        minStart = jmin (minStart, n->getStartBeat().inBeats());

    clipboard.clear();
    for (auto* n : selection)
        clipboard.push_back ({ n->getNoteNumber(),
                               n->getStartBeat().inBeats() - minStart,
                               n->getLengthBeats().inBeats(),
                               n->getVelocity() });
}

void PianoRollView::pasteClipboard()
{
    if (clip == nullptr || clipboard.empty())
        return;

    // Anchor at the snapped transport-playhead content-beat when the playhead sits within the clip's
    // span (so paste lands where the user is looking); otherwise fall back to one bar-ish (4 beats)
    // past the clip start so the paste is visible and doesn't stack on beat 0.
    auto& ts = clip->edit.tempoSequence;
    const auto playheadTime = clip->edit.getTransport().getPosition();
    const double playheadBeat = ts.toBeats (playheadTime).inBeats() - clipStartBeat();

    double clipLenBeats = 0.0;
    for (auto* n : clip->getSequence().getNotes())
        clipLenBeats = jmax (clipLenBeats, n->getEndBeat().inBeats());

    double anchorBeat;
    if (playheadBeat >= 0.0 && (clipLenBeats <= 0.0 || playheadBeat <= clipLenBeats + 4.0))
        anchorBeat = snapBeat (playheadBeat);
    else
        anchorBeat = snapBeat (4.0);   // one 4/4 bar past the clip start

    auto* undo = &clip->edit.getUndoManager();

    // addNote returns the live MidiNote*; collect them so we can select exactly the pasted set after
    // the rebuild (the pointers remain valid — they are owned by the MidiList, not removed).
    juce::Array<te::MidiNote*> pasted;
    for (const auto& c : clipboard)
    {
        const double start = jmax (0.0, anchorBeat + c.startBeatOffset);
        auto* n = clip->getSequence().addNote (jlimit (0, 127, c.pitch),
                                               te::BeatPosition::fromBeats (start),
                                               te::BeatDuration::fromBeats (c.lenBeats),
                                               jlimit (1, 127, c.velocity), 0, undo);
        if (n != nullptr)
            pasted.add (n);
    }

    rebuildNotes();                 // structural change -> rebuild (clears selection)
    selectNotes (pasted, false);    // then select exactly the new notes for an immediate move
    notifyMutated();
}

//==============================================================================
bool PianoRollView::keyPressed (const juce::KeyPress& key)
{
    if (clip == nullptr)
        return false;

    // Delete / Backspace -> remove the whole selection in one undo transaction.
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selection.empty())
            return false;

        auto targets = selection;   // snapshot (removeNote + rebuild invalidate the selection)
        auto* undo = &clip->edit.getUndoManager();
        for (auto* n : targets)
            clip->getSequence().removeNote (*n, undo);

        rebuildNotes();
        notifyMutated();
        return true;
    }

    // Ctrl/Cmd + C / V -> copy / paste the selection.
    if (key.getModifiers().isCommandDown())
    {
        if (key.isKeyCode ('C') || key.isKeyCode ('c'))
        {
            copySelection();
            return true;
        }
        if (key.isKeyCode ('V') || key.isKeyCode ('v'))
        {
            pasteClipboard();
            return true;
        }
    }

    return false;
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

    // --- Marquee (rubber-band) overlay -----------------------------------------------------------
    if (marquee)
    {
        const auto band = Rectangle<int> (anchor, current).toFloat();
        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (0.18f));
        g.fillRect (band);
        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (0.8f));
        g.drawRect (band, 1.0f);
    }
}

void PianoRollView::GridCanvas::mouseDown (const MouseEvent& e)
{
    // The roll grabs keyboard focus on any grid press so Delete / Copy / Paste land here.
    owner.grabKeyboardFocus();

    pressOnEmpty = false;
    marquee = false;

    // Right-click and gutter / null-clip presses are inert here (right-click delete lives on notes).
    if (owner.clip == nullptr || e.x < PianoRollLayout::gutterW || e.mods.isPopupMenu())
        return;

    // Record the anchor only; the action is decided on mouseUp/mouseDrag. A plain click (no drag)
    // DRAWS a note (mouseUp); a drag past the threshold starts a marquee select (mouseDrag).
    pressOnEmpty = true;
    shiftAtPress = e.mods.isShiftDown();
    anchor  = e.getPosition();
    current = anchor;
}

void PianoRollView::GridCanvas::mouseDrag (const MouseEvent& e)
{
    if (! pressOnEmpty || e.mods.isPopupMenu())
        return;

    current = e.getPosition();

    if (! marquee)
    {
        // Require a small movement before treating this as a marquee (so a plain click still draws).
        if (std::abs (current.x - anchor.x) < 3 && std::abs (current.y - anchor.y) < 3)
            return;

        marquee = true;
    }

    repaint();
}

void PianoRollView::GridCanvas::mouseUp (const MouseEvent& e)
{
    if (! pressOnEmpty || e.mods.isPopupMenu())
    {
        pressOnEmpty = false;
        return;
    }

    if (marquee)
    {
        // Select every note whose component intersects the band; Shift adds, otherwise replaces.
        const auto band = Rectangle<int> (anchor, current);
        juce::Array<te::MidiNote*> hits;
        for (auto* nc : owner.noteComps)
            if (nc->getBounds().intersects (band))
                hits.add (&nc->getNote());

        owner.selectNotes (hits, shiftAtPress);

        marquee = false;
        pressOnEmpty = false;
        repaint();
        return;
    }

    // No drag past the threshold -> a plain click on the empty grid DRAWS a new note (as before).
    pressOnEmpty = false;

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
