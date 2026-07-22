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
        by dragging the edge back / one Ctrl+Z). No-op (returns false) when the clip is looping, auto-tempo,
        has no readable source, is already tight, or the first non-silent sample lies at/behind the visible
        start. Non-unity speed ratios (time-stretch / varispeed) are fully handled — see the note below.

        SPEED-RATIO NOTE (B5, closes the W23 residual): the clip offset is in EDIT-seconds while the silence
        scan yields a SOURCE-second, and the engine's own mapping for a non-looping, non-auto-tempo clip is
        sourceTime = (clipRelativeEditTime + offset) * speed (AudioClipBase::clipTimeToSourceFileTime,
        tracktion_AudioClipBase.cpp:1367). So the left edge (clipRelativeEditTime 0) plays source-time
        offset * speed, and moving the edge so it plays source-time Ts needs an edit-time advance of
        Δ = Ts/speed − offset — NOT (Ts − offset)/speed; the two agree only at speed 1. The scan window is
        likewise speed-mapped: it opens at source-frame offset*speed*rate and spans length*speed source-
        seconds. At speed 1 every term reduces to the old unity-speed arithmetic bit-identically.

        AUTO-TEMPO is declined (logged): an auto-tempo clip reinterprets the stored offset in BEATS
        (clipTimeToSourceFileTime's auto-tempo branch divides by the source's own BPM, not the speed ratio),
        so this second-domain scan cannot place the edge — trimming would move it to a wrong boundary.

        Undoable via the clip's Edit UndoManager (setStart is UM-bound); the caller seals the gesture.
        Message-thread only (opens a juce::AudioFormatReader). Returns true iff the clip start was moved. */
    inline bool trimLeadingSilence (te::AudioClipBase& clip, float thresholdDb = -60.0f)
    {
        if (clip.isLooping())                                  // arrange one-shot only (mirrors the MIDI helper)
            return false;

        // Auto-tempo reinterprets the stored offset in BEATS (see the header note) — a source-second scan
        // cannot place the edge. Decline with a diagnostic rather than trim to a wrong boundary. (A clip
        // copied out of a slot keeps auto-tempo until normalised — the W10 gotcha — so this is reachable.)
        if (clip.getAutoTempo())
        {
            FORGE_LOG_WARN ("trimLeadingSilence: clip '" + clip.getName()
                            + "' is auto-tempo (beat-domain offset) - declining");
            return false;
        }

        // Speed-correct source mapping: sourceTime = (clipRelativeEditTime + offset) * speed. The ratio must
        // be a positive finite number; anything else is corrupt clip state, not a stretch setting.
        const double speed = clip.getSpeedRatio();
        if (! std::isfinite (speed) || speed <= 0.0)
        {
            FORGE_LOG_WARN ("trimLeadingSilence: clip '" + clip.getName() + "' has an unusable speed ratio ("
                            + juce::String (speed) + ") - declining");
            return false;
        }

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

        const double sampleRate  = reader->sampleRate;
        const auto   pos         = clip.getPosition();
        const double offSecs     = pos.getOffset().inSeconds();   // EDIT-seconds absorbed at the left edge
        const double srcEdgeSecs = offSecs * speed;               // source-time the left edge plays: (0 + offset) * speed

        // Bound the scan to the clip's USED source span (srcEdge .. srcEdge + length*speed in SOURCE-seconds
        // — an on-timeline length of L edit-seconds consumes L*speed source-seconds) so a huge source file is
        // never fully read; clamp to what the reader actually holds.
        const juce::int64 firstFrame = juce::jmax ((juce::int64) 0,
                                                   (juce::int64) std::llround (srcEdgeSecs * sampleRate));
        if (firstFrame >= reader->lengthInSamples)
            return false;

        const juce::int64 usedFrames = (juce::int64) std::ceil (pos.getLength().inSeconds() * speed * sampleRate);
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

        // Advance the left edge so it plays the onset's source-time Ts: the new offset must satisfy
        // offset' * speed == Ts, so the edit-time delta is Δ = Ts/speed − offSecs (Clip::setStart with
        // preserveSync = true adds the edit-time delta straight onto the offset, tracktion_Clip.cpp:314-315).
        // onset > firstFrame guarantees Δ > 0, and an onset inside the scanned window guarantees Δ < the clip
        // length, so the edge never crosses the clip end. keepLength = false holds the end fixed (the body
        // shortens by the trimmed lead-in); the audio stays put on the timeline.
        const double onsetSecs = (double) onset / sampleRate;                // SOURCE-seconds
        const auto   newStart  = pos.getStart() + te::TimeDuration::fromSeconds (onsetSecs / speed - offSecs);
        clip.setStart (newStart, /*preserveSync*/ true, /*keepLength*/ false);
        return true;
    }
}
