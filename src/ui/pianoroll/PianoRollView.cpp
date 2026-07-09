#include "ui/pianoroll/PianoRollView.h"
#include "ui/pianoroll/MidiNoteComponent.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"
#include "engine/MidiEditHelpers.h"   // W4: forge::midiedit::quantiseNoteStarts

#include <algorithm>   // std::find for the selection set
#include <cmath>       // std::llround / std::floor / std::round

using namespace juce;

namespace
{
    using namespace PianoRollLayout;

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
PianoRollView::PianoRollView()
{
    canvas.setSize (1, canvasHeight());
    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, false);   // vertical only; the time axis uses our own hScrollBar
    addAndMakeVisible (viewport);

    addAndMakeVisible (playhead);                 // OVER the grid (added after the viewport), mouse-transparent
    addAndMakeVisible (velocityLane);             // fixed bottom strip, sized in resized()

    // --- Bottom nav strip: zoom buttons + a horizontal (time) scrollbar --------------------------
    auto initZoomButton = [this] (TextButton& b, const String& text, const String& tip)
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setWantsKeyboardFocus (false);          // never steal focus from the roll (keeps Ctrl+Z live)
        b.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
        b.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
        addAndMakeVisible (b);
    };
    initZoomButton (zoomOutTimeBtn,  "-", "Zoom out (time)  \xe2\x80\x94  Ctrl+Scroll");
    initZoomButton (zoomInTimeBtn,   "+", "Zoom in (time)  \xe2\x80\x94  Ctrl+Scroll");
    initZoomButton (zoomOutPitchBtn, "-", "Zoom out (pitch)  \xe2\x80\x94  Ctrl+Shift+Scroll");
    initZoomButton (zoomInPitchBtn,  "+", "Zoom in (pitch)  \xe2\x80\x94  Ctrl+Shift+Scroll");
    initZoomButton (fitBtn,       "Fit", "Fit the whole clip to the view");
    initZoomButton (trimBtn,     "Trim", "Trim the clip's silent start");

    zoomOutTimeBtn.onClick  = [this] { zoomTime  (0.8,  gutterW + timeAxisWidth() / 2); };
    zoomInTimeBtn.onClick   = [this] { zoomTime  (1.25, gutterW + timeAxisWidth() / 2); };
    zoomOutPitchBtn.onClick = [this] { zoomPitch (0.8,  viewport.getMaximumVisibleHeight() / 2); };
    zoomInPitchBtn.onClick  = [this] { zoomPitch (1.25, viewport.getMaximumVisibleHeight() / 2); };
    fitBtn.onClick          = [this] { fitClipToView(); };
    trimBtn.onClick         = [this]
    {
        // A CLIP-level (arrange) edit, not a note-level one -- the shell performs it (see the
        // onTrimClipRequested doc comment). No-op with no clip bound or the callback unset.
        if (getClip() == nullptr)
            return;
        if (onTrimClipRequested != nullptr)
            onTrimClipRequested();
    };

    hScrollBar.setWantsKeyboardFocus (false);
    hScrollBar.setAutoHide (false);
    hScrollBar.addListener (this);
    addAndMakeVisible (hScrollBar);

    // Take keyboard focus on any click inside the roll so Delete / Copy / Paste / Ctrl+Z land here.
    // The canvas, notes and lane forward clicks up via the standard focus-on-click path.
    setWantsKeyboardFocus (true);
}

PianoRollView::~PianoRollView()
{
    hScrollBar.removeListener (this);
}

//==============================================================================
void PianoRollView::setMidiClip (te::MidiClip* c)
{
    clip = c;
    clipboard.clear();          // a fresh clip starts with an empty clipboard
    rebuildNotes();             // clears the selection too
    fitClipToView();            // horizontal fit (defers to resized() if the width isn't known yet)
    scrollToClipPitchRange();
    repaint();
}

