# W7 — MIDI Record into Session Clip Slots (FINAL, build-ready)

Status: FROZEN design. Every contract, ordering rule, and risk below is source-verified against the
vendored `libs/tracktion_engine` and current Forge `src/` at authoring time (citations inline). The
orchestrator alone edits `CMakeLists.txt`, `src/main.cpp`, and integration wiring, and runs the single build.

---

## 0. Decisive slot-recording verdict

> **UPDATE (shipped `160f6cc`, 2026-07-01): verdict (A) is now EMPIRICALLY PROVEN.** The `--selftest-midi` gate
> (§4) records 4 synthetic MIDI notes straight into slot `(0,0)` and asserts the committed clip holds exactly
> those notes — PASS. The "untested in-engine" caveat below is resolved: this path is now runtime-verified.

**VERDICT: (A) — Direct slot recording IS supported and is the W7 path.**

A `te::ClipSlot` is a `ClipOwner` (`tracktion_ClipSlot.h:18-20`) and a first-class record target. The
capture pipeline is:

1. Arm a MIDI `InputDeviceInstance` to the **slot's** `itemID` — `setTarget(slot.itemID, /*moveToTrack=*/false, um, index)` (`tracktion_InputDevice.cpp:135-171`).
2. `setRecordingEnabled(slot.itemID, true)` (`tracktion_InputDevice.cpp:217`).
3. **Roll the transport with `transport.record(false)`** — this is what starts capture.
4. On **stop**, the engine calls `MidiInputDevice::addMidiAsTransaction(edit, slot.itemID, …)`, which resolves
   the target as a `ClipSlot` (`findClipSlotForID`), does `clipSlot->setClip(nullptr)` then
   `insertMIDIClip(*clipOwner, …)` — the born-audible `MidiClip` materialises **in the slot**
   (`tracktion_MidiInputDevice.cpp:692-777`). **No takes** are created for slot recordings; `isLooping` and
   `wasPunchRecording` are forced false (`tracktion_MidiInputDevice.cpp:1120-1122, 1129`).

**What (A) means for the recipe — and the single biggest correction to the incoming design:**

> **The slot is NOT launched to record.** Recording is driven by the edit-global transport
> (`transport.record()` → `EditPlaybackContext::prepareForRecording` → `startRecording`, which records every
> input where `mi->isRecordingActive()` is true — `tracktion_EditPlaybackContext.cpp:705, 731, 795-798`).
> `isRecordingActive()` is pure `destination.recordEnabled` with **zero coupling to `LaunchHandle`**
> (`tracktion_InputDevice.cpp:189-196`). An empty armed slot has no clip and therefore no `LaunchHandle` to
> launch, so `ProjectSession::launchSlot` is a hard no-op on it (`ProjectSession.cpp` — `if (auto c =
> slot->getClip())` guard). **Do not call `launchSlot` on the record path.** The prior design's "launchSlot so
> the LaunchHandle reaches playing" (risk #7) is deleted.

Caveat honestly recorded: ~~this code path is **untested in-engine**~~ **RESOLVED — now proven** (`160f6cc`):
at authoring time no vendored test or example armed a `ClipSlot` as a record target (`ClipLauncherDemo` only
pre-authors clips into slots, no record code), so the path was untested in-engine. The `--selftest-midi` gate
below is the **first proof** of the path and now **PASSES** — verdict (A) is empirically confirmed. The
verdict-(B) track-target fallback (`armFirstMidiInputToTrack` + a commit-move step) is kept fully specified so a
downgrade is additive, not a redesign.

---

## 1. Frozen NEW contracts (exact header signatures)

### 1a. `RecordController` (`src/engine/RecordController.h`) — additive, new symbols only

