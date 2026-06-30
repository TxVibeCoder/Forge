# Integration pass — Phase 2 fan-out

*Orchestrator's record of integrating the 5-agent parallel fan-out (arrange · session ·
device · shell · hygiene). Companion to the per-area devlogs in this folder.*

## Outcome

- **Builds green** (`cmake --build build --config Debug`) — all five agents' changes compiled
  and linked together on the **first** integration build. The exclusive-file-ownership +
  additive-only-interface discipline held; no cross-agent collisions.
- **Playback self-test PASS** (`Forge.exe --selftest`): `device=<the saved Bluetooth output>`,
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
default device and clobbered the saved output (Bluetooth headset → onboard device). After: the shell
calls `EngineHelpers::initialiseAudioForRecording(engine)`, which preserves the saved output. The
playback self-test now keeps the saved Bluetooth output device — i.e. that output survived
startup. ✅

## Integration the orchestrator wired

- **`arrangeView.onEditMutated = [this]{ session.save(); }`** in `MainComponent`'s ctor — so
  structural edits made through the new arrange context menus / lane controls (add/delete/rename
  track, delete clip, set colour, mute/solo) are persisted. ArrangeView rebuilds itself before
  firing this, so the shell only needs to save.
- **`arrangeView.onArmToggled`** → real per-track record arm/disarm. The lane **R** button flips
  its visual state optimistically, then the shell calls `RecordController::armFirstInputToTrack`
  (true) or `RecordController::disarmTrack` (false). Afterwards it calls
  **`ArrangeView::refreshArmStates()`**, which re-derives EVERY lane's R indicator from engine
  truth via the `isTrackArmed` query — so a rejected arm snaps back, and a lane whose single
  physical input was stolen by arming another track clears too. Arm state is the engine
  (`InputDeviceInstance` targets), never a transient UI flag — see the validation pass below.
  `disarmTrack` clears `setRecordingEnabled` and `removeTarget` for every wave instance (called
  unconditionally so a disabled device can't leave a stale target), then `restartPlayback`.

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

## Post-merge validation pass (adversarial review of aa0982c)

A 5-dimension adversarial review (lifetime · record · API · arrange · shell), each candidate
finding independently skepticism-verified against the real engine headers, surfaced **8
candidates → 5 confirmed real, 3 refuted**. All 5 were fixed (build stays warning-clean; playback
self-test PASS):

1. **(high) Arm indicator desynced from the engine after every `rebuild()`.** The lane's `armed`
   was a transient bool; mute/solo were read from the track but arm wasn't, so any structural edit
   (add/delete/rename track, delete clip, import) brought an actually-armed track back showing
   disarmed. **Fix:** arm is now derived from engine truth — `RecordController::isTrackArmed()`
   scans the wave `InputDeviceInstance` targets; `ArrangeView` exposes an `isTrackArmed` query
   that `refreshControlStates()` consults on every rebuild, exactly like mute/solo. The transient
   single-lane setters (`setArmed`/`setTrackArmState`) were removed to enforce one source of truth.
2. **(low) `disarmTrack` left a stale target when the input device was disabled.** `getTargets()`
   returns empty for a disabled device, so the old membership gate skipped cleanup, and a later
   re-enable could phantom-re-arm the track. **Fix:** call `removeTarget` unconditionally for every
   wave instance (it iterates persisted destinations directly); use `getTargets()` only to decide
   whether a *live* target was removed (whether to `restartPlayback`).
3. **(low) Arming a 2nd track silently stole the single input but left the 1st lane lit.**
   `setTarget(move=true)` drops all prior targets. **Fix:** `onArmToggled` now calls
   `refreshArmStates()` (re-derives ALL lanes from the engine), so the stolen lane clears.
4. **(low) Arm/disarm failure message wiped within 200 ms by the status-bar timer.** **Fix:**
   `setStatusMessage()` + a `statusHoldUntilMs` guard the periodic refresh respects (~4 s hold).
5. **(low) Bar-line detection dropped/spurious markers for non-exact tempos.** `getWholeBeats()==0`
   on a value round-tripped through `toTime()/toBarsAndBeats()` misfires by an epsilon. **Fix:**
   detect bar starts by a change in the integer `bars` field (robust); the first visible beat uses
   a tolerance fallback. (Also avoided round-tripping a negative beat, which is best avoided.)

Refuted (verified NOT bugs): a claimed key-truncation collision (`juce_wchar` is 32-bit on
Windows, no truncation); "interactive arm never enables inputs" (wave inputs default to enabled);
and a dangling-callback UAF (the `onClipSelected`/`onTrackSelected` consumers are unwired, so inert).

**Environment note (not a code bug):** when the Bluetooth output device disconnects and the default
device changes, app startup gets slow — `initialiseAudioForRecording` scans device types and opens a
default input, which took ~26 s to settle once (then playback PASS on the onboard device). It is
*latency*, not a hang (the committed baseline shows the same). A future hardening:
open inputs lazily / off the message thread so a slow capture-device open can't stall startup.

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

## Build wave 2 — Mixer view · volume/pan · clip drag+snap · WAV export

A second 4-agent file-disjoint wave (per-area devlogs: `mixer.md`, `engine-mix.md`,
`arrange-drag.md`, `export.md`). **Integration build: 0 warnings, 0 errors on the first try.**

- **MixerView** (`src/ui/mixer/MixerView.{h,cpp}`, new) — a real Mixer view: one channel strip per
  audio track (dB volume fader, rotary pan, M/S, colour swatch) in a horizontal Viewport,
  replacing the placeholder Label. Volume/pan are pushed live to the engine via the new
  `EngineHelpers::get/setTrackVolumeDb` + `get/setTrackPan` (which drive each track's
  `VolumeAndPanPlugin`). Manual-rebuild model like ArrangeView (`setEdit`).
- **Clip drag-to-move + bar snap + info hint** (`ArrangeView`) — horizontal clip drag commits via
  `Clip::setStart(...)` and fires `onEditMutated`; snap-to-bar (default on, Ctrl bypasses);
  one-line hint strip. The arrange agent also fixed a *latent pre-existing bug*: the full-width
  `PlayheadComponent` (intercepts mouse) shadowed every clip, so clips received no mouse events —
  fixed with a `hitTest` that only grabs the ±5px playhead band; empty-area clicks still scrub.
- **WAV export** (`src/services/export/Exporter.{h,cpp}`, new) — `Exporter::renderEditToWav` renders
  the whole edit to a 24-bit stereo WAV via the engine renderer. Wired to a new ControlBar
  **Export** button → save-chooser → render, status via the `setStatusMessage` hold.

Orchestrator integration: added both new `.cpp` to CMakeLists; swapped the `mixerPanel` Label for a
`MixerView` in the view-slot with `setEdit` wired at ctor/rebind/swapProject; added the Export
button + `onExport` to ControlBar and an `exportDialog()` (SafePointer-guarded, own FileChooser) to
the shell.

**Runtime verification status:** the build is definitive, but the headless playback self-test could
NOT be cleanly run afterward because the audio environment is degraded — the Bluetooth headset
disconnected (default → onboard device) and repeated launch/kill cycles during diagnosis left the WASAPI
device contended, so `initialiseAudioForRecording` startup negotiation ballooned to 26–77 s and one
run came back `playing=0` (device couldn't sustain playback). This is environmental, affects the
committed baseline identically, and is unrelated to this wave's code (all new code is UI/service and
never touches device init — `importedClip=1`/`numClipComponents=1` confirm the non-device path is
fine). Re-run the self-test after reconnecting a stable audio device (or a reboot to clear the
WASAPI lock); it passed at 26 s earlier this session on equivalent code. This re-confirms the
standing hardening item: **open audio inputs lazily / off the message thread** so a slow capture
device can't stall startup.

## Build wave 3 — plugin hosting · mixer inserts/meters/master · file browser · clip inspector

A third 4-agent file-disjoint wave (per-area devlogs: `plugins.md`, `mixer-fx.md`, `browser.md`,
`detail.md`). **Integration build: 0 warnings, 0 errors on the first try** — including the complex
plugin-hosting engine integration. Headless smoke test: the full shell + all four new components
construct without crashing (`importedClip=1`, `numClipComponents=1`, `exit=0`); `playing=0` remains
the degraded-device environmental issue above, not a code fault.

- **Plugin hosting** (`src/engine/PluginHost.{h,cpp}` + `src/ui/plugins/PluginWindow.{h,cpp}`, new) —
  `getAvailablePluginNames` (built-in effects: EQ, Compressor, Reverb, Delay, Chorus, Phaser, … +
  any scanned externals), `addPluginToTrack` (inserts before the volume/level tail), `getTrackInserts`,
  `removePlugin`. `PluginWindow::show` opens a floating editor — the external plugin's native editor,
  or a generated parameter panel for built-ins (which have no native UI); windows auto-close when
  their plugin/Edit goes away (`closeAll`).
- **Mixer inserts + master + meters** (`MixerView`) — per-strip insert list (+ to add via the plugin
  menu, click to open editor, remove), a fixed master strip on `edit.getMasterVolumePlugin()`, and
  ~28 Hz peak meters reading each track's `LevelMeterPlugin` measurer (master-meter data hookup is
  best-effort — only fed if the master chain already has a `LevelMeterPlugin`).
