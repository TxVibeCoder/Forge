#include "ui/tray/ChannelTray.h"
#include "ui/ForgeLookAndFeel.h"
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"
#include "ui/plugins/PluginWindow.h"
#include "services/files/ProjectSession.h"   // aux-send seam + getEdit
#include "core/Log.h"

using namespace juce;

namespace
{
    // Fader travel in dB — matches the mixer strip fader so the two surfaces read identically
    // (and matches the range EngineHelpers::set/getTrackVolumeDb round-trips cleanly).
    constexpr double kMinDb = -60.0;
    constexpr double kMaxDb =   6.0;

    // Pan travel: hard-left (-1) .. centre (0) .. hard-right (+1).
    constexpr double kMinPan = -1.0;
    constexpr double kMaxPan =  1.0;

    // Aux-send knob travel in dB. The seam reports engine silence (<= -100) for "no send", so the
    // knob clamps to the bottom until the user dials one in — same convention as the mixer.
    constexpr double kMinSendDb = -60.0;
    constexpr double kMaxSendDb =   6.0;

    // Layout constants (compact tray variants of the MixerView strip metrics).
    constexpr int bandH      = 6;     // track-colour band across the top (painted, not a child)
    constexpr int nameH      = 20;
    constexpr int panH       = 48;
    constexpr int sendRowH   = 46;    // knob + its bus letter
    constexpr int sendLabelH = 12;
    constexpr int insertRowH = 16;
    constexpr int controlsH  = 26;    // the M/S row at the bottom
    constexpr int faderMinH  = 110;   // insert rows never squeeze the fader below this

    juce::String busLetter (int b) { return String::charToString ((juce_wchar) ('A' + b)); }
}

//==============================================================================
ChannelTray::ChannelTray (ProjectSession& s)
    : session (s)
{
    // --- Name --------------------------------------------------------------------------------
    nameLabel.setJustificationType (Justification::centred);
    nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textPrim));
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (nameLabel);

    // --- Pan (rotary, -1..+1) ----------------------------------------------------------------
    pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    pan.setRange (kMinPan, kMaxPan, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);   // double-click -> centre
    pan.setColour (Slider::thumbColourId,             Colour (ForgeLookAndFeel::accent));
    pan.setColour (Slider::rotarySliderFillColourId,  Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
    pan.setColour (Slider::rotarySliderOutlineColourId, Colour (ForgeLookAndFeel::hairline));
    pan.setTooltip ("Pan");
    pan.onValueChange = [this]
    {
        if (auto* t = resolveLiveTrack())
            EngineHelpers::setTrackPan (*t, (float) pan.getValue());
    };
    pan.onDragStart = [this] { panDragging = true; };
    pan.onDragEnd   = [this] { panDragging = false; };
    addAndMakeVisible (pan);

    // --- A/B aux sends (side by side; levels through the ProjectSession seam only) ------------
    for (int b = 0; b < auxBusCount; ++b)
    {
        auto* knob = sendKnobs.add (new Slider());
        knob->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        knob->setRange (kMinSendDb, kMaxSendDb, 0.1);
        knob->setDoubleClickReturnValue (true, kMinSendDb);   // double-click -> no send
        knob->setColour (Slider::thumbColourId,             Colour (ForgeLookAndFeel::accent));
        knob->setColour (Slider::rotarySliderFillColourId,  Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        knob->setColour (Slider::rotarySliderOutlineColourId, Colour (ForgeLookAndFeel::hairline));
        knob->setTooltip ("Send to Return " + busLetter (b));
        knob->onValueChange = [this, b, knob]
        {
            // Resolve the ABSOLUTE track index live (the send seam keys on it); a dead track no-ops
            // — the next tick clears the tray.
            int liveIndex = -1;
            if (resolveLiveTrack (&liveIndex) != nullptr)
                session.setTrackSendLevel (liveIndex, b, (float) knob->getValue());
        };
        knob->onDragStart = [this, b] { sendDragging[b] = true; };
        knob->onDragEnd   = [this, b] { sendDragging[b] = false; };
        addAndMakeVisible (knob);

        auto* lab = sendLabels.add (new Label());
        lab->setText (busLetter (b), dontSendNotification);
        lab->setJustificationType (Justification::centred);
        lab->setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
        lab->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (lab);
    }

    // --- Insert list "+" row -------------------------------------------------------------------
    addInsertButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::raisedBg));
    addInsertButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::accent));
    addInsertButton.setTooltip ("Add an insert plugin");
    addInsertButton.onClick = [this] { showAddInsertMenu(); };
    addAndMakeVisible (addInsertButton);

    // --- Volume fader (vertical, dB text box below) --------------------------------------------
    fader.setSliderStyle (Slider::LinearVertical);
    fader.setRange (kMinDb, kMaxDb, 0.1);
    fader.setNumDecimalPlacesToDisplay (1);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);   // double-click -> unity (0 dB)
    fader.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
    fader.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
    fader.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
    fader.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
    fader.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));
    fader.setTextBoxStyle (Slider::TextBoxBelow, false, preferredWidth - 32, 16);
    fader.onValueChange = [this]
    {
        if (auto* t = resolveLiveTrack())
            EngineHelpers::setTrackVolumeDb (*t, (float) fader.getValue());
    };
    fader.onDragStart = [this] { faderDragging = true; };
    fader.onDragEnd   = [this] { faderDragging = false; };
    addAndMakeVisible (fader);

    // --- M / S toggles --------------------------------------------------------------------------
    auto configureToggle = [this] (TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (TextButton::buttonColourId,   Colour (ForgeLookAndFeel::raisedBg));
        b.setColour (TextButton::buttonOnColourId, Colour (ForgeLookAndFeel::accent));
        b.setColour (TextButton::textColourOffId,  Colour (ForgeLookAndFeel::textSec));
        b.setColour (TextButton::textColourOnId,   Colour (ForgeLookAndFeel::onAccent));
        addAndMakeVisible (b);
    };
    configureToggle (muteButton);
    configureToggle (soloButton);

    muteButton.onClick = [this] { if (auto* t = resolveLiveTrack()) t->setMute (muteButton.getToggleState()); };
    soloButton.onClick = [this] { if (auto* t = resolveLiveTrack()) t->setSolo (soloButton.getToggleState()); };

    setTrack (nullptr);   // start in the empty-hint state
}

