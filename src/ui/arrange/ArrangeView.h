/*
    ArrangeView — the arrangement surface: a vertical stack of track lanes, each showing its
    clips as rectangles with waveform thumbnails, plus a moving playhead overlay.

    Coordinate model: a shared TimelineView maps edit-time <-> pixels across the clip area
    (everything right of the fixed-width track header). Manual rebuild() is called by the
    owner after import / new / open (no ValueTree-listener storms in Phase 1).

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
        if (width <= 0)
            return viewStart;

        return viewStart + te::TimeDuration::fromSeconds (span * (double) x / (double) width);
    }
};

//==============================================================================
/** One clip rectangle; owns a SmartThumbnail and draws the waveform. */
class AudioClipComponent : public juce::Component
{
public:
    AudioClipComponent (TimelineView&, te::Clip&);

    void paint (juce::Graphics&) override;

    te::Clip& getClip() { return clip; }

private:
    TimelineView& view;
    te::Clip& clip;
    std::unique_ptr<te::SmartThumbnail> thumbnail;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipComponent)
};

//==============================================================================
/** One audio-track lane: a header strip + clip rectangles in the clip area. */
class TrackLaneComponent : public juce::Component
{
public:
    TrackLaneComponent (TimelineView&, te::AudioTrack&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void rebuildClips();
    int  getNumClipComponents() const { return clipComps.size(); }

private:
    TimelineView& view;
    te::AudioTrack& track;
    juce::OwnedArray<AudioClipComponent> clipComps;

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

private:
    TimelineView& view;
    te::Edit* edit = nullptr;
    juce::OwnedArray<TrackLaneComponent> lanes;
    std::unique_ptr<PlayheadComponent> playhead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
