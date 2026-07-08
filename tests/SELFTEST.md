# Forge self-test contract

Forge drives itself headless for CI-free verification. Each mode writes a key=value report
to `%TEMP%\forge_phase0_selftest.log` then quits. `result=PASS|FAIL` is the bottom line.

Since the logging subsystem (`src/core/Log.*`, see [../docs/LOGGING.md](../docs/LOGGING.md)) landed, every
`FORGE_LOG_*` line also **echoes to stderr**, so a headless run surfaces its full diagnostic trace (startup
banner, device, phase boundaries, any failure) on the console with no debugger attached — and each report now
appends a `logFile=<path>` line pointing at the persistent log (`%APPDATA%\Forge\logs\forge.log`).

## `Forge --selftest` (playback / arrange path)

Imports a generated 440 Hz tone onto track 1, plays it, then samples state at ~3s.

| field | meaning | PASS requires |
|---|---|---|
| `device` | current audio output device name | non-empty (device open) |
| `sampleRate` / `bufferSize` | device config | — |
| `editFile` | project path on disk | — |
| `editLoaded` | 1 if an existing project was loaded (vs newly created) | — |
| `numTracks` | audio track count | ≥ 1 |
| `importedClip` | 1 if the tone imported as a clip | 1 |
| `clipLengthSecs` | imported clip length | > 0 |
| `numClipComponents` | clip rects built on track 1 | ≥ 1 |
| `playheadX` | playhead pixel x for the current position | — (informational) |
| `transportReadout` | 1 if the transport bar readout has text | — (informational) |
| `hasContext` | 1 if the playback context is allocated | — |
| `playing` / `position` | transport state | `playing==1` OR `position>0.05` |

## `Forge --selftest-record` (recording path)

**Event-driven** (mirrors the real arm path, which must NOT block the message loop): phase 1 opens
the capture input lazily (`ensureRecordingInputOpen`) and yields; phase 2 (≈200 ms later, after
Tracktion's async wave-input-list rebuild has delivered) arms track 1 + records; phase 3 (≈1.5 s
later) stops keeping the take, then verifies a clip on disk. Doing this synchronously in one callback
silently starves the async rebuild (`getNumWaveInDevices()` stays 0) — that was the old harness's
false FAIL; see `docs/devlog/device-recording.md`.

| field | meaning | PASS requires |
|---|---|---|
| `availableInputDevices` | OS-visible capture endpoint names | — (diagnostic) |
| `openInputError` / `deviceAfterOpen` / `inputChansAfterOpen` | lazy-open diagnostics | — |
| `inputDeviceCount` | engine wave-in devices | > 0 |
| `trackArmed` | input assigned + armed to track 1 | 1 |
| `recordingStarted` | transport entered record | 1 |
| `recordedClipCount` | clips on track 1 after stop | ≥ 1 |
| `recordedFileExists` | the take's source file exists | 1 |
| `recordedClipLengthSecs` | take length | > 0 |
| `recordedPeakMagnitude` | peak |sample| of the captured file | — (informational; > 0 ⇒ real signal flowed) |
| `recordError` | last setTarget/arm error, if any | empty |

> Note: on a machine whose active audio config exposes no capture endpoint at all (e.g. a Bluetooth
> A2DP output headset with no mic), `inputDeviceCount=0` and this test FAILs — that is an environment
> limitation, not a Forge bug. Select an input via the **Audio** dialog. On a box with a working input
> this test PASSes and captures a real take (verified 2026-06-30: clip + file + length + non-zero peak).

## `Forge --selftest-session` (Session clip-grid launch / audibility path)

The wave-1 acceptance gate for the [Session clip grid](../docs/devlog/session-design.md). **Event-driven**:
yields to the message loop, grows the grid to 16 scenes (`ensureScenes`), creates a born-audible MIDI clip
in slot `(track 0, scene 0)` (`createMidiClipInSlot` → default 4OSC), launches it (`launchSlot`, which starts
the transport), waits ≈1.5 s for the launcher to engage, then verifies the clip's `LaunchHandle` reached the
`playing` state with the transport rolling — i.e. the playback graph actually routes the launched slot
(proves clip launch is AUDIBLE, not merely queued).

| field | meaning | PASS requires |
|---|---|---|
| `numScenes` | grid rows after `ensureScenes(16)` | ≥ 16 |
| `sessionColumns` | track columns the `SessionView` built | — (informational) |
| `clipCreated` | 1 if the MIDI clip was inserted into the slot | 1 |
| `slotHasClip` | 1 if the slot resolves to a clip after launch | 1 |
| `hasLaunchHandle` | 1 if the clip exposes a `LaunchHandle` | 1 |
| `transportPlaying` | 1 if `launchSlot` started the transport | 1 |
| `clipPlaying` | 1 if the launch handle reached `PlayState::playing` | 1 |

> Verified 2026-06-30: PASS on a box with a working output device — the launched slot's handle reaches
> `playing` with the transport rolling, confirming the launcher playback path engages.

## `Forge --selftest-midi` (MIDI record into a Session clip slot)

The acceptance gate for **W7 — MIDI record into Session clip slots** (design:
[../docs/devlog/midi-record-design.md](../docs/devlog/midi-record-design.md)). It is the **first proof of
verdict-A direct `ClipSlot` recording** — the design's record path was "untested in-engine"; this gate makes it
empirical. **Event-driven** (mirrors the record/session yield discipline), it proves MIDI capture straight into
a Session `ClipSlot` with **zero hardware**:

- **Phase 1** — grows the grid to 16 scenes (`ensureScenes`), asserts slot `(0,0)` is EMPTY
  (`preExistingNotes=0`), ensures a born-audible default 4OSC on track 0, creates a `VirtualMidiInputDevice`
  (`createVirtualMidiDevice`, async), then YIELDS.
- **Phase 2** — finds the virtual device by name, `setEnabled(true)` + `setMonitorMode(automatic)`,
  `ensureContextAllocated`, arms **only the slot** `(0,0)` (`setTarget(slot.itemID, /*moveToTrack=*/false)` +
  `setRecordingEnabled`), rolls the transport (`transport.record(false)`), then YIELDS.
- **Phase 3 (W16 — owed W05 debt, dimension 2)** — `midiSelftestUndoAttempt()`: attempts a `doUndo()` via the
  **real shell wrapper** (never a raw `ed->undo()`) WHILE genuinely (synthetically) recording, proving the
  record-gate no-op (`undoOrRedo`'s `isRecording()` early-return) actually fires on this real-but-synthetic
  recording path — not just a state-level assertion — before continuing to note injection. Then YIELDS.
- **Phase 4** — injects 4 deterministic notes (C4/E4/G4/C5) via
  `handleIncomingMidiMessage(msg, getMPESourceID())`, then YIELDS.
- **Phase 5** — YIELDS after injecting matching note-offs.
- **Phase 6** — `transport.stop` (the engine commits the captured notes into a new `MidiClip` in the slot via
  `addMidiAsTransaction`, never a take), disarms the slot, re-resolves the slot's clip, counts its notes,
  `deleteVirtualMidiDevice` (mandatory — a leaked name fails the next run), writes the report, and quits.

| field | meaning | PASS requires |
|---|---|---|
| `availableMidiInputs` | names of engine MIDI-in devices (incl. the virtual one) | — (diagnostic) |
| `midiDeviceEnabled` | 1 if the virtual MIDI input was enabled | 1 |
| `preExistingNotes` | notes already in slot `(0,0)` before recording | **0** (slot must start empty) |
| `trackArmed` | 1 if the MIDI input armed to the **slot's** `itemID` (slot armed, not the track) | 1 |
| `recordingStarted` | 1 if the transport entered record | 1 |
| `stillRecordingAfterUndoAttempt` (W16) | the transport is STILL recording immediately after a real `doUndo()` attempt mid-take | 1 |
| `undoStackUnchangedDuringRecord` (W16) | `um.canUndo()` unchanged before vs. after the attempt — nothing silently consumed | 1 |
| `undoBlockedWhileRecording` (W16) | the status-label text confirms the `isRecording()` early-return branch specifically fired (not some other early-return) | 1 |
| `notesInjected` | count of synthetic notes injected while rolling | ≥ 4 |
| `clipCreated` | 1 if a `MidiClip` materialised in the slot on stop | 1 |
| `capturedNoteCount` | notes in the committed slot clip's sequence | **`== notesInjected`** (EXACT — proves the mid-take undo attempt didn't corrupt or retarget the capture) |

### PASS criteria (all must hold)
```
midiDeviceEnabled && trackArmed && recordingStarted
  && preExistingNotes == 0
  && notesInjected >= 4
  && clipCreated
  && capturedNoteCount == notesInjected      // EXACT, not >=
```
The `capturedNoteCount == notesInjected` equality (not `>=`) is the false-pass defense: it catches an
empty-but-present clip, a wrong-target capture (notes into the track instead of the slot), and any pre-seeded
notes.

> Verified 2026-07-01: **PASS** — the four injected notes land **exactly** in the slot's committed clip
> (`capturedNoteCount == notesInjected == 4`, `preExistingNotes == 0`), the first in-engine proof that Forge
> records MIDI directly into a Session `ClipSlot` (verdict A).

## `Forge --selftest-midilearn` (MIDI-learn CC → plugin param bind)

