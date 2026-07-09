/*
    AudioEditHelpers — pure, header-only te::-touching helpers for ARRANGE audio-clip edits, the audio
    sibling of MidiEditHelpers. Both ArrangeView (the UI trigger) and the --selftest-trim gate call ONE
    implementation so no raw sample-scanning / te:: edit leaks into a view (shared-utility principle).

    W23 core: non-destructive "trim leading silence" on an arrange audio clip — scan the SOURCE audio for
    the first sample above a dB threshold and move the clip's LEFT edge there via Clip::setStart
    (preserveSync = true, keepLength = false), exactly as forge::midiedit::trimLeadingSilence moves a
    MidiClip's edge to its first event: the audio keeps its absolute timeline position, the skipped lead-in
    is absorbed into the clip offset, and NOTHING is destroyed (reversible by dragging the edge back out /
    one Ctrl+Z).

    Message-thread only (opens a juce::AudioFormatReader; never call on the audio/RT thread).
*/

#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <memory>

#include "core/Log.h"

namespace te = tracktion;

namespace forge::audioedit
{
    /** Trims leading silence on an ARRANGE audio clip: scans the clip's source audio for the first sample
        whose |amplitude| exceeds `thresholdDb`, and moves the clip's LEFT edge forward to that point via
        Clip::setStart (preserveSync = true, keepLength = false) — the audio keeps its absolute timeline
        position, the skipped lead-in is absorbed into the clip offset, and NOTHING is destroyed (reversible
        by dragging the edge back / one Ctrl+Z). No-op (returns false) when the clip is looping, has no
        readable source, is already tight, or the first non-silent sample lies at/behind the visible start.

        SPEED-RATIO NOTE: only unity speed (no time-stretch / varispeed) is handled exactly. At speed 1 the
        clip offset (edit-seconds from the left edge) coincides with a plain source-file offset (seconds), so
        the left edge plays source-time == getPosition().getOffset() and advancing the start by Δ edit-seconds
        advances the source by Δ. For a non-unity speed that mapping picks up the ratio (and a time-stretch
        proxy would have to be scanned instead of the raw source) — rather than risk a wrong boundary, the
        helper declines (returns false) unless the speed ratio is 1. Non-stretched imports/recordings — the
        only clips this UI currently offers the action on — are always speed 1.

        Undoable via the clip's Edit UndoManager (setStart is UM-bound); the caller seals the gesture.
        Message-thread only (opens a juce::AudioFormatReader). Returns true iff the clip start was moved. */
    inline bool trimLeadingSilence (te::AudioClipBase& clip, float thresholdDb = -60.0f)
    {
        if (clip.isLooping())                                  // arrange one-shot only (mirrors the MIDI helper)
            return false;

        // Only unity speed maps the offset (edit-seconds) 1:1 onto the source file; decline otherwise.
        const double speed = clip.getSpeedRatio();
        if (std::abs (speed - 1.0) > 1.0e-9)
            return false;

        const auto audioFile = clip.getAudioFile();            // the SOURCE file (not the possibly-stretched proxy)
        if (! audioFile.isValid())
        {
            FORGE_LOG_WARN ("trimLeadingSilence: clip '" + clip.getName() + "' has no valid source audio file");
            return false;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (
            clip.edit.engine.getAudioFileFormatManager().readFormatManager.createReaderFor (audioFile.getFile()));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            FORGE_LOG_WARN ("trimLeadingSilence: could not open a reader for " + audioFile.getFile().getFullPathName());
            return false;
        }

        const double sampleRate = reader->sampleRate;
        const auto   pos        = clip.getPosition();
        const double offSecs    = pos.getOffset().inSeconds();   // source-time the left edge plays (speed 1)

        // Bound the scan to the clip's USED source span (offset .. offset+length in source-seconds, which at
        // speed 1 equals the on-timeline length) so a huge source file is never fully read; clamp to what the
        // reader actually holds.
        const juce::int64 firstFrame = juce::jmax ((juce::int64) 0,
                                                   (juce::int64) std::llround (offSecs * sampleRate));
        if (firstFrame >= reader->lengthInSamples)
            return false;

        const juce::int64 usedFrames = (juce::int64) std::ceil (pos.getLength().inSeconds() * sampleRate);
        const juce::int64 scanFrames = juce::jmin (usedFrames, reader->lengthInSamples - firstFrame);
        if (scanFrames <= 0)
            return false;

        // First frame whose |amplitude| is at/above the threshold gain, scanning ALL channels. The upper
        // bound is effectively infinite (any sample louder than the threshold ends the silence). searchForLevel
        // reads the source in blocks internally (memory-bounded) and returns an ABSOLUTE source-frame index,
        // or -1 if the whole scanned window is below threshold.
        const double thresholdGain = (double) juce::Decibels::decibelsToGain (thresholdDb);
        const juce::int64 onset = reader->searchForLevel (firstFrame, scanFrames,
                                                          thresholdGain, 1.0e6,
                                                          /*minimumConsecutiveSamples*/ 1);

        if (onset <= firstFrame)          // -1 (all silent) OR already tight (onset AT the visible start)
            return false;

        // Advance the left edge to the onset's source-time. At speed 1 the edit-time delta equals the
        // source-time delta; preserveSync = true folds it into the offset so the audio stays put on the
        // timeline, keepLength = false holds the clip end fixed (the body shortens by the trimmed lead-in).
        const double onsetSecs = (double) onset / sampleRate;
        const auto   newStart  = pos.getStart() + te::TimeDuration::fromSeconds (onsetSecs - offSecs);
        clip.setStart (newStart, /*preserveSync*/ true, /*keepLength*/ false);
        return true;
    }
}