```cpp
// ---- MIDI enable (NOT a clone of enableInputs: no setStereoPair; rescanMidiDeviceList, not dispatch) ----
/** Enables every MIDI-in device with automatic monitoring, then async-rescans the MIDI list.
    Iterates dm.getMidiInDevices() (std::shared_ptr<MidiInputDevice>); setEnabled(true) +
    setMonitorMode(MonitorMode::automatic) on each; then dm.rescanMidiDeviceList() (ASYNC). MUST run
    AND be allowed to settle (yield) BEFORE any arm, so the isEnabled()&&isAvailableToEdit() gate in
    EditPlaybackContext::getAllInputs() admits the instance. Message-thread only. */
void enableMidiInputs();

/** Count of engine MIDI-in devices (rescans async first; treat as diagnostic — re-query after a yield). */
int  getMidiInputDeviceCount() const;

/** Names of known MIDI-in devices (diagnostics/UI). Rescans first. */
juce::StringArray getAvailableMidiInputDeviceNames() const;

// ---- ARM: slot is the primary W7 target (VERDICT A) ----
/** Arms the first enabled MIDI input to RECORD INTO `slot`. Steps: ensureContextAllocated(); pick the
    first MIDI InputDeviceInstance whose device type is in {physicalMidiDevice, virtualMidiDevice,
    trackMidiDevice} and isEnabled(); setTarget(slot.itemID, /*moveToTrack=*/false, &um, index) — false so a
    slot arm is ADDITIVE (does not wipe a co-existing arm); GUARD the returned value is non-null (a nullptr
    success means the itemID did not resolve to a ClipSlot); setRecordingEnabled(slot.itemID, true);
    restartPlayback(). Returns true on success; sets getLastError() on failure. Message-thread only; obey
    the async-arm rule. MUST be called while the transport is STOPPED (setTarget fails while recording). */
bool armFirstMidiInputToSlot (te::Edit&, te::ClipSlot&);

/** VERDICT-(B) fallback / plain "record to track" MIDI variant: arms MIDI to the whole track
    (setTarget(track.itemID, /*moveToTrack=*/true, …) — exclusive, audio-parity). */
bool armFirstMidiInputToTrack (te::Edit&, te::AudioTrack&);

// ---- DISARM (unconditional removeTarget; REQUIRES transport stopped) ----
/** Clears the MIDI record assignment for `slot`. For EVERY MIDI instance (even disabled):
    setRecordingEnabled(slot.itemID,false) then removeTarget(slot.itemID,&um) UNCONDITIONALLY (never gated
    on getTargets().contains — disabled devices hide targets). restartPlayback only if a live target was
    removed. PRECONDITION: transport stopped — setTarget/removeTarget fail while isRecording(). */
bool disarmSlot (te::Edit&, te::ClipSlot&);
bool disarmMidiTrack (te::Edit&, te::AudioTrack&);

/** Authoritative arm read (re-derivable every 25 Hz poll): true iff some MIDI InputDeviceInstance has
    slot.itemID / track.itemID in getTargets(). Filter getDeviceType() to the MIDI group. Pure read. */
bool isSlotMidiArmed  (te::Edit&, te::ClipSlot&) const;
bool isTrackMidiArmed (te::Edit&, te::AudioTrack&) const;

// getLastError() reused as-is.
```

> Design note on the `moveToTrack` flag (real param name at `tracktion_InputDevice.h:128`): the exclusive-clear
> branch is `if (owner.isTrackDevice() || moveToTrack)` (`.cpp:149`). For a **physical/virtual** MIDI input,
> `moveToTrack=false` is additive as specified. A `trackMidiDevice` input is **always** exclusive regardless of
> the flag — acceptable for W7 (we target physical/virtual MIDI ins in the selftest and the common case).

### 1b. `ProjectSession` (`src/services/files/ProjectSession.h`) — Session record seam

The view makes **no** raw `te::` record calls; this is the only new engine entry point the record gesture
touches. Injection seam: `ProjectSession` gains a `std::function`-based hook to the `RecordController`
(orchestrator wires it in `main.cpp`), so `ProjectSession` keeps no hard `RecordController` dependency.

```cpp
/** Arm cell (trackIndex, sceneIndex) for MIDI slot recording (VERDICT A). Idempotent. Orchestrates the
    born-audible + arm recipe so callers never touch raw engine APIs: ensureScenes(sceneIndex+1); ensure the
    track exists; PluginHost::ensureDefaultInstrument(track) so the captured clip is born-audible (4OSC);
    resolve the ClipSlot (getClipSlot); hand off to RecordController::armFirstMidiInputToSlot (via the
    injected recorder seam). Does NOT pre-insert a clip — the engine creates it at commit. Returns true iff
    the slot ends armed; sets no clip. Message-thread only. */
bool recordArmSlot (int trackIndex, int sceneIndex);

/** Disarms cell (trackIndex, sceneIndex). Wraps RecordController::disarmSlot. Stops the transport FIRST if
    it is recording (removeTarget fails while recording). Message-thread only. */
void recordDisarmSlot (int trackIndex, int sceneIndex);

/** True iff cell (trackIndex, sceneIndex) is armed for MIDI record (re-derived from engine targets each
    call — no cached flag). Feeds SlotVisualState::recArmed in the 25 Hz poll. */
bool isSlotRecordArmed (int trackIndex, int sceneIndex) const;

/** Begins capture: ensures the track has a default instrument, then rolls the transport with
    transport.record(false). Records because the slot's destination is recordEnabled — NOT because the slot
    is launched (there is no clip to launch yet). No-op if no slot is armed. Message-thread only. */
void beginSlotRecord (int trackIndex, int sceneIndex);

/** Ends capture: transport.stop(false,false); the engine commits the captured notes into a new MidiClip in
    the slot via addMidiAsTransaction on stop. Then disarms the slot. Returns the resulting clip if one was
    captured (getClipSlot(...)->getClip() dyn_cast to MidiClip), else {}. Message-thread only. */
te::MidiClip::Ptr commitSlotRecord (int trackIndex, int sceneIndex);

/** True iff cell (trackIndex, sceneIndex) is the slot currently CAPTURING: it is the armed record target
    (RecordController::isSlotMidiArmed) AND edit.getTransport().isRecording(). Pure engine read — NOT
    "LaunchHandle playing" (which never becomes true for an empty capturing slot). Drives the recording pad.
    Message-thread only. */
bool isSlotRecording (int trackIndex, int sceneIndex) const;
```

> `recordMidiIntoSlot` / `stopSlotRecording` from the incoming UX draft collapse into the pair
> `beginSlotRecord` + `commitSlotRecord` above (single active record slot tracked by ProjectSession). The
> shell wires the UX seams (`onSlotRecord`, `isSlotRecording`) to these.

