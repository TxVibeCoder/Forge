# Integration pass — Phase 2 fan-out

*Orchestrator's record of integrating the 5-agent parallel fan-out (arrange · session ·
device · shell · hygiene). Companion to the per-area devlogs in this folder.*

## Outcome

- **Builds green** (`cmake --build build --config Debug`) — all five agents' changes compiled
  and linked together on the **first** integration build. The exclusive-file-ownership +
  additive-only-interface discipline held; no cross-agent collisions.
- **Playback self-test PASS** (`Forge.exe --selftest`): `device=Headphones (Bose QC35 II)`,
  `numClipComponents=1`, `playing=1`, `result=PASS`.
- **Record self-test FAIL** (`--selftest-record`) — *honest, hardware-gated, unchanged from
  before this work*: this dev box exposes no capture endpoint (`availableInputDevices=(none)`),
  so `inputDeviceCount=0`. Not a regression. See "Synthetic-input record self-test" below.
- **Normal app** launches, runs, and closes with no startup crash (full UI constructs).

## What each agent delivered (see per-area devlogs)

| Area | Files | Highlights |
|---|---|---|
| arrange | `ui/arrange/ArrangeView.{h,cpp}` | bars\|beats ruler, clip/track selection, per-lane M/S/R + colour swatch, right-click context menus, `xToTime` span guard, looped-clip waveform tiling |
| session | `services/files/ProjectSession.{h,cpp}` | saveAs assigns file only on success; `isModified()`; `getNumClipsOnTrack0()` |
| device | `engine/EngineHelpers.h`, `engine/RecordController.{h,cpp}` | `initialiseAudioForRecording()` (device-override fix), robust arm + diagnostics, synthetic-input helper |
| shell | `main.cpp`, `ui/ControlBar.{h,cpp}` | keyboard shortcuts, draggable region resizers, SafePointer dialog guards, per-dialog choosers, saveAs bool check |
| hygiene | `.gitattributes`, `ui/transport/TransportBar.cpp` | line-ending normalization, `formatTimecode` sign fix |

## Device-override fix — VERIFIED

The headline STATUS blocker. Before: `engine.getDeviceManager().initialise()` re-selected the
default device and clobbered the saved output (Bose → Realtek). After: the shell calls
`EngineHelpers::initialiseAudioForRecording(engine)`, which preserves the saved output. The
playback self-test now reports `device=Headphones (Bose QC35 II)` — i.e. the saved Bose output
survived startup. ✅

## Integration the orchestrator wired

- **`arrangeView.onEditMutated = [this]{ session.save(); }`** in `MainComponent`'s ctor — so
  structural edits made through the new arrange context menus / lane controls (add/delete/rename
  track, delete clip, set colour, mute/solo) are persisted. ArrangeView rebuilds itself before
  firing this, so the shell only needs to save.
- **`arrangeView.onArmToggled`** → real per-track record arm/disarm. The lane **R** button flips
  its visual state optimistically, then the shell calls `RecordController::armFirstInputToTrack`
  (true) or the new **`RecordController::disarmTrack`** (false). If the record path rejects the
  request (e.g. no capture device on this box), the shell pushes the true state back via the new
  **`ArrangeView::setTrackArmState`** (reverting the optimistic toggle) and shows
  `recorder.getLastError()` in the status strip. `disarmTrack` clears `setRecordingEnabled` and
  `removeTarget` for every wave `InputDeviceInstance` targeting the track, then `restartPlayback`.

## Seams deliberately left UNWIRED (documented, no code needed yet)

These ArrangeView callbacks exist and are safe to leave null; wire them when the target feature
lands:
- `onClipSelected(te::Clip*)` / `onTrackSelected(te::Track*)` → drive the **Inspector / Detail
  drawer** (Interface Phase 3/4). No inspector exists yet; the drawer is a placeholder Label.
- `ProjectSession::isModified()` → drive a Save-enabled state / prompt-on-close in the shell
  (the session exposes it; ControlBar would need a `setSaveEnabled(bool)`).

