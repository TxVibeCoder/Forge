#include "ui/mixer/MixerView.h"
#include "ui/ForgeLookAndFeel.h"
#include "ui/common/PeakMeter.h"             // shared level bar (W04b extraction)
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"
#include "ui/plugins/PluginWindow.h"
#include "services/files/ProjectSession.h"   // the aux seam (buses/sends)
#include "core/Log.h"

using namespace juce;

namespace
{
    // Fader travel in dB. -60 reads as -inf-ish silence at the bottom; +6 of headroom up top.
    // This matches the range mapped by EngineHelpers::set/getTrackVolumeDb (which clamps the
    // underlying 0..1 volume parameter into dB), so the slider position round-trips cleanly.
    constexpr double kMinDb =  -60.0;
    constexpr double kMaxDb =    6.0;

    // Pan travel: hard-left (-1) .. centre (0) .. hard-right (+1).
    constexpr double kMinPan = -1.0;
    constexpr double kMaxPan =  1.0;

    // Aux-send knob travel in dB. Bottom of the knob (kMinSendDb) reads as "no send"; the seam
    // reports <= kMinSendDb (engine silence) when a track has no send plugin for that bus, so the
    // knob sits at the bottom until the user dials one in. +6 of headroom up top, matching the fader.
    constexpr double kMinSendDb = -60.0;
    constexpr double kMaxSendDb =   6.0;

    /** Applies Forge's shared vertical dB-fader styling (range, readout, colours) to a slider.
        One source for the strip / master / return faders so their look can't drift apart. The
        caller sets the text-box style afterwards (its width differs per strip) and wires
        setValue/onValueChange. */
    inline void styleDbFader (Slider& f)
    {
        f.setSliderStyle (Slider::LinearVertical);
        f.setRange (kMinDb, kMaxDb, 0.1);
        f.setNumDecimalPlacesToDisplay (1);
        f.setTextValueSuffix (" dB");
        f.setDoubleClickReturnValue (true, 0.0);   // double-click -> unity (0 dB)
        f.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
        f.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        f.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
        f.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
        f.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));
    }
}

// PeakMeter — the shared thin vertical level bar — now lives in ui/common/PeakMeter.h (W04b),
// consumed here and by the Arrange channel tray. Its ballistics constants + dbToMeterFraction
// helper moved with it; the fader/pan/send constants above stay local to the mixer.

//==============================================================================
/*  InsertPanel — the list of a track's insert plugins plus a "+" add button. Each existing
    insert is a row: left-click the name opens its editor window (PluginWindow::show); the
    trailing "x" (or a right-click) removes it (PluginHost::removePlugin); a leading bypass dot
    toggles the plugin's enabled state (te::Plugin::setEnabled); and ▲/▼ reorder it within the
    track's chain. "+" pops a menu of available plugin names (PluginHost::getAvailablePluginNames)
    and adds the chosen one (PluginHost::addPluginToTrack). After any add/remove/move it calls
    onChanged() so the owning strip rebuilds its rows and the layout re-flows.

    The volume/level-meter plugins that every track carries are filtered out by PluginHost::
    getTrackInserts (per its contract — it returns user inserts only), so they never appear
    here as removable rows.

    Reorder recipe: getTrackInserts() returns the user inserts in chain order; the always-present
    [volume&pan, level-meter] tail sits AFTER them. Moving an insert means re-inserting its
    Plugin::Ptr at the chain index of its new neighbour via te::PluginList::insertPlugin — which
    inserts BEFORE the plugin currently at that index, exactly matching the invariant that
    PluginHost::addPluginToTrack relies on (new effects land just before the volume tail). We
    only ever swap an insert with an adjacent insert, so the tail is never displaced.            */
class InsertPanel : public Component
{
public:
    InsertPanel (te::AudioTrack& t, std::function<void()> changedCb)
        : track (t), onChanged (std::move (changedCb))
    {
        addButton.setButtonText ("+");
        addButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
        addButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::accent));
        addButton.setTooltip ("Add an insert plugin");
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);

        rebuildRows();
    }

    /** Number of insert rows currently shown (for height calc by the strip). */
    int getInsertCount() const { return rows.size(); }

    void rebuildRows()
    {
        rows.clear();

        const auto inserts = PluginHost::getTrackInserts (track);

        for (int i = 0; i < inserts.size(); ++i)
        {
            auto* plugin = inserts[i];
            if (plugin == nullptr)
                continue;

            // The row notifies us ASYNCHRONOUSLY after a remove/move: rebuildRows() deletes the
            // row, so we must not re-enter it from inside the row's own click handler. The
            // SafePointer guards against the panel being torn down (setEdit) before the async
            // fires.
            Component::SafePointer<InsertPanel> safeThis (this);

            auto notifyAsync = [safeThis]
            {
                MessageManager::callAsync ([safeThis]
                {
                    if (safeThis != nullptr && safeThis->onChanged)
                        safeThis->onChanged();
                });
            };

            auto* row = rows.add (new InsertRow (*plugin,
                notifyAsync,
                [safeThis, notifyAsync] (te::Plugin& p)
                {
                    if (safeThis != nullptr && safeThis->moveInsert (p, -1))
                        notifyAsync();
                },
                [safeThis, notifyAsync] (te::Plugin& p)
                {
                    if (safeThis != nullptr && safeThis->moveInsert (p, +1))
                        notifyAsync();
                }));

            // Reflect chain position so the ends can't move past the boundary.
            row->setMovePermitted (i > 0, i < inserts.size() - 1);
            addAndMakeVisible (row);
        }

        resized();
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colour (ForgeLookAndFeel::shellBg));
        g.fillRect (getLocalBounds());
    }

    void resized() override
    {
        auto r = getLocalBounds();

        for (auto* row : rows)
            row->setBounds (r.removeFromTop (rowH));

        addButton.setBounds (r.removeFromTop (rowH).reduced (0, 1));
    }

    static constexpr int rowH = 16;

