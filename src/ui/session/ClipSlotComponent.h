/*
    ClipSlotComponent — one clip-launch pad in the SessionView grid (the leaf component,
    twin of ArrangeView's ClipComponent).

    A pad renders the chrome for a single cell (trackIndex, sceneIndex) of the tracks×scenes
    grid. Per the load-bearing threading rules (docs/devlog/session-design.md, R1), it holds
    ONLY its grid coordinates, its track colour, a selection flag, the last visual state the
    parent/poll PUSHED into it, and a display label. It NEVER caches a te::ClipSlot* / Clip*
    pointer and NEVER reads the engine itself — the 25 Hz poll resolves a live slot each tick,
    computes a SlotVisualState (SlotVisualState.h), and calls setVisualState() to push it here.

    paint() draws the §(d) pad recipe (rounded laneBg base, track-colour@0.55 fill when the pad
    has a clip, black@0.6 border, textPrim label) using ONLY ForgeLookAndFeel named colours and
    the one sanctioned laneBg literal. The outline vocabulary is semantic (W04a): a play-family
    ring — playGreen while playing, playGreenDim while queued/stopping — whose alpha is the
    beat pulse pushed in via setPulseAlpha() by the parent's poll (derived from the transport,
    never a free-running animation); AMBER is reserved for the selection / keyboard-focus cursor.

    Mouse intent is surfaced through null-guarded std::function callbacks; the parent (which knows
    the resolved engine state) disambiguates launch vs. create vs. open.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/session/SlotVisualState.h"
#include "ui/session/SessionLayout.h"
#include "ui/ForgeLookAndFeel.h"

//==============================================================================
/** A single clip-launch pad. Holds (trackIndex, sceneIndex) only — no cached engine pointer
    (R1). Its display state is pushed in via setVisualState(); it never re-reads the engine.

    Also an OS-external file-drop target (W07): dragging an audio file from Explorer onto the pad
    imports it into this slot (mirrors Tracktion's ClipLauncherDemo per-slot recipe). The drop
    intent bubbles UP via onFilesDropped — the pad never touches the te:: model itself. */
class ClipSlotComponent : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    ClipSlotComponent (int trackIndex, int sceneIndex, juce::Colour trackColour);

    /** Draws the pad chrome for the last-pushed SlotVisualState (§d). */
    void paint (juce::Graphics&) override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    //==============================================================================
    // OS-external file drop (juce::FileDragAndDropTarget, W07). Audio-only: isInterestedInFileDrag
    // filters to te::soundFileExtensions so a non-audio drag is rejected before any callback fires.
    // The dragHover highlight is set on enter and CLEARED in every exit path (exit AND drop — JUCE
    // may not fire fileDragExit after a drop, so filesDropped clears it too).
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

    /** Stores the pushed state + label and repaints, but ONLY if either changed. Called by the
        parent's poll on the message thread with a state derived from a freshly-resolved slot. */
    void setVisualState (SlotVisualState newState, juce::String label);

    /** Pushes the beat-pulse alpha for the play-family ring (padPulseAlpha output). Negative =
        no pulse flowing (transport stopped / pad not animated): paint falls back to the state's
        static peak. Repaints ONLY when the value changes, so the parent may push every tick —
        stopped pads (parked at the sentinel) stay repaint-free (§e repaint discipline). */
    void setPulseAlpha (float newPulseAlpha);

    /** Selection / keyboard-focus flag — rendered as the 2px AMBER cursor outline (§d; W04a:
        amber is selection-only, inset to an inner ring when a play-family ring is showing). */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const                  { return selected; }

    /** Updates the pad's track colour (e.g. after a track recolour) and repaints if changed. */
    void setTrackColour (juce::Colour newColour);

    int getTrackIndex() const                { return trackIndex; }
    int getSceneIndex() const                { return sceneIndex; }
    SlotVisualState getVisualState() const    { return state; }

    /** Fired on a left single-click (the parent decides: launch a filled slot, or create/import
        into an empty one). */
    std::function<void()> onClicked;
    /** Fired on a plain left mouse-UP (non-popup) — drives Gate launch-mode "stop on release". Null => no-op. */
    std::function<void()> onReleased;
    /** Fired on a left double-click (the parent opens a filled MIDI slot's clip in the drawer). */
    std::function<void()> onDoubleClicked;
    /** Fired on a right-click; param is the event for context-menu placement. */
    std::function<void (const juce::MouseEvent&)> onRightClicked;
    /** Fired when an OS-external audio file is dropped on the pad (W07): the FIRST accepted file.
        The parent (which owns the ProjectSession seam) imports it into this slot. */
    std::function<void (const juce::File&)> onFilesDropped;

private:
    // R1: coordinates only — never a te::ClipSlot* / Clip*.
    const int trackIndex;
    const int sceneIndex;

    juce::Colour trackColour;
    bool selected = false;

    SlotVisualState state = SlotVisualState::empty;   // last state pushed by the parent/poll
    juce::String label;                               // clip name (or empty)
    float pulseAlpha = -1.0f;                         // beat-pulse ring alpha; negative = no pulse (static render)
    bool dragHover = false;                           // W07: an accepted file drag is hovering this pad

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipSlotComponent)
};