*(The arm-toggle status message is briefly overwritten by the 200 ms status-strip timer; a
dedicated transient-message area is future polish. Arm still can't succeed on this box until a
real capture input is selected — see "Recording verification" in §4 of STATUS.md.)*

## Synthetic-input record self-test — INVESTIGATED, NOT LANDED (future work)

Goal: let `--selftest-record` self-verify the record path with **no microphone**, using the
engine's `HostedAudioDeviceInterface` (a virtual device the test drives via `processBlock`). The
device agent shipped `EngineHelpers::installSyntheticInputForSelftest(...)` for this; the
orchestrator attempted to wire + drive it in `main.cpp`, then **reverted** the wiring after it
did not reach a captured take. The helper remains in `EngineHelpers.h` as documented
infrastructure (now also calling `prepareToPlay` + `dispatchPendingUpdates`, matching the engine's
own `test_utilities::EnginePlayer` recipe). `main.cpp` is back to the clean shell-agent version.

What was learned (so the next attempt starts here, not from zero):

1. **Install timing matters.** Installing the hosted interface in the `MainComponent` ctor works
   momentarily (`hostedInUse=1, getNumWaveInDevices()==1, currentDeviceType="Hosted Device"`), but
   a message-loop turn between the ctor and the async `beginSelfTestRecording` **resets the device
   type back to empty** (`typeAtBegin=(empty)`), tearing the hosted device down. Installing it
   **just-in-time inside `beginSelfTestRecording`** fixed that: `synthInstalled=1, hostedInUse=1,
   currentDeviceInputChans=1, trackArmed=1, recordingStarted=1`.

2. **Arm via the playback context, not the rescanned device list.** `RecordController::enableInputs()`
   calls `rescanWaveDeviceList()`, which on a real machine pulls in unopened Windows Audio /
   DirectSound capture descriptions (`inputDeviceCount` jumped to 8), so "arm the first wave input"
   can pick a silent one. The engine's own test arms from `context->getAllInputs()` filtered to
   `waveDevice` — that targets the open hosted input.

3. **Where it still fails:** even with hosting in use and the hosted input armed + recording
   started, **`posAtFinish=0.000`** — the transport never advanced under synthetic driving, and
   `recordedClipCount=0`. So the background thread's `processBlock` calls were **not driving this
   Edit's playback context/graph**. That is the open problem.

Likely next steps (for a future session):
- Confirm the driver thread is actually pumping (add a processed-block counter) and that the
  buffer shape (`max(in,out)` channels × blockSize) is what the hosted graph expects.
- Verify the Edit's playback context is the one bound to the hosted device (allocation order vs.
  `prepareToPlay`), and use `EditPlaybackContext::getUnloopedPosition()` to observe the playhead
  rather than `TransportControl::getPosition()`.
- Mirror `tracktion_TransportControl.test.cpp` more closely (its `ProcessThread` +
  `createEditWithTracksForInputs`) — it may require driving `processBlock` to "prime" the device
  *before* `ensureContextAllocated`, and a valid recording-destination directory for a temp Edit.
- The cleanest landing may be a dedicated `EnginePlayer`-style harness rather than threading a
  driver through the live GUI shell.

Real-hardware recording is still verified manually per `device-recording.md` (§"Verifying a real
take ..."): pick an input in the Audio dialog, then the existing `toggleRecordTake()` path records.

## Housekeeping notes

- **`.gitattributes`** (`* text=auto`) is in place to silence CRLF/LF warnings. The optional
  one-time `git add --renormalize .` was **not** run (it would produce a large, noisy diff);
  warnings are already silenced for new commits. Run it later as its own isolated commit if
  desired.
- **Interactive UI not auto-verified.** Keyboard shortcuts, drag-resizers, context menus, and
  per-lane controls **compile and construct** (the playback self-test builds the full shell +
  arrange view and renders a clip without crashing), but their interactive behaviour is not
  covered by the headless self-test. Manual or computer-use verification recommended before
  relying on them.