private:
    //==========================================================================
    /*  One insert row, left to right: a bypass dot · ▲ up · ▼ down · the name button (open) ·
        an "x" (remove). The Plugin is held by reference; the row is owned by InsertPanel which
        is rebuilt whenever the insert set changes, so a row never outlives its plugin between
        rebuilds. The move callbacks ask the panel to reorder this plugin in the engine chain;
        the panel notifies onChanged() afterwards (async) so the row set is rebuilt cleanly.

        Bypassed state (te::Plugin::isEnabled() == false) is reflected by dimming the name and
        striking it through — no fabricated colours, all from ForgeLookAndFeel.                  */
    class InsertRow : public Component
    {
    public:
        InsertRow (te::Plugin& p,
                   std::function<void()> removedCb,
                   std::function<void (te::Plugin&)> moveUpCb,
                   std::function<void (te::Plugin&)> moveDownCb)
            : plugin (p),
              onChanged (std::move (removedCb)),
              onMoveUp (std::move (moveUpCb)),
              onMoveDown (std::move (moveDownCb))
        {
            // Bypass toggle: a compact dot that lights amber when the plugin is ACTIVE and dims
            // to secondary text when bypassed. canBeDisabled() gates plugins that refuse it.
            bypassButton.setButtonText ("o");
            bypassButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
            bypassButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
            bypassButton.setConnectedEdges (Button::ConnectedOnRight);
            bypassButton.setEnabled (plugin.canBeDisabled());
            bypassButton.onClick = [this]
            {
                plugin.setEnabled (! plugin.isEnabled());
                refreshBypassLook();
            };
            addAndMakeVisible (bypassButton);

            // Reorder buttons. canBeMoved() guards plugins that must stay put.
            const bool movable = plugin.canBeMoved();

            upButton.setButtonText (String::charToString ((juce_wchar) 0x25b2));   // ▲
            upButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
            upButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
            upButton.setTooltip ("Move " + plugin.getName() + " earlier in the chain");
            upButton.setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight);
            upButton.setEnabled (movable);
            upButton.onClick = [this] { if (onMoveUp) onMoveUp (plugin); };
            addAndMakeVisible (upButton);

            downButton.setButtonText (String::charToString ((juce_wchar) 0x25bc)); // ▼
            downButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
            downButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
            downButton.setTooltip ("Move " + plugin.getName() + " later in the chain");
            downButton.setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight);
            downButton.setEnabled (movable);
            downButton.onClick = [this] { if (onMoveDown) onMoveDown (plugin); };
            addAndMakeVisible (downButton);

            openButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::panelBg));
            openButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
            openButton.setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight);
            openButton.onClick = [this] { PluginWindow::show (plugin); };
            addAndMakeVisible (openButton);

            removeButton.setButtonText ("x");
            removeButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
            removeButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
            removeButton.setTooltip ("Remove " + plugin.getName());
            removeButton.setConnectedEdges (Button::ConnectedOnLeft);
            removeButton.onClick = [this] { remove(); };
            addAndMakeVisible (removeButton);

            refreshBypassLook();
        }

        /** Enables/disables the reorder buttons to reflect this insert's position in the chain
            (the topmost insert can't move up; the bottom-most can't move down). */
        void setMovePermitted (bool canMoveUp, bool canMoveDown)
        {
            if (plugin.canBeMoved())
            {
                upButton.setEnabled (canMoveUp);
                downButton.setEnabled (canMoveDown);
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                remove();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            bypassButton.setBounds (r.removeFromLeft (12));
            upButton.setBounds     (r.removeFromLeft (11));
            downButton.setBounds   (r.removeFromLeft (11));
            removeButton.setBounds (r.removeFromRight (12));
            openButton.setBounds (r);
            nameBounds = openButton.getBounds();
        }

        /** A strike-through line across the name when the insert is bypassed, so a glance reads
            "this insert is off" even at strip width. Drawn over the child button. */
        void paintOverChildren (Graphics& g) override
        {
            if (! bypassed)
                return;

            const float midY = (float) nameBounds.getCentreY();
            g.setColour (Colour (ForgeLookAndFeel::textSec));
            g.fillRect ((float) nameBounds.getX() + 3.0f, midY,
                        (float) nameBounds.getWidth() - 6.0f, 1.0f);
        }

    private:
        void refreshBypassLook()
        {
            bypassed = ! plugin.isEnabled();

            // Bypass dot: amber when active, dimmed when bypassed.
            bypassButton.setColour (TextButton::textColourOffId,
                                    bypassed ? Colour (ForgeLookAndFeel::textSec)
                                             : Colour (ForgeLookAndFeel::accent));
            bypassButton.setTooltip ((bypassed ? "Enable " : "Bypass ") + plugin.getName());

            // Name reads dimmed when bypassed (the strikethrough is drawn in paintOverChildren).
            openButton.setButtonText (plugin.getName());
            openButton.setColour (TextButton::textColourOffId,
                                  bypassed ? Colour (ForgeLookAndFeel::textSec)
                                           : Colour (ForgeLookAndFeel::textPrim));
            openButton.setTooltip ("Open " + plugin.getName() + "  (right-click to remove)");
            repaint();
        }

        void remove()
        {
            // removePlugin() drops the engine plugin (our `plugin` ref dangles after this), then
            // onChanged() schedules an ASYNC panel rebuild — so this row is not deleted from
            // inside its own click handler. We touch no members after onChanged().
            PluginHost::removePlugin (plugin);
            if (onChanged)
                onChanged();
        }

        te::Plugin& plugin;
        std::function<void()> onChanged;
        std::function<void (te::Plugin&)> onMoveUp, onMoveDown;
        TextButton bypassButton, upButton, downButton, openButton, removeButton;
        Rectangle<int> nameBounds;
        bool bypassed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertRow)
    };

    /*  Reorders one insert within the track's plugin chain by re-inserting its Plugin::Ptr at
        the chain position of its target neighbour. direction < 0 moves it EARLIER (swap with the
        previous insert), direction > 0 moves it LATER (swap with the next insert).

        We work in terms of the user-insert list (getTrackInserts), then map the chosen neighbour
        to its FULL-chain index for te::PluginList::insertPlugin. insertPlugin captures the target
        sibling's tree position before detaching the moving plugin and re-adds it there, so:
          - earlier: target = chain index of the previous insert  -> lands before it;
          - later:   target = chain index of the next insert      -> lands after it
                     (the detach shifts the sibling down one, which is exactly what we want).
        The volume&pan + level-meter tail is never a neighbour here (it's filtered out of the
        insert list and the end buttons are disabled), so the tail always stays last. Returns
        true if a move was actually performed.                                                   */
    bool moveInsert (te::Plugin& plugin, int direction)
    {
        if (direction == 0 || ! plugin.canBeMoved())
            return false;

        const auto inserts = PluginHost::getTrackInserts (track);
        const int here = inserts.indexOf (&plugin);
        if (here < 0)
            return false;

        const int neighbourPos = here + (direction < 0 ? -1 : +1);
        if (neighbourPos < 0 || neighbourPos >= inserts.size())
            return false;   // already at an end; nothing to swap with

        auto* neighbour = inserts[neighbourPos];
        if (neighbour == nullptr)
            return false;

        const int targetChainIndex = track.pluginList.indexOf (neighbour);
        if (targetChainIndex < 0)
            return false;

        // Hold a Ptr so the plugin survives the detach-and-reinsert inside insertPlugin.
        te::Plugin::Ptr held (&plugin);
        track.pluginList.insertPlugin (held, targetChainIndex, nullptr);
        return true;
    }

    void showAddMenu()
    {
        auto names = PluginHost::getAvailablePluginNames (track.edit.engine);

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

        Component::SafePointer<InsertPanel> safeThis (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addButton),
                            [safeThis, names] (int result)
                            {
                                if (safeThis == nullptr || result <= 0 || result > names.size())
                                    return;

                                if (PluginHost::addPluginToTrack (safeThis->track, names[result - 1]) == nullptr)
                                    FORGE_LOG_WARN ("Failed to add plugin '" + names[result - 1] + "' to track '" + safeThis->track.getName() + "'");

                                if (safeThis->onChanged)
                                    safeThis->onChanged();
                            });
    }

    te::AudioTrack& track;
    std::function<void()> onChanged;
    TextButton addButton;
    OwnedArray<InsertRow> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertPanel)
};