### 1c. `SessionView` shell seams (`src/ui/session/SessionView.h`) — mirror the existing arm pair

```cpp
/** Engine-truth: is this track MIDI record-armed? Set by the shell. ORed with the audio isTrackArmed in
    the poll to drive the recArmed pad tint (a MIDI-armed track must tint its empty slots too). */
std::function<bool (te::AudioTrack&)> isTrackMidiArmed;

/** Toggle MIDI record-arm on a track (routes to RecordController MIDI path). */
std::function<void (te::AudioTrack&, bool arm)> onMidiArmToggled;

/** Arm a SPECIFIC empty slot as the record destination and begin capture (arm slot + roll transport;
    NO launch). (trackIdx, sceneIdx) identify the slot. */
std::function<void (int trackIdx, int sceneIdx)> onSlotRecord;

/** Stop the active slot recording (commit). */
std::function<void (int trackIdx, int sceneIdx)> onSlotRecordStop;

/** True iff (trackIdx,sceneIdx) is the slot currently capturing MIDI. Set by shell → session.isSlotRecording.
    Drives SlotVisualState::recording. */
std::function<bool (int trackIdx, int sceneIdx)> isSlotRecording;
```

### 1d. `SlotVisualState` (`src/ui/session/SlotVisualState.h`) — new state + cross-file ripple

```cpp
enum class SlotVisualState { empty, hasClip, queued, playing, stopping, recArmed, recording };

/** recordingHere (4th param) DOMINATES all clip/queue states for that one pad — checked FIRST. */
inline SlotVisualState computeSlotState (te::ClipSlot* slot, bool transportRunning,
                                         bool armed, bool recordingHere);
```

`toPadFeedback`: `recording → { colourIdx = redHueIdx (1), state = 2 (pulse) }` — pulsing red, distinct from
`recArmed`'s solid red and `playing`'s hue-pulse.

**Cross-file ripple (MUST be done together — the enum is a cross-file contract):**
- `ClipSlotComponent::paint` (`ClipSlotComponent.cpp:81`): exclude `recording` from the `hasClip` predicate
  and add an explicit `else if (state == SlotVisualState::recording)` branch painting pulsing-red chrome.
  Otherwise a recording pad renders as an ordinary track-colour clip body.
- Audit every `switch`/predicate over `SlotVisualState` (paint, `toPadFeedback`, any label logic).
- `lastSlotState` diff-buffer priming in `rebuild()` seeds `empty` (benign; the first post-rebuild
  `refreshSlotStates()` recomputes both new states from engine truth).

### 1e. SelfTest hook (`src/main.cpp`, orchestrator-owned)

```cpp
enum class SelfTest { none, playback, record, session, screenshot, midi };   // + midi ("--selftest-midi")
```

---

## 2. Record-layer behavior

### MIDI enable sequence (`enableMidiInputs`) — verified vs `MidiInputDevice.cpp` / `DeviceManager`
```
auto& dm = engine.getDeviceManager();
for (auto mi : dm.getMidiInDevices())            // std::vector<std::shared_ptr<MidiInputDevice>>
    if (mi) { mi->setEnabled (true); mi->setMonitorMode (te::InputDevice::MonitorMode::automatic); }
dm.rescanMidiDeviceList();                        // ASYNC — startTimer(5); NOT dispatchPendingUpdates.
```
Load-bearing divergences from audio `enableInputs()`: **no `setStereoPair`** (absent on `MidiInputDevice`);
**`rescanMidiDeviceList()` not `dispatchPendingUpdates()`**. Never trust the ctor `enabled=true` default
(`loadMidiProps` can override it) — always `setEnabled(true)` explicitly, then **yield** before arming.

### The gate: `isEnabled() && isAvailableToEdit()` (`EditPlaybackContext::getAllInputs` :551-553)
A MIDI instance is added to `getAllInputs()` only if it passes this gate. So `enableMidiInputs()` MUST run and
its async rescan MUST settle **before** `ensureContextAllocated()`/arm — else the edit has no MIDI instance to
arm. This is why arm is split across a message-loop yield (async-arm rule).

### Arm (`armFirstMidiInputToSlot`)
1. `lastError = {}`.
2. `edit.getTransport().ensureContextAllocated()`; if `getCurrentPlaybackContext()==nullptr` → `FORGE_LOG_ERROR` + set `lastError`.
3. Iterate `edit.getAllInputDevices()`; skip null; skip unless `getDeviceType()` in the MIDI group; count MIDI instances.
4. Skip if `!instance->getInputDevice().isEnabled()` (record precise reason).
5. `auto r = instance->setTarget (slot.itemID, /*moveToTrack=*/false, &edit.getUndoManager(), index);`
   - **`moveToTrack=false` is deliberate** (`.cpp:149`): additive so one instance can target several slots and
     a slot arm does not silently un-arm a track.
   - `if (! r) { lastError = r.error(); FORGE_LOG_DEBUG; continue; }`
   - **`if (r.value() == nullptr) { lastError = "slot itemID did not resolve to a ClipSlot"; continue; }`** —
     a value-initialised `tl::expected` is a *success* holding a null `Destination*` when the id resolves to
     neither track nor slot (`.cpp:143-144`). Without this guard the arm silently no-ops and reports success.
