#include "engine/RecordController.h"
#include "engine/EngineHelpers.h"
#include "core/Log.h"

using namespace juce;

RecordController::RecordController (te::Engine& e)
    : engine (e)
{
}

int RecordController::getInputDeviceCount() const
{
    // Tracktion derives this list from the currently-open audio device's active input
    // channels, so a fresh rescan keeps the count honest after a device change.
    auto& dm = engine.getDeviceManager();
    dm.rescanWaveDeviceList();
    return dm.getNumWaveInDevices();
}

juce::StringArray RecordController::getAvailableInputDeviceNames() const
{
    StringArray names;

    auto& dm = engine.getDeviceManager();
    dm.rescanWaveDeviceList();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* wip = dm.getWaveInDevice (i))
            names.add (wip->getName());

    return names;
}

void RecordController::enableInputs()
{
    auto& dm = engine.getDeviceManager();

    // Make sure the engine-level wave-in list is rebuilt from the open device before we
    // enumerate it; otherwise a device that came online after init would be invisible.
    dm.rescanWaveDeviceList();

    const int numWaveIns = dm.getNumWaveInDevices();

    for (int i = 0; i < numWaveIns; ++i)
    {
        if (auto* wip = dm.getWaveInDevice (i))
        {
            wip->setStereoPair (false);
            wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            wip->setEnabled (true);
        }
    }

    // Flush the (async) device-property changes so a record() called immediately after this
    // sees the inputs as enabled.
    dm.dispatchPendingUpdates();
}

bool RecordController::armFirstInputToTrack (te::Edit& edit, te::AudioTrack& track)
{
    lastError = {};

    auto& dm = engine.getDeviceManager();
    dm.rescanWaveDeviceList();

    if (dm.getNumWaveInDevices() == 0)
    {
        // No capture channels are open. This is the common dev-box symptom: a mic exists in
        // Windows but the open audio device was opened output-only, so Tracktion sees no
        // wave inputs. EngineHelpers::ensureRecordingInputOpen() (called on the record path) tries
        // to avoid this by opening a default capture input on demand.
        auto available = EngineHelpers::getAvailableWaveInputDeviceNames (engine);

        lastError = available.isEmpty()
            ? "No audio input available. The current audio device has no open input channels "
              "(check the audio device's INPUT selection, and Windows microphone privacy)."
            : "An input device exists (" + available.joinIntoString (", ")
              + ") but no input channels are open on the current audio device. "
                "Open the Audio Settings dialog and select an input device.";
        return false;
    }

    // InputDeviceInstance objects only exist once the playback context is allocated.
    edit.getTransport().ensureContextAllocated();

    if (edit.getTransport().getCurrentPlaybackContext() == nullptr)
        FORGE_LOG_ERROR ("Failed to allocate playback context for recording");

    int waveInstancesSeen = 0;
    juce::String lastSetTargetError;

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        ++waveInstancesSeen;

        // Only consider enabled inputs; a disabled one can't be armed. enableInputs() should
        // have enabled them, but guard so we report a precise reason if it didn't.
        if (! instance->getInputDevice().isEnabled())
        {
            lastSetTargetError = "Wave input '" + instance->getInputDevice().getName()
                                 + "' is not enabled.";
            continue;
        }

        auto result = instance->setTarget (track.itemID, true, &edit.getUndoManager(), 0);

        if (! result)
        {
            lastSetTargetError = result.error();
            FORGE_LOG_DEBUG ("setTarget failed for wave input '" + instance->getInputDevice().getName() + "': " + lastSetTargetError);
            continue;
        }

        instance->setRecordingEnabled (track.itemID, true);
        edit.restartPlayback();
        return true;
    }

    if (waveInstancesSeen == 0)
        lastError = "No wave input instance available for this Edit "
                    "(the playback context has no wave inputs assigned).";
    else if (lastSetTargetError.isNotEmpty())
        lastError = "Could not arm any wave input: " + lastSetTargetError;
    else
        lastError = "Could not arm any of the " + juce::String (waveInstancesSeen)
                    + " wave input(s) to the track.";

    return false;
}

