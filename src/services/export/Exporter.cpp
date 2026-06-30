#include "services/export/Exporter.h"

using namespace juce;

namespace Exporter
{

namespace
{
    // Total number of clips across all audio tracks. Used to reject an empty edit before we
    // bother spinning up a render graph (the renderer would otherwise produce a silent file
    // or fail with a "completely silent" error).
    int countClipsOnAudioTracks (te::Edit& edit)
    {
        int total = 0;

        for (auto* at : te::getAudioTracks (edit))
            total += at->getNumTrackItems();

        return total;
    }
}

bool renderEditToWav (te::Edit& edit, const juce::File& outFile, juce::String& error)
{
    error.clear();

    // --- Validate the edit has something to render -----------------------------------------
    const auto editLength = edit.getLength();

    if (countClipsOnAudioTracks (edit) == 0 || editLength <= te::TimeDuration())
    {
        error = "Nothing to export: the project has no audio clips.";
        return false;
    }

    // --- Validate the destination ----------------------------------------------------------
    if (outFile == juce::File())
    {
        error = "No output file was specified.";
        return false;
    }

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();   // overwrite any existing file; render expects to create it fresh

    auto& engine = edit.engine;

    auto* wavFormat = engine.getAudioFileFormatManager().getWavFormat();

    if (wavFormat == nullptr)
    {
        error = "No WAV audio format is available in the engine.";
        return false;
    }

    auto sampleRate = engine.getDeviceManager().getSampleRate();

    if (sampleRate < 8000.0)
        sampleRate = 44100.0;   // device not open / nonsense rate — fall back to a safe default

    // --- Build the render parameters -------------------------------------------------------
    // Render every track in the edit, mixed to stereo, 24-bit WAV, over [0, end-of-last-clip].
    te::Renderer::Parameters params (edit);
    params.destFile           = outFile;
    params.audioFormat        = wavFormat;
    params.bitDepth           = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio  = 512;
    params.time               = te::TimeRange (te::TimePosition(), editLength);
    params.tracksToDo         = te::toBitSet (te::getAllTracks (edit));   // empty would also mean "all"
    params.usePlugins         = true;
    params.useMasterPlugins   = true;
    params.canRenderInMono    = false;   // force stereo output
    params.mustRenderInMono   = false;
    params.realTimeRender     = false;
    params.createMidiFile     = false;

    // --- Run the render on the calling thread ----------------------------------------------
    // Stop any playback and deinit plugins, mirroring Renderer::renderToFile's own preamble, so
    // the render graph isn't fighting the live playback graph for the same plugin state.
    te::TransportControl::stopAllTransports (engine, false, true);
    te::Renderer::turnOffAllPlugins (edit);

    const te::Edit::ScopedRenderStatus renderStatus (edit, true);

    auto task = te::render_utils::createRenderTask (params, "Forge export", nullptr, nullptr);

    if (task == nullptr)
    {
        te::Renderer::turnOffAllPlugins (edit);
        error = "Couldn't initialise the renderer.";
        return false;
    }

    // Drive the task to completion synchronously — no UIBehaviour progress bar, no message loop.
    while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {
    }

    te::Renderer::turnOffAllPlugins (edit);

    if (task->errorMessage.isNotEmpty())
    {
        outFile.deleteFile();
        error = task->errorMessage;
        return false;
    }

    if (! outFile.existsAsFile())
    {
        error = "The render finished but no file was produced.";
        return false;
    }

    return true;
}

} // namespace Exporter
