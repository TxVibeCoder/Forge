#include "ui/transport/TransportBar.h"
#include "engine/EngineHelpers.h"

using namespace juce;

static String formatTimecode (double seconds)
{
    const int ms = roundToInt (seconds * 1000.0);
    const int a  = std::abs (ms);
    return String (ms < 0 ? "-" : "")
         + String::formatted ("%02d:%02d:%02d.%03d",
                              a / 3600000, (a / 60000) % 60, (a / 1000) % 60, a % 1000);
}

TransportBar::TransportBar()
{
    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton })
        addAndMakeVisible (b);

    recordButton.setColour (TextButton::buttonOnColourId, Colours::red);
    loopButton.setColour (TextButton::buttonOnColourId, Colours::orange);

    playButton.onClick   = [this] { if (edit != nullptr) EngineHelpers::togglePlay (*edit); };
    stopButton.onClick   = [this] { if (transport != nullptr) transport->stop (false, false); };
    recordButton.onClick = [this] { if (onRecord) onRecord(); };
    loopButton.onClick   = [this] { if (transport != nullptr) { transport->looping = ! transport->looping; updateButtons(); } };

    readout.setJustificationType (Justification::centredRight);
    readout.setFont (Font (FontOptions (16.0f)));
    addAndMakeVisible (readout);

    startTimerHz (25);
}

TransportBar::~TransportBar()
{
    if (transport != nullptr)
        transport->removeChangeListener (this);
}

void TransportBar::setEdit (te::Edit* e)
{
    if (transport != nullptr)
        transport->removeChangeListener (this);

    edit = e;
    transport = (e != nullptr) ? &e->getTransport() : nullptr;

    if (transport != nullptr)
        transport->addChangeListener (this);

    updateButtons();
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (4, 4);

    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton })
    {
        b->setBounds (r.removeFromLeft (64));
        r.removeFromLeft (4);
    }

    readout.setBounds (r.removeFromRight (jmin (240, r.getWidth())));
}

void TransportBar::changeListenerCallback (ChangeBroadcaster*)
{
    updateButtons();
}

void TransportBar::updateButtons()
{
    const bool playing   = transport != nullptr && transport->isPlaying();
    const bool recording = transport != nullptr && transport->isRecording();
    const bool looping    = transport != nullptr && transport->looping;

    playButton.setButtonText (playing ? "Pause" : "Play");
    recordButton.setToggleState (recording, dontSendNotification);
    loopButton.setToggleState (looping, dontSendNotification);
}

void TransportBar::timerCallback()
{
    if (transport == nullptr || edit == nullptr)
    {
        readout.setText ("00:00:00.000   1|1", dontSendNotification);
        return;
    }

    const auto pos = transport->getPosition();
    const auto bb  = edit->tempoSequence.toBarsAndBeats (pos);

    readout.setText (formatTimecode (pos.inSeconds())
                         + "   " + String (bb.bars + 1) + "|" + String (bb.getWholeBeats() + 1),
                     dontSendNotification);
}
