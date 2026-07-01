/*
    GridControlDriver — device-agnostic abstract seam for a grid clip-launch control surface
    (Launchpad, APC40, ...), plus a testable outgoing-MIDI sink.

    This is the "add a driver, not a rework" boundary promised by SlotVisualState.h: the same
    (colourIdx 0-18, state 0/1/2) PadFeedback vocabulary that drives on-screen pads drives LEDs
    here. A concrete driver (LaunchpadDriver) translates that vocabulary to/from a specific
    device's MIDI, and ControlSurfaceHost owns one active driver, polls the session, and pushes
    LED changes — DECOUPLED from any on-screen view.

    THREADING (load-bearing):
      - Incoming device MIDI arrives on a MIDI callback thread. A driver's handleIncomingMidi()
        runs on THAT thread, so it must do nothing but parse and invoke the action callbacks —
        which the host wires to a lock-free hand-off onto the message thread. A driver NEVER
        touches an Edit / ProjectSession / SlotVisualState directly.
      - setPadLed() and the action-callback WIRING are configured on the message thread. The
        actual LED encode+send is called from the host's message-thread poll timer.

    Header-only abstract interface (#pragma once). Include as: #include "engine/GridControlDriver.h"
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Injectable sink for a driver's OUTGOING MIDI (LED updates).

    The default sink wraps a real juce::MidiOutput; a headless self-test injects a capturing
    sink to assert the exact LED bytes with no hardware. A driver owns its sink and sends every
    outgoing message through it, so the byte encoding is identical on hardware and under test. */
struct MidiSink
{
    virtual ~MidiSink() = default;
    virtual void send (const juce::MidiMessage&) = 0;
};

/** MidiSink that forwards to a real juce::MidiOutput (may be null — then send() is a safe no-op,
    so a driver with no hardware out still runs). Message-thread only (matches the poll). */
struct MidiOutputSink  : public MidiSink
{
    explicit MidiOutputSink (juce::MidiOutput* out) noexcept : output (out) {}

    void send (const juce::MidiMessage& m) override
    {
        if (output != nullptr)
            output->sendMessageNow (m);
    }

    juce::MidiOutput* output = nullptr;   // NOT owned; the driver owns the unique_ptr lifetime
};

/** MidiSink that records every message it is handed, for headless assertions. Thread-affine to
    whoever calls send() — the self-test drives it from the message thread. */
struct CapturingMidiSink  : public MidiSink
{
    void send (const juce::MidiMessage& m) override   { messages.add (m); }

    juce::Array<juce::MidiMessage> messages;
};

//==============================================================================
/** Abstract grid control-surface driver.

    A grid controller presents an N-track x M-scene pad matrix plus a scene-launch column. The
    driver:
      - opens/closes its ports by device-name match (start()/stop());
      - decodes incoming device MIDI into high-level grid actions (handleIncomingMidi), emitted
        through the std::function action callbacks;
      - encodes a Forge PadFeedback (colourIdx 0-18, state 0/1/2) as device LED MIDI and sends it
        through the injected MidiSink (setPadLed).

    The host wires the action callbacks to ProjectSession launch/stop calls (marshalled to the
    message thread) and calls setPadLed from its poll. The driver holds NO engine/session state. */
class GridControlDriver
{
public:
    virtual ~GridControlDriver() = default;

    /** Opens the device's MIDI in/out ports by name match. Returns true if an input port opened
        (LED output is best-effort — a device may present only an input). If no matching device is
        present, returns false and the driver is inert (the app still runs). Message-thread only. */
    virtual bool start() = 0;

    /** Closes ports (input first). Idempotent; safe if never started. Message-thread only. */
    virtual void stop() = 0;

    /** True between a successful start() and stop(). */
    virtual bool isOpen() const = 0;

    /** Human-readable driver/device name (diagnostics + status). */
    virtual juce::String name() const = 0;

    /** Grid dimensions the driver exposes: pad columns map to Forge track indices, pad rows to
        scene indices. A host clamps its poll to these AND to the live track/scene counts. */
    virtual int numTrackPads() const = 0;   // grid columns (tracks)
    virtual int numScenePads() const = 0;   // grid rows (scenes)

    /** Parses one incoming device MIDI message and, on a recognised control, invokes the matching
        action callback. Runs on the MIDI callback thread — must be non-blocking and touch NOTHING
        but the callbacks (which marshal to the message thread). Ignores unrecognised messages. */
    virtual void handleIncomingMidi (const juce::MidiMessage&) = 0;

    /** Encodes (trackIndex, sceneIndex, colourIdx 0-18, state 0/1/2) as this device's LED MIDI and
        sends it through the sink. Out-of-grid indices are ignored. Message-thread only (poll). */
    virtual void setPadLed (int trackIndex, int sceneIndex, int colourIdx, int state) = 0;

    //==============================================================================
    // Action callbacks — set by the host BEFORE start(). Invoked from handleIncomingMidi() on the
    // MIDI thread, so the host's handler must marshal to the message thread before touching the
    // session. Left empty they are safe no-ops.

    std::function<void (int trackIndex, int sceneIndex)> onPadPressed;   // a grid pad pressed
    std::function<void (int sceneIndex)>                 onScenePressed; // a scene-launch pad pressed
    std::function<void()>                                onStopAll;      // a global "stop all" control
};
