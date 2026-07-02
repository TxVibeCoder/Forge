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
- **Phase 3** — injects 4 deterministic notes (C4/E4/G4/C5) via
  `handleIncomingMidiMessage(msg, getMPESourceID())`, then YIELDS.
- **Phase 4** — `transport.stop` (the engine commits the captured notes into a new `MidiClip` in the slot via
  `addMidiAsTransaction`, never a take), disarms the slot, re-resolves the slot's clip, counts its notes,
  `deleteVirtualMidiDevice` (mandatory — a leaked name fails the next run), writes the report, and quits.

| field | meaning | PASS requires |
|---|---|---|
| `availableMidiInputs` | names of engine MIDI-in devices (incl. the virtual one) | — (diagnostic) |
| `midiDeviceEnabled` | 1 if the virtual MIDI input was enabled | 1 |
| `preExistingNotes` | notes already in slot `(0,0)` before recording | **0** (slot must start empty) |
| `trackArmed` | 1 if the MIDI input armed to the **slot's** `itemID` (slot armed, not the track) | 1 |
| `recordingStarted` | 1 if the transport entered record | 1 |
| `notesInjected` | count of synthetic notes injected while rolling | ≥ 4 |
| `clipCreated` | 1 if a `MidiClip` materialised in the slot on stop | 1 |
| `capturedNoteCount` | notes in the committed slot clip's sequence | **`== notesInjected`** (EXACT) |

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

> Verified 2026-07-02: **PASS**. Known behavioural edges (by design, not gate-covered): track **mute/solo
> are NOT undoable** (the engine binds them with a null UndoManager); **record-arm IS on the undo stack**
> (the shell blocks undo while recording); undo history does not survive a project swap. The live shell
> path additionally fans a cross-surface refresh out after every undo/redo — that path is exercised by the
> gate's shell but its view-level effects are screenshot/QC territory, not report fields.

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

> Verified 2026-07-02: **PASS** (all legs 1), bringing the floor to **24 gates**. `--selftest-sendarrange` does
> not collide with any existing gate name (no substring relationship), and is ordered before the bare
> `--selftest` in both command-line ladders; verify the report's `mode=sendarrange` line.

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

> Verified 2026-06-30: `session_scrolled` shows scenes 10–16 with the scene column aligned to the pads,
> confirming the Session-grid vertical scroll. This is the headless stand-in for the one check that still
> needs a human: a live mouse/keyboard GUI smoke pass.
