/*
    TimeSigPopup — the little time-signature editor a CallOutBox hosts when the LCD's signature
    zone (the "· 4/4" part of the key/sig readout) is clicked. Modelled on TempoPopup: a dumb,
    engine-free view that reads the current signature through a std::function<juce::String()>
    query ("n/d") and pushes edits back through a std::function<void(int,int)> sink — the shell
    wires those to EngineHelpers::setTimeSigAt on the live edit, so this class makes no te:: calls
    of its own.

    Two controls, one row:
      - the NUMERATOR: an editable label clamped to [1, 32] (return / focus-loss commits);
      - the DENOMINATOR: a ComboBox limited to the valid power-of-two meter denominators
        {1, 2, 4, 8, 16} (free entry would let a user type a non-power-of-two, which the engine
        would silently snap — a selector makes the constraint visible). Its item IDs ARE the
        denominator values, so getSelectedId() returns the denominator directly.

    Any commit re-applies BOTH fields together (a signature is a num+den pair) and echoes the
    canonical numerator back into the label. Seeding uses dontSendNotification so merely OPENING
    the popup never writes to (dirties) the edit — only a genuine user edit fires onSigChanged.

    Fixed size (setSize in the ctor) so CallOutBox sizes its bubble to fit. Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

class TimeSigPopup : public juce::Component
{
public:
    /** @param querySig     returns the current signature as "n/d" (seeds the fields on construction).
        @param onSigChanged applied for every edit — the shell routes it to the engine. */
    TimeSigPopup (std::function<juce::String()> querySig,
                  std::function<void (int, int)> onSigChanged);

    void resized() override;

private:
    /** Reads the numerator label (clamped [1,32]) + the selected denominator, applies them via
        onSigChanged, and echoes the canonical numerator back into the label. */
    void applySig();

    std::function<juce::String()>  querySig;
    std::function<void (int, int)> onSigChanged;

    juce::Label    numeratorLabel;
    juce::Label    slashLabel;
    juce::ComboBox denominatorBox;

    static constexpr int minNumerator = 1;
    static constexpr int maxNumerator = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeSigPopup)
};
