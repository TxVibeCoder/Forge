/*
    Metronome — a thin seam over Tracktion's built-in click track and its native count-in.
    Keeps the click / count-in engine calls in one place so views make no raw te:: click calls.

    Both facts this seam touches are ENGINE-AUTHORITATIVE — Forge holds no mirror of either
    (that would be a drift-prone duplicate; see CLAUDE.md "one canonical source per fact"):

      - CLICK on/off is juce::CachedValue<bool> te::Edit::clickTrackEnabled, backed by the Edit's
        CLICKTRACK child ValueTree (tracktion_Edit.h:847; set in Edit::initialiseClickTrack,
        tracktion_Edit.cpp:967-981). It PERSISTS per-project and defaults OFF. The audio ClickNode
        reads it live (tracktion_ClickNode.cpp:204), so it is toggled on the MESSAGE THREAD only.

      - COUNT-IN is Tracktion's native te::Edit::CountIn { none, oneBar, twoBar, twoBeat, oneBeat }
        (tracktion_Edit.h:702-718). te::TransportControl::record() reads getNumCountInBeats() itself
        and pre-rolls the transport before the punch-in (tracktion_TransportControl.cpp:1483-1489),
        so enabling count-in needs NO change to Forge's record recipe — only setCountInMode() before
        record() from a STOPPED transport (already how both Forge record starts roll). Whole-bar
        count-in tops out at TWO bars natively. NOTE: the mode is a GLOBAL engine setting
        (PropertyStorage SettingID::countInMode), shared across Edits and NOT persisted per-project.

    All calls are MESSAGE-THREAD ONLY. No logging happens on the audio/RT thread here — this seam
    only mutates message-thread CachedValues / engine settings the RT graph reads.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace Metronome
{
    //==============================================================================
    // Click (metronome sound)

    /** Enables or disables the metronome click on `edit`. Persists on the Edit. */
    void enableClick (te::Edit& edit, bool shouldBeEnabled);

    /** True iff the metronome click is currently enabled on `edit`. */
    bool isClickEnabled (te::Edit& edit);

    //==============================================================================
    // Count-in (pre-roll before recording)

    /** Sets the count-in length in whole bars: 0 = off, 1 = one bar, 2 = two bars. Tracktion's
        native count-in tops out at two bars, so a larger request is clamped to 2 and logged.
        The engine's transport.record() path consumes this automatically (it pre-rolls
        getNumCountInBeats()), so no change to the record recipe is required. Because the mode is
        a GLOBAL engine setting, this is shared across Edits and not saved per-project. */
    void setCountInBars (te::Edit& edit, int bars);

    /** Current count-in length in whole bars (0/1/2). Sub-bar native modes (which Forge never
        sets) are bucketed to the nearest whole bar for display. */
    int getCountInBars (te::Edit& edit);
}