bool RecordController::disarmTrack (te::Edit& edit, te::AudioTrack& track)
{
    lastError = {};

    bool removedActive = false;
    juce::String lastRemoveError;

    // No need to allocate the context here: if it isn't allocated there are no input instances
    // and therefore nothing armed, so the track is already disarmed.
    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        // IMPORTANT: do NOT gate the removal on getTargets().contains(...). getTargets() returns
        // EMPTY for a currently-disabled input device even when a destination still targets this
        // track, so gating on it would leave a stale target + recordEnabled flag behind that would
        // silently re-arm the track if the device is later re-enabled. removeTarget() iterates the
        // persisted destinations directly and is a safe no-op when nothing matches, so we call it
        // unconditionally for every wave instance. getTargets() is used ONLY to decide whether a
        // *live* (enabled) target was actually removed, i.e. whether the playback graph needs a
        // restart — a disabled device is not in the active graph, so skipping the restart is fine.
        const bool wasActiveTarget = instance->getTargets().contains (track.itemID);

        instance->setRecordingEnabled (track.itemID, false);

        auto result = instance->removeTarget (track.itemID, &edit.getUndoManager());

        if (! result.wasOk())
        {
            lastRemoveError = result.getErrorMessage();
            FORGE_LOG_ERROR ("Could not disarm input device '" + instance->getInputDevice().getName()
                             + "' from track: " + lastRemoveError);
        }
        else if (wasActiveTarget)
            removedActive = true;
    }

    if (removedActive)
        edit.restartPlayback();

    if (lastRemoveError.isNotEmpty())
    {
        lastError = "Could not fully disarm the track: " + lastRemoveError;
        return false;
    }

    return true;
}

bool RecordController::isTrackArmed (te::Edit& edit, te::AudioTrack& track) const
{
    for (auto* instance : edit.getAllInputDevices())
        if (instance != nullptr
            && instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
            && instance->getTargets().contains (track.itemID))
            return true;

    return false;
}

//==============================================================================
// MIDI record layer (W7 — record into Session clip slots). VERDICT (A).
//==============================================================================

namespace
{
    // The MIDI device-type group. A MIDI InputDeviceInstance is one whose owning device's
    // getDeviceType() is one of these three; the wave/trackWave types are excluded.
    inline bool isMidiDeviceType (te::InputDevice::DeviceType t) noexcept
    {
        return t == te::InputDevice::physicalMidiDevice
            || t == te::InputDevice::virtualMidiDevice
            || t == te::InputDevice::trackMidiDevice;
    }
}

void RecordController::enableMidiInputs()
{
    auto& dm = engine.getDeviceManager();

    // NOTE: unlike enableInputs() this is NOT a wave clone — MidiInputDevice has no setStereoPair,
    // and the MIDI list is rebuilt via the ASYNC rescanMidiDeviceList() (a startTimer, not a
    // synchronous dispatch). Callers MUST yield after this before arming so the async rebuild
    // settles and the isEnabled()&&isAvailableToEdit() gate in getAllInputs() admits the instance.
    auto midiIns = dm.getMidiInDevices();

    if (midiIns.empty())
        FORGE_LOG_WARN ("enableMidiInputs: no MIDI-in devices found");

    for (auto& mi : midiIns)
    {
        if (mi != nullptr)
        {
            // Never trust the ctor enabled=true default (loadMidiProps can override it) — set it
            // explicitly every time.
            mi->setEnabled (true);
            mi->setMonitorMode (te::InputDevice::MonitorMode::automatic);
        }
    }

    dm.rescanMidiDeviceList();
}

int RecordController::getMidiInputDeviceCount() const
{
    auto& dm = engine.getDeviceManager();
    dm.rescanMidiDeviceList();
    return (int) dm.getMidiInDevices().size();
}

juce::StringArray RecordController::getAvailableMidiInputDeviceNames() const
{
    StringArray names;

    auto& dm = engine.getDeviceManager();
    dm.rescanMidiDeviceList();

    for (auto& mi : dm.getMidiInDevices())
        if (mi != nullptr)
            names.add (mi->getName());

    return names;
}

