/*
    TempoPopup — the little tempo editor a CallOutBox hosts when the LCD's tempo zone is
    clicked (W?? 1.4). A dumb, engine-free view: it reads the current BPM through a
    std::function<double()> query and pushes edits back through a std::function<void(double)>
    sink — the shell wires those to EngineHelpers::setTempoAt on the live edit, so this class
    makes no te:: calls of its own.

    Three ways to set the tempo, all clamped to [20, 300] and all echoed back into the label:
      - type a value into the editable BPM label (return commits it);
      - the +/- steppers: +/-0.1 BPM per click, +/-1.0 when Shift is held;
      - TAP: taps an owned TapTempo with the hi-res millisecond clock; once THREE taps exist (so the
        estimate averages at least two intervals, never a single-interval guess) the BPM is applied.

    Fixed size (setSize in the ctor) so CallOutBox sizes its bubble to fit. Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/transport/TapTempo.h"

class TempoPopup : public juce::Component
{
public:
    /** @param queryBpm     returns the current BPM (used to seed the label on construction).
        @param onBpmChanged applied for every edit — the shell routes it to the engine. */
    TempoPopup (std::function<double()> queryBpm,
                std::function<void (double)> onBpmChanged);

    void resized() override;

private:
    /** Clamps `bpm` to [20, 300], applies it via onBpmChanged, and refreshes the label. */
    void applyBpm (double bpm);

    /** Re-reads the label text as the BPM to display (1 dp). */
    void refreshLabel (double bpm);

    std::function<double()>      queryBpm;
    std::function<void (double)> onBpmChanged;

    forge::transport::TapTempo tapTempo;

    juce::Label      bpmLabel;
    juce::TextButton upButton   { "+" };
    juce::TextButton downButton { "-" };
    juce::TextButton tapButton  { "TAP" };

    static constexpr double minBpm = 20.0;
    static constexpr double maxBpm = 300.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoPopup)
};