The acceptance gate for **Wave 01 P2 — MIDI-learn** (design: [../docs/devlog/wave-01-features.md](../docs/devlog/wave-01-features.md)).
First runtime proof that the `MidiLearn` seam binds a MIDI CC to a plugin parameter over Tracktion's native
`ParameterControlMappings` **with no focused Edit** — the crux of the seam's design (the default `te::UIBehaviour`
returns a null focused Edit, so the seam drives an *explicit* Edit's mappings). Injection goes **through the seam**
(`MidiLearn::handleIncomingController`), not a virtual MIDI device — a virtual device's CC never reaches the
engine's controller parser, so the seam is the same entry a real MIDI-input listener would use.

- Ensures a born-audible 4OSC on track 0 and picks its first automatable param (`Tune 1`).
- Arms a learn on that param, then injects **CC 74 / channel 1** through `handleIncomingController`.
- Yields for the native `AsyncUpdater` bind, then asserts the param is mapped to exactly CC 74 / channel 1.

| field | meaning | PASS requires |
|---|---|---|
| `wasMappedBefore` | param already mapped before the learn | **0** |
| `learnArmed` | 1 if `beginLearn` armed on the target param | 1 |
| `isMappedAfter` | 1 if the param is mapped after injecting the CC | 1 |
| `mappedCc` | the CC number the param bound to | **74** |
| `mappedChannel` | the MIDI channel the param bound to | **1** |

### PASS criteria (all must hold)
```
learnArmed && wasMappedBefore == 0 && isMappedAfter && mappedCc == 74 && mappedChannel == 1
```

> Verified 2026-07-01: **PASS** — the param binds to exactly CC 74 / ch 1 with no focused Edit. **Mode-detection
> note:** the parse ternaries check `--selftest-midilearn` **before** `--selftest-midi` (the latter is a substring
> of the former). **Not covered headlessly:** completion from *real hardware* CC — the deferred `ForgeUIBehaviour`
> / MIDI-input-listener follow-up; this gate proves the seam's bind, and Ctrl+L arms it live in the app.

## `Forge --selftest-midiinput` (focused-Edit MIDI-learn hardware routing)

The acceptance gate for **W02 item 2a — MIDI-learn hardware routing** (design:
[../docs/devlog/wave-02-features.md](../docs/devlog/wave-02-features.md)). Asserts the `ForgeUIBehaviour`
`te::UIBehaviour` subclass reports the app's open Edit as the **focused Edit** (the missing link the engine's
native CC→param routing keys off — the default returned null), and that a CC→param bind lands through it. This
closes the Wave-01 deferral so a focused Edit exists for the engine's `ParameterControlMappings` to route into.

**Asserts (both must hold):** `ForgeUIBehaviour::getLastFocusedEdit()` reports the app's open Edit, and a
CC→param bind lands through that focused-Edit path.

> **Not covered headlessly:** a real hardware CC actually driving a param — a `VirtualMidiInputDevice` has NO
> `controllerParser` (only `PhysicalMidiInputDevice` does), so the physical-CC path is a **real-hardware smoke
> item**. This gate proves the focused-Edit plumbing the routing depends on.

## `Forge --selftest-controlsurface` (Forge-native grid control surface)

The acceptance gate for **W02 item 3 — the Forge-native control surface** (design:
[../docs/devlog/wave-02-features.md](../docs/devlog/wave-02-features.md)). Proves the driver seam end-to-end with
**zero hardware**: a virtual pad-press launches slot `(0,0)` (via `ProjectSession::launchSlot` directly — Option
B, since Tracktion's `ControlSurface` clip-launch is an unwired `std::function`), and one LED poll emits the exact
expected note-on from `SlotVisualState::toPadFeedback`.

**Asserts (both must hold):** a virtual pad-press launches slot `(0,0)`, and one LED poll emits the exact expected
note-on for that pad.

> **Not covered headlessly:** the real-device byte mapping / LED palette — the Launchpad driver is built to the
> published MIDI spec with no hardware on hand, so the exact bytes are a **real-hardware smoke item**.

## `Forge --selftest-lufs` (offline BS.1770-4 loudness)

The acceptance gate for **W02 item 4 — offline LUFS** (design:
[../docs/devlog/wave-02-features.md](../docs/devlog/wave-02-features.md)). Runs the self-contained BS.1770-4
integrated-loudness analyzer (K-weighting biquads, 400 ms/75%-overlap gating, −70 LUFS abs + −10 LU rel gates) on
a known signal and checks it against the spec reference: a mono full-scale 1 kHz sine measures **−3.00 LUFS**.

**Asserts:** (buffer leg) the analyzer's integrated loudness for a mono full-scale 1 kHz sine is **−3.00 LUFS
within ±0.5 LU**; (file+thread leg, W03) `analyzeFile` run on a spawned worker thread — the exact static path
the async export worker executes — agrees with the buffer-fed result within **0.1 LU**; (abort leg, W03) an
always-true abort predicate returns the silence sentinel promptly, proving the cancel/teardown guard.

> The ±0.5 LU tolerance is the BS.1770-4 reference-signal check. **Live master LUFS is not offered:** the
> read-only `tracktion_engine` submodule exposes no post-fader sample tap and JUCE sums secondary callbacks, and
> integrated loudness is a whole-program measurement — so the analysis runs on the export render (offline), which
> is the correct tool. Since W03 that analysis runs **on the render worker thread** (the UI never blocks); the
> measured integrated LUFS is surfaced in the export-done status strip.

## `Forge --selftest-automation` (volume-curve automation read)

The acceptance gate for **W03 — automation lanes** (design:
[../docs/devlog/wave-03-features.md](../docs/devlog/wave-03-features.md)). Imports a tone on track 0, writes a
falling 2-point volume curve through the `AutomationHelpers` seam (fader position 0.8 @ 0 s → 0.2 @ 2 s,
linear), forces the read stream live (`updateStream` — activation is otherwise deferred to a 10 ms engine
timer), rolls the transport, and polls `volParam->getCurrentValue()` at 10 Hz (bounded ~4 s, looping off).

### PASS criteria (all must hold)
- preconditions: curve active, exactly 2 points, static `getValueAt(1.5 s)` = 0.35 ± 0.01, automation read ON;
- the FIRST poll tick (early sample) reads **≥ 0.7**;
- a tick in the 1.5–2.4 s window (late sample) reads **≤ 0.45** — the curve demonstrably drives the parameter
  during playback.

## `Forge --selftest-sync` (MIDI-clock output, end-to-end)

The acceptance gate for **W03 — MIDI-clock out** (design:
[../docs/devlog/wave-03-features.md](../docs/devlog/wave-03-features.md)). Captures the engine's ACTUAL clock
bytes: a probe subclass of `te::MidiOutputDevice` overrides the virtual `sendMessageNow` (the seam that writes
to the wire, downstream of the real graph + 24 PPQN generator + 1 ms dispatcher) and logs every message. The
gate freezes the periodic MIDI rescan, evicts the engine's same-NAME device entry (restored at teardown),
injects the probe as sole owner of a real system MIDI out (on stock Windows: the Microsoft GS software synth),
rolls ~2 s, stops, yields 300 ms for the dispatcher to flush the stop edge, then verifies. The real device's
persisted props (shared by NAME with the probe) are snapshotted and losslessly restored.

### PASS criteria (all must hold)
- probe enabled + its port actually opened (checked via the underlying device, never `openDevice()`'s
  return string) + `setSendingClock` round-trips + transport rolled;
- captured: ≥ 1 song-position pointer, ≥ 1 start/continue, clock count within **0.5×–1.5×** of
  `seconds × (bpm/60) × 24`, ≥ 1 stop after the stop edge.
- **Zero-MIDI-outs machines degrade honestly** (`skip-degraded=1`): property round-trip + no-crash roll only —
  the gate never claims clock-out it didn't capture.

## `Forge --selftest-livesync` (cross-surface live refresh)

The acceptance gate for **W03 — live cross-surface refresh** (design:
[../docs/devlog/wave-03-features.md](../docs/devlog/wave-03-features.md)). Writes engine-side values exactly as
another surface would, then forces one sync tick through the views' deterministic test seams (the mirrors of
their poll timers).

### PASS criteria (all must hold)
- mixer leg: after `setTrackVolumeDb(−12)` + `setMute(true)` and one `refreshControls()`, strip 0's fader reads
  **−12 dB ± 0.15** (0.1 slider snap + fader-curve round-trip) and its mute reads true;
- inspector leg: after `setGainDB(−6)` and one `refreshNow()`, the gain slider reads **−6 dB ± 0.1** —
  no re-select, no rebuild.

## `Forge --selftest-lcd` (transport LCD model + pad pulse curve)

The acceptance gate for **W04a — the LCD + sequence lighting** (design:
[../docs/devlog/wave-04a-ux.md](../docs/devlog/wave-04a-ux.md)). A **pure model gate** (no engine /
edit / device — runs and quits before the window, like `--selftest-lufs`): asserts the
`forge::lcd::computeLcdState` acceptance table and the `padPulseAlpha` lighting curve.

### PASS criteria (all must hold)
- idle face: position "1|1", tempo "120.0", "C · 4/4", no count-in, phase 0; playing at 6.5 beats
  reads "2|3" with phase 0.5;
- count-in digits derive from the **click grid** (whole timeline beats): the aligned N=4 table
  (lead-in 0 → 1 → 3 → 4 → punched) AND a **non-aligned punch at beat 2.3** (digit flips ON click
  beats −1/0/1/2 — the QC-caught desync case);
- skeptic guard 1 (a mid-playback record never lights the count-in face) and guard 2 (epsilon at
  exact click boundaries) hold;
- the pulse curve: playing 1.0→0.55 across the beat, queued 0.35..0.75 over two beats, recording
  unmodulated 1.0, non-animated states 0, out-of-range phases fold.

