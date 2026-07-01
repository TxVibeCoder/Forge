/*
    LaunchpadDriver — concrete GridControlDriver for the Novation Launchpad Mini MK3 / Launchpad X
    (programmer mode). Built to the PUBLISHED Novation programmer-mode MIDI spec; NO device was on
    hand, so the byte-level mapping needs real-hardware confirmation (flagged in results).

    ---------------------------------------------------------------------------------------------
    LAUNCHPAD PROGRAMMER-MODE MIDI (spec references: Novation "Launchpad Mini MK3 Programmer's
    Reference Manual" and "Launchpad X Programmer's Reference Manual"):

      Pad addressing — the 9x9 surface is addressed by a two-digit note number "RC" where R is the
      1-based row from the BOTTOM (1..9) and C is the 1-based column from the LEFT (1..9):
          note = R*10 + C
      The central 8x8 grid is rows 1..8, columns 1..8 → notes 11..88 (skipping any digit 0 or 9).
      Row 9 (notes 91..98) is the top function row (arrows / logo); column 9 (notes 19,29,...,89)
      is the RIGHT-hand scene-launch column. (The top-left origin the SessionView uses is the
      OPPOSITE row order, so we flip: Forge scene 0 = top row = Launchpad row 8.)

      Pads/CC — in programmer mode the 8x8 pads and the right scene column send NOTE ON (velocity
      > 0 on press) / NOTE OFF (or note-on velocity 0 on release) on channel 1. The top function
      row (row 9) sends CC, not notes. We only consume the 8x8 grid + the scene column, both notes.

      LED control — a NOTE ON addressed to a pad's note sets its colour from a 0..127 velocity
      PALETTE index, and the MIDI CHANNEL selects the lighting behaviour:
          channel 1 (status 0x90) → static (solid)
          channel 2 (status 0x91) → flashing (blink)
          channel 3 (status 0x92) → pulsing (pulse)
      Velocity 0 turns the pad off. This channel-encodes-behaviour scheme is the SIMPLEST correct
      option (SysEx RGB is also available but unnecessary for an 18-hue palette), so we use it.

    FORGE → LAUNCHPAD MAPPING:
      colourIdx (0..18):  0 → off (velocity 0). 1..18 → a fixed palette-index table spanning the
                          Launchpad colour wheel (see kColourIdxToPalette). colourIdx tracks a
                          quantised track hue (SlotVisualState::toPadFeedback), so exact hue match
                          is not required — only a stable, distinguishable colour per index.
      state (0/1/2):      0 solid → channel 1; 1 blink → channel 2; 2 pulse → channel 3.

    Incoming: a grid note-on (velocity > 0) whose note decodes to a valid (track,scene) inside the
    8x8 → onPadPressed. A scene-column note-on → onScenePressed. Note-offs and velocity-0 note-ons
    are ignored (pads are momentary — we launch on press only).

    THREADING: handleIncomingMidi runs on the MIDI thread and only invokes the (host-marshalled)
    action callbacks. start()/stop()/setPadLed run on the message thread.
    ---------------------------------------------------------------------------------------------
*/

#pragma once

#include "engine/GridControlDriver.h"

