/*
    VelocityLane — the fixed-height strip beneath the piano-roll grid that shows and edits each
    note's velocity as a vertical bar. It shares the HORIZONTAL (time) axis with the grid via the
    owning PianoRollView (owner.beatToX), honouring the same left keybed gutter so each bar lines up
    directly under its note. A bar's height is proportional to velocity/127 measured from the lane
    bottom; clicking or dragging on a bar sets that note's velocity (lane-y -> 1..127, clamped).

    The lane lives OUTSIDE the grid's Viewport (it must not scroll vertically with the pitch axis),
    so it reads the clip and notes straight from the owner each paint rather than holding components.
    A null clip / no notes draws the strip empty and gracefully ignores edits.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class PianoRollView;

//==============================================================================
namespace VelocityLaneLayout
{
    constexpr int laneHeight = 64;   // fixed strip height at the bottom of the editor
    constexpr int barWidth   = 5;    // px width of each velocity bar (centred on the note's x)
    constexpr int padTop     = 6;    // breathing room above the tallest (127) bar
}

//==============================================================================
class VelocityLane : public juce::Component
{
public:
    explicit VelocityLane (PianoRollView& owner);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    /** Maps a lane-local y to a velocity in 1..127 (top = 127, bottom = 1). */
    int yToVelocity (int localY) const;

    /** Finds the note whose bar is nearest the given lane-local x (within a small hit tolerance),
        or nullptr if the clip is empty / no bar is close enough. When several notes share an x the
        loudest (topmost bar) wins, so the most visible bar is the one you grab. */
    te::MidiNote* noteAtX (int localX) const;

    /** Applies a velocity edit at the pointer to the nearest note (shared by mouseDown/mouseDrag). */
    void editVelocityAt (const juce::MouseEvent&);

    PianoRollView& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityLane)
};
