/*
    CaptureCountIn — the pure beat arithmetic behind an audible count-in before performance capture
    (B4). Header-only and engine-free by design: no te:: types, no Component, no transport — just
    (currentBeat, countInBeats) -> where to start rolling and where capture actually arms. That makes
    the decision exhaustively gate-able without a device or a rolling transport (the same discipline as
    forge::meter::advanceMeterHold and forge::midiedit::*).

    WHY THIS EXISTS AT ALL. Tracktion's native count-in is consumed inside TransportControl::record():
    it pre-rolls the transport by getNumCountInBeats() before the punch-in
    (tracktion_TransportControl.cpp:1483-1489). W22 deferred B4 on the reading that this made record
    mode the ONLY audible-count-in path — which would have held the transport in record for a whole
    Capture session and lit the Rec button for a non-recording action. Reading the engine source
    refutes that: the count-in is a POSITION OFFSET plus an audible click, and both are reproducible
    without record mode. Forge rolls the transport with the click on from `prerollBeat` and arms
    capture when the playhead reaches `captureBeat`. No record mode, no punch-in, and the W17 capture
    tick is untouched — it simply starts later.

    DELIBERATE DIFFERENCE from the engine recipe: it pre-rolls by `countInBeats + 0.5` beats, a
    half-beat fudge tied to how it sets the record click RANGE. Forge rolls the ordinary transport with
    the ordinary click, so it needs no fudge and starts exactly on the beat — which also keeps the
    count math whole-numbered and assertable.
*/

#pragma once

#include <algorithm>

namespace forge::capture
{
    /** Where an audible count-in starts and where capture arms, both in absolute Edit BEATS. */
    struct CountInPlan
    {
        double prerollBeat = 0.0;   // transport starts rolling here (click audible)
        double captureBeat = 0.0;   // capture arms here — the first beat that can be captured
        bool   active      = false; // false => no count-in was requested/possible; arm immediately
    };

    /** Plans the count-in for a capture starting at `currentBeat` (the transport's parked position)
        with `countInBeats` of pre-roll (the caller passes Edit::getNumCountInBeats(), so the capture
        count-in is exactly as long as the record count-in — one setting, one answer).

        `countInBeats <= 0` returns an INACTIVE plan at the current position: no count-in, arm now.

        There is never room to roll before beat 0, so when the playhead sits closer to the start than
        the count-in is long, the count-in is NOT shortened — the CAPTURE POINT moves later instead
        (`captureBeat = max(currentBeat, countInBeats)`). The user always gets the full count they
        asked for, and the transport still starts where it was parked rather than jumping backwards.
        A negative `currentBeat` (never produced by the transport, but cheap to defend) clamps to 0. */
    inline CountInPlan planCountIn (double currentBeat, int countInBeats)
    {
        const double from = std::max (0.0, currentBeat);

        CountInPlan plan;

        if (countInBeats <= 0)
        {
            plan.prerollBeat = plan.captureBeat = from;
            return plan;
        }

        plan.captureBeat = std::max (from, (double) countInBeats);
        plan.prerollBeat = plan.captureBeat - (double) countInBeats;
        plan.active      = true;
        return plan;
    }
}
