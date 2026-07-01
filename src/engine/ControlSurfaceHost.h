/*
    ControlSurfaceHost — owns ONE active grid control-surface driver and bridges it to a
    ProjectSession, DECOUPLED from any on-screen view.

    Responsibilities:
      1. Wire the driver's action callbacks (onPadPressed / onScenePressed / onStopAll) to
         ProjectSession launch/stop. Those callbacks fire on the driver's MIDI thread, so the
         host marshals every action onto the MESSAGE THREAD via a lock-free FIFO drained by the
         poll timer (NEVER touch ProjectSession off the message thread).
      2. Run a message-thread poll timer (~30 Hz) that, for each pad within BOTH the driver grid
         and the live track/scene counts, re-resolves the live ClipSlot FRESH (const getClipSlot,
         R1/R2 — never caches a ClipSlot or Clip pointer), derives its SlotVisualState + PadFeedback (the
         SAME model the SessionView renders), and pushes setPadLed ONLY when a pad's feedback
         changed since the last send (per-pad debounce — avoids flooding the USB LED bus).

    Robust to a null/absent Edit and to track/scene count changes: it re-resolves every tick and
    clamps indices; a pad that falls outside the live grid is driven to "off" once, then skipped.

    Teardown (load-bearing): stop the TIMER first, then close the driver (mirrors the SessionView
    R1 teardown discipline — kill the poll before releasing what it reads).

    Message-thread only for construction/destruction and the poll. Include as:
        #include "engine/ControlSurfaceHost.h"
*/

#pragma once

#include <JuceHeader.h>
#include "engine/GridControlDriver.h"
#include "services/files/ProjectSession.h"
#include "ui/session/SlotVisualState.h"

class ControlSurfaceHost  : private juce::Timer
{
public:
    /** @param sessionToControl  the session this surface launches clips in (message-thread lifetime).
        @param driverToOwn       the concrete driver (e.g. LaunchpadDriver). Ownership taken. If null,
                                 the host is inert (no timer, no driver).
        @param openNow           when true, opens the driver + starts the poll immediately (interactive
                                 mode). A headless test passes false and drives begin()/pollOnce() itself. */
    ControlSurfaceHost (ProjectSession& sessionToControl,
                        std::unique_ptr<GridControlDriver> driverToOwn,
                        bool openNow = true)
        : session (sessionToControl),
          driver (std::move (driverToOwn))
    {
        wireDriverCallbacks();

        if (openNow)
            begin();
    }

    ~ControlSurfaceHost() override
    {
        // TIMER FIRST (so the poll can't run against a half-closed driver), then close the driver.
        stopTimer();
        if (driver != nullptr)
            driver->stop();
    }

    /** Opens the driver (best-effort — a missing device is a no-op, not a failure) and starts the
        ~30 Hz message-thread LED poll. Idempotent. Message-thread only. */
    void begin()
    {
        if (driver == nullptr)
            return;

        // Only poll if a device actually opened. With no controller present (the current reality —
        // hardware is a future capability) a 30 Hz full-grid LED poll would burn CPU for nothing, so
        // stay fully inert until a driver opens. start() scans once, so hot-plugging a controller
        // after launch needs a session restart (a documented limitation, acceptable pre-hardware).
        if (! driver->start())            // logs + returns false if no device is present
            return;

        lastFeedback.clear();             // force a full LED refresh on the first tick
        startTimerHz (kPollHz);
    }

    /** Stops the poll and closes the driver. Idempotent. Message-thread only. */
    void end()
    {
        stopTimer();
        if (driver != nullptr)
            driver->stop();
    }

    /** True if a driver is present and its input is open. */
    bool isActive() const   { return driver != nullptr && driver->isOpen(); }

    /** The owned driver (for a self-test to inject input / swap the LED sink). May be null. */
    GridControlDriver* getDriver() const   { return driver.get(); }

    /** Runs ONE LED poll pass synchronously (the exact body the timer runs). A headless test calls
        this to prove the output path deterministically instead of waiting on the timer. */
    void pollOnce()   { pushLedUpdates(); }

    /** Drains any queued input actions onto the message thread NOW (the timer also drains each tick).
        Lets a headless test flush an injected pad-press without spinning the message loop by hand. */
    void drainActions()   { drainPendingActions(); }

private:
    //==============================================================================
    static constexpr int kPollHz    = 30;  // LED refresh rate (message thread)
    static constexpr int kMaxScenes = 64;  // stride for the debounce cache key (track*kMaxScenes + scene)

    // A queued grid action, produced on the MIDI thread and consumed on the message thread.
    struct Action
    {
        enum class Type { pad, scene, stopAll };
        Type type = Type::pad;
        int  a = 0;   // pad: track  / scene: scene / stopAll: unused
        int  b = 0;   // pad: scene  / (unused otherwise)
    };

