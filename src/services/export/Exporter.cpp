#include "services/export/Exporter.h"

#include "core/Log.h"

#include <atomic>
#include <memory>
#include <vector>

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

    // Picks a filesystem-legal, collision-free "<name>.wav" child of dir. Unlike
    // File::getNonexistentChildFile (which the sync stem path leans on, disambiguating against
    // files it has already written), the async path resolves every stem name up front — before
    // any file exists — so it must also disambiguate against names it has already handed out this
    // batch. usedLowerNames accumulates the chosen lower-case filenames across the batch.
    juce::File uniqueStemFile (const juce::File& dir, const juce::String& trackName,
                               juce::StringArray& usedLowerNames)
    {
        auto baseName = juce::File::createLegalFileName (trackName.trim());

        if (baseName.isEmpty())
            baseName = "Track";

        auto candidate = baseName;

        for (int n = 2; ; ++n)
        {
            auto fileName = candidate + ".wav";

            if (! usedLowerNames.contains (fileName.toLowerCase())
                && ! dir.getChildFile (fileName).existsAsFile())
            {
                usedLowerNames.add (fileName.toLowerCase());
                return dir.getChildFile (fileName);
            }

            candidate = baseName + " (" + juce::String (n) + ")";
        }
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

    if (! outFile.getParentDirectory().createDirectory())
        FORGE_LOG_WARN ("Failed to create output directory: " + outFile.getParentDirectory().getFullPathName());

    if (outFile.existsAsFile() && ! outFile.deleteFile())   // overwrite any existing file; render expects to create it fresh
        FORGE_LOG_WARN ("Failed to delete existing file " + outFile.getFullPathName() + " — it may be locked or read-only");

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
        FORGE_LOG_ERROR ("Couldn't initialise the renderer.");
        error = "Couldn't initialise the renderer.";
        return false;
    }

    FORGE_LOG_INFO ("Exporting audio: " + juce::String (countClipsOnAudioTracks (edit))
                    + " clips, " + juce::String (editLength.inSeconds()) + "s");

    // Drive the task to completion synchronously — no UIBehaviour progress bar, no message loop.
    while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {
    }

    te::Renderer::turnOffAllPlugins (edit);

    if (task->errorMessage.isNotEmpty())
    {
        outFile.deleteFile();
        FORGE_LOG_ERROR ("Export render failed: " + task->errorMessage);
        error = task->errorMessage;
        return false;
    }

    if (! outFile.existsAsFile())
    {
        FORGE_LOG_ERROR ("The render finished but no file was produced.");
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
        FORGE_LOG_ERROR ("Couldn't create the output folder: " + outputDir.getFullPathName());
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

        if (destFile.existsAsFile() && ! destFile.deleteFile())   // render expects to create the file fresh
            FORGE_LOG_WARN ("Failed to delete existing stem file " + destFile.getFullPathName());

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
            FORGE_LOG_WARN ("Couldn't initialize renderer for track '" + at->getName() + "' — skipping this stem");
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
            FORGE_LOG_WARN ("Stem render failed for '" + at->getName() + "': " + task->errorMessage);
            failures.add (at->getName() + " (" + task->errorMessage + ")");
            continue;
        }

        if (! destFile.existsAsFile())
        {
            FORGE_LOG_WARN ("Render for track '" + at->getName() + "' finished but no file was produced");
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

//==============================================================================
// Asynchronous export -----------------------------------------------------------------------

// The background render worker. It owns nothing: it holds references to the AsyncRender that
// created it and to the RenderTask (which the AsyncRender owns on the message thread). It only
// runs the per-block runJob() loop off the message thread, then marshals the pass result back.
//
// Cancellation is delivered by AsyncRender::cancel() / the destructor calling
// task.signalJobShouldExit() on the message thread — renderNextBlock() sees shouldExit() within
// one block, deletes the partial file, and runJob() returns jobHasFinished, so the loop exits.
struct AsyncRender::PassThread : public juce::Thread
{
    PassThread (AsyncRender& ownerIn, te::Renderer::RenderTask& taskIn)
        : juce::Thread ("Forge export render"), owner (ownerIn), task (taskIn) {}

    void run() override
    {
        while (! threadShouldExit()
               && task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }

        bool ok = true;
        juce::String err;

        if (threadShouldExit() || task.shouldExit())
        {
            ok = false;
            err = "Export cancelled.";
        }
        else if (task.errorMessage.isNotEmpty())
        {
            ok = false;
            err = task.errorMessage;
        }
        else if (! task.params.destFile.existsAsFile())
        {
            ok = false;
            err = "The render finished but no file was produced.";
        }

        // Marshal the result back to the message thread. The liveness token makes this a no-op if
        // the handle was destroyed before the message is delivered.
        auto aliveToken = owner.alive;
        auto* self = &owner;

        juce::MessageManager::callAsync ([aliveToken, self, ok, err]
        {
            if (aliveToken != nullptr && aliveToken->load())
                self->onPassWorkerFinished (ok, err);
        });
    }

    AsyncRender& owner;
    te::Renderer::RenderTask& task;
};

AsyncRender::AsyncRender (te::Edit& editIn,
                          juce::String taskDescription,
                          bool stems,
                          double sampleRateIn,
                          juce::AudioFormat* wavFormatIn,
                          te::TimeRange timeRangeIn,
                          std::vector<RenderPass> passesIn)
    : edit (editIn),
      desc (std::move (taskDescription)),
      isStems (stems),
      sampleRate (sampleRateIn),
      wavFormat (wavFormatIn),
      timeRange (timeRangeIn),
      passes (std::move (passesIn)),
      alive (std::make_shared<std::atomic<bool>> (true))
{
}

AsyncRender::~AsyncRender()
{
    // Must run on the message thread (the shell owns this handle there): stop the progress poll,
    // cancel + join the worker, then tear down the render state (ScopedRenderStatus dtor
    // reallocates the playback context, which asserts the message thread).
    stopTimer();

    cancelRequested = true;

    if (currentTask != nullptr)
        currentTask->signalJobShouldExit();

    if (worker != nullptr)
    {
        worker->stopThread (5000);
        worker.reset();
    }

    currentTask.reset();

    if (! finished)
        teardownEngineState();

    if (alive != nullptr)
        alive->store (false);
}

void AsyncRender::begin()
{
    // Message-thread engine setup — mirrors the sync recipe's preamble.
    te::TransportControl::stopAllTransports (edit.engine, false, true);
    te::Renderer::turnOffAllPlugins (edit);

    // reallocateOnDestruction = true: our ScopedRenderStatus is destroyed on the message thread
    // (finishAll / dtor), so restoring the live playback context on teardown is safe.
    renderStatus = std::make_unique<te::Edit::ScopedRenderStatus> (edit, true);

    FORGE_LOG_INFO ("Exporting audio (async): " + juce::String ((int) passes.size())
                    + (isStems ? " stem(s)" : " pass"));

    startTimerHz (25);
    startPass (0);
}

void AsyncRender::cancel()
{
    if (finished)
        return;

    cancelRequested = true;

    if (currentTask != nullptr)
        currentTask->signalJobShouldExit();

    // The running worker finishes and posts onPassWorkerFinished, which routes to finishAll. If no
    // worker is live (between passes / a create failure), close out now.
    if (worker == nullptr)
        finishAll();
}

void AsyncRender::timerCallback()
{
    if (! onProgress)
        return;

    const float pp = juce::jlimit (0.0f, 1.0f, passProgress.load());
    const float aggregate = passes.empty() ? 0.0f
                                           : ((float) currentPass + pp) / (float) passes.size();
    onProgress (aggregate);
}

void AsyncRender::startPass (int index)
{
    currentPass  = index;
    passProgress = 0.0f;

    const auto& pass = passes[(size_t) index];

    te::Renderer::Parameters params (edit);
    params.destFile           = pass.destFile;
    params.audioFormat        = wavFormat;
    params.bitDepth           = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio  = 512;
    params.time               = timeRange;
    params.tracksToDo         = pass.tracksToDo;
    params.usePlugins         = true;
    params.useMasterPlugins   = pass.useMasterPlugins;
    params.canRenderInMono    = false;   // force stereo output
    params.mustRenderInMono   = false;
    params.realTimeRender     = false;
    params.createMidiFile     = false;

    // createRenderTask builds the node graph via callBlocking on the message thread — so this MUST
    // stay on the message thread (it is: startPass runs from begin() / onPassWorkerFinished). It
    // writes render progress into passProgress as it runs.
    currentTask = te::render_utils::createRenderTask (params, desc, &passProgress, nullptr);

    if (currentTask == nullptr)
    {
        FORGE_LOG_ERROR ("Couldn't initialise the renderer for '" + pass.label + "'.");

        // Defer so we never recurse through startPass -> onPassWorkerFinished -> startPass.
        auto aliveToken = alive;
        auto* self = this;

        juce::MessageManager::callAsync ([aliveToken, self]
        {
            if (aliveToken != nullptr && aliveToken->load())
                self->onPassWorkerFinished (false, "Couldn't initialise the renderer.");
        });

        return;
    }

    worker = std::make_unique<PassThread> (*this, *currentTask);

    if (! worker->startThread())
    {
        // OS couldn't spawn the render thread → run() will never fire, so we must post the
        // completion ourselves (same deferred path as a create failure) or the export would hang
        // with onComplete never called. Drop back to a clean (worker/task null) state first.
        FORGE_LOG_ERROR ("Couldn't start the render thread for '" + pass.label + "'.");
        worker.reset();
        currentTask.reset();

        auto aliveToken = alive;
        auto* self = this;

        juce::MessageManager::callAsync ([aliveToken, self]
        {
            if (aliveToken != nullptr && aliveToken->load())
                self->onPassWorkerFinished (false, "Couldn't start the render thread.");
        });
    }
}

void AsyncRender::onPassWorkerFinished (bool ok, juce::String error)
{
    if (finished)
        return;

    if (worker != nullptr)
    {
        worker->stopThread (5000);
        worker.reset();
    }

    currentTask.reset();   // message-thread destroy, as the sync path does each loop iteration

    if (ok)
    {
        ++renderedCount;
    }
    else if (! cancelRequested.load())
    {
        FORGE_LOG_ERROR ("Export pass failed for '" + passes[(size_t) currentPass].label + "': " + error);

        if (isStems)
            failures.add (passes[(size_t) currentPass].label + " (" + error + ")");
        else
            primaryError = error;
    }

    if (cancelRequested.load())
    {
        finishAll();
        return;
    }

    if (currentPass + 1 < (int) passes.size())
    {
        startPass (currentPass + 1);
        return;
    }

    finishAll();
}

void AsyncRender::finishAll()
{
    if (finished)
        return;

    finished = true;
    stopTimer();

    if (worker != nullptr)
    {
        worker->stopThread (5000);
        worker.reset();
    }

    currentTask.reset();
    teardownEngineState();

    bool ok = false;
    juce::String error;

    if (cancelRequested.load())
    {
        ok = false;
        error = "Export cancelled.";
    }
    else if (isStems)
    {
        if (renderedCount == 0)
        {
            ok = false;
            error = "No stems were exported.";

            if (! failures.isEmpty())
                error << " Failures:\n" << failures.joinIntoString ("\n");
        }
        else
        {
            ok = true;   // at least one stem — partial failures are reported but non-fatal

            if (! failures.isEmpty())
                error = "Some stems failed to export:\n" + failures.joinIntoString ("\n");
        }
    }
    else
    {
        ok = (renderedCount >= 1);

        if (! ok)
            error = primaryError.isNotEmpty() ? primaryError : "Export failed.";
    }

    // Fire the user callback LAST, from a stack-local copy. The shell may destroy this handle from
    // inside onComplete (resetting its owning unique_ptr) — which would free the std::function's own
    // storage while the closure is still executing. Moving the callable onto the stack first makes
    // the closure's state outlive ~AsyncRender; nothing below touches *this afterwards.
    if (onComplete)
    {
        auto callback = std::move (onComplete);
        callback (ok, error);
    }
}

void AsyncRender::teardownEngineState()
{
    te::Renderer::turnOffAllPlugins (edit);
    renderStatus.reset();   // dtor on the message thread → reallocates the live playback context
}

std::unique_ptr<AsyncRender> renderEditToWavAsync (te::Edit& edit, const juce::File& outFile, juce::String& error)
{
    error.clear();

    const auto editLength = edit.getLength();

    if (countClipsOnAudioTracks (edit) == 0 || editLength <= te::TimeDuration())
    {
        error = "Nothing to export: the project has no audio clips.";
        return nullptr;
    }

    if (outFile == juce::File())
    {
        error = "No output file was specified.";
        return nullptr;
    }

    if (! outFile.getParentDirectory().createDirectory())
        FORGE_LOG_WARN ("Failed to create output directory: " + outFile.getParentDirectory().getFullPathName());

    if (outFile.existsAsFile() && ! outFile.deleteFile())   // render expects to create the file fresh
        FORGE_LOG_WARN ("Failed to delete existing file " + outFile.getFullPathName() + " — it may be locked or read-only");

    auto& engine = edit.engine;

    auto* wavFormat = engine.getAudioFileFormatManager().getWavFormat();

    if (wavFormat == nullptr)
    {
        error = "No WAV audio format is available in the engine.";
        return nullptr;
    }

    auto sampleRate = engine.getDeviceManager().getSampleRate();

    if (sampleRate < 8000.0)
        sampleRate = 44100.0;   // device not open / nonsense rate — fall back to a safe default

    std::vector<RenderPass> passes;
    RenderPass pass;
    pass.destFile         = outFile;
    pass.tracksToDo       = te::toBitSet (te::getAllTracks (edit));
    pass.useMasterPlugins = true;
    pass.label            = outFile.getFileName();
    passes.push_back (std::move (pass));

    auto handle = std::make_unique<AsyncRender> (edit, "Forge export", /*stems*/ false, sampleRate,
                                                 wavFormat, te::TimeRange (te::TimePosition(), editLength),
                                                 std::move (passes));
    handle->begin();
    return handle;
}

std::unique_ptr<AsyncRender> renderStemsAsync (te::Edit& edit, const juce::File& outputDir, juce::String& error)
{
    error.clear();

    const auto editLength = edit.getLength();

    if (editLength <= te::TimeDuration())
    {
        error = "Nothing to export: the project is empty.";
        return nullptr;
    }

    auto audioTracks = te::getAudioTracks (edit);

    if (audioTracks.isEmpty())
    {
        error = "Nothing to export: the project has no audio tracks.";
        return nullptr;
    }

    if (countClipsOnAudioTracks (edit) == 0)
    {
        error = "Nothing to export: the project has no audio clips.";
        return nullptr;
    }

    if (outputDir == juce::File())
    {
        error = "No output folder was specified.";
        return nullptr;
    }

    if (! outputDir.createDirectory())
    {
        FORGE_LOG_ERROR ("Couldn't create the output folder: " + outputDir.getFullPathName());
        error = "Couldn't create the output folder: " + outputDir.getFullPathName();
        return nullptr;
    }

    auto& engine = edit.engine;

    auto* wavFormat = engine.getAudioFileFormatManager().getWavFormat();

    if (wavFormat == nullptr)
    {
        error = "No WAV audio format is available in the engine.";
        return nullptr;
    }

    auto sampleRate = engine.getDeviceManager().getSampleRate();

    if (sampleRate < 8000.0)
        sampleRate = 44100.0;

    // Resolve every non-empty track's pass up front, on the message thread (names disambiguated
    // against each other since none of the files exist yet, and any stale same-name file is
    // pre-deleted so the render creates it fresh).
    std::vector<RenderPass> passes;
    juce::StringArray usedNames;

    for (auto* at : audioTracks)
    {
        if (at == nullptr || at->getClips().isEmpty())
            continue;   // skip empty tracks — nothing to print into a stem

        auto destFile = uniqueStemFile (outputDir, at->getName(), usedNames);

        if (destFile.existsAsFile() && ! destFile.deleteFile())
            FORGE_LOG_WARN ("Failed to delete existing stem file " + destFile.getFullPathName());

        RenderPass pass;
        pass.destFile         = destFile;
        pass.tracksToDo       = singleTrackBitSet (edit, *at);
        pass.useMasterPlugins = false;   // per-track stems exclude the master chain
        pass.label            = at->getName();
        passes.push_back (std::move (pass));
    }

    if (passes.empty())
    {
        error = "Nothing to export: no track has any clips.";
        return nullptr;
    }

    auto handle = std::make_unique<AsyncRender> (edit, "Forge stem export", /*stems*/ true, sampleRate,
                                                 wavFormat, te::TimeRange (te::TimePosition(), editLength),
                                                 std::move (passes));
    handle->begin();
    return handle;
}

} // namespace Exporter
