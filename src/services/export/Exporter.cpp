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

    // Builds the tracksToDo bitset for a single track. The renderer indexes its bitset against
    // te::getAllTracks (edit) ordering, so we look the target up there and set just that one bit.
    // NOTE: we deliberately do NOT use te::toBitSet({ track }) here — that helper ignores the
    // array's contents and sets a bit for *every* track in the edit, which would render all of
    // them into each stem. Building the single bit by hand is what gives us a true per-track stem.
    juce::BigInteger singleTrackBitSet (te::Edit& edit, te::Track& track)
    {
        juce::BigInteger bits;

        auto allTracks = te::getAllTracks (edit);

        if (auto index = allTracks.indexOf (&track); index >= 0)
            bits.setBit (index);

        return bits;
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

bool renderStems (te::Edit& edit, const juce::File& outputDir, juce::String& error)
{
    error.clear();

    // --- Validate the edit has something to render -----------------------------------------
    const auto editLength = edit.getLength();

    if (editLength <= te::TimeDuration())
    {
        error = "Nothing to export: the project is empty.";
        return false;
    }

    auto audioTracks = te::getAudioTracks (edit);

    if (audioTracks.isEmpty())
    {
        error = "Nothing to export: the project has no audio tracks.";
        return false;
    }

    if (countClipsOnAudioTracks (edit) == 0)
    {
        error = "Nothing to export: the project has no audio clips.";
        return false;
    }

    // --- Validate the destination ----------------------------------------------------------
    if (outputDir == juce::File())
    {
        error = "No output folder was specified.";
        return false;
    }

    if (! outputDir.createDirectory())
    {
        error = "Couldn't create the output folder: " + outputDir.getFullPathName();
        return false;
    }

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

    // --- Render each non-empty audio track to its own file ---------------------------------
    // One render pass per track. We restrict the otherwise-identical (to renderEditToWav)
    // parameters to a single track via tracksToDo. ScopedRenderStatus is held across the whole
    // batch so the live playback graph stays out of the way for the entire export.
    te::TransportControl::stopAllTransports (engine, false, true);
    te::Renderer::turnOffAllPlugins (edit);

    const te::Edit::ScopedRenderStatus renderStatus (edit, true);

    int renderedCount = 0;
    juce::StringArray failures;

    for (auto* at : audioTracks)
    {
        if (at == nullptr || at->getClips().isEmpty())
            continue;   // skip empty tracks — nothing to print into a stem

        // Sanitise the track name for the filesystem; disambiguate collisions (and untitled
        // tracks) by letting getNonexistentChildFile append a number when the name is taken.
        auto baseName = juce::File::createLegalFileName (at->getName().trim());

        if (baseName.isEmpty())
            baseName = "Track";

        auto destFile = outputDir.getNonexistentChildFile (baseName, ".wav", false);
        destFile.deleteFile();   // render expects to create the file fresh

        te::Renderer::Parameters params (edit);
        params.destFile           = destFile;
        params.audioFormat        = wavFormat;
        params.bitDepth           = 24;
        params.sampleRateForAudio = sampleRate;
        params.blockSizeForAudio  = 512;
        params.time               = te::TimeRange (te::TimePosition(), editLength);
        params.tracksToDo         = singleTrackBitSet (edit, *at);
        params.usePlugins         = true;
        params.useMasterPlugins   = false;   // per-track stems exclude the master chain
        params.canRenderInMono    = false;   // force stereo output
        params.mustRenderInMono   = false;
        params.realTimeRender     = false;
        params.createMidiFile     = false;

        auto task = te::render_utils::createRenderTask (params, "Forge stem export", nullptr, nullptr);

        if (task == nullptr)
        {
            failures.add (at->getName() + " (couldn't initialise the renderer)");
            continue;
        }

        // Drive the task to completion synchronously — no UIBehaviour progress bar, no message loop.
        while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }

        if (task->errorMessage.isNotEmpty())
        {
            destFile.deleteFile();
            failures.add (at->getName() + " (" + task->errorMessage + ")");
            continue;
        }

        if (! destFile.existsAsFile())
        {
            failures.add (at->getName() + " (no file was produced)");
            continue;
        }

        ++renderedCount;
    }

    te::Renderer::turnOffAllPlugins (edit);

    if (renderedCount == 0)
    {
        error = "No stems were exported.";

        if (! failures.isEmpty())
            error << " Failures:\n" << failures.joinIntoString ("\n");

        return false;
    }

    // At least one stem succeeded. If some failed, succeed but report the failures.
    if (! failures.isEmpty())
        error = "Some stems failed to export:\n" + failures.joinIntoString ("\n");

    return true;
}

} // namespace Exporter