void PianoRollView::refreshAfterExternalEdit()
{
    if (clip == nullptr)
        return;

    // A structural change (add/remove) destroys-and-recreates the affected te::MidiNote object(s)
    // (the engine's MidiList is a ValueTreeObjectList over the note child nodes), so a HELD
    // component's note reference goes dangling/orphaned when that happens. A pure position/content
    // edit (move, resize, velocity) mutates the SAME te::MidiNote object's properties in place — its
    // identity survives. So: if every component's note is still present (by identity) in the clip's
    // CURRENT live set, and the counts match, nothing structural happened — a cheap layoutNotes()
    // (which reads each note's current beat/pitch and repositions, touching neither the selection
    // nor the scroll) is enough. Only fall back to the destructive rebuildNotes() when the held set
    // has genuinely diverged from the engine's.
    const auto& liveNotes = clip->getSequence().getNotes();

    bool structurallyStale = (liveNotes.size() != noteComps.size());

    if (! structurallyStale)
        for (auto* nc : noteComps)
            if (! liveNotes.contains (&nc->getNote()))
            {
                structurallyStale = true;
                break;
            }

    if (structurallyStale)
        rebuildNotes();
    else
        layoutNotes();

    playhead.repaint();
}

void PianoRollView::resized()
{
    auto area = getLocalBounds();

    // Fixed velocity strip at the very bottom (outside the Viewport so it never scrolls with pitch);
    // then a compact nav strip (zoom buttons + horizontal scrollbar); the remainder hosts the grid.
    const int laneH = jmin (VelocityLaneLayout::laneHeight, area.getHeight() / 3);
    velocityLane.setBounds (area.removeFromBottom (laneH));

    navBounds = area.removeFromBottom (PianoRollLayout::navStripH);
    layoutNavStrip (navBounds);

    viewport.setBounds (area);
    playhead.setBounds (area);   // exactly over the grid viewport (mouse-transparent)

    canvas.setSize (jmax (1, viewport.getMaximumVisibleWidth()), canvasHeight());

    if (pendingFit && timeAxisWidth() > 0)
    {
        fitClipToView();
    }
    else
    {
        // Re-clamp the offset to the (possibly changed) width and resync scrollbar + notes.
        const double maxOff = jmax (0.0, contentLengthBeats() - visibleBeats());
        hOffsetBeats = jlimit (0.0, maxOff, hOffsetBeats);
        updateHScrollBar();
        layoutNotes();
    }

    velocityLane.repaint();
    playhead.repaint();
}

void PianoRollView::layoutNavStrip (juce::Rectangle<int> strip)
{
    strip.reduce (3, 2);

    auto place = [&strip] (Component& c, int w)
    {
        c.setBounds (strip.removeFromLeft (w).reduced (0, 0));
        strip.removeFromLeft (3);
    };

    strip.removeFromLeft (34);   // "Time" caption (drawn in paint())
    place (zoomOutTimeBtn, 22);
    place (zoomInTimeBtn,  22);
    strip.removeFromLeft (8);
    strip.removeFromLeft (36);   // "Pitch" caption (drawn in paint())
    place (zoomOutPitchBtn, 22);
    place (zoomInPitchBtn,  22);
    strip.removeFromLeft (8);
    place (fitBtn, 34);
    strip.removeFromLeft (6);
    place (trimBtn, 40);
    strip.removeFromLeft (6);

    hScrollBar.setBounds (strip.withTrimmedTop (3).withTrimmedBottom (3));   // fills the rest
}

void PianoRollView::paint (Graphics& g)
{
    // The canvas paints the grid; this fills behind the (possibly shorter) viewport + the nav strip.
    g.fillAll (Colour (ForgeLookAndFeel::shellBg));

    if (! navBounds.isEmpty())
    {
        g.setColour (Colour (ForgeLookAndFeel::panelBg));
        g.fillRect (navBounds);
        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (navBounds.getX(), navBounds.getY(), navBounds.getWidth(), 1);

        // Axis captions to the left of each zoom pair (buttons are laid out already).
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (FontOptions (10.0f));
        const auto tb = zoomOutTimeBtn.getBounds();
        const auto pb = zoomOutPitchBtn.getBounds();
        g.drawText ("Time",  tb.getX() - 34, tb.getY(), 31, tb.getHeight(), Justification::centredRight, false);
        g.drawText ("Pitch", pb.getX() - 36, pb.getY(), 33, pb.getHeight(), Justification::centredRight, false);
    }
}

