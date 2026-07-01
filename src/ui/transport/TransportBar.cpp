#include "ui/transport/TransportBar.h"
#include "engine/EngineHelpers.h"

using namespace juce;

// Count-in choices offered by the selector, in bars (0 == no count-in). Tracktion's native
// count-in (te::Edit::CountIn) tops out at two bars, so we offer 0/1/2 and never present an
// option the engine would silently clamp. ComboBox item ids are 1-based (id 0 means "no
// selection"), so an option's id is its index here + 1.
static constexpr int countInOptions[]  = { 0, 1, 2 };
static constexpr int numCountInOptions = (int) (sizeof (countInOptions) / sizeof (countInOptions[0]));

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
    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton })
        addAndMakeVisible (b);

    recordButton.setColour (TextButton::buttonOnColourId, Colours::red);
    loopButton.setColour (TextButton::buttonOnColourId, Colours::orange);
    metronomeButton.setColour (TextButton::buttonOnColourId, Colours::orange);

    playButton.onClick   = [this] { if (edit != nullptr) EngineHelpers::togglePlay (*edit); };
    stopButton.onClick   = [this] { if (transport != nullptr) transport->stop (false, false); };
    recordButton.onClick = [this] { if (onRecord) onRecord(); };
    loopButton.onClick   = [this] { if (transport != nullptr) { transport->looping = ! transport->looping; updateButtons(); } };

    // Metronome toggle: ask engine truth for the current state, request the inverse, then reflect.
    // Guarded on a live Edit (like playButton) — the owner's seam dereferences the Edit.
    metronomeButton.onClick = [this]
    {
        const bool current = (edit != nullptr && queryMetronomeEnabled) ? queryMetronomeEnabled()
                                                                        : metronomeButton.getToggleState();
        if (edit != nullptr && onMetronomeToggled)
            onMetronomeToggled (! current);

        syncMetronomeControls();
    };

    // Count-in selector: item id (index+1) maps back to a bar count in countInOptions.
    countInBox.setTooltip ("Count-in before recording");
    for (int i = 0; i < numCountInOptions; ++i)
        countInBox.addItem (countInOptions[i] == 0 ? String ("No count-in")
                                                   : String (countInOptions[i]) + " bar"
                                                         + (countInOptions[i] == 1 ? "" : "s"),
                            i + 1);
    countInBox.onChange = [this]
    {
        const int id = countInBox.getSelectedId();
        if (edit != nullptr && id >= 1 && id <= numCountInOptions && onCountInBarsChanged)
            onCountInBarsChanged (countInOptions[id - 1]);
    };
    addAndMakeVisible (countInBox);

    readout.setJustificationType (Justification::centredRight);
    readout.setFont (Font (FontOptions (16.0f)));
    addAndMakeVisible (readout);

    syncMetronomeControls();   // defaults until the shell wires the query seams + calls setEdit

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

    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton })
    {
        b->setBounds (r.removeFromLeft (64));
        r.removeFromLeft (4);
    }

    countInBox.setBounds (r.removeFromLeft (jmin (120, r.getWidth())));
    r.removeFromLeft (4);

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

    syncMetronomeControls();
}

void TransportBar::syncMetronomeControls()
{
    // Click state and count-in length are both engine-authoritative, read back via the query seams
    // (which need a live Edit — count-in is a global engine setting reached through the Edit). With
    // no Edit loaded or a seam unwired, fall back to the control's own value / the engine's OFF
    // default rather than inventing state that would mislead about the real record behaviour.
    const bool clickOn = (edit != nullptr && queryMetronomeEnabled) ? queryMetronomeEnabled()
                                                                    : metronomeButton.getToggleState();
    metronomeButton.setToggleState (clickOn, dontSendNotification);

    const int bars = (edit != nullptr && queryCountInBars) ? queryCountInBars() : 0;
    for (int i = 0; i < numCountInOptions; ++i)
        if (countInOptions[i] == bars)
        {
            countInBox.setSelectedId (i + 1, dontSendNotification);
            break;
        }
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
