/*
    ModifierHelpers — small, header-only free functions over the engine's Modifier system
    (frontier Wave 9): create/configure an LFOModifier on a track's ModifierList, assign it as a
    live modulation source on an AutomatableParameter (e.g. a track's volParam), and tear the
    whole thing back down. The engine seam a future "Modulate" UI affordance (deferred to a Fable
    follow-up — this wave is engine-only, no UI) and the --selftest-modifier gate build on.

    Frozen recipe: docs/wave-9-lfo-recipe.local.md (source-verified against libs/tracktion_engine,
    ~all claims file:line-cited there). Every signature below was re-verified directly against the
    engine headers/sources while writing this file:
      - model/tracks/tracktion_Track.h            (Track::getModifierList)
      - model/automation/tracktion_Modifier.h      (Modifier, Modifier::remove, ModifierList::insertModifier)
      - model/automation/modifiers/tracktion_LFOModifier.h/.cpp (LFOModifier, its param CachedValues + defaults)
      - model/automation/modifiers/tracktion_ModifierCommon.h   (ModifierCommon::RateType: hertz=0, bar=3)
      - model/automation/tracktion_AutomatableParameter.h/.cpp  (addModifier/removeModifier/getAssignments/
                                                                   hasActiveModifierAssignments/updateToFollowCurve)
      - utilities/tracktion_Identifiers.h          (IDs::LFO)

    Every AudioTrack carries a non-null ModifierList (Track::getModifierList(), Track.h:337) — the
    null-check in addLFO() below is defensive/logged, not expected to fire in practice.

    Message-thread only, same contract as AutomationHelpers.h: ModifierList::insertModifier and
    AutomatableParameter::addModifier / removeModifier / getAssignments / hasActiveModifierAssignments
    all TRACKTION_ASSERT_MESSAGE_THREAD internally (tracktion_Modifier.cpp, tracktion_AutomatableParameter.cpp).
    Edit::updateModifierTimers has NO thread assert (it is normally driven from the audio render
    callback once per block) but is safe to call from the message thread in a headless selftest that
    never spins up the audio graph — the caller (not this header) owns that call, since it is an
    Edit-wide tick, not a per-modifier one.

    Load-bearing gotchas (full source trace in the frozen recipe):
      - LFOModifier::rateType CachedValue defaults to ModifierCommon::bar, NOT hertz
        (tracktion_LFOModifier.cpp:160) — applyConfig() always writes rateType explicitly from
        LFOConfig::tempoSync, never relies on the engine default.
      - depth MUST be > 0 for the LFO to produce anything but a flat line: LFOModifierTimer::setPhase
        multiplies the raw waveform sample by depthParam before adding offset/applying the bipolar
        remap (tracktion_LFOModifier.cpp:117-124).
      - In free-running (non-tempo-synced) mode the LFO's phase advances by `numSamples` passed to
        Edit::updateModifierTimers(), NOT by editTime — an internal Ramp accumulates
        numSamples/sampleRate each call (tracktion_LFOModifier.cpp:23,34-43). A caller sweeping ticks
        with numSamples == 0 will see a frozen value; numSamples must be > 0 (e.g. 512) to observe
        oscillation.
      - AutomatableParameter::removeModifier(ModifierSource&) removes ALL assignments driven by that
        source in one call (tracktion_AutomatableParameter.cpp:768-776) — unassign() below wraps
        exactly that overload, so it is already "detach everything this LFO drives on this param",
        not a single-assignment removal.
      - Modifier::remove() (tracktion_Modifier.cpp:112-129) already walks every parameter the modifier
        currently drives and calls removeModifier() on each before detaching the modifier's own
        ValueTree child from the track's ModifierList — so removeLFO() alone is a complete teardown;
        callers do not need to unassign() every target first (doing so anyway, as the selftest below
        does, is harmless and makes a two-step teardown explicit/testable).

    No logging on the pure accessors (addLFO is the one fallible seam here — a null ModifierList or a
    failed IDs::LFO -> LFOModifier resolve — so it is the only function that logs; applyConfig/assign/
    unassign/removeLFO are thin, side-effect-explicit wrappers over calls that do not fail in normal
    use, matching AutomationHelpers.h's "log at the fallible seam, not inside a pure accessor" rule).
*/

