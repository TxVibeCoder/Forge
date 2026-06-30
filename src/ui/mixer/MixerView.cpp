#include "ui/mixer/MixerView.h"
#include "ui/ForgeLookAndFeel.h"
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"
#include "ui/plugins/PluginWindow.h"

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

    // Meter ballistics / scale.
    constexpr float  kMeterMinDb  = -60.0f;   // bottom of the meter
    constexpr float  kMeterMaxDb  =   6.0f;   // top of the meter (matches fader headroom)
    constexpr float  kMeterDecayDbPerSec = 18.0f;   // visual fall-off when the signal drops

    /** Maps a dB level into a 0..1 fill fraction for the meter (clamped). */
    inline float dbToMeterFraction (float db)
    {
        return jlimit (0.0f, 1.0f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
    }
}

//==============================================================================
/*  PeakMeter — a thin vertical level bar driven by a LevelMeasurer (via a Client we register
    on the source measurer). pushLevelDb() is called from the MixerView timer; the bar holds a
    smoothed "current" value that decays toward the live reading so movement looks like a real
    meter rather than a strobe. If no measurer is attached the meter simply draws empty (the
    data hookup is documented as deferred for that source) — it NEVER fabricates a level.

    Reads the louder of the (up to two) measured channels for a single mono-ish bar; this keeps
    the strip narrow while still showing clipping on either side.                              */
class PeakMeter : public Component
{
public:
    PeakMeter() = default;

    ~PeakMeter() override
    {
        detach();
    }

    /** Registers as a Client on `m`'s measurer so getAndClearAudioLevel() returns live peaks. */
    void attach (te::LevelMeasurer* m)
    {
        if (m == measurer)
            return;

        detach();
        measurer = m;

        if (measurer != nullptr)
            measurer->addClient (client);
    }

    void detach()
    {
        if (measurer != nullptr)
        {
            measurer->removeClient (client);
            measurer = nullptr;
        }

        client.reset();
        currentDb = kMeterMinDb;
        repaint();
    }

    bool hasSource() const noexcept { return measurer != nullptr; }

    /** Pull the latest peak off the measurer and apply decay. Called on the MixerView timer.
        `secondsSinceLast` is the timer interval, used for the fall-off rate. */
    void poll (float secondsSinceLast)
    {
        if (measurer == nullptr)
            return;

        // Peak across the active channels (mono bar shows the hotter side).
        float liveDb = kMeterMinDb;
        const int chans = jmax (1, client.getNumChannelsUsed());

        for (int ch = 0; ch < jmin (chans, 2); ++ch)
        {
            const auto pair = client.getAndClearAudioLevel (ch);
            liveDb = jmax (liveDb, pair.dB);
        }

        // Instant attack, timed decay: a quiet/zero reading must not snap the bar to silence.
        if (liveDb >= currentDb)
            currentDb = liveDb;
        else
            currentDb = jmax (liveDb, currentDb - kMeterDecayDbPerSec * secondsSinceLast);

        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (Colour (ForgeLookAndFeel::raisedBg));
        g.fillRect (r);

        const float frac = dbToMeterFraction (currentDb);
        if (frac > 0.0f)
        {
            auto fill = r.removeFromBottom (r.getHeight() * frac);

            // Amber under 0 dBFS-ish, red once we're into the top headroom band (clipping warning).
            const bool hot = currentDb > 0.0f;
            g.setColour (hot ? Colour (ForgeLookAndFeel::recordRed)
                             : Colour (ForgeLookAndFeel::accent));
            g.fillRect (fill);
        }

        // 0 dB tick line so the user can read the headroom point.
        const float zeroFrac = dbToMeterFraction (0.0f);
        const float zeroY = (float) getHeight() * (1.0f - zeroFrac);
        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (0.0f, zeroY, (float) getWidth(), 1.0f);

        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }

private:
    te::LevelMeasurer* measurer = nullptr;
    te::LevelMeasurer::Client client;
    float currentDb = kMeterMinDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakMeter)
};

