/*
    ForgeUIBehaviour — the engine-owned UIBehaviour that closes Forge's "focused Edit" gap so
    real hardware CCs reach the native CC->parameter routing (both live control and MIDI-learn).

    WHY THIS EXISTS
    ---------------
    Forge builds the Engine with a NULL UIBehaviour argument, so the engine falls back to the
    default te::UIBehaviour whose getCurrentlyFocusedEdit()/getLastFocusedEdit() return null
    (tracktion_UIBehaviour.h:24-25). That focused-Edit is the pivot the engine's own hardware-CC
    path keys off:
      - The MidiControllerParser routes an incoming CC to
        ParameterControlMappings::getCurrentlyFocusedMappings(engine)
        (tracktion_MidiInputDevice.cpp:232), which resolves the target mappings via
        engine.getUIBehaviour().getLastFocusedEdit() (tracktion_ParameterControlMappings.cpp:60-66).
      - With a null focused Edit that lookup finds no mappings, so a real knob sweep drives nothing
        and a native learn never completes.

    Installing this behaviour makes both getCurrentlyFocusedEdit() and getLastFocusedEdit() return
    the app's currently-open Edit, so the engine's native device->parser->mappings path fires. That
    path is already message-thread-safe: the parser MARSHALS from the MIDI-callback thread to the
    message thread via triggerAsyncUpdate BEFORE calling sendChange (MidiInputDevice.cpp:220-234),
    so returning the live Edit here does NOT introduce any RT/thread hazard.

    This is the companion wiring MidiLearn.h documents ("install a ForgeUIBehaviour returning the
    current Edit"): the explicit MidiLearn::handleIncomingController() entry still works with or
    without it, but this is what lets PHYSICAL MIDI input reach live control and learn completion.

    LIFETIME
    --------
    The Engine OWNS this behaviour and is constructed BEFORE MainComponent (which owns the
    ProjectSession). So:
      - `session` starts null; getCurrentlyFocusedEdit()/getLastFocusedEdit() return null until it
        is set (harmless — matches the pre-existing default-UIBehaviour behaviour).
      - After MainComponent/session exists, the app calls setSession(&session).
      - On MainComponent teardown, the app MUST call setSession(nullptr) BEFORE the Engine (and thus
        this behaviour) is destroyed, so a late CC callback never dereferences a dangling session.
    getEdit() is re-queried on every call, so a project open/close/swap is safe with no extra
    plumbing: the accessors simply return the new Edit, or null when no project is open.

    Message-thread only. Not thread-safe; the engine's parser already marshals to the message thread
    before these accessors are reached.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class ProjectSession;

class ForgeUIBehaviour  : public te::UIBehaviour
{
public:
    ForgeUIBehaviour() = default;
    ~ForgeUIBehaviour() override = default;

    /** Sets (or clears) the session whose open Edit is reported as focused. Pass the app's
        ProjectSession once MainComponent exists, and nullptr on teardown BEFORE the Engine is
        destroyed. Message-thread only. */
    void setSession (ProjectSession* s) noexcept            { session = s; }

    //==============================================================================
    // te::UIBehaviour — the two accessors the engine's native CC->parameter routing keys off.
    // Both report the app's currently-open Edit (or null when no project / no session), re-queried
    // on every call so a project swap needs no extra notification.

    te::Edit* getCurrentlyFocusedEdit() override;
    te::Edit* getLastFocusedEdit() override;

private:
    ProjectSession* session = nullptr;   // not owned; set/cleared by the app around MainComponent's life

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeUIBehaviour)
};
