/*
    LcdModel — the pure display model behind the control-bar LCD (W04a).

    computeLcdState() maps plain transport/tempo facts (read from the engine by LcdDisplay,
    or synthesised by the --selftest-lcd gate) to exactly what the LCD face renders: the
    four zone strings (position, timecode, tempo, key), the count-in digit state, and the
    beat-pulse phase.

    Deliberately engine-free: plain ints/doubles/juce::String in, so the selftest gate
    asserts the whole acceptance table headlessly (no device, no Edit) before the window
    exists. The engine-facing reads live in LcdDisplay.cpp; the maths live here.

    Count-in digit contract (W04a dossier + the QC correction):
      - the engine's count-in clicks land on WHOLE TIMELINE BEATS inside the click range, and
        the punch point is NOT beat-snapped (recording from a mid-beat stop is the common
        case) — so the digit is derived from the CLICK GRID, never from whole-beat distances
        to the punch: firstClickBeat = ceil(punchBeat − N), digit = floor(currentBeat) −
        firstClickBeat + 1. QC proved the distance form desyncs digit from click by up to a
        full beat whenever the punch is mid-beat.
      - anything before the first click (the engine pre-rolls ~(N + 0.5) beats) maps to
        digit 0 — the "ready" lead-in (rendered as a dimmed placeholder, never a zero).
      - a small epsilon is applied inside floor()/ceil() so arbitrary tempi cannot glitch the
        digit for one tick at an exact click boundary (skeptic guard 2).
      - countInActive additionally requires startedFromStopped (skeptic guard 1): a record()
        fired while already playing punches in with NO pre-roll and a stale punch time, so
        without this gate the LCD would render a phantom count-in after a backward seek.

    Header-only (#pragma once, inline). Pure functions — no threading requirements of its own.
*/

#pragma once

#include <JuceHeader.h>

#include <cmath>   // std::ceil (count-in digit), std::llround (timecode)