//==============================================================================
/*  SendControls — a compact row of aux-send knobs (one per aux bus) for a single track. Each
    knob drives the send level (dB) from this track to that bus through the ProjectSession aux
    seam via the injected getLevel / setLevel callbacks — SendControls itself makes NO engine
    calls, so it stays inside MixerView's "no raw te::" rule. Turning a knob up for the first
    time provisions the bus lazily (the seam creates the aux-return track + AuxSendPlugin); the
    setLevel callback owner (MixerView) then flips the matching return strip from placeholder to
    live in place — the row's own knob is never destroyed mid-drag.

    Present only on real track strips; MixerView passes null callbacks (and so omits the row)
    on the aux-return strips and whenever no ProjectSession is bound.                          */
class SendControls : public Component
{
public:
    SendControls (int trackIndex,
                  int busCount,
                  std::function<float (int, int)> getLevel,
                  std::function<void (int, int, float)> setLevel)
        : trackIdx (trackIndex),
          getLevelFn (std::move (getLevel)),
          setLevelFn (std::move (setLevel))
    {
        for (int b = 0; b < busCount; ++b)
        {
            const int busIdx = b;

            auto* knob = knobs.add (new Slider());
            knob->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            knob->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            knob->setRange (kMinSendDb, kMaxSendDb, 0.1);
            knob->setDoubleClickReturnValue (true, kMinSendDb);   // double-click -> no send
            knob->setColour (Slider::thumbColourId,             Colour (ForgeLookAndFeel::accent));
            knob->setColour (Slider::rotarySliderFillColourId,  Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
            knob->setColour (Slider::rotarySliderOutlineColourId, Colour (ForgeLookAndFeel::hairline));
            knob->setTooltip ("Send to Return " + busLetter (busIdx));

            if (getLevelFn)
                knob->setValue (jlimit (kMinSendDb, kMaxSendDb, (double) getLevelFn (trackIdx, busIdx)),
                                dontSendNotification);

            knob->onValueChange = [this, busIdx, knob]
            {
                if (setLevelFn)
                    setLevelFn (trackIdx, busIdx, (float) knob->getValue());
            };
            addAndMakeVisible (knob);

            auto* lab = labels.add (new Label());
            lab->setText (busLetter (busIdx), dontSendNotification);
            lab->setJustificationType (Justification::centred);
            lab->setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
            lab->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (lab);
        }
    }

    /** Fixed height this row wants (a knob + its bus letter). */
    static constexpr int prefHeight = 46;

    void paint (Graphics& g) override
    {
        g.setColour (Colour (ForgeLookAndFeel::shellBg));
        g.fillRect (getLocalBounds());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = knobs.size();
        if (n == 0)
            return;

        const int cellW = r.getWidth() / n;
        constexpr int labelH = 12;

        for (int i = 0; i < n; ++i)
        {
            auto cell = r.removeFromLeft (i == n - 1 ? r.getWidth() : cellW);
            labels[i]->setBounds (cell.removeFromBottom (labelH));
            knobs[i]->setBounds (cell.reduced (2, 1));
        }
    }

private:
    static juce::String busLetter (int b) { return String::charToString ((juce_wchar) ('A' + b)); }

    int trackIdx = 0;
    std::function<float (int, int)>        getLevelFn;
    std::function<void (int, int, float)>  setLevelFn;
    OwnedArray<Slider> knobs;
    OwnedArray<Label>  labels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendControls)
};

