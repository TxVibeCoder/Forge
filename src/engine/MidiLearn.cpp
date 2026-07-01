#include "engine/MidiLearn.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
/*  How this drives Tracktion's native store
    ----------------------------------------
    A bind is completed entirely by te::ParameterControlMappings, which we never subclass. The
    engine's own learn flow is: MidiLearnState active + a "pending" parameter (the one the user
    touched) + an incoming CC → ParameterControlMappings::handleAsyncUpdate() adds the mapping
    (tracktion_ParameterControlMappings.cpp:118-136). We reproduce that flow explicitly for a
    chosen parameter:

      beginLearn(param):
        1. engine MidiLearnState::setActive(true)                     — arms the native learn branch.
        2. edit ParameterChangeHandler::parameterChanged(param,false) — marks `param` PENDING (the
           same call a real param-touch makes; it does not alter the value).
        3. pcm.listenToRow(pcm.getNumControllerIDs())                 — sets listeningOnRow >= 0.

    Step 3 is the load-bearing bit for Forge: sendChange() only schedules its async update when
    `listeningOnRow >= 0 || (learnActive && focusedEdit == edit)` (…ParameterControlMappings.cpp:149).
    Forge's default UIBehaviour has no focused Edit, so we force the trigger via listeningOnRow. The
    row's own value is irrelevant — handleAsyncUpdate's learn branch overwrites listeningOnRow with
    the freshly-added mapping's index and then resets it. (A stray non-learn CC could leave a
    null-parameter row, but saveToEdit() skips null-parameter rows — …:313 — so nothing junk persists.)

    handleIncomingController(cc,…) simply calls pcm.sendChange(0x10000|cc, value, channel). In learn
    mode the async fires and binds; otherwise sendChange's own apply loop drives existing mappings.
    Completion is observed via MidiLearnState::Listener::midiLearnAssignmentChanged(added), which the
    native code raises through a ScopedChangeCaller once the mapping lands (…:122).                  */
//==============================================================================

MidiLearn::MidiLearn (te::Engine& e)
    : te::MidiLearnState::Listener (e.getMidiLearnState()),
      engine (e)
{
}

MidiLearn::~MidiLearn()
{
    // Do NOT touch any Edit here (it may already be gone); only the engine-owned MidiLearnState,
    // which outlives us. The base Listener dtor removes us from the listener list after this body.
    if (learning)
    {
        learning = false;
        engine.getMidiLearnState().setActive (false);
    }

    learningParam = nullptr;
    learningEdit  = nullptr;
    activeEdit    = nullptr;
}

//==============================================================================
void MidiLearn::beginLearn (te::AutomatableParameter& paramToMap)
{
    if (learning)
        cancelLearn();

    learningParam = &paramToMap;                 // retain across the async bind
    learningEdit  = &paramToMap.getEdit();
    learning      = true;

    auto& pcm = learningEdit->getParameterControlMappings();

    // Arm the native learn branch, mark our chosen param pending, and force sendChange to schedule
    // its async update even though Forge has no focused Edit (see the file header comment).
    engine.getMidiLearnState().setActive (true);
    learningEdit->getParameterChangeHandler().parameterChanged (paramToMap, /*fromAutomation*/ false);
    pcm.listenToRow (pcm.getNumControllerIDs());

    FORGE_LOG_INFO ("MIDI-learn: listening for a CC to map to '" + paramToMap.getParameterName() + "'");
}

void MidiLearn::cancelLearn()
{
    if (! learning)
        return;

    learning = false;

    if (learningEdit != nullptr)
    {
        // Undo the native learn arming so a later stray CC can't bind or leave a junk row.
        learningEdit->getParameterControlMappings().listenToRow (-1);
        learningEdit->getParameterChangeHandler().getPendingParam (/*consumeEvent*/ true);
    }

    engine.getMidiLearnState().setActive (false);
    resetLearnState();

    FORGE_LOG_DEBUG ("MIDI-learn: cancelled");
}

//==============================================================================
void MidiLearn::handleIncomingController (int ccNumber, float value0to1, int channel)
{
    // During a learn, always drive the target parameter's own Edit; otherwise the live-apply Edit.
    te::Edit* edit = learning ? learningEdit : activeEdit;

    if (edit == nullptr)
        return;   // nothing to route to yet — silent no-op (this is a high-rate path; never log here)

    edit->getParameterControlMappings().sendChange (ccToControllerID (ccNumber), value0to1, channel);
}

//==============================================================================
void MidiLearn::midiLearnStatusChanged (bool isActive)
{
    // If MIDI-learn is turned off from elsewhere while we were mid-learn, abandon our learn cleanly.
    // (When WE deactivate it, `learning` is already false by this point, so this is a no-op then.)
    if (learning && ! isActive)
    {
        learning = false;
        resetLearnState();
    }
}

void MidiLearn::midiLearnAssignmentChanged (te::MidiLearnState::ChangeType type)
{
    if (type != te::MidiLearnState::added || ! learning)
        return;

    // The native store has just bound our pending param to the last CC. Finalise: clear `learning`
    // FIRST so the setActive(false) below doesn't re-enter this handler's sibling.
    auto bound = learningParam;   // keep alive for the callback even after resetLearnState()

    learning = false;
    engine.getMidiLearnState().setActive (false);
    resetLearnState();

    if (bound != nullptr)
    {
        FORGE_LOG_INFO ("MIDI-learn: mapped a CC to '" + bound->getParameterName() + "'");

        if (onLearnComplete != nullptr)
            onLearnComplete (*bound);
    }
}

void MidiLearn::resetLearnState()
{
    learningParam = nullptr;
    learningEdit  = nullptr;
}

//==============================================================================
int MidiLearn::controllerIDToCC (int controllerID) noexcept
{
    // Plain CCs are encoded 0x10000 | ccNumber (tracktion_MidiInputDevice.cpp:201). Anything at or
    // above 0x20000 is an NRPN/RPN/pressure controller, which is not a plain CC.
    return (controllerID >= 0x10000 && controllerID < 0x20000) ? (controllerID & 0x7f) : -1;
}

//==============================================================================
bool MidiLearn::isMapped (te::AutomatableParameter& param)
{
    return param.getEdit().getParameterControlMappings().isParameterMapped (param);
}

bool MidiLearn::getMappedCC (te::AutomatableParameter& param, int& ccNumberOut, int& channelOut)
{
    int channel = -1, controllerID = -1;

    if (! param.getEdit().getParameterControlMappings().getParameterMapping (param, channel, controllerID))
        return false;

    const int cc = controllerIDToCC (controllerID);

    if (cc < 0)
        return false;   // mapped, but to a non-CC controller

    ccNumberOut = cc;
    channelOut  = channel;
    return true;
}

bool MidiLearn::clearMapping (te::AutomatableParameter& param)
{
    return param.getEdit().getParameterControlMappings().removeParameterMapping (param);
}
