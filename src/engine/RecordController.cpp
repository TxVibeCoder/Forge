#include "engine/RecordController.h"
#include "engine/EngineHelpers.h"

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
        // wave inputs. EngineHelpers::initialiseAudioForRecording() tries to avoid this.
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

    bool removedAny = false;
    juce::String lastRemoveError;

    // No need to allocate the context here: if it isn't allocated there are no input instances
    // and therefore nothing armed, so the track is already disarmed. We only touch instances that
    // actually target this track (mirrors the wave-only filtering in armFirstInputToTrack).
    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance == nullptr)
            continue;

        if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        if (! instance->getTargets().contains (track.itemID))
            continue;

        // Clear record-enable first (while the target still exists), then drop the target so the
        // lane is fully disconnected from the input.
        instance->setRecordingEnabled (track.itemID, false);

        auto result = instance->removeTarget (track.itemID, &edit.getUndoManager());

        if (result.wasOk())
            removedAny = true;
        else
            lastRemoveError = result.getErrorMessage();
    }

    if (removedAny)
        edit.restartPlayback();

    if (lastRemoveError.isNotEmpty())
    {
        lastError = "Could not fully disarm the track: " + lastRemoveError;
        return false;
    }

    return true;
}