//==============================================================================
int PianoRollView::timeAxisWidth() const
{
    // The playable strip: the canvas width minus the left keybed gutter.
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
    // Pure linear beat -> pixel scale, independent of the arrange timeline (W23). beat 0 = clip start.
    return PianoRollLayout::gutterW + (int) std::llround ((contentBeat - hOffsetBeats) * pxPerBeat);
}

double PianoRollView::xToBeat (int x) const
{
    return hOffsetBeats + (double) (x - PianoRollLayout::gutterW) / jmax (1.0e-6, pxPerBeat);
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

double PianoRollView::contentLengthBeats() const
{
    if (clip == nullptr)
        return 4.0;

    double noteExtent = 0.0;
    for (auto* n : clip->getSequence().getNotes())
        noteExtent = jmax (noteExtent, n->getEndBeat().inBeats());

    auto& ts = clip->edit.tempoSequence;
    const double clipLen = jmax (0.0, ts.toBeats (clip->getPosition().getEnd()).inBeats() - clipStartBeat());

    return jmax (noteExtent, jmax (clipLen, 4.0));
}

double PianoRollView::visibleBeats() const
{
    return pxPerBeat > 0.0 ? (double) timeAxisWidth() / pxPerBeat : 0.0;
}

int PianoRollView::beatsPerBar() const
{
    if (clip == nullptr)
        return 4;

    // Time signature numerator at the clip start (mirrors ProjectSession's beats-per-bar idiom).
    return jmax (1, clip->edit.tempoSequence.getTimeSigAt (
                        te::BeatPosition::fromBeats (clipStartBeat())).numerator.get());
}

//==============================================================================
void PianoRollView::zoomTime (double factor, int focusX)
{
    const double focalBeat = xToBeat (focusX);
    const double newPx = jlimit (PianoRollLayout::minPxPerBeat, PianoRollLayout::maxPxPerBeat, pxPerBeat * factor);
    if (newPx == pxPerBeat)   // already at a zoom clamp — nothing to do
        return;

    // Keep focalBeat fixed under focusX: focusX = gutter + (focalBeat - hOffset') * newPx.
    hOffsetBeats = focalBeat - (double) (focusX - PianoRollLayout::gutterW) / newPx;
    pxPerBeat = newPx;
    applyViewChange();
}

void PianoRollView::zoomPitch (double factor, int focusY)
{
    const int oldH = keyHeight;
    const int newH = jlimit (PianoRollLayout::minKeyH, PianoRollLayout::maxKeyH,
                             (int) std::llround (keyHeight * factor));
    if (newH == oldH)
        return;

    // Canvas-space y under the pointer (focusY is viewport-local), split into pitch row + fraction so
    // the exact spot under the pointer stays put across the zoom.
    const int    canvasY = viewport.getViewPositionY() + focusY;
    const int    rowsDown = jlimit (0, PianoRollLayout::numKeys - 1, canvasY / jmax (1, oldH));
    const double frac    = (double) (canvasY - rowsDown * oldH) / (double) jmax (1, oldH);

    keyHeight = newH;
    canvas.setSize (jmax (1, viewport.getMaximumVisibleWidth()), canvasHeight());

    const int newCanvasY = (int) std::llround ((rowsDown + frac) * newH);
    const int targetTop  = jlimit (0, jmax (0, canvasHeight() - viewport.getMaximumVisibleHeight()),
                                   newCanvasY - focusY);
    viewport.setViewPosition (0, targetTop);

    layoutNotes();
    canvas.repaint();
    velocityLane.repaint();
    playhead.repaint();
}

void PianoRollView::panTime (double deltaBeats)
{
    hOffsetBeats += deltaBeats;
    applyViewChange();
}

void PianoRollView::fitClipToView()
{
    const double len = jmax (1.0, contentLengthBeats());
    const int w = timeAxisWidth();
    if (w <= 0)
    {
        pendingFit = true;   // width not known yet — resized() will re-run this
        return;
    }

    pxPerBeat = jlimit (PianoRollLayout::minPxPerBeat, PianoRollLayout::maxPxPerBeat, (double) w / len);
    hOffsetBeats = 0.0;
    pendingFit = false;

    applyViewChange();
    scrollToClipPitchRange();
}

bool PianoRollView::handleWheel (const juce::ModifierKeys& mods, float deltaX, float deltaY, int canvasX, int canvasY)
{
    // Ctrl/Cmd -> zoom (time, or pitch with Shift), anchored under the pointer. Sign of deltaY picks
    // in vs out; a fixed step keeps it predictable across mice with different wheel resolutions.
    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        const double f = (deltaY >= 0.0f) ? 1.25 : 0.8;
        if (mods.isShiftDown())
            zoomPitch (f, canvasY - viewport.getViewPositionY());   // canvasY (canvas-space) -> viewport-local
        else
            zoomTime (f, canvasX);
        return true;
    }

    // Shift -> pan the time axis. Some mice report the horizontal delta, others the vertical.
    if (mods.isShiftDown())
    {
        const double d = (deltaX != 0.0f ? (double) deltaX : (double) deltaY);
        panTime (-d * jmax (1.0, visibleBeats()) * 0.2);
        return true;
    }

    // Plain wheel -> not consumed; the caller forwards it to the Viewport for native vertical scroll.
    return false;
}

