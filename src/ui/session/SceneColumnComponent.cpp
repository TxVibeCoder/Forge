#include "ui/session/SceneColumnComponent.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

//==============================================================================
/*  SceneRow — one scene-launch row: a ▶ launch button on the left and the scene name (or its
    1-based row number) filling the rest. The row holds its scene index, a pushed-in
    SceneLaunchState, and a pushed-in beat-pulse alpha only — never a te::Scene*. The launch
    button fires onLaunched(); a right-click (or the playing/queued affordance) fires onStopped();
    both are wired up to the column's seams by SceneColumnComponent, and both are named in the
    row's tooltip (the right-click stop is otherwise undiscoverable).

    State render (mirrors the slot pad vocabulary, §d + the W04a semantic accents, named
    ForgeLookAndFeel colours only — playing/queued speak the play-green family, never amber;
    hover lifts the base fill to neutral raisedBg — chrome, not a semantic accent):
      - idle    → panelBg row (raisedBg while hovered), textPrim name, neutral ▶
      - queued  → 2px playGreenDim outline (about to fire), playGreenDim ▶
      - playing → playGreen-tinted fill + 2px playGreen outline, dark name, playGreen ▶
    The queued/playing outline beat-pulses via the pushed-in pulse alpha, matching the pads.
*/
class SceneColumnComponent::SceneRow : public Component,
                                       public SettableTooltipClient
{
public:
    SceneRow (int idx, const String& displayName)
        : sceneIndex (idx), name (displayName)
    {
        // Idle-hover affordance: repaint on mouse enter/exit ONLY (no polling) so paint() can
        // lift the base fill to raisedBg while the pointer is over the row.
        setRepaintsOnMouseActivity (true);

        // Name BOTH interactions in one tooltip — the row (SettableTooltipClient) covers the
        // name area and the ▶ button carries the same text.
        const String tip = "Launch " + name + String::fromUTF8 (" \xe2\x80\x94 right-click stops the row");
        setTooltip (tip);

        // ▶ launch button. Transparent fill so the row's own paint() shows through; the glyph
        // recolours with state via refreshLaunchLook().
        launchButton.setButtonText (String::charToString ((juce_wchar) 0x25b6));   // ▶
        launchButton.setColour (TextButton::buttonColourId, Colours::transparentBlack);
        launchButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
        launchButton.setTooltip (tip);
        launchButton.onClick = [this] { if (onLaunched) onLaunched (sceneIndex); };
        addAndMakeVisible (launchButton);

        // Route the button's mouse events through the row too: setRepaintsOnMouseActivity only
        // sees the ROW's own enter/exit, so without this the hover fill drops out (and never
        // re-fires) while the pointer crosses the ▶ button — a visible flicker over the row's
        // primary click target. This also makes right-click-stop work over the button, which
        // the shared tooltip already promises.
        launchButton.addMouseListener (this, false);

        refreshLaunchLook();
    }

    int getSceneIndex() const noexcept { return sceneIndex; }

    void setState (SceneLaunchState s)
    {
        if (state != s)
        {
            state = s;
            refreshLaunchLook();
            repaint();
        }
    }

    /** Pushed every poll tick while this row is animated (queued / playing), and parked at the
        negative no-pulse sentinel otherwise — the change gate below (copied from
        ClipSlotComponent::setPulseAlpha) is what keeps static rows repaint-free (§e): a row
        repaints per tick ONLY while its pulse value is actually moving. */
    void setPulse (float newPulseAlpha)
    {
        if (! approximatelyEqual (pulseAlpha, newPulseAlpha))
        {
            pulseAlpha = newPulseAlpha;
            repaint();
        }
    }

    /** Right-click anywhere on the row stops this scene's clips (the symmetric of left-click
        launch), routed up through the column's onSceneStopped seam. */
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onStopped != nullptr)
            onStopped (sceneIndex);
    }

    // Explicit repaint on enter/exit: covers the events the ▶ button forwards via its mouse
    // listener (child→outside / outside→child), which setRepaintsOnMouseActivity can't see.
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit  (const MouseEvent&) override { repaint(); }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced ((float) SessionLayout::slotPad);

        // Base fill — panel row, lifted to raisedBg while hovered (neutral chrome, NOT a semantic
        // accent: hover is not launch state; enter/exit-driven, never polled — and child-inclusive,
        // so the fill holds while the pointer is over the ▶ button), tinted playGreen when this
        // scene is the active (playing) row (W04a: "sound is happening here" is green, never amber).
        const bool playing = (state == SceneLaunchState::playing);
        g.setColour (Colour (isMouseOverOrDragging (true) ? ForgeLookAndFeel::raisedBg
                                                          : ForgeLookAndFeel::panelBg));
        g.fillRoundedRectangle (b, 3.0f);

        if (playing)
        {
            g.setColour (Colour (ForgeLookAndFeel::playGreen).withAlpha (0.55f));
            g.fillRoundedRectangle (b, 3.0f);
        }

        // Border: black hairline normally; a 2px play-family outline when queued or playing — the
        // same "about-to-fire / firing" treatment the slot pads use, drawn last (§d).
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 3.0f, 1.0f);

        // Scene name, right of the ▶ button. Dark ink on the green playing fill (onAccent means
        // text-on-AMBER, so it isn't reused here), textPrim otherwise.
        g.setColour (playing ? Colours::black.withAlpha (0.85f)
                             : Colour (ForgeLookAndFeel::textPrim));
        g.setFont (Font (FontOptions (13.0f)));
        g.drawText (name, nameBounds, Justification::centredLeft, true);

        if (state == SceneLaunchState::queued || playing)
        {
            // Beat-pulse parity with the slot pads (W04a): the pushed-in pulseAlpha modulates the
            // ring. pulseAlpha < 0 => no beat pulse flowing (transport not running / poll edge):
            // fall back to the state's peak so the ring never vanishes (mirrors the pad render).
            const float ringAlpha = playing ? (pulseAlpha >= 0.0f ? pulseAlpha : 1.0f)
                                            : (pulseAlpha >= 0.0f ? pulseAlpha : 0.75f);

            g.setColour (Colour (playing ? ForgeLookAndFeel::playGreen
                                         : ForgeLookAndFeel::playGreenDim).withAlpha (ringAlpha));
            g.drawRoundedRectangle (b, 3.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (SessionLayout::slotPad);
        launchButton.setBounds (r.removeFromLeft (launchBtnW));
        r.removeFromLeft (4);
        nameBounds = r;
    }

    std::function<void (int sceneIndex)> onLaunched, onStopped;

private:
    void refreshLaunchLook()
    {
        // ▶ glyph: play-family when the row is hot (playGreen firing, playGreenDim about to fire),
        // primary text when idle. Amber no longer marks launch state (W04a).
        launchButton.setColour (TextButton::textColourOffId,
                                Colour (state == SceneLaunchState::playing ? ForgeLookAndFeel::playGreen
                                      : state == SceneLaunchState::queued  ? ForgeLookAndFeel::playGreenDim
                                                                           : ForgeLookAndFeel::textPrim));
    }

    static constexpr int launchBtnW = 26;   // ▶ button width (sheet 00: 26×22)

    const int sceneIndex;
    const String name;
    SceneLaunchState state = SceneLaunchState::idle;
    float pulseAlpha = -1.0f;   // negative = no pulse flowing (transport stopped / poll edge)

    TextButton launchButton;
    Rectangle<int> nameBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneRow)
};

