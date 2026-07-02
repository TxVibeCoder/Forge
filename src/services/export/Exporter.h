/*
    Exporter — renders the current Edit to WAV file(s) on disk.

    Two flavours share one render recipe (built on Tracktion's te::Renderer / RenderTask):

      - SYNCHRONOUS (renderEditToWav / renderStems): a thin, blocking wrapper that drives the
        RenderTask to completion on the calling thread. No progress, no message-loop dependency.
        Kept intact for callers/selftests that want a simple blocking render.

      - ASYNCHRONOUS (renderEditToWavAsync / renderStemsAsync): runs the same recipe with the
        render loop on a background thread, reporting progress (0..1) and a completion callback
        back on the message thread, and supporting cancel. Returns an AsyncRender handle the
        shell owns for the lifetime of the export.

    Both are MESSAGE-THREAD entry points. For the async path, everything that Tracktion requires
    on the message thread (stopAllTransports, turnOffAllPlugins, ScopedRenderStatus, and each
    createRenderTask) runs on the message thread; only the per-block RenderTask::runJob() loop
    runs on the worker thread — mirroring the engine's own EditRenderer.
*/

#pragma once

#include <JuceHeader.h>

#include "engine/dsp/LoudnessAnalyzer.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace te = tracktion;

namespace Exporter
{
    /** Integrated-loudness measurement of a completed whole-edit render (BS.1770-4). Surfaced
        additively alongside the existing success surface. Only the whole-edit render carries the
        master chain, so this is populated for whole-edit exports (sync + async), not per-track
        stems. See forge::dsp::LoudnessAnalyzer. */
    using LoudnessResult = forge::dsp::LoudnessAnalyzer::Result;

    //==============================================================================
    // Synchronous / blocking API (unchanged) ------------------------------------------------

    /** Renders the edit's full content to a 24-bit WAV at outFile.

        Synchronous/blocking — renders on the calling thread (acceptable for now). Renders the
        range [0, edit.getLength()] (i.e. start of the edit to the end of the last clip), mixing
        all tracks down to stereo at the Edit's current sample rate.

        Returns true on success. On failure returns false and sets `error` to an explanatory
        message. An edit with no clips returns false with an explanatory error.

        If `loudnessOut` is non-null and the render succeeds, the produced WAV is analysed for its
        BS.1770-4 integrated loudness + true peak and the result is written there (additive — a null
        pointer, the default, preserves the original behaviour exactly). */
    bool renderEditToWav (te::Edit& edit, const juce::File& outFile, juce::String& error,
                          LoudnessResult* loudnessOut = nullptr);

    /** Renders each audio track in the edit to its own 24-bit WAV file inside outputDir.

        Synchronous/blocking — renders on the calling thread, same recipe as renderEditToWav but
        restricted to a single track per file (one render pass per track). Audio tracks that have
        no clips are skipped. Each file is named after its track (the name is sanitised for the
        filesystem and disambiguated against collisions). Renders the range [0, edit.getLength()]
        at the Edit's current sample rate, forced to stereo.

        outputDir is created if it doesn't exist.

        Returns true if at least one stem renders. Returns false (with an explanatory `error`) if
        the edit has no audio tracks, no track has any clips, or every render fails. If some stems
        render and others fail, returns true but appends the per-track failures to `error`. */
    bool renderStems (te::Edit& edit, const juce::File& outputDir, juce::String& error);

    //==============================================================================
    // Asynchronous API ----------------------------------------------------------------------

    /** A single render pass — one output file rendered from a chosen set of tracks. Whole-edit
        export is one pass over all tracks; stem export is one pass per non-empty audio track.
        Built entirely on the message thread by the factory functions below. */
    struct RenderPass
    {
        juce::File       destFile;
        juce::BigInteger tracksToDo;             // renderer bitset, indexed against te::getAllTracks
        bool             useMasterPlugins = true;
        juce::String     label;                  // human name for status / failure reporting
    };

    /** Handle for an in-flight asynchronous export.

        MESSAGE-THREAD ONLY (except the private render worker): construct via the factory
        functions, keep it alive for the duration, and destroy it on the message thread. The
        onProgress / onComplete callbacks always fire on the message thread. onComplete fires
        exactly once (unless the handle is destroyed first, which suppresses it). Destroying the
        handle cancels the render and joins the worker.

        Progress is aggregate across all passes (0..1). A render that produced no measurable
        progress simply stays near its current value; the bar is otherwise determinate. */
    class AsyncRender : private juce::Timer
    {
    public:
        // Constructed by renderEditToWavAsync / renderStemsAsync — not intended for direct use.
        AsyncRender (te::Edit& edit,
                     juce::String taskDescription,
                     bool stems,
                     double sampleRate,
                     juce::AudioFormat* wavFormat,
                     te::TimeRange timeRange,
                     std::vector<RenderPass> passes);

