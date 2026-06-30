<!--
  Design-phase output for "MIDI tracks + piano-roll" (engine Phase 3).
  Produced 2026-06-30 by a multi-agent design pass (understand -> design -> adversarial-verify ->
  synthesize). 5/6 load-bearing assumptions confirmed against source, 1 refuted (MIDI input
  recording is NOT a mechanical clone of the audio arm). DESIGN ONLY — no code yet. The build is
  the file-disjoint 7-wave plan in section 6.
-->

# Forge: MIDI Tracks + Piano-Roll — Build-Ready Design

## 0. Orientation

Tracktion has **no MIDI track type**: a `te::AudioTrack` already hosts both wave clips and MIDI clips, and audibility comes from inserting an instrument plugin (the built-in 4OSC synth) at the head of that track's plugin chain. This collapses the feature into four additive concerns:

1. A **MIDI-clip create path** (none exists today — clips only arrive via Browser import).
2. A **clip-component factory branch** so a `te::MidiClip` renders as a note-block instead of falling through `AudioClipComponent`'s name-only rectangle.
3. A **piano-roll surface** hosted in the bottom drawer, reusing `TimelineView` (time↔x) + `snapToGrid` (time snap), with a net-new pitch↔y axis.
4. An **instrument seam** in `PluginHost` to drop a default 4OSC.

MIDI-input recording is a *separate, deferrable* concern — but **not** a mechanical clone of the audio arm (see §5). It is its own late wave and carries more risk than initially assumed.