ChannelTray::~ChannelTray()
{
    stopTimer();   // stop the live-sync poll FIRST so no tick can land while members tear down
}

//==============================================================================
te::AudioTrack* ChannelTray::resolveLiveTrack (int* liveIndexOut) const
{
    if (liveIndexOut != nullptr)
        *liveIndexOut = -1;

    auto* ed = session.getEdit();
    if (ed == nullptr || track == nullptr)
        return nullptr;

    // The R1 identity scan: pointer-compare against the CURRENT edit's track list — no dereference
    // happens unless the track is found. Also yields the absolute index the send seam keys on.
    const int idx = te::getAudioTracks (*ed).indexOf (track);
    if (idx < 0)
        return nullptr;

    if (liveIndexOut != nullptr)
        *liveIndexOut = idx;

    return track;
}

void ChannelTray::setTrack (te::AudioTrack* t)
{
    track = t;
    rebuildFromTrack();

    // rebuildFromTrack() nulls a bind that failed validation, so re-check before starting the
    // poll — and only poll while actually VISIBLE (QC: a hidden tray polled the engine at 10 Hz
    // indefinitely); visibilityChanged() starts/stops the timer as the sidebar tab flips.
    if (track != nullptr && isVisible())
        startTimerHz (10);
    else
        stopTimer();
}

void ChannelTray::visibilityChanged()
{
    if (isVisible() && track != nullptr)
    {
        refreshNow();       // resync immediately so a re-shown tray never renders a stale frame
        startTimerHz (10);
    }
    else
    {
        stopTimer();
    }
}

StringArray ChannelTray::chainSignature (te::AudioTrack& t)
{
    StringArray sig;
    for (auto* p : t.pluginList)
        if (p != nullptr)
            sig.add (String::toHexString ((juce::pointer_sized_int) p) + ":" + p->getName());
    return sig;
}

void ChannelTray::clearIfShowing (te::Track* t)
{
    // Pure identity compare — at a delete-track hook `t` may already dangle, so never dereference.
    if (t != nullptr && static_cast<te::Track*> (track) == t)
        setTrack (nullptr);
}