void PianoRollView::applyViewChange()
{
    // Clamp the horizontal offset to the content span, then resync canvas size, note layout,
    // scrollbar and repaint. Shared tail of every zoom/pan/scroll path.
    const double maxOff = jmax (0.0, contentLengthBeats() - visibleBeats());
    hOffsetBeats = jlimit (0.0, maxOff, hOffsetBeats);

    canvas.setSize (jmax (1, viewport.getMaximumVisibleWidth()), canvasHeight());
    layoutNotes();
    updateHScrollBar();

    canvas.repaint();
    velocityLane.repaint();
    playhead.repaint();
}

void PianoRollView::updateHScrollBar()
{
    const double total = jmax (contentLengthBeats(), visibleBeats());
    hScrollBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScrollBar.setCurrentRange (hOffsetBeats, jmax (0.0, visibleBeats()), juce::dontSendNotification);
}

void PianoRollView::scrollBarMoved (juce::ScrollBar* sb, double newRangeStart)
{
    if (sb != &hScrollBar)
        return;

    hOffsetBeats = jmax (0.0, newRangeStart);
    layoutNotes();
    canvas.repaint();
    velocityLane.repaint();
    playhead.repaint();
}

int PianoRollView::playheadX() const
{
    if (clip == nullptr)
        return -1;

    auto& ts = clip->edit.tempoSequence;
    const double edtBeat     = ts.toBeats (clip->edit.getTransport().getPosition()).inBeats();
    const double contentBeat = edtBeat - clipStartBeat();
    const int x = beatToX (contentBeat);

    if (x < PianoRollLayout::gutterW || x > canvas.getWidth())
        return -1;

    return x;
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

        nc->setBounds (x1, y, jmax (2, x2 - x1), jmax (2, keyHeight - 1));
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
    const int yBottom = pitchToY (lowPitch) + keyHeight;
    const int mid     = (yTop + yBottom) / 2;
    const int targetY = jlimit (0, jmax (0, canvasHeight() - viewport.getMaximumVisibleHeight()),
                                mid - viewport.getMaximumVisibleHeight() / 2);

    viewport.setViewPosition (0, targetY);
}

