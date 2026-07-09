/*
    LcdDisplay — the GarageBand-style transport LCD in the control bar (W04a).

    An inset "screen on the device": lcdBg face, 1 px lcdFrame bezel, 3 px corner radius.
    Idle/playing face, four zones: LEFT bars|beats (16 px, timeTempo, monospaced), then the
    absolute-time timecode right of it (12 px, textSec, monospaced — secondary to musical
    time in the design hierarchy; W04b), CENTRE tempo ("120.0" + a small BPM tag), RIGHT
    key + time-sig ("C · 4/4"). During record pre-roll the count-in face REPLACES the whole
    layout: one large centred digit with a recordRed underline whose alpha pulses with the
    beat (peak on the click, decaying across the beat) — the raw bars|beats are NEVER shown
    while counting in (they run negative during the engine pre-roll). Narrow widths drop the
    timecode zone first, then the key zone, then the tempo zone; the position readout always
    survives.

    Data flow: a 25 Hz message-thread timer (same cadence as TransportBar) re-resolves the
    edit/transport per tick, reads position / tempo map / pitch sequence, feeds the pure
    forge::lcd::computeLcdState, and repaints only when the rendered state changed. The
    edge-compare includes the pulse phase, so a moving transport animates per tick and an
    idle one paints nothing.

    Count-in latch (W04a dossier, skeptic guard 1): the count-in beat total is latched at the
    isRecording rising edge, and ONLY when the previous tick saw a stopped transport — a
    record() fired mid-playback punches in with no pre-roll and a stale start time, so an
    unguarded latch would render a phantom count-in after a backward seek. The engine's
    recordingStarted transport-listener callback was rejected for this job: it fires after
    performRecord, when the transport already reads as playing, so it cannot distinguish the
    two record paths; the previous-tick snapshot can.

    Message-thread only. No logging: the only engine access is the per-tick poll (a hot path
    per docs/LOGGING.md) and none of its reads are fallible seams.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/transport/LcdModel.h"

namespace te = tracktion;

class LcdDisplay : public juce::Component,
                   private juce::Timer
{
public:
    LcdDisplay();
    ~LcdDisplay() override;

    /** May be null (project swap in flight) — the face renders a static default until the
        next edit arrives. A SAME-edit call (the shell uses controlBar.setEdit as a generic
        toggle resync) only exits demo mode and refreshes — it must NOT reset the record
        edge-tracking, or a menu command issued mid-count-in wipes the latch and the face
        falls back to raw negative bars (QC). A genuine edit swap re-seeds everything. Both
        paths repaint synchronously so a frozen demo face can never leak into a snapshot
        taken before the next tick. */
    void setEdit (te::Edit*);

    /** Test seam: freeze the face on an injected model state. The real count-in cannot be
        driven headlessly, so the screenshot state-matrix harness renders the count-in face
        through this. The 25 Hz poll stops feeding the face until the next setEdit(). */
    void setDemoState (const forge::lcd::LcdState&);

    /** Screenshot-report probe: TRUE only after the live 25 Hz poll has actually computed a
        state from a bound edit (the old TransportBar label's "was the readout fed" meaning —
        a construction-time default must not satisfy it, QC). */
    bool readoutIsNonEmpty() const  { return fedByLivePoll && current.positionText.isNotEmpty(); }

    /** Layout hints for the ControlBar, derived from the zone widths (10 px insets each side
        + 64 px position + 80 px timecode + 70 px tempo strip, the 24 px BPM tag inside it,
        + 56 px key: 20 + 64 + 80 + 70 + 56 = 290): below timecodeMinWidth the timecode zone
        drops first (absolute time is the most expendable readout — W04b), below
        keyZoneMinWidth (20 + 64 + 70 + 56 = 210) the key zone drops (the tempo strip keeps
        its full width), below minimumWidth the tempo zone drops too — the bars|beats
        position always survives (W04a contract, QC-corrected thresholds). preferredWidth
        fits all four zones plus the same 20 px slack the three-zone value (230 = 210 + 20)
        carried. */
    static constexpr int preferredWidth   = 310;   // timecodeMinWidth + 20 px slack
    static constexpr int timecodeMinWidth = 290;   // 20 + 64 + 80 + 70 + 56 (all four zones exact)
    static constexpr int keyZoneMinWidth  = 210;   // 20 + 64 + 70 + 56 (timecode already shed)
    static constexpr int minimumWidth     = 150;

    void paint (juce::Graphics&) override;

    /** The tempo and time-signature readouts are the clickable parts of the LCD — hitTest returns
        true ONLY inside the last-painted tempoZoneBounds or sigZoneBounds, so every other part of
        the face stays click-through to the crowded control bar underneath exactly as before. */
    bool hitTest (int x, int y) override;

    /** Clicking the tempo zone launches the tempo popup; clicking the signature zone launches the
        time-signature popup (each a CallOutBox anchored to its own zone). */
    void mouseUp (const juce::MouseEvent&) override;

    //==============================================================================
    // Tempo-edit seams, wired by the shell (see main.cpp). Both must be set for the tempo zone
    // to become clickable at runtime; hitTest also requires them so the click-through behaviour
    // is unchanged until they are wired.
    std::function<double()>       queryBpm;       // current BPM, seeds the tempo popup
    std::function<void (double)>  onBpmChanged;   // apply an edited BPM to the engine

    // Time-signature-edit seams, wired by the shell alongside the tempo pair. Both must be set for
    // the signature zone to become clickable; hitTest/mouseUp gate on them the same way.
    std::function<juce::String()>   querySig;       // current signature as "n/d", seeds the sig popup
    std::function<void (int, int)>  onSigChanged;   // apply an edited (numerator, denominator) to the engine

private:
    void timerCallback() override;

    /** The poll body, callable synchronously (setEdit uses it so demo-mode exit and edit
        swaps repaint immediately instead of waiting a tick). */
    void refreshNow();

    te::Edit* edit = nullptr;
    te::TransportControl* transport = nullptr;

    forge::lcd::LcdState current;      // last rendered state (the edge-compare target)
    bool demoMode = false;             // setDemoState freezes the face; setEdit resumes polling
    bool fedByLivePoll = false;        // a live tick computed a state from a bound edit (the probe)

    // Previous-tick transport snapshot for the record rising-edge latch (skeptic guard 1).
    bool prevPlaying = false, prevRecording = false;
    int  latchedCountInTotal = 0;      // edit.getNumCountInBeats() at the record trigger, else 0
    bool latchedFromStopped  = false;  // that trigger came from a stopped transport

    // The last-painted clickable readout rectangles (local coords). Set every paint() so hitTest /
    // mouseUp know where the clickable zones are; each is emptied when its readout is shed at a
    // narrow width (or the count-in face replaces the zones), which correctly makes that part of
    // the LCD click-through again.
    juce::Rectangle<int> tempoZoneBounds;
    juce::Rectangle<int> sigZoneBounds;   // the "· 4/4" signature part of the key/sig readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};