//==============================================================================
/*  ChannelStrip — one vertical strip for a single audio track. Owns its own controls and
    pushes value changes straight to the engine. Holds the track by reference; MixerView
    rebuilds the whole OwnedArray whenever the Edit or its track list changes, so a strip
    never outlives its track.

    Layout, top to bottom: colour swatch · name · pan · sends · insert panel · [meter | fader] · M/S.
    The sends row is present only when the aux seam is bound (MixerView passes live send
    callbacks); otherwise it is omitted and the strip lays out exactly as before.               */
class MixerView::ChannelStrip : public Component
{
public:
    ChannelStrip (te::AudioTrack& t,
                  int trackIndex,
                  std::function<void()> insertsChangedCb,
                  std::function<float (int, int)> sendGet,
                  std::function<void (int, int, float)> sendSet)
        : track (t), onInsertsChanged (std::move (insertsChangedCb))
    {
        // --- Name (top) -------------------------------------------------------------------
        nameLabel.setText (track.getName(), dontSendNotification);
        nameLabel.setJustificationType (Justification::centred);
        nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textPrim));
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (nameLabel);

        // --- Pan (rotary, -1..+1) ---------------------------------------------------------
        pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        pan.setRange (kMinPan, kMaxPan, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);     // double-click -> centre
        pan.setColour (Slider::thumbColourId,           Colour (ForgeLookAndFeel::accent));
        pan.setColour (Slider::rotarySliderFillColourId, Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        pan.setColour (Slider::rotarySliderOutlineColourId, Colour (ForgeLookAndFeel::hairline));
        pan.setValue (EngineHelpers::getTrackPan (track), dontSendNotification);
        pan.onValueChange = [this] { EngineHelpers::setTrackPan (track, (float) pan.getValue()); };
        pan.onDragStart   = [this] { panDragging = true; };
        pan.onDragEnd     = [this] { panDragging = false; };
        addAndMakeVisible (pan);

        // --- Aux sends (only when the aux seam is bound) ----------------------------------
        if (sendGet && sendSet)
        {
            sends = std::make_unique<SendControls> (trackIndex, MixerLayout::auxBusCount,
                                                    std::move (sendGet), std::move (sendSet));
            addAndMakeVisible (*sends);
        }

        // --- Insert panel -----------------------------------------------------------------
        insertPanel = std::make_unique<InsertPanel> (track, [this]
        {
            // Re-pull our own rows immediately, then ask the view to re-flow heights.
            if (insertPanel != nullptr)
                insertPanel->rebuildRows();
            resized();
            if (onInsertsChanged)
                onInsertsChanged();
        });
        addAndMakeVisible (*insertPanel);

        // --- Peak meter -------------------------------------------------------------------
        if (auto* lm = track.getLevelMeterPlugin())
            meter.attach (&lm->measurer);
        addAndMakeVisible (meter);

        // --- Volume fader (vertical, dB) --------------------------------------------------
        styleDbFader (fader);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, MixerLayout::stripW - 8, 16);
        fader.setValue (EngineHelpers::getTrackVolumeDb (track), dontSendNotification);
        fader.onValueChange = [this] { EngineHelpers::setTrackVolumeDb (track, (float) fader.getValue()); };
        fader.onDragStart   = [this] { faderDragging = true; };
        fader.onDragEnd     = [this] { faderDragging = false; };
        addAndMakeVisible (fader);

        // --- M / S toggles ----------------------------------------------------------------
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

        muteButton.onClick  = [this] { track.setMute (muteButton.getToggleState()); };
        soloButton.onClick  = [this] { track.setSolo (soloButton.getToggleState()); };

        muteButton.setToggleState (track.isMuted (false), dontSendNotification);
        soloButton.setToggleState (track.isSolo  (false), dontSendNotification);
    }

    /** Total height this strip wants, given its current insert count. The MixerView gives
        every strip the tallest requested height so the row stays aligned. */
    int getDesiredHeight() const
    {
        const int inserts = insertPanel != nullptr ? insertPanel->getInsertCount() : 0;
        const int insertPanelH = (inserts + 1) * InsertPanel::rowH;   // rows + the "+" button
        const int sendsH = sends != nullptr ? SendControls::prefHeight : 0;
        return swatchH + nameH + panH + sendsH + insertPanelH + faderRegionH + controlsH;
    }

    PeakMeter& getMeter() { return meter; }

    /** Engine→widget sync for one 28 Hz tick: pulls fader/pan/M/S/name from the live track so a
        change made on another surface (MIDI-learn hardware, automation) appears here without a
        re-select. Skips any control mid-interaction; all writes use dontSendNotification, so no
        engine write-back can loop. Slider::setValue and Button::setToggleState self-no-op on
        unchanged values and the name is edge-compared, so a steady-state tick repaints nothing.
        Hot path — never logs. */
    void syncControls()
    {
        if (! faderDragging && ! fader.hasKeyboardFocus (true))
            fader.setValue (EngineHelpers::getTrackVolumeDb (track), dontSendNotification);

        if (! panDragging && ! pan.hasKeyboardFocus (true))
            pan.setValue (EngineHelpers::getTrackPan (track), dontSendNotification);

        if (! muteButton.isMouseButtonDown())
            muteButton.setToggleState (track.isMuted (false), dontSendNotification);

        if (! soloButton.isMouseButtonDown())
            soloButton.setToggleState (track.isSolo (false), dontSendNotification);

        const auto liveName = track.getName();
        if (nameLabel.getText() != liveName)
            nameLabel.setText (liveName, dontSendNotification);
    }

    /** Fader value (dB) currently shown — selftest seam (read via MixerView::getStripFaderDb). */
    double getFaderDb() const { return fader.getValue(); }

    /** Mute-button state currently shown — selftest seam (read via MixerView::getStripMuted). */
    bool isMuteShown() const { return muteButton.getToggleState(); }

    void paint (Graphics& g) override
    {
        auto bounds = getLocalBounds();

        g.setColour (Colour (ForgeLookAndFeel::panelBg));
        g.fillRect (bounds);

        // Colour swatch: a thin band across the very top of the strip, matching the track.
        g.setColour (track.getColour());
        g.fillRect (bounds.removeFromTop (swatchH));

        // Right-edge hairline separates adjacent strips.
        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (swatchH);                 // colour swatch band (painted, not a child)

        nameLabel.setBounds (r.removeFromTop (nameH).reduced (3, 1));
        pan.setBounds      (r.removeFromTop (panH).reduced (8, 2));

        if (sends != nullptr)
            sends->setBounds (r.removeFromTop (SendControls::prefHeight).reduced (4, 2));

        if (insertPanel != nullptr)
        {
            const int inserts = insertPanel->getInsertCount();
            const int panelH = (inserts + 1) * InsertPanel::rowH;
            insertPanel->setBounds (r.removeFromTop (panelH).reduced (4, 0));
        }

        auto controls = r.removeFromBottom (controlsH).reduced (6, 4);
        const int bw = jmax (16, (controls.getWidth() - 4) / 2);
        muteButton.setBounds (controls.removeFromLeft (bw));
        controls.removeFromLeft (4);
        soloButton.setBounds (controls.removeFromLeft (bw));

        // Remaining middle space: meter on the left, fader filling the rest.
        auto faderRegion = r.reduced (4, 4);
        meter.setBounds (faderRegion.removeFromLeft (meterW));
        faderRegion.removeFromLeft (3);
        fader.setBounds (faderRegion);
    }

