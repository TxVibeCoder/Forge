#include "engine/LaunchpadDriver.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
// kColourIdxToPalette is a static constexpr member with an in-class initialiser. Under C++17+ it is
// implicitly `inline`, so it needs NO out-of-class definition even when ODR-used.

LaunchpadDriver::LaunchpadDriver (std::unique_ptr<MidiSink> injectedSink)
    : sink (injectedSink != nullptr ? std::move (injectedSink)
                                    : std::unique_ptr<MidiSink> (new MidiOutputSink (nullptr)))
{
}

LaunchpadDriver::~LaunchpadDriver()
{
    stop();
}

//==============================================================================
StringArray LaunchpadDriver::defaultNameMatches()
{
    // Case-insensitive substrings. The MK3/X programmer port is the one that speaks the 11..88
    // note scheme; the plain "Launchpad" match also catches the Mini MK3 and older units. "LPX"
    // is the Launchpad X short name some drivers expose.
    return { "Launchpad", "LPX", "LPMiniMK3" };
}

bool LaunchpadDriver::start()
{
    if (midiInput != nullptr)
        return true;   // already open

    const auto matches = defaultNameMatches();

    auto nameMatches = [&matches] (const String& deviceName)
    {
        for (auto& m : matches)
            if (deviceName.containsIgnoreCase (m))
                return true;
        return false;
    };

    // ---- Open the input (device -> Forge). This is the required half; LED output is best-effort. ----
    for (auto& info : MidiInput::getAvailableDevices())
    {
        if (! nameMatches (info.name))
            continue;

        midiInput = MidiInput::openDevice (info.identifier, this);
        if (midiInput != nullptr)
        {
            midiInput->start();
            FORGE_LOG_INFO ("LaunchpadDriver: opened MIDI input '" + info.name + "'");
            break;
        }

        FORGE_LOG_WARN ("LaunchpadDriver: failed to open MIDI input '" + info.name + "'");
    }

    if (midiInput == nullptr)
    {
        // No hardware — inert but non-fatal (the app runs without a surface).
        FORGE_LOG_INFO ("LaunchpadDriver: no Launchpad input found — control surface inactive");
        return false;
    }

    // ---- Open the output (Forge LEDs -> device). Optional; keep running input-only if it fails. ----
    for (auto& info : MidiOutput::getAvailableDevices())
    {
        if (! nameMatches (info.name))
            continue;

        midiOutput = MidiOutput::openDevice (info.identifier);
        if (midiOutput != nullptr)
        {
            FORGE_LOG_INFO ("LaunchpadDriver: opened MIDI output '" + info.name + "'");
            break;
        }

        FORGE_LOG_WARN ("LaunchpadDriver: failed to open MIDI output '" + info.name + "'");
    }

    // Rebuild the default sink around the (possibly null) real output — UNLESS the caller injected a
    // custom sink (e.g. a CapturingMidiSink). We can only tell "default" apart by identity here, so a
    // caller who wants a capturing sink must set it via setSink() AFTER start(). Default construction
    // gave us a MidiOutputSink(nullptr); replace it now that we may have a real port.
    if (auto* asOutputSink = dynamic_cast<MidiOutputSink*> (sink.get()))
        asOutputSink->output = midiOutput.get();

    return true;
}

void LaunchpadDriver::stop()
{
    if (midiInput != nullptr)
    {
        midiInput->stop();     // stop the callback FIRST so handleIncomingMidiMessage can't re-enter
        midiInput.reset();
    }

    // Detach the sink from a soon-to-be-destroyed output before we drop the port.
    if (auto* asOutputSink = dynamic_cast<MidiOutputSink*> (sink.get()))
        asOutputSink->output = nullptr;

    midiOutput.reset();
}

//==============================================================================
void LaunchpadDriver::handleIncomingMidiMessage (MidiInput*, const MidiMessage& message)
{
    // MIDI THREAD. Parse only; the action callbacks marshal to the message thread.
    handleIncomingMidi (message);
}

void LaunchpadDriver::handleIncomingMidi (const MidiMessage& message)
{
    // Pads + scene column send note-on on press (velocity > 0) and note-off / velocity-0 on release.
    // We act on PRESS only (momentary), so ignore note-offs and velocity-0 note-ons.
    if (! message.isNoteOn())
        return;   // isNoteOn() is false for velocity-0 note-ons and for note-offs

    const int note = message.getNoteNumber();

    // Scene-launch column takes priority (it shares the note-number space but note%10 == 9).
    if (const int scene = noteToScene (note); scene >= 0)
    {
        if (onScenePressed)
            onScenePressed (scene);
        return;
    }

    int track = 0, scene = 0;
    if (noteToCell (note, track, scene))
    {
        if (onPadPressed)
            onPadPressed (track, scene);
    }
    // Unrecognised (function row / off-grid) — ignore.
}

//==============================================================================
void LaunchpadDriver::setPadLed (int trackIndex, int sceneIndex, int colourIdx, int state)
{
    // MESSAGE THREAD (poll). Encode -> device LED note-on and send through the sink. Off-grid → skip.
    // Guard on the CELL (not the message) because a default-constructed MidiMessage is a non-empty
    // sysex, not a zero-length message — cellToNote is the authoritative in-grid test.
    if (cellToNote (trackIndex, sceneIndex) < 0)
        return;

    if (sink != nullptr)
        sink->send (makeLedMessage (trackIndex, sceneIndex, colourIdx, state));
}