    void wireDriverCallbacks()
    {
        if (driver == nullptr)
            return;

        // These lambdas run on the MIDI thread — do NOTHING but enqueue (drained by the poll timer).
        driver->onPadPressed   = [this] (int t, int s) { enqueue ({ Action::Type::pad,     t, s }); };
        driver->onScenePressed = [this] (int s)        { enqueue ({ Action::Type::scene,   s, 0 }); };
        driver->onStopAll      = [this]                { enqueue ({ Action::Type::stopAll,  0, 0 }); };
    }

    // MIDI-thread producer: push into the lock-free FIFO. The 30 Hz message-thread poll drains it
    // (≤33 ms later — imperceptible, and clip launches quantise anyway). We deliberately do NOT wake
    // the message thread from here: a raw `this`-capturing callAsync would race the host's destructor,
    // and the host is not a Component (no SafePointer). If the FIFO is momentarily full we drop the
    // action — benign, and vanishingly rare at human press rates against a 128-slot ring.
    void enqueue (const Action& action)
    {
        const auto scope = actionFifo.write (1);
        if (scope.blockSize1 > 0)
            actions[(size_t) scope.startIndex1] = action;
        // else: FIFO full — drop (see note above). scope's dtor calls finishedWrite for what fit.
    }

    // MESSAGE-THREAD consumer: apply every queued action against the session.
    void drainPendingActions()
    {
        int start1, size1, start2, size2;
        const int ready = actionFifo.getNumReady();
        if (ready <= 0)
            return;

        actionFifo.prepareToRead (ready, start1, size1, start2, size2);

        auto apply = [this] (const Action& action)
        {
            switch (action.type)
            {
                case Action::Type::pad:     session.launchSlot (action.a, action.b); break;
                case Action::Type::scene:   session.launchScene (action.a);          break;
                case Action::Type::stopAll: session.stopAllSlots();                  break;
            }
        };

        for (int i = 0; i < size1; ++i) apply (actions[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i) apply (actions[(size_t) (start2 + i)]);

        actionFifo.finishedRead (size1 + size2);
    }

    //==============================================================================
    // juce::Timer — message thread. Drain queued presses first (so a press acts this tick), then push
    // any LED changes.
    void timerCallback() override
    {
        drainPendingActions();
        pushLedUpdates();
    }

    // Re-resolve each in-grid pad FRESH, derive its PadFeedback, and setPadLed only on a change.
    void pushLedUpdates()
    {
        if (driver == nullptr)
            return;

        auto* edit = session.getEdit();
        auto* transport = session.getTransport();
        const bool transportRunning = transport != nullptr && transport->isPlaying();

        const int gridTracks = driver->numTrackPads();
        const int gridScenes = driver->numScenePads();

        // Live grid extent (clamped to the driver grid). A null edit → 0 live tracks/scenes, so every
        // pad resolves empty and is driven off (once).
        juce::Array<te::AudioTrack*> tracks;
        if (edit != nullptr)
            tracks = te::getAudioTracks (*edit);

        const int liveTracks = tracks.size();

        for (int t = 0; t < gridTracks; ++t)
        {
            // Per-track arm + colour resolved once (scene-invariant). A pad past the live track count
            // is treated as empty/off.
            const bool trackLive = (t < liveTracks) && (tracks[t] != nullptr);
            const juce::Colour trackColour = trackLive ? tracks[t]->getColour() : juce::Colour();

            for (int s = 0; s < gridScenes; ++s)
            {
                // R1/R2: resolve the live slot FRESH via the const, non-mutating seam; never stored.
                te::ClipSlot* slot = trackLive ? session.getClipSlot (t, s) : nullptr;

                // recording-here / arm are message-thread engine reads via the session seams. We do
                // NOT have a record-arm read on the const path here, so drive from the launch/clip
                // state only (armed=false, recordingHere=false) — LEDs still reflect play/queue/clip.
                // A future arm read can be threaded in without changing the LED encoding.
                const SlotVisualState state = computeSlotState (slot, transportRunning,
                                                                /*armed=*/ false, /*recordingHere=*/ false);

                const PadFeedback fb = toPadFeedback (t, s, state, trackColour);

                // Debounce: only send when this pad's feedback changed since the last send.
                const int key = t * kMaxScenes + s;
                auto it = lastFeedback.find (key);
                if (it != lastFeedback.end()
                    && it->second.colourIdx == fb.colourIdx
                    && it->second.state == fb.state)
                    continue;

                lastFeedback[key] = fb;
                driver->setPadLed (t, s, fb.colourIdx, fb.state);
            }
        }
    }

    //==============================================================================
    ProjectSession& session;
    std::unique_ptr<GridControlDriver> driver;

    // Lock-free single-producer (MIDI thread) / single-consumer (message thread) action queue.
    static constexpr int kFifoSize = 128;
    juce::AbstractFifo actionFifo { kFifoSize };
    std::array<Action, (size_t) kFifoSize> actions {};

    // Per-pad LED debounce cache: key = track*kMaxScenes + scene → last-sent feedback.
    std::map<int, PadFeedback> lastFeedback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSurfaceHost)
};
