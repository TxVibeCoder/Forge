#include "ui/session/TrackColumnComponent.h"

using namespace juce;

//==============================================================================
TrackColumnComponent::TrackColumnComponent (te::AudioTrack& t, int index)
    : track (t), trackIndex (index), trackColour (t.getColour())
{
    // --- Clip pads (one per scene) ---------------------------------------------------------
    // Each pad holds (trackIndex, sceneIndex) only (R1) and maps its mouse intent up to this
    // column's callbacks, tagged with its own sceneIndex.
    for (int s = 0; s < SessionLayout::numScenes; ++s)
    {
        auto* pad = slots.add (new ClipSlotComponent (trackIndex, s, trackColour));

        pad->onClicked = [this, s]
        {
            if (onSlotClicked != nullptr)
                onSlotClicked (trackIndex, s);
        };

        pad->onDoubleClicked = [this, s]
        {
            if (onSlotDoubleClicked != nullptr)
                onSlotDoubleClicked (trackIndex, s);
        };

        pad->onRightClicked = [this, s] (const MouseEvent& e)
        {
            if (onSlotRightClicked != nullptr)
                onSlotRightClicked (trackIndex, s, e);
        };

        addAndMakeVisible (pad);
    }

    // --- M / S / R toggles -----------------------------------------------------------------
    configureToggle (muteButton);
    configureToggle (soloButton);
    configureToggle (armButton);

    // M / S mutate the track locally (mirroring ChannelStrip / TrackLaneComponent) AND notify
    // the parent so the shell can persist; R only notifies (the engine's record path owns arm
    // state, queried back via isTrackArmed on refresh).
    muteButton.onClick = [this]
    {
        track.setMute (muteButton.getToggleState());
        if (onMute != nullptr)
            onMute (trackIndex);
    };

    soloButton.onClick = [this]
    {
        track.setSolo (soloButton.getToggleState());
        if (onSolo != nullptr)
            onSolo (trackIndex);
    };

    armButton.onClick = [this]
    {
        if (onArm != nullptr)
            onArm (trackIndex);
    };

    // --- Per-track clip-stop ■ (footer) ----------------------------------------------------
    stopButton.setButtonText (String::charToString ((juce_wchar) 0x25a0));   // ■
    stopButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::raisedBg));
    stopButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
    stopButton.setTooltip ("Stop this track's clips");
    stopButton.onClick = [this]
    {
        if (onTrackStopAll != nullptr)
            onTrackStopAll (trackIndex);
    };
    addAndMakeVisible (stopButton);

    refreshHeader();
}

//==============================================================================
void TrackColumnComponent::configureToggle (TextButton& b)
{
    b.setClickingTogglesState (true);
    b.setColour (TextButton::buttonColourId,   Colour (ForgeLookAndFeel::raisedBg));
    b.setColour (TextButton::buttonOnColourId, Colour (ForgeLookAndFeel::accent));
    b.setColour (TextButton::textColourOffId,  Colour (ForgeLookAndFeel::textSec));
    b.setColour (TextButton::textColourOnId,   Colour (ForgeLookAndFeel::onAccent));
    addAndMakeVisible (b);
}

bool TrackColumnComponent::trackHasInstrument() const
{
    // A MIDI track is one whose chain hosts a synth / MIDI-input plugin (mirrors
    // PluginHost::ensureDefaultInstrument's own probe). Audio tracks have no such plugin.
    for (auto* p : track.pluginList)
        if (p != nullptr && (p->isSynth() || p->takesMidiInput()))
            return true;

    return false;
}

//==============================================================================
void TrackColumnComponent::setSlotVisual (int sceneIndex, SlotVisualState state, String label)
{
    if (auto* pad = slots[sceneIndex])
        pad->setVisualState (state, std::move (label));
}

void TrackColumnComponent::setSlotPulse (int sceneIndex, float pulseAlpha)
{
    if (auto* pad = slots[sceneIndex])
        pad->setPulseAlpha (pulseAlpha);
}

void TrackColumnComponent::setSlotSelected (int sceneIndex, bool shouldBeSelected)
{
    if (auto* pad = slots[sceneIndex])
        pad->setSelected (shouldBeSelected);
}

void TrackColumnComponent::setTrackColour (Colour newColour)
{
    if (trackColour == newColour)
        return;

    trackColour = newColour;

    for (auto* pad : slots)
        pad->setTrackColour (trackColour);

    repaint();   // header swatch
}