- **Browser** (`src/ui/browser/BrowserView.{h,cpp}`, new) — a `FileTreeComponent` file browser for
  the left region; double-click an audio file → `onImportFile` → import.
- **Clip inspector** (`src/ui/detail/DetailView.{h,cpp}`, new) — the bottom drawer now shows the
  selected clip (name, gain, mute, fades, position, larger waveform); holds the clip as a
  `te::Clip::Ptr` so it can't dangle. Selecting a clip auto-opens the drawer.

Orchestrator integration: registered the 4 new `.cpp` in CMakeLists; replaced the Browser + Detail
placeholder Labels with the real `BrowserView`/`DetailView` (kept the `browserPanel` member name to
reuse layout call-sites; `drawerPanel` → `detailView`); wired `onImportFile`, `onClipSelected →
detailView.setClip` (auto-opens the drawer), and `detailView.onEditMutated → save`; and added
`PluginWindow::closeAll()` to the dtor + `swapProject` (with `detailView.setClip(nullptr)`) so
plugin editors and the inspector never outlive their Edit. `setupPlaceholders` became
`setupStatusStrip` (no placeholders left to style).

Three placeholder regions (Browser, Detail-drawer, Mixer) are now all real. Remaining roadmap
headliners after this: external VST3/AU **scanning** UI (the host can already load what's in the
known list), MIDI tracks + piano roll, automation, and metering on the master output node.