bool RecordController::armFirstMidiInputToSlot (te::Edit& edit, te::ClipSlot& slot)
{
    lastError = {};

    // InputDeviceInstance objects only exist once the playback context is allocated.
    edit.getTransport().ensureContextAllocated();

    if (edit.getTransport().getCurrentPlaybackContext() == nullptr)
    {
        lastError = "Failed to allocate playback context for MIDI recording.";
        FORGE_LOG_ERROR ("armFirstMidiInputToSlot: " + lastError);
        return false;
    }

    int midiInstancesSeen = 0;
    juce::String lastSetTargetError;

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (! isMidiDeviceType (instance->getInputDevice().getDeviceType()))
            continue;

        ++midiInstancesSeen;

        if (! instance->getInputDevice().isEnabled())
        {
            lastSetTargetError = "MIDI input '" + instance->getInputDevice().getName()
                                 + "' is not enabled.";
            continue;
        }

        // moveToTrack=false is deliberate: a slot arm is ADDITIVE, so one instance can target
        // several slots and a slot arm never silently un-arms a co-existing track/slot target.
        auto result = instance->setTarget (slot.itemID, /*moveToTrack=*/false,
                                           &edit.getUndoManager(), 0);

        if (! result)
        {
            lastSetTargetError = result.error();
            FORGE_LOG_DEBUG ("setTarget failed for MIDI input '" + instance->getInputDevice().getName()
                             + "' (slot): " + lastSetTargetError);
            continue;
        }

        // A value-initialised tl::expected is a SUCCESS holding a null Destination* when the id
        // resolved to neither a track nor a ClipSlot (tracktion_InputDevice.cpp:143-144). Without
        // this guard the arm would silently no-op and report success.
        if (result.value() == nullptr)
        {
            lastSetTargetError = "slot itemID did not resolve to a ClipSlot";
            FORGE_LOG_DEBUG ("setTarget for MIDI input '" + instance->getInputDevice().getName()
                             + "' returned a null Destination (slot id unresolved).");
            continue;
        }

        instance->setRecordingEnabled (slot.itemID, true);
        edit.restartPlayback();
        return true;
    }

    if (midiInstancesSeen == 0)
        lastError = "No MIDI input instance available for this Edit "
                    "(enable MIDI inputs and yield before arming).";
    else if (lastSetTargetError.isNotEmpty())
        lastError = "Could not arm any MIDI input to the slot: " + lastSetTargetError;
    else
        lastError = "Could not arm any of the " + juce::String (midiInstancesSeen)
                    + " MIDI input(s) to the slot.";

    return false;
}

bool RecordController::armFirstMidiInputToTrack (te::Edit& edit, te::AudioTrack& track)
{
    lastError = {};

    edit.getTransport().ensureContextAllocated();

    if (edit.getTransport().getCurrentPlaybackContext() == nullptr)
    {
        lastError = "Failed to allocate playback context for MIDI recording.";
        FORGE_LOG_ERROR ("armFirstMidiInputToTrack: " + lastError);
        return false;
    }

    int midiInstancesSeen = 0;
    juce::String lastSetTargetError;

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (! isMidiDeviceType (instance->getInputDevice().getDeviceType()))
            continue;

        ++midiInstancesSeen;

        if (! instance->getInputDevice().isEnabled())
        {
            lastSetTargetError = "MIDI input '" + instance->getInputDevice().getName()
                                 + "' is not enabled.";
            continue;
        }

        // moveToTrack=true — a track arm is EXCLUSIVE (audio-parity), clearing co-existing targets.
        auto result = instance->setTarget (track.itemID, /*moveToTrack=*/true,
                                           &edit.getUndoManager(), 0);

        if (! result)
        {
            lastSetTargetError = result.error();
            FORGE_LOG_DEBUG ("setTarget failed for MIDI input '" + instance->getInputDevice().getName()
                             + "' (track): " + lastSetTargetError);
            continue;
        }

        if (result.value() == nullptr)
        {
            lastSetTargetError = "track itemID did not resolve to a target";
            FORGE_LOG_DEBUG ("setTarget for MIDI input '" + instance->getInputDevice().getName()
                             + "' returned a null Destination (track id unresolved).");
            continue;
        }

        instance->setRecordingEnabled (track.itemID, true);
        edit.restartPlayback();
        return true;
    }

    if (midiInstancesSeen == 0)
        lastError = "No MIDI input instance available for this Edit "
                    "(enable MIDI inputs and yield before arming).";
    else if (lastSetTargetError.isNotEmpty())
        lastError = "Could not arm any MIDI input to the track: " + lastSetTargetError;
    else
        lastError = "Could not arm any of the " + juce::String (midiInstancesSeen)
                    + " MIDI input(s) to the track.";

    return false;
}

