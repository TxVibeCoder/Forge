#include "engine/ForgeUIBehaviour.h"
#include "services/files/ProjectSession.h"

//==============================================================================
// Both accessors resolve the focused Edit the same way: the app's currently-open Edit, via the
// ProjectSession. getEdit() returns null when no project is open, and `session` is null before the
// app wires it (and after teardown), so a missing project or an unwired/late callback both yield a
// safe null rather than a dangling dereference. This is deliberately trivial and allocation-free —
// it can be reached (via the engine's message-thread-marshalled parser) on every incoming CC, so it
// does NOT log (a knob sweep is high-rate; logging here would violate the "never log per-tick" rule).

te::Edit* ForgeUIBehaviour::getCurrentlyFocusedEdit()
{
    return session != nullptr ? session->getEdit() : nullptr;
}

te::Edit* ForgeUIBehaviour::getLastFocusedEdit()
{
    return session != nullptr ? session->getEdit() : nullptr;
}