## Build wave 4 — Phase 4 polish: snap grid · stem export · plugin scan · mixer bypass/reorder + master meter

A fourth 4-agent file-disjoint wave, then a dedicated adversarial-review wave, committed as
**`ad7d885`**. **Integration build: 0 warnings, 0 errors on the first try; playback `--selftest`
PASS** (both before and after the review fixes). Per-feature detail lives in the agents' reports;
this section is the orchestrator's record + the review outcome + where things stand.

### What each agent delivered

| Area | Files | Highlights |
|---|---|---|
| snap | `ui/arrange/ArrangeView.{h,cpp}` | snap-division selector (Off/Bar/½/¼/⅛/1⁄16) in the ruler corner; clip drag-to-move snaps to the chosen division; legacy `setSnapEnabled`/`isSnapEnabled` preserved as a wrapper over the division model |
| stems | `services/export/Exporter.{h,cpp}` | `renderStems` — each non-empty audio track → its own 24-bit WAV in a chosen folder; additive (whole-edit `renderEditToWav` unchanged). Per-track render bitset built by hand because the engine's `toBitSet` helper sets a bit for *every* track on single-track input |
| scan | `engine/PluginScanner.{h,cpp}` (new) | JUCE `PluginListComponent` scan dialog bound to the engine's `pluginFormatManager` + `knownPluginList`; persists automatically (PluginManager change-listener → property storage); newly-found plugins surface in `PluginHost::getAvailablePluginNames` |
| mixer | `ui/mixer/MixerView.cpp` | per-insert **bypass** dot (`te::Plugin::setEnabled`) + ▲/▼ **reorder** within the chain (volume/meter tail preserved); **master meter** |

### Orchestrator integration (the files only the orchestrator owns)

- **CMakeLists.txt** — added `src/engine/PluginScanner.cpp` to `target_sources`.
- **ControlBar.{h,cpp}** — new **Plugins** button → `onScanPlugins`; the **Export** button now opens
  a popup (`showExportMenu`: *Mixdown (WAV)* → `onExport`, *Stems* → `onExportStems`) rather than
  exporting directly, so no extra top-bar button is needed for stems.
