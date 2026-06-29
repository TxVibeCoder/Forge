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
            transport.stop (true, false);
        else
            transport.record (false);
    }
}
