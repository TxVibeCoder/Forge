/*
    TransportBar — play / pause / stop / record / loop buttons plus a timecode + bars|beats
    readout. Observes the TransportControl (ChangeBroadcaster) for button state and polls
    getPosition() on a 25Hz timer for the readout. The record button defers to onRecord so
    the owner can wire arm + record/stop.

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

    bool readoutIsNonEmpty() const { return readout.getText().isNotEmpty(); }

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();

    te::Edit* edit = nullptr;
    te::TransportControl* transport = nullptr;

    juce::TextButton playButton   { "Play" },
                     stopButton   { "Stop" },
                     recordButton { "Rec" },
                     loopButton   { "Loop" };
    juce::Label readout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
