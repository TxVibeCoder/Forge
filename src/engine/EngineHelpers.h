/*
    EngineHelpers — small, header-only free functions for common track/transport/import
    operations. Trimmed and ported into Forge from the Tracktion examples'
    examples/common/Utilities.h (which we deliberately do NOT vendor wholesale, as it drags
    in plugin trees, MIDI-clip drawing and BinaryData we don't want yet).

    Message-thread only.
*/

#pragma once

#include <cstdlib>   // std::abs (int)              — nearestPowerOfTwoDenominator
#include <limits>    // std::numeric_limits<int>    — nearestPowerOfTwoDenominator

#include <JuceHeader.h>

#include "core/Log.h"

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
                auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                                   { { start, start + length }, {} }, false);

                if (clip == nullptr)
                    FORGE_LOG_ERROR ("Failed to insert audio clip from " + file.getFullPathName()
                                     + " onto track " + juce::String (trackIndex));

                return clip;
            }

            FORGE_LOG_ERROR ("Failed to validate audio file " + file.getFullPathName()
                             + " — format may be unsupported");
        }

        return {};
    }

    /** Inserts an empty MIDI clip spanning `range` (in SECONDS) onto `trackIndex`. The clip's
        on-timeline position is a seconds range; any notes drawn into it later are in beats. Does
        NOT touch the track's plugin chain — ensuring an audible instrument is the caller's job
        (ProjectSession::createMidiClip ensures the default 4OSC), so this stays free of PluginHost. */
    inline te::MidiClip::Ptr insertDrawnMidiClip (te::Edit& edit, int trackIndex,
                                                  te::TimeRange range, const juce::String& name)
    {
        if (auto* track = getOrInsertAudioTrackAt (edit, trackIndex))
        {
            auto clip = track->insertMIDIClip (name, range, nullptr);

            if (clip == nullptr)
                FORGE_LOG_ERROR ("Failed to insert MIDI clip onto track " + juce::String (trackIndex));

            return clip;
        }

        FORGE_LOG_ERROR ("Failed to insert MIDI clip onto track " + juce::String (trackIndex));
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
        audio device type (e.g. "Microphone (Built-in)", "Line In", ...). This queries the
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

    /** Lazily opens a wave INPUT device so recording can work, WITHOUT clobbering the saved/open
        OUTPUT device. Call this ONLY when the user actually wants to record (arming a track or
        starting a take) — never at startup.

        Startup is kept output-only by constructing the engine with a ForgeEngineBehaviour whose
        shouldOpenAudioInputByDefault() returns false (see main.cpp): the engine's own device init
        then opens output only, so a capture device is never negotiated on the message thread at
        launch (which could stall 25–77 s when the default device changed). This function does the
        input half, on demand.

        Idempotent and cheap once an input is available: it early-outs if an input device is already
        selected or the engine already exposes open wave inputs, so the potentially-slow OS
        negotiation happens at most once, on the first record attempt.

        Two engine subtleties this has to get right (both surfaced by the startup-latency review):
          - The device was opened with ZERO input channels needed, and JUCE only recomputes that
            count when useDefaultInputChannels is FALSE. So we request EXPLICIT input channels here;
            the default-channels path would open the input with an empty channel mask and silently
            record SILENCE.
          - Adding an input rebuilds the device as a combined in+out device, which first tears down
            the currently-open OUTPUT device. If the combined open fails (input busy / mic privacy),
            JUCE leaves NO device open. We snapshot the working output-only setup and reopen it on
            failure, so a failed arm can never kill playback.

        Residual limitation: if the OS denies microphone access (Windows privacy) or the default
        input fails to open, no input channels appear and getNumWaveInDevices() stays 0 — see
        docs/devlog/device-recording.md.

        Message-thread only. May block for several seconds on the FIRST call if the OS audio stack
        is slow to open the capture device — acceptable because it is user-initiated and off the
        startup path.

        Returns the device-open error string for diagnostics: empty on success OR on the early-out
        no-op (an input is already open); a "(no capture endpoint ...)" sentinel when no input
        device exists; otherwise the exact juce::AudioDeviceManager::setAudioDeviceSetup() error.
        Callers may ignore the return (it is purely informational). */
    inline juce::String ensureRecordingInputOpen (te::Engine& engine)
    {
        auto& dm  = engine.getDeviceManager();
        auto& adm = dm.deviceManager;   // the underlying juce::AudioDeviceManager

        const auto current = adm.getAudioDeviceSetup();   // the working, output-only setup

        // 1. Already have an input (explicit choice, or wave inputs already open) -> no-op.
        if (current.inputDeviceName.isNotEmpty() || dm.getNumWaveInDevices() > 0)
            return {};

        auto setup = current;

        // 2. Choose the current type's default capture device.
        if (auto* type = adm.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto inputNames = type->getDeviceNames (true);   // true => input names

            if (! inputNames.isEmpty())
            {
                const int defIdx = type->getDefaultDeviceIndex (true);
                setup.inputDeviceName = inputNames[(defIdx >= 0 && defIdx < inputNames.size()) ? defIdx : 0];
            }
        }

        if (setup.inputDeviceName.isEmpty())
            return "(no capture endpoint advertised by the current device type)";

        // 3. Request EXPLICIT input channels (NOT useDefaultInputChannels — see the note above),
        //    keeping the working output device.
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, te::DeviceManager::defaultNumChannelsToOpen, true);

        if (current.outputDeviceName.isNotEmpty())
            setup.outputDeviceName = current.outputDeviceName;   // never drop the output we have

        const auto err = adm.setAudioDeviceSetup (setup, false);   // treatAsChosenDevice=false

        // 4. If the combined in+out open failed, JUCE has already torn down the output device.
        //    Reopen the output-only setup we had so playback survives a failed arm.
        if (err.isNotEmpty() && current.outputDeviceName.isNotEmpty())
        {
            FORGE_LOG_ERROR ("Failed to open input device '" + setup.inputDeviceName + "': " + err
                             + " — attempting output-only restore");

            const auto restoreErr = adm.setAudioDeviceSetup (current, false);

            if (restoreErr.isNotEmpty())
                FORGE_LOG_ERROR ("Recovery: failed to restore output-only device — playback may be unavailable");
        }

        // 5. Rebuild Tracktion's wave-in list from whatever input channels are now open.
        dm.rescanWaveDeviceList();

        return err;
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

        if (engine.getDeviceManager().isHostedAudioDeviceInterfaceInUse())
        {
            FORGE_LOG_INFO ("Initialized synthetic audio device: " + juce::String (numInputChannels)
                            + " in, " + juce::String (numOutputChannels) + " out @ "
                            + juce::String (sampleRate) + "Hz");
            return &hostedInterface;
        }

        FORGE_LOG_ERROR ("Engine refused to switch to synthetic audio device — record selftest cannot proceed");
        return nullptr;
    }

    //==============================================================================
    // Track volume / pan helpers
    //
    // Each te::AudioTrack carries a built-in te::VolumeAndPanPlugin on its plugin chain,
    // reachable via AudioTrack::getVolumePlugin() (declared in tracktion_AudioTrack.h:59,
    // returns VolumeAndPanPlugin* which may be null if the track has no vol/pan plugin).
    //
    // Volume uses the plugin's direct dB API:
    //     float VolumeAndPanPlugin::getVolumeDb() const;   // tracktion_VolumeAndPan.h:34
    //     void  VolumeAndPanPlugin::setVolumeDb (float);    // tracktion_VolumeAndPan.h:36
    // Internally these convert through volumeFaderPositionToDB / decibelsToVolumeFaderPosition
    // (tracktion_AudioUtilities.cpp:138-146); the engine's dB floor is -100 dB (silence) and
    // the fader curve reaches +6 dB at slider position 1.0. We clamp to [-100, +12] per the
    // mixer contract — the floor matches the engine's own silence value.
    //
    // Pan uses the plugin's getPan()/setPan() which are already in the -1..+1 range
    // (tracktion_VolumeAndPan.h:39-40), -1 = hard left, 0 = centre, +1 = hard right.
    //
    // All four guard a null plugin (return 0.0f / no-op) so they are safe on any track.
    //
    // Message-thread only.

    /** Current track volume in dB, or 0.0f (unity) if the track has no volume plugin. */
    inline float getTrackVolumeDb (te::AudioTrack& track)
    {
        if (auto* vp = track.getVolumePlugin())
            return vp->getVolumeDb();

        return 0.0f;
    }

    /** Sets the track volume in dB, clamped to [-100, +12]. No-op if no volume plugin. */
    inline void setTrackVolumeDb (te::AudioTrack& track, float db)
    {
        if (auto* vp = track.getVolumePlugin())
            vp->setVolumeDb (juce::jlimit (-100.0f, 12.0f, db));
    }

    /** Current track pan in -1..+1 (-1 = left, 0 = centre, +1 = right), or 0.0f if no plugin. */
    inline float getTrackPan (te::AudioTrack& track)
    {
        if (auto* vp = track.getVolumePlugin())
            return vp->getPan();

        return 0.0f;
    }

    /** Sets the track pan, clamped to [-1, +1]. No-op if no volume plugin. */
    inline void setTrackPan (te::AudioTrack& track, float pan)
    {
        if (auto* vp = track.getVolumePlugin())
            vp->setPan (juce::jlimit (-1.0f, 1.0f, pan));
    }

    //==============================================================================
    // Tempo
    //
    // The tempo at a timeline position lives on a te::TempoSetting reachable via
    //     TempoSetting& TempoSequence::getTempoAt (TimePosition) const;   // tracktion_TempoSequence.h:94
    //     void          TempoSetting::setBpm (double);                     // tracktion_TempoSetting.h:76
    // Read it back with TempoSequence::getBpmAt (TimePosition) (curve-aware). Clamped to
    // [20, 300] to match the tempo popup's editable range (and TapTempo's own clamp).
    //
    // Message-thread only.

    /** Sets the BPM of the tempo section covering `pos`, clamped to [20, 300]. */
    inline void setTempoAt (te::Edit& edit, te::TimePosition pos, double bpm)
    {
        edit.tempoSequence.getTempoAt (pos).setBpm (juce::jlimit (20.0, 300.0, bpm));
    }

    //==============================================================================
    // Time signature (hands-on: clickable LCD "· 4/4" zone)
    //
    // A time signature lives on a te::TimeSigSetting inside the Edit's tempo sequence:
    //     TimeSigSetting::Ptr TempoSequence::insertTimeSig (BeatPosition);   // tracktion_TempoSequence.h:137
    //     juce::CachedValue<int> TimeSigSetting::numerator, denominator;      // tracktion_TimeSigSetting.h:50
    //     juce::String TimeSigSetting::getStringTimeSig() const;              // -> "n/d", TimeSigSetting.cpp:40-43
    //
    // insertTimeSig(BeatPosition) delegates to insertTimeSig(TimePosition, getUndoManager())
    // (tracktion_TempoSequence.cpp:225-227 -> 230-257), so the write is UM-bound / undoable just like
    // setTempoAt. Two behaviours the callers rely on (source-traced there):
    //   - AT an existing break (beat 0 always has the Edit's initial sig): it returns the EXISTING
    //     setting rather than stacking a second one at the same beat (cpp:248-249) — so a beat-0 call
    //     mutates the initial time signature in place.
    //   - ELSEWHERE: it rounds to the nearest beat (cpp:241) and inserts a NEW break (a copy of the
    //     previous sig, cpp:245) which we then overwrite.
    // The numerator/denominator CachedValues are referTo'd with the edit's UndoManager in the
    // TimeSigSetting ctor (tracktion_TimeSigSetting.cpp:18-22), so assigning them is a single undoable
    // step. Only powers of two are valid meter denominators; the numerator is clamped to [1, 32] to
    // match the popup's editable range.
    //
    // Message-thread only.

    /** Snaps `d` to the nearest valid meter denominator (a power of two in {1,2,4,8,16,32}); ties
        resolve to the SMALLER power of two (the {1,2,4,...} scan keeps the first-seen minimum, so
        3 -> 2, not 4). Used to sanitise a typed/garbage denominator before it reaches the engine. */
    inline int nearestPowerOfTwoDenominator (int d)
    {
        static const int valid[] = { 1, 2, 4, 8, 16, 32 };
        int best = 4, bestErr = std::numeric_limits<int>::max();
        for (int v : valid) { const int e = std::abs (v - d); if (e < bestErr) { bestErr = e; best = v; } }
        return best;
    }

    /** Sets (or inserts) the time signature in force at `pos`. At beat 0 this mutates the Edit's
        initial sig; elsewhere it inserts a change rounded to the nearest beat. UM-bound (undoable)
        like setTempoAt. The numerator is clamped to [1, 32]; the denominator is snapped to the
        nearest power of two (the only valid meter denominators). */
    inline void setTimeSigAt (te::Edit& edit, te::BeatPosition pos, int numerator, int denominator)
    {
        if (auto sig = edit.tempoSequence.insertTimeSig (pos))   // existing sig at pos, or a new one; UM-bound
        {
            sig->numerator   = juce::jlimit (1, 32, numerator);
            sig->denominator = nearestPowerOfTwoDenominator (denominator);
        }
        else
            FORGE_LOG_ERROR ("setTimeSigAt: insertTimeSig returned null at beat "
                             + juce::String (pos.inBeats()));
    }

    /** The time signature in force at `pos` as "n/d" (for seeding the popup / reading back). */
    inline juce::String getTimeSigStringAt (te::Edit& edit, te::BeatPosition pos)
    {
        return edit.tempoSequence.getTimeSigAt (pos).getStringTimeSig();
    }
}
