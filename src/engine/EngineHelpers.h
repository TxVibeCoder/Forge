/*
    EngineHelpers — small, header-only free functions for common track/transport/import
    operations. Trimmed and ported into Forge from the Tracktion examples'
    examples/common/Utilities.h (which we deliberately do NOT vendor wholesale, as it drags
    in plugin trees, MIDI-clip drawing and BinaryData we don't want yet).

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace EngineHelpers
{
    inline te::AudioTrack* getOrInsertAudioTrackAt (te::Edit& edit, int index)
    {
        edit.ensureNumberOfAudioTracks (index + 1);
        return te::getAudioTracks (edit)[index];
    }

    /** Inserts a wave clip referencing `file` onto `trackIndex` starting at `start`. */
    inline te::WaveAudioClip::Ptr loadAudioFileAsClip (te::Edit& edit, const juce::File& file,
                                                       int trackIndex, te::TimePosition start)
    {
        if (auto* track = getOrInsertAudioTrackAt (edit, trackIndex))
        {
            te::AudioFile audioFile (edit.engine, file);

            if (audioFile.isValid())
            {
                const auto length = te::TimeDuration::fromSeconds (audioFile.getLength());
                return track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                              { { start, start + length }, {} }, false);
            }
        }

        return {};
    }

    /** Async file chooser filtered to the engine's supported audio formats. */
    inline void browseForAudioFile (te::Engine& engine, std::function<void (const juce::File&)> fileChosen)
    {
        auto fc = std::make_shared<juce::FileChooser> (
            "Select an audio file to import...",
            engine.getPropertyStorage().getDefaultLoadSaveDirectory ("forgeImport"),
            engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());

        fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [fc, &engine, callback = std::move (fileChosen)] (const juce::FileChooser&)
                         {
                             const auto f = fc->getResult();

                             if (f.existsAsFile())
                             {
                                 engine.getPropertyStorage().setDefaultLoadSaveDirectory ("forgeImport", f.getParentDirectory());
                                 callback (f);
                             }
                         });
    }

    /** Opens the audio device selector (pick input/output devices, sample rate, buffer). */
    inline void showAudioDeviceSettings (te::Engine& engine)
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Audio Settings";
        o.dialogBackgroundColour = juce::Colours::black;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;

        auto* selector = new juce::AudioDeviceSelectorComponent (
            engine.getDeviceManager().deviceManager,
            1, 512,   // min/max input channels (min 1 -> prompt for an input device)
            1, 512,   // min/max output channels
            false, false, true, false);
        selector->setSize (500, 450);
        o.content.setOwned (selector);
        o.launchAsync();
    }

    inline void togglePlay (te::Edit& edit)
    {
        auto& transport = edit.getTransport();

        if (transport.isPlaying())
            transport.stop (false, false);
        else
            transport.play (false);
    }

    inline void toggleRecord (te::Edit& edit)
    {
        auto& transport = edit.getTransport();

        if (transport.isRecording())
            transport.stop (false, false);   // keep the take (engine inserts the clip)
        else
            transport.record (false);
    }

    //==============================================================================
    /** Returns the list of wave INPUT device names advertised by the currently-selected
        audio device type (e.g. "Microphone (Realtek)", "Line In", ...). This queries the
        JUCE driver layer directly (AudioIODeviceType::getDeviceNames(true)), so it reflects
        the OS-visible capture endpoints regardless of whether one is currently open. Handy
        for diagnostics/UI ("why is recording greyed out?").

        Message-thread only. */
    inline juce::StringArray getAvailableWaveInputDeviceNames (te::Engine& engine)
    {
        juce::StringArray names;

        if (auto* type = engine.getDeviceManager().deviceManager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            names = type->getDeviceNames (true);   // true => input names
        }

        return names;
    }

    /** Initialises the engine's audio so that wave INPUT channels are opened (so recording can
        work) WITHOUT clobbering a previously-saved OUTPUT device.

        Why this exists (the device-override blocker):
          - te::DeviceManager::initialise() -> loadSettings() restores the saved
            `audio_device_setup` XML when it exists, so the saved OUTPUT is preserved by the
            engine itself. On the very first run (no saved XML) it falls back to
            initialiseWithDefaultDevices(), which picks the system-default output.
          - Tracktion derives its wave-in list (getNumWaveInDevices()) purely from the ACTIVE
            input channel names of the currently-open juce::AudioIODevice
            (DeviceManager::AvailableWaveDeviceList::describeStandardDevices ->
            device.getInputChannelNames()). If the restored/opened device has no input device
            name (because the user never explicitly chose an input), it opens output-only and
            getInputChannelNames() is empty -> zero wave inputs -> recording is impossible.

        Strategy (best-effort, documented tradeoff):
          1. Let the engine initialise normally (restores saved output if XML present).
          2. Read back the now-open AudioDeviceSetup and remember the restored OUTPUT name.
          3. If no INPUT device is selected, pick the current type's DEFAULT input device.
          4. Re-apply the setup with useDefaultInputChannels=true and the SAME output name,
             passing treatAsChosenDevice=false so this auto-opened input is NOT persisted as
             the user's explicit choice (their next explicit pick via the Audio dialog wins,
             and we never overwrite a saved output in storage).
          5. Rescan so Tracktion rebuilds its wave-in list from the freshly-opened channels.

        Residual limitation: on a machine with NO saved settings at all, the OUTPUT is still
        whatever the OS default is (there is nothing saved to restore). Also, if the OS denies
        microphone access (Windows privacy) or the default input device fails to open, no input
        channels appear and getNumWaveInDevices() stays 0 — see docs/devlog/device-recording.md.

        Message-thread only. */
    inline void initialiseAudioForRecording (te::Engine& engine)
    {
        auto& dm  = engine.getDeviceManager();
        auto& adm = dm.deviceManager;   // the underlying juce::AudioDeviceManager

        // 1. Normal init. Requests up to 512 in / 512 out channels and restores saved XML.
        dm.initialise (te::DeviceManager::defaultNumChannelsToOpen,
                       te::DeviceManager::defaultNumChannelsToOpen);

        // 2. Snapshot what actually opened.
        auto setup = adm.getAudioDeviceSetup();
        const auto restoredOutput = setup.outputDeviceName;

        // 3. If nothing is selected for input, choose the type's default capture device.
        if (setup.inputDeviceName.isEmpty())
        {
            if (auto* type = adm.getCurrentDeviceTypeObject())
            {
                type->scanForDevices();
                const auto inputNames = type->getDeviceNames (true);

                if (! inputNames.isEmpty())
                {
                    const int defIdx = type->getDefaultDeviceIndex (true);
                    setup.inputDeviceName = inputNames[(defIdx >= 0 && defIdx < inputNames.size()) ? defIdx : 0];
                }
            }
        }

        // 4. Re-apply only if we now have an input to open, keeping the restored output intact.
        if (setup.inputDeviceName.isNotEmpty())
        {
            setup.useDefaultInputChannels = true;       // open the device's natural input channels

            if (restoredOutput.isNotEmpty())
                setup.outputDeviceName = restoredOutput;  // belt-and-braces: never drop the output

            // treatAsChosenDevice=false => do not persist this auto-input as the user's choice,
            // so we never rewrite the saved audio_device_setup (output stays as the user left it).
            const auto err = adm.setAudioDeviceSetup (setup, false);
            juce::ignoreUnused (err);   // tolerate failure (e.g. input busy / privacy-blocked)
        }

        // 5. Rebuild Tracktion's wave-in list from whatever input channels are now open.
        dm.rescanWaveDeviceList();
    }

    /** Hardware-free SYNTHETIC input for `--selftest-record`. Installs a Tracktion
        HostedAudioDeviceInterface so the engine exposes `inputChannels` virtual wave inputs
        (and `outputChannels` virtual outputs) instead of opening real hardware. The caller
        MUST then drive te::HostedAudioDeviceInterface::processBlock() on a regular cadence
        (a background thread or a high-rate timer) feeding it a known signal; the engine will
        record exactly those samples, letting the record path self-verify with no microphone.

        Returns the interface (owned by the engine's DeviceManager) so the caller can pump it,
        or nullptr if the engine refuses to switch to the hosted interface.

        IMPORTANT: this REPLACES the real device manager with the hosted one for the lifetime
        of the engine, so it must ONLY be used in the record-selftest process, never in the
        normal app or the playback selftest (which rely on real output hardware). Call this
        BEFORE opening/playing any Edit. See docs/devlog/device-recording.md for the exact
        main.cpp driver loop.

        Message-thread only (for setup; processBlock is then called from the driver). */
    inline te::HostedAudioDeviceInterface* installSyntheticInputForSelftest (te::Engine& engine,
                                                                             double sampleRate = 44100.0,
                                                                             int blockSize = 512,
                                                                             int numInputChannels = 1,
                                                                             int numOutputChannels = 2)
    {
        auto& hostedInterface = engine.getDeviceManager().getHostedAudioDeviceInterface();

        te::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate     = sampleRate;
        params.blockSize      = blockSize;
        params.inputChannels  = numInputChannels;
        params.outputChannels = numOutputChannels;
        params.useMidiDevices = false;
        hostedInterface.initialise (params);

        // prepareToPlay sets the interface's maxChannels (= max(in,out)); without it the hosted
        // device exposes 0 channels and no wave-in devices are built. dispatchPendingUpdates
        // flushes the device-list rebuild synchronously. This mirrors the engine's own
        // test_utilities::EnginePlayer recipe (initialise -> prepareToPlay -> dispatchPendingUpdates).
        hostedInterface.prepareToPlay (sampleRate, blockSize);
        engine.getDeviceManager().dispatchPendingUpdates();

        // Make the new hosted wave inputs visible to RecordController/getNumWaveInDevices().
        engine.getDeviceManager().rescanWaveDeviceList();

        return engine.getDeviceManager().isHostedAudioDeviceInterfaceInUse() ? &hostedInterface
                                                                             : nullptr;
    }
}