//==============================================================================
/*  InsertPanel — the list of a track's insert plugins plus a "+" add button. Each existing
    insert is a row button: left-click opens its editor window (PluginWindow::show), right-click
    or the trailing "x" removes it (PluginHost::removePlugin). "+" pops a menu of available
    plugin names (PluginHost::getAvailablePluginNames) and adds the chosen one
    (PluginHost::addPluginToTrack). After any add/remove it calls onChanged() so the owning
    strip rebuilds its rows and the layout re-flows.

    The volume/level-meter plugins that every track carries are filtered out by PluginHost::
    getTrackInserts (per its contract — it returns user inserts only), so they never appear
    here as removable rows.                                                                    */
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

        for (auto* plugin : PluginHost::getTrackInserts (track))
        {
            if (plugin == nullptr)
                continue;

            // The row notifies us ASYNCHRONOUSLY after a remove: rebuildRows() deletes the row,
            // so we must not re-enter it from inside the row's own click handler. The SafePointer
            // guards against the panel being torn down (setEdit) before the async fires.
            Component::SafePointer<InsertPanel> safeThis (this);
            auto* row = rows.add (new InsertRow (*plugin, [safeThis]
            {
                MessageManager::callAsync ([safeThis]
                {
                    if (safeThis != nullptr && safeThis->onChanged)
                        safeThis->onChanged();
                });
            }));
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
    /*  One insert row: a name button (open) + an "x" button (remove). The Plugin is held by
        reference; the row is owned by InsertPanel which is rebuilt whenever the insert set
        changes, so a row never outlives its plugin between rebuilds. */
    class InsertRow : public Component
    {
    public:
        InsertRow (te::Plugin& p, std::function<void()> changedCb)
            : plugin (p), onChanged (std::move (changedCb))
        {
            openButton.setButtonText (plugin.getName());
            openButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::panelBg));
            openButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
            openButton.setTooltip ("Open " + plugin.getName() + "  (right-click to remove)");
            openButton.setConnectedEdges (Button::ConnectedOnRight);
            openButton.onClick = [this] { PluginWindow::show (plugin); };
            addAndMakeVisible (openButton);

            removeButton.setButtonText ("x");
            removeButton.setColour (TextButton::buttonColourId, Colour (ForgeLookAndFeel::raisedBg));
            removeButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
            removeButton.setTooltip ("Remove " + plugin.getName());
            removeButton.setConnectedEdges (Button::ConnectedOnLeft);
            removeButton.onClick = [this] { remove(); };
            addAndMakeVisible (removeButton);
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                remove();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            removeButton.setBounds (r.removeFromRight (14));
            openButton.setBounds (r);
        }

    private:
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
        TextButton openButton, removeButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertRow)
    };

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

                                PluginHost::addPluginToTrack (safeThis->track, names[result - 1]);
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
/*  ChannelStrip — one vertical strip for a single audio track. Owns its own controls and
    pushes value changes straight to the engine. Holds the track by reference; MixerView
    rebuilds the whole OwnedArray whenever the Edit or its track list changes, so a strip
    never outlives its track.

    Layout, top to bottom: colour swatch · name · pan · insert panel · [meter | fader] · M/S. */
class MixerView::ChannelStrip : public Component
{
public:
    ChannelStrip (te::AudioTrack& t, std::function<void()> insertsChangedCb)
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
        addAndMakeVisible (pan);

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
        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, MixerLayout::stripW - 8, 16);
        fader.setRange (kMinDb, kMaxDb, 0.1);
        fader.setNumDecimalPlacesToDisplay (1);
        fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0.0);   // double-click -> unity (0 dB)
        fader.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
        fader.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        fader.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
        fader.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
        fader.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));
        fader.setValue (EngineHelpers::getTrackVolumeDb (track), dontSendNotification);
        fader.onValueChange = [this] { EngineHelpers::setTrackVolumeDb (track, (float) fader.getValue()); };
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
        return swatchH + nameH + panH + insertPanelH + faderRegionH + controlsH;
    }

    PeakMeter& getMeter() { return meter; }

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

    Label nameLabel;
    Slider fader, pan;
    TextButton muteButton { "M" }, soloButton { "S" };
    std::unique_ptr<InsertPanel> insertPanel;
    PeakMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

//==============================================================================
/*  MasterStrip — the fixed right-hand strip driving the edit's master volume plugin
    (edit.getMasterVolumePlugin(), a VolumeAndPanPlugin with getVolumeDb/setVolumeDb). Its
    meter is fed from a LevelMeterPlugin on the master plugin list if one exists; if the master
    chain has no level meter, the meter is drawn empty (data hookup deferred — never faked).   */
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

        // Master meter source: a LevelMeterPlugin in the master plugin list, if present.
        if (auto* lm = edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>())
            meter.attach (&lm->measurer);
        addAndMakeVisible (meter);

        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, MixerLayout::masterW - 8, 16);
        fader.setRange (kMinDb, kMaxDb, 0.1);
        fader.setNumDecimalPlacesToDisplay (1);
        fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
        fader.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        fader.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
        fader.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
        fader.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));

        if (auto mv = edit.getMasterVolumePlugin())
            fader.setValue (mv->getVolumeDb(), dontSendNotification);

        fader.onValueChange = [this]
        {
            if (auto mv = edit.getMasterVolumePlugin())
                mv->setVolumeDb (jlimit (-100.0f, 12.0f, (float) fader.getValue()));
        };
        addAndMakeVisible (fader);
    }

    PeakMeter& getMeter() { return meter; }

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
    Label nameLabel;
    Slider fader;
    PeakMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterStrip)
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
        startTimerHz (28);   // ~28 Hz meter polling — only while an edit is bound
    else
        stopTimer();
}

void MixerView::rebuild()
{
    strips.clear();
    master.reset();

    if (edit != nullptr)
    {
        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* strip = strips.add (new ChannelStrip (*track, [this] { resized(); }));
            stripHolder.addAndMakeVisible (strip);
        }

        master = std::make_unique<MasterStrip> (*edit);
        addAndMakeVisible (*master);
    }

    resized();
    repaint();
}

void MixerView::timerCallback()
{
    constexpr float dt = 1.0f / 28.0f;

    for (auto* strip : strips)
        strip->getMeter().poll (dt);

    if (master != nullptr)
        master->getMeter().poll (dt);
}

void MixerView::resized()
{
    auto area = getLocalBounds();

    // Master strip pinned to the right (outside the scrolling viewport), if present.
    if (master != nullptr)
        master->setBounds (area.removeFromRight (MixerLayout::masterW));

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
