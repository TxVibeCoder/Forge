/*
    TransportBar — play / pause / stop / record / loop / metronome buttons plus a count-in
    selector and a launch-quantisation (free-trigger) selector. Observes the TransportControl
    (ChangeBroadcaster) for button state. The record button defers to onRecord so the owner can
    wire arm + record/stop.

    The old timecode + bars|beats readout label (and the 25 Hz timer that fed it) is gone —
    the LcdDisplay in the ControlBar supersedes it (W04a).

    The metronome toggle, count-in selector, and launch-quantisation selector expose
    std::function seams (same shape as onRecord) so the shell can route them through the engine
    seams (Metronome / ProjectSession) without this view making any raw te:: calls. State is
    reflected from engine truth via the query seams.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class TransportBar : public juce::Component,
                     private juce::ChangeListener
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

    /** Fired when the user changes the launch quantisation (free-trigger) selector; delivers the
        0-based te::LaunchQType enum index (matches te::getLaunchQTypeChoices() order). The owner
        routes this to ProjectSession::setGlobalLaunchQuantisation. */
    std::function<void(int)> onLaunchQuantisationChanged;

    /** Truth query for the current te::LaunchQType enum index, used to seed/refresh the selector.
        Wired by the owner (ProjectSession::getGlobalLaunchQuantisation). If unset, defaults to the
        'bar' entry. */
    std::function<int()> queryLaunchQuantisation;

    /** Selects the launch-quantisation combo item matching `enumIndex` WITHOUT firing
        onLaunchQuantisationChanged (dontSendNotification) — the shell calls this on setEdit to
        seed the control from engine state. */
    void setLaunchQuantisation (int enumIndex);

    /** Fired when the user toggles MIDI-clock output; delivers the DESIRED enabled state. The owner
        routes this to the MidiClockSync seam (MidiClockSync::setSendClockToAll). */
    std::function<void(bool)> onMidiClockToggled;

    /** Engine-truth query: is MIDI-clock currently being sent to any output? Wired by the owner
        (MidiClockSync::isSendingClockAny). If unset, the toggle reflects its own last state. */
    std::function<bool()> queryMidiClockEnabled;

    /** The width the bar's fixed controls occupy (6 buttons + gaps + the count-in selector +
        the launch-quantisation selector + the 4 px side insets). Now that the bar hosts no
        stretching readout, the ControlBar sizes it to this and gives the leftover span to the LCD. */
    static constexpr int preferredWidth = 8 + 6 * (64 + 4) + 120 + 4 + 110;   // = 650

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateButtons();
    void syncMetronomeControls();   // reflect engine truth into the click / clock toggles + count-in box

    te::Edit* edit = nullptr;
    te::TransportControl* transport = nullptr;

    juce::TextButton playButton      { "Play" },
                     stopButton      { "Stop" },
                     recordButton    { "Rec" },
                     loopButton      { "Loop" },
                     metronomeButton { "Click" },
                     midiClockButton { "Clock" };
    juce::ComboBox   countInBox;
    juce::ComboBox   launchQuantBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
