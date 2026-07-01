/*
    TransportBar — play / pause / stop / record / loop / metronome buttons plus a count-in
    selector and a timecode + bars|beats readout. Observes the TransportControl
    (ChangeBroadcaster) for button state and polls getPosition() on a 25Hz timer for the
    readout. The record button defers to onRecord so the owner can wire arm + record/stop.

    The metronome toggle and count-in selector expose std::function seams (same shape as
    onRecord) so the shell can route them through the Metronome engine seam without this view
    making any raw te:: click calls. State is reflected from engine truth via the query seams.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class TransportBar : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    TransportBar();
    ~TransportBar() override;

    void setEdit (te::Edit*);

    /** Wired by the owner: arm + start/stop a take. */
    std::function<void()> onRecord;

    /** Fired when the user toggles the metronome; delivers the DESIRED enabled state. The owner
        routes this to the Metronome seam (Metronome::enableClick). */
    std::function<void(bool)> onMetronomeToggled;

    /** Engine-truth query: is the metronome click currently enabled? Wired by the owner
        (Metronome::isClickEnabled). If unset, the toggle reflects its own last state. */
    std::function<bool()> queryMetronomeEnabled;

    /** Fired when the user changes the count-in length (in bars, 0 = off). The owner routes this
        to Metronome::setCountInBars (which the record path consults). */
    std::function<void(int)> onCountInBarsChanged;

    /** Truth query for the current count-in length in bars, used to initialise/refresh the
        selector. Wired by the owner (Metronome::getCountInBars). If unset, defaults to 1 bar. */
    std::function<int()> queryCountInBars;

    bool readoutIsNonEmpty() const { return readout.getText().isNotEmpty(); }

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();
    void syncMetronomeControls();   // reflect engine truth into the click toggle + count-in box

    te::Edit* edit = nullptr;
    te::TransportControl* transport = nullptr;

    juce::TextButton playButton      { "Play" },
                     stopButton      { "Stop" },
                     recordButton    { "Rec" },
                     loopButton      { "Loop" },
                     metronomeButton { "Click" };
    juce::ComboBox   countInBox;
    juce::Label readout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
