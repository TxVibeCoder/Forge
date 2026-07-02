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

## `Forge --screenshot` (headless render — no PASS/FAIL)

Not a pass/fail gate: builds a populated 6-track demo session, launches scene 3, and renders each view to a
PNG in `%TEMP%` via `createComponentSnapshot`, so the UI can be inspected without a live display. Writes:

| file | what it proves |
|---|---|
| `forge_shot_session.png` | the Session clip grid (matches [mockups](../mockups/) sheet 00) |
| `forge_shot_arrange.png` / `forge_shot_mix.png` | the Arrange timeline / Mixer |
| `forge_shot_session_top.png` / `forge_shot_session_scrolled.png` | **vertical-scroll proof** — the grid at a short (1040×360) window, snapped at the top then scrolled to the bottom. Comparing them confirms all 16 scene rows are reachable (not clipped), pads stay ~46 px (no stretch), and the pinned scene column stays aligned with the pads while scrolling. |

> Verified 2026-06-30: `session_scrolled` shows scenes 10–16 with the scene column aligned to the pads,
> confirming the Session-grid vertical scroll. This is the headless stand-in for the one check that still
> needs a human: a live mouse/keyboard GUI smoke pass.
