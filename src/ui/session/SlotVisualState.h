/*
    SlotVisualState — the single shared pad-state model for the SessionView clip grid.

    This is the contract every clip pad renders from AND the model a future hardware
    control-surface driver (Launchpad / APC40) reuses unchanged: "add a driver, not a
    rework." It defines:

      - enum class SlotVisualState — the discrete pad states (empty / has-clip / queued /
        playing / stopping / rec-armed), per docs/devlog/session-design.md §(d).

      - computeSlotState(...) — the ONE pure function that derives a SlotVisualState from a
        live te::ClipSlot* by reading the clip's LaunchHandle. Called ONLY on the message
        thread inside the 25 Hz poll (§e), with a slot pointer resolved fresh that same tick
        — the pointer is never stored (R1). getQueuedStatus() is spin_mutex-guarded, so the
        caller gates its use (transport-running) and this function additionally only reads it
        when a launch handle exists; it is never invoked for empty/stopped pads.

      - PadFeedback + toPadFeedback(...) — the (colourIdx, state) LED encoding identical to
        the future ControlSurface padStateChanged model (colourIdx 0=off, 1-18=hue;
        state 0=solid, 1=blink, 2=pulse), so the same state drives screen pixels and LEDs.

    Header-only (#pragma once, inline). Self-contained — any component may include it.
    <JuceHeader.h> brings in the tracktion engine types.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
/** The discrete visual state of a single clip-launch pad (§d).
    One source of truth for on-screen pad chrome and future LED feedback. */
enum class SlotVisualState
{
    empty,      // slot has no clip
    hasClip,    // clip present, stopped, not queued
    queued,     // clip queued to start playing (playQueued)
    playing,    // clip playing, not queued
    stopping,   // clip queued to stop (stopQueued)
    recArmed,   // track is record-armed (derived from track arm, not the launch handle)
    recording   // this slot is the one currently CAPTURING MIDI (dominates all clip/queue states)
};

//==============================================================================
/** Derives the pad state for a slot on the message thread.

    @param slot              the live ClipSlot resolved fresh this tick (may be null); NEVER stored.
    @param transportRunning  whether the edit transport is running. Queue state is only meaningful
                             while playing, so getQueuedStatus() (spin_mutex-guarded) is read only
                             when true — empty/stopped pads skip the lock entirely (§e).
    @param armed             whether the owning track is record-armed.
    @param recordingHere     whether THIS slot is the one currently capturing MIDI. DOMINATES all
                             clip/queue/arm states — checked FIRST so a mid-capture pad reads "hot"
                             regardless of any clip that may already resolve in the slot.

    Mapping (§d): recording dominates (checked first). Otherwise empty when no clip. With a clip,
    queued/stopping take priority over the play state (only read while transportRunning); otherwise
    playing vs has-clip. rec-armed applies only to an empty pad on an armed track (an armed track
    with a clip still shows its clip state).
*/
inline SlotVisualState computeSlotState (te::ClipSlot* slot, bool transportRunning,
                                         bool armed, bool recordingHere)
{
    // recording DOMINATES every other state for this one pad (§1d): checked FIRST.
    if (recordingHere)
        return SlotVisualState::recording;

    if (slot == nullptr)
        return armed ? SlotVisualState::recArmed : SlotVisualState::empty;

    auto* clip = slot->getClip();

    if (clip == nullptr)
        return armed ? SlotVisualState::recArmed : SlotVisualState::empty;

    // A clip is present. Read its launch handle (may be null — e.g. a non-launchable clip type).
    if (auto handle = clip->getLaunchHandle())
    {
        // Gate the spin_mutex-guarded queue read: only while the transport is running, where
        // queue states are meaningful (play(nullopt) is immediate when stopped). (§e)
        if (transportRunning)
        {
            if (auto queued = handle->getQueuedStatus())
            {
                switch (*queued)
                {
                    case te::LaunchHandle::QueueState::playQueued:  return SlotVisualState::queued;
                    case te::LaunchHandle::QueueState::stopQueued:  return SlotVisualState::stopping;
                    default:                                        break;
                }
            }
        }

        if (handle->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
            return SlotVisualState::playing;
    }

    return SlotVisualState::hasClip;
}

//==============================================================================
/** Hardware-driver-ready LED descriptor for one pad (§d).

    @param trackIndex  grid column.
    @param sceneIndex  grid row.
    @param colourIdx   0 = off; 1-18 = hue index (track hue, or a reserved hue for rec-armed).
    @param state       0 = solid; 1 = blink; 2 = pulse.

    Plain ints so a future ControlSurface driver can emit identical LEDs without depending on
    JUCE/engine types beyond this header. */
struct PadFeedback
{
    int trackIndex = 0;
    int sceneIndex = 0;
    int colourIdx  = 0;   // 0 = off, 1-18 = hue
    int state      = 0;   // 0 = solid, 1 = blink, 2 = pulse
};

//==============================================================================
/** Maps a SlotVisualState (+ the pad's track colour) to the (colourIdx, state) LED encoding.

    colourIdx: empty → 0 (off); rec-armed → a reserved red hue; every clip state → a hue
    derived from the track colour, quantised into the 1-18 palette by JUCE hue.
    state: solid for has-clip / rec-armed; blink for queued / stopping; pulse for playing.

    This is the exact vocabulary the future padStateChanged LED model pushes, so a hardware
    driver reuses it unchanged. */
inline PadFeedback toPadFeedback (int trackIndex, int sceneIndex,
                                  SlotVisualState state, juce::Colour trackColour)
{
    PadFeedback fb;
    fb.trackIndex = trackIndex;
    fb.sceneIndex = sceneIndex;

    // Quantise the track colour's hue into the 1-18 LED hue palette (1-based; 0 is reserved off).
    auto trackHueIdx = [trackColour]() -> int
    {
        const int hue = juce::jlimit (0, 17, (int) (trackColour.getHue() * 18.0f));
        return hue + 1;   // 1..18
    };

    constexpr int redHueIdx = 1;   // reserved hue for rec-armed (red end of the palette)

    switch (state)
    {
        case SlotVisualState::empty:     fb.colourIdx = 0;             fb.state = 0; break;  // off, solid
        case SlotVisualState::hasClip:   fb.colourIdx = trackHueIdx(); fb.state = 0; break;  // hue, solid
        case SlotVisualState::queued:    fb.colourIdx = trackHueIdx(); fb.state = 1; break;  // hue, blink
        case SlotVisualState::playing:   fb.colourIdx = trackHueIdx(); fb.state = 2; break;  // hue, pulse
        case SlotVisualState::stopping:  fb.colourIdx = trackHueIdx(); fb.state = 1; break;  // hue, blink
        case SlotVisualState::recArmed:  fb.colourIdx = redHueIdx;     fb.state = 0; break;  // red, solid
        case SlotVisualState::recording: fb.colourIdx = redHueIdx;     fb.state = 2; break;  // red, pulse
        default:                         fb.colourIdx = 0;             fb.state = 0; break;
    }

    return fb;
}
