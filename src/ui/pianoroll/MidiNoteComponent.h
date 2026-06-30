/*
    MidiNoteComponent — one rectangle per te::MidiNote in the piano roll, modelled on
    AudioClipComponent's drag interaction (drag threshold, dragOrigin capture, commit-then-persist).

    The note is held raw (te::MidiNote*): notes live in the MidiList owned by the clip, which
    PianoRollView holds by Ptr and rebuilds on any structural change — so a raw pointer is never
    dereferenced after the note is removed. All edits route back through PianoRollView (snap +
    commit + onEditMutated); the component owns no engine state itself.

    Interaction: left-drag on the body moves the note (horizontally in time, vertically in pitch);
    left-drag in the right-edge zone resizes the note's length; right-click deletes it. A plain
    click never nudges (the move threshold guards it). mouseDown routes the click through
    PianoRollView's selection model (plain click selects only this note; Shift/Ctrl/Cmd toggles it),
    and a body-drag of a selected note moves the WHOLE selection by the dragged note's delta.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class PianoRollView;

//==============================================================================
class MidiNoteComponent : public juce::Component
{
public:
    MidiNoteComponent (PianoRollView& owner, te::MidiNote& note);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    te::MidiNote& getNote() { return note; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const { return selected; }

private:
    /** Width in px of the right-edge zone that begins a length-resize rather than a move. */
    static constexpr int kResizeZone = 6;

    /** True if the pointer (component-local x) is in the right-edge resize zone. */
    bool inResizeZone (int localX) const;

    PianoRollView& owner;
    te::MidiNote& note;
    bool selected = false;

    // Drag state. dragging stays false until the pointer passes a small threshold so a plain click
    // never nudges. The anchor is captured in the canvas (parent) space — stable as this component
    // moves under us during a move — together with the note's start/length/pitch at mouseDown.
    bool dragging  = false;
    bool resizing  = false;                  // chosen at mouseDown from the hit zone
    int  dragAnchorX = 0;                     // mouseDown x in PARENT (canvas) space
    int  dragAnchorY = 0;                     // mouseDown y in PARENT (canvas) space
    double dragOriginStartBeat  = 0.0;
    double dragOriginLengthBeats = 0.0;
    int    dragOriginPitch       = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNoteComponent)
};