private:
    static constexpr int swatchH      = 5;
    static constexpr int nameH        = 20;
    static constexpr int panH         = 52;
    static constexpr int controlsH    = 26;
    static constexpr int faderRegionH = 150;   // baseline fader/meter height used for sizing
    static constexpr int meterW       = 8;

    te::AudioTrack& track;
    std::function<void()> onInsertsChanged;

    // Set for the duration of a mouse drag on the matching slider — syncControls() skips a
    // control the user is holding, so the 28 Hz engine→widget sync never fights a gesture.
    bool faderDragging = false, panDragging = false;

    Label nameLabel;
    Slider fader, pan;
    TextButton muteButton { "M" }, soloButton { "S" };
    std::unique_ptr<SendControls> sends;   // aux-send knobs; null when no aux seam is bound
    std::unique_ptr<InsertPanel> insertPanel;
    PeakMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

//==============================================================================
/*  MasterStrip — the fixed right-hand strip driving the edit's master volume plugin
    (edit.getMasterVolumePlugin(), a VolumeAndPanPlugin with getVolumeDb/setVolumeDb).

    Its meter reads the engine's master OUTPUT measurer, EditPlaybackContext::masterLevels —
    the same post-fader master level the engine surfaces to control surfaces. We deliberately do
    NOT insert a te::LevelMeterPlugin into the master plugin list: that would mutate the
    persisted Edit (write a spurious plugin), dirty a clean project and push an undo step just
    for opening the mixer. It would also meter PRE master-fader, since the master plugin list is
    rendered before the master volume plugin. masterLevels avoids both problems — it lives on the
    playback context, not the Edit model, and is measured after the master fader.

    The playback context (and its masterLevels) is created/destroyed as the transport allocates
    and frees its graph, so the meter re-binds to the current context every poll (see pollMeter)
    rather than caching a pointer.                                                              */
