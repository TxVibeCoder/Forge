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

    // Fill alpha tracks velocity so the grid reads as dynamics: soft notes are dimmer, loud notes
    // near solid. Selection overrides to fully opaque so the selected set stays legible regardless.
    const float vel   = jlimit (1.0f, 127.0f, (float) note.getVelocity()) / 127.0f;
    const float alpha = selected ? 1.0f : (0.45f + 0.5f * vel);

    g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (alpha));
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
    // Take keyboard focus on the roll so Delete / Copy / Paste land here after a note click.
    owner.grabKeyboardFocus();

    if (e.mods.isPopupMenu())
    {
        // Right-click deletes this note, or the whole selection if this note is part of one
        // (PianoRollView rebuilds, so don't touch `note` afterwards).
        owner.deleteNoteOrSelection (note);
        return;
    }

    // Route the click through the owner's selection model: Shift / Ctrl / Cmd toggles this note in
    // or out of the set (multi-select); a plain click selects only this note.
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
        owner.toggleSelect (note);
    else if (! owner.isSelected (note))
        owner.selectOnly (note);

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
        // Live preview of a move: convert the pixel delta to a (beat, pitch) delta off the dragged
        // note's origin, then let the owner reposition the WHOLE selection by that same delta (the
        // dragged note is always in the selection — mouseDown selected it). Commit re-snaps on up.
        const double newStartBeat = owner.xToBeat (pointerX) - (owner.xToBeat (dragAnchorX) - dragOriginStartBeat);
        const int    newPitch     = PianoRollView::yToPitch (pointerY);
        const double beatDelta  = newStartBeat - dragOriginStartBeat;
        const int    pitchDelta = newPitch - dragOriginPitch;
        owner.previewMoveSelection (note, beatDelta, pitchDelta);
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
        // Commit the whole selection by the dragged note's (beat, pitch) delta. The owner snaps the
        // dragged note's start and applies the SAME beat delta to the rest (see commitMoveSelection).
        const double newStartBeat = owner.xToBeat (pointerX) - (owner.xToBeat (dragAnchorX) - dragOriginStartBeat);
        const int    newPitch     = PianoRollView::yToPitch (pointerY);
        owner.commitMoveSelection (note, newStartBeat - dragOriginStartBeat, newPitch - dragOriginPitch);
    }
}
