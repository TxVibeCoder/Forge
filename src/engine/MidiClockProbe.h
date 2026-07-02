/*
    MidiClockProbe — a SELFTEST-ONLY capture subclass of te::MidiOutputDevice used by the headless
    --selftest-sync gate to prove MIDI-clock output end-to-end WITHOUT any external hardware or a
    logic-analyzer.

    Why a subclass works (all source-verified against the vendored engine):
      - MidiOutputDevice::sendMessageNow is a protected VIRTUAL (tracktion_MidiOutputDevice.h:87) —
        the exact seam that writes each MIDI byte to the wire, downstream of the real playback graph
        (EditNodeBuilder includes a clock-only device), the MidiClockGenerator (24 PPQN), and the
        MidiNoteDispatcher's 1 ms hi-res timer. Overriding it captures the ACTUAL clock messages the
        engine produced — a hollow pass is impossible because clock generation is gated on the
        device's juce::MidiOutput actually opening (MidiOutputDeviceInstance::prepareToPlay
        early-returns when outputDevice == nullptr), which cannot be faked.
      - The public ctor MidiOutputDevice(Engine&, MidiDeviceInfo) and the protected `outputDevice`
        member (a std::unique_ptr<juce::MidiOutput>, tracktion_MidiOutputDevice.h:102) are both
        reachable from a Forge-side subclass, so the probe can wrap a REAL system MIDI out identifier
        and expose whether the port actually opened.

    In-source precedent: HostedMidiOutputDevice (tracktion_HostedAudioDevice.cpp:257-312) subclasses
    MidiOutputDevice and overrides sendMessageNow the same way — this validates the seam.

    THREADING: sendMessageNow is invoked from the MidiNoteDispatcher's high-resolution timer thread,
    NOT the message thread. The capture log is therefore guarded by a juce::CriticalSection and read
    back via snapshot() under that same lock (project gotcha: use ScopedLockType, not a bare
    ScopedLock, which is the global alias — see CLAUDE.md). No logging (FORGE_LOG_*) happens in
    sendMessageNow: it runs on a timer thread and is effectively a hot path, so the selftest surfaces
    its findings from the message thread after capture, never from inside the override.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
/** Selftest-only MIDI output device that records every message the engine sends to it. */
class MidiClockProbeDevice : public te::MidiOutputDevice
{
public:
    MidiClockProbeDevice (te::Engine& e, juce::MidiDeviceInfo info)
        : te::MidiOutputDevice (e, std::move (info))
    {
    }

    /** True iff the underlying juce::MidiOutput actually opened. This is the HONEST open-check:
        the base openDevice() returns an empty (success-looking) string when the device is not
        enabled, so the return value can't distinguish "opened" from "skipped" — but a null
        outputDevice unambiguously means clock generation is gated OFF. */
    bool isOpen() const noexcept                { return outputDevice != nullptr; }

    /** Selftest-only: force the device enabled WITHOUT the setEnabled() path, which triggers a
        DeviceManager MIDI rescan that would swap this probe out of dm.midiOutputs. The enabled
        flag is a protected OutputDevice member (tracktion_OutputDevice.h:43), reachable here. */
    void forceEnabledForSelfTest() noexcept     { enabled = true; }

    /** Copies the captured message log out under the capture lock, so the caller (message thread)
        never touches the vector while the dispatcher timer thread may still be appending to it. */
    std::vector<juce::MidiMessage> snapshot() const
    {
        const juce::CriticalSection::ScopedLockType sl (captureLock);
        return capturedMessages;   // copy under lock
    }

protected:
    /** Capture seam: runs on the MidiNoteDispatcher timer thread. Record the message under lock,
        then forward to the base so the probe behaves exactly like a real device (writes to the
        wrapped port if it opened). No logging here — timer-thread hot path. */
    void sendMessageNow (const juce::MidiMessage& message) override
    {
        {
            const juce::CriticalSection::ScopedLockType sl (captureLock);
            capturedMessages.push_back (message);
        }

        te::MidiOutputDevice::sendMessageNow (message);
    }

private:
    juce::CriticalSection captureLock;
    std::vector<juce::MidiMessage> capturedMessages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClockProbeDevice)
};
