# W05 — global undo/redo + the polish sweep: Edit ▸ Undo/Redo · scene-row polish · strip-widget extraction · W04b deferrals

> Wave record, 2026-07-02. Baseline `7034955`. Two tracks under one wave: **global Undo/Redo** over the
> Edit's own `UndoManager` (menu + Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y, cross-surface refresh fan-out, a new
> **`--selftest-undo`** gate on a **sixteen-gate floor**), and the **W04c polish list** (scene-row hover /
> tooltips / STOP ALL / beat-pulse parity, the empty-centre hint, popout placement persistence, and the
> tray↔mixer strip-widget extraction). **QC was limit-interrupted** — see the honest note below.

## Process

2 source-verify dossiers (undo mechanics — transaction sealing, what is/isn't on the stack, the record-path
hazard; polish inventory — 4 items feasibility-checked with cited line evidence) → 3 partitions: **P-A**
scene-column polish and **P-C** strip-widget extraction ran as parallel file-disjoint agents; **P-B**
(the two main.cpp polish items) and the whole undo track were orchestrator-applied serially (main.cpp is
hard-stop territory). Integration → clean build → **16-gate floor + screenshots** → adversarial QC
(**interrupted by the session agent limit** — see below) → 2 recovered findings fixed by hand → floor re-run.

## What shipped

