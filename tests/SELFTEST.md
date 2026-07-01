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