class LaunchpadDriver  : public GridControlDriver,
                         private juce::MidiInputCallback
{
public:
    /** @param sink   outgoing-LED sink. If null, a MidiOutputSink(nullptr) no-op sink is used until
                      start() opens a real output (start() rebuilds the default sink around the opened
                      port). A caller (self-test) may inject a CapturingMidiSink to record LEDs. */
    explicit LaunchpadDriver (std::unique_ptr<MidiSink> sink = nullptr);
    ~LaunchpadDriver() override;

    //==============================================================================
    // GridControlDriver
    bool         start() override;
    void         stop() override;
    bool         isOpen() const override             { return midiInput != nullptr; }
    juce::String name() const override               { return "Launchpad (programmer mode)"; }
    int          numTrackPads() const override       { return kGridSize; }   // 8 columns
    int          numScenePads() const override       { return kGridSize; }   // 8 rows

    void handleIncomingMidi (const juce::MidiMessage&) override;
    void setPadLed (int trackIndex, int sceneIndex, int colourIdx, int state) override;

    //==============================================================================
    // Test seams (headless): inject a virtual input's messages and swap the LED sink without opening
    // real hardware.

    /** Replaces the outgoing LED sink (e.g. a CapturingMidiSink for a self-test). Never null-checks
        the caller's intent — pass a valid sink. Message-thread only. */
    void setSink (std::unique_ptr<MidiSink> newSink)   { sink = std::move (newSink); }

    /** Feeds one message as if it had arrived from the device — the exact path start()'s real input
        callback uses. Lets a headless test drive input without opening hardware. */
    void injectIncomingForTest (const juce::MidiMessage& m)   { handleIncomingMidi (m); }

    //==============================================================================
    // Encoding constants + helpers (public + static so the self-test can assert exact bytes without
    // reaching into a live driver).

    static constexpr int kGridSize     = 8;    // 8x8 launch grid
    static constexpr int kSceneColumn  = 9;    // Launchpad column 9 = right-hand scene-launch column
    static constexpr int kMidiChannelSolid = 1, kMidiChannelBlink = 2, kMidiChannelPulse = 3;

    /** Launchpad note for Forge grid cell (trackIndex=column 0-based, sceneIndex=row 0-based from the
        TOP). note = R*10 + C, R 1-based from the BOTTOM (so we flip the scene row), C = track+1.
        Returns -1 if the cell is outside the 8x8 grid. Static + pure — the self-test asserts on it. */
    static int cellToNote (int trackIndex, int sceneIndex) noexcept
    {
        if (trackIndex < 0 || trackIndex >= kGridSize || sceneIndex < 0 || sceneIndex >= kGridSize)
            return -1;
        const int rowFromBottom = kGridSize - sceneIndex;   // scene 0 (top) → row 8
        const int col           = trackIndex + 1;           // track 0 → column 1
        return rowFromBottom * 10 + col;
    }

    /** Inverse of cellToNote for the 8x8 grid: decodes a note into (trackOut, sceneOut). Returns true
        only for a valid in-grid note (digits 1..8 in both places). */
    static bool noteToCell (int note, int& trackOut, int& sceneOut) noexcept
    {
        const int r = note / 10;   // row from bottom
        const int c = note % 10;   // column from left
        if (r < 1 || r > kGridSize || c < 1 || c > kGridSize)
            return false;
        trackOut = c - 1;
        sceneOut = kGridSize - r;
        return true;
    }

    /** If `note` is a scene-launch pad (right column, note%10 == 9, row 1..8), returns its 0-based
        scene index (flipped so top = 0); else -1. */
    static int noteToScene (int note) noexcept
    {
        const int r = note / 10;
        const int c = note % 10;
        if (c != kSceneColumn || r < 1 || r > kGridSize)
            return -1;
        return kGridSize - r;
    }

    /** Maps a Forge colourIdx (0..18) to a Launchpad palette velocity (0 = off). Static + pure so the
        self-test asserts the exact velocity. Clamps out-of-range indices to off. */
    static int colourIdxToPalette (int colourIdx) noexcept
    {
        if (colourIdx <= 0 || colourIdx > 18)
            return 0;   // off
        return kColourIdxToPalette[colourIdx - 1];
    }

    /** Maps a Forge state (0 solid / 1 blink / 2 pulse) to the Launchpad LED MIDI CHANNEL (1-based).
        Any out-of-range state falls back to solid. */
    static int stateToChannel (int state) noexcept
    {
        switch (state)
        {
            case 1:  return kMidiChannelBlink;
            case 2:  return kMidiChannelPulse;
            default: return kMidiChannelSolid;
        }
    }

    /** Builds the exact LED note-on message for a cell (the same bytes setPadLed sends). Static so a
        test can compare byte-for-byte. PRECONDITION: cellToNote(trackIndex,sceneIndex) >= 0 — the
        caller must guard off-grid cells (a default-constructed MidiMessage is a non-empty sysex, not a
        distinguishable "empty" sentinel, so this returns a well-formed note-on for note 0 if misused). */
    static juce::MidiMessage makeLedMessage (int trackIndex, int sceneIndex, int colourIdx, int state) noexcept
    {
        const int note     = juce::jmax (0, cellToNote (trackIndex, sceneIndex));
        const int velocity = colourIdxToPalette (colourIdx);
        const int channel  = stateToChannel (state);
        // juce::MidiMessage::noteOn takes a 1-based channel and a 0..127 float velocity; we pass an
        // integer velocity via the uint8 overload for exact byte control.
        return juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity);
    }

private:
    // juce::MidiInputCallback — arrives on the MIDI thread.
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    // Device-name substrings we accept (case-insensitive). The Launchpad presents ports whose names
    // contain "Launchpad"; the MK3/X often expose a "LPX MIDI"/"MIDIIN2 (...)" programmer port too.
    static juce::StringArray defaultNameMatches();

    // 18-entry palette: colourIdx 1..18 → a distinguishable Launchpad palette velocity spanning the
    // colour wheel. Values are valid programmer-mode palette indices (1..127). colourIdx 1 is the
    // reserved rec-armed red (SlotVisualState uses redHueIdx=1), so palette[0] is a solid red.
    static constexpr int kColourIdxToPalette[18] =
    {
          5,   // 1  red        (reserved rec-armed)
          9,   // 2  amber
         13,   // 3  yellow
         17,   // 4  lime
         21,   // 5  green
         25,   // 6  spring green
         29,   // 7  teal
         33,   // 8  cyan
         37,   // 9  sky blue
         41,   // 10 azure
         45,   // 11 blue
         49,   // 12 indigo
         53,   // 13 violet
         57,   // 14 purple
         61,   // 15 magenta
         65,   // 16 pink
         69,   // 17 rose
          3    // 18 white/near-white (wrap)
    };

    std::unique_ptr<juce::MidiInput>  midiInput;    // opened device input (null when closed)
    std::unique_ptr<juce::MidiOutput> midiOutput;   // opened device output (may be null)
    std::unique_ptr<MidiSink>         sink;         // LED sink (default wraps midiOutput; test swaps)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaunchpadDriver)
};
