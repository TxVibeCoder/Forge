#include "ui/session/SessionMixerStrip.h"
#include "ui/ForgeLookAndFeel.h"
#include "ui/common/StripWidgets.h"      // shared fader/knob/toggle styling + ranges (W05)
#include "engine/EngineHelpers.h"

using namespace juce;

namespace
{
    // Compact band metrics (Session variant of the tray strip metrics) — file-local. The band is
    // SessionLayout::mixerBandH (96) tall under a SessionLayout::trackColW (179) column.
    constexpr int padX      = 6;    // horizontal inset inside the strip
    constexpr int padY      = 5;    // vertical inset inside the strip
    constexpr int meterH     = 9;   // horizontal peak-meter row height (a thin bar across the top)
    constexpr int faderH     = 22;  // horizontal dB fader row height
    constexpr int knobW       = 40; // pan knob column width (square rotary)
    constexpr int controlsH   = 22; // the M / S row height
    constexpr int gapY        = 4;  // vertical gap between rows

    // The poll cadence. 12 Hz is plenty for a glanceable strip and is cheaper than the pad grid's
    // 25 Hz across many columns.
    constexpr int pollHz      = 12;
}

//==============================================================================
SessionMixerStrip::SessionMixerStrip()
{
    // --- Horizontal dB fader ---------------------------------------------------------------------
    // Shared styling sets the range / colours / double-click-to-unity; override the orientation to a
    // horizontal bar (the shared helper defaults to LinearVertical — the caller owns the style, per
    // StripWidgets.h). No text box: the band is too short and the fader dB is a selftest read-back,
    // not an on-screen readout here.
    forge::strip::styleDbFader (fader);
    fader.setSliderStyle (Slider::LinearHorizontal);
    fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    fader.setTooltip ("Volume (dB)");
    fader.onValueChange = [this]
    {
        if (auto* t = resolveLiveTrack())
            EngineHelpers::setTrackVolumeDb (*t, (float) fader.getValue());
    };
    fader.onDragStart = [this] { faderDragging = true; };
    fader.onDragEnd   = [this] { faderDragging = false; };
    addAndMakeVisible (fader);

    // --- Pan knob (rotary, -1..+1) ---------------------------------------------------------------
    forge::strip::stylePanKnob (pan);
    pan.setTooltip ("Pan");
    pan.onValueChange = [this]
    {
        if (auto* t = resolveLiveTrack())
            EngineHelpers::setTrackPan (*t, (float) pan.getValue());
    };
    pan.onDragStart = [this] { panDragging = true; };
    pan.onDragEnd   = [this] { panDragging = false; };
    addAndMakeVisible (pan);

    // --- Peak meter (source rebound in rebindFromTrack) ------------------------------------------
    meter.setHorizontal (true);   // W08: wide, short meter row -> left->right fill (PeakMeter horizontal mode)
    addAndMakeVisible (meter);

    // --- M / S toggles ---------------------------------------------------------------------------
    // Shared style (W05) + caller-owned add/show; the lambdas re-resolve the track before any write
    // (R1) and toggle the CachedValue directly (mute/solo are NOT on the undo stack — W05 gotcha).
    auto configureToggle = [this] (TextButton& b)
    {
        forge::strip::styleStripToggle (b);
        addAndMakeVisible (b);
    };
    configureToggle (muteButton);
    configureToggle (soloButton);

    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    muteButton.onClick = [this] { if (auto* t = resolveLiveTrack()) t->setMute (muteButton.getToggleState()); };
    soloButton.onClick = [this] { if (auto* t = resolveLiveTrack()) t->setSolo (soloButton.getToggleState()); };

    setTrack (nullptr, -1);   // start in the empty state
}

SessionMixerStrip::~SessionMixerStrip()
{
    stopTimer();   // R4: stop the poll FIRST so no tick lands while members tear down
}

//==============================================================================
te::AudioTrack* SessionMixerStrip::resolveLiveTrack() const
{
    if (edit == nullptr || trackIndex < 0)
        return nullptr;

    // R1 resolve: fresh subscript of the CURRENT edit's track list every time — no dereference
    // happens unless the absolute index is in range.
    const auto tracks = te::getAudioTracks (*edit);
    return isPositiveAndBelow (trackIndex, tracks.size()) ? tracks[trackIndex] : nullptr;
}

void SessionMixerStrip::setTrack (te::Edit* e, int index)
{
    edit       = e;
    trackIndex = index;
    rebindFromTrack();

    // Poll only while actually bound AND visible (a hidden Session tab does no engine work).
    if (bound && isVisible())
        startTimerHz (pollHz);
    else
        stopTimer();
}

void SessionMixerStrip::setIsReturn (bool r)
{
    if (isReturn == r)
        return;

    isReturn = r;
    repaint();
}

void SessionMixerStrip::visibilityChanged()
{
    if (isVisible() && bound)
    {
        refreshControls();      // resync immediately so a re-shown strip never renders stale
        startTimerHz (pollHz);
    }
    else
    {
        stopTimer();
    }
}

