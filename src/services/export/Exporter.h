/*
    Exporter — renders the current Edit to a .wav file on disk.

    A thin, blocking wrapper over the Tracktion Engine renderer (te::Renderer). Builds a
    Renderer::Parameters for the whole Edit and runs the RenderTask to completion on the
    calling thread, so there is no UIBehaviour progress bar and no message-loop dependency.

    The shell calls this from an "Export" Control-bar action behind a save-file chooser.

    Message-thread only (the render runs synchronously on whatever thread calls it; the shell
    calls it on the message thread).
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace Exporter
{
    /** Renders the edit's full content to a 24-bit WAV at outFile.

        Synchronous/blocking — renders on the calling thread (acceptable for now). Renders the
        range [0, edit.getLength()] (i.e. start of the edit to the end of the last clip), mixing
        all tracks down to stereo at the Edit's current sample rate.

        Returns true on success. On failure returns false and sets `error` to an explanatory
        message. An edit with no clips returns false with an explanatory error. */
    bool renderEditToWav (te::Edit& edit, const juce::File& outFile, juce::String& error);
}
