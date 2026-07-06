# W16 — frontier Wave 6: W05 QC debt discharge (undo + popout + lifetime hardening)

> Frontier build program, Wave 6. Baseline **`20500c1`**. Assert-only, `main.cpp`-only (plus one sanctioned
> exception — see below): discharges the QC debt W05's original adversarial pass never ran
> ("undo-correctness" + "shell-integration", flagged owed since `docs/devlog/wave-05-undo.md`). Zero new gate
> names — all 6 dimensions extend 4 existing gates (`--selftest-undo`, `--selftest-midi`, `--selftest-popout`,
> `--selftest-sendarrange`); floor stays **32**. A source-verification swarm froze the spec; building it
> **uncovered a confirmed, pre-existing engine defect** partway through, which forced two rounds of gate
> redesign plus one additional fix beyond the wave's original scope. Full record of both below.

## What shipped

Six dimensions, each proving the shell's **real** entry point (`doUndo()`/`doRedo()`, the popout's actual
`keyPressed` forward chain, `swapProject`) rather than the `ed->undo()`/`ed->redo()` bypass every pre-existing
gate in this file used:

- **`--selftest-undo`** — two new legs drive `doUndo()`/`doRedo()` across two independently-sealed shell
  mutations (mirroring `sessionView.onEditMutated` + `markerBar.onEditMutated`), proving per-gesture transaction
  isolation and that `reconcileDrawerClip()` fires on the real path (it only runs inside `undoOrRedo()`, never via
  a bare `ed->undo()`).
- **`--selftest-midi`** — a new phase (`midiSelftestUndoAttempt`) drives a real `doUndo()` mid-take, proving the
  record-gate no-op fires on this synthetic-but-real recording path, then the existing inject/capture legs
  continue unchanged (byte-identical `capturedNoteCount` baseline).
- **`--selftest-popout`** — three new sub-legs: undo/redo through the shell wrapper while both views are torn
  off (proving the refresh fan-out doesn't disturb popout ownership, and proving the dim-5 fix below); and the
  popout's **real** key-forwarding chain (`PopoutWindow::keyPressed` → `onUnhandledKey` →
  `MainComponent::keyPressed` → `doUndo()`/`doRedo()`), driven by literally calling `mixerPopout->keyPressed(...)`
  — never a direct call into `MainComponent::keyPressed`, which would bypass the very forwarding this dimension
  exists to prove.
- **`--selftest-sendarrange`** — a runtime-render leg: `Exporter::renderStems` (already used by the export UI) +
  the existing `readPeakMagnitude` helper sample the arrangement's actual audio output. Three-state
  (`PASS`/`FAIL`/`SKIP`) — `SKIP` is logged and non-blocking, never a fictional `PASS`.

## The confirmed defect (found building this wave, not anticipated by the frozen spec)

Dimension 1's first draft asserted `um.canRedo()` survives a real `doUndo()`. It didn't — a live, reproducible
trace (temporary diagnostic logging in `main.cpp` and four vendored engine files, all reverted before this
commit) isolated the cause to `FourOscPlugin::flushPluginStateToValueTree()`
(`libs/tracktion_engine/.../tracktion_FourOscPlugin.cpp:1393-1424`): it calls `state.addChild(mm, -1, um)`
**unconditionally** on every flush (i.e. every `session.save()`, which `doUndo()`/`doRedo()` always call) — even
with a completely empty mod-matrix (`modMatrixChildren=0` still triggered it). `ValueTree::addChild` has no
equality gate, so this is a genuine new tracked action on every save, which discards the pending redo stack and
becomes a new top-of-stack entry ahead of whatever the real undo/redo just did.

**Practical impact:** any edit containing a `FourOscPlugin` — Forge's own default instrument, auto-created on
every MIDI track — loses Redo immediately after any Undo, and a second Ctrl+Z can silently consume the phantom
action instead of the user's intended next step. This is **not fixed this wave**: the fix lives in vendored
engine code, out of scope for an assert-only/`main.cpp`-only wave, and per the project's standing "do not fork
the engine" default it's a **maintainer decision**, not something to patch unilaterally. See the new CLAUDE.md
gotcha for the full trace and the content-level assertion workaround every affected gate now uses.