namespace forge::lcd
{

//==============================================================================
/** Everything computeLcdState needs, as plain values (no engine types).

    bars / beatInBar are the RAW 0-based numbers from tempo::BarsAndBeats (bb.bars and
    bb.getWholeBeats()); the display strings are 1-based. During a count-in pre-roll the
    position runs pre-punch (negative when punching at 0), so bars may be negative — the
    count-in face replaces the position readout for exactly that reason. */
struct LcdInput
{
    int bars = 0;                       // 0-based whole bars — may be negative in pre-roll
    int beatInBar = 0;                  // 0-based whole beat within the bar
    double fractionalBeat = 0.0;        // [0, 1) — bb.getFractionalBeats().inBeats(); 0 = on the beat
    double bpm = 120.0;                 // tempoSequence.getBpmAt (pos), curve-aware
    juce::String timeSigString;         // "4/4" — TimeSigSetting::getStringTimeSig()
    juce::String keyString;             // "C" / "Am" — formatted by the caller (key policy lives there)
    bool recording = false;             // transport.isRecording() — TRUE throughout the count-in
    double positionSeconds = 0.0;       // transport.getPosition()
    double punchTimeSeconds = 0.0;      // transport.getTimeWhenStarted() — the punch point
    double currentBeat = 0.0;           // toBeats(pos) — the TIMELINE beat (clicks land on its whole values)
    double punchBeat = 0.0;             // toBeats(punch); NOT beat-snapped by the engine
    int countInTotal = 0;               // edit.getNumCountInBeats(), latched at the record trigger
    bool startedFromStopped = false;    // latched: record() fired from a STOPPED transport (guard 1)
};

//==============================================================================
/** What the LCD face renders. Edge-compared by LcdDisplay each tick so a static face never
    repaints; pulsePhase participates in the compare, so a moving transport animates. */
struct LcdState
{
    juce::String positionText;   // "3|2" (bars|beats, 1-based) — NEVER shown while countInActive
    juce::String tempoText;      // "120.0"
    juce::String keySigText;     // "C · 4/4" (key omitted when unknown)
    juce::String timecodeText;   // "1:23.204" / "1:02:03.500" (W04b) — clamped at 0:00.000, never negative
    bool countInActive = false;  // render the count-in face instead of the four zones
    int countInDigit = 0;        // 1..countInTotal; 0 = the "ready" lead-in half-beat
    int countInTotal = 0;        // the latched beat total (context for rendering + the selftest)
    double pulsePhase = 0.0;     // [0, 1) fractional beat; 0 = on the click/beat
};

inline bool operator== (const LcdState& a, const LcdState& b)
{
    return a.positionText == b.positionText
        && a.tempoText == b.tempoText
        && a.keySigText == b.keySigText
        && a.timecodeText == b.timecodeText
        && a.countInActive == b.countInActive
        && a.countInDigit == b.countInDigit
        && a.countInTotal == b.countInTotal
        && a.pulsePhase == b.pulsePhase;   // exact compare is right for edge-detect: identical
                                           // inputs yield identical doubles; any motion repaints
}

inline bool operator!= (const LcdState& a, const LcdState& b)  { return ! (a == b); }

//==============================================================================
/** Skeptic guard 2: epsilon subtracted before ceil() in the digit maths. ceil(beatsLeft) is
    binary-exact at 120 BPM but an arbitrary tempo map can land fractionally ABOVE a whole
    beat at a click boundary, which would bump the digit back for one tick. 1e-6 beats is far
    above tempo-map rounding error and far below a UI tick (~0.08 beats at 25 Hz / 120 BPM). */
constexpr double countInCeilEpsilonBeats = 1.0e-6;

/** The one pure mapping from transport facts to the rendered LCD state. */
inline LcdState computeLcdState (const LcdInput& in)
{
    LcdState s;

    s.positionText = juce::String (in.bars + 1) + "|" + juce::String (in.beatInBar + 1);
    s.tempoText    = juce::String (in.bpm, 1);
    s.keySigText   = in.keyString.isEmpty() ? in.timeSigString
                                            : in.keyString + " · " + in.timeSigString;
    s.pulsePhase   = in.fractionalBeat;
    s.countInTotal = in.countInTotal;

    // Timecode (W04b): absolute transport time — "M:SS.mmm" under an hour, "H:MM:SS.mmm" from
    // one hour up. Clamped at zero: a count-in pre-roll runs the position negative, and while
    // the count-in face replaces the zones for exactly that window, the model must still never
    // emit a minus sign.
    {
        const auto totalMs = (juce::int64) std::llround (juce::jmax (0.0, in.positionSeconds) * 1000.0);
        const auto millis  = (int) (totalMs % 1000);
        const auto seconds = (int) ((totalMs / 1000) % 60);
        const auto minutes = (int) ((totalMs / 60000) % 60);
        const auto hours   = (int) (totalMs / 3600000);

        s.timecodeText = (hours > 0 ? juce::String (hours) + ":" + juce::String (minutes).paddedLeft ('0', 2)
                                    : juce::String (minutes))
                       + ":" + juce::String (seconds).paddedLeft ('0', 2)
                       + "." + juce::String (millis).paddedLeft ('0', 3);
    }

    // Count-in ACTIVE test (dossier recipe + skeptic guard 1): only a record latched from a
    // stopped transport pre-rolls; while it does, the position runs pre-punch. A mid-playback
    // record() (startedFromStopped == false) punches in immediately with a stale punch time,
    // so it must never light the count-in face.
    if (in.recording && in.startedFromStopped && in.countInTotal > 0
        && in.positionSeconds < in.punchTimeSeconds)
    {
        s.countInActive = true;

        // Digit k flips exactly on CLICK beat k (whole timeline beats), for ANY punch position:
        // the first click is the first whole beat at least N beats before the punch. Everything
        // earlier clamps to 0 (the lead-in; also absorbs an Ableton-Link-extended pre-roll), and
        // a beat just shy of the punch clamps to N.
        const int firstClickBeat = (int) std::ceil (in.punchBeat - (double) in.countInTotal
                                                    - countInCeilEpsilonBeats);
        s.countInDigit = juce::jlimit (0, in.countInTotal,
                                       (int) std::floor (in.currentBeat + countInCeilEpsilonBeats)
                                         - firstClickBeat + 1);
    }

    return s;
}

} // namespace forge::lcd