6. `instance->setRecordingEnabled (slot.itemID, true); edit.restartPlayback(); return true;`
7. Exhausted → set `lastError` precisely (none seen / all failed / none enabled) and return false.

`armFirstMidiInputToTrack` is identical but `targetID = track.itemID` and `moveToTrack=true` (exclusive).

> **PRECONDITION for arm: transport STOPPED.** `setTarget`/`removeTarget` early-return failure while
> `isRecording()` (`.cpp:146-147, 175-176`). Arm before rolling; disarm after stopping.

### Disarm (`disarmSlot` / `disarmMidiTrack`)
For every MIDI instance (**including disabled** — do NOT gate on `getTargets().contains`, same rationale as
`RecordController::disarmTrack:155-167`): `wasActive = getTargets().contains(id)`;
`setRecordingEnabled(id,false)`; `removeTarget(id,um)` unconditionally; if `!ok` `FORGE_LOG_ERROR` + remember;
else if `wasActive` set `removedActive`. `restartPlayback()` only if `removedActive`. **Stop the transport
first** if recording — otherwise `removeTarget` silently no-ops and leaves a stale armed target.

### `isSlotMidiArmed` / `isTrackMidiArmed`
Pure read — iterate MIDI instances, return `getTargets().contains(id)`. No mutation; safe from the 25 Hz poll.

### Transport → capture → commit (`ProjectSession`)
- **`beginSlotRecord`**: `PluginHost::ensureDefaultInstrument(track)` (born-audible 4OSC; omit → silent
  captured clip); `transport.record(false)`. **No `launchSlot`.** The armed slot's `recordEnabled` destination
  is what `startRecording` captures.
- **capture window**: engine collects live MIDI. On stop, `MidiInputDevice::applyRecording` →
  `addMidiAsTransaction(edit, slot.itemID, …)` resolves the slot (`findClipSlotForID`), `clipSlot->setClip(nullptr)`,
  `insertMIDIClip(*clipOwner, …)`, and `mergeInMidiSequence` fills the born-audible clip
  (`tracktion_MidiInputDevice.cpp:734-757`). **No takes** (`:1120-1122`).
- **`commitSlotRecord`**: `transport.stop(false,false)`; the clip is now committed; `disarmSlot`; return the
  `MidiClip` in the slot or `{}`.

### Captured-clip length (corrected)
The clip runs from the transport position at record-start to the position at stop
(`recordedRange = { punchRange.getStart(), unloopedEndTime }`, `tracktion_MidiInputDevice.cpp:1141`;
`getDefaultRecordingParameters` sets punch end to max edit time for the non-punch case). **There is no
launch-quantise rounding on the record range** (quantisation only quantises note timestamps, `:709`); and
`wasPunchRecording` is forced false for slots. For a bar-aligned loop, Forge must start/stop on bar boundaries
itself — the engine will not round the clip length.

### Async-arm discipline (load-bearing)
NEVER `enableMidiInputs()` + arm inside one blocking callback. UI and selftest both split: enable → **yield
(~200 ms, `startTimer`)** so `rescanMidiDeviceList` + device rebuild settle → arm + `setRecordingEnabled`.

### Logging seams (all off-RT, per build principle)
`FORGE_LOG_ERROR` when `ensureContextAllocated` yields null, when `removeTarget` fails during disarm, when
`recordArmSlot` cannot resolve slot/track. `FORGE_LOG_DEBUG` per-instance `setTarget` failure (non-fatal).
`FORGE_LOG_WARN` if `enableMidiInputs` finds zero MIDI-in devices. `getLastError()` carries the precise
user-facing arm reason. **No logs** in `isSlot/TrackMidiArmed` / `isSlotRecording` (poll path) or in
`beginSlotRecord`'s per-tick region.

---

## 3. Session UX + transport interaction + pad states + main.cpp wiring

### Gesture model (does NOT regress launch/create)
Single-click a filled slot launches; single-click an empty MIDI slot creates a born-audible clip + opens the
drawer. Record-arm-a-slot must not collide with either.
- **Track-level MIDI arm**: the arm button. `SessionView::wireColumn`'s `onArm` lambda branches on
  `trackIsMidi(t)` → `onMidiArmToggled` (MIDI) vs `onArmToggled` (audio). *(The branch lives in SessionView —
  the shell never sees `column.onArm`; the earlier "shell branches at the onArm callsite" language is deleted.)*
  Arming tints every empty slot on that track `recArmed`.
- **Slot record trigger (new gesture)** — never a plain single-click:
  - **Ctrl+Enter** on the focused empty slot of a MIDI-armed track → `onSlotRecord(focusTrack,focusScene)`.
    Added as a branch in `SessionView::keyPressed` **before** the plain-`returnKey` handler:
    `if (key.isKeyCode(returnKey) && key.getModifiers().isCommandDown()) { if (midi-armed && slot empty)
    onSlotRecord(...); return true; }`. This is safe: `SessionView` is the focused child and runs before the
    parent `MainComponent::keyPressed`, whose `isCommandDown` block returns false for unrecognised combos
    (`main.cpp:636`) and never sees Enter anyway.
  - **Right-click** item "Record into slot" (new id, must not collide with `{idLaunch=1,idStop,idEdit,idImport}`),
    shown ONLY when the slot is empty AND `isTrackMidiArmed`. While recording, the menu shows "Stop recording"
    → `onSlotRecordStop`.