- **Global Undo/Redo** (`main.cpp` + `ui/menu/ForgeMenuModel.{h,cpp}`): **Edit ▸ Undo / Redo** with LIVE
  enablement (a new `enabledWhen` column in the command table, wired to `canUndo`/`canRedo` — greyed out
  when the stack is empty), plus Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y in the shell's `keyPressed`. The
  implementation is a thin shell over the Edit's own `UndoManager`:
  - **Granularity**: the engine's `UndoTransactionTimer` already seals a transaction 350 ms after each
    change burst (deferred while the mouse is down); the five `onEditMutated` hooks
    (arrange/detail/piano-roll/session/markers) now also seal eagerly (`beginNewTransaction`), so each
    user gesture is a discrete undo unit.
  - **`Edit::undo()`/`redo()`**, never the raw UM — keeps the engine's selection refresh in the loop.
  - **The record gate**: record-arm targets sit ON the undo stack (`setTarget`/`removeTarget` write
    through the UM) and `removeTarget` fails while recording — an undo mid-take would silently retarget
    the capture. Undo/redo is an explicit no-op with a status-strip message while recording.
  - **The refresh fan-out**: undo does NOT fire `onEditMutated` and no Forge view listens to the state
    tree, so after `undo()` the shell saves (disk must never stay newer than memory — a crash would
    "restore" the pre-undo state) then synchronously rebuilds: `arrangeView.rebuild()` (stale
    ClipComponents are a UAF otherwise), `sessionView.rebuild()`, `mixerView.refreshControls()` (its
    structural guard rebuilds strips if the track set changed), `channelTray.refreshNow()`,
    `markerBar.refresh()`. All synchronous on the message thread — no timer can interleave a stale deref.
  - **The detached-clip guard**: a redo-of-delete leaves the piano-roll holding a live but PARENTLESS
    `MidiClip::Ptr` (edits would write to a dead state tree, silently). The fan-out checks
    `state.getParent().isValid()` and closes the roll back to the Detail drawer if not.
  - `ensureScenes` stays deliberately OFF the stack (inhibitor + `clearUndoHistory` in
    `ProjectSession`) — grid growth is plumbing, not a user gesture. Do not "fix".
  - Gate: **`--selftest-undo`** — from a `clearUndoHistory` baseline, slot-clip create → delete → undo
    (clip back) → redo (gone again), with `canUndo`/`canRedo` transition asserts at every step, then a
    MIDI-note add undone on the resurrected clip (through the sequence's UM-aware `addNote`).
- **Scene-column polish** (P-A: `ui/session/SceneColumnComponent.{h,cpp}` + `SessionView.cpp` — the one
  W04 charter item still open): idle-hover lifts the row fill to `raisedBg` (neutral chrome, not a
  semantic accent); the row + ▶ button share a tooltip naming the hidden right-click stop; the stop-all
  button widened to full-width **"■ STOP ALL"** with the SCENES label relocated into the header; the
  queued/playing ring gains **beat-pulse parity with the pads** (`setScenePulse`, pushed from the same
  poll that pushes row state, with the pads' change-gate + no-pulse sentinel so static rows stay
  repaint-free).
- **Strip-widget extraction** (P-C: NEW `ui/common/StripWidgets.h` + `MixerView.cpp` + `ChannelTray.cpp`):
  the tray↔mixer styling duplication beyond W04b's PeakMeter — dB-fader / pan-knob / send-knob styling,
  shared range constants, `busLetter` — now lives once in `namespace forge::strip` as **style-only** free
  helpers (no `addAndMakeVisible` inside — `ReturnStrip::refresh()` manages visibility separately, and the
  style-only shape is the drift-proof one). Header-only: zero CMake edit, zero behaviour change.
- **The empty-centre hint** (P-B, a W04b deferral): tearing the mixer off WHILE in Mix view blanked the
  centre with no explanation (W04b only covered selecting Mix while already torn off). `paint()` now draws
  a centred hint ("Mixer is popped out - press F11 or View > Mix to bring it forward") whenever
  `viewMode == Mixer` with the popout live. ASCII only — see the QC finding below.
- **Popout placement persistence** (P-B, a W04b deferral): tear-off windows reopen where the user left
  them — `getWindowStateAsString()` saved per window (`forgeMixerPopoutState` / `forgePianoRollPopoutState`
  in the engine PropertiesFile) at the top of each restore AND at shell teardown (quit-with-popout-open),
  restored on tear-off via `restoreWindowStateFromString` (JUCE's parser includes off-screen rescue).
  The save runs while the window is still alive — restore defers destruction, so saving in the deferred
  lambda would read a dying window.

## Adversarial QC — LIMIT-INTERRUPTED (honest record)

The planned QC wave was three dimensions (**undo-correctness**, **shell-integration**,
**polish-regressions**), each finder feeding per-finding adversarial verifiers. The session hit the
subagent limit mid-wave: only the **polish-regressions finder** completed (2 raw findings, recovered from
the workflow transcript and self-adjudicated by the orchestrator); its verifiers and the other two
dimensions **never ran**.

| Sev | Finding (recovered) | Fix |
|---|---|---|
| major | The empty-centre hint rendered mojibake: a raw em-dash in a `char*` literal through `juce::String`'s ASCII-only ctor (draws garbage + jasserts every paint under a debugger) | ASCII hyphen; comment warns the ctor is ASCII-only |
| minor | The scene-row hover fill dropped out (and never re-lit) while the pointer crossed the ▶ launch button — a flicker over the row's primary click target | Child-inclusive `isMouseOverOrDragging (true)` + the button's mouse events routed through the row (`addMouseListener`) with explicit enter/exit repaints; right-click-stop now also works over the button, which the shared tooltip already promised |

**QC debt owed (first action of the next session):** run the **undo-correctness** and
**shell-integration** dimensions. The orchestrator's inline self-review found no issue in the fan-out
ordering (the whole undo path is synchronous message-thread code — no timer or paint can interleave
between `undo()` and the rebuilds) or in refreshing a torn-off mixer (its refresh is
parentage-independent), but a self-review is not adversarial verification — treat those two dimensions
as UNVERIFIED until the wave runs.

## Verified

Clean MSVC Debug build (0 warnings). **All SIXTEEN selftests PASS** on the final binary (post-fix floor
re-run); `--screenshot` renders the 9-state matrix. Known behavioural edges (by design, documented in
HANDOFF): track **mute/solo are NOT undoable** (the engine binds them with a null UM); **record-arm IS
on the stack** (an undo can disarm a track); undo history does not survive a project swap.

## Deferred follow-ups

The owed QC dimensions (above) · DetailView holds a detached audio clip after a redo-of-delete the same
way the piano-roll did — edits are safe no-ops (the Ptr keeps it alive) but a matching close-guard would
be tidier · scene rename (no affordance exists; a product decision, not polish) · the optional
strip-toggle adoption in `TrackColumnComponent`/`ArrangeView` (deferred to keep the blast radius small).