#pragma once

#include <JuceHeader.h>

#include "core/Log.h"

namespace te = tracktion;

namespace forge::modifier
{
    //==============================================================================
    /** Configuration for a new (or re-applied) LFO modifier. Field values mirror the units of the
        matching LFOModifier CachedValue (LFOModifier.h:79): rateHz/depth/offset are the engine's own
        0..1 (or 0.01..50 for rate) parameter ranges, wave is te::LFOModifier::Wave (0 == waveSine),
        bipolar/tempoSync are the two engine bools re-exposed as bools rather than raw param floats. */
    struct LFOConfig
    {
        float rateHz    = 2.0f;    ///< Free-running rate in Hz, used when tempoSync == false. Engine range {0.01, 50}.
        float depth     = 1.0f;    ///< 0..1. MUST be > 0 for the LFO to oscillate (0 = flat line at offset).
        bool  bipolar   = true;    ///< true: output remapped to swing -1..+1 through 0. false: raw 0..1 (unipolar).
        int   wave      = 0;       ///< te::LFOModifier::Wave enum value (0 == waveSine).
        bool  tempoSync = false;   ///< false -> rateType = hertz (uses rateHz, free-running). true -> rateType = bar (tempo-synced; rateHz ignored).
        float offset    = 0.0f;    ///< 0..1, added to the raw waveform sample before the bipolar remap.
    };

    //==============================================================================
    // Construction / configuration
    //
    // Message-thread only (setParameter itself has no assert, but every realistic caller reaches
    // these functions from the message thread via addLFO/the UI — see the file header).

    /** Writes every LFOModifier parameter from `cfg` via setParameter(raw, dontSendNotification).
        Safe to call repeatedly on a live LFO (e.g. to retune it from a future UI) — each call is a
        plain property write, nothing is torn down or recreated. */
    inline void applyConfig (te::LFOModifier& lfo, const LFOConfig& cfg)
    {
        const auto rateType = cfg.tempoSync ? te::ModifierCommon::bar : te::ModifierCommon::hertz;

        lfo.rateTypeParam->setParameter ((float) rateType, juce::dontSendNotification);
        lfo.rateParam    ->setParameter (cfg.rateHz, juce::dontSendNotification);
        lfo.depthParam   ->setParameter (cfg.depth, juce::dontSendNotification);
        lfo.bipolarParam ->setParameter (cfg.bipolar ? 1.0f : 0.0f, juce::dontSendNotification);
        lfo.offsetParam  ->setParameter (cfg.offset, juce::dontSendNotification);
        lfo.waveParam    ->setParameter ((float) cfg.wave, juce::dontSendNotification);
    }

    /** Inserts a new LFO modifier at index 0 of `track`'s ModifierList and applies `cfg` to it.
        insertModifier auto-calls Modifier::initialise() (ModifierList::createNewObject,
        tracktion_Modifier.cpp:302-317), which registers the LFO's ModifierTimer with the Edit — no
        manual init step needed before the caller starts ticking Edit::updateModifierTimers().

        Returns a null Ptr and logs FORGE_LOG_ERROR if `track` has no ModifierList (every AudioTrack
        does — this is a defensive guard, not an expected path) or if insertModifier's IDs::LFO child
        fails to resolve to an LFOModifier (would indicate an engine-side invariant break, since
        ModifierList::createNewObject switches on IDs::LFO specifically to construct one). */
    inline te::LFOModifier::Ptr addLFO (te::AudioTrack& track, const LFOConfig& cfg = {})
    {
        auto* modifierList = track.getModifierList();

        if (modifierList == nullptr)
        {
            FORGE_LOG_ERROR ("addLFO: track '" + track.getName() + "' has no ModifierList");
            return {};
        }

        auto inserted = modifierList->insertModifier (juce::ValueTree (te::IDs::LFO), 0, nullptr);
        auto* lfo = dynamic_cast<te::LFOModifier*> (inserted.get());

        if (lfo == nullptr)
        {
            FORGE_LOG_ERROR ("addLFO: insertModifier(IDs::LFO) did not yield an LFOModifier on track '"
                             + track.getName() + "'");
            return {};
        }

        applyConfig (*lfo, cfg);
        return lfo;
    }