All existing selection/drag/snap/persist seams are base-typed on `te::Clip*` / `te::Track*` (verified: `TrackLaneComponent::rebuildClips` ArrangeView.cpp:455 iterates `track.getClips()` yielding `te::Clip*`; `onClipSelected(te::Clip*)` is `std::function<void(te::Clip*)>` ArrangeView.h:283; selection routes through `selectClip`/`onClipSelected` at the `te::Clip*` level), so MIDI rides them unchanged. The one nuance the builder must absorb: the per-clip **component callbacks** (`onClicked`, `onDragCommitted`, `snapStartTime`, ArrangeView.cpp:459–487) are currently typed `AudioClipComponent&`, so the base-extraction in Wave 3 must re-type those lambda parameters to `ClipComponent&` (additive, but a real signature change inside W3's owned file — see §2.2 and §7).

---

## 1. SCOPE & MVP

**MVP (Waves 1–4): a drawn MIDI clip is audible end-to-end with ZERO recording code.**

This slice is verified-feasible: three shipped engine demos (PatternGeneratorDemo.h, ClipLauncherDemo.h, EngineInPluginDemo.h) do exactly `dynamic_cast<te::FourOscPlugin*>(edit.getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {}).get())` → `pluginList.insertPlugin(*synth, 0, nullptr)` → insert MIDI clip → `addNote` → play, and hear sound with **no input device and no recording code**.

User flow for the MVP:
1. Right-click an empty area of a track lane → "New MIDI Clip" (or a toolbar button) → a `te::MidiClip` appears on the `te::AudioTrack`.
2. A 4OSC instrument is auto-inserted at chain index 0 on that track if absent (`PluginHost::ensureDefaultInstrument`).
3. Selecting the MIDI clip opens the bottom drawer showing the **piano-roll** (not the audio DetailView).
4. User draws a few notes (click-drag on the grid), which call `seq.addNote(pitch, startBeat, lengthBeats, vel, 0, um)` on the **live** sequence.
5. Hit play → notes route through 4OSC → track output. Audible. `producesAudioWhenNoAudioInput()==true` (FourOscPlugin.h:196, verified) means the default patch sounds immediately.

This is the smallest slice that proves the whole stack. Everything below MVP — velocity lane, marquee select, copy/paste, MIDI-input recording — is explicitly **post-MVP** and waved separately.

**De-risking decision:** MIDI-input recording is deferred to **Wave 7 (last)**. It touches a separate device subsystem with real hardware-negotiation risk, and **is not a clean clone of the audio arm** (verification refuted that assumption — see §5). Deferring it means the audible-MIDI feature ships and is demonstrable before any input-device enable/sequencing risk is incurred.

---

## 2. MIDI-CLIP CONTRACT

### 2.1 Create path (additive — new symbols only)

Today there is **no** clip-creation path other than import. Add a create helper on `ProjectSession` (a sibling to `importAudioFile`):

```cpp
// ProjectSession.h / .cpp — NEW, additive
te::MidiClip::Ptr ProjectSession::createMidiClip (int trackIndex, te::TimeRange range, const juce::String& name);
```

**Which API to call (verified).** Two valid forms exist on `te::AudioTrack` (inherited from `te::ClipTrack`, tracktion_ClipTrack.h:79-83):
- `insertMIDIClip(TimeRange, SelectionManager*)`
- `insertMIDIClip(const juce::String& name, TimeRange, SelectionManager*)`

Either works. **Recommended:** the type-dispatched member `insertNewClip(te::TrackItem::Type::midi, name, TimeRange, SelectionManager*)` (tracktion_ClipTrack.h:70), which the vendored demos prefer (PatternGeneratorDemo.h:245) and which returns a base `te::Clip*` you then `dynamic_cast<te::MidiClip*>`.

**Name-collision warning (verified).** There is a same-named **free function** `te::insertMIDIClip(ClipOwner&, ...)` (tracktion_ClipOwner.h:115/118) used by the clip-launcher demo against a `ClipSlot`. The create helper MUST call the **member** on the `te::AudioTrack` (or pass the track as the `ClipOwner`), never the `ClipSlot` launcher variant.

Build `range` from beats via `edit.tempoSequence.toTime({bars, beats})` — **clip range is in SECONDS** (the single most error-prone seam; notes inside the clip are in BEATS).

After creation, the helper calls `PluginHost::ensureDefaultInstrument(track)` (Wave 1) so the clip is born audible, then fires the existing `notifyEditMutated` → `session.save()` seam.

### 2.2 Arrange-lane rendering (the only structurally-required change)

`TrackLaneComponent::rebuildClips` (ArrangeView.cpp:455) is the clip-component factory. Today it unconditionally does `new AudioClipComponent(view, *c)` (line 457) for every clip, with no type switch, and wires six callbacks typed `AudioClipComponent&` (lines 459–487). Refactor additively:

- **Extract a base `ClipComponent`** holding the type-agnostic machinery: `te::Clip&` ref (`clip` member is `te::Clip&` ArrangeView.h:129; `getClip()` returns `te::Clip&`), geometry (`clipAreaWidth`, the `timeToX`-based bounds in `resized`), drag/snap/commit (`mouseDown/Drag/Up`, ArrangeView.cpp:190–284 — these read only `clip.getPosition()`, `clip.setStart`, `view.xToTime`; **no audio API**), selection callbacks. *None of this touches audio* (verified: the only two `WaveAudioClip` sites in the whole component are the ctor SmartThumbnail at cpp:162 and `paint` waveform at cpp:310).
- **Re-type the callbacks.** The six lambdas (`onClicked`, `onRightClicked`, `onDragStarted`, `onDragCommitted`, `snapStartTime`, plus selection) currently take `AudioClipComponent&`; move them to the base and re-type their parameter to `ClipComponent&`. This is additive within W3's owned file but is a genuine signature edit of those callback typedefs — not a pure no-op extraction.
- **`AudioClipComponent : ClipComponent`** keeps the two `dynamic_cast<te::WaveAudioClip*>` sites (ctor SmartThumbnail + `paint` waveform).
- **NEW `MidiClipComponent : ClipComponent`** — `paint()` draws a mini note-preview (iterate `clip.getSequence().getNotes()`, plot tiny rects scaled to the clip rect) plus the clip name. Distinct fill colour so MIDI vs wave is visible at a glance.

Factory branch (the one new conditional):
```cpp
ClipComponent* cc = dynamic_cast<te::MidiClip*>(c)
    ? static_cast<ClipComponent*>(clipComps.add (new MidiClipComponent (view, *c)))
    : static_cast<ClipComponent*>(clipComps.add (new AudioClipComponent (view, *c)));
```
All callback wiring (cpp:459–487) moves to the base type and is shared (with the parameter re-typed to `ClipComponent&`).

**Reused unchanged:** selection (`selectClip`/`onClipSelected`), persistence (`notifyEditMutated`/`onEditMutated`), geometry (`TrackLaneComponent::resized` reads only `cc->getClip().getPosition()`), snap (`snapToGrid`). `getClips()` returns MIDI + wave mixed on the same track (verified: `WaveAudioClip : AudioClipBase : … : Clip`, and `MidiClip : Clip`, both retrievable via the inherited `getClips()`), so classification is **per-clip in the factory**, never per-track.

---

## 3. PIANO-ROLL SURFACE

### 3.1 Where it lives — sibling component, drawer-hosted, NOT inside DetailView

`DetailView` is the **audio-clip inspector** (it `dynamic_cast`s to `AudioClipBase`/`WaveAudioClip` and shows "No waveform" otherwise). Do **not** extend it. Instead add a sibling `PianoRollView : juce::Component` in its own files, and route by clip type in the shell's selection callback:

```cpp
// main.cpp (orchestrator-owned wiring) — selection routing
arrangeView.onClipSelected = [this](te::Clip* c) {
    if (auto* mc = dynamic_cast<te::MidiClip*>(c)) {
        pianoRoll.setMidiClip(mc);          // show pianoRoll in bottom region
        bottomMode = BottomMode::PianoRoll;
    } else {
        detailView.setClip(c);              // show detailView
        bottomMode = BottomMode::Detail;
    }
    if (c && !drawerVisible) { drawerVisible = true; }
    resized();
};
pianoRoll.onEditMutated = [this]{ session.save(); };   // symmetric with detailView
```

The drawer region mechanism is reused. Verified mechanics: `resized()` assigns the bottom region with a single `centre.removeFromBottom(h)` and only `detailView.setVisible/setBounds` fills it (main.cpp:310–317); `drawerVisible`/`drawerHeight`/clamp are plain mutable ints (main.cpp:360/365); toggle paths are field-write + `resized()` (main.cpp:383/409–410). Swapping in a `pianoRoll.setVisible/setBounds` on the same rect (selected by `bottomMode`) is a direct, additive edit within the orchestrator's `main.cpp` ownership. `DetailView` is a plain `juce::Component`; `PianoRollView` is added identically via `addAndMakeVisible`.

### 3.2 Coordinate mapping

- **Horizontal (time) — REUSE.** `PianoRollView` ctor takes the **same shared** `TimelineView&` the shell owns (main.cpp:345; `ArrangeView` already takes it by ref at main.cpp:346), so it scrolls/zooms in lockstep with the arrange view. Note x = `view.timeToX(noteStartTime)`, width via `xToTime`. Vertical grid lines drawn by walking `gridStepInBeats(division, num, denom)` through `edit->tempoSequence` — identical math to the ruler. **Do not construct a second `TimelineView`.**
- **Vertical (pitch) — NEW.** `kKeyHeight` (~12px); `pitchToY(note) = (127 - note) * kKeyHeight`; `yToPitch` inverse. A left keybed gutter (black/white strip). **This gutter is the vertical analog of the arrange header offset** — its width must be excluded from the `timeToX` clip-area width (same way `AudioClipComponent::clipAreaWidth` handles `headerW`), or notes shift horizontally.

### 3.3 The 88-key vs drawer-height tension — Viewport is MANDATORY

The drawer clamps to 90–420px (main.cpp:365); 128 rows × ~12px ≈ 1536px, far exceeding any sane drawer height (the bottom region's usable height is only whatever `centre` has left after controlBar(46) + status(24) + browser column are removed). **A `juce::Viewport` for vertical scroll inside the roll is therefore required, not optional** — raising the height clamp alone does **not** resolve the tension; it only bounds the visible window. Resolution:
- Wrap the roll's note grid in a `juce::Viewport` for **vertical scroll** inside the drawer.
- On `setMidiClip`, **auto-scroll to center the clip's note range** (or default to C3–C5 if empty) so the user lands on relevant pitches.
- Optionally raise the drawer's effective max height when the piano-roll is the active bottom child. **Important correction (verified):** `drawerMaxHeight` (420) is **also baked into the `ResizerBar` at construction** (main.cpp:368, `ResizerBar drawerResizer { false, drawerMinHeight, drawerMaxHeight }`); `ResizerBar` copies `minPx/maxPx` into private `minSize/maxSize` at ctor (main.cpp:96) and clamps drags against its own copy (mouseDrag main.cpp:134) with **no setter**. So merely mutating the `drawerMaxHeight` field per `bottomMode` raises only the layout clamp (main.cpp:314) — the resizer drag would still cap at the old 420. To vary max height by mode the orchestrator must either (a) **add a public `setMax()`/mutable bound to `ResizerBar`** (additive, allowed under orchestrator ownership of main.cpp), or (b) **construct the resizer with the larger piano-roll max from the start** and rely on `drawerMaxHeight` alone for the layout clamp. A pop-out/maximize affordance is **post-MVP**.

### 3.4 Interaction model (MVP = draw/move/resize/delete; velocity = post-MVP)

A `MidiNoteComponent` per `te::MidiNote`, modeled on `AudioClipComponent`'s interaction (Ctrl-bypass snap, `dragOrigin` capture, commit-then-persist):
- **Draw:** click empty grid → `seq.addNote(yToPitch(y), snapToGrid(xToTime(x))→beats, defaultLen, 100, 0, um)`.
- **Move:** drag → new start (time-snapped via the shared `snapStartTime` `std::function` seam) + new pitch (`yToPitch`, rounded to semitone row).
- **Resize:** drag right edge → `note->setStartAndLength(start, newLen, um)` (MidiNote.h:61, verified).
- **Delete:** right-click / Delete key → remove from `MidiList`.
- **Always edit `getSequence()` — never `getSequenceLooped()`/`createSequenceLooped()`.** Verified: `getSequence()` (MidiClip.h:32) returns a reference into the clip's persistent take store; `getSequenceLooped()` returns a throwaway cached copy whose edits are discarded on a **looping** clip. Nuance (verified): on a **non-looping** clip `getSequenceLooped()` happens to return `getSequence()` directly, so the trap only springs when looping — which is exactly why the rule must be **unconditional**: always `getSequence()`, regardless of loop state.
- **Pass the Edit's UndoManager** (`&clip.getUndoManager()`) to `addNote`/`setStartAndLength` so the ValueTree write is persisted and undoable. Each commit fires `onEditMutated`.
- **Velocity lane** (drag note-bottom or a strip below) is **post-MVP (Wave 6)**.

---

## 4. INSTRUMENT SEAM

Default instrument = **`te::FourOscPlugin`** (verified: `xmlTypeName == "4osc"` h:175/178; `getPluginName() == "4OSC"` h:174; `takesMidiInput()==true` h:193; `takesAudioInput()==false` h:194; `isSynth()==true` h:195; `producesAudioWhenNoAudioInput()==true` h:196). It is the engine's only built-in synth; no external scan needed.

Today `PluginHost` deliberately omits synths: `getBuiltInEffects()` (PluginHost.cpp:63) lists only effects, `makeBuiltIn` hardcodes category `"Effect"` (cpp:52), and `getScannedExternals` skips `desc.isInstrument` (cpp:86). Critically, **`addPluginToTrack` inserts at the volume-plugin index, NOT index 0** (verified: cpp:166–175 finds `track.getVolumePlugin()`'s index and inserts there so the effect lands just before the fader/meter tail). An instrument must instead go at the **head**, so the MVP needs its **own insert-at-0 path** rather than reusing `addPluginToTrack`. Add, additively:

```cpp
// PluginHost.h/.cpp — NEW symbols
static juce::Array<Creatable> getBuiltInInstruments();         // { makeBuiltIn<te::FourOscPlugin>() }, category "Instrument"
te::Plugin::Ptr addInstrumentToTrack (te::AudioTrack&, const juce::String& displayName); // creates + inserts at index 0
bool            ensureDefaultInstrument (te::AudioTrack&);     // insert 4OSC iff no instrument present
```

`addInstrumentToTrack` reuses the **creation** mechanism (`edit.getPluginCache().createNewPlugin(xmlType, desc)`, PluginHost.cpp:157) but inserts at the **HEAD**: `track.pluginList.insertPlugin(plugin, 0, nullptr)`. Index 0 matters — the instrument is the sound source; effects must follow it. **Creation-API nit (verified):** use the two-arg `createNewPlugin(const juce::String& type, const juce::PluginDescription&)` overload (PluginManager.h:142); for a built-in, passing `{}` for the description works (as the demos do). Do **not** use the single-`ValueTree` overload — it will not create a 4OSC from a type name. `ensureDefaultInstrument` scans `pluginList` for an existing instrument (`takesMidiInput()`/`isSynth()` or type=="4osc") and no-ops if found, so re-creating clips doesn't stack synths. Parameterize `makeBuiltIn`'s category so 4OSC reports "Instrument".

---

## 5. MIDI INPUT RECORDING (deferred — Wave 7, NOT a mechanical clone)

**Verification refuted the "clean clone, swap the device filter, drop the wave-open branch" assumption.** The base-class arm calls **are** reusable verbatim — `setTarget`/`removeTarget`/`getTargets`/`setRecordingEnabled` are `InputDeviceInstance` methods (InputDevice.h:125–152) identical for MIDI and wave, and `restartPlayback` is shared. But the **enable + context-population path is wave-specific and must be rewritten**:

1. **A MIDI-specific enable step is required BEFORE `ensureContextAllocated()`.** `EditPlaybackContext` only adds a MIDI instance to `getAllInputs()` if `mi->isEnabled() && mi->isAvailableToEdit()` (EditPlaybackContext.cpp:551–553). A disabled MIDI device is silently dropped, so the device must be enabled first.
2. **`RecordController::enableInputs()` cannot be reused as-is.** It iterates **only** `getWaveInDevices()` and calls `setStereoPair(false)` + `setMonitorMode` + `setEnabled(true)` + `dm.dispatchPendingUpdates()` (RecordController.cpp:34–57). `setStereoPair()` **does not exist on `MidiInputDevice`**, and the loop never touches MIDI devices — a wave-arm clone finds **zero** MIDI instances. A new MIDI enable step must iterate `dm.getMidiInDevices()` (returns `std::shared_ptr<MidiInputDevice>`, DeviceManager.h:158–161), call `setEnabled(true)` + `setMonitorMode(MonitorMode::automatic)` on each.
3. **Different flush call.** `MidiInputDevice::setEnabled` calls `engine.getDeviceManager().rescanMidiDeviceList()` (MidiInputDevice.cpp:352–360), **not** `dispatchPendingUpdates()` — a different enable/flush sequence than wave. MIDI device count comes from the `dm.midiInputs` vector via `rescanMidiDeviceList()`, independent of open audio-input channels.
4. **Filter is a group, not one value.** Accept `getDeviceType()` in `{physicalMidiDevice, virtualMidiDevice, trackMidiDevice}` (InputDevice.h:25–32), **not** `waveDevice`.
5. **The `getNumWaveInDevices()==0` / `ensureRecordingInputOpen` branch is correctly dropped** — MIDI has no lazy wave-capture-open dance — but a "is the MIDI device discovered + enabled" precondition replaces it.

```cpp
// RecordController.h/.cpp — NEW (NOT verbatim clones of the audio methods)
bool enableMidiInputs();                                          // iterate getMidiInDevices(): setEnabled(true)+setMonitorMode, then rescanMidiDeviceList()
bool armFirstMidiInputToTrack (te::Edit&, te::AudioTrack&);       // arm after the MIDI enable step + ensureContextAllocated()
bool disarmMidiTrack (te::Edit&, te::AudioTrack&);
```

**Mitigating factor (verified, but fragile):** `MidiInputDevice`'s ctor defaults `enabled=true` (MidiInputDevice.cpp:336), so a freshly-discovered hardware controller *may* already pass the :552 gate without an explicit enable. But this is **not guaranteed** — `loadMidiProps` can override `enabled` from saved state, and discovery still depends on `rescanMidiDeviceList()`. Do not rely on the default.

**Critical async-rebuild rule carried forward:** the audio record path learned never to arm synchronously inside one blocking callback. The MIDI arm must follow the same pattern — arm/restart on the message-thread but let the engine's async rebuild settle; do not block waiting on `restartPlayback`. Reuse the existing `onArmToggled`/`toggleRecordTake` structure (main.cpp), branching to the MIDI path for tracks with an instrument; `transport.record(false)`/`stop(false,false)` are unchanged.

**Why a later wave:** it requires its own enable-sequence design **and a runtime test with a physical MIDI controller attached** (which has not been run). It carries **more** risk than the "mechanical clone" framing implied, and the audible-MIDI feature is fully demonstrable (draw + play) without it.

---

## 6. BUILD-WAVE PLAN (file-disjoint, additive-only, contract-first)

Each wave/agent has **EXCLUSIVE** file ownership. New components land as **new disjoint files**. The **orchestrator alone** edits `CMakeLists.txt`, `main.cpp`, and ControlBar wiring, and runs the single integration build per wave. Interfaces are additive (new symbols); existing signatures are never mutated — except the intra-file callback re-typing in W3 (`AudioClipComponent&`→`ClipComponent&`), which is confined to W3's owned file.

| Wave | Agent owns (exclusive) | Deliverable / Contract exposed |
|---|---|---|
| **W1 — Instrument seam** | `src/engine/PluginHost.{h,cpp}` | `getBuiltInInstruments()`, `addInstrumentToTrack(track,name)` (own insert-at-0 path, NOT addPluginToTrack), `ensureDefaultInstrument(track)`; `makeBuiltIn` category parameterized. Unit-provable: inserting 4OSC at index 0. No UI. |
| **W2 — MIDI clip create** | `src/services/files/ProjectSession.{h,cpp}`, `src/engine/EngineHelpers.h` (additive `insertDrawnMidiClip`) | `ProjectSession::createMidiClip(trackIndex, range, name)` returning `te::MidiClip::Ptr`, calling the **AudioTrack member** `insertMIDIClip`/`insertNewClip(midi,…)` (never the ClipOwner free-fn), then W1's `ensureDefaultInstrument`. Builds range (SECONDS) from beats via tempoSequence. |
| **W3 — Clip-component split** | `src/ui/arrange/ArrangeView.{h,cpp}` | Extract base `ClipComponent`; re-type the six callbacks to `ClipComponent&`; `AudioClipComponent : ClipComponent`; new `MidiClipComponent : ClipComponent`; factory branch in `rebuildClips`; "New MIDI Clip" lane context-menu entry calling a `std::function` create-seam the orchestrator wires to W2. *Single-owner file — the one wave that touches ArrangeView.* |
| **W4 — Piano-roll surface** | `src/ui/pianoroll/PianoRollView.{h,cpp}`, `src/ui/pianoroll/MidiNoteComponent.{h,cpp}` (new dir) | `PianoRollView(TimelineView&)` (shared instance, by ref) with `setMidiClip(te::MidiClip*)`, `onEditMutated`, `std::function snapStartTime`; **Viewport-wrapped** note grid (mandatory). Draw/move/resize/delete on `getSequence()` with the Edit UndoManager. **MVP completes here.** |
| **W5 — Integration** | orchestrator: `CMakeLists.txt`, `main.cpp`, ControlBar | Add new files to build; wire selection routing (MidiClip→pianoRoll, else detailView); wire `pianoRoll.onEditMutated`→save; wire lane create-seam→`createMidiClip`; add `bottomMode`; handle the **ResizerBar max-height** issue (add `setMax()` or construct with larger max) if varying drawer height by mode. Single integration build. **Audible-MIDI demo.** |
| **W6 — Velocity + polish** (post-MVP) | `src/ui/pianoroll/*` (W4's files) | Velocity lane, multi-select/marquee, copy/paste, keyboard nav. Disjoint from all engine files. |
| **W7 — MIDI input recording** (post-MVP) | `src/engine/RecordController.{h,cpp}` | `enableMidiInputs` (own MIDI enable sequence — NOT a clone of enableInputs), `armFirstMidiInputToTrack`, `disarmMidiTrack`. Requires a runtime test with a physical MIDI controller. Orchestrator wires `onArmToggled` branch + integration build. |

**Seam contracts agreed before coding (contract-first):**
- W3↔W2: lane "New MIDI Clip" emits a `std::function<void(int trackIndex, te::TimePosition)>` the orchestrator binds to `createMidiClip`. W3 never includes ProjectSession.
- W4↔shell: `PianoRollView::setMidiClip(te::MidiClip*)` + `onEditMutated` (mirrors DetailView's shape so wiring is symmetric).
- W3/W4↔W1: clip creation triggers `ensureDefaultInstrument`; W4 assumes an instrument exists (W1 guarantees it via W2).
- Shared `TimelineView&` instance (main.cpp:345) is passed by ref to W4 — **do not create a second one**.

Waves W1, W2, W4 are mutually file-disjoint and can run in parallel; **W3 is the sole editor of ArrangeView** and runs alone; W5 integrates. W6/W7 follow.

---

## 7. RISKS & OPEN QUESTIONS

1. **ClipComponent refactor blast radius.** Splitting `AudioClipComponent` into base + subclass touches the most heavily-wired file, and (verified) requires re-typing six callbacks from `AudioClipComponent&` to `ClipComponent&` (ArrangeView.cpp:459–487). Mitigation: W3 is single-owner, additive, and gated by the integration build. *Keep `AudioClipComponent`'s public surface otherwise identical; the only deliberate change is the shared-base callback parameter type.*
2. **Drawer height & the ResizerBar max-clamp divergence.** (Verified) `drawerMaxHeight` is baked into `ResizerBar` at construction with no setter, so a per-mode height bump needs either an additive `setMax()` on `ResizerBar` or constructing the resizer with the larger max up front. A `juce::Viewport` for vertical scroll is **mandatory** regardless. Open question: do we need a pop-out/full-height mode for MVP, or defer? Current plan defers.
3. **Tempo-relative horizontal spacing.** `TimelineView` is seconds-based; notes drawn beat-uniform will look non-uniform under tempo changes (same as the ruler). Acceptable and consistent, but worth a UX note.
4. **MidiClipComponent paint cost.** Drawing per-note previews for large clips on every repaint could be slow; cache a downsampled note image if profiling shows cost.
5. **Instrument default-patch loudness/character.** 4OSC's default patch is audible but may be an unpleasant default tone. Open question: ship a curated default patch ValueTree (`restorePluginStateFromValueTree`) in W1, or accept the raw default for MVP?
6. **MIDI input recording is higher-risk than assumed (W7).** (Verified-refuted) It is **not** a mechanical clone: it needs a bespoke MIDI enable sequence (`getMidiInDevices()` + `setEnabled` + `setMonitorMode` + `rescanMidiDeviceList()`) before context allocation, a different device-type filter group, and **a runtime test with real MIDI hardware** that has not yet been performed. The async-arm lesson still applies on top of that.
7. **Channel-10 rhythm semantics.** `MidiClip::setMidiChannel` treats channel 10 as rhythm; not relevant for 4OSC-only MVP but matters if/when the sampler is offered as an alternative instrument.

### Verification notes (what changed vs the draft, and residual uncertainty)

- **Confirmed unchanged:** the core premise (no MIDI track type; MIDI + wave clips coexist on `te::AudioTrack`, retrieved via `getClips()`); the audible slice (4OSC at index 0 → MIDI clip → `addNote` → play, no input/record code); FourOsc properties (h:174–196); the `getSequence()` vs `getSequenceLooped()` data-loss trap; the base-typed selection/drag/snap/persist seams; the drawer hosting mechanism.
- **Corrected from verification:**
  1. **Instrument insert (§4):** `addPluginToTrack` inserts at the **volume-plugin index, not 0** — the instrument needs its own insert-at-0 path; reusing the effect helper would place 4OSC after the fader.
  2. **ResizerBar max height (§3.3, §7):** `drawerMaxHeight` is **baked into the resizer at construction with no setter** — varying it per mode requires an additive `setMax()` or constructing with the larger max. A Viewport is **mandatory**, not merely a tidy option.
  3. **MIDI recording (§5):** **refuted** as a mechanical clone — `enableInputs()` is wave-only (`setStereoPair` nonexistent on MIDI; iterates only wave devices), MIDI needs its own enable sequence before `ensureContextAllocated()` (else the device is dropped from `getAllInputs()`), a different flush call, and the multi-value MIDI device-type filter.
  4. **Create-path API (§2.1):** clarified the exact valid overloads, recommended the type-dispatched `insertNewClip(midi,…)`, and flagged the `te::insertMIDIClip(ClipOwner&,…)` **free-function name collision** (must call the AudioTrack member, not the ClipSlot variant).
  5. **createNewPlugin overload (§4):** must use the two-arg `(String type, PluginDescription)` form; the single-`ValueTree` overload will not build a 4OSC from a type name.
  6. **Callback re-typing (§0, §2.2, §7):** the W3 base extraction is not a pure no-op — six lambda callbacks must move from `AudioClipComponent&` to `ClipComponent&`.
- **Residual uncertainty a builder must resolve FIRST:** Wave 7's MIDI enable/arm sequence has **not been runtime-tested with a physical MIDI controller**. Before committing the W7 design, attach a controller and confirm: (a) the device appears in `dm.getMidiInDevices()` after `rescanMidiDeviceList()`, (b) it passes the `isEnabled() && isAvailableToEdit()` gate (EditPlaybackContext.cpp:551–553), and (c) live-played notes monitor through the armed track's 4OSC with `MonitorMode::automatic`. Everything in the MVP path (W1–W5) is source-verified and needs no such runtime gate.