void ChannelTray::rebuildFromTrack()
{
    int liveIndex = -1;
    auto* t = resolveLiveTrack (&liveIndex);

    if (t == nullptr && track != nullptr)
    {
        // Event path only (setTrack) — the shell handed us a track that is not in the current
        // edit. Degrade to the empty state rather than trusting the pointer.
        FORGE_LOG_WARN ("ChannelTray: bound track is not in the current edit — showing the empty state");
        track = nullptr;
    }

    const bool showing = (track != nullptr);
    sendsShown = showing && ! session.isAuxReturnTrack (*t);   // a return never sends to itself

    nameLabel.setVisible (showing);
    pan.setVisible (showing);
    for (int b = 0; b < auxBusCount; ++b)
    {
        sendKnobs[b]->setVisible (sendsShown);
        sendLabels[b]->setVisible (sendsShown);
    }
    addInsertButton.setVisible (showing);
    fader.setVisible (showing);
    muteButton.setVisible (showing);
    soloButton.setVisible (showing);

    if (! showing)
    {
        insertRows.clear();
        lastChainSig.clear();
        trackColour = Colour (ForgeLookAndFeel::panelBg);
        resized();
        repaint();
        return;
    }

    // Seed the paint cache + structure state, then let the shared sync path fill every value.
    trackColour  = t->getColour();
    lastChainSig = chainSignature (*t);
    rebuildInsertRows();
    syncControls (*t, liveIndex);

    resized();
    repaint();
}

void ChannelTray::rebuildInsertRows()
{
    insertRows.clear();

    auto* t = resolveLiveTrack();
    if (t == nullptr)
        return;

    for (auto* plugin : PluginHost::getTrackInserts (*t))
    {
        if (plugin == nullptr)
            continue;

        // Each row captures the engine's reference-counted handle, so even a plugin removed on
        // another surface between ticks can never be a freed dereference; the containment check
        // on click keeps a just-removed insert from opening a stale editor.
        te::Plugin::Ptr held (plugin);

        auto* row = insertRows.add (new TextButton());
        row->setButtonText (plugin->getName());
        row->setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::panelBg));
        row->setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
        row->setTooltip ("Open " + plugin->getName());
        row->onClick = [this, held]
        {
            if (auto* live = resolveLiveTrack())
                if (held != nullptr && live->pluginList.contains (held.get()))
                    PluginWindow::show (*held);
        };
        addAndMakeVisible (row);
    }
}

void ChannelTray::showAddInsertMenu()
{
    auto* t = resolveLiveTrack();
    if (t == nullptr)
        return;

    auto names = PluginHost::getAvailablePluginNames (t->edit.engine);

    PopupMenu menu;
    if (names.isEmpty())
    {
        menu.addItem (1, "(no plugins available)", false, false);
    }
    else
    {
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i]);
    }

    Component::SafePointer<ChannelTray> safeThis (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addInsertButton),
                        [safeThis, names] (int result)
                        {
                            if (safeThis == nullptr || result <= 0 || result > names.size())
                                return;

                            // Re-validate across the async gap: the track can die while the menu is open.
                            auto* live = safeThis->resolveLiveTrack();
                            if (live == nullptr)
                                return;

                            if (PluginHost::addPluginToTrack (*live, names[result - 1]) == nullptr)
                                FORGE_LOG_WARN ("ChannelTray: failed to add plugin '" + names[result - 1]
                                                + "' to track '" + live->getName() + "'");

                            // The next tick's chain-size edge-compare would catch this anyway;
                            // force it so the new row appears immediately.
                            safeThis->refreshNow();
                        });
}

//==============================================================================
// 10 Hz live-sync poll (message thread — juce::Timer). Order is load-bearing: identity first
// (self-clear before ANY dereference — the R1 rule), then structure (insert-chain edge compare),
// then guarded value sync. Never logs: the self-clear on a vanished track is an EXPECTED path
// (track deleted in Arrange / undo of a track add), not a failure — the shell's mutation hooks
// clear eagerly and this is the designed backstop.

void ChannelTray::timerCallback()
{
    if (track == nullptr)
        return;

    int liveIndex = -1;
    auto* t = resolveLiveTrack (&liveIndex);
    if (t == nullptr)
    {
        setTrack (nullptr);   // bound track left the edit — empty state, timer stops
        return;
    }

    // Structure: rebuild the insert rows when the chain SIGNATURE moved — identity + name per
    // plugin, so a same-count replace or a rename refreshes too (QC: a size-only compare missed
    // both). The signature build is a short message-thread walk at 10 Hz, within the tray's
    // documented per-tick read budget.
    auto sig = chainSignature (*t);
    if (sig != lastChainSig)
    {
        lastChainSig = std::move (sig);
        rebuildInsertRows();
        resized();
    }

    syncControls (*t, liveIndex);
}

