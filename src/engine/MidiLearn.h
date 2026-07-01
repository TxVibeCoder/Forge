/*
    MidiLearn — CC → plugin-parameter mapping, driven over Tracktion's NATIVE mapping store.

    Forge does not own a mapping table. Tracktion already models "MIDI-controller → automatable
    parameter" bindings in te::ParameterControlMappings (one instance per Edit, reachable via
    edit.getParameterControlMappings()). That native store:
      - PERSISTS on the Edit — it serialises to the Edit's <CONTROLLERMAPPINGS> child, saved
        automatically by EditFileOperations on save (tracktion_EditFileOperations.cpp:243,357) and
        reloaded automatically by Edit::initialiseControllerMappings on load (tracktion_Edit.cpp:1091,
        wired from Edit construction at :841), so a learned mapping survives a project save/reopen for free.
      - APPLIES live — on an incoming CC it calls param->midiControllerMoved(value) for every
        matching mapping (tracktion_ParameterControlMappings.cpp:154-164). This runs on the MESSAGE
        THREAD (the engine's own MidiControllerParser defers it there via an AsyncUpdater —
        tracktion_MidiInputDevice.cpp:220-234), so there is NO audio/RT-thread apply path to build
        and none to make RT-safe: this seam never touches the audio thread.

    This class is a THIN message-thread driver over that native store. It exists because:
      1. Forge uses the DEFAULT te::UIBehaviour, whose getCurrentlyFocusedEdit()/getLastFocusedEdit()
         return nullptr (tracktion_UIBehaviour.h:24-25). The engine's own device→mappings routing
         (ParameterControlMappings::getCurrentlyFocusedMappings, tracktion_ParameterControlMappings.cpp:60)
         and the native learn-trigger inside sendChange both key off the focused Edit, so without a
         focused-edit UIBehaviour neither fires. This seam drives an EXPLICIT Edit's mappings and
         forces the learn to complete regardless of the focused-edit state (see MidiLearn.cpp).
      2. A learn target is chosen up-front from a picker (PluginHost::getAutomatableParameters),
         so beginLearn() takes the exact parameter, rather than the native "touch a param, then
         wiggle a controller" flow.

    RECOMMENDED companion wiring (proposed to the orchestrator, NOT in this seam's territory):
    install a ForgeUIBehaviour returning the current Edit, so real hardware CCs flow through the
    engine's native MidiControllerParser (correct MIDI-thread → message-thread hand-off for free).
    With or without it, handleIncomingController() is the explicit entry the headless self-test and
    any Forge MIDI-input listener use — the seam works either way.

    Message-thread only. Not thread-safe; do not call from the audio/RT thread.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class MidiLearn  : private te::MidiLearnState::Listener
{
public:
    /** Registers with the engine's MidiLearnState (engine-owned; lives for the app's lifetime). */
    explicit MidiLearn (te::Engine&);
    ~MidiLearn() override;

    //==============================================================================
    // Learn

    /** Enters learn mode targeting `paramToMap`. The NEXT controller delivered to
        handleIncomingController() binds that CC to this parameter (via the native store) and exits
        learn mode; onLearnComplete fires once the binding lands. Re-entrant: a prior in-flight learn
        is cancelled first. The parameter is retained for the duration of the learn. Message-thread only. */
    void beginLearn (te::AutomatableParameter& paramToMap);

    /** Leaves learn mode without binding anything. Safe to call when not learning. */
    void cancelLearn();

    /** True between beginLearn() and the bind/cancel. */
    bool isLearning() const noexcept                        { return learning; }

    /** The parameter currently being learned, or nullptr when not learning. */
    te::AutomatableParameter* getLearningParam() const noexcept  { return learningParam.get(); }

    //==============================================================================
    // Live routing

    /** The Edit whose mappings live (non-learn) CCs drive. The app sets this on project open/swap.
        During a learn the target parameter's own Edit is used instead, so this is only the
        live-apply fallback. */
    void setActiveEdit (te::Edit* edit) noexcept            { activeEdit = edit; }

    /** Feeds one incoming controller-change into the native store. In learn mode this completes the
        pending binding for the chosen parameter; otherwise it drives any existing CC→param mappings
        (live control). `ccNumber` is a 0–127 MIDI CC; `value0to1` is the normalised controller value;
        `channel` is the 1-based MIDI channel. No-op if there is no Edit to act on.

        RT-UNSAFE by design (takes locks, broadcasts changes, schedules an async update) — MESSAGE
        THREAD ONLY. A MIDI-input listener that runs on the MIDI callback thread must marshal to the
        message thread before calling this. Deliberately does NOT log: a knob sweep is a high-rate
        stream and logging here would violate the "never log per-tick" rule. */
    void handleIncomingController (int ccNumber, float value0to1, int channel);

    //==============================================================================
    // Thin reads/removes over the native store (all message-thread; derive the Edit from the param)

    /** True if `param` currently has any CC mapping in its Edit's native store. */
    static bool isMapped (te::AutomatableParameter& param);

    /** If `param` is mapped to a plain MIDI CC, fills ccNumberOut (0–127) + channelOut and returns
        true. Returns false if unmapped OR mapped to a non-CC controller (NRPN/RPN/pressure). */
    static bool getMappedCC (te::AutomatableParameter& param, int& ccNumberOut, int& channelOut);

    /** Removes `param`'s mapping from its Edit's native store. Returns true if one was removed. */
    static bool clearMapping (te::AutomatableParameter& param);

    //==============================================================================
    /** The native controllerID encoding for a plain MIDI CC is 0x10000 | ccNumber
        (tracktion_MidiInputDevice.cpp:201). These convert between a 0–127 CC and that id. */
    static constexpr int ccToControllerID (int ccNumber) noexcept   { return 0x10000 + (ccNumber & 0x7f); }
    static int controllerIDToCC (int controllerID) noexcept;

    /** Fired once, on the message thread, when a learn completes (the parameter has just been bound).
        Set by the UI to refresh a "learned CC" indicator. */
    std::function<void (te::AutomatableParameter&)> onLearnComplete;

private:
    // te::MidiLearnState::Listener
    void midiLearnStatusChanged (bool isActive) override;
    void midiLearnAssignmentChanged (te::MidiLearnState::ChangeType) override;

    void resetLearnState();

    te::Engine& engine;

    bool learning = false;
    te::AutomatableParameter::Ptr learningParam;   // retained across the async bind
    te::Edit* learningEdit = nullptr;              // the learning param's Edit
    te::Edit* activeEdit = nullptr;                // live-apply fallback Edit

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
};