void SessionMixerStrip::rebindFromTrack()
{
    auto* t = resolveLiveTrack();
    bound = (t != nullptr);

    fader.setVisible (bound);
    pan.setVisible (bound);
    meter.setVisible (bound);   // hidden in the empty state so no stray bar paints
    muteButton.setVisible (bound);
    soloButton.setVisible (bound);

    if (! bound)
    {
        meter.detach();         // release the old track's measurer with the empty-state clear
        repaint();
        return;
    }

    // Bind the meter to this track's LevelMeterPlugin measurer. Edit-owned + held as a
    // WeakReference in PeakMeter, so attach-on-rebind is the whole lifetime story; the poll
    // re-resolves the track before every meter.poll but never re-attaches unless the source moved.
    if (auto* lm = t->getLevelMeterPlugin())
        meter.attach (&lm->measurer);
    else
        meter.detach();

    refreshControls();          // seed every value from the engine
    repaint();
}

//==============================================================================
void SessionMixerStrip::timerCallback()
{
    // R1: re-resolve before ANY dereference; a vanished index self-clears to empty (the timer then
    // stops via setTrack). This is an EXPECTED path (track deleted on another surface), not a
    // failure — never logs.
    auto* t = resolveLiveTrack();
    if (t == nullptr)
    {
        if (bound)
            setTrack (nullptr, -1);
        return;
    }

    refreshControls();
    meter.poll (1.0f / (float) pollHz);
}

void SessionMixerStrip::refreshControls()
{
    auto* t = resolveLiveTrack();
    if (t == nullptr)
    {
        // Selftest / re-validate path: the bound index no longer resolves — degrade to empty.
        if (bound)
        {
            bound = false;
            meter.detach();
            fader.setVisible (false);
            pan.setVisible (false);
            meter.setVisible (false);
            muteButton.setVisible (false);
            soloButton.setVisible (false);
            repaint();
        }
        return;
    }

    bound = true;

    // Engine->widget only: every write is dontSendNotification so no onValueChange/onClick fires and
    // nothing writes back. setValue / setToggleState self-no-op on unchanged values. Skip any control
    // the user is actively holding (drag) or has keyboard focus so the poll never fights a gesture.
    if (! faderDragging && ! fader.hasKeyboardFocus (true))
        fader.setValue (EngineHelpers::getTrackVolumeDb (*t), dontSendNotification);

    if (! panDragging && ! pan.hasKeyboardFocus (true))
        pan.setValue (EngineHelpers::getTrackPan (*t), dontSendNotification);

    if (! muteButton.isMouseButtonDown())
        muteButton.setToggleState (t->isMuted (false), dontSendNotification);

    if (! soloButton.isMouseButtonDown())
        soloButton.setToggleState (t->isSolo (false), dontSendNotification);
}

//==============================================================================
bool   SessionMixerStrip::isBound()     const { return bound; }
double SessionMixerStrip::getFaderDb()  const { return fader.getValue(); }
double SessionMixerStrip::getPanValue() const { return pan.getValue(); }
bool   SessionMixerStrip::isMuteOn()    const { return muteButton.getToggleState(); }
bool   SessionMixerStrip::isSoloOn()    const { return soloButton.getToggleState(); }

//==============================================================================
void SessionMixerStrip::resized()
{
    auto r = getLocalBounds().reduced (padX, padY);

    if (! bound)
        return;   // empty state is painted; no children are visible

    // Row 1: horizontal peak meter (thin bar across the full strip width).
    meter.setBounds (r.removeFromTop (meterH));
    r.removeFromTop (gapY);

    // Row 2: horizontal dB fader across the full width.
    fader.setBounds (r.removeFromTop (faderH));
    r.removeFromTop (gapY);

    // Row 3 (bottom): pan knob on the left, M / S toggles filling the rest.
    auto bottom = r.removeFromBottom (jmax (controlsH, r.getHeight()));
    pan.setBounds (bottom.removeFromLeft (knobW));
    bottom.removeFromLeft (padX);

    const int bw = jmax (16, (bottom.getWidth() - 4) / 2);
    muteButton.setBounds (bottom.removeFromLeft (bw));
    bottom.removeFromLeft (4);
    soloButton.setBounds (bottom.removeFromLeft (bw));
}

void SessionMixerStrip::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    // Base panel fill. An aux-return column gets a SUBTLE desaturated tint (a muted, greyed variant
    // of the existing panel tone — NOT a new accent) so it reads as a return without inventing a
    // colour (Fable charter: one colour = one meaning). A plain track is the standard panel tone.
    if (isReturn)
        g.setColour (Colour (ForgeLookAndFeel::panelBg)
                         .brighter (0.06f)
                         .withMultipliedSaturation (0.5f));   // muted, desaturated variant — not a new accent
    else
        g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (bounds);

    // Top hairline separates the band from the pad grid above; left hairline separates columns.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, 0, 1, getHeight());

    if (! bound)
    {
        // Empty column (no track): a dim placeholder tone, no controls. Keeps the band legible when
        // the grid has no tracks or a column is out of range.
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.35f));
        g.setFont (Font (FontOptions (10.0f)));
        g.drawText ("-", bounds, Justification::centred);
    }
}
