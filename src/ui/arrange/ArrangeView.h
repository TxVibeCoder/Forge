/*
    ArrangeView — the arrangement surface: a vertical stack of track lanes, each showing its
    clips as rectangles with waveform thumbnails, plus a moving playhead overlay.

    Coordinate model: a shared TimelineView maps edit-time <-> pixels across the clip area
    (everything right of the fixed-width track header). Manual rebuild() is called by the
    owner after import / new / open (no ValueTree-listener storms in Phase 1).

    Phase 2 polish adds: a bars|beats ruler across the top of the clip area, per-lane
    mute/solo/arm controls + colour swatch, clip/track selection with accent outlines,
    and right-click context menus for structural edits. Optional std::function callbacks
    let the shell drive an Inspector and persist structural mutations.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
namespace ArrangeLayout
{
    constexpr int headerW = 150;   // track header strip width
    constexpr int laneH   = 76;    // track lane height
    constexpr int gap     = 4;     // vertical gap between lanes
    constexpr int rulerH  = 22;    // bars|beats ruler strip height (top of clip area)
}

//==============================================================================
/** Linear edit-time <-> pixel mapping over a visible time window. */
struct TimelineView
{
    te::TimePosition viewStart { te::TimePosition::fromSeconds (0.0) };
    te::TimePosition viewEnd   { te::TimePosition::fromSeconds (60.0) };

    int timeToX (te::TimePosition t, int width) const
    {
        const double span = (viewEnd - viewStart).inSeconds();
        if (width <= 0 || span <= 0.0)
            return 0;

        return juce::roundToInt ((t - viewStart).inSeconds() / span * (double) width);
    }

    te::TimePosition xToTime (int x, int width) const
    {
        const double span = (viewEnd - viewStart).inSeconds();
        if (width <= 0 || span <= 0.0)
            return viewStart;

        return viewStart + te::TimeDuration::fromSeconds (span * (double) x / (double) width);
    }
};

//==============================================================================
/** Bars|beats ruler strip drawn across the clip area (right of the track headers). */
class TimeRulerComponent : public juce::Component
{
public:
    explicit TimeRulerComponent (TimelineView&);

    void setEdit (te::Edit*);
    void paint (juce::Graphics&) override;

private:
    TimelineView& view;
    te::Edit* edit = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeRulerComponent)
};