        ~AsyncRender() override;

        /** Sets the engine up (stop transports, deinit plugins, ScopedRenderStatus) and starts
            the first pass + the progress timer. Called by the factory on the message thread. */
        void begin();

        /** Requests cancellation. The current RenderTask aborts within one block (its partial
            file is deleted) and onComplete fires with ok=false. Idempotent; message thread. */
        void cancel();

        /** True until the export has finished (successfully, by failure, or by cancel). */
        bool isRunning() const noexcept   { return ! finished; }

        /** Fired on the message thread with aggregate progress 0..1 while rendering. */
        std::function<void (float)> onProgress;

        /** Fired once on the message thread when the export finishes: (ok, error). On partial
            stem success ok is true and error carries the per-stem failures. */
        std::function<void (bool, juce::String)> onComplete;

        /** Additive: fired on the message thread just BEFORE onComplete, only for a successful
            whole-edit (non-stem) export, carrying the rendered file's BS.1770-4 integrated loudness.
            The analysis itself runs on the render worker (off the message thread) and is delivered
            here already computed, so it never blocks the UI. Never fires for stems (they exclude the
            master chain) or for a failed/cancelled export (a cancel mid-analysis drops the value).
            Optional — leave unset to ignore. The same value is also available via getLoudness(). */
        std::function<void (LoudnessResult)> onLoudness;

        /** The whole-edit loudness measured at completion, if any (see onLoudness). Empty for
            stems, failures, cancellations, or before completion. Message-thread read. */
        std::optional<LoudnessResult> getLoudness() const  { return loudness; }

    private:
        void timerCallback() override;
        void startPass (int index);
        void onPassWorkerFinished (bool ok, juce::String error, std::optional<LoudnessResult> measured = {});
        void finishAll();
        void teardownEngineState();

        struct PassThread;   // background render worker — defined in the .cpp

        te::Edit&               edit;
        juce::String            desc;
        bool                    isStems = false;
        double                  sampleRate = 44100.0;
        juce::AudioFormat*      wavFormat = nullptr;
        te::TimeRange           timeRange;
        std::vector<RenderPass> passes;

        std::unique_ptr<te::Edit::ScopedRenderStatus> renderStatus;
        std::unique_ptr<te::Renderer::RenderTask>     currentTask;
        std::unique_ptr<PassThread>                   worker;

        std::atomic<float> passProgress { 0.0f };   // written by the RenderTask on the worker
        std::atomic<bool>  cancelRequested { false };

        // Liveness token: a completion callAsync captures a copy and checks it, so a message that
        // is delivered after the handle is destroyed becomes a safe no-op.
        std::shared_ptr<std::atomic<bool>> alive;

        int  currentPass   = 0;
        int  renderedCount = 0;
        bool finished      = false;
        juce::StringArray failures;      // labelled per-pass failures (stems)
        juce::String      primaryError;  // the single error (whole-edit)

        std::optional<LoudnessResult> loudness;   // whole-edit integrated loudness at completion

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AsyncRender)
    };

    /** Starts an asynchronous whole-edit render to a 24-bit WAV at outFile. Same recipe as
        renderEditToWav but off the message thread with progress + cancel.

        Returns a running handle on success. Returns nullptr (and sets `error`) for a pre-flight
        failure (nothing to export, no output file, no WAV format) — in that case no callback
        fires. Once a handle is returned, the outcome arrives via handle->onComplete. Call on the
        message thread. */
    std::unique_ptr<AsyncRender> renderEditToWavAsync (te::Edit& edit, const juce::File& outFile, juce::String& error);

    /** Starts an asynchronous per-track stem render into outputDir. Same recipe as renderStems
        but off the message thread with progress + cancel.

        Returns a running handle on success. Returns nullptr (and sets `error`) for a pre-flight
        failure (empty edit, no audio tracks, no clips, no output folder, no WAV format). Once a
        handle is returned, the outcome arrives via handle->onComplete (ok=true with a failures
        list if some stems fail). Call on the message thread. */
    std::unique_ptr<AsyncRender> renderStemsAsync (te::Edit& edit, const juce::File& outputDir, juce::String& error);
}
