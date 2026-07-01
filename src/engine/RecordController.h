/*
    RecordController — encapsulates the Tracktion recording recipe for an Edit:
    enable the hardware wave inputs, allocate the playback context, and assign + arm the
    first available wave input to a target audio track. Start/stop is driven by the
    transport (record(false) / stop(false,false)).

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class RecordController
{
public:
    explicit RecordController (te::Engine&);

    /** Number of engine-level wave input devices (one per mono channel / stereo pair). */
    int getInputDeviceCount() const;

    /** Enables all wave inputs with automatic monitoring (audible only while armed).
        Rescans the wave device list first so the count reflects the current hardware. */
    void enableInputs();

    /** Allocates the context, assigns + arms the first wave input to `track`.
        Returns true on success; getLastError() describes a failure. */
    bool armFirstInputToTrack (te::Edit&, te::AudioTrack&);

    /** The inverse of armFirstInputToTrack: removes any wave-input recording assignment from
        `track`. For every wave InputDeviceInstance targeting the track it clears the
        recording-enabled flag and drops the target, then restarts playback if anything changed.
        Returns true if the track ends up with no wave input targeting it — including the case
        where it was never armed; returns false only if a target could not be removed, with
        getLastError() describing why. */
    bool disarmTrack (te::Edit&, te::AudioTrack&);

    /** True iff some (enabled) wave InputDeviceInstance currently targets `track` for recording.
        This is the authoritative arm state held in the Edit, so the UI can re-derive a lane's
        arm indicator after any rebuild instead of trusting a stale local flag. */
    bool isTrackArmed (te::Edit&, te::AudioTrack&) const;

    /** Names of the engine-level wave input devices currently known (one per mono channel /
        stereo pair). Rescans first. Useful for diagnostics/logging/UI; empty means no capture
        channels are open (see getLastError() after an arm attempt for the likely reason). */
    juce::StringArray getAvailableInputDeviceNames() const;

    // ---- MIDI enable (NOT a clone of enableInputs: no setStereoPair; rescanMidiDeviceList, not dispatch) ----
    /** Enables every MIDI-in device with automatic monitoring, then async-rescans the MIDI list.
        Iterates dm.getMidiInDevices() (std::shared_ptr<MidiInputDevice>); setEnabled(true) +
        setMonitorMode(MonitorMode::automatic) on each; then dm.rescanMidiDeviceList() (ASYNC). MUST run
        AND be allowed to settle (yield) BEFORE any arm, so the isEnabled()&&isAvailableToEdit() gate in
        EditPlaybackContext::getAllInputs() admits the instance. Message-thread only. */
    void enableMidiInputs();

    /** Count of engine MIDI-in devices (rescans async first; treat as diagnostic — re-query after a yield). */
    int getMidiInputDeviceCount() const;

    /** Names of known MIDI-in devices (diagnostics/UI). Rescans first. */
    juce::StringArray getAvailableMidiInputDeviceNames() const;

    // ---- ARM: slot is the primary W7 target (VERDICT A) ----
    /** Arms the first enabled MIDI input to RECORD INTO `slot`. Picks the first MIDI
        InputDeviceInstance whose device type is in {physicalMidiDevice, virtualMidiDevice,
        trackMidiDevice} and isEnabled(); setTarget(slot.itemID, moveToTrack=false, &um, index) —
        additive so a slot arm does not wipe a co-existing arm; GUARDs the returned value is non-null (a
        nullptr success means the itemID did not resolve to a ClipSlot); setRecordingEnabled(slot.itemID,
        true); restartPlayback(). Returns true on success; sets getLastError() on failure. Message-thread
        only. MUST be called while the transport is STOPPED (setTarget fails while recording). */
    bool armFirstMidiInputToSlot (te::Edit&, te::ClipSlot&);

    /** VERDICT-(B) fallback / plain "record to track" MIDI variant: arms MIDI to the whole track
        (setTarget(track.itemID, moveToTrack=true, …) — exclusive, audio-parity). */
    bool armFirstMidiInputToTrack (te::Edit&, te::AudioTrack&);

    // ---- DISARM (unconditional removeTarget; REQUIRES transport stopped) ----
    /** Clears the MIDI record assignment for `slot`. For EVERY MIDI instance (even disabled):
        setRecordingEnabled(slot.itemID,false) then removeTarget(slot.itemID,&um) UNCONDITIONALLY (never
        gated on getTargets().contains — disabled devices hide targets). restartPlayback only if a live
        target was removed. PRECONDITION: transport stopped — setTarget/removeTarget fail while
        isRecording(). */
    bool disarmSlot (te::Edit&, te::ClipSlot&);

    /** MIDI track-disarm variant of disarmSlot (used by the track-target arm path). */
    bool disarmMidiTrack (te::Edit&, te::AudioTrack&);

    /** Authoritative arm read (re-derivable every 25 Hz poll): true iff some MIDI InputDeviceInstance has
        slot.itemID / track.itemID in getTargets(). Filters getDeviceType() to the MIDI group. Pure read. */
    bool isSlotMidiArmed  (te::Edit&, te::ClipSlot&) const;
    bool isTrackMidiArmed (te::Edit&, te::AudioTrack&) const;

    juce::String getLastError() const { return lastError; }

private:
    te::Engine& engine;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordController)
};
