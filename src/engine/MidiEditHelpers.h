/*
    MidiEditHelpers — pure, header-only te::-touching MIDI edit helpers, so both PianoRollView
    (the UI trigger) and the --selftest-quantise gate call ONE implementation and no raw
    te::QuantisationType call leaks into the view (shared-utility principle).

    W4 core: destructive quantise of note STARTS to the grid, preserving each note's length, on the
    Edit UndoManager (undoable + visually verifiable). Mirrors the engine's own unit test at
    tracktion_QuantisationType.cpp:334-336.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <cmath>
#include <algorithm>
#include <vector>

namespace te = tracktion;

namespace forge::midiedit
{
    // Maps the piano-roll grid step (a FRACTION OF A BEAT) to the engine QuantisationType type-name.
    // The engine's names are fractions of a BEAT, not note-values: "1/4 beat".beatFraction == 0.25
    // (tracktion_QuantisationType.cpp:33). So gridBeats 0.25 -> "1/4" (NOT "1/16") — notes snap to the
    // VISIBLE grid, not 4x finer. Returns "(none)" when no grid matches (caller treats it as a no-op via
    // q.isEnabled()).
    inline juce::String gridBeatsToTypeName (double gridBeats)
    {
        struct M { double beats; const char* name; };
        static const M table[] = {
            { 1.0, "1" }, { 0.5, "1/2" }, { 1.0 / 3.0, "1/3" }, { 0.25, "1/4" },
            { 1.0 / 6.0, "1/6" }, { 0.125, "1/8" }, { 1.0 / 9.0, "1/9" }, { 1.0 / 12.0, "1/12" },
            { 0.0625, "1/16" }, { 1.0 / 24.0, "1/24" }, { 0.03125, "1/32" }, { 1.0 / 64.0, "1/64" }
        };

        for (auto& m : table)
            if (std::abs (gridBeats - m.beats) < 1.0e-6)
                return m.name;

        return "(none)";
    }

    // Rewrites note STARTS in clip.getSequence() to the grid, preserving each note's length. gridBeats:
    // the piano-roll grid step in beats (e.g. 0.25). strength: 0..1 (UI percent / 100). selectedOnly ==
    // false quantises ALL notes; else only notes for which isSelected(note) is true. Returns the number of
    // notes actually moved. undo: the Edit's UndoManager (&clip.edit.getUndoManager()) so it is ONE
    // undoable step.
    inline int quantiseNoteStarts (te::MidiClip& clip,
                                   double gridBeats,
                                   double strength,
                                   bool selectedOnly,
                                   const std::function<bool (te::MidiNote&)>& isSelected,
                                   juce::UndoManager* undo)
    {
        // LOCAL throwaway QuantisationType — never mutate clip.getQuantisation() (the clip's persistent
        // live-playback quantise setting). setType accepts the bare fraction ("1/4"); setProportion IS the
        // 0-100% strength (roundBeatToNearest folds it in — no hand-rolled lerp).
        te::QuantisationType q;
        q.setType (gridBeatsToTypeName (gridBeats));
        q.setProportion ((float) juce::jlimit (0.0, 1.0, strength));

        if (! q.isEnabled())          // "(none)" grid OR 0% strength -> clean no-op
            return 0;

        // Snapshot before mutating: setStartAndLength marks the cached notes array dirty (triggerSort).
        juce::Array<te::MidiNote*> notes (clip.getSequence().getNotes());

        int moved = 0;
        for (auto* n : notes)
        {
            if (n == nullptr || (selectedOnly && ! isSelected (*n)))
                continue;

            const auto newStart = q.roundBeatToNearest (n->getStartBeat());   // strength folded in
            if (newStart != n->getStartBeat())
            {
                // Preserve length: pass the ORIGINAL getLengthBeats() back unchanged (starts-only).
                n->setStartAndLength (newStart, n->getLengthBeats(), undo);
                ++moved;
            }
        }

        return moved;
    }

    // Shifts every note in `notes` by the SAME beat delta, group-clamped so the whole set moves
    // together and never crosses beat 0 (mirrors PianoRollView::commitMoveSelection's edge clamp,
    // generalized to a FIXED delta rather than snapping a drag's absolute position). Returns the
    // actual (clamped) delta applied. One undoable step. Caller ensures `notes` is non-empty.
    inline double shiftNoteStarts (const std::vector<te::MidiNote*>& notes, double beatDelta, juce::UndoManager* undo)
    {
        if (notes.empty())
            return 0.0;

        double minStart = notes.front()->getStartBeat().inBeats();
        for (auto* n : notes)
            minStart = std::min (minStart, n->getStartBeat().inBeats());

        const double clamped = std::max (beatDelta, -minStart);

        if (clamped != 0.0)
            for (auto* n : notes)
                n->setStartAndLength (te::BeatPosition::fromBeats (std::max (0.0, n->getStartBeat().inBeats() + clamped)),
                                      n->getLengthBeats(), undo);

        return clamped;
    }

    // Trims leading silence on an ARRANGE MidiClip: moves the clip's LEFT edge forward to its first
    // event (note/CC/sysex via MidiList::getFirstBeatNumber), keeping every event at its absolute
    // timeline position (Clip::setStart preserveSync=true, keepLength=false — the boundary moves, the
    // content does not) and DELETING NOTHING (the skipped lead-in is absorbed into the clip offset,
    // reversible by dragging the edge back). No-op (returns false) when the clip is empty, already
    // tight (first event at/behind the visible start), or LOOPING (an arrange one-shot is the target;
    // W5/W13: a looping/slot clip re-normalises). Undoable via the clip's Edit UndoManager (setStart
    // is UM-bound); the caller seals the gesture. Message-thread only. Returns true iff the clip
    // start was moved.
    inline bool trimLeadingSilence (te::MidiClip& clip)
    {
        if (clip.isLooping())                          // arrange one-shot only — never a looping/slot clip
            return false;

        auto& seq = clip.getSequence();                // NEVER getSequenceLooped() (edits there are discarded)
        if (seq.getNumNotes() == 0)
            return false;

        const auto firstBeat    = seq.getFirstBeatNumber();                                   // earliest event, content-relative
        const auto visStartBeat = clip.getContentBeatAtTime (clip.getPosition().getStart());  // current visible content start
        if (firstBeat <= visStartBeat + te::BeatDuration::fromBeats (1.0e-6))                  // already tight
            return false;

        const auto newStart = clip.getTimeOfContentBeat (firstBeat);   // absolute edit time the 1st event plays at now
        clip.setStart (newStart, /*preserveSync*/ true, /*keepLength*/ false);   // move left edge, keep notes + end
        return true;
    }
}
