#include "ui/session/SessionMasterStrip.h"
#include "ui/ForgeLookAndFeel.h"
#include "ui/common/StripWidgets.h"      // shared fader styling + ranges (W05)
#include "core/Log.h"

using namespace juce;

namespace
{
    // Corner metrics — the strip fills SessionLayout::sceneColW (168) × SessionLayout::mixerBandH (96).
    constexpr int swatchH = 5;   // accent band across the top (twin of MixerView::MasterStrip)
    constexpr int padX   = 8;    // horizontal inset
    constexpr int padY   = 6;    // vertical inset
    constexpr int nameH  = 16;   // "MASTER" label row
    constexpr int meterH = 10;   // horizontal peak-meter row
    constexpr int gapY   = 5;    // vertical gap between rows

    constexpr int pollHz = 12;   // glanceable cadence, matches SessionMixerStrip
}

//==============================================================================
SessionMasterStrip::SessionMasterStrip()
{
    nameLabel.setText ("MASTER", dontSendNotification);
    nameLabel.setJustificationType (Justification::centredLeft);
    nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::accent));
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (Font (FontOptions (11.0f)));
    addAndMakeVisible (nameLabel);

    // Horizontal dB fader (shared style + horizontal override, like SessionMixerStrip). No text box —
    // the corner is compact and the dB is a selftest read-back, not an on-screen readout.
    forge::strip::styleDbFader (fader);
    fader.setSliderStyle (Slider::LinearHorizontal);
    fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    fader.setTooltip ("Master volume (dB)");
    fader.onValueChange = [this]
    {
        if (auto* mv = resolveMaster())
            mv->setVolumeDb (jlimit (-100.0f, 12.0f, (float) fader.getValue()));
    };
    fader.onDragStart = [this] { faderDragging = true; };
    fader.onDragEnd   = [this] { faderDragging = false; };
    addAndMakeVisible (fader);

    // Peak meter: horizontal, with the W08 peak-HOLD line + sticky clip latch enabled (the master is
    // the meter most worth watching; per-track strips stay default-off so their paint is unchanged).
    meter.setHorizontal (true);
    meter.setPeakHold (true);
    meter.setShowClip (true);   // click the meter to clear a latched clip (PeakMeter::mouseDown)
    addAndMakeVisible (meter);

    setEdit (nullptr);   // start empty
}

SessionMasterStrip::~SessionMasterStrip()
{
    stopTimer();   // R4: stop the poll FIRST so no tick lands while members tear down
}

//==============================================================================
te::VolumeAndPanPlugin* SessionMasterStrip::resolveMaster() const
{
    if (edit == nullptr)
        return nullptr;

    return edit->getMasterVolumePlugin().get();   // fresh every call — never cached (R1)
}

void SessionMasterStrip::setEdit (te::Edit* e)
{
    edit = e;
    rebindFromEdit();

    if (bound && isVisible())
        startTimerHz (pollHz);
    else
        stopTimer();
}

void SessionMasterStrip::visibilityChanged()
{
    if (isVisible() && bound)
    {
        refreshControls();   // resync immediately so a re-shown strip never renders stale
        startTimerHz (pollHz);
    }
    else
    {
        stopTimer();
    }
}

void SessionMasterStrip::rebindFromEdit()
{
    auto* mv = resolveMaster();
    bound = (mv != nullptr);

    nameLabel.setVisible (bound);
    fader.setVisible (bound);
    meter.setVisible (bound);

    if (! bound)
    {
        meter.detach();   // release any prior source with the empty-state clear
        if (edit != nullptr)   // an edit with no master plugin is a genuine anomaly (log once, not on a tick)
            FORGE_LOG_ERROR ("Session master strip: master volume plugin not found — master fader inert");
        repaint();
        return;
    }

    refreshControls();   // seed the fader from the engine
    repaint();
}

//==============================================================================
void SessionMasterStrip::timerCallback()
{
    if (resolveMaster() == nullptr)
    {
        if (bound)
            setEdit (nullptr);   // master vanished (project swap mid-poll) — self-clear to empty
        return;
    }

    refreshControls();

    // Re-bind the meter to the CURRENT playback context's master output measurer and pull a sample.
    // The context is created/freed with the transport graph, so re-bind every poll (PeakMeter::attach
    // no-ops on an unchanged source and its WeakReference makes a freed context's measurer safe).
    te::LevelMeasurer* src = nullptr;
    if (auto* ctx = edit->getTransport().getCurrentPlaybackContext())
        src = &ctx->masterLevels;

    meter.attach (src);
    meter.poll (1.0f / (float) pollHz);
}

void SessionMasterStrip::refreshControls()
{
    auto* mv = resolveMaster();
    if (mv == nullptr)
    {
        if (bound)
        {
            bound = false;
            meter.detach();
            nameLabel.setVisible (false);
            fader.setVisible (false);
            meter.setVisible (false);
            repaint();
        }
        return;
    }

    bound = true;

    // Engine->widget only, dontSendNotification, and never while the user holds the fader / has focus.
    if (! faderDragging && ! fader.hasKeyboardFocus (true))
        fader.setValue (mv->getVolumeDb(), dontSendNotification);
}

//==============================================================================
bool   SessionMasterStrip::isBound()    const { return bound; }
double SessionMasterStrip::getFaderDb() const { return fader.getValue(); }

//==============================================================================
void SessionMasterStrip::resized()
{
    auto r = getLocalBounds().reduced (padX, padY);

    if (! bound)
        return;   // empty state is painted; no children are visible

    nameLabel.setBounds (r.removeFromTop (nameH));
    r.removeFromTop (gapY);
    meter.setBounds (r.removeFromTop (meterH));
    r.removeFromTop (gapY);
    fader.setBounds (r.removeFromTop (jmax (18, r.getHeight())));
}

void SessionMasterStrip::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (bounds);

    // Accent band across the top (matches MixerView::MasterStrip + the scene column's master chrome)
    // so the corner reads as "master" and ties to the scene column directly above it.
    g.setColour (Colour (ForgeLookAndFeel::accent));
    g.fillRect (bounds.removeFromTop (swatchH));

    // Top + left hairlines separate the corner from the scene column above and the mixer band to its left.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, 0, 1, getHeight());
}