### Transport interaction (arm → record → stop-commits)
1. **Arm** (`onMidiArmToggled(true)`): `EngineHelpers::ensureRecordingInputOpen` + `recorder.enableMidiInputs()`
   + `recorder.armFirstMidiInputToTrack`. Async-arm rule: enable happens on the arm-button press; the actual
   capture is a later, separate gesture, so the yield is naturally satisfied.
2. **Record** (`onSlotRecord(t,s)`): `session.recordArmSlot(t,s)` (ensure instrument + arm the SLOT itemID)
   then `session.beginSlotRecord(t,s)` (roll `transport.record(false)`). **No launch.**
3. **Stop / commit** (`onSlotRecordStop(t,s)`): `session.commitSlotRecord(t,s)` — `transport.stop`, engine
   materialises the clip in the slot, disarm. The pad transitions `recording → hasClip` on the next poll (the
   clip now resolves via `getClipSlot`).

### Pad visual states + LED encoding
- **`recArmed`** (existing): empty pad on an armed track → solid red `(colourIdx=1, state=0)`. Idle-armed.
- **`recording`** (new): the ONE capturing slot → pulsing red `(colourIdx=1, state=2)`. Dominates all
  clip/queue states.
- After commit: normal `hasClip`.
- The 25 Hz poll resolves `recordingHere` once per pad from `isSlotRecording(t,s)` (cheap engine read; R1
  preserved — no `ClipSlot*` cached). `recording` is checked FIRST so a mid-capture pad reads "hot".

### recArmed tint must reflect MIDI arm (poll fix)
The poll currently computes `tp.armed = isTrackArmed(*track)` (audio only). Change to:
`tp.armed = (isTrackArmed && isTrackArmed(*track)) || (isTrackMidiArmed && isTrackMidiArmed(*track))`
so a MIDI-armed (audio-disarmed) track tints its empty slots. **v1 rule: audio/MIDI arm are mutually
exclusive per track** (arming one disarms the other) so the single R indicator + single recArmed tint stay
unambiguous.

### main.cpp wiring (orchestrator-owned; mirrors the `onArmToggled` block at `main.cpp:372`)
```cpp
sessionView.isTrackMidiArmed = [this](te::AudioTrack& t){
    auto* ed = session.getEdit(); return ed && recorder.isTrackMidiArmed(*ed, t); };

sessionView.onMidiArmToggled = [this](te::AudioTrack& track, bool arm){
    if (auto* ed = session.getEdit()) {
        if (arm) { EngineHelpers::ensureRecordingInputOpen(engine); recorder.enableMidiInputs(); }
        const bool ok = arm ? recorder.armFirstMidiInputToTrack(*ed, track)
                            : recorder.disarmMidiTrack(*ed, track);
        if (!ok) { setStatusMessage("MIDI arm failed: " + recorder.getLastError());
                   FORGE_LOG_ERROR("MIDI arm failed: " + recorder.getLastError()); }
        sessionView.refreshArmStates();
    } };

sessionView.onSlotRecord = [this](int t,int s){
    if (!(session.recordArmSlot(t,s))) { setStatusMessage("Slot arm failed: " + recorder.getLastError());
        FORGE_LOG_ERROR("Slot arm failed: " + recorder.getLastError()); return; }
    session.beginSlotRecord(t,s); };

sessionView.onSlotRecordStop = [this](int t,int s){ session.commitSlotRecord(t,s); };
sessionView.isSlotRecording  = [this](int t,int s){ return session.isSlotRecording(t,s); };
```
Teardown: on edit-close, disarm MIDI (`recorder.disarmMidiTrack` per armed track — stop transport first)
before `setEdit(nullptr)`. R4 teardown unchanged; new seams are `std::function` members cleared on destruction.

---

## 4. `--selftest-midi` headless gate (event-driven synthetic MIDI → slot)

Goal: prove MIDI capture straight into a Session `ClipSlot` with **zero hardware**, mirroring
`--selftest-record`'s yield discipline. Lives in `MainComponent` (`src/main.cpp`), additive.

### Synthetic-MIDI seam (verified)
Create a virtual input: `engine.getDeviceManager().createVirtualMidiDevice("Forge SelfTest MIDI")` — returns
`juce::Result` (NOT a pointer; async, calls `rescanMidiDeviceList`) (`tracktion_DeviceManager.h:192`). Locate
the device by name via `getMidiInDevices()` after the yield.

Inject via the **public** override `VirtualMidiInputDevice::handleIncomingMidiMessage(const juce::MidiMessage&,
MPESourceID)` (`tracktion_VirtualMidiInputDevice.h:22-23`), using `dev->getMPESourceID()`
(`tracktion_MidiInputDevice.h:109`):
```cpp
miDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOn (1, note, 0.8f),  miDevice->getMPESourceID());
miDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOff(1, note),        miDevice->getMPESourceID());
```
> **Do NOT use `handleNoteOn/handleNoteOff`** — those are `protected` `MidiKeyboardStateListener` overrides
> (`tracktion_MidiInputDevice.h:158-159`), not callable from the harness and not MIDI-message injectors.

