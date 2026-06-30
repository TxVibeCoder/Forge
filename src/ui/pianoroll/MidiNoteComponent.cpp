#include "ui/pianoroll/MidiNoteComponent.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

//==============================================================================
MidiNoteComponent::MidiNoteComponent (PianoRollView& o, te::MidiNote& n)
    : owner (o), note (n)
{
    setMouseCursor (MouseCursor::NormalCursor);
}

void MidiNoteComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

bool MidiNoteComponent::inResizeZone (int localX) const
{
    return localX >= getWidth() - kResizeZone;
}

//==============================================================================
void MidiNoteComponent::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (selected ? 1.0f : 0.85f));
    g.fillRoundedRectangle (r, 2.0f);

    g.setColour (Colour (selected ? ForgeLookAndFeel::textPrim : ForgeLookAndFeel::hairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
}

void MidiNoteComponent::mouseMove (const MouseEvent& e)
{
    // Right-edge zone offers a horizontal resize cursor; the body offers the default move cursor.
    setMouseCursor (inResizeZone (e.x) ? MouseCursor::LeftRightResizeCursor
                                       : MouseCursor::NormalCursor);
}

void MidiNoteComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // Right-click deletes the note (PianoRollView rebuilds, so don't touch `note` afterwards).
        owner.deleteNote (note);
        return;
    }

    setSelected (true);   // selection is purely visual (the accent fill); no engine state here.

    // Decide move-vs-resize once, at mouseDown, from the hit zone. Capture the anchor in PARENT
    // (canvas) space so the live move — which shifts this component — doesn't move the anchor under
    // us, mirroring AudioClipComponent's parent-relative anchor.
    resizing = inResizeZone (e.x);
    dragging = false;

    if (auto* parent = getParentComponent())
    {
        const auto pe = e.getEventRelativeTo (parent);
        dragAnchorX = pe.x;
        dragAnchorY = pe.y;
    }
    else
    {
        dragAnchorX = e.x;
        dragAnchorY = e.y;
    }

    dragOriginStartBeat   = note.getStartBeat().inBeats();
    dragOriginLengthBeats = note.getLengthBeats().inBeats();
    dragOriginPitch       = note.getNoteNumber();
}

void MidiNoteComponent::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    auto* parent = getParentComponent();
    const int pointerX = (parent != nullptr) ? e.getEventRelativeTo (parent).x : e.x;
    const int pointerY = (parent != nullptr) ? e.getEventRelativeTo (parent).y : e.y;

    if (! dragging)
    {
        // Require a small movement before treating this as a drag (avoids nudging on a click).
        if (std::abs (pointerX - dragAnchorX) < 3 && std::abs (pointerY - dragAnchorY) < 3)
            return;

        dragging = true;
    }

    if (resizing)
    {
        // Live preview of a length change: derive the candidate end-beat from the pointer x.
        const double endBeat = owner.xToBeat (pointerX);
        const double len = juce::jmax (0.0625, endBeat - dragOriginStartBeat);
        const int x1 = owner.beatToX (dragOriginStartBeat);
        const int x2 = owner.beatToX (dragOriginStartBeat + len);
        setBounds (x1, getY(), juce::jmax (2, x2 - x1), getHeight());
    }
    else
    {
        // Live preview of a move: convert the pixel delta to a beat delta, and the absolute pointer
        // y to a pitch row, then reposition the component (the commit re-snaps on mouseUp).
        const double startBeat = owner.xToBeat (pointerX) - (owner.xToBeat (dragAnchorX) - dragOriginStartBeat);
        const int pitch = juce::jlimit (0, 127, PianoRollView::yToPitch (pointerY));
        const int x1 = owner.beatToX (juce::jmax (0.0, startBeat));
        const int x2 = owner.beatToX (juce::jmax (0.0, startBeat) + dragOriginLengthBeats);
        setBounds (x1, PianoRollView::pitchToY (pitch), juce::jmax (2, x2 - x1), getHeight());
    }
}

void MidiNoteComponent::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! dragging)
        return;

    dragging = false;

    auto* parent = getParentComponent();
    const int pointerX = (parent != nullptr) ? e.getEventRelativeTo (parent).x : e.x;
    const int pointerY = (parent != nullptr) ? e.getEventRelativeTo (parent).y : e.y;

    if (resizing)
    {
        const double endBeat = owner.xToBeat (pointerX);
        owner.commitNoteResize (note, juce::jmax (0.0625, endBeat - dragOriginStartBeat));
    }
    else
    {
        const double startBeat = owner.xToBeat (pointerX) - (owner.xToBeat (dragAnchorX) - dragOriginStartBeat);
        const int pitch = juce::jlimit (0, 127, PianoRollView::yToPitch (pointerY));
        owner.commitNoteMove (note, juce::jmax (0.0, startBeat), pitch);
    }
}