//==============================================================================
SceneColumnComponent::SceneColumnComponent()
{
    // MASTER "stop all" — stops every launched clip in the grid. Labelled across the full control
    // row (legibility: a lone ■ read as decoration); amber-on-raised, matching the mixer's
    // transport affordances (W04a: amber stays on interactive MASTER chrome only).
    stopAllButton.setButtonText (String::charToString ((juce_wchar) 0x25a0) + " STOP ALL");   // ■ STOP ALL
    stopAllButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::raisedBg));
    stopAllButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::accent));
    stopAllButton.setTooltip ("Stop all clips");
    stopAllButton.onClick = [this] { if (onStopAll) onStopAll(); };
    addAndMakeVisible (stopAllButton);

    masterLabel.setText ("MASTER", dontSendNotification);
    masterLabel.setJustificationType (Justification::centredLeft);
    masterLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::accent));
    masterLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (masterLabel);

    scenesLabel.setText ("SCENES", dontSendNotification);
    scenesLabel.setJustificationType (Justification::centredLeft);
    scenesLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
    scenesLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (scenesLabel);

    // "+ Scene" add-affordance (W07), pinned in the column's bottom footer band. Per the Fable
    // charter it must read as a NEUTRAL/subtle add control, NOT selection-amber (amber = selection
    // only): panel-tone fill, secondary-text label. The button's own hover highlight (JUCE lightens
    // the fill on mouse-over) supplies the "brightening on hover" affordance without a second accent.
    addSceneButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::panelBg));
    addSceneButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textSec));
    addSceneButton.setTooltip ("Add a scene row to the grid");
    addSceneButton.onClick = [this] { if (onAddScene) onAddScene(); };
    addAndMakeVisible (addSceneButton);
}