    //==============================================================================
    // Assignment (LFO -> AutomatableParameter)
    //
    // Message-thread only: AutomatableParameter::addModifier/removeModifier/getAssignments/
    // hasActiveModifierAssignments all TRACKTION_ASSERT_MESSAGE_THREAD internally.

    /** Assigns `lfo` as a modulation source on `target` (e.g. a track's volParam) with the given
        assignment depth (the engine's own 0..1 "value" scaler on the assignment itself, layered on
        top of whatever the LFO's own depthParam already applies; offset 0, curve 0.5 — the engine's
        addModifier defaults, curve 0.5 == a linear mapping per its doc comment). Returns the new
        ModifierAssignment. AutomatableParameter::addModifier only returns a null Ptr when `target`
        IS `lfo`'s own AutomatableEditItem (a self-assignment guard, tracktion_AutomatableParameter.cpp:
        724-727) — unreachable when assigning an LFO on one track to a different plugin's parameter,
        but callers of an as-yet-unbuilt UI that lets a user pick an arbitrary (source, target) pair
        should still treat a null return as "no-op, nothing assigned" rather than assume success. */
    inline te::AutomatableParameter::ModifierAssignment::Ptr assign (te::AutomatableParameter& target,
                                                                      te::LFOModifier& lfo,
                                                                      float depth = 1.0f)
    {
        return target.addModifier (lfo, depth, /*offset*/ 0.0f, /*curve*/ 0.5f);
    }

    /** Removes every assignment `lfo` currently drives on `target`
        (AutomatableParameter::removeModifier(ModifierSource&) removes ALL assignments of that
        source in one call, not just the most recent one — tracktion_AutomatableParameter.cpp:768-776).
        A no-op if `lfo` has no assignment on `target`. */
    inline void unassign (te::AutomatableParameter& target, te::LFOModifier& lfo)
    {
        // Guard so this is a genuine no-op when `lfo` isn't assigned here: the wrapped
        // AutomatableParameter::removeModifier(ModifierSource&) hits jassertfalse in a Debug build if the
        // source has no assignment on `target` (tracktion_AutomatableParameter.cpp) — a real trap for a
        // future "Modulate" UI that lets a user unassign an arbitrary (source, target) pair.
        if (target.getModifiers().contains (static_cast<te::AutomatableParameter::ModifierSource*> (&lfo)))
            target.removeModifier (lfo);
    }

    //==============================================================================
    // Teardown

    /** Removes `lfo` from its track's ModifierList (Modifier::remove(), tracktion_Modifier.cpp:112-129).
        This already walks every AutomatableParameter the modifier currently drives and detaches it
        (equivalent to calling unassign() against each one) before removing the modifier's own
        ValueTree child — callers do not need to unassign() every target first. Any te::LFOModifier::Ptr
        the caller is still holding stays valid (ReferenceCountedObject; ModifierList::deleteObject
        only decRefCounts, never deletes directly) but the modifier is fully detached from the Edit:
        do not call addLFO-style re-registration on the same object, and drop the Ptr once done with it. */
    inline void removeLFO (te::LFOModifier& lfo)
    {
        lfo.remove();
    }
}