class MixerView::MasterStrip : public Component
{
public:
    explicit MasterStrip (te::Edit& e)
        : edit (e)
    {
        nameLabel.setText ("MASTER", dontSendNotification);
        nameLabel.setJustificationType (Justification::centred);
        nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::accent));
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);

        // The meter binds to the playback context's master output measurer on each poll
        // (pollMeter) — nothing to provision here, and no mutation of the Edit.
        addAndMakeVisible (meter);

        styleDbFader (fader);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, MixerLayout::masterW - 8, 16);

        if (auto mv = edit.getMasterVolumePlugin())
            fader.setValue (mv->getVolumeDb(), dontSendNotification);
        else
            FORGE_LOG_ERROR ("Master volume plugin not found — master fader will not control output level");

        fader.onValueChange = [this]
        {
            if (auto mv = edit.getMasterVolumePlugin())
                mv->setVolumeDb (jlimit (-100.0f, 12.0f, (float) fader.getValue()));
        };
        fader.onDragStart = [this] { faderDragging = true; };
        fader.onDragEnd   = [this] { faderDragging = false; };
        addAndMakeVisible (fader);
    }

    /*  Re-points the meter at the CURRENT playback context's master output measurer and pulls a
        sample. masterLevels is post master-fader and reading it mutates nothing in the Edit; the
        context comes and goes with the transport graph, so we re-bind every poll. PeakMeter::
        attach() no-ops when the source is unchanged and detaches (empties the bar) on nullptr —
        and its WeakReference source makes a freed context's dead measurer safe to rebind over
        (the W03 sync-gate teardown spun forever on the old raw-pointer detach).

        Also live-syncs the fader from the master volume plugin (guarded: never mid-gesture, and
        with dontSendNotification so nothing writes back) so an external change — MIDI-learn,
        automation — shows without a rebuild. A null plugin is skipped silently: the ctor already
        logged it once, and this poll is a hot path that must never log.                        */
    void pollMeter (float secondsSinceLast)
    {
        te::LevelMeasurer* src = nullptr;
        if (auto* ctx = edit.getTransport().getCurrentPlaybackContext())
            src = &ctx->masterLevels;

        meter.attach (src);
        meter.poll (secondsSinceLast);

        if (! faderDragging && ! fader.hasKeyboardFocus (true))
            if (auto mv = edit.getMasterVolumePlugin())
                fader.setValue (mv->getVolumeDb(), dontSendNotification);
    }

    void paint (Graphics& g) override
    {
        auto bounds = getLocalBounds();

        g.setColour (Colour (ForgeLookAndFeel::panelBg));
        g.fillRect (bounds);

        // Accent band across the top so the master reads as distinct from track strips.
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.fillRect (bounds.removeFromTop (swatchH));

        // Left-edge hairline separates the master from the scrolling track row.
        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (0, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (swatchH);

        nameLabel.setBounds (r.removeFromTop (nameH).reduced (3, 1));

        auto faderRegion = r.reduced (6, 6);
        meter.setBounds (faderRegion.removeFromLeft (meterW));
        faderRegion.removeFromLeft (3);
        fader.setBounds (faderRegion);
    }

private:
    static constexpr int swatchH = 5;
    static constexpr int nameH   = 20;
    static constexpr int meterW  = 10;

    te::Edit& edit;

    // Set for the duration of a fader drag — pollMeter's fader sync skips a held control, so the
    // 28 Hz engine→widget sync never fights a gesture.
    bool faderDragging = false;

    Label nameLabel;
    Slider fader;
    PeakMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterStrip)
};

//==============================================================================
/*  ReturnStrip — one aux-return ("bus") strip, pinned to the right of the track row (before the
    master). A bus is backed by a plain te::AudioTrack that hosts an AuxReturnPlugin(busIndex);
    a track that sends to the bus carries an AuxSendPlugin(busIndex). ReturnStrip owns NO engine
    state — it re-queries the ProjectSession aux seam every refresh and makes only display
    reads / vol-pan writes through EngineHelpers (never a raw te:: bus/plugin call).

    Two states, toggled by whether the seam has provisioned the bus:
      - placeholder (no return track yet): just the bus name + an "Enable" button that calls
        session->ensureAuxBus(busIndex). Rendering a placeholder means the returns are always
        visible (discoverable + screenshot-friendly) WITHOUT mutating a clean Edit on open.
      - live (return track exists): name · insert panel (drop a reverb/delay here) ·
        [meter | return fader] · M/S — i.e. a real FX-return channel.

    The placeholder→live flip happens IN PLACE (refresh()), so turning up a send knob on some
    other strip never destroys the strip being dragged.                                          */