void ChannelTray::syncControls (te::AudioTrack& t, int liveIndex)
{
    // Engine-to-widget only: every write uses dontSendNotification so no onValueChange/onClick
    // fires and nothing writes back. Slider::setValue / Button::setToggleState self-no-op on
    // unchanged values and the name/colour are edge-compared, so a steady-state tick repaints
    // nothing. Hot path — never logs.
    if (! faderDragging && ! fader.hasKeyboardFocus (true))
        fader.setValue (EngineHelpers::getTrackVolumeDb (t), dontSendNotification);

    if (! panDragging && ! pan.hasKeyboardFocus (true))
        pan.setValue (EngineHelpers::getTrackPan (t), dontSendNotification);

    if (sendsShown && liveIndex >= 0)
        for (int b = 0; b < auxBusCount; ++b)
            if (! sendDragging[b] && ! sendKnobs[b]->hasKeyboardFocus (true))
                sendKnobs[b]->setValue (jlimit (kMinSendDb, kMaxSendDb,
                                                (double) session.getTrackSendLevel (liveIndex, b)),
                                        dontSendNotification);

    if (! muteButton.isMouseButtonDown())
        muteButton.setToggleState (t.isMuted (false), dontSendNotification);

    if (! soloButton.isMouseButtonDown())
        soloButton.setToggleState (t.isSolo (false), dontSendNotification);

    const auto liveName = t.getName();
    if (nameLabel.getText() != liveName)
        nameLabel.setText (liveName, dontSendNotification);

    const auto liveColour = t.getColour();
    if (liveColour != trackColour)
    {
        trackColour = liveColour;
        repaint();
    }
}

//==============================================================================
void ChannelTray::refreshNow()
{
    timerCallback();
}

double ChannelTray::getFaderDb() const
{
    return fader.getValue();
}

bool ChannelTray::getMuteShown() const
{
    return muteButton.getToggleState();
}

bool ChannelTray::isShowingTrack() const
{
    return track != nullptr;
}

//==============================================================================
void ChannelTray::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (bandH);   // track-colour band (painted, not a child)

    if (track == nullptr)
        return;   // empty state is painted; no children are visible

    nameLabel.setBounds (r.removeFromTop (nameH).reduced (4, 1));

    // Pan: a square rotary centred in its row.
    pan.setBounds (r.removeFromTop (panH).withSizeKeepingCentre (panH, panH).reduced (2));

    if (sendsShown)
    {
        auto row = r.removeFromTop (sendRowH).reduced (12, 2);
        const int cellW = row.getWidth() / auxBusCount;

        for (int b = 0; b < auxBusCount; ++b)
        {
            auto cell = (b == auxBusCount - 1) ? row : row.removeFromLeft (cellW);
            sendLabels[b]->setBounds (cell.removeFromBottom (sendLabelH));
            sendKnobs[b]->setBounds (cell.reduced (2, 1));
        }
    }

    // Insert list: rows + the pinned "+" row, clamped so the fader never drops below faderMinH.
    // Rows that no longer fit are hidden (never overlapped) and reappear when the tray grows.
    {
        const int reserve = faderMinH + controlsH + 8;
        const int wanted  = (insertRows.size() + 1) * insertRowH;
        const int panelH  = jlimit (insertRowH, jmax (insertRowH, r.getHeight() - reserve), wanted);

        auto panel = r.removeFromTop (panelH).reduced (8, 0);
        addInsertButton.setBounds (panel.removeFromBottom (insertRowH).reduced (0, 1));

        for (auto* row : insertRows)
        {
            const bool fits = panel.getHeight() >= insertRowH;
            row->setVisible (fits);
            if (fits)
                row->setBounds (panel.removeFromTop (insertRowH));
        }
    }

    // M/S pinned to the bottom; the fader (dB text box below) fills everything in between.
    auto controls = r.removeFromBottom (controlsH).reduced (8, 4);
    const int bw = jmax (16, (controls.getWidth() - 6) / 2);
    muteButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (6);
    soloButton.setBounds (controls.removeFromLeft (bw));

    fader.setBounds (r.reduced (8, 4));
}

void ChannelTray::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (bounds);

    if (track == nullptr)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (13.0f)));
        g.drawText ("Select a track", getLocalBounds(), Justification::centred);
    }
    else
    {
        // Track-colour band across the top — painted from the tick-maintained cache, so paint()
        // never touches the engine (a repaint can land between ticks, after the track died).
        g.setColour (trackColour);
        g.fillRect (bounds.removeFromTop (bandH));
    }

    // Right-edge hairline separates the tray from the arrangement beside it.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}
