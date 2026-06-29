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

    /** Enables all wave inputs with automatic monitoring (audible only while armed). */
    void enableInputs();

    /** Allocates the context, assigns + arms the first wave input to `track`.
        Returns true on success; getLastError() describes a failure. */
    bool armFirstInputToTrack (te::Edit&, te::AudioTrack&);

    juce::String getLastError() const { return lastError; }

private:
    te::Engine& engine;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordController)
};