## `Forge --selftest-menu` (menu-bar model)

The acceptance gate for **W04a — the menu bar**. Pure model gate: dispatches through a BARE model
(every callback unset — must no-op, never crash), then asserts the tree shape (5 menus, pinned item
counts), non-zero item ids, known shortcut labels (Save = Ctrl+S, Open = Ctrl+O — display-only
strings that must not drift from `keyPressed`), callback dispatch (flag capture), and live tick
marks from the query functions (unset query = unticked).

## `Forge --selftest-tray` (channel-tray live sync)

The acceptance gate for **W04a — the channel tray**. Mirrors `--selftest-livesync`: import a tone,
bind track 0, write volume −9 dB + mute engine-side, force one `refreshNow()` tick.

### PASS criteria (all must hold)
- the tray reports bound; the fader reads −9 dB ± 0.15 and mute reads true after one sync tick;
- `setTrack (nullptr)` lands in the empty state ("Select a track").

## `Forge --selftest-popout` (tear-off panel round-trip)

The acceptance gate for **W04b — tear-off panels** (design:
[../docs/devlog/wave-04b-ux.md](../docs/devlog/wave-04b-ux.md)). Turn 1: both views are docked
shell children → tear each off into a real visible `PopoutWindow` (the content becomes a direct
child of the window), force one live mixer sync tick while popped out, then drive the REAL close
path (`closeButtonPressed` → restore, which defers each window's destruction). Turn 2, after the
deferred resets ran:

### PASS criteria (all must hold)
- parentage out (each view's parent == its popout) and back (parent == the shell, windows gone);
- **noGhostOverlay** — BEFORE any rescue relayout, under the Session+Detail default both restored
  views are already hidden and the piano-roll holds no keyboard focus (the QC blocker: a restored
  view overlaying the shell at stale popout bounds);
- after driving Mixer view / drawer+PianoRoll state, both views read visible in their home slots.

### W16 additions (owed W05 debt, dimensions 3+4+5) — undo/redo while popped out + real key-routing

Three sub-legs run inside the SAME popped-out turn, before the real close path:

- **dims 3+5 — undo-refresh-into-popout / the piano-roll-staleness fix's proof.** Seeds a note-content-ONLY
  mutation (add then undo a single note) on a clip bound into the popped-out piano roll, driven through the
  REAL `doUndo()`/`doRedo()` wrapper. Proves (a) the refresh fan-out doesn't disturb popout ownership
  (`mixerView`/`pianoRoll`'s parent stays the popout window throughout), and (b) the piano-roll's own note view
  stays in sync via `PianoRollView::refreshAfterExternalEdit()` rather than holding a stale, dangling
  `te::MidiNote&` (the confirmed dimension-5 defect this fix closes — see the CLAUDE.md gotcha).
- **dim 4 — popout-key-routing-gate.** Drives the REAL forward chain — `PopoutWindow::keyPressed` →
  `onUnhandledKey` → `MainComponent::keyPressed` → `doUndo()`/`doRedo()` — by literally calling
  `mixerPopout->keyPressed(Ctrl+Z)` / `pianoRollPopout->keyPressed(Ctrl+Y)` (never a direct call into
  `MainComponent::keyPressed`, which would bypass the very forwarding this dimension proves). The undo and redo
  sub-legs are DELIBERATELY isolated (separate seeded mutations, separate `clearUndoHistory()` calls) — chaining
  them on the SAME mutation would hit the FourOscPlugin mod-matrix-flush defect twice over (see the CLAUDE.md
  gotcha): an undo's own `session.save()` discards the redo entry it just created before a following Ctrl+Y could
  ever use it. The redo sub-leg instead seeds its OWN mutation and reverts it via a direct `ed->undo()` (bypassing
  `session.save()` and the phantom action entirely), leaving a genuinely redoable stack for the Ctrl+Y to prove.

| field | meaning | PASS requires |
|---|---|---|
| `undoNoParentDisturbed` | popout ownership survives a real doUndo()/doRedo() fan-out | 1 |
| `undoNoCrash` / `redoNoCrash` | reaching past the note-content undo/redo without a jassert/crash | 1 / 1 |
| `rollStillBoundAfterUndo` | the piano-roll's bound clip pointer survives (not nulled) | 1 |
| `pianoRollNoteCountMatchesAfterUndo` | the clip's engine-level note count matches the pre-mutation baseline | 1 |
| `keyRoutedToShell` | `mixerPopout->keyPressed(Ctrl+Z)` returned true (consumed, forwarded) | 1 |
| `undoFiredThroughPopoutKey` | CONTENT-level: the seeded clip is actually gone after the key fires (not `um.canUndo()` — see the gotcha) | 1 |
| `redoFiredThroughPopoutKey` | CONTENT-level: the isolated, freshly-seeded clip is back after Ctrl+Y | 1 |

> `pianoRollNoteCountMatchesAfterUndo` proves engine-level correctness; it does NOT by itself prove the UI's
> `noteComps` avoided a dangling reference (that would need a test-only accessor into `PianoRollView`, deliberately
> not added — see the frozen spec's own reasoning). `undoNoCrash`/`redoNoCrash` are the probabilistic UI-level
> proxy: reaching past a real undo/redo of a note-content clip without a fault is the practical, available signal.

## `Forge --selftest-undo` (global undo/redo round-trip)

The acceptance gate for **W05 — global Undo/Redo** (design:
[../docs/devlog/wave-05-undo.md](../docs/devlog/wave-05-undo.md)). Proves the undo/redo round-trip against
the Edit's own `UndoManager`, all synchronous message-thread work. Explicit `beginNewTransaction` calls make
the gate deterministic (the engine's 350 ms auto-seal timer needs message-loop time); `clearUndoHistory`
first gives a guaranteed clean baseline whatever ran before.

| field | meaning | PASS requires |
|---|---|---|
| `baselineClean` | no undo/redo available after `clearUndoHistory` | 1 |
| `clipCreated` | T1: a born-audible MIDI clip created in slot (0,0) | 1 |
| `canUndoAfterCreate` | the create landed on the stack | 1 |
| `emptyAfterDelete` | T2: the clip deleted — slot empty | 1 |
| `filledAfterUndo` | `Edit::undo()` brings the clip back | 1 |
| `canRedoAfterUndo` | the redo leg is armed | 1 |
| `emptyAfterRedo` | `Edit::redo()` removes it again | 1 |
| `noteLeg` | on the resurrected clip: a UM-tracked `addNote` is undone (count returns, `canRedo` true) | 1 |

### W16 additions (owed W05 debt, dimension 1 — undo-shell-hooks-gate)

Every leg above drives `ed->undo()`/`ed->redo()` **directly** — exactly the bypass flagged as owed debt: Ctrl+Z
and the Edit menu only ever call the shell's `doUndo()`/`doRedo()` wrapper (`undoOrRedo()`), which ALSO fans a
cross-surface refresh out (`arrangeView.rebuild()`, `mixerView.refreshControls()`, `reconcileDrawerClip()`, …)
that a raw `ed->undo()` never exercises. This addition drives the REAL wrapper across two independently-sealed
shell mutations (mirroring `sessionView.onEditMutated` + `markerBar.onEditMutated`) to prove per-gesture
transaction isolation and that `reconcileDrawerClip()` fires on the real path.

| field | meaning | PASS requires |
|---|---|---|
| `shellPathASealed` | a Session-grid clip create (slot (0,1)), its own sealed transaction | 1 |
| `shellPathBSealed` | a marker add, a SEPARATE sealed transaction | 1 |
| `shellUndoRevertsOnlyMarker` | one real `doUndo()` reverts ONLY the marker (most recent transaction) — the clip-clear from an interleaved third mutation stays in effect | 1 |
| `drawerReconcileOnRealPath` | the piano-roll drawer, bound to the (now-detached) clip, gets nulled by `reconcileDrawerClip()` — a path ONLY reachable via `undoOrRedo()`, never a bare `ed->undo()` | 1 |
| `redoAvailableAfterSingleUndo` | **informational, NON-GATING** — see below | not gated |

> **`redoAvailableAfterSingleUndo` is deliberately excluded from PASS/FAIL.** A confirmed, pre-existing engine
> defect (`FourOscPlugin::flushPluginStateToValueTree()` unconditionally wipes the redo stack on every save — see
> the CLAUDE.md gotcha) makes `um.canRedo()` unreliable immediately after any real `doUndo()`, for any edit
> containing Forge's own default instrument. This field reads `0` today and is expected to keep doing so until
> the engine defect is fixed (a maintainer decision, out of scope for this wave) — it is logged
> (`FORGE_LOG_WARN`) so the defect stays monitored rather than silently hidden, without making this gate
> artificially red for a bug this wave didn't introduce and can't fix in `main.cpp`.

> Verified 2026-07-06: **PASS** (all 12 gating fields). Known behavioural edges (by design, not gate-covered):
> track **mute/solo are NOT undoable** (the engine binds them with a null UndoManager); **record-arm IS on the
> undo stack** (the shell blocks undo while recording — see `--selftest-midi`'s W16 dimension-2 addition);
> undo history does not survive a project swap; `sessionView.rebuild()` does not execute during this gate
> (`SelfTest::undo` fails `sessionViewBinds()`) — an accepted, out-of-scope residual, not silently dropped.

## `Forge --selftest-taptempo` (tap-tempo model + tempo-write seam)

The acceptance gate for **hands-on 1.4 — the clickable tempo popup** (design:
[../docs/devlog/wave-06-handson.md](../docs/devlog/wave-06-handson.md)). Two headless legs. **Leg 1**
(pure): drives the engine-free `forge::transport::TapTempo` estimator with synthetic timestamps —
`<2` taps yield no estimate, four taps 500 ms apart read **120.0 BPM**, a `>2000 ms` gap starts a fresh
sequence (no estimate), and two taps 10 ms apart clamp to the **300 BPM** ceiling. **Leg 2** (engine):
writes a tempo through `EngineHelpers::setTempoAt` and reads it back off `tempoSequence.getBpmAt` — a
`140.0` write round-trips, and a below-floor `5.0` write clamps to **20.0**.

| field | meaning | PASS requires |
|---|---|---|
| `oneTapNull` | a single tap yields no BPM | 1 |
| `bpm120` | four taps 500 ms apart → 120.0 | 1 |
| `gapReset` | a >2000 ms gap clears the sequence | 1 |
| `clampHigh` | a too-fast tap clamps to 300 | 1 |
| `engineWrite` | `setTempoAt(…,140)` reads back 140 | 1 |
| `engineClamp` | `setTempoAt(…,5)` reads back the 20 BPM floor | 1 |

> Verified 2026-07-02: **PASS**. The popup's UI (CallOutBox, editable label, ±steppers, TAP button) is
> screenshot/interaction territory; this gate proves the tap math and the clamped engine write it drives.

> **`--selftest-session` also gained a `launchQRoundTrip` field** (hands-on 1.3): after the launch, the
> global launch-quantization seam is round-tripped — `setGlobalLaunchQuantisation(none)`→reads `none`,
> `→(bar)`→reads `bar` — proving the free-trigger selector's engine seam. The free-vs-quantized launch
> *timing* is pre-existing engine behavior (the ticket only exposes the existing `Edit`-level global).

> **`--selftest-session` also gained a `perClipLaunchQ` field** (W12, frontier Wave 2): on the fixture clip in
> slot (0,0), with the global at `bar`, a per-clip override is set to `none` and the **real** launch resolver is
> proven to honour it — `resolveEffectiveLaunchQType(0,0) == none` while `getGlobalLaunchQuantisation() == bar` —
> then `clearClipLaunchQuantisation` reverts it and it inherits `bar` again. Asserting through
> `resolveEffectiveLaunchQType` (which delegates into the exact file-local resolver the live launch feeds) proves
> precedence through the real path, not a mirror. No new gate name — the floor stays 26.

## `Forge --selftest-slotdelete` (Session clip delete)

The acceptance gate for **W07 — Delete clip** (design:
[../docs/devlog/wave-07-handson-grid.md](../docs/devlog/wave-07-handson-grid.md)). Proves
`ProjectSession::clearSlot` synchronously: create a born-audible MIDI clip in slot (0,0), assert filled,
`clearSlot` → empty, `clearSlot` again → **false** (the no-op-when-empty contract), then `Edit::undo()`
restores the clip (the delete rides the Edit UndoManager — the same stack W05 global Undo drives).

| field | meaning | PASS requires |
|---|---|---|
| `clipCreated` | the MIDI clip inserted into the slot | 1 |
| `filledBefore` | slot resolves to a clip pre-delete | 1 |
| `cleared` | `clearSlot` returned true | 1 |
| `emptyAfter` | slot empty after clear | 1 |
| `clearEmptyIsNoop` | a second `clearSlot` on the empty slot returns false | 1 |
| `undoRestored` | `undo()` brings the clip back | 1 |

## `Forge --selftest-addtrack` (append a Session track)

The acceptance gate for **W07 — + Track**. Proves `ProjectSession::appendAudioTrack`: the audio-track count
increments by exactly 1, and a slot on the newly-appended (last) track resolves + accepts a born-audible clip
(the new column is a real, addressable track — not a phantom).

| field | meaning | PASS requires |
|---|---|---|
| `tracksBefore` / `tracksAfter` | audio-track count around the append | `after == before + 1` |
| `appended` | `appendAudioTrack` returned a track | 1 |
| `newSlotResolves` | a slot on the new track resolves | 1 |
| `clipOnNewTrack` | a born-audible clip creates in the new track's slot | 1 |

## `Forge --selftest-scene` (dynamic scene count)

The acceptance gate for **W07 — + Scene**. Proves the grid handles a scene count ≠ 16: `ensureScenes(20)`
grows `getNumScenes()` past the former `constexpr=16` ceiling, and a clip created in scene 18 resolves. (The
UI's dynamic-N *rendering* — that rows 16+ paint + stay aligned — is proved by the `session_scenes` screenshot.)

| field | meaning | PASS requires |
|---|---|---|
| `scenesBase` | scene count before the grow | — (informational) |
| `scenesGrown` | scene count after `ensureScenes(20)` | ≥ 20 |
| `grewPast16` | grew past the former ceiling | 1 |
| `slot18Resolves` | slot (0,18) resolves | 1 |
| `clipInScene18` | a clip creates in scene 18 | 1 |

## `Forge --selftest-dragdrop` (file drag-drop import paths)

The acceptance gate for **W07 — real file drag-drop** (Session pads + Arrange lanes). A real OS drag can't be
synthesized headlessly, so this gate proves the seam paths both drops route through, plus the replace-undo
contract. **Session leg:** `importAudioIntoSlot` fills a slot; a SECOND import replaces the clip and `undo()`
restores the prior one (replace-on-drop is undoable — the QC-F2 hardening). **Arrange leg:**
`importAudioFile(file, time, trackIndex)` lands the clip on track N (a fresh empty target ends with exactly
one clip, proving the track-index routes and not to track 0). The Arrange pointer→time math is the pure,
independently unit-testable `TimelineView::xToTime`.

| field | meaning | PASS requires |
|---|---|---|
| `sessionImported` | `importAudioIntoSlot` returned a clip | 1 |
| `sessionSlotFilled` | the slot resolves to a clip | 1 |
| `replaceUndoRestores` | a replace-on-filled drop is undone → the slot stays filled | 1 |
| `arrangeImported` | `importAudioFile(…, trackIndex)` returned a clip | 1 |
| `arrangeLandedOnTarget` | the clip landed on the target track (not track 0) | 1 |

> Verified 2026-07-02: **all four W07 gates PASS**, bringing the floor to **21 gates**.

## `Forge --selftest-sessionmixer` (per-track Session mixer strip sync)

The acceptance gate for **W08 — the per-track Session mixer strip** (design:
[../docs/devlog/wave-08-session-mixer.md](../docs/devlog/wave-08-session-mixer.md)). Mirrors
`--selftest-livesync` / `--selftest-tray`: write vol/pan/mute/solo engine-side on track 0, bind a
`SessionMixerStrip` to it, force ONE `refreshControls()` tick, and read the strip's controls back through its
accessors. The strip works headless/non-visible (`refreshControls()` has no visibility dependency; only the
poll timer is visibility-gated).

| field | meaning | PASS requires |
|---|---|---|
| `bound` | the strip resolved a live track | 1 |
| `faderOk` | the fader reads back `setTrackVolumeDb(-9)` within 0.15 dB | 1 |
| `panOk` | the pan reads back `setTrackPan(-0.5)` within 0.02 | 1 |
| `muteOk` | the strip reflects `track.setMute(true)` | 1 |
| `soloOk` | the strip reflects `track.setSolo(true)` | 1 |

> Verified 2026-07-02: **PASS** (`mode=sessionmixer`, all legs 1), bringing the floor to **22 gates**.
> **Ladder-ordering note:** `--selftest-sessionmixer` CONTAINS `--selftest-session`, so it MUST be matched
> before it in the `commandLine.contains(...)` ladders — else it silently runs the session gate and reports a
> false `result=PASS` under `mode=session`. Always verify the `mode=` line, not just `result=`.

The base `session` screenshot state now includes the mixer band (it's part of SessionView's tree), so no new
`--screenshot` state was added — the 10-state matrix stands.

## `Forge --selftest-demo` (audible demo — instruments + seeded notes)

The acceptance gate for **W09 — the audible demo** (design:
[../docs/devlog/wave-09-instruments.md](../docs/devlog/wave-09-instruments.md)). Structural + synchronous:
`PluginHost::applyInstrumentPreset(track0, Kick)` inserts a 4OSC (not the Sampler); `applyInstrumentPreset(track2,
Piano)` inserts the engine **Sampler**; `InstrumentSamples::ensurePianoOneShot()` generates the self-rendered
CC0 piano one-shot on disk; and a seeded clip actually holds notes. It does NOT render audio (the Sampler loads
its sample on an AsyncUpdater; playback engagement is covered by `--selftest-session`).

| field | meaning | PASS requires |
|---|---|---|
| `kickIsSynth` | the Kick preset inserted a 4OSC synth (not a Sampler) | 1 |
| `pianoIsSampler` | the Piano preset inserted a `te::SamplerPlugin` | 1 |
| `pianoFileExists` | the self-rendered CC0 piano one-shot exists in `%APPDATA%\Forge\library` | 1 |
| `noteCount` / `clipHasNotes` | a seeded demo clip holds notes | `noteCount>0`, `clipHasNotes=1` |

> Verified 2026-07-02: **PASS** (all legs 1, `noteCount=16`), bringing the floor to **23 gates**. Known
> follow-up (QC NIT, not gated): the gate proves the piano one-shot exists on disk but not that the Sampler
> *ingested* it (an async load) — a render/ingestion leg would prove the final audible link.

## `Forge --selftest-sendarrange` (Session → Arrangement "Send to" bridge)

The acceptance gate for **W10 — the Session → Arrangement bridge** (design:
[../docs/devlog/wave-10-send-to-arrangement.md](../docs/devlog/wave-10-send-to-arrangement.md)). Synchronous.
Two legs: a **MIDI leg** (seed 4 notes into slot (0,0), send it onto track 0's linear timeline) and a **wave
leg** (a self-generated sine WAV imported into slot (1,0), sent onto track 1) — the wave leg exercises the
`AudioClipBase` normalization the MIDI leg can't reach. It does NOT render audio; audibility is proven by the
`playSlotClips` state flip, not by sampling non-zero output.

| field | meaning | PASS requires |
|---|---|---|
| `sourceIntact` | the send is a COPY, not a move — the source slot still holds its clip | 1 |
| `arrangeClipAppeared` / `secondAppended` | a new clip lands on the timeline; a 2nd send appends after the 1st | 1 / 1 (`clipsAfterSecond=2`) |
| `noteCountPreserved` | the copied MidiClip carries the SAME note count (the sequence rode along in the state clone) | 1 (`copiedNotes=4`) |
| `landedAtStart` | the first send appends at 0:00 on the empty lane | 1 |
| `copyNotLooping` | the slot's inherited loop range was cleared → a plain one-shot | 1 |
| `arrangeAudible` | a track with `playSlotClips` latched TRUE is flipped back to arrange playback by the send | 1 |
| `undoRemovedCopy` / `sourceIntactAfterUndo` | one Ctrl+Z removes the copy and leaves the source slot filled | 1 / 1 |
| `waveIsAudioClip` / `waveNotLooping` / `waveNoAutoTempo` | the wave copy is a non-looping, non-auto-tempo `WaveAudioClip` | 1 / 1 / 1 |
| `waveSourceMatches` | the wave copy's audio source survived the state copy (matches the slot clip's source) | 1 |

### W16 addition (owed W05 debt, dimension 6 — arrange-render-audibility-gate-leg)

Everything above proves audibility only via the `playSlotClips` STATE assertion — never sampled non-zero output.
This leg renders the un-undone first send (track 0's single 4-note MIDI arrange clip on a 4OSC instrument —
sidesteps the Sampler's async-load blocker) via the existing, synchronous `Exporter::renderStems` (already used
by the export UI; no `UndoManager` touch) and samples the stem's peak via the existing `readPeakMagnitude`
helper (already used by `--selftest-record`).

| field | meaning | PASS requires |
|---|---|---|
| `renderAttempted` | the render call was made | 1 |
| `renderPeak` | the stem's peak absolute sample value (logged always, `-1.0` sentinel on failure) | — (diagnostic) |
| `renderAudible` | three-state: `PASS` (genuinely non-silent), `FAIL` (a stem WAS produced but reads silent — a real regression), `SKIP` (render infrastructure couldn't produce a file, logged, non-blocking) | `PASS` or `SKIP` — `FAIL` blocks the gate |

> `renderAudible` folds into the overall `pass` boolean as `renderAudible != "FAIL"` — `SKIP` is honest and
> non-blocking (never a fictional `PASS`), matching the frontier program's stated product decision for this leg.

> Verified 2026-07-02: **PASS** (all legs 1), bringing the floor to **24 gates**. `--selftest-sendarrange` does
> not collide with any existing gate name (no substring relationship), and is ordered before the bare
> `--selftest` in both command-line ladders; verify the report's `mode=sendarrange` line. Re-verified
> 2026-07-06 (W16): the new render-audibility leg reads `renderAudible=PASS` (`renderPeak≈0.39`), floor
> unchanged at 32 (this wave extends 4 existing gates, adds zero new gate names).

### W17 addition (Wave 7 fast-follow — send-as-loop)

`sendSlotToArrangement` gained a `keepAsLoop` parameter (default `false` — every prior caller is unchanged). A
**loop leg** (track 2): seeds a MIDI clip in slot (2,0), toggles it looping (`setSlotClipLooping`), sends it
with `keepAsLoop=true`, then sends the SAME slot again with the default (`false`) — proving the flag genuinely
toggles per-call behaviour rather than being persistent state.

| field | meaning | PASS requires |
|---|---|---|
| `loopClipCreated` / `loopToggled` | the fixture clip exists and is looping before the send | 1 / 1 |
| `loopSent` / `loopCopyIsLooping` | `keepAsLoop=true` sends a copy that IS STILL looping (not normalized) | 1 / 1 |
| `loopControlSent` / `loopControlNotLooping` | a second send of the SAME slot with the default `false` DOES normalize to one-shot | 1 / 1 |

> No new gate name — extends the existing `--selftest-sendarrange` gate, contributing 0 to the floor bump on
> its own; see `--selftest-scenesend` below for the one NEW gate this wave adds (the source of the 33→34 bump).

## `Forge --selftest-scenesend` (whole-scene "Send to Arrangement", Wave 7 fast-follow)

The acceptance gate for whole-scene send: every FILLED slot in a scene is sent to its own track's linear
Arrangement, all aligned at **ONE SHARED start beat** (the max append point across only the tracks that
actually have a filled slot in that scene), in **one undo transaction**. Synchronous. Fixture: seeds
different-length MIDI clips in slots (0,2) (4 notes) and (1,2) (2 notes), then PRE-SEEDS track 0's arrangement
with an unrelated send (via the existing single-slot seam) so track 0's append point is pushed forward while
track 1's stays at 0 — making the "shared start" assertion meaningful (without the fix, track 1's copy would
land at its OWN zero append point instead of matching track 0's).

| field | meaning | PASS requires |
|---|---|---|
| `clip0Created` / `clip1Created` | both scene-2 fixture clips exist | 1 / 1 |
| `preSeedSent` | the unrelated track-0 pre-seed send succeeded | 1 |
| `sentCount` / `sent2` | `sendSceneToArrangement(2)` sends exactly 2 clips (one per filled track) | 2 / 1 |
| `start0` / `start1` | the two new clips' start times | equal to each other, both > 0 |
| `sharedStartMatches` | both land at the SAME beat (not each track's own append point) | 1 |
| `sharedStartNonZero` | the shared start is track 0's pushed-forward point, not 0 (proves the max, not a fallback) | 1 |
| `sourceIntact0` / `sourceIntact1` | both source slots stay filled — a copy, not a move | 1 / 1 |
| `undoRemovedBoth` | ONE `ed->undo()` removes BOTH copies atomically (one transaction, not two) | 1 |
| `emptySceneNoop` | sending a scene with nothing filled anywhere returns 0 (logged, non-fatal) | 1 |

> `-scenesend` CONTAINS `-scene` → placed **before** `--selftest-scene` (longest-first, alongside
> `-scenerename`/`-scenedelete`/`-scenereorder`) in both ladders; verify `mode=scenesend`. **Floor is now 34
> gates.** Adversarially QC'd alongside the capture-core wave's swarm — see
> [devlog/wave-17-performance-capture.md](../docs/devlog/wave-17-performance-capture.md) for the fast-follow
> addendum.

## `Forge --selftest-followaction` (per-clip follow actions + loop toggle)

The acceptance gate for **W11 — launcher expressiveness** (design:
[../docs/devlog/wave-11-launcher-expressiveness.md](../docs/devlog/wave-11-launcher-expressiveness.md)).
Synchronous. Fills slots (0,0) and (0,1) on track 0 (so `trackNext` has a valid sibling — `createFollowAction`
resolves non-null only when a filled clip exists AFTER this one). Proves the follow-action seams + the
loop-toggle seam. It does NOT prove a clip AUDIBLY chains (needs a pumped playback loop the harness doesn't run —
parked, W09/W10 convention).

| field | meaning | PASS |
|---|---|---|
| `setReadsBack` | `setFollowAction(0,0,trackNext)` reads back trackNext | 1 |
| `actionListSingle` | exactly ONE action, front == trackNext — the footgun (no stray auto-plant) | 1 |
| `durationPersists` | a loops-duration write persists AND the action type survived it (footgun re-check) | 1 |
| `functorNonNull` / `noneGivesEmpty` | `createFollowAction` returns a non-null functor for trackNext, empty for none | 1 / 1 |
| `valueTreeRoundTrip` | the action + duration survive a `clip->state.createCopy()` (serialization) | 1 |
| `undoReverts` | one undo reverts a follow-action set | 1 |
| `loopToggleOn` / `loopToggleOff` / `neverEmptyLoopRange` | loop toggle on↔off; loop range is never empty (the `setLoopRangeBeats({})` gotcha) | 1 / 1 / 1 |

> Verified 2026-07-02: **PASS** (all 11 legs 1). Collision-free; before bare `--selftest`; verify `mode=followaction`.

## `Forge --selftest-launchmode` (per-clip launch mode Trigger/Gate/Toggle)

The acceptance gate for the **W11 launch-mode seam**. Synchronous, state-level. Proves the seam (set / read-back /
persist / absence-is-Trigger) and that the `isSlotActive` query the Toggle routing reads is sound. The AUDIBLE
Gate-hold / Toggle-off transitions are driven by the view's mouse routing (not a seam) and are parked / manual —
proven-adjacent by the launch path in `--selftest-session`; this gate never fakes a PASS for them.

| field | meaning | PASS |
|---|---|---|
| `defaultIsTrigger` / `absenceIsTrigger` | a fresh clip and a never-set sibling both read Trigger (existing edits unchanged) | 1 / 1 |
| `setToggleReadsBack` / `setGateReadsBack` | Toggle and Gate set + read back | 1 / 1 |
| `persists` | `forgeLaunchMode` survives a `clip->state.createCopy()` | 1 |
| `toggleQuerySound` | `isSlotActive` is false when nothing is launched (the Toggle routing query is sound) | 1 |

> Verified 2026-07-02: **PASS** (all 7 legs 1). Collision-free; before bare `--selftest`; verify `mode=launchmode`.
> **Floor is now 26 gates.** Follow-up (documented): a disk save→reload leg proving `forgeLaunchMode` survives a
> written-and-reloaded `.tracktionedit` (disk persistence is guaranteed by the engine's whole-tree serializer;
> the gate currently proves the in-memory `createCopy`).

## `Forge --selftest-duplicate` (grid clip duplicate → auto-grow)

The acceptance gate for **W13 — grid clip primitives** (design:
[../docs/devlog/wave-13-grid-clip-primitives.md](../docs/devlog/wave-13-grid-clip-primitives.md)). Synchronous.
Seeds a 4-note MIDI clip into (0,0), makes it a one-shot, then exercises `duplicateSlotClip`. Proves the seam +
auto-grow + content + one-shot fidelity + undo — NOT that the copy audibly plays.

| field | meaning | PASS |
|---|---|---|
| `dupToEmptyBelow` / `sourceStillFilled` | the duplicate lands in the first empty slot below (scene 1); the source stays filled (a copy, not a move) | 1 / 1 |
| `contentIdentical` (`dupNotes`) | the duplicate carries the source's 4 notes (the sequence rode the state clone) | 1 (4) |
| `sourceOneShot` / `oneShotPreserved` | a one-shot source stays one-shot — the engine re-loops a non-looping clip unless `cloneClipIntoSlot` re-asserts `disableLooping()` (QC-caught) | 1 / 1 |
| `undoRemovedDup` / `undoLeftSourceIntact` | one undo removes just the duplicate; the source is intact | 1 / 1 |
| `grewWhenFull` | with no empty slot below, a duplicate grows a new scene row (`growDst == oldNumScenes`, count +1) | 1 |
| `emptySourceNoop` | duplicating an empty slot is a no-op (returns -1) | 1 |

> Verified 2026-07-03: **PASS**. Collision-free (`duplicate` shares no substring with any gate); before bare `--selftest`; verify `mode=duplicate`.

## `Forge --selftest-slotmove` (grid clip move / copy / replace)

The acceptance gate for the **W13 `copySlotClip` / `moveSlotClip` seams**. Synchronous. Seeds a 4-note clip and
exercises copy, move, replace-on-filled, and MOVE atomic undo — cross-track, note-count faithful.

| field | meaning | PASS |
|---|---|---|
| `copyKeepsSource` / `copyDestFilled` / `copyNotes` | COPY (0,0)→(1,0) leaves the source filled, fills the dest with the 4 notes | 1 / 1 / 1 |
| `moveEmptiesSource` / `moveDestFilled` / `moveNotes` | MOVE (0,0)→(2,0) empties the source, fills the dest with the notes | 1 / 1 / 1 |
| `replaceOneClip` / `replaceNotes` | copying onto a FILLED dest replaces it (one clip; the engine auto-removes the old) | 1 / 1 |
| `moveUndoRestoredSource` / `moveUndoEmptiedDest` | one Ctrl+Z reverses BOTH halves of a MOVE — the source restored AND the dest emptied (the atomic-transaction proof) | 1 / 1 |
| `emptyMoveNoop` | moving an empty source is a no-op (false; no dest created) | 1 |

> Verified 2026-07-03: **PASS**. `slotmove` vs `slotdelete` share only the `slot` prefix (neither contains the other) — collision-free; before bare `--selftest`; verify `mode=slotmove`. **Floor is now 28 gates.**

## `Forge --selftest-quantise` (piano-roll MIDI quantise)

The acceptance gate for **W14 — MIDI quantise** (design:
[../docs/devlog/wave-14-midi-quantise.md](../docs/devlog/wave-14-midi-quantise.md)). Synchronous. Seeds 3
off-grid notes (starts 0.1/1.1/2.1, distinct lengths) and drives `forge::midiedit::quantiseNoteStarts` directly.
Proves the quantise math + interpolation + undo — NOT audible playback.

| field | meaning | PASS |
|---|---|---|
| `seededNotes` | 3 notes seeded at off-grid starts | 3 |
| `snappedToGrid` | quantise to the 0.25 grid at 100% snaps each start to its nearest multiple (0.1→0.0, 1.1→1.0, 2.1→2.0) within 1e-4 | 1 |
| `lengthPreserved` | each note's length is unchanged (starts-only) | 1 |
| `partialStrength` | a note at 0.1 quantised at 50% lands at ~0.05 (halfway — proves `setProportion` interpolation, not a full snap) | 1 |
| `undoReverts` | one undo restores the pre-quantise start (the destructive rewrite rides the Edit UndoManager) | 1 |

> Verified 2026-07-03: **PASS**. `quantise` is substring-collision-free; before bare `--selftest`; verify `mode=quantise`. **Floor is now 29 gates.** Grid mapping is a fraction of a BEAT (0.25 → "1/4", not "1/16"). Groove is a documented fast-follow (2 built-in swing templates on the same seam).

## `Forge --selftest-scenerename` (scene rename)

The acceptance gate for **W15 — scene rename** (`ProjectSession::setSceneName`). Synchronous. Proves a name
reads back, a blank name persists (the row falls back to its number), the rename is undoable
(`te::Scene::name` is UndoManager-bound), and renaming one scene leaves others untouched.

| field | meaning | PASS |
|---|---|---|
| `nameReadsBack` | `setSceneName(2,"Verse")` → `getSceneName(2)=="Verse"` | 1 |
| `blankPersists` | `setSceneName(2,"")` → `getSceneName(2)` empty | 1 |
| `undoReverts` | a sealed rename of scene 3 reverts on `ed->undo()` | 1 |
| `otherUntouched` | renaming scene 5 leaves scene 4 unchanged | 1 |

> `-scenerename` CONTAINS `-scene` → placed **before** `--selftest-scene` (longest-first) in both ladders; verify `mode=scenerename`.

## `Forge --selftest-scenedelete` (scene delete)

The acceptance gate for **W15 — scene delete** (`ProjectSession::deleteScene`). Synchronous. Deleting a scene
drops the count and shifts scenes + their per-track slots down in lockstep; one Ctrl+Z restores the scene, its
name, AND the clip in a shifted slot (both `removeFromParent` + `deleteSlot` ride the UndoManager). Fixture fills
(0,3) with a marker clip so the slot-shift + clip-restore are observable.

| field | meaning | PASS |
|---|---|---|
| `countBefore` | 8 scenes, (0,3) filled | 1 |
| `deleteDropsCount` | `deleteScene(2)` → count 7 | 1 |
| `indexShift` | old scene 3 ("THREE") is now at index 2 | 1 |
| `slotShift` | the (0,3) clip shifted up to (0,2) | 1 |
| `undoRestoresCount` | undo → count 8 | 1 |
| `undoRestoresName` | undo → scene 2="TWO", scene 3="THREE" | 1 |
| `undoRestoresClip` | undo → clip back at (0,3) (rode the UndoManager) | 1 |
| `outOfRangeNoop` | `deleteScene(999)` returns false, count unchanged | 1 |

> `-scenedelete` CONTAINS `-scene` → placed **before** `--selftest-scene` (longest-first) in both ladders; verify `mode=scenedelete`.

## `Forge --selftest-scenereorder` (scene reorder)

The acceptance gate for **W15 — scene reorder** (`ProjectSession::moveScene`). Synchronous. Moving scene `from`
to `to` reorders the SCENES tree AND every track's CLIPSLOTS tree in lockstep, so both scene NAMES and per-track
CLIPS follow (the desync guard — no engine `moveScene` seam exists; this is raw `ValueTree::moveChild` on each
list's public state). Undoable; equal / out-of-range indices are no-ops. Fixture names S0..S3 and fills (0,0) +
(0,2) so a scene↔slot desync would be visible.

| field | meaning | PASS |
|---|---|---|
| `moveReorders` | `moveScene(0,2)` → names become [S1,S2,S0,…] | 1 |
| `slotsFollowScenes` | the (0,0) clip rides to (0,2), (0,2)→(0,1); (0,0) now empty | 1 |
| `undoReverts` | undo → scene 0="S0" and clip back at (0,0) | 1 |
| `noopGuards` | `moveScene(3,3)` and `moveScene(0,999)` both false, count unchanged | 1 |

> `-scenereorder` CONTAINS `-scene` → placed **before** `--selftest-scene` (longest-first) in both ladders; verify `mode=scenereorder`. **Floor is now 32 gates.** The inline-rename focus/teardown + the row PopupMenu are UI, proved by `--screenshot` + adversarial QC, not by a gate.

## `Forge --selftest-capture` (performance capture — the real Session → Arrange bridge, W7)

The acceptance gate for **frontier Wave 7 — performance recording** (`ProjectSession::startPerformanceCapture` /
`stopPerformanceCapture` / `performanceCaptureTick`). Unlike `--selftest-sendarrange` (a single static clip
appended at the end), capture preserves WHEN each clip fired: it samples `LaunchHandle::getPlayedRange()` on the
message thread (~30 Hz, manually pumped here in lockstep with `blockUntilSyncPointChange` for determinism) and
stamps a one-shot clip at its absolute captured Edit beat. Fixture: seeds a 4-note MIDI clip in slot (0,0),
rolls the transport to a KNOWN beat 4 (deterministic — not wall-clock pre-roll) before arming capture, launches,
samples, stops, and commits, bracketed in one undo transaction exactly like the shell's Global-Rec toggle.

| field | meaning | PASS requires |
|---|---|---|
| `clipCreated` | the 4-note fixture clip exists in slot (0,0) | 1 |
| `captureArmed` | `isPerformanceCaptureArmed()` true after arming | 1 |
| `spanCount` | accumulated span count while sampling | > 0 |
| `accumulated` | a span was captured after the stop transition | 1 |
| `stamped` | clips stamped onto the Arrangement at commit | ≥ 1 |
| `clipStamped` | track 0's arrange clip count grew by exactly `stamped` | 1 |
| `isMidiCopy` | the stamped clip is a `te::MidiClip` | 1 |
| `oneShot` | the stamped clip is NOT looping (normalized) | 1 |
| `capturedStartBeat` / `placedStartBeat` | the live launch beat vs. the stamped clip's beat | both ≈ 4.0, within 0.5 of each other |
| `absoluteBeatPreserved` | the stamped clip landed at its CAPTURED beat (not append-at-0) | 1 |
| `sourceIntact` | the source slot clip is still filled (a copy, not a move) | 1 |
| `undoRemovedTake` | one `ed->undo()` removes the whole stamped take | 1 |

> `-capture` is collision-free (no substring overlap) — placed before bare `--selftest` in both ladders.
> **Floor is now 33 gates.** Adversarially QC'd (4 dimensions): the accumulation logic's stop/relaunch reseal
> and identity-resolution-at-commit were both scrutinized — QC caught and a fix landed for a confirmed defect
> (commit was resolving the source clip by CELL INDEX, so clearing+replacing a clip in a slot mid-capture-session
> would stamp the replacement clip's content at the original clip's captured beat); commit now resolves by
> `te::EditItemID` (captured at span-OPEN time, never re-derived), verified via `te::findClipForID`. The
> `sendSlotToArrangement` clone/normalize/strip body was factored into a shared `insertClipCopyOnTimeline` helper
> (behaviour-preserving — `--selftest-sendarrange` stays byte-identical, confirmed clean by an independent QC
> pass). The TransportBar's new "Capture" toggle was checked against the W06 starved-control regression class at
> the 760 px window minimum — confirmed clean (worst-case control width 40 px, no starvation).

## `Forge --selftest-sessionmaster` (Session master-corner strip, W8 mixer polish)

The acceptance gate for the **Session master strip** (`SessionMasterStrip`, the fixed bottom-right corner of the
Session grid). Mirrors `--selftest-sessionmixer` but for the master: sets the Edit's master volume via
`getMasterVolumePlugin()->setVolumeDb` at **two distinct dB values** and asserts the strip's fader tracks each —
so a "reads a default once and sticks" bug fails the second leg. Synchronous. The master volume plugin exists on
every edit; no playback context in headless, so the meter is not asserted (same posture as `--selftest-sessionmixer`).

| field | meaning | PASS requires |
|---|---|---|
| `bound` | the strip resolved the master volume plugin | 1 |
| `faderDbAtMinus6` / `faderReadOk` | after `setVolumeDb(-6)` + refresh, the fader reads ≈ -6 | ≈ -6 / 1 |
| `faderDbAtUnity` / `faderTracksChange` | after `setVolumeDb(0)` + refresh, the fader TRACKS to ≈ 0 (not stuck at -6) | ≈ 0 / 1 |

> `-sessionmaster` CONTAINS `-session` → placed **before** `--selftest-session` (longest-first, alongside
> `-sessionmixer`) in both ladders; verify `mode=sessionmaster`. Disjoint from `-sessionmixer` (neither contains
> the other). **Floor is now 35 gates** at this point (36 with `-peakhold` below).

## `Forge --selftest-peakhold` (PeakMeter peak-hold + clip latch, W8 mixer polish)

The acceptance gate for the **peak-hold line + sticky clip latch** — a FULLY PURE gate driving
`forge::meter::advanceMeterHold` directly (the `computeLcdState` pattern; no edit / engine / paint).

| field | meaning | PASS requires |
|---|---|---|
| `holdJumped` | a +3 dBFS transient jumps the hold line up instantly (instant attack) | 1 |
| `clipLatched` | crossing 0 dBFS sets the sticky clip latch | 1 |
| `holdLingers` | after 6 silent ticks the hold (6 dB/s) sits clearly ABOVE where the faster bar (18 dB/s) would be | 1 |
| `clipSticky` | the latch is NEVER auto-cleared under silence | 1 |
| `clipStaysWhenDisabled` | a click on a plain meter (`showClip=false`) must NOT clear the latch | 1 |
| `clipClearedWhenEnabled` | a click on a clip-enabled meter (`showClip=true`) DOES clear it | 1 |
| `holdSettles` | after long silence the hold decays to the floor (`kMeterMinDb`) | 1 |

> `-peakhold` is collision-free (no substring overlap) — placed before the bare `--selftest` in both ladders;
> verify `mode=peakhold`. **Floor is now 36 gates.** The peak-hold/clip features are opt-in (default OFF), so
> every existing meter renders byte-identically; the new `SessionMasterStrip` enables both. `clipStaysWhenDisabled`
> / `clipClearedWhenEnabled` drive the REAL `forge::meter::clearClipLatch` predicate that `PeakMeter::mouseDown`
> routes through (a QC fix — the earlier `clipCleared` leg was a vacuous field-write assertion).

## `Forge --selftest-midifile` (MIDI-file drag-and-drop import)

The acceptance gate for **MIDI-file import** (`ProjectSession::importMidiIntoSlot` + `importMidiFile`, both over
the engine's `te::createClipFromFile`). Writes a real 4-note `.mid` (960 ticks/quarter, notes on beats 0–3) to
disk, then imports it BOTH into a Session slot and onto an Arrange lane at a 2.0s drop time. Synchronous. The
engine reader is tempo-INDEPENDENT (ticks→beats), so notes land on file beats regardless of the edit tempo.

| field | meaning | PASS requires |
|---|---|---|
| `wroteFile` | the test `.mid` was written to disk | 1 |
| `slotImported` / `slotNoteCount` / `slotNotesOk` | slot import returns a MidiClip carrying all 4 notes | 1 / 4 / 1 |
| `arrImported` / `arrNoteCount` / `arrNotesOk` | arrange import returns a MidiClip carrying all 4 notes | 1 / 4 / 1 |
| `arrStartSecs` / `arrStartOk` | the arrange clip slid to the 2.0s drop point | ≈2.0 / 1 |
| `instrumentOk` | the seam's `ensureDefaultInstrument` made the track born-audible (a synth) | 1 |
| `emptyGuard` | a non-existent `.mid` path degrades to `{}` (the pre-guard) | 1 |
| `notelessGuard` | an EXISTING but note-less `.mid` degrades to `{}` (the `createClipFromFile`-null branch — the real graceful-degradation path) | 1 |

> `-midifile` CONTAINS `-midi` → placed **before** `--selftest-midi` (longest-first, alongside `-midilearn`/
> `-midiinput`) in both ladders; verify `mode=midifile`. **Floor is now 38 gates.** The real UI drop path
> (accept `mid;midi` in `isInterestedInFileDrag`; dispatch audio vs MIDI by extension in `handleSlotFilesDropped`
> + the arrange `onFilesDropped`) is proved by adversarial QC, not by a gate. v1 imports only the FIRST
> track/channel of a multi-track file (a documented limitation); the 4OSC gives pitches (a MIDI file is melodic).

## `Forge --selftest-stepclip` (Step Clip drum-grid seam, W20 / frontier Wave 10)

The acceptance gate for **Step Clips** (`ProjectSession::createStepClipInSlot` + the engine's `StepClip`). Creates
a step clip in slot (0,0) and proves the whole chain headlessly. Synchronous. The StepClip constructor
auto-builds the default grid (8 GM-drum channels × a 16-step pattern in 4/4), so the seam inserts + ensures the
track's drum-kit Sampler (W21; a default 4OSC before then) and nothing else.

| field | meaning | PASS requires |
|---|---|---|
| `clipCreated` | `createStepClipInSlot` returned a `StepClip` | 1 |
| `numChannels` / `channelsOk` | the ctor auto-built 8 GM-drum channels | 8 / 1 |
| `numSteps` / `stepsOk` | pattern[0] has 16 steps (`getBeatsPerBar()*4` in 4/4) | 16 / 1 |
| `instrumentOk` | the seam's `ensureDrumKitInstrument` put a synth (a drum-kit Sampler, W21) on the track (born-audible) | 1 |
| `noteOnsWhenOff` / `midiEmptyWhenOff` | a fresh clip (all cells off) generates NO MIDI note-ons | 0 / 1 |
| `cellToggleOn` | `setCell(0,0,0,true)` → `getCell` true | 1 |
| `noteOnsWhenOn` / `midiWhenOn` | with a cell on, `generateMidiSequence` emits note-ons (the born-audible link) | >0 / 1 |
| `cellToggleOff` | `setCell(0,0,0,false)` → `getCell` false (round-trips) | 1 |
| `undoRevertsCell` | a cell toggle in a fresh transaction reverts on `ed->undo()` (content-level, per the FourOsc redo-wipe gotcha) | 1 |
| `stepGridSurvivesDelete` | UAF regression: a `StepGridView` holds the clip as a refcounted `Ptr`, so deleting the slot clip while bound leaves it alive-but-parentless (readable `state`, invalid parent) — a raw pointer would dangle | 1 |

> `-stepclip` is collision-free (no substring overlap) — placed before the bare `--selftest` in both ladders;
> verify `mode=stepclip`. **Floor is now 37 gates.** The 16×8 drag-to-toggle grid UI (`StepGridView`) + the
> `BottomMode::StepGrid` drawer routing (the `isMidi()==false` gotcha — a StepClip is routed by an explicit
> `dynamic_cast<te::StepClip*>` before the MidiClip cast) are proved by `--screenshot` + adversarial QC, not by a
> gate. Step clips are now born with a **drum-kit Sampler** (W21) so they sound like a kit — see
> `--selftest-drumkit` below (the "4OSC gives pitches, not drum timbres" follow-up is now done).

## `Forge --selftest-modifier` (LFO plugin-param modifier seam, frontier Wave 9)

The acceptance gate for **frontier Wave 9 — live modulation** (`src/engine/ModifierHelpers.h`, over the engine's
unit-tested `ModifierList`). Synchronous, message-thread. Creates an LFO on a fresh track, sweeps it headlessly
(`edit->updateModifierTimers({}, 512)` — `numSamples>0` is load-bearing: the free-running phase advances by
numSamples, not editTime), assigns it to the track volume param, and tears it back down. Content-level asserts
only (never `canUndo/canRedo` — the FourOsc redo-wipe defect). It does NOT render audio.

| field | meaning | PASS requires |
|---|---|---|
| `lfoCreated` | `forge::modifier::addLFO` inserted a live `LFOModifier` | 1 |
| `lfoSpread` / `lfoOscillates` | the default-config LFO genuinely oscillates over the sweep | spread > 0.3 / 1 |
| `assigned` / `modifierActive` | `assign()` binds the LFO to `volParam`; `hasActiveModifierAssignments()` true | 1 / 1 |
| `modifierValueVaries` | the param's `getCurrentModifierValue()` is non-constant while assigned | 1 |
| `unassignedCleanly` / `finalValueConstant` | `unassign()`+`removeLFO()` leave `getAssignments()` empty and the param constant again | 1 / 1 |
| `depthZeroFlat` (`depthZeroSpread`) | a `depth=0` LFO does NOT oscillate — proves `applyConfig`'s param writes take effect (the engine default also oscillates, so this catches an `applyConfig` regression) | 1 (≈0) |

> `-modifier` is collision-free (no substring overlap) — placed before the bare `--selftest` in both ladders;
> verify `mode=modifier`. **Floor is now 39 gates.** The engine seam + gate stand alone; the "Modulate" UI
> affordance is deferred to Fable. Adversarial QC (SHIP) added the `depthZeroFlat` config-sensitivity leg and
> guarded `unassign()` against the `removeModifier` Debug `jassertfalse` (a new CLAUDE.md gotcha).

## `Forge --selftest-drumkit` (self-rendered CC0 drum-kit sampler)

The acceptance gate for the **drum sampler** (`InstrumentSamples::ensureDrumKit` +
`PluginHost::ensureDrumKitInstrument`) that Step Clips are now born with (via a one-line `createStepClipInSlot`
reroute). Synchronous. Appends a fresh track, inserts the drum kit, and proves the whole chain structurally +
non-silent.

| field | meaning | PASS requires |
|---|---|---|
| `isSampler` | `ensureDrumKitInstrument` inserted a `te::SamplerPlugin` at the track head | 1 |
| `numSounds` / `soundCountOk` | the Sampler holds 8 sounds (one per GM drum voice) | 8 / 1 |
| `notesOk` | each sound is keyed (key==min==max) to its GM note in StepClip channel order (36/38/42/46/39/45/50/51) | 1 |
| `idempotentReturnedFalse` / `idempotentStillEight` | a 2nd `ensureDrumKitInstrument` no-ops (returns false; still 8 sounds — never stacks) | 1 / 1 |
| `filesOk` | the 8 `drum_<note>.wav` one-shots exist on disk > 1 KB | 1 |
| `audioNonSilent` | each generated one-shot DECODES to non-silent PCM (peak > 0.05) — proves the generators produce real audio, not just valid files (on a cold cache this exercises the full generate path) | 1 |

> `-drumkit` is collision-free (no substring overlap) — placed before the bare `--selftest` in both ladders;
> verify `mode=drumkit`. **Floor is now 40 gates.** All audio is self-rendered CC0 into `%APPDATA%\Forge\library`
> (no committed binaries; deterministic seeded synthesis; the piano one-shot is byte-identical after the
> shared-writer refactor). A full note-on → engine-render (Sampler-ingestion) leg stays parked (W09/W10 class);
> the mixed-clip "first-instrument-wins" limitation (a melodic pitch played through a drum kit is silent) is a
> documented v1 behavior.

## `Forge --selftest-nudge` (piano-roll keyboard nudge)

The acceptance gate for **W22 — Shift+Left/Right note nudge** (`forge::midiedit::shiftNoteStarts`, driven directly,
no PianoRollView). Synchronous. Seeds a 3-note chord (0.5-beat gaps) and drives the helper across four legs.

| field | meaning | PASS |
|---|---|---|
| `seededNotes` | 3 notes seeded | 3 |
| `rightNudged` | a +gridBeats nudge shifts every note by exactly +0.25 | 1 |
| `undoReverts` | one `ed->undo()` reverts the nudge transaction | 1 |
| `leftNudged` | a −gridBeats nudge shifts every note by −0.25 | 1 |
| `clampedAtZero` / `gapsPreserved` | a large left nudge group-clamps the whole chord at beat 0 while preserving each internal gap (not a per-note clamp) | 1 / 1 |

> `-nudge` is collision-free — before bare `--selftest`; verify `mode=nudge`. **Floor 40 → 41.** The live
> `keyPressed` Shift+arrow path is UI (proven by the shared helper + adversarial review, not a gate). The
> "snap off → 1-bar fallback" branch is currently unreachable through the shell (no piano-roll snap toggle yet).

## `Forge --selftest-retrocapture` (MIDI retrospective capture)

The acceptance gate for **W22 — `ProjectSession::commitRetrospectiveToSlot`** (over the engine's per-instance
`InputDeviceInstance::applyRetrospectiveRecord`). Event-driven (mirrors `--selftest-midi`'s yield discipline).
Creates a `VirtualMidiInputDevice`, arms **track 0** for MIDI, injects 4 notes with **NO transport roll anywhere**,
then commits — proving capture works when the user was NOT recording.

| field | meaning | PASS requires |
|---|---|---|
| `midiDeviceEnabled` / `trackArmed` | the virtual MIDI in is enabled + targets track 0 | 1 / 1 |
| `neverRecorded` | `transport.isRecording()` is false before AND after commit (the whole point) | 1 |
| `notesInjected` | 4 note-ons injected into the retrospective buffer (no record) | 4 |
| `clipCreated` / `clipInSlot` | commit returns a MidiClip that landed in a launcher ClipSlot | 1 / 1 |
| `capturedNoteCount` | the committed clip holds exactly the injected notes | 4 |

> `-retrocapture` is collision-free — it does NOT contain the substring `--selftest-capture` (they diverge after
> `--selftest-`), so no longest-first ordering vs `-capture`; before bare `--selftest`; verify `mode=retrocapture`.
> **Floor 41 → 42.** A **use-after-free** in the relocate-to-slot step was caught by running this gate and fixed
> (hold a `MidiClip::Ptr` across `removeFromParent`→`setClip`; see the CLAUDE.md gotcha). The `--selftest-modifier`
> gate also gained W22 `offsetApplied` (offset/bipolar config-sensitivity) + `paramResolvesTrack` (the Modulate
> menu's `param->getTrack()` resolution) legs — no new gate, floor unaffected.

## `Forge --screenshot` (headless render — no PASS/FAIL)

Not a pass/fail gate: builds a populated, NOTE-SEEDED 6-track demo (W09: per-track instrument presets — a 4OSC
kick, a 4OSC bass, and a Sampler piano — plus a 4-on-floor + bass + chord pattern), launches scene 0 (the
coherent kick+bass+piano groove), and renders each view to a
PNG in `%TEMP%` via `createComponentSnapshot`, so the UI can be inspected without a live display. Writes:

| file | what it proves |
|---|---|
| `forge_shot_session.png` | the Session clip grid (matches [mockups](../mockups/) sheet 00) |
| `forge_shot_arrange.png` / `forge_shot_mix.png` | the Arrange timeline / Mixer |
| `forge_shot_session_top.png` / `forge_shot_session_scrolled.png` | **vertical-scroll proof** — the grid at a short (1040×360) window, snapped at the top then scrolled to the bottom. Comparing them confirms all 16 scene rows are reachable (not clipped), pads stay ~46 px (no stretch), and the pinned scene column stays aligned with the pads while scrolling. |
| `forge_shot_session_scenes.png` | **dynamic scene-count proof (W07)** — the demo grid grown to **20 scenes** via `ensureScenes`, scrolled to the bottom: rows 16–20 render, stay row-aligned with the pinned scene column (the `rowBand` equal-height invariant — a QC-fixed drift), and scroll. |
| `forge_shot_session_stepgrid.png` | **Step Grid drawer proof (W20)** — a step clip created + populated with a drum pattern (kick on the beats, snare on 2 & 4, hat on eighths), opened in the `BottomMode::StepGrid` drawer: the 16×8 grid paints with the 8 GM-drum lane names in the gutter, active cells in `playGreen`, beat grouping every 4 steps. Proves the drag-to-toggle grid renders + a StepClip routes to the StepGrid drawer (the `isMidi()==false` gotcha). |

> Verified 2026-06-30: `session_scrolled` shows scenes 10–16 with the scene column aligned to the pads,
> confirming the Session-grid vertical scroll. This is the headless stand-in for the one check that still
> needs a human: a live mouse/keyboard GUI smoke pass.