bool RecordController::disarmSlot (te::Edit& edit, te::ClipSlot& slot)
{
    lastError = {};

    bool removedActive = false;
    juce::String lastRemoveError;

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (! isMidiDeviceType (instance->getInputDevice().getDeviceType()))
            continue;

        // Do NOT gate removal on getTargets().contains(...): a disabled device reports EMPTY
        // targets even while a destination still targets this slot, so gating would leave a stale
        // target + recordEnabled behind. removeTarget() iterates the persisted destinations and is
        // a safe no-op when nothing matches, so call it unconditionally. getTargets() is used ONLY
        // to decide whether a *live* target was removed (whether a playback restart is needed).
        const bool wasActiveTarget = instance->getTargets().contains (slot.itemID);

        instance->setRecordingEnabled (slot.itemID, false);

        auto result = instance->removeTarget (slot.itemID, &edit.getUndoManager());

        if (! result.wasOk())
        {
            lastRemoveError = result.getErrorMessage();
            FORGE_LOG_ERROR ("Could not disarm MIDI input '" + instance->getInputDevice().getName()
                             + "' from slot: " + lastRemoveError);
        }
        else if (wasActiveTarget)
            removedActive = true;
    }

    if (removedActive)
        edit.restartPlayback();

    if (lastRemoveError.isNotEmpty())
    {
        lastError = "Could not fully disarm the slot: " + lastRemoveError;
        return false;
    }

    return true;
}

bool RecordController::disarmMidiTrack (te::Edit& edit, te::AudioTrack& track)
{
    lastError = {};

    bool removedActive = false;
    juce::String lastRemoveError;

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (! isMidiDeviceType (instance->getInputDevice().getDeviceType()))
            continue;

        const bool wasActiveTarget = instance->getTargets().contains (track.itemID);

        instance->setRecordingEnabled (track.itemID, false);

        auto result = instance->removeTarget (track.itemID, &edit.getUndoManager());

        if (! result.wasOk())
        {
            lastRemoveError = result.getErrorMessage();
            FORGE_LOG_ERROR ("Could not disarm MIDI input '" + instance->getInputDevice().getName()
                             + "' from track: " + lastRemoveError);
        }
        else if (wasActiveTarget)
            removedActive = true;
    }

    if (removedActive)
        edit.restartPlayback();

    if (lastRemoveError.isNotEmpty())
    {
        lastError = "Could not fully disarm the MIDI track: " + lastRemoveError;
        return false;
    }

    return true;
}

bool RecordController::isSlotMidiArmed (te::Edit& edit, te::ClipSlot& slot) const
{
    // Pure read — safe from the 25 Hz poll (no mutation, no logging).
    for (auto* instance : edit.getAllInputDevices())
        if (instance != nullptr
            && isMidiDeviceType (instance->getInputDevice().getDeviceType())
            && instance->getTargets().contains (slot.itemID))
            return true;

    return false;
}

bool RecordController::isTrackMidiArmed (te::Edit& edit, te::AudioTrack& track) const
{
    // Pure read — safe from the 25 Hz poll (no mutation, no logging).
    for (auto* instance : edit.getAllInputDevices())
        if (instance != nullptr
            && isMidiDeviceType (instance->getInputDevice().getDeviceType())
            && instance->getTargets().contains (track.itemID))
            return true;

    return false;
}
