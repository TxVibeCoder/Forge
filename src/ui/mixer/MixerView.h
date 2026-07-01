/*
    MixerView — the mixing surface: a horizontal row of channel strips, one per audio track
    in the Edit, plus a fixed MASTER strip pinned to the right edge, replacing the Phase-2
    placeholder Label in the centre view-slot.

    Each track strip shows the track name, a vertical dB volume fader, a rotary pan control,
    M/S toggle buttons, a colour swatch, an INSERT-SLOTS panel (the track's insert plugins
    with a "+" to add, click to open, right-click/X to remove — via the PluginHost contract),
    and a thin vertical peak meter beside the fader. The MASTER strip drives the edit master
    volume (edit.getMasterVolumePlugin()) with its own fader + meter.

    Fader/pan moves push to the engine live (via EngineHelpers); M/S call AudioTrack::set
    Mute/setSolo. Strips are rebuilt from scratch on setEdit() (no ValueTree-listener storms —
    same manual-rebuild model as ArrangeView). Peak meters are polled by a single ~28 Hz timer
    owned by MixerView, which runs only while an Edit is bound.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class ProjectSession;   // the aux seam (buses/sends) lives here — see setSession()

//==============================================================================
namespace MixerLayout
{
    constexpr int stripW      = 92;   // channel-strip width (widened for the insert panel + meter)
    constexpr int masterW     = 96;   // master strip width
    constexpr int returnW     = 92;   // aux-return strip width (pinned right, before master)
    constexpr int stripGap    = 0;    // strips sit flush; the strip paints its own right hairline
    constexpr int auxBusCount = 2;    // "A"/"B" aux buses (README product call: default 2)
}

//==============================================================================
class MixerView : public juce::Component,
                  private juce::Timer
{
public:
    MixerView();
    ~MixerView() override;

    /** Binds the view to an Edit (or nullptr) and rebuilds one strip per audio track
        plus the master strip. Starts/stops the meter timer with the binding. */
    void setEdit (te::Edit*);

    /** Binds the aux seam (buses/sends). Call ONCE after construction, before/around setEdit;
        the ProjectSession outlives individual Edits. When unset (nullptr) the mixer degrades
        gracefully: no send knobs, no aux-return strips — every structural aux op routes through
        this seam, so the view makes no raw te:: bus calls. Triggers a rebuild. */
    void setSession (ProjectSession*);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Track-strip count (excludes the master strip), for diagnostics / self-tests. */
    int getNumStrips() const;

private:
    class ChannelStrip;   // track strip — defined in the .cpp
    class MasterStrip;    // master strip — defined in the .cpp
    class ReturnStrip;    // aux-return strip (placeholder or live) — defined in the .cpp

    void rebuild();
    void timerCallback() override;

    te::Edit*       edit    = nullptr;
    ProjectSession* session = nullptr;   // aux seam (buses/sends); null until setSession()

    // Track strips live inside a Viewport's content holder so an Edit with many tracks scrolls
    // horizontally instead of clipping. The aux-return strips and the master strip are OUTSIDE
    // the viewport, pinned to the right (returns first, then master).
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    juce::OwnedArray<ReturnStrip>  returnStrips;   // always MixerLayout::auxBusCount of them
    std::unique_ptr<MasterStrip> master;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