- **main.cpp** — `#include "engine/PluginScanner.h"`; `onScanPlugins → PluginScanner::showScanDialog
  (engine)`; `onExportStems → exportStemsDialog()` — a new directory chooser (SafePointer-guarded,
  own `stemsChooser` member) that calls `Exporter::renderStems`.
- Snap (A) and mixer (D) are **self-contained** — reached through the existing `setEdit` path, no
  wiring added.

### Adversarial-review wave — 4 candidates → 3 confirmed real, 1 refuted

5 reviewers (one per feature + the integration wiring), each candidate finding independently
skeptic-verified against the real engine headers (default stance: *refuted unless proven*). Stems,
scan, and integration came back clean. The 3 confirmed bugs were all fixed (rebuild green, selftest
PASS):

1. **(major · snap) Snap grid wrong in non-4/4 time signatures.** `gridStepInBeats` hardcoded
   quarter = 1.0 / eighth = 0.5 / sixteenth = 0.25 *engine beats*, but under Tracktion's default
   `LengthOfOneBeat::dependsOnTimeSignature` one engine beat is one *denominator*-note, not always a
   quarter (`tracktion_EngineBehaviour.h:200`; `tracktion_Tempo.h:586` → `secondsPerBeat =
   240/(bpm·denominator)`). So in 6/8, "1/4" snapped to eighth boundaries, etc. Latent (the app only
   ever creates 4/4) but reachable by opening an authored non-4/4 `.tracktionedit`. **Fix:** scale
   the musical divisions by `denominator/4`, reading the denominator from
   `tempoSequence.getTimeSigAt(t)`; the 4/4 path is unchanged.
2. **(major · mixer) Opening the mixer mutated a clean Edit.** `ensureMasterLevelMeter()` inserted a
   `LevelMeterPlugin` into the *persisted* `MASTERPLUGINS` list via `insertPlugin(...,
   getUndoManager())` — so merely opening the Mixer dirtied a clean project, wrote a spurious plugin
   to disk, and pushed an undo transaction. **Fix:** removed the insertion entirely; the master meter
   now binds to `EditPlaybackContext::masterLevels` (the engine's own master-output measurer, the
   same one control surfaces read), re-bound each poll because the playback context comes and goes
   with the transport graph. **Zero Edit mutation.**
3. **(minor · mixer) Master meter read pre-fader.** The master plugin list is rendered *before* the
   master volume plugin (`tracktion_EditNodeBuilder.cpp:1772-1780`), so an inserted meter measured
   pre-fader — inconsistent with the post-fader track meters. **Fix:** subsumed by #2 —
   `masterLevels` is measured at the master output, i.e. post-fader.

**Refuted** (verified NOT a bug): the Export-menu `showMenuAsync` lambda captures `[this]`
(ControlBar) with no `withDeletionCheck` — but ControlBar's lifetime equals the window's (it is a
plain `MainComponent` member, never reconstructed on project swap), so the action callback can never
fire after `this` is destroyed. Matches the `[this]` idiom of every other ControlBar button.

### Where things stand

Phases 0–3 plus this Phase-4 polish are committed and build clean. The arrange surface has a working
**snap-division grid**; **export** does mixdown *and* per-track stems; external **plugins are
scannable** from the toolbar and flow into the insert menu; the **mixer** has per-insert bypass +
reorder and a correct, non-mutating, **post-fader master meter**.

**Verification gap (honest, per project doctrine):** the clean build + playback selftest confirm
everything *constructs and plays*. The new *interactive* paths — stem-export output files, the scan
dialog, mixer bypass/reorder, and the snap selector UI — are **not** covered headlessly. Manual or
computer-use verification is recommended before relying on them. The snap fix only changes behaviour
for non-4/4 projects (which the app never creates itself), so it is correctness-grounded against the
engine headers but not exercised by the 4/4 selftest.

**Standing items unchanged by this wave** (see STATUS.md §4): **startup-latency hardening** (open
audio inputs lazily / off the message thread — still the highest-leverage reliability fix);
**async export + progress** (both mixdown and stems block the message thread); and the big remaining
capability, **MIDI tracks + piano-roll**.
