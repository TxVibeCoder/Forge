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

    /** Names of the engine-level wave input devices currently known (one per mono channel /
        stereo pair). Rescans first. Useful for diagnostics/logging/UI; empty means no capture
        channels are open (see getLastError() after an arm attempt for the likely reason). */
    juce::StringArray getAvailableInputDeviceNames() const;

    juce::String getLastError() const { return lastError; }

private:
    te::Engine& engine;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordController)
};
