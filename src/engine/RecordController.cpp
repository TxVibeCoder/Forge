#include "engine/RecordController.h"

RecordController::RecordController (te::Engine& e)
    : engine (e)
{
}

int RecordController::getInputDeviceCount() const
{
    return engine.getDeviceManager().getNumWaveInDevices();
}

void RecordController::enableInputs()
{
    auto& dm = engine.getDeviceManager();
    dm.rescanWaveDeviceList();

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
    {
        if (auto* wip = dm.getWaveInDevice (i))
        {
            wip->setStereoPair (false);
            wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            wip->setEnabled (true);
        }
    }
}

bool RecordController::armFirstInputToTrack (te::Edit& edit, te::AudioTrack& track)
{
    lastError = {};

    // InputDeviceInstance objects only exist once the playback context is allocated.
    edit.getTransport().ensureContextAllocated();

    for (auto* instance : edit.getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
        {
            auto result = instance->setTarget (track.itemID, true, &edit.getUndoManager(), 0);

            if (! result)
            {
                lastError = result.error();
                continue;
            }

            instance->setRecordingEnabled (track.itemID, true);
            edit.restartPlayback();
            return true;
        }
    }

    if (lastError.isEmpty())
        lastError = "No wave input instance available";

    return false;
}