void TrackColumnComponent::refreshHeader()
{
    // Pull colour fresh in case the track was recoloured (propagates to pads + repaints swatch).
    setTrackColour (track.getColour());

    // Engine-truth M/S/R, so the toggles survive a rebuild (mirrors TrackLane::refreshControlStates).
    muteButton.setToggleState (track.isMuted (false), dontSendNotification);
    soloButton.setToggleState (track.isSolo  (false), dontSendNotification);
    armButton .setToggleState (isTrackArmed != nullptr && isTrackArmed (track), dontSendNotification);

    repaint();   // header text / type tag / instrument chip
}

//==============================================================================
void TrackColumnComponent::paint (Graphics& g)
{
    using namespace SessionLayout;

    auto bounds = getLocalBounds();

    // Lane backdrop behind the whole column (matches ArrangeView's lane literal, via SessionLayout).
    g.setColour (Colour (laneBg));
    g.fillRect (bounds);

    // --- Header band -----------------------------------------------------------------------
    auto header = bounds.removeFromTop (headerH);

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (header);

    // Colour swatch: a thin band across the very top of the header.
    auto swatch = header.removeFromTop (5);
    g.setColour (trackColour);
    g.fillRect (swatch);

    auto headerText = header.reduced (6, 2);

    // Track name (top line of the header text region).
    auto nameRow = headerText.removeFromTop (20);
    g.setColour (Colour (ForgeLookAndFeel::textPrim));
    g.setFont (Font (FontOptions (14.0f, Font::bold)));
    g.drawText (track.getName(), nameRow, Justification::centredLeft, true);

    // Audio / MIDI type tag.
    const bool midi = trackHasInstrument();
    auto tagRow = headerText.removeFromTop (16);
    g.setColour (Colour (ForgeLookAndFeel::textSec));
    g.setFont (Font (FontOptions (11.0f)));
    g.drawText (midi ? "MIDI" : "Audio", tagRow, Justification::centredLeft, true);

    // Instrument chip (MIDI tracks): the head synth's name in an accent-tinted pill; audio
    // tracks show a dimmed "—" so the row stays aligned.
    auto chipRow = headerText.removeFromTop (18);

    if (midi)
    {
        String instName;
        for (auto* p : track.pluginList)
            if (p != nullptr && (p->isSynth() || p->takesMidiInput()))
            {
                instName = p->getName();
                break;
            }

        auto chip = chipRow.withTrimmedRight (2).toFloat();
        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (0.18f));
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.setFont (Font (FontOptions (11.0f)));
        g.drawText (instName, chipRow.reduced (5, 0), Justification::centredLeft, true);
    }
    else
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (11.0f)));
        g.drawText (String::charToString ((juce_wchar) 0x2014), chipRow, Justification::centredLeft, true);  // —
    }

    // Armed tracks get a subtle record-red tint down the header's left edge.
    if (armButton.getToggleState())
    {
        g.setColour (Colour (ForgeLookAndFeel::recordRed).withAlpha (0.85f));
        g.fillRect (0, 0, 2, headerH);
    }

    // Right-edge hairline separates adjacent columns (columns sit flush, gap == 0).
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    // Hairline under the header band.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, headerH - 1, getWidth(), 1);
}

void TrackColumnComponent::resized()
{
    using namespace SessionLayout;

    auto r = getLocalBounds();

    r.removeFromTop (headerH);                  // header band (painted, M/S/R laid out below)
    auto footer = r.removeFromBottom (stopRowH);

    // The M/S/R toggles sit in the lower portion of the header band.
    auto controls = getLocalBounds().removeFromTop (headerH)
                        .removeFromBottom (20).reduced (6, 1);
    const int bw = jmax (16, (controls.getWidth() - 8) / 3);
    muteButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    soloButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    armButton.setBounds (controls.removeFromLeft (bw));

    // Clip pads fill the space between header and footer, split into scene rows via the SHARED
    // SessionLayout::rowBand partition so each pad row lines up exactly with the scene column's
    // launch row at any window height (no drift — QC fix).
    const int rowCount = slots.size();
    const int midTop   = r.getY();
    const int midH     = r.getHeight();

    for (int s = 0; s < rowCount; ++s)
    {
        const auto band = SessionLayout::rowBand (s, rowCount, midH);
        const Rectangle<int> cell (r.getX(), midTop + band.getStart(), r.getWidth(), band.getLength());
        slots[s]->setBounds (cell.reduced (slotPad));
    }

    // Per-track clip-stop ■.
    stopButton.setBounds (footer.reduced (slotPad, 2));
}
