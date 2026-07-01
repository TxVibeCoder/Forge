/*
    TrackColumnComponent — one per-track column in the SessionView clip-launch grid (the twin
    of MixerView's ChannelStrip / ArrangeView's TrackLaneComponent).

    A column renders one AudioTrack's vertical run of clip pads plus its chrome:
      - an inline track HEADER (colour swatch, track name, an Audio/MIDI type tag, the
        instrument chip, and M / S / R toggle buttons),
      - numScenes ClipSlotComponent pads (one per scene), top-to-bottom,
      - a per-track clip-STOP ■ button in the footer row.

    Layout, top to bottom (SessionLayout): removeFromTop(headerH) header · removeFromTop(slotH)
    per pad · removeFromBottom(stopRowH) stop button.

    Like every SessionView component it is MESSAGE-THREAD only and caches NO te::ClipSlot* / Clip*
    (R1) — the pads hold (trackIndex, sceneIndex) only and the parent's poll PUSHES visual state
    in via setSlotVisual(). The bound AudioTrack& is read ONLY for header display (name / colour /
    type / instrument / arm) on the message thread; no engine pointer is cached across repaints.

    All engine mutations bubble UP to SessionView (which routes them through ProjectSession) via
    null-guarded std::function callbacks tagged with this column's trackIndex; the column itself
    never edits the te:: model except the local M/S toggle (mirroring ChannelStrip/TrackLane).
*/

#pragma once

#include <JuceHeader.h>

#include "ui/session/ClipSlotComponent.h"
#include "ui/session/SlotVisualState.h"
#include "ui/session/SessionLayout.h"
#include "ui/ForgeLookAndFeel.h"

namespace te = tracktion;

//==============================================================================
/** One track column: a header, numScenes clip pads, and a clip-stop footer. Holds the track by
    reference (SessionView rebuilds the whole OwnedArray when the Edit/track list changes, so a
    column never outlives its track) and its fixed trackIndex. Caches no engine pointer (R1). */
class TrackColumnComponent : public juce::Component
{
public:
    TrackColumnComponent (te::AudioTrack& track, int trackIndex);

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Push-down API — the parent/poll drives the column's pads and chrome on the message thread.

    /** Pushes a freshly-computed visual state + label into the pad at sceneIndex (no-op if the
        index is out of range). The pad repaints only if its state/label actually changed. */
    void setSlotVisual (int sceneIndex, SlotVisualState state, juce::String label);

    /** Sets the selection / keyboard-focus highlight on the pad at sceneIndex. */
    void setSlotSelected (int sceneIndex, bool shouldBeSelected);

    /** Updates the track colour used by the header swatch and propagated to every pad. */
    void setTrackColour (juce::Colour newColour);

    /** Re-reads the bound track for header display (name, type, instrument chip, arm) and
        refreshes the M/S/R toggle states from engine truth. Message-thread only. */
    void refreshHeader();

    int getTrackIndex() const                { return trackIndex; }
    int getNumSlots() const                  { return slots.size(); }

    //==============================================================================
    // Callbacks bubbled UP to SessionView (set during rebuild()), tagged with this trackIndex.

    /** Left single-click on a pad (parent decides launch vs. create/import). */
    std::function<void (int trackIdx, int sceneIdx)> onSlotClicked;
    /** Left double-click on a pad (parent opens a filled MIDI slot's clip in the drawer). */
    std::function<void (int trackIdx, int sceneIdx)> onSlotDoubleClicked;
    /** Right-click on a pad; param is the event for context-menu placement. */
    std::function<void (int trackIdx, int sceneIdx, const juce::MouseEvent&)> onSlotRightClicked;

    /** The track's clip-stop ■ was clicked — stop every clip on this track. */
    std::function<void (int trackIdx)> onTrackStopAll;

    /** M / S / R toggled — the parent routes mute/solo/arm through ProjectSession / the shell. */
    std::function<void (int trackIdx)> onMute;
    std::function<void (int trackIdx)> onSolo;
    std::function<void (int trackIdx)> onArm;

    /** Authoritative arm state for this track, queried from the engine on every refresh so the
        R button never relies on a stale local flag (set by SessionView during rebuild). */
    std::function<bool (te::AudioTrack&)> isTrackArmed;

private:
    void configureToggle (juce::TextButton&);
    bool trackHasInstrument() const;     // synth / MIDI-input plugin in the chain → MIDI track

    te::AudioTrack& track;
    const int trackIndex;
    juce::Colour trackColour;

    juce::OwnedArray<ClipSlotComponent> slots;   // numScenes pads, top-to-bottom

    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" };
    juce::TextButton stopButton;                 // per-track clip-stop ■ (footer)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackColumnComponent)
};
