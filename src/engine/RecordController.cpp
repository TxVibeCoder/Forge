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