SceneColumnComponent::~SceneColumnComponent() = default;

void SceneColumnComponent::setScenes (const StringArray& names, int numScenes)
{
    rebuildRows (names, numScenes);
}

void SceneColumnComponent::rebuildRows (const StringArray& names, int numScenes)
{
    rows.clear();

    for (int i = 0; i < numScenes; ++i)
    {
        // Plain value only: the name comes from the rebuild snapshot, never a live te::Scene*.
        // Blank / absent names fall back to the 1-based row number (sheet 00: rows 9-16).
        String display = (i < names.size() ? names[i] : String()).trim();
        if (display.isEmpty())
            display = String (i + 1);

        auto* row = rows.add (new SceneRow (i, display));
        row->onLaunched = [this] (int sceneIndex) { if (onSceneLaunched) onSceneLaunched (sceneIndex); };
        row->onStopped  = [this] (int sceneIndex) { if (onSceneStopped)  onSceneStopped  (sceneIndex); };
        addAndMakeVisible (row);
    }

    resized();
    repaint();
}

void SceneColumnComponent::setSceneState (int sceneIndex, SceneLaunchState state)
{
    if (auto* row = rows[sceneIndex])
        row->setState (state);
}

void SceneColumnComponent::setScenePulse (int sceneIndex, float pulseAlpha)
{
    if (auto* row = rows[sceneIndex])
        row->setPulse (pulseAlpha);
}

int SceneColumnComponent::getNumSceneRows() const
{
    return rows.size();
}

void SceneColumnComponent::resized()
{
    auto r = getLocalBounds();

    // MASTER band across the top — same height as the track header so it aligns horizontally.
    // Stacked down the band: MASTER, the full-width "■ STOP ALL" control row, then the SCENES
    // sublabel on its own line in the remaining (formerly dead) space.
    auto header = r.removeFromTop (SessionLayout::headerH).reduced (6, 4);
    masterLabel.setBounds (header.removeFromTop (20));
    stopAllButton.setBounds (header.removeFromTop (24));
    scenesLabel.setBounds (header);

    // Reserve the per-track clip-stop footer height at the bottom so the scene rows occupy the
    // SAME vertical band as the slot pads in every track column (header / rows / stop-footer).
    // The scene column's footer band hosts the "+ Scene" add-affordance (W07) where the track
    // columns carry their clip-stop ■ — a clean structural parallel, and it scrolls with the grid.
    auto footer = r.removeFromBottom (SessionLayout::stopRowH);
    addSceneButton.setBounds (footer.reduced (SessionLayout::slotPad, 2));

    // Divide the remaining middle into scene rows via the SHARED SessionLayout::rowBand partition —
    // the IDENTICAL algorithm the track columns use — so each scene launch row lines up exactly with
    // its clip pad row at any window height (QC fix: previously a float division here drifted from the
    // columns' integer-floor division on viewports taller than 844px).
    const int n = rows.size();
    if (n <= 0 || r.getHeight() <= 0)
        return;

    const int top  = r.getY();
    const int midH = r.getHeight();

    for (int i = 0; i < n; ++i)
    {
        const auto band = SessionLayout::rowBand (i, n, midH);
        rows.getUnchecked (i)->setBounds (r.getX(), top + band.getStart(), r.getWidth(), band.getLength());
    }
}

void SceneColumnComponent::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (bounds);

    // Accent band across the very top so the MASTER header reads as distinct from track headers
    // (twin of MixerView::MasterStrip's top swatch).
    g.setColour (Colour (ForgeLookAndFeel::accent));
    g.fillRect (bounds.removeFromTop (swatchH));

    // Left-edge hairline separates the pinned column from the scrolling track grid.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, 1, getHeight());
}
