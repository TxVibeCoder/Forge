/*
    SceneColumnComponent — the pinned right-hand scene-launch column of the SessionView clip
    grid, the twin of MixerView's pinned MASTER strip (it lives OUTSIDE the grid's horizontal
    viewport, fixed to the right edge at SessionLayout::sceneColW px).

    Top band: an amber MASTER header (height SessionLayout::headerH so it aligns with the track
    headers) carrying a full-width "■ STOP ALL" button and a "SCENES" sublabel on its own line
    below it. Under the header, one launch row per scene — a ▶ launch button plus the scene name
    (or the row number) — laid to the same row geometry the grid uses (header at top, per-track
    clip-stop footer reserved at the bottom, the numScenes rows dividing the middle evenly) so
    each scene row aligns horizontally with the clip slot row in every track column.

    Receives scene names as PLAIN VALUES via setScenes() during a rebuild — it never caches a
    te::Scene* (R1). Per-row visual state (idle / queued / playing) is pushed in by the owner via
    setSceneState(), the beat-pulse alpha via setScenePulse(), and rendered with the shared
    ForgeLookAndFeel play-family colours (playGreen / playGreenDim, W04a semantic accents),
    matching the slot pads. Amber stays on the interactive MASTER chrome only.

    All engine ops route up through null-guarded std::function seams (onSceneLaunched,
    onSceneStopped, onStopAll); this component NEVER touches the te:: model directly.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include "ui/session/SessionLayout.h"

namespace te = tracktion;

//==============================================================================
/** Per-row launch state for a scene row, mirroring the slot pad vocabulary (§d).
    Drives the row's play-family treatment (W04a): idle = quiet, queued = playGreenDim outline
    (about to fire), playing = filled playGreen. Pushed in by the owner; the row never reads the
    engine itself. */
enum class SceneLaunchState
{
    idle,       // no clips queued / playing for this scene
    queued,     // scene queued to launch at the next quantise boundary
    playing     // scene currently the active row (one or more clips playing)
};

//==============================================================================
class SceneColumnComponent : public juce::Component
{
public:
    SceneColumnComponent();
    ~SceneColumnComponent() override;

    /** Rebuilds the launch rows from plain scene names (NO te::Scene* cached, R1).

        @param names      one entry per scene; an empty / absent entry falls back to a row number.
        @param numScenes  the number of rows to build (the grid's scene count). When names is
                          shorter than numScenes the trailing rows show their 1-based row number;
                          extra names beyond numScenes are ignored. */
    void setScenes (const juce::StringArray& names, int numScenes);

    /** Pushes a row's launch state (idle / queued / playing) and repaints just that row.
        No-op for an out-of-range index. Called by the owner's poll on the message thread. */
    void setSceneState (int sceneIndex, SceneLaunchState state);

    /** Pushes a row's beat-pulse alpha for its queued/playing ring (padPulseAlpha output;
        negative = no pulse flowing). Change-gated inside the row — an unchanged value never
        repaints — so idle rows stay repaint-free across the owner's 25 Hz poll (§e).
        No-op for an out-of-range index. */
    void setScenePulse (int sceneIndex, float pulseAlpha);

    /** Number of scene launch rows currently built (for diagnostics / self-tests). */
    int getNumSceneRows() const;

    void resized() override;
    void paint (juce::Graphics&) override;

    //==========================================================================
    // Upward seams (null-guarded by the owner-facing convention). The owner wires these to
    // ProjectSession ops; this component never mutates the te:: model directly.

    /** A scene's ▶ was clicked → launch every track's clip in that row. */
    std::function<void (int sceneIndex)> onSceneLaunched;

    /** A scene's stop affordance was used → stop that row's clips across all tracks. */
    std::function<void (int sceneIndex)> onSceneStopped;

    /** The MASTER "stop all" ■ was clicked → stop every launched clip in the grid. */
    std::function<void()> onStopAll;

private:
    class SceneRow;   // one launch row — defined in the .cpp

    static constexpr int swatchH = 5;   // accent band across the top (twin of MasterStrip)

    void rebuildRows (const juce::StringArray& names, int numScenes);

    juce::TextButton stopAllButton;
    juce::Label      masterLabel, scenesLabel;
    juce::OwnedArray<SceneRow> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneColumnComponent)
};