### Phases (mirror the record state machine: `beginSelfTestRecording` / `armAndStartRecording` /
`finishSelfTestRecording`; timerCallback dispatch mirrors `main.cpp:1259-1272`)

**Phase 1 — `beginSelfTestMidi()`** (callAsync off ctor):
- Guard: `session.getEdit()==nullptr` → `finishSelfTestMidi()` (FAIL) + return.
- `session.ensureScenes(16)` so slot (0,0) exists (off undo stack; throwaway edit).
- **Assert the slot is EMPTY**: `getClipSlot(0,0)->getClip()==nullptr` (report `preExistingNotes=0`).
- `PluginHost::ensureDefaultInstrument(track0)` (born-audible; do NOT pre-insert a clip in the target slot).
- `createVirtualMidiDevice("Forge SelfTest MIDI")`; then (after yield) find it by name in `getMidiInDevices()`,
  `setEnabled(true)` + `setMonitorMode(automatic)`; cache `miDevice`, record `miAvailableMidiIns`.
- `miPhase=1; startTimer(200);` — YIELD so the async midi-device-list rebuild delivers and the
  `isEnabled()&&isAvailableToEdit()` gate passes BEFORE `ensureContextAllocated`.

**Phase 2 — `armAndStartMidiRecording()`** (miPhase 1→2):
- `edit->getTransport().ensureContextAllocated()` (AFTER the device is enabled).
- Resolve `slot = session.getClipSlot(0,0)`.
- Arm **only the SLOT** (not the track): for each MIDI instance in `getAllInputDevices()` that is our virtual
  device and `isEnabled()`: `setTarget(slot->itemID, false, &undo, 0)` (guard non-null value) +
  `setRecordingEnabled(slot->itemID, true)`. `miTrackArmed = setTarget succeeded`.
  *(Do NOT also arm the track — two contexts would let notes land in both and mask a wrong-target bug.)*
- `edit->restartPlayback()`.
- Roll: `session.getTransport()->record(false)`; `miRecordingStarted = transport->isRecording()`.
- `miPhase=2; startTimer(300);` — YIELD so the record graph is live before injection.

**Phase 3 — `injectSyntheticMidiNotes()`** (miPhase 2→3):
- Inject 4 deterministic notes (C4/E4/G4/C5 @ vel 0.8) via `miDevice->handleIncomingMidiMessage`. The virtual
  device restamps a ts==0 message to wall-clock **before** the recording append
  (`tracktion_MidiInputDevice.cpp:1605-1606`, then `sendMessageToInstances`), and the instance maps it via
  `globalStreamTimeToEditTime` into the rolling take (`:1046-1058`) — so injecting on the message thread while
  rolling lands the events inside the take. `miNotesInjected += 4`.
- `miPhase=3; startTimer(800);` — YIELD to let the notes accrue.

**Phase 4 — `finishSelfTestMidi()`** (miPhase 3→4):
- `transport->stop(false,false)` — finalises: `applyRecording → addMidiAsTransaction` inserts the `MidiClip`
  in the slot (never a take).
- Disarm: unconditionally `setRecordingEnabled(slot->itemID,false)` + `removeTarget(slot->itemID,&undo)` on
  EVERY MIDI instance.
- Re-resolve `slot = session.getClipSlot(0,0)`; `miClip = dynamic_cast<MidiClip*>(slot->getClip())`;
  `miClipCreated = miClip != nullptr`; `miCapturedNotes = miClip ? miClip->getSequence().getNumNotes() : 0`.