Two gate legs (`--selftest-undo`'s multi-step undo/redo chain, `--selftest-popout`'s key-routing sub-legs) were
redesigned around this: assertions now check **content-level** state (slot occupancy, marker/note counts)
instead of `um.canUndo()`/`um.canRedo()`, which this defect makes unreliable immediately after a real
`doUndo()`/`doRedo()`. `--selftest-undo` carries a new `redoAvailableAfterSingleUndo` field — informational,
non-gating, logs a `FORGE_LOG_WARN` when false — so the defect stays monitored (a future engine fix would show up
as a signal) without making this wave's own gates artificially red for an out-of-scope bug.

## The second finding — a real regression in this wave's own fix, caught by QC

Dimension 5 (the piano-roll note-content-staleness fix, already known before this wave started) was fixed as
sanctioned: `undoOrRedo()` re-syncs the piano-roll's bound clip after every undo/redo so a note-content-only edit
(add/delete/move a note without touching clip parentage) never leaves a dangling `te::MidiNote&` reference. The
first version called `pianoRoll.setMidiClip(mc)` unconditionally — **adversarial QC caught that this rebuilds
unconditionally**, wiping the user's note selection and resetting the piano-roll's scroll position on **every**
app-wide undo/redo, not just ones touching that clip (e.g. undoing a Session scene rename while a MIDI clip
happened to be bound in the drawer). Fixed with a new `PianoRollView::refreshAfterExternalEdit()`: compares the
bound clip's live note set by `te::MidiNote*` identity against what's currently displayed (a structural
add/remove destroys-and-recreates the note object; a move/resize mutates it in place) and only calls the
destructive `rebuildNotes()` on a genuine structural divergence — everything else (including "nothing changed")
gets the cheap, non-destructive `layoutNotes()`, which touches neither selection nor scroll.

This is the second file touched beyond `main.cpp` this wave (`PianoRollView.h`/`.cpp`) — justified as refining
the ALREADY-sanctioned dimension-5 exception (fixing a regression in that same fix), not a new scope expansion.

## Proof — 4 gates extended, floor stays 32

All 32 pre-existing gates plus the 6 new/extended legs verified PASS; `--screenshot`'s 10-state matrix renders
unchanged. `redoAvailableAfterSingleUndo` reads `0` (confirming the documented defect is real and monitored, not
silently hidden) without failing the gate.

## Adversarial QC (5 dimensions)

- **dim5-fix-side-effects — MAJOR (fixed).** The `setMidiClip()`-based dim-5 fix silently wiped note selection
  (confirmed: `rebuildNotes()` unconditionally clears `selection`) and reset scroll (confirmed:
  `scrollToClipPitchRange()` runs unconditionally) on any unrelated undo/redo. Fixed via
  `refreshAfterExternalEdit()` (above); re-verified clean across the full floor.
- **dim1-gate-honesty — MINOR (doc debt, addressed here).** `tests/SELFTEST.md`'s `--selftest-undo` table
  predated the redesign — updated in this pass.
- **dim4-gate-redesign, dim2-dim6-mechanics, gate-ladder-regression — CLEAN.** Phase renumbering
  (`midiSelftestUndoAttempt` insertion), the twice-redesigned popout key-routing sub-legs, the render-audibility
  three-state semantics, and the gate-ladder/regression sweep (zero new gate names, no slot-index collisions, no
  stale call sites) all verified sound.

## Follow-ups (documented, not built)

- **The `FourOscPlugin` mod-matrix redo-wipe defect** — a real, monitored, out-of-scope engine bug (see the
  CLAUDE.md gotcha). Fixing it means patching vendored `libs/tracktion_engine` — a maintainer call, not something
  to do unilaterally in an assert-only wave.
- **The rest of the frontier program** continues at Wave 7 (performance recording — the real Session→Arrange
  bridge, which also appends one item to W15's scene-row PopupMenu).