class MixerView::ReturnStrip : public Component
{
public:
    ReturnStrip (ProjectSession* s, int busIdx)
        : session (s), busIndex (busIdx)
    {
        nameLabel.setJustificationType (Justification::centred);
        nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::accent));
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (nameLabel);

        enableButton.setButtonText ("+ Enable");
        enableButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::raisedBg));
        enableButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::accent));
        enableButton.setTooltip ("Create this aux-return bus");
        enableButton.onClick = [this]
        {
            if (session != nullptr)
            {
                session->ensureAuxBus (busIndex);   // structural; seam logs on failure
                refresh();
            }
        };
        addAndMakeVisible (enableButton);

        styleDbFader (fader);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, MixerLayout::returnW - 8, 16);
        fader.onValueChange = [this]
        {
            if (returnTrack != nullptr)
                EngineHelpers::setTrackVolumeDb (*returnTrack, (float) fader.getValue());
        };
        fader.onDragStart = [this] { faderDragging = true; };
        fader.onDragEnd   = [this] { faderDragging = false; };
        addAndMakeVisible (fader);

        addAndMakeVisible (meter);

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
        muteButton.onClick = [this] { if (returnTrack != nullptr) returnTrack->setMute (muteButton.getToggleState()); };
        soloButton.onClick = [this] { if (returnTrack != nullptr) returnTrack->setSolo (soloButton.getToggleState()); };

        refresh();
    }

    /** Re-reads the seam and rebuilds this strip's children for the current bus state. Cheap;
        called on construction, on the "Enable" click, and (async) when a send first provisions
        the bus. Message-thread only. */
    void refresh()
    {
        returnTrack = (session != nullptr) ? session->getAuxReturnTrack (busIndex) : nullptr;
        const bool live = returnTrack != nullptr;

        juce::String nm = (session != nullptr) ? session->getAuxBusName (busIndex) : juce::String();
        if (nm.isEmpty())
            nm = defaultBusName();
        nameLabel.setText (nm, dontSendNotification);

        enableButton.setVisible (! live);
        fader.setVisible (live);
        muteButton.setVisible (live);
        soloButton.setVisible (live);

        if (live)
        {
            if (auto* lm = returnTrack->getLevelMeterPlugin())
                meter.attach (&lm->measurer);
            else
                meter.detach();

            fader.setValue (EngineHelpers::getTrackVolumeDb (*returnTrack), dontSendNotification);
            muteButton.setToggleState (returnTrack->isMuted (false), dontSendNotification);
            soloButton.setToggleState (returnTrack->isSolo  (false), dontSendNotification);

            insertPanel = std::make_unique<InsertPanel> (*returnTrack, [this]
            {
                if (insertPanel != nullptr)
                    insertPanel->rebuildRows();
                resized();
            });
            addAndMakeVisible (*insertPanel);
        }
        else
        {
            meter.detach();
            insertPanel.reset();
        }

        resized();
        repaint();
    }

    /** Async self-refresh — used by MixerView after a send lazily provisions this bus, so the
        flip to "live" never happens inside the dragged send knob's own callback. */
    void refreshAsync()
    {
        Component::SafePointer<ReturnStrip> safeThis (this);
        MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->refresh(); });
    }

    bool isLive() const noexcept { return returnTrack != nullptr; }

    /** Pull a meter sample and live-sync fader/M/S from the return track (no-op until the bus is
        live). Guards mirror ChannelStrip::syncControls; called on the MixerView timer — never logs. */
    void pollMeter (float dt)
    {
        // Re-resolve through the seam BEFORE any dereference (the SessionView R1 rule): the cached
        // track can be deleted out from under us (e.g. the return lane's Delete Track in Arrange),
        // and MixerView's structural guard deliberately excludes aux tracks from its count — so a
        // stale pointer here would be a deterministic 28 Hz use-after-free (W03 QC blocker).
        auto* live = (session != nullptr) ? session->getAuxReturnTrack (busIndex) : nullptr;
        if (live != returnTrack)
        {
            refresh();   // re-resolves the pointer and flips back to placeholder if the bus died
            return;
        }

        if (returnTrack == nullptr)
            return;

        meter.poll (dt);

        if (! faderDragging && ! fader.hasKeyboardFocus (true))
            fader.setValue (EngineHelpers::getTrackVolumeDb (*returnTrack), dontSendNotification);

        if (! muteButton.isMouseButtonDown())
            muteButton.setToggleState (returnTrack->isMuted (false), dontSendNotification);

        if (! soloButton.isMouseButtonDown())
            soloButton.setToggleState (returnTrack->isSolo (false), dontSendNotification);
    }

    void paint (Graphics& g) override
    {
        auto bounds = getLocalBounds();

        g.setColour (Colour (ForgeLookAndFeel::panelBg));
        g.fillRect (bounds);

        // Accent band on top; dimmed while the bus is only a placeholder.
        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (isLive() ? 1.0f : 0.4f));
        g.fillRect (bounds.removeFromTop (swatchH));

        // Left-edge hairline separates the returns from the scrolling track row / each other.
        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (0, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (swatchH);

        nameLabel.setBounds (r.removeFromTop (nameH).reduced (3, 1));

        if (! isLive())
        {
            // Placeholder: a single "Enable" button near the top; the rest stays empty.
            enableButton.setBounds (r.removeFromTop (30).reduced (8, 2));
            return;
        }

        auto controls = r.removeFromBottom (controlsH).reduced (6, 4);
        const int bw = jmax (16, (controls.getWidth() - 4) / 2);
        muteButton.setBounds (controls.removeFromLeft (bw));
        controls.removeFromLeft (4);
        soloButton.setBounds (controls.removeFromLeft (bw));

        if (insertPanel != nullptr)
        {
            const int inserts = insertPanel->getInsertCount();
            const int panelH = (inserts + 1) * InsertPanel::rowH;
            insertPanel->setBounds (r.removeFromTop (panelH).reduced (4, 0));
        }

        auto faderRegion = r.reduced (4, 4);
        meter.setBounds (faderRegion.removeFromLeft (meterW));
        faderRegion.removeFromLeft (3);
        fader.setBounds (faderRegion);
    }

private:
    juce::String defaultBusName() const
    {
        return "RETURN " + String::charToString ((juce_wchar) ('A' + busIndex));
    }

    static constexpr int swatchH   = 5;
    static constexpr int nameH     = 20;
    static constexpr int controlsH = 26;
    static constexpr int meterW    = 8;

    ProjectSession* session = nullptr;
    int busIndex = 0;
    te::AudioTrack* returnTrack = nullptr;   // re-resolved each refresh; never cached across one

    // Set for the duration of a fader drag — pollMeter's sync skips a held control, so the
    // 28 Hz engine→widget sync never fights a gesture.
    bool faderDragging = false;

    Label nameLabel;
    TextButton enableButton;
    Slider fader;
    TextButton muteButton { "M" }, soloButton { "S" };
    PeakMeter meter;
    std::unique_ptr<InsertPanel> insertPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReturnStrip)
};

//==============================================================================
MixerView::MixerView()
{
    viewport.setViewedComponent (&stripHolder, false);   // we own stripHolder ourselves
    viewport.setScrollBarsShown (false, true);           // horizontal scroll only
    addAndMakeVisible (viewport);
}

MixerView::~MixerView()
{
    stopTimer();
}

void MixerView::setEdit (te::Edit* e)
{
    edit = e;
    rebuild();

    if (edit != nullptr)
        startTimerHz (28);   // ~28 Hz meter + control live-sync poll — only while an edit is bound
    else
        stopTimer();
}

void MixerView::setSession (ProjectSession* s)
{
    session = s;
    rebuild();   // send knobs + aux-return strips depend on the seam
}