//==============================================================================
void PianoRollView::commitNoteMove (te::MidiNote& note, double newStartBeat, int newPitch)
{
    if (clip == nullptr)
    {
        FORGE_LOG_ERROR ("Cannot commit note move: no MIDI clip is bound to the piano roll");
        return;
    }

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
    {
        FORGE_LOG_ERROR ("Cannot commit note resize: no MIDI clip is bound to the piano roll");
        return;
    }

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
    {
        FORGE_LOG_ERROR ("Cannot commit selection move: no MIDI clip is bound to the piano roll");
        return;
    }

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

void PianoRollView::quantiseSelectionOrClip()
{
    if (clip == nullptr)
    {
        FORGE_LOG_ERROR ("Cannot quantise: no MIDI clip is bound to the piano roll");
        return;
    }

    // Selection if any, else the whole clip; 100% strength (the plain "snap to grid" action — the helper
    // accepts 0..1 for a future strength control). Non-structural: layoutNotes() (NOT rebuildNotes, which
    // clears the selection) repositions each component from its note's new start. One undoable step.
    const bool selectedOnly = ! selection.empty();
    auto* undo = &clip->edit.getUndoManager();

    const int moved = forge::midiedit::quantiseNoteStarts (
        *clip, gridBeats, /*strength*/ 1.0, selectedOnly,
        [this] (te::MidiNote& n) { return isSelected (n); }, undo);

    juce::ignoreUnused (moved);

    layoutNotes();
    velocityLane.repaint();
    notifyMutated();
}

void PianoRollView::nudgeSelection (int direction)
{
    if (clip == nullptr)
    {
        FORGE_LOG_ERROR ("Cannot nudge: no MIDI clip is bound to the piano roll");
        return;
    }

    if (selection.empty())
        return;

    // gridBeats when a snap grid is set; else one bar via the engine's own beats-per-bar idiom
    // (mirrors ProjectSession.cpp's slot-clip content-length derivation) so a nudge with no visible
    // grid still moves by a musically sensible amount instead of a hardcoded 4.
    const double amount = gridBeats > 0.0
        ? gridBeats
        : (double) jmax (1, clip->edit.tempoSequence.getTimeSigAt (te::BeatPosition()).numerator.get());

    auto* undo = &clip->edit.getUndoManager();
    const double applied = forge::midiedit::shiftNoteStarts (selection, (double) direction * amount, undo);
    juce::ignoreUnused (applied);

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
    // Local editing keys apply only with a bound clip; each returns true when it consumes the key.
    if (clip != nullptr)
    {
        // Delete / Backspace -> remove the whole selection in one undo transaction.
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (! selection.empty())
            {
                auto targets = selection;   // snapshot (removeNote + rebuild invalidate the selection)
                auto* undo = &clip->edit.getUndoManager();
                for (auto* n : targets)
                    clip->getSequence().removeNote (*n, undo);

                rebuildNotes();
                notifyMutated();
                return true;
            }
        }

        // Shift+Left / Shift+Right (no Ctrl/Cmd) -> nudge the selection by one grid step (or one bar when
        // no grid step is set). Guard on !isCommandDown so a Cmd/Ctrl-modified chord isn't swallowed here.
        else if (key.getModifiers().isShiftDown() && ! key.getModifiers().isCommandDown()
                 && (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey)))
        {
            if (! selection.empty())
            {
                nudgeSelection (key.isKeyCode (juce::KeyPress::leftKey) ? -1 : +1);
                return true;
            }
        }

        // Ctrl/Cmd + C / V -> copy / paste the selection. (Ctrl/Cmd+Z falls through to the shell below.)
        else if (key.getModifiers().isCommandDown())
        {
            if (key.isKeyCode ('C') || key.isKeyCode ('c')) { copySelection();  return true; }
            if (key.isKeyCode ('V') || key.isKeyCode ('v')) { pasteClipboard(); return true; }
        }

        // Q (no modifier) -> quantise note starts to the grid: the selection, or the whole clip when nothing
        // is selected. Guard on !isCommandDown so Ctrl+Q (File > Exit) propagates instead of being swallowed.
        else if (! key.getModifiers().isCommandDown() && (key.isKeyCode ('Q') || key.isKeyCode ('q')))
        {
            quantiseSelectionOrClip();
            return true;
        }
    }

    // Anything the roll didn't consume -> hand to the shell so app-wide shortcuts (Undo/Redo Ctrl+Z/Y,
    // Save, view/transport toggles) fire from inside the roll regardless of which sub-component holds
    // focus. Mirrors PopoutWindow::onUnhandledKey; the shell returns true only if it acted on the key.
    if (onUnhandledKey && onUnhandledKey (key))
        return true;

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
    const int  w  = bounds.getWidth();
    const int  h  = bounds.getHeight();
    const int  kh = jmax (1, owner.keyHeight);

    g.fillAll (Colour (ForgeLookAndFeel::panelBg));

    // --- Pitch rows: white / black key shading + per-row + octave separators ---------------------
    for (int pitch = 0; pitch < PianoRollLayout::numKeys; ++pitch)
    {
        const int y = owner.pitchToY (pitch);
        if (y + kh < 0 || y > h)
            continue;   // cull rows outside the visible band

        // Black-key rows sit a touch darker than the shell so the white/black banding reads clearly;
        // white-key rows are the raised panel tone.
        g.setColour (isBlackKey (pitch) ? Colour (ForgeLookAndFeel::panelBg).darker (0.25f)
                                        : Colour (ForgeLookAndFeel::raisedBg));
        g.fillRect (PianoRollLayout::gutterW, y, jmax (0, w - PianoRollLayout::gutterW), kh);

        // Row separators in textSec (clearly legible against the dark rows — hairline is too close to
        // raisedBg to read): a faint line under every row + a distinct line on each octave (C) boundary.
        const bool octave = (pitch % 12 == 0);
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (octave ? 0.42f : 0.12f));
        g.fillRect (PianoRollLayout::gutterW, y + kh - 1, jmax (0, w - PianoRollLayout::gutterW), 1);
    }

    // --- Vertical time grid: sub-beat / beat / bar hierarchy -------------------------------------
    if (owner.clip != nullptr && owner.timeAxisWidth() > 0)
    {
        const int    bpb = owner.beatsPerBar();
        const double px  = owner.pxPerBeat;
        const double sub = owner.gridBeats;
        const bool drawSub  = sub > 0.0 && sub * px >= 6.0;   // hide sub-beat lines when too dense
        const bool drawBeat = px >= 5.0;

        const double startBeat = jmax (0.0, owner.hOffsetBeats);
        const double endBeat   = owner.hOffsetBeats + owner.visibleBeats();
        const double step      = (drawSub ? sub : 1.0);

        for (double b = std::floor (startBeat / step) * step; b <= endBeat + step; b += step)
        {
            if (b < -1.0e-6)
                continue;

            const int x = owner.beatToX (b);
            if (x < PianoRollLayout::gutterW || x > w)
                continue;

            const bool onBeat = std::abs (b - std::round (b)) < 1.0e-6;
            const bool onBar  = onBeat && ((long long) std::llround (b) % jmax (1, bpb) == 0);

            // Graduated textSec hierarchy (one colour, alpha carries the rank): bars boldest, then
            // beats, then the faint sub-beat grid — all clearly legible against the dark rows.
            float alpha; int thick;
            if      (onBar)  { alpha = 0.55f; thick = 2; }
            else if (onBeat) { if (! drawBeat) continue; alpha = 0.30f; thick = 1; }
            else             { if (! drawSub)  continue; alpha = 0.12f; thick = 1; }

            g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (alpha));
            g.fillRect (x, 0, thick, h);
        }
    }

    // --- Left keybed gutter: a mini piano (black/white keys) + octave labels ----------------------
    g.setColour (Colour (ForgeLookAndFeel::shellBg));
    g.fillRect (0, 0, PianoRollLayout::gutterW, h);

    for (int pitch = 0; pitch < PianoRollLayout::numKeys; ++pitch)
    {
        const int y = owner.pitchToY (pitch);
        if (y + kh < 0 || y > h)
            continue;

        g.setColour (isBlackKey (pitch) ? Colour (ForgeLookAndFeel::panelBg).darker (0.35f)
                                        : Colour (ForgeLookAndFeel::raisedBg).brighter (0.06f));
        g.fillRect (1, y, PianoRollLayout::gutterW - 2, jmax (1, kh - 1));
    }

    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (PianoRollLayout::gutterW - 1, 0, 1, h);

    g.setFont (FontOptions (9.0f));
    for (int pitch = 0; pitch < PianoRollLayout::numKeys; pitch += 12)
    {
        const int y = owner.pitchToY (pitch);
        if (y + kh < 0 || y > h)
            continue;

        const int octave = pitch / 12 - 1;   // MIDI: note 0 = C-1, note 60 = C4
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("C" + String (octave),
                    Rectangle<int> (2, y, PianoRollLayout::gutterW - 3, jmax (kh, 10)),
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
    // The roll grabs keyboard focus on any grid press so Delete / Copy / Paste / Ctrl+Z land here.
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
    const int pitch = jlimit (0, 127, owner.yToPitch (e.y));

    auto* undo = &owner.clip->edit.getUndoManager();
    owner.clip->getSequence().addNote (pitch,
                                       te::BeatPosition::fromBeats (startBeat),
                                       te::BeatDuration::fromBeats (owner.defaultNoteLenBeats),
                                       100, 0, undo);

    owner.rebuildNotes();
    owner.notifyMutated();
}

void PianoRollView::GridCanvas::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    // The decode itself lives in owner.handleWheel() (a public seam so the routing is testable
    // headlessly, W23 follow-up); a plain wheel (not consumed) forwards to the Viewport for its
    // native vertical (pitch) scroll, exactly as before the extraction.
    if (! owner.handleWheel (e.mods, wheel.deltaX, wheel.deltaY, e.x, e.y))
        owner.viewport.useMouseWheelMoveIfNeeded (e.getEventRelativeTo (&owner.viewport), wheel);
}

