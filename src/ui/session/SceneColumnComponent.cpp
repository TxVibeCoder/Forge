#include "ui/session/SceneColumnComponent.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

//==============================================================================
/*  SceneRow — one scene-launch row: a ▶ launch button on the left and the scene name (or its
    1-based row number) filling the rest. The row holds its scene index and a pushed-in
    SceneLaunchState only — never a te::Scene*. The launch button fires onLaunched(); a
    right-click (or the playing/queued affordance) fires onStopped(); both are wired up to the
    column's seams by SceneColumnComponent.

    State render (mirrors the slot pad vocabulary, §d + the W04a semantic accents, named
    ForgeLookAndFeel colours only — playing/queued speak the play-green family, never amber):
      - idle    → panelBg row, textPrim name, neutral ▶
      - queued  → 2px playGreenDim outline (about to fire), playGreenDim ▶
      - playing → playGreen-tinted fill + 2px playGreen outline, dark name, playGreen ▶
*/
class SceneColumnComponent::SceneRow : public Component
{
public:
    SceneRow (int idx, const String& displayName)
        : sceneIndex (idx), name (displayName)
    {
        // ▶ launch button. Transparent fill so the row's own paint() shows through; the glyph
        // recolours with state via refreshLaunchLook().
        launchButton.setButtonText (String::charToString ((juce_wchar) 0x25b6));   // ▶
        launchButton.setColour (TextButton::buttonColourId, Colours::transparentBlack);
        launchButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::textPrim));
        launchButton.setTooltip ("Launch " + name);
        launchButton.onClick = [this] { if (onLaunched) onLaunched (sceneIndex); };
        addAndMakeVisible (launchButton);

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

    /** Right-click anywhere on the row stops this scene's clips (the symmetric of left-click
        launch), routed up through the column's onSceneStopped seam. */
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onStopped != nullptr)
            onStopped (sceneIndex);
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced ((float) SessionLayout::slotPad);

        // Base fill — panel row, tinted playGreen when this scene is the active (playing) row
        // (W04a: "sound is happening here" is green, never amber).
        const bool playing = (state == SceneLaunchState::playing);
        g.setColour (Colour (ForgeLookAndFeel::panelBg));
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
            g.setColour (Colour (playing ? ForgeLookAndFeel::playGreen
                                         : ForgeLookAndFeel::playGreenDim));
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

    TextButton launchButton;
    Rectangle<int> nameBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneRow)
};

//==============================================================================
SceneColumnComponent::SceneColumnComponent()
{
    // MASTER "stop all" ■ — stops every launched clip in the grid. Amber-on-raised, matching
    // the mixer's transport affordances.
    stopAllButton.setButtonText (String::charToString ((juce_wchar) 0x25a0));   // ■
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

int SceneColumnComponent::getNumSceneRows() const
{
    return rows.size();
}

void SceneColumnComponent::resized()
{
    auto r = getLocalBounds();

    // MASTER band across the top — same height as the track header so it aligns horizontally.
    auto header = r.removeFromTop (SessionLayout::headerH).reduced (6, 4);
    masterLabel.setBounds (header.removeFromTop (20));
    auto controlRow = header.removeFromTop (24);
    stopAllButton.setBounds (controlRow.removeFromLeft (28));
    controlRow.removeFromLeft (6);
    scenesLabel.setBounds (controlRow);

    // Reserve the per-track clip-stop footer height at the bottom so the scene rows occupy the
    // SAME vertical band as the slot pads in every track column (header / rows / stop-footer).
    r.removeFromBottom (SessionLayout::stopRowH);

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
