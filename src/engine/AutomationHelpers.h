/*
    AutomationHelpers — small, header-only free functions over a track's built-in
    VolumeAndPanPlugin automation curves. The engine seam the automation UI lane (and the
    --selftest-automation gate) build on: get the volume/pan AutomatableParameter, then
    add / move / remove / clear points on its AutomationCurve and read the interpolated value.

    Models its style on EngineHelpers.h. Pure and message-thread only: every call touches the
    engine's ValueTree-backed automation state, AutomationCurve::getValueAt asserts the message
    thread, and updateStream builds the interpolated stream on the message thread. No logging —
    these are thin, side-effect-explicit wrappers; the caller owns diagnostics at the seam it
    drives (per docs/LOGGING.md, we log at the fallible seam, not inside a pure accessor).

    Value units (verified against the engine, tracktion_VolumeAndPan.h): volume is the plugin's
    fader SLIDER POSITION 0..1 (NOT dB) and pan is -1..+1. curveShape 0 = linear; the range is
    -1..+1 (bezier either side), which the engine jasserts in AutomationPoint.

    Why every mutator ends with param.updateStream(): after a curve edit the engine only rebuilds
    the interpolated read stream (and flips isAutomationActive on) on a deferred 10 ms message-
    thread timer. updateStream() forces that rebuild synchronously so the change is live on the
    very next processed audio block — load-bearing for a headless test that plays immediately, and
    the same reason Edit load calls updateStream() on every parameter.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace AutomationHelpers
{
    //==============================================================================
    // Parameter access
    //
    // Each te::AudioTrack carries a built-in te::VolumeAndPanPlugin (AudioTrack::getVolumePlugin(),
    // may be null) whose public volParam / panParam members ARE the AutomatableParameters
    // (tracktion_VolumeAndPan.h:74). We hand back the ::Ptr; a null Ptr means the track has no
    // vol/pan plugin, which every caller must tolerate.
    //
    // Message-thread only.

    /** The track's volume automation parameter (slider position 0..1), or a null Ptr if the
        track has no volume/pan plugin. */
    inline te::AutomatableParameter::Ptr getTrackVolumeParam (te::AudioTrack& track)
    {
        if (auto* vp = track.getVolumePlugin())
            return vp->volParam;

        return {};
    }

    /** The track's pan automation parameter (-1..+1), or a null Ptr if the track has no
        volume/pan plugin. */
    inline te::AutomatableParameter::Ptr getTrackPanParam (te::AudioTrack& track)
    {
        if (auto* vp = track.getVolumePlugin())
            return vp->panParam;

        return {};
    }

    //==============================================================================
    // Curve editing / reading
    //
    // The curve lives on the parameter (AutomatableParameter::getCurve()). Points are (time in
    // SECONDS, value in the parameter's units, curveShape). Indices are 0-based and ordered by
    // time; the engine keeps them sorted on insert. Out-of-range indices are a no-op returning a
    // safe default (false / -1 / current value), never an assert — so a stale UI index can't take
    // the app down. Every mutator ends with updateStream() (see the file header).
    //
    // Message-thread only.

    /** Adds a point at `seconds` with `value` and `curveShape` (0 = linear). Returns the index of
        the new point (points stay sorted by time). Rebuilds the read stream synchronously. */
    inline int addPoint (te::AutomatableParameter& param, double seconds, float value, float curveShape = 0.0f)
    {
        const int index = param.getCurve().addPoint (te::TimePosition::fromSeconds (seconds), value, curveShape);
        param.updateStream();
        return index;
    }

    /** Removes the point at `index`. Returns false (no-op) if the index is out of range. Rebuilds
        the read stream synchronously on success. */
    inline bool removePoint (te::AutomatableParameter& param, int index)
    {
        auto& curve = param.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
            return false;

        curve.removePoint (index);
        param.updateStream();
        return true;
    }

    /** Moves the point at `index` to (`seconds`, `value`), returning the point's resulting index
        (which may differ if the move reorders it past a neighbour). No-op returning `index`
        unchanged if the index is out of range. Rebuilds the read stream synchronously.

        Implemented as remove + add rather than the engine's AutomationCurve::movePoint: that
        method CLAMPS the new time between the point's immediate neighbours (so it can never carry
        a point past another point in time) and also snaps the value through the owner parameter's
        range/state. remove + re-add gives the caller a true reposition to an arbitrary (time,
        value) with the correct, possibly-changed sorted index — the behaviour the seam's contract
        promises. The point's curveShape is preserved across the move. */
    inline int movePoint (te::AutomatableParameter& param, int index, double seconds, float value)
    {
        auto& curve = param.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
            return index;

        const float curveShape = curve.getPointCurve (index);

        curve.removePoint (index);
        const int newIndex = curve.addPoint (te::TimePosition::fromSeconds (seconds), value, curveShape);
        param.updateStream();
        return newIndex;
    }

    /** Removes every point from the curve. Rebuilds the read stream synchronously (the parameter
        goes back to following its explicit value). */
    inline void clearAutomation (te::AutomatableParameter& param)
    {
        param.getCurve().clear();
        param.updateStream();
    }

    /** Number of automation points on the parameter's curve. */
    inline int getNumPoints (const te::AutomatableParameter& param)
    {
        return param.getCurve().getNumPoints();
    }

    /** Time in SECONDS of the point at `index`, or 0.0 if the index is out of range. */
    inline double getPointTime (const te::AutomatableParameter& param, int index)
    {
        const auto& curve = param.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
            return 0.0;

        return curve.getPointTime (index).inSeconds();
    }

    /** Value of the point at `index` (parameter units), or 0.0f if the index is out of range. */
    inline float getPointValue (const te::AutomatableParameter& param, int index)
    {
        const auto& curve = param.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
            return 0.0f;

        return curve.getPointValue (index);
    }

    /** The curve's interpolated value at `seconds` (parameter units). With no points the curve
        reports the parameter's current value; this is pure curve math, independent of playback. */
    inline float getValueAt (te::AutomatableParameter& param, double seconds)
    {
        return param.getCurve().getValueAt (te::TimePosition::fromSeconds (seconds));
    }

    /** True if the parameter is currently being driven by automation (its curve stream is active,
        i.e. it has points and the stream has been built) or by a modifier source. */
    inline bool isActive (const te::AutomatableParameter& param)
    {
        return param.isAutomationActive();
    }
}
