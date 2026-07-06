#include "ui/transport/TransportBar.h"
#include "engine/EngineHelpers.h"

using namespace juce;

// Count-in choices offered by the selector, in bars (0 == no count-in). Tracktion's native
// count-in (te::Edit::CountIn) tops out at two bars, so we offer 0/1/2 and never present an
// option the engine would silently clamp. ComboBox item ids are 1-based (id 0 means "no
// selection"), so an option's id is its index here + 1.
static constexpr int countInOptions[]  = { 0, 1, 2 };
static constexpr int numCountInOptions = (int) (sizeof (countInOptions) / sizeof (countInOptions[0]));

TransportBar::TransportBar()
{
    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton,
                     &midiClockButton, &globalRecButton })
        addAndMakeVisible (b);

    recordButton.setColour (TextButton::buttonOnColourId, Colours::red);
    loopButton.setColour (TextButton::buttonOnColourId, Colours::orange);
    metronomeButton.setColour (TextButton::buttonOnColourId, Colours::orange);
    midiClockButton.setColour (TextButton::buttonOnColourId, Colours::orange);
    midiClockButton.setTooltip ("Send MIDI clock to all outputs");

    // Global performance-capture toggle (Wave 7). Record-family, so it speaks the record-red accent when
    // armed (distinct from the orange sync toggles); armed means "stamp every clip I launch onto the
    // Arrangement at the beat it fires." A DISTINCT control from Rec (MIDI-into-slot take).
    globalRecButton.setColour (TextButton::buttonOnColourId, Colours::red);
    globalRecButton.setTooltip ("Capture performance to Arrangement — records which clips launch, when, and for how long");

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

    // MIDI-clock toggle: ask engine truth (is any output sending clock?), request the inverse, then
    // reflect. Unlike the click toggle this operates on the engine's MIDI outputs (not the Edit), so
    // it is NOT gated on a live Edit — clock output is a device-level setting that stands on its own.
    midiClockButton.onClick = [this]
    {
        const bool current = queryMidiClockEnabled ? queryMidiClockEnabled()
                                                   : midiClockButton.getToggleState();
        if (onMidiClockToggled)
            onMidiClockToggled (! current);

        syncMetronomeControls();
    };

    // Global-capture toggle (Wave 7): ask engine truth (is capture armed?), request the inverse, reflect.
    // Gated on a live Edit — the owner's seam records against the Edit. Toggling OFF commits the take.
    globalRecButton.onClick = [this]
    {
        const bool current = (edit != nullptr && queryGlobalRecordArmed) ? queryGlobalRecordArmed()
                                                                         : globalRecButton.getToggleState();
        if (edit != nullptr && onGlobalRecordToggled)
            onGlobalRecordToggled (! current);

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

    // Launch-quantisation (free-trigger) selector: item id (index+1) maps back to a 0-based
    // te::LaunchQType enum index. Populated in te::getLaunchQTypeChoices() order (enum order).
    launchQuantBox.setTooltip ("Launch quantize");
    {
        const auto choices = te::getLaunchQTypeChoices();
        for (int i = 0; i < choices.size(); ++i)
            launchQuantBox.addItem (choices[i], i + 1);
    }
    setLaunchQuantisation ((int) te::LaunchQType::bar);   // default until the shell seeds engine truth
    launchQuantBox.onChange = [this]
    {
        const int id = launchQuantBox.getSelectedId();
        if (edit != nullptr && id >= 1 && onLaunchQuantisationChanged)
            onLaunchQuantisationChanged (id - 1);
    };
    addAndMakeVisible (launchQuantBox);

    syncMetronomeControls();   // defaults until the shell wires the query seams + calls setEdit
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

    // Seven buttons + two combos. At/above preferredWidth everything renders at its preferred size; below
    // it (a user-shrunk window, ~760 px), the WHOLE strip shrinks proportionally with per-element floors so
    // nothing ever collapses to 0 px — the W06 QC lesson (a starved control is unclickable), generalized
    // from the two combos to the buttons the Wave-7 7th button now crowds.
    juce::TextButton* buttons[] = { &playButton, &stopButton, &recordButton, &loopButton,
                                    &metronomeButton, &midiClockButton, &globalRecButton };
    constexpr int numButtons = (int) (sizeof (buttons) / sizeof (buttons[0]));

    constexpr int gap = 4, btnPref = 64, countInPref = 120, launchPref = 110;
    constexpr int btnFloor = 34, comboFloor = 40;

    const int numGaps   = numButtons + 1;   // between each button, and before each combo
    const int prefTotal = numButtons * btnPref + countInPref + launchPref + numGaps * gap;
    const int avail     = jmax (0, r.getWidth());

    const double scale = (avail >= prefTotal || prefTotal <= 0) ? 1.0 : (double) avail / (double) prefTotal;

    const int btnW     = jmax (btnFloor,   roundToInt (btnPref     * scale));
    const int countInW = jmax (comboFloor, roundToInt (countInPref * scale));
    const int launchW  = jmax (comboFloor, roundToInt (launchPref  * scale));

    for (auto* b : buttons)
    {
        b->setBounds (r.removeFromLeft (jmin (btnW, r.getWidth())));
        r.removeFromLeft (gap);
    }

    countInBox.setBounds (r.removeFromLeft (jmin (countInW, r.getWidth())));
    r.removeFromLeft (gap);
    launchQuantBox.setBounds (r.removeFromLeft (jmin (launchW, r.getWidth())));
}

void TransportBar::changeListenerCallback (ChangeBroadcaster*)
{
    updateButtons();
}

void TransportBar::setLaunchQuantisation (int enumIndex)
{
    // id (index+1) maps back to a 0-based te::LaunchQType enum index — see the ctor population.
    launchQuantBox.setSelectedId (enumIndex + 1, dontSendNotification);
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

    // MIDI-clock is a device-level setting (not Edit-scoped), so its query needs no live Edit; fall
    // back to the toggle's own state only when the seam is unwired.
    const bool clockOn = queryMidiClockEnabled ? queryMidiClockEnabled()
                                               : midiClockButton.getToggleState();
    midiClockButton.setToggleState (clockOn, dontSendNotification);

    // Global performance-capture armed state (Wave 7) — Edit-scoped engine truth via the query seam.
    const bool captureOn = (edit != nullptr && queryGlobalRecordArmed) ? queryGlobalRecordArmed()
                                                                       : globalRecButton.getToggleState();
    globalRecButton.setToggleState (captureOn, dontSendNotification);

    const int bars = (edit != nullptr && queryCountInBars) ? queryCountInBars() : 0;
    for (int i = 0; i < numCountInOptions; ++i)
        if (countInOptions[i] == bars)
        {
            countInBox.setSelectedId (i + 1, dontSendNotification);
            break;
        }

    // Launch quantisation is Edit-global engine state; fall back to the control's own selection
    // (rather than forcing a default) when there is no live Edit or the seam is unwired, so we
    // never overwrite a user's still-pending choice with a guess.
    if (edit != nullptr && queryLaunchQuantisation)
        setLaunchQuantisation (queryLaunchQuantisation());
}
