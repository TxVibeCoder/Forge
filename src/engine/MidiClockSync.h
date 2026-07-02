/*
    MidiClockSync — a thin, header-only seam over Tracktion's per-device MIDI-clock output.
    Keeps the clock-out engine calls in one place so views make no raw te:: device calls.

    The single fact this seam touches is ENGINE-AUTHORITATIVE — Forge holds no mirror of it
    (that would be a drift-prone duplicate; see CLAUDE.md "one canonical source per fact"):

      - MIDI-CLOCK OUT is a per-device flag: te::MidiOutputDevice::setSendingClock(bool) /
        isSendingClock() (tracktion_MidiOutputDevice.h:50-51). It PERSISTS per device via the
        device's saveProps ("sendMidiClock" attribute, tracktion_MidiOutputDevice.cpp:401) and
        defaults OFF. When set on an enabled+open device, the playback graph includes that device
        for clock even with no track routed to it (the EditNodeBuilder adds a device node for any
        out that isSendingClock()), and the MidiClockGenerator emits songPositionPointer /
        midiStart|Continue / midiClock at 24 PPQN + midiStop on the play->stop edge.

    Forge's model here is deliberately COARSE: "send clock to all MIDI outs" — a single app-level
    toggle rather than per-device UI, matching the transport-bar Click toggle's shape. The engine
    owns the per-device persistence, so there is no Forge-side state to drift.

    All calls are MESSAGE-THREAD ONLY. setSendingClock only mutates message-thread device props +
    calls changed()/saveProps() (no device rescan, no RT-thread work), so it is safe here; the RT
    graph reads sendMidiClock live when it (re)builds the playback node list.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace MidiClockSync
{
    /** Turns MIDI-clock output on or off for EVERY enabled MIDI output device in the engine's
        DeviceManager. Each device persists the flag itself (saveProps). No-op if there are no
        MIDI outs. Message-thread only. */
    inline void setSendClockToAll (te::Engine& engine, bool shouldSend)
    {
        auto& dm = engine.getDeviceManager();

        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
            if (auto* out = dm.getMidiOutDevice (i))
                out->setSendingClock (shouldSend);
    }

    /** True iff AT LEAST ONE MIDI output device is currently sending clock. Used to reflect engine
        truth into the transport-bar toggle at construction/refresh. Message-thread only. */
    inline bool isSendingClockAny (te::Engine& engine)
    {
        auto& dm = engine.getDeviceManager();

        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
            if (auto* out = dm.getMidiOutDevice (i))
                if (out->isSendingClock())
                    return true;

        return false;
    }
}
