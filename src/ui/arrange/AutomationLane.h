/*
    AutomationLane — a per-track automation sub-lane for the Arrange view.

    Renders one automatable parameter of its track (Volume or Pan, picked by a compact selector
    in the header column) as a point-to-point polyline across the SAME TimelineView time axis the
    clip lanes use, so curve / clip / playhead alignment is pixel-exact. Collapsed by default;
    ArrangeView creates one lane per track on rebuild() and shows it directly below the track
    lane when the header's A button expands it.

    Value mapping: volume 0..1 maps bottom..top; pan -1..+1 maps bottom..top with a subtle
    horizontal centre line at 0.

    Interactions: left-click on the empty body adds a point at the cursor time/value; dragging a
    square handle moves its point (value clamped to the parameter range, time clamped to >= 0);
    right-click on a handle offers Delete Point; right-click on the body offers Clear Automation
    and the Volume/Pan switch. All mutations go through AutomationHelpers (each helper commits
    the interpolated stream synchronously); onEditMutated fires once per completed gesture so
    the shell's autosave path runs exactly like clip edits.

    Engine-pointer discipline (the R1-class rule): the lane caches NO raw engine pointers — it
    holds the track reference for its own lifetime (identical to TrackLaneComponent, both
    recreated by every ArrangeView::rebuild()) and re-resolves the AutomatableParameter from the
    track on every use, so a structural edit can never leave a dangling parameter behind. The one
    deliberate exception is `listenedParam`, a REF-COUNTED Ptr held only so the curve-change
    listener can be unregistered — it cannot dangle by construction (same class of exception as
    DetailView's Clip::Ptr).

    External curve changes are reflected live: the lane registers as the shown parameter's
    listener and repaints on curveHasChanged (message-thread). This also makes the ENGINE'S
    single-point semantics visible rather than silent — with exactly one point on a curve,
    Tracktion intentionally MOVES that point when the parameter value is set directly (e.g. a
    mixer fader gesture), because one point and a static value are the same statement. Forge
    accepts those semantics; the lane simply shows the point move as it happens.

    Message-thread only. No timers, no polling; repaints after its own edits, on edge-gated hover
    changes, and on listener-reported external curve changes.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

struct TimelineView;   // shared edit-time <-> pixel mapping, defined in ui/arrange/ArrangeView.h

//==============================================================================
class AutomationLane : public juce::Component,
                        private te::AutomatableParameter::Listener
{
public:
    /** The automatable parameter shown in the lane. Numeric values back the selector's item IDs
        (id = (int) value + 1) and ArrangeView's per-track view state, so do not reorder. */
    enum class Param { volume = 0, pan = 1 };

    AutomationLane (TimelineView&, te::AudioTrack&, Param initialParam);
    ~AutomationLane() override;

    Param getParam() const { return param; }

    te::AudioTrack& getTrack() { return track; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    //==============================================================================
    // Callbacks bubbled up to ArrangeView (set during rebuild()).

    /** Invoked once per completed mutation gesture (point added / drag released / point deleted /
        curve cleared), so the shell can save. */
    std::function<void()> onEditMutated;
    /** Invoked when the selector switches parameter, so the owner can persist the choice across
        rebuild() (the lane component itself is recreated). */
    std::function<void (Param)> onParamChanged;

private:
    /** Re-resolves the shown parameter from the track (never cached — see the file header).
        Returns a null Ptr when the track has no volume/pan plugin. */
    te::AutomatableParameter::Ptr resolveParam() const;

    /** The curve area: everything right of the fixed header column. */
    juce::Rectangle<int> bodyArea() const;

    /** Value range of the shown parameter (volume 0..1 slider position, pan -1..+1). */
    juce::Range<float> paramRange() const;

    /** Parameter value -> lane y (bottom..top maps range-low..range-high, with a small vertical
        inset so extreme-value handles stay fully visible). */
    float valueToY (float value, const juce::Rectangle<int>& body) const;
    /** Lane y -> parameter value, clamped to paramRange(). */
    float yToValue (int y, const juce::Rectangle<int>& body) const;

    /** The square handle rect for the curve point at `index`, in lane-local coordinates. */
    juce::Rectangle<int> handleRect (te::AutomatableParameter&, int index,
                                     const juce::Rectangle<int>& body) const;

    /** Index of the handle under `pos` (with a small grab margin), or -1 for none. */
    int hitTestHandle (juce::Point<int> pos) const;

    /** Switches the shown parameter via the selector so the ComboBox state and the switch logic
        stay on a single path (the onChange handler no-ops when unchanged). */
    void selectParam (Param);

    void showHandleContextMenu (int pointIndex);
    void showLaneContextMenu();

    void notifyEditMutated();

    /** te::AutomatableParameter::Listener — the engine reports curve edits (ours AND external
        ones: a mixer-fader single-point move, MIDI-learn, undo) on the message thread. */
    void curveHasChanged (te::AutomatableParameter&) override;

    /** Re-targets the curve-change listener at the currently shown parameter (ctor, param
        switch). The old registration is removed first; the dtor removes the final one. */
    void refreshCurveListener();

    TimelineView& view;
    te::AudioTrack& track;
    Param param = Param::volume;

    // Ref-counted listener target — the documented exception to the no-cached-pointers rule (it
    // exists only so removeListener has a guaranteed-alive object to run against).
    te::AutomatableParameter::Ptr listenedParam;

    juce::ComboBox paramSelector;   // compact Volume | Pan picker in the header column

    int dragIndex  = -1;       // point index being dragged (tracks movePoint's returned index)
    int hoverIndex = -1;       // handle under the pointer (edge-gated repaint, no polling)
    bool dragMutated = false;  // gesture touched the curve; fire onEditMutated once on mouseUp

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLane)
};