void MixerView::rebuild()
{
    lastLoggedTrackCount = -1;   // re-arm the poll's edge-triggered mismatch WARN

    strips.clear();
    returnStrips.clear();
    master.reset();

    if (edit != nullptr)
    {
        // Aux-send seam hooks handed to each track strip's SendControls. Null when no session is
        // bound, so ChannelStrip then omits the sends row entirely. All bus/plugin structure is
        // created inside the seam — MixerView makes no raw te:: bus calls.
        std::function<float (int, int)> sendGet;
        std::function<void (int, int, float)> sendSet;

        if (session != nullptr)
        {
            sendGet = [this] (int trackIdx, int busIdx) -> float
            {
                return session->getTrackSendLevel (trackIdx, busIdx);
            };

            sendSet = [this] (int trackIdx, int busIdx, float db)
            {
                const bool wasLive = session->getAuxReturnTrack (busIdx) != nullptr;
                session->setTrackSendLevel (trackIdx, busIdx, db);   // provisions bus + send lazily

                // If this send just created the bus, flip its placeholder return strip to live —
                // in place (async), so the send knob being dragged is never torn down under it.
                if (! wasLive
                    && session->getAuxReturnTrack (busIdx) != nullptr
                    && busIdx >= 0 && busIdx < returnStrips.size())
                    returnStrips[busIdx]->refreshAsync();
            };
        }

        // One channel strip per audio track, EXCLUDING aux-return tracks (they render as
        // ReturnStrips). trackIndex is the absolute index into te::getAudioTracks — the same
        // index the seam's send methods expect.
        auto tracks = te::getAudioTracks (*edit);
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* track = tracks[i];
            if (track == nullptr)
                continue;
            if (session != nullptr && session->isAuxReturnTrack (*track))
                continue;

            auto* strip = strips.add (new ChannelStrip (*track, i, [this] { resized(); }, sendGet, sendSet));
            stripHolder.addAndMakeVisible (strip);
        }

        // Always render the aux-return strips (placeholder until a bus is provisioned).
        for (int b = 0; b < MixerLayout::auxBusCount; ++b)
        {
            auto* r = returnStrips.add (new ReturnStrip (session, b));
            addAndMakeVisible (*r);
        }

        master = std::make_unique<MasterStrip> (*edit);
        addAndMakeVisible (*master);
    }

    resized();
    repaint();
}

void MixerView::timerCallback()
{
    if (edit == nullptr)
        return;

    // Structural guard FIRST (same poll-rebuild model as SessionView): if the live non-aux track
    // count no longer matches our strips, the ChannelStrips hold stale track references — rebuild
    // BEFORE any strip dereferences one (a track added/deleted in Arrange never rebuilds us
    // directly). The count mirrors rebuild()'s own filter exactly.
    int live = 0;
    for (auto* t : te::getAudioTracks (*edit))
        if (t != nullptr && ! (session != nullptr && session->isAuxReturnTrack (*t)))
            ++live;

    if (live != strips.size())
    {
        // Edge-triggered (NOT per-tick): only log when the live count differs from the last value
        // we logged, so a persistent mismatch across ticks emits a single WARN rather than 28/s.
        if (live != lastLoggedTrackCount)
        {
            FORGE_LOG_WARN ("Track count mismatch in mixer poll: " + juce::String (live)
                            + " live vs " + juce::String (strips.size()) + " (rebuilding)");
            lastLoggedTrackCount = live;
        }

        rebuild();
        return;
    }

    constexpr float dt = 1.0f / 28.0f;

    // Meters + engine→widget control sync per strip; the returns and master sync inside their
    // own pollMeter. No logging below this line (hot path).
    for (auto* strip : strips)
    {
        strip->getMeter().poll (dt);
        strip->syncControls();
    }

    for (auto* r : returnStrips)
        r->pollMeter (dt);

    if (master != nullptr)
        master->pollMeter (dt);
}

void MixerView::refreshControls()
{
    timerCallback();
}

void MixerView::resized()
{
    auto area = getLocalBounds();

    // Master strip pinned to the far right (outside the scrolling viewport), if present.
    if (master != nullptr)
        master->setBounds (area.removeFromRight (MixerLayout::masterW));

    // Aux-return strips pinned to the right, just left of the master. Peel from the right so
    // bus B lands right of bus A (final left→right: tracks … | A | B | MASTER). Full-height,
    // like the master — a handful of returns don't need to scroll.
    for (int b = returnStrips.size() - 1; b >= 0; --b)
        returnStrips[b]->setBounds (area.removeFromRight (MixerLayout::returnW));

    viewport.setBounds (area);

    const int n = strips.size();
    const int contentW = jmax (viewport.getMaximumVisibleWidth(),
                               n * MixerLayout::stripW);

    // The row is as tall as the tallest strip (strips differ by insert count); fall back to
    // the viewport height so a strip always fills the visible column.
    int desiredH = viewport.getMaximumVisibleHeight();
    for (auto* strip : strips)
        desiredH = jmax (desiredH, strip->getDesiredHeight());

    stripHolder.setBounds (0, 0, contentW, desiredH);

    int x = 0;
    for (auto* strip : strips)
    {
        strip->setBounds (x, 0, MixerLayout::stripW, desiredH);
        x += MixerLayout::stripW + MixerLayout::stripGap;
    }
}

void MixerView::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::shellBg));

    if (strips.isEmpty() && master == nullptr)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("No tracks — import audio to begin", getLocalBounds(), Justification::centred);
    }
}

int MixerView::getNumStrips() const
{
    return strips.size();
}

double MixerView::getStripFaderDb (int index) const
{
    if (auto* s = strips[index])   // OwnedArray operator[] is range-checked (nullptr out of range)
        return s->getFaderDb();

    return 0.0;
}

bool MixerView::getStripMuted (int index) const
{
    if (auto* s = strips[index])
        return s->isMuteShown();

    return false;
}