//==============================================================================
/** One clip rectangle; owns a SmartThumbnail and draws the waveform. */
class AudioClipComponent : public juce::Component
{
public:
    AudioClipComponent (TimelineView&, te::Clip&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    te::Clip& getClip() { return clip; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const { return selected; }

    /** Invoked on left-click (clip selection). */
    std::function<void (AudioClipComponent&)> onClicked;
    /** Invoked on right-click; param is the screen-space event for menu placement. */
    std::function<void (AudioClipComponent&, const juce::MouseEvent&)> onRightClicked;

private:
    TimelineView& view;
    te::Clip& clip;
    std::unique_ptr<te::SmartThumbnail> thumbnail;
    bool selected = false;

    void drawWaveformWindow (juce::Graphics&, juce::Rectangle<int> area,
                             double sourceStartSecs, double sourceLenSecs, double speed);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipComponent)
};

//==============================================================================
/** One audio-track lane: a header strip (name + M/S/R + colour swatch) + clip rects. */
class TrackLaneComponent : public juce::Component
{
public:
    TrackLaneComponent (TimelineView&, te::AudioTrack&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void rebuildClips();
    int  getNumClipComponents() const { return clipComps.size(); }

    te::AudioTrack& getTrack() { return track; }

    /** Highlights/clears the selected outline on a clip owned by this lane. */
    void setSelectedClip (te::Clip* clip);
    /** Sets the track-selected visual highlight on the header. */
    void setTrackSelected (bool shouldBeSelected);

    /** Sets the visual arm state (e.g. to revert an optimistic toggle when real arming failed). */
    void setArmed (bool shouldBeArmed);

    void refreshControlStates();

    //==============================================================================
    // Callbacks bubbled up to ArrangeView (set during rebuild()).
    std::function<void (AudioClipComponent&)> onClipClicked;
    std::function<void (AudioClipComponent&, const juce::MouseEvent&)> onClipRightClicked;
    std::function<void (TrackLaneComponent&)> onHeaderClicked;
    /** Left-click on the empty clip area of this lane (not a clip, not the header). */
    std::function<void (TrackLaneComponent&)> onLaneAreaClicked;
    std::function<void (TrackLaneComponent&, const juce::MouseEvent&)> onHeaderRightClicked;
    /** Reflects/toggles the visual arm state; real input arming wired by the record path. */
    std::function<void (te::AudioTrack&, bool)> onArmToggled;
    /** Invoked after a control (mute/solo/colour) mutates the Edit, so the shell can save. */
    std::function<void()> onEditMutated;

private:
    TimelineView& view;
    te::AudioTrack& track;
    juce::OwnedArray<AudioClipComponent> clipComps;

    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" };
    bool armed = false;
    bool trackSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackLaneComponent)
};

//==============================================================================
/** Moving playhead overlay; polls the transport on a ~30Hz timer and supports scrub. */
class PlayheadComponent : public juce::Component,
                          private juce::Timer
{
public:
    PlayheadComponent (TimelineView&, te::TransportControl&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    int getCurrentX() const { return lastX; }

private:
    void timerCallback() override;
    void scrubTo (const juce::MouseEvent&);

    TimelineView& view;
    te::TransportControl& transport;
    int lastX = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayheadComponent)
};

//==============================================================================
class ArrangeView : public juce::Component
{
public:
    explicit ArrangeView (TimelineView&);

    /** Binds the view to an Edit (or nullptr) and rebuilds. */
    void setEdit (te::Edit*);

    /** Re-enumerates tracks/clips and rebuilds the lanes + playhead. */
    void rebuild();

    void resized() override;
    void paint (juce::Graphics&) override;

    int getNumClipComponentsOnTrack0() const;
    int getPlayheadX() const { return playhead != nullptr ? playhead->getCurrentX() : -1; }

    /** Pushes the real arm state for a track's lane back into the view (e.g. after the record
        path accepts or rejects an arm request). No-op if the track has no lane. */
    void setTrackArmState (te::AudioTrack& track, bool armed);

    //==============================================================================
    // Optional callbacks the shell can set to drive Inspector / persistence.
    // Default null => no-op. Selection state itself is owned here.
    std::function<void (te::Clip*)>  onClipSelected;
    std::function<void (te::Track*)> onTrackSelected;
    /** Invoked after any structural Edit mutation (add/delete/rename track, delete clip). */
    std::function<void()> onEditMutated;
    /** Reflects/toggles a lane's visual arm state; real input arming wired by record path. */
    std::function<void (te::AudioTrack&, bool)> onArmToggled;

private:
    void selectClip (AudioClipComponent*);
    void selectTrack (TrackLaneComponent*);
    void clearSelection();

    void showClipContextMenu (AudioClipComponent&, const juce::MouseEvent&);
    void showLaneContextMenu (TrackLaneComponent&, const juce::MouseEvent&);

    void renameClip (te::Clip&);
    void renameTrack (te::AudioTrack&);
    void deleteClip (te::Clip&);
    void addTrack (te::AudioTrack* after);
    void deleteTrack (te::AudioTrack&);

    void notifyEditMutated();

    TimelineView& view;
    te::Edit* edit = nullptr;
    juce::OwnedArray<TrackLaneComponent> lanes;
    std::unique_ptr<TimeRulerComponent> ruler;
    std::unique_ptr<PlayheadComponent> playhead;

    te::Clip* selectedClip = nullptr;
    te::Track* selectedTrack = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
