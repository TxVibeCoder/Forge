/*
    LcdDisplay — the GarageBand-style transport LCD in the control bar (W04a).

    An inset "screen on the device": lcdBg face, 1 px lcdFrame bezel, 3 px corner radius.
    Idle/playing face, three zones: LEFT bars|beats (16 px, timeTempo, monospaced), CENTRE
    tempo ("120.0" + a small BPM tag), RIGHT key + time-sig ("C · 4/4"). During record
    pre-roll the count-in face REPLACES the whole layout: one large centred digit with a
    recordRed underline whose alpha pulses with the beat (peak on the click, decaying across
    the beat) — the raw bars|beats are NEVER shown while counting in (they run negative
    during the engine pre-roll). Narrow widths drop the key zone first, then the tempo zone;
    the position readout always survives.

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

    /** Layout hints for the ControlBar, derived from the zone widths (10 px insets + 64 px
        position + 56 px key + 70 px tempo + 24 px BPM tag): below keyZoneMinWidth the key
        zone drops (the tempo strip keeps its full width), below minimumWidth the tempo zone
        drops too — the bars|beats position always survives (W04a contract, QC-corrected
        thresholds). */
    static constexpr int preferredWidth  = 230;
    static constexpr int keyZoneMinWidth = 210;
    static constexpr int minimumWidth    = 150;

    void paint (juce::Graphics&) override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};