//==============================================================================
PianoRollView::PlayheadOverlay::PlayheadOverlay (PianoRollView& o)
    : owner (o)
{
    setInterceptsMouseClicks (false, false);   // never block note editing / scrolling underneath
    startTimerHz (30);
}

void PianoRollView::PlayheadOverlay::paint (Graphics& g)
{
    const int x = owner.playheadX();
    if (x < 0)
        return;

    // timeTempo (the transport-clock colour family), NOT accent — the playhead is a clock element,
    // mirroring the arrange PlayheadComponent; amber accent stays selection-only. A 1px dark edge
    // either side separates the bright 2px line from grid content it crosses.
    g.setColour (Colour (ForgeLookAndFeel::shellBg).withAlpha (0.8f));
    g.fillRect (x - 1, 0, 1, getHeight());
    g.fillRect (x + 2, 0, 1, getHeight());
    g.setColour (Colour (ForgeLookAndFeel::timeTempo).brighter (0.35f));
    g.fillRect (x, 0, 2, getHeight());
}

void PianoRollView::PlayheadOverlay::timerCallback()
{
    const int x = owner.playheadX();
    if (x == lastX)
        return;

    // Repaint only the thin band spanning the old + new positions (both may be -1 = off-screen).
    const int a = (lastX < 0 ? x : lastX);
    const int b = (x < 0 ? lastX : x);
    lastX = x;

    if (a < 0 && b < 0)
        return;

    const int lo = jmin (a < 0 ? b : a, b < 0 ? a : b) - 2;
    const int hi = jmax (a, b) + 3;
    repaint (lo, 0, hi - lo, getHeight());
}
