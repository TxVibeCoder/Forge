/*
    StepGridView — the drawer-hosted step-sequencer editor for a single te::StepClip, the drum
    analogue of PianoRollView. When a step clip is selected the shell calls setStepClip(c); this
    binds the clip and paints a drag-to-toggle grid: one ROW per channel (drum lane), one COLUMN
    per pattern step. A left gutter shows each channel's display name; the remaining width is the
    step grid, sized proportionally to fill the bounds.

    Interaction (Ableton-style "paint"): mouseDown on a cell flips it and seeds paintValue with the
    cell's NEW state; a drag paints that same value onto every cell the pointer crosses, so one
    gesture lays down (or erases) a run of steps. Each cell that actually changes pushes
    clip->setCell (an undoable engine write) and fires onEditMutated() so the shell seals the undo
    transaction + saves; crossing an already-correct cell is a no-op (no undo flood).

    R1 discipline (mirrors the SessionView rule in CLAUDE.md): the ONLY engine handle cached is the
    te::StepClip*. Rows, columns, channel names and cell states are re-resolved from the live clip
    on every paint and every gesture — a Channel* / Pattern is NEVER stored (both are transient
    ValueTree views).

    Lifetime: StepClip is a juce::ChangeBroadcaster; setStepClip (de)registers this as a listener,
    and the destructor drops the registration FIRST so the view never outlives / dangles off the
    broadcaster. changeListenerCallback repaints, so external edits / undo / redo are reflected.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
class StepGridView : public juce::Component,
                     private juce::ChangeListener
{
public:
    StepGridView();
    ~StepGridView() override;

    /** Binds the grid to `clip` (nullptr = empty state). Registers as a ChangeListener on the clip
        (StepClip is a juce::ChangeBroadcaster) and deregisters from any previous clip. Caches ONLY
        the te::StepClip* — never a Channel* / Pattern (those are transient ValueTree views;
        re-resolve each paint/gesture). Message-thread only. */
    void setStepClip (te::StepClip* clip);

    /** The currently bound clip (or nullptr) — for the shell's reconcileDrawerClip staleness check. */
    te::StepClip* getStepClip() const noexcept;

    /** Fired AFTER a cell toggle mutates the clip, so the shell seals the undo transaction + saves.
        Null-guarded by the owner-facing convention. */
    std::function<void()> onEditMutated;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    //==============================================================================
    // Drag-to-toggle "paint" gesture (Ableton-style). Pointer handlers are JUCE-called, not part of
    // the shell-facing surface; kept private so the public API matches the frozen contract exactly.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    /** Applies paintValue to (channelIndex, stepIndex); writes setCell + fires onEditMutated ONLY on
        a real value change, and records the cell as last-painted so the drag never re-fires on it. */
    void paintCell (int channelIndex, int stepIndex);

    /** Fires onEditMutated if a listener is attached. */
    void notifyMutated();

    //==============================================================================
    // Held as a ref-counted Ptr (NOT a raw pointer), exactly like PianoRollView's MidiClip::Ptr: a slot
    // clip is refcounted, so deleting it drops the list's ref and would SYNCHRONOUSLY free a raw pointer,
    // leaving the shell's reconcileDrawerClip (which reads clip->state) + our own removeChangeListener
    // dereferencing freed memory (a QC-caught UAF). The Ptr keeps a deleted clip alive-but-PARENTLESS so
    // the parent-loss reconcile is safe. Still R1: R1 forbids caching a transient Channel*/Pattern, not a
    // refcounted handle to the clip itself — re-resolve channels/pattern/cells fresh every paint/gesture.
    te::StepClip::Ptr clip;   // the ONLY engine handle cached (bound); re-resolve everything else

    // Paint-drag state: seeded on mouseDown, applied through the drag, cleared on mouseUp / rebind.
    bool painting   = false;        // a paint drag is in progress
    bool paintValue = false;        // the on/off value being painted for the whole drag
    int  lastPaintedChannel = -1;   // last cell touched this drag (so we never re-fire on the same cell)
    int  lastPaintedStep    = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGridView)
};