- **`deleteVirtualMidiDevice(*miDevice)`** — MANDATORY (the name persists via `setVirtualDeviceIDs` in engine
  PropertyStorage; a leaked name fails the *next* run's `createVirtualMidiDevice` with "Name already in use").
  Defensively tolerate a pre-existing name before create.
- Compute PASS, write report, `systemRequestedQuit()`.

### Report — `%TEMP%\forge_phase0_selftest.log` (same path as record/session; runs are one-at-a-time)
```
mode=midi
availableMidiInputs=<names or (none)>
midiDeviceEnabled=<0|1>
preExistingNotes=<0 required>
trackArmed=<0|1>              (slot armed to a MIDI target)
recordingStarted=<0|1>
notesInjected=<N>
clipCreated=<0|1>
capturedNoteCount=<M>
result=<PASS|FAIL>
logFile=<forge::log::getLogFile().getFullPathName()>
```

### PASS criteria (all must hold)
```
miDeviceEnabled && miTrackArmed && miRecordingStarted
  && preExistingNotes == 0
  && miNotesInjected >= 4
  && miClipCreated
  && miCapturedNotes == miNotesInjected      // EXACT, not >=
```

### False-pass defenses
- **Clip exists but empty**: gate on `capturedNoteCount == notesInjected` (a slot clip can be born with 0 notes).
- **Notes into WRONG target** (track instead of slot): arm ONLY the slot; assert on
  `getClipSlot(0,0)->getClip()->getSequence().getNumNotes()`; additionally assert track 0 has no new clip.
- **Injected before rolling / after stop**: phase ordering (roll in 2, inject in 3, stop in 4, each behind a
  YIELD) keeps injection strictly inside the rolling window; the `isRecording()` gate at
  `MidiInputDevice.cpp:1044` drops out-of-window events → count 0 → FAIL.
- **Device dropped from `getAllInputs()`** (enabled after `ensureContextAllocated`): enforced by ordering
  (enable in phase 1, YIELD, `ensureContextAllocated` in phase 2).
- **Pre-seeded notes**: assert `preExistingNotes==0` and never pre-insert a clip in the target slot;
  `capturedNoteCount == notesInjected` catches any seed.

### Orchestrator edits for the enum/parse (`main.cpp`)
Insert `--selftest-midi` **before** the bare `--selftest` test in BOTH the `modeDesc` and `mode` ladders
(because `"--selftest-midi"` contains the substring `"--selftest"`). `sessionViewBinds()` stays UNCHANGED
(midi is NOT added → the grid does not bind → throwaway edit stays pristine; the harness resolves the slot
directly through `session`). ctor dispatch: `else if (mode == SelfTest::midi) MessageManager::callAsync([this]{
beginSelfTestMidi(); });`. timerCallback: a `if (mode == SelfTest::midi) { stopTimer(); advance miPhase; return; }`
block before the interactive tail.

---

## 5. File-disjoint build-wave plan

Additive-only, contract-first, new symbols only. The ORCHESTRATOR alone edits `CMakeLists.txt`,
`src/main.cpp`, integration wiring, and runs the single build.

| Wave | Agent owns (edits) | Deliverable |
|------|-------------------|-------------|
| **A. Record layer** | `src/engine/RecordController.h`, `src/engine/RecordController.cpp` | `enableMidiInputs`, `getMidiInputDeviceCount`, `getAvailableMidiInputDeviceNames`, `armFirstMidiInputToSlot`, `armFirstMidiInputToTrack`, `disarmSlot`, `disarmMidiTrack`, `isSlotMidiArmed`, `isTrackMidiArmed`. Pure additions; no `main.cpp`/CMake edits. |
| **B. Session record seam** | `src/services/files/ProjectSession.h`, `src/services/files/ProjectSession.cpp` | `recordArmSlot`, `recordDisarmSlot`, `isSlotRecordArmed`, `beginSlotRecord`, `commitSlotRecord`, `isSlotRecording` + the injected-recorder `std::function` seam field. Depends on Wave A signatures (contract-first: code to the frozen header). |
| **C. Pad state** | `src/ui/session/SlotVisualState.h`, `src/ui/session/ClipSlotComponent.cpp` | `recording` enum member; 4-arg `computeSlotState`; `toPadFeedback` mapping; `ClipSlotComponent::paint` `recording` branch + `hasClip` predicate fix. Self-contained. |
| **D. Session view seams** | `src/ui/session/SessionView.h`, `src/ui/session/SessionView.cpp` | `isTrackMidiArmed`/`onMidiArmToggled`/`onSlotRecord`/`onSlotRecordStop`/`isSlotRecording` seams; `wireColumn` `onArm` branch on `trackIsMidi`; `keyPressed` Ctrl+Enter branch; right-click "Record into slot"/"Stop recording" items; poll `tp.armed` OR-in of MIDI arm + `recordingHere` resolve. Depends on Wave C's 4-arg `computeSlotState` + `recording`. |
| **E. Orchestrator (integration + build)** | `src/main.cpp`, `CMakeLists.txt` | `SelfTest::midi` enum + parse (before bare `--selftest`); the selftest harness members + `beginSelfTestMidi`/`armAndStartMidiRecording`/`injectSyntheticMidiNotes`/`finishSelfTestMidi`; timerCallback + ctor dispatch; wire `sessionView.*` MIDI seams to `recorder`/`session`; the `onArmToggled` audio/MIDI mutual-exclusion; teardown disarm. Runs the single build + the `--selftest-midi` gate. |

Ordering: A + C can proceed in parallel (disjoint, no cross-dependency). B follows A (codes to A's header). D
follows C (needs the new `computeSlotState` arity). E integrates last and is the only wave that touches
`main.cpp`/CMake. Every wave writes only its listed files.

---

## 6. Verified issues folded in / refuted

**Confirmed BLOCKERS (all fixed above):**
- **Launch vs transport**: recording is driven by `transport.record()` → `startRecording` over
  `isRecordingActive()` destinations (`EditPlaybackContext.cpp:705,731,795-798`), NOT by launching the slot.
  `launchSlot` no-ops on an empty slot (`ProjectSession.cpp` clip guard). → `launchSlot` removed from the
  record path; risk #7 deleted.
- **`isSlotRecording` definition**: an empty capturing slot has no `LaunchHandle`, so "launch playing" is
  unreachable. → Redefined as `isSlotMidiArmed(slot) && transport.isRecording()`; ProjectSession tracks the
  single active record slot.
- **`ClipSlotComponent::paint` ripple**: `hasClip = state != empty && state != recArmed` would render a
  `recording` pad as a clip body (`ClipSlotComponent.cpp:81`). → Explicit `recording` branch + predicate fix
  (Wave C).
- **Ctrl+Enter never reaching the grid** was the *risk*; the fix (branch inside `SessionView::keyPressed`
  before plain returnKey; parent `isCommandDown` returns false for unknown combos at `main.cpp:636`) is
  incorporated.

**Confirmed MAJORS (all addressed):**
- `setTarget` returns a *success* holding `nullptr` when the id resolves to neither track nor slot
  (`.cpp:143-144`) → explicit `r.value()==nullptr` guard in the arm loop.
- `removeTarget`/`setTarget` fail while `isRecording()` (`.cpp:146-147,175-176`) → disarm/arm require the
  transport stopped; `commitSlotRecord`/`recordDisarmSlot` stop first.
- Captured-clip length has **no** launch-quantise rounding (`MidiInputDevice.cpp:1141`, `EditUtilities`
  punch-range) → length narrative corrected; Forge must schedule bar-aligned start/stop for a quantised loop.
- Verdict (A) is code-path-supported but **untested in-engine** → gate is the first proof; (B) fallback kept.
  **RESOLVED (`160f6cc`): `--selftest-midi` PASSES — verdict (A) is now empirically proven in-engine.**
- recArmed tint driven by audio `isTrackArmed` only → poll ORs in `isTrackMidiArmed`; audio/MIDI arm mutually
  exclusive per track (v1).
- Arm routing contradiction → the branch lives solely in `SessionView::wireColumn` on `trackIsMidi`.
- Selftest false-pass surface (wrong target / pre-seed / empty clip) → arm slot only, assert
  `preExistingNotes==0`, assert `capturedNoteCount == notesInjected` exactly, assert track 0 has no new clip.
- `createVirtualMidiDevice` returns `Result` not a pointer; persists the name → find by name after yield;
  `deleteVirtualMidiDevice` mandatory on finish.
- `handleNoteOn/handleNoteOff` are protected keyboard-listener overrides, not an injector → inject via the
  public `handleIncomingMidiMessage(MidiMessage, MPESourceID)`.

**REFUTED (do NOT over-engineer):**
- *"Injected ts==0 notes fall before `startPos` and are silently dropped"* — **refuted for a
  `VirtualMidiInputDevice`**: `handleIncomingMidiMessage` calls `handleIncomingMessage`, which restamps a
  ts==0 message to `Time::getMillisecondCounterHiRes()` (wall-clock) **before** routing to the recording
  append (`tracktion_MidiInputDevice.cpp:1605-1606`, then `sendMessageToInstances` →
  instance `handleIncomingMidiMessage` :1033). The injected note is stamped ≈ current stream time and lands
  inside the take, same as hardware. → No two-tick spreading / explicit-timestamp mitigation needed; keep the
  phase ordering (roll before inject) which the `isRecording()` gate at :1044 still requires.
- The MIDI enable/arm ordering facts (enable + pass the gate before `ensureContextAllocated`;
  `rescanMidiDeviceList` not `dispatchPendingUpdates`; async yield) are **CONFIRMED** — no threading/RT
  violation in enable/arm/disarm/read paths.

---

## 7. Residual open questions for the human

1. **Count-in**: recommended v1 = none (capture starts when the transport rolls). A metronome count-in touches
   the transport recipe (out of W7 scope). Confirm no count-in for v1.
   > **RESOLVED (v1, shipped `160f6cc`): NONE.** No count-in — capture starts the moment the transport rolls.
2. **Overdub vs replace into a FILLED slot**: recommended v1 = empty slots only (gesture + tint gated on
   empty). `addMidiAsTransaction` takes a `MergeMode`, so the engine supports overdub later, but the UX
   (which mode, how to choose) needs its own pass. Confirm empty-only for v1.
   > **RESOLVED (v1, shipped `160f6cc`): EMPTY-slots-ONLY.** The record gesture + recArmed tint are gated on an
   > empty slot; overdub/replace into a filled slot is deferred.
3. **Fixed-length vs open-ended capture**: recommended v1 = open-ended (stop when the user stops). A
   fixed-N-bar auto-stop must be a Forge-scheduled `stop` (slots force `wasPunchRecording=false`, so the engine
   won't punch out). Confirm open-ended for v1.
   > **RESOLVED (v1, shipped `160f6cc`): OPEN-ENDED.** The user stops manually; no fixed-length auto-punch-out.
4. **Ctrl+Enter binding**: confirm no shell global accelerator already claims Ctrl+Enter (grep of
   `main.cpp:624-637` shows only S/O/N/I under Ctrl, so it is free). Fallback: a plain `R` on the focused slot.
   > **RESOLVED (v1, shipped `160f6cc`): Ctrl+Enter CONFIRMED FREE + USED.** No shell accelerator claims it; the
   > `R`-on-focused-slot fallback was not needed.
5. **Audio/MIDI arm exclusivity**: recommended v1 = mutually exclusive per track (one R indicator, one recArmed
   tint). Confirm — or design a dual-arm indicator if both must coexist.
   > **RESOLVED (v1, shipped `160f6cc`): MUTUALLY EXCLUSIVE per track.** Arming one disarms the other, so the
   > single R indicator + single recArmed tint stay unambiguous. (Implemented in the arm wiring.)
6. **Recording into an armed slot while another slot on the track is playing**: transport-level record captures
   regardless of playback; confirm the desired interaction (does starting a slot record stop the track's
   playing clip?). Recommended v1: independent (record does not stop playback), revisit if confusing.
   > **RESOLVED (v1, shipped `160f6cc`): INDEPENDENT.** Starting a slot record does not stop a playing clip on
   > the track — transport-level record and slot playback coexist.
