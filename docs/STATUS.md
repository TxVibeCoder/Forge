# Forge — Project Status & Roadmap

*Living status document. Last updated 2026-07-01 (this session: **W02 — MIDI-learn HW routing · Forge-native
control surface · offline LUFS** — three engine-facing feature seams (roadmap items 2a / 3 / 4) built against
source-verified Tracktion facts and proven headless, in **ONE scoped commit `bb9ef5e`** on top of `1eb876d`.
**Committed + PUSHED to `origin/main`** (docs commits `d5dbe1a` + `2da5f16` follow it; the pushed set was
sanitized); working tree clean: clean MSVC Debug build (0 warnings); **all EIGHT selftests PASS** — `--selftest`, `--selftest-record`,
`--selftest-session`, `--selftest-midi`, `--selftest-midilearn`, `--selftest-midiinput`,
`--selftest-controlsurface`, `--selftest-lufs`; full record → [devlog/wave-02-features.md](devlog/wave-02-features.md).
Prior session: **Wave 01 — six parallel feature seams** (Forge's first multi-CLI wave — metronome + count-in ·
MIDI-learn (Ctrl+L) · buses/sends (A/B aux) · async export + progress · markers · anti-click clip edge-fade;
**pushed**, `e3b8c7c`; full record → [devlog/wave-01-features.md](devlog/wave-01-features.md)). Before that:
**W7 — MIDI record into Session clip slots** (`160f6cc`, verdict A, proven by `--selftest-midi`), and
**Session-grid vertical scroll** + an **app-wide logging + error-handling subsystem** (`src/core/Log.*`,
logging-at-the-seam a standing build principle; `8d15234`).)*
*Primary product direction (Session/scene-based, controller-driven) is in **[DIRECTION.md](DIRECTION.md)** —
it supersedes any "arrangement-first" framing still in this file pending a Session-first rewrite.*
*For picking the project back up cold, start with **[HANDOFF.md](HANDOFF.md)**.*
*Companion to [ARCHITECTURE.md](ARCHITECTURE.md) (engine/design), [INTERFACE.md](INTERFACE.md)
(UI plan), [FEATURE_CATALOG.md](FEATURE_CATALOG.md) (feature landscape), and
[../tests/SELFTEST.md](../tests/SELFTEST.md) (verification contract).*

---

## 1. What Forge is

A native desktop **DAW** built on **JUCE + Tracktion Engine** in **C++20**, targeting
Windows + macOS. Repo: <https://github.com/TxVibeCoder/Forge> (public, AGPLv3).

| | |
|---|---|
| **Engine** | Tracktion Engine, pinned to **v3.2.0** (submodule under `libs/`, bundles JUCE) |
| **Build** | CMake (≥3.22); generator "Visual Studio 17 2022"; **MSVC v143** (C++20) |
| **Identity** | **Sample / scene-based** — an Ableton-style **Session clip grid**, played from grid controllers (Launchpad / APC40 mkII). Linear arrange is **secondary**. See **[DIRECTION.md](DIRECTION.md)**. |
| **UI direction** | Ableton's *look + interaction*; the **Session clip grid is the primary surface** (Arrange = secondary view); **controller-driven**; dark + **warm amber** accent; single-window. |
| **Code size** | ~8,400 lines of Forge source (engine/JUCE excluded) across 43 files |

---

## 2. Accomplished

Ten green commits, each compiling clean. Phases 2–3 were built by successive **file-disjoint
Workflow fan-outs** (4–5 parallel agents per wave, exclusive file ownership + additive-only
interfaces + contract-first seams), each integrated by the orchestrator in **one green build (0
warnings)**, plus an adversarial-review wave that found + fixed 5 real bugs:

### Phase 0 — toolchain + first sound  (`1f5eb70`, `5fa6f8e`)
- Installed the toolchain (VS2022 Build Tools / MSVC v143, CMake) and vendored Tracktion +
  JUCE at v3.2.0.
- Scaffolded the CMake build; a minimal app constructs a `tracktion::Engine`, generates a
  sine, and **plays it through the engine graph to the audio device**.
- Established the `--selftest` harness (headless PASS/FAIL report).
- *Verified:* device opens, transport plays, playhead advances → PASS.

### Phase 1 — the spine  (`92aeaa5`, `5247c52`, `e661350`, `9cf4fe4`)
- **Project save/load:** `ProjectSession` owns the Edit; create/open/save via
  `EditFileOperations` (empty edit written before first clip insert so sources serialize
  relative). Real `.tracktionedit` on disk. *Round-trip verified.*
- **Audio import:** `EngineHelpers` + `ProjectSession::importAudioFile`; async file chooser.
- **Arrange view** (`src/ui/arrange/`): track lanes, clip rectangles, **`SmartThumbnail`
  waveforms**, and a **moving playhead** (30 Hz, click/drag scrub).
- **Transport bar** (`src/ui/transport/`): play/stop/record/loop + timecode/bars|beats.
- **Recording** (`src/engine/RecordController.cpp`): the verified Tracktion recipe (enable
  wave inputs → `ensureContextAllocated` → `setTarget`/arm → `record`/`stop`). *Verified end-to-end
  on real hardware via `--selftest-record` (captures a real take) — see §2 "Verified" + §4.*
- **Audio Settings** dialog; toolbar (New/Open/Save/Save As/Import/Audio).
- **Adversarial review** (4-dimension fan-out): fixed a real use-after-free on project
  switch, a waveform speed-ratio bug, and a tautological selftest check.

### Interface shell  (`952b013`)
- **`ForgeLookAndFeel`** — dark, amber-accented theme; every colour via colour IDs.
- **`ControlBar`** — merged top strip (file commands + embedded transport + **Arrange|Mix
  view-switch** + Browser/Editor toggles).
- **Shell** — center **view-slot** (`ViewMode`: Arrange now, Mixer placeholder reserved),
  collapsible **Browser** + **Detail-drawer** regions, status strip, app-wide tooltips.
- Self-tests isolated to a fresh temp project (deterministic counts).
- **Planning docs produced:** ARCHITECTURE, FEATURE_CATALOG, INTERFACE, SELFTEST, plus a
  rendered interface mockup.

### Phase 2 — arrange polish + device fix + robustness  (parallel 5-agent fan-out)
Delivered by five file-disjoint agents, integrated in one green build. Per-area writeups live in
`docs/devlog/` (arrange · session · device-recording · shell · hygiene · integration).
- **Device-override FIXED + verified.** `EngineHelpers::initialiseAudioForRecording()` opens input
  channels while preserving the saved output device; the playback selftest now keeps the saved
  Bluetooth output (it survives startup, with no stomp to the onboard device). *(Later refactored:
  startup-latency hardening removed this function — startup is now output-only and the input opens
  lazily via `ensureRecordingInputOpen()`. See §4 + `integration.md` Wave 5.)*
- **Arrange polish** (`ArrangeView`): bars|beats ruler, clip/track **selection** (accent outline),
  per-lane **M/S/R** buttons + **colour swatch**, **right-click context menus** (clip:
  rename/delete/colour; lane: add/delete/rename track), `xToTime` span guard, looped-clip waveform
  tiling. Structural edits persist via `onEditMutated → session.save()`.
- **Shell interactions** (`main.cpp`): **keyboard shortcuts** (B/E region toggles, F9/F11 view
  switch, Space play/stop, R record, Ctrl+S/Shift+S/O/N/I), **draggable resizer bars** for the
  Browser + Detail-drawer, async-dialog **SafePointer guards**, per-dialog `FileChooser` members,
  `saveAs` success-checked.
- **Session robustness** (`ProjectSession`): `saveAs` assigns the file only on success;
  `isModified()`; `getNumClipsOnTrack0()`.
- **Recording robustness** (`RecordController`): rescan-before-count, precise `getLastError()`,
  input-name diagnostics; plus a synthetic-input helper (see §4 — wiring is future work).
- **Hygiene:** `.gitattributes` (`* text=auto`); `formatTimecode` negative-sign fix.

### Phase 3 — mixer · export · plugins · browser · inspector  (`53034a5`; two more fan-outs)
The Browser, Detail-drawer and Mixer placeholder regions are now **all real**. Per-area writeups in
`docs/devlog/` (mixer · engine-mix · arrange-drag · export · plugins · mixer-fx · browser · detail).
- **Mixer view** (`src/ui/mixer/MixerView`): real channel strips — vertical dB fader, rotary pan,
  M/S, colour swatch — over the Edit (volume/pan via new `EngineHelpers::get/setTrackVolumeDb` +
  `…Pan`, driving each track's `VolumeAndPanPlugin`); per-strip **plugin insert slots**, a **master
  strip** (`edit.getMasterVolumePlugin()`), and ~28 Hz **peak meters** off each track's level
  measurer. Horizontal Viewport so many tracks scroll.
- **Plugin hosting** (`src/engine/PluginHost` + `src/ui/plugins/PluginWindow`): list built-in
  effects (EQ, Compressor, Reverb, Delay, Chorus, Phaser…) + any scanned externals; add/remove on a
  track; **floating editor windows** (native editor for externals, a generated parameter panel for
  built-ins) that auto-close with their Edit.
- **WAV export** (`src/services/export/Exporter`): render the Edit to a 24-bit stereo WAV via the
  engine renderer, behind a ControlBar **Export** button + save-chooser.
- **File Browser** (`src/ui/browser/BrowserView`): a `FileTreeComponent` in the left region;
  double-click an audio file to import.
- **Clip Inspector** (`src/ui/detail/DetailView`): the bottom drawer shows the selected clip —
  name, gain (dB), mute, fades, position, larger waveform; auto-opens on selection; holds the clip
  as a `te::Clip::Ptr` so it can't dangle.
- **Clip drag-to-move + snap-to-bar** in the arrange view (Ctrl bypasses snap) + a one-line info
  hint; fixed a *latent* bug where the full-width playhead overlay shadowed all clip mouse events.
- **Recording arm/disarm** wired to the lane **R** button — arm state is now **engine-derived**
  (`RecordController::isTrackArmed`), not a transient flag, so it survives rebuilds and input-steal.

### Phase 4 — polish wave  (file-disjoint fan-out + adversarial review)
Four file-disjoint feature agents, integrated by the orchestrator in one green build, then an
adversarial-review wave (5 reviewers → independent skeptic-verify per finding) that confirmed +
fixed 3 real correctness bugs. Build clean (0 warnings); playback selftest PASS.
- **Snap-division selector** (`ArrangeView`): Off/Bar/½/¼/⅛/1⁄16 grid selector in the ruler corner;
  drag-to-move snaps to the chosen division. Grid math is **denominator-aware** (review fix: one
  engine beat is a denominator-note, not always a quarter, so ¼/⅛/1⁄16 scale by denominator/4).
- **Stem export** (`Exporter::renderStems`): each non-empty audio track → its own 24-bit WAV in a
  chosen folder (ControlBar **Export ▸ Stems**). Per-track render bitset built by hand (the engine's
  `toBitSet` helper is buggy for single-track input).
- **External plugin scan** (`PluginScanner`): JUCE `PluginListComponent` dialog (**Plugins** button)
  bound to the engine's format manager + known list; auto-persists; surfaces in the insert menu.
- **Mixer polish** (`MixerView`): per-insert **bypass** dot + ▲/▼ **reorder** (tail preserved);
  **master meter** now reads the post-fader `EditPlaybackContext::masterLevels` (no Edit mutation —
  review fix: the previous insert-a-meter approach dirtied a clean project and metered pre-fader).

### MIDI tracks + piano-roll — MVP  (`9a24989`; file-disjoint 5-wave fan-out + adversarial verify)
The audible-MIDI slice (engine Phase 3) is built. Right-click an empty lane area → **New MIDI Clip** →
a `te::MidiClip` is created on that track, **born audible** via an auto-inserted **4OSC** at plugin-chain
**index 0**, and the **piano-roll** opens in the bottom drawer. Draw / move / resize / delete notes →
**play → hear it**. No recording code (MIDI-input record is the later W7). Per-wave record in
[devlog/midi-build.md](devlog/midi-build.md); the source-verified design in [devlog/midi-design.md](devlog/midi-design.md).
- **Instrument seam** (`PluginHost`): `ensureDefaultInstrument` inserts a 4OSC at chain head (its own
  insert-at-0 path, *not* the volume-index effect path) and is idempotent (never stacks synths);
  `addInstrumentToTrack`; `makeBuiltIn` category parameterized.
- **MIDI-clip create** (`ProjectSession::createMidiClip`): the **AudioTrack-member** `insertMIDIClip`
  (not the `ClipOwner` free-fn) + `ensureDefaultInstrument`; range in SECONDS, notes in BEATS.
- **Polymorphic clips** (`ArrangeView`): base **`ClipComponent`** extracted; `AudioClipComponent` +
  new **`MidiClipComponent`** derive from it; `rebuildClips` branches by `dynamic_cast`; wave-clip
  behaviour byte-for-byte preserved. A **"New MIDI Clip"** lane menu emits `onCreateMidiClipRequested`.
- **Piano-roll** (`src/ui/pianoroll/`): `PianoRollView` (shared `TimelineView` time axis, mandatory
  `juce::Viewport` for the 128 pitch rows, keybed gutter) + `MidiNoteComponent`; all edits go to the
  **live** `getSequence()` (never the looped copy) with the Edit's UndoManager. Content-relative beats.
- **Integration** (`main.cpp`): selection routes a `MidiClip`→piano-roll, any other clip→DetailView, via
  a `bottomMode` drawer that swaps editors; project-swap drops the held clip safely.
- **W6 polish** (`bb5b6bf`, all in `src/ui/pianoroll/`): multi-select (click / Shift-Ctrl-click /
  marquee), Delete-key, multi-note move (group-clamped so chords keep their shape at the edges),
  copy/paste (Ctrl+C/V at the playhead), and a **velocity lane** (`VelocityLane`, draggable bar per note)
  + velocity shading on notes. The roll grabs keyboard focus but lets unconsumed keys propagate, so the
  shell shortcuts still work. Details in [devlog/midi-build.md](devlog/midi-build.md).
- **Verified:** clean first-try integration build; `--selftest` + `--selftest-record` both **PASS** (no
  regression); a 3-agent adversarial verify wave (W3/W4/W5, default-refuted) returned **`correct` with
  zero blocker/major/minor findings** — incl. tracing `MidiNote&` lifetime safety and instrument-at-0
  audibility against engine source. The live GUI draw→play path still needs a **manual smoke pass** (the
  dev-built window can't be GUI-driven headlessly).

### Session clip-launch grid — the PRIMARY view  (`def1193`; design + build workflows → adversarial QC → fix re-verify)
**The pivot from [DIRECTION.md](DIRECTION.md) is built.** `SessionView` is the new DEFAULT `ViewMode`
(`Session ∣ Arrange ∣ Mix`) — an Ableton-style tracks×scenes clip-launch grid on Tracktion's `Scene` /
`ClipSlot` / `LaunchHandle`, playable with mouse + keyboard. The shipped 4OSC / piano-roll / mixer ride
inside slots and scenes. Full design record in [devlog/session-design.md](devlog/session-design.md).
- **Grid** (`src/ui/session/`, 10 new files): `SessionLayout` (geometry + a shared `rowBand` partition so
  scene rows never drift from pads), `SlotVisualState` (ONE pad-state model for the screen + the future LED
  encoding), `ClipSlotComponent` (the pad leaf — holds `(track,scene)` indices only, **never** a cached
  engine pointer, R1), `TrackColumnComponent` (per-track header — name / Audio-MIDI tag / 4OSC chip / M-S-R
  — + pads + clip-stop), `SceneColumnComponent` (pinned scene-launch column + MASTER stop-all), `SessionView`
  (top-level grid: a 25 Hz **gated** state poll, keyboard launch, null-guarded shell seams).
- **Engine seam** (`ProjectSession`): `ensureScenes` (grow-only, off the user undo stack, seeds the sheet-00
  scene names for newly-grown rows only), a **const non-mutating** `getClipSlot` (safe at 25 Hz — never
  inserts a track/slot), `createMidiClipInSlot` (born-audible via 4OSC), `importAudioIntoSlot`, and
  `launchSlot`/`stopSlot`/`stopTrackClips`/`stopAllSlots`/`launchScene` — **quantised + audible** launch
  ported from the engine's `ClipLauncherDemo`.
- **Shell** (`main.cpp`, `ControlBar`): `ViewMode::Session` default + **F8**; the atomic 3-way view-switch
  renumber (0=Session / 1=Arrange / 2=Mixer); grid selection routes a MIDI clip → piano-roll drawer; a
  **rebuild-on-track-change** guard closes a cross-view use-after-free.
- **Interaction:** single-click **launches** (instant); **right-click "Edit clip"** opens a clip
  launch-free; double-click opens it as a convenience (belt-and-suspenders edit paths).
- **Verified (headless):** a new **`--selftest-session`** audibility gate (create a clip in a slot, launch,
  assert the `LaunchHandle` reaches *playing* with the transport rolling) → **PASS**; a new **`--screenshot`**
  mode renders each view to PNG for headless visual review (the grid matches sheet 00). Built by
  file-disjoint agents against fixed header contracts; a **5-lens adversarial QC** confirmed 12 real issues
  (a use-after-free blocker, dead keyboard focus, a 25 Hz poll doing ~6,400 tree-walks/s, scene/pad row
  drift, a double-click that also launched) — all fixed; an **independent fix re-verify** then caught + fixed
  two regressions introduced by those fixes.
- **Open:** ~~16 scene rows don't fit a short window~~ **RESOLVED — vertical scroll shipped (see below).**
  Real hardware controllers are the next "one day" seam (§5). Live mouse interaction still needs a manual pass
  (the dev window can't be GUI-driven headlessly, but `--screenshot` covers rendering).

### Session-grid vertical scroll — SHIPPED  (`8d15234`; built + verified + pushed this session)
The 16-scene layout question is **resolved: vertical scroll** (Ableton-style, keep full-size pads) over
fit-to-window. The grid was fit-to-window — pads stretched tall and the bottom scene rows were clipped and
unreachable on a short window. It now scrolls vertically with **FIXED ~46 px pads** (`SessionLayout::slotH`
promoted to 46): all 16 scene rows are reachable, the pinned scene column tracks the pads while scrolling, and
a real vertical scrollbar appears. Isolated to `src/ui/session/SessionView.{h,cpp}` + `SessionLayout.h`.
- A nested **`ScrollingViewport`** overrides `visibleAreaChanged` → `onScroll` → `syncSceneColumnToScroll()`,
  which offsets the pinned scene column by `-getViewPositionY()` so scene launch rows stay aligned with pads.
- **Bug found + fixed during verification:** `resized()` now uses `columnHolder.setSize(...)` (not
  `setBounds(0,0,...)`) — the viewed component's top-left **is** the scroll offset, so the old code snapped the
  grid back to the top on any relayout while scrolled.
- **Verified:** `--screenshot` now renders a short-window `session_top` + `session_scrolled` pair as headless
  proof of the scroll; all three selftests still PASS. Design record:
  [devlog/session-scroll-design.md](devlog/session-scroll-design.md).

### App-wide logging + error-handling subsystem — SHIPPED (NEW)  (`8d15234`; built + verified + pushed this session)
Forge previously had essentially **no logging** (3 incidental sites; ad-hoc status-strip messages only). New
facility **`src/core/Log.h` / `src/core/Log.cpp`** (added to CMake `target_sources` after `main.cpp`):
- A `juce::Logger` subclass installed via `setCurrentLogger` (so JUCE's own device logs are captured too) +
  a `SystemStats` crash handler; **four levels** (ERROR / WARN / INFO / DEBUG — DEBUG compiled out of Release);
  ergonomic `FORGE_LOG_*(juce::String)` macros.
- A `CriticalSection`-guarded **file sink** at `%APPDATA%\Forge\logs\forge.log` (1 MiB cap, single `.1`
  rollover) + unconditional **stderr echo** so headless selftests surface logs. Installed as the first line of
  `ForgeApplication::initialise`, torn down in `shutdown`.
- **~90 failure seams backfilled** across ~15 files (ProjectSession, Exporter, engine/*, main.cpp, session,
  mixer/browser/detail/arrange/pianoroll). **HARD RULES:** never log on the audio/RT thread or per-tick in a
  poll/paint (the one allowed poll log is an edge-gated track-count-mismatch in SessionView); autosave logs
  only on `save()==false`.
- This is now a **STANDING BUILD PRINCIPLE** — log fallible seams as you build them — documented in
  **[LOGGING.md](LOGGING.md)** (principle + cheat-sheet + new-feature checklist). `.gitignore` now excludes
  `*.log` / `forge.log*`. Design record: [devlog/logging-design.md](devlog/logging-design.md).

### W7 — MIDI record into Session slots — SHIPPED  (`160f6cc`; built + verified + pushed this session)
**A track can be MIDI record-armed and an empty slot captured straight into a born-audible clip.** The Session
arm button branches MIDI vs audio; an empty slot on a MIDI-armed track is captured via **Ctrl+Enter** or
right-click **"Record into slot"** → a live-played MIDI loop is recorded straight into that slot as a new
born-audible (4OSC) `te::MidiClip`. Recording is **transport-driven** (`transport.record()`), **NOT**
launch-driven — the armed slot's `itemID` is the record target and on stop the engine materialises the clip in
the slot. This is **verdict (A): direct `ClipSlot` recording**, and it is now **empirically proven** by the new
`--selftest-midi` gate. Frozen design record: [devlog/midi-record-design.md](devlog/midi-record-design.md).
- **Record layer** (`RecordController`): new MIDI methods (`enableMidiInputs`,
  `armFirstMidiInputToSlot` / `…ToTrack`, `disarmSlot` / `disarmMidiTrack`,
  `isSlotMidiArmed` / `isTrackMidiArmed`) with **its own MIDI enable sequence** (`getMidiInDevices` +
  `setMonitorMode(automatic)` + `rescanMidiDeviceList`) — **not** a clone of the wave path.
- **Session record seam** (`ProjectSession`): `recordArmSlot` / `beginSlotRecord` / `commitSlotRecord` /
  `isSlotRecording`, delegating to the recorder via injected `std::function`s (no hard `RecordController`
  dependency). Slot capture is **slot-ONLY** — it drops any track record target first so notes never
  double-capture to the arrangement. Audio/MIDI arm are **mutually exclusive per track** (v1).
- **Pad state** (`SlotVisualState`): new `recording` state (pulsing red), dominating all clip/queue states for
  the one capturing pad; `ClipSlotComponent::paint` gains an explicit `recording` branch.
- **Verified (headless):** the new **`--selftest-midi`** gate creates a `VirtualMidiInputDevice`, arms slot
  (0,0), rolls the transport, injects 4 synthetic notes, and asserts the slot's committed clip holds
  **exactly** those notes (`capturedNoteCount == notesInjected`, `preExistingNotes==0`) → **PASS**. Adversarial
  QC: 0 blocker/major, 2 minor found + fixed (swapProject MIDI-teardown; slot-arm error-message fallback).

### Wave 01 — six parallel feature seams — SHIPPED  (first multi-CLI wave; baseline `6100fb9`)
Forge's first **flat parallel multi-CLI wave**: six file-disjoint feature CLIs (P1–P6) built against
contract-first seams and committed their own scoped commits; the orchestrator (P7) implemented the two shared
`ProjectSession` seams, wired everything into the single integration build, and ran adversarial QC. Full record:
[devlog/wave-01-features.md](devlog/wave-01-features.md).
- **P1 metronome + count-in** (`096c9bd`): a stateless `Metronome` seam over the engine's `Edit::clickTrackEnabled`
  (persisted, OFF by default) + native `Edit::CountIn` (a global setting; whole-bar count-in ≤ 2 bars). TransportBar
  gains a **Click** toggle + count-in selector. Native count-in — `transport.record()` pre-rolls it, no
  `RecordController` change.
- **P2 MIDI-learn** (`1ef4f37`): a thin `MidiLearn` driver over Tracktion's native `ParameterControlMappings`
  (persists on the Edit) + `PluginHost::getAutomatableParameters`. Wired minimal — a **Ctrl+L** track▸plugin▸param
  picker arms a learn, proven headlessly by the new **`--selftest-midilearn`** gate. **Deferred:**
  `ForgeUIBehaviour` / a MIDI-input listener so real controller CCs reach the seam.
- **P3 buses / sends** (`c5062a3`): per-track **A/B aux-send knobs** + two **aux-return strips** in the mixer, over
  a new `ProjectSession` aux seam (`ensureAuxBus`/`setTrackSendLevel`/…). An aux bus = a plain `AudioTrack` +
  `AuxReturnPlugin`, appended at the **END** so absolute track indices stay stable; `onTracksChanged` rebuilds the
  grid/lanes on add.
- **P4 async export + progress** (`8d0afdf`): `renderEditToWavAsync`/`renderStemsAsync` run the render recipe
  off the message thread with progress + cancel; an `ExportProgress` panel drives it. **Sync API preserved.**
- **P5 markers** (`fe1bfcb`): a `MarkerBar` timeline strip over a new `ProjectSession` markers seam (keyed on the
  stable `EditItemID`), sharing the arrange `TimelineView`.
- **P6 clip edge-fade** (`975846e`): a `ClipFades` helper (5 ms linear anti-click fade, audio-only, idempotent),
  wired into `importAudioFile` + `importAudioIntoSlot`.
- **Consolidation:** implemented the aux + markers `ProjectSession` seams (source-verified engine APIs); wired all
  6 features in `main.cpp` + 5 new CMake sources. Caught + fixed a **nested-block-comment** bug in the committed
  `MarkerBar.h` (`te::MarkerClip*/Clip*` closed a `/* */` doc comment early — the CLAUDE.md gotcha; a build-less CLI
  can't catch it). **Adversarial QC** (5 dimensions → per-finding skeptic verify): **3 confirmed, 0 refuted** — two
  distinct lifetime blockers, both fixed in `swapProject` before the Edit is torn down: an **async-export UAF**
  (Edit freed under the render worker → `activeRender.reset()`) and a **MIDI-learn dangling `learningEdit`** (→
  `midiLearn.cancelLearn()`). Aux-index-stability + markers-alignment dimensions came back clean.
- **Verified:** clean MSVC Debug build (0 warnings); **all five selftests PASS** (`--selftest`,
  `--selftest-record`, `--selftest-session`, `--selftest-midi`, `--selftest-midilearn`); `--screenshot` renders all views
  (mixer shows the A/B sends + Return A/B strips; arrange shows Click + count-in). Live-gesture smoke pass still
  maintainer-only.

### W02 — MIDI-learn HW routing · Forge-native control surface · offline LUFS — SHIPPED  (`bb9ef5e` on `1eb876d`; PUSHED to `origin/main`)
Three engine-facing feature seams (roadmap items **2a / 3 / 4**), built against **source-verified Tracktion
facts** and proven headless, in **one scoped commit**. Same multi-phase agent process as prior waves: 3 parallel
source-verify spikes → an adversarial verification pass (4 skeptics, default-refuted) → 3 parallel file-disjoint
implementation agents → orchestrator integration (`main.cpp` / `CMakeLists` + the 3 new selftest gates) →
adversarial QC. Session decisions: no controller hardware on hand → build to the published MIDI spec with headless
proofs; **Option B (Forge-native)** for the control surface; **LUFS offline-only** (automation lanes + comping were
offered for item 4, the user chose LUFS); **Launchpad first** (APC40 deferred). Full record:
[devlog/wave-02-features.md](devlog/wave-02-features.md).
- **Item 2a — MIDI-learn hardware routing** (`engine/ForgeUIBehaviour.{h,cpp}`): a `te::UIBehaviour` subclass that
  returns the app's open Edit as the focused Edit — the Wave-01 deferral. The engine's native CC→param routing (in
  `PhysicalMidiInputDevice`'s `controllerParser` → `ParameterControlMappings::getCurrentlyFocusedMappings`) keys
  off `getLastFocusedEdit()`, which the default returned null for → a real knob drove nothing. Installed at Engine
  construction; the `ProjectSession` is set once `MainComponent` exists and cleared before teardown (a verified
  construction-ordering fix). Proven by the new **`--selftest-midiinput`** gate. **KEY LIMITATION:** a
  `VirtualMidiInputDevice` has NO `controllerParser` (only `PhysicalMidiInputDevice` does), so a real hardware CC
  actually driving a param is a **real-hardware smoke item**, not headless-provable.
- **Item 3 — Forge-native control surface** (`engine/GridControlDriver.h` + `LaunchpadDriver.{h,cpp}` +
  `ControlSurfaceHost.h`): a device-agnostic grid-controller driver + a Novation Launchpad (programmer-mode) driver
  to the published MIDI spec (**no hardware on hand** — byte mapping needs real-device confirmation). **Option B
  (Forge-native), verified:** Tracktion's `ControlSurface` clip-launch forwards to an UNWIRED `std::function`
  (`ExternalControllerManager`'s `launchClip`) → the framework gives no clip-launch for free, so the driver calls
  `ProjectSession::launchSlot`/`launchScene`/`stopAllSlots` directly and pushes LEDs from
  `SlotVisualState::toPadFeedback`. `ControlSurfaceHost` runs a view-decoupled ~30 Hz message-thread LED poll
  (per-pad debounce) + marshals MIDI-thread pad presses to the message thread via a lock-free SPSC
  `juce::AbstractFifo`. **Inert without hardware.** Proven by the new **`--selftest-controlsurface`** gate.
  **APC40 mkII NOT built** — its faders/transport/metering are where Tracktion's framework carries real plumbing (a
  per-device architecture call).
- **Item 4 — offline LUFS** (`engine/dsp/LoudnessAnalyzer.{h,cpp}` + `services/export/Exporter.{h,cpp}`): a
  self-contained BS.1770-4 integrated-loudness + true-peak analyzer (K-weighting biquads, 400 ms/75%-overlap
  gating, −70 LUFS abs + −10 LU rel gates), run on the rendered WAV after export; the integrated LUFS shows in the
  export-done status strip (e.g. `… — done   ·   -14.2 LUFS`). **Live master LUFS was ruled out (verified):** the
  read-only `tracktion_engine` submodule exposes no post-fader sample tap (`LevelMeasurer` = reduced dB only;
  master-tap node internal), JUCE's `AudioDeviceManager` sums secondary callbacks, and integrated loudness is
  inherently a whole-program measurement — so on-render is the correct tool. Proven by the new **`--selftest-lufs`**
  gate (a full-scale 1 kHz sine measures −3.00 LUFS ±0.5 LU).
- **Consolidation + QC:** the **nested-`*/` comment gotcha bit a THIRD time** — `ControlSurfaceHost.h`'s header
  comment had `ClipSlot*/Clip*`, whose `*/` closed the block comment early; the integration build caught it (a
  build-less agent could not; same class as `RecordController.h`/W7 and `MarkerBar.h`/Wave 01). **Adversarial QC**
  (3 dimensions): the control-surface + `ForgeUIBehaviour` dimensions returned **ZERO findings**; two LUFS findings
  — an `AsyncRender::onLoudness` latent use-after-free (**fixed:** `finishAll` snapshots `onComplete` before firing
  `onLoudness`) and synchronous whole-file analysis on the message thread blocking the UI on very large renders (a
  documented **minor follow-up, not fixed** — the async fix would re-introduce the lifetime surface just cleaned up).
- **Verified:** clean MSVC Debug build (0 warnings); **all EIGHT selftests PASS** (`--selftest`,
  `--selftest-record`, `--selftest-session`, `--selftest-midi`, `--selftest-midilearn`, `--selftest-midiinput`,
  `--selftest-controlsurface`, `--selftest-lufs`); `--screenshot` renders 5 PNGs; a normal interactive launch is
  clean (control surface constructs inert, logs "no Launchpad input found — control surface inactive"). The
  export-done LUFS readout + Ctrl+L MIDI-learn still want a maintainer-only GUI smoke pass.

### Verified by `--selftest` (current)
`mode=playback`: device open · `importedClip=1` · `numClipComponents=1` · **result=PASS** (`playing=1`).
Now **hardened against a default-device hot-swap** (a headset unplug falling back to onboard audio): the
selftest runs its import+play off the message loop, drains the device-change async cascade
(`dispatchPendingUpdates`), waits for the stream to actually roll (`blockUntilSyncPointChange`), and
**bounded-polls** instead of sampling at a blind fixed time — so a just-swapped output device no longer
reports a false `playing=0 / position=0`. (Root-caused via a source-grounded investigation workflow: it was
**test-only** fragility — the real app's Play button already flows through the robust path.) The old
input-negotiation startup balloon (25–77 s on a device change) remains fixed — startup is output-only with a
lazy record-input open.
`mode=session`: **result=PASS** — the Session-grid **audibility gate**: a clip created in slot (0,0),
launched, reaches *playing* with the transport rolling (proves the launcher playback path engages on the
same device).
`mode=midi`: **result=PASS** — the **MIDI-slot record gate** (first proof of verdict-A direct `ClipSlot`
recording): a `VirtualMidiInputDevice` is created + enabled, slot (0,0) is armed, the transport rolls, 4
synthetic notes are injected, and the slot's committed clip is asserted to hold **exactly** those notes.
`availableMidiInputs`=the virtual device · `midiDeviceEnabled=1` · `preExistingNotes=0` · `trackArmed=1`
(slot armed) · `recordingStarted=1` · `notesInjected=4` · `clipCreated=1` · `capturedNoteCount=4`
(`== notesInjected`, EXACT).
`mode=record`: **result=PASS** — recording is now **verified end-to-end on real hardware**. The
event-driven harness opens the input lazily, yields to the message loop, then arms + records a real
take: `inputDeviceCount=8 · trackArmed=1 · recordingStarted=1 · recordedClipCount=1 ·
recordedFileExists=1 · recordedClipLengthSecs≈1.44 · recordedPeakMagnitude≈0.0014` (non-zero ⇒ real
samples reached disk). The prior `FAIL` was a **harness bug** (everything ran synchronously in one
blocking message callback, starving Tracktion's async wave-input-list rebuild), not a product bug —
full root cause in `docs/devlog/device-recording.md`.

---

## 3. Code map

```
src/
├── main.cpp                       ForgeApplication (owns Engine + LookAndFeel) + MainComponent (the shell)
├── core/
│   └── Log.h / Log.cpp            app-wide logging + crash handler (juce::Logger sink → %APPDATA%\Forge\logs\forge.log + stderr; FORGE_LOG_* macros)
├── services/
│   ├── files/ProjectSession       owns the Edit; create/open/save/import; createMidiClip; isModified; MIDI slot-record seam (recordArmSlot/beginSlotRecord/commitSlotRecord/isSlotRecording via injected recorder)
│   └── export/Exporter            render the Edit → 24-bit WAV (whole-edit mixdown + per-track stems); async render + progress/cancel; offline BS.1770-4 LUFS on the render (→ export-done status strip)
├── engine/
│   ├── EngineHelpers.h            track insert, clip load, file chooser, transport toggles, lazy record-input open, track vol/pan
│   ├── RecordController           recording recipe (enable/arm/record) + disarm + isTrackArmed; MIDI slot record (enableMidiInputs, armFirstMidiInputToSlot/…ToTrack, disarmSlot/…MidiTrack, isSlotMidiArmed/isTrackMidiArmed)
│   ├── ForgeUIBehaviour           te::UIBehaviour subclass — reports the app's open Edit as the focused Edit so the engine's native CC→param routing reaches it (MIDI-learn HW routing, W02 2a)
│   ├── GridControlDriver.h        device-agnostic grid-controller driver seam (W02 item 3)
│   ├── LaunchpadDriver            Novation Launchpad (programmer-mode) driver to the published MIDI spec — drives ProjectSession launch/stop directly, LEDs from SlotVisualState (byte mapping needs a real device)
│   ├── ControlSurfaceHost.h       ~30 Hz message-thread LED poll + lock-free SPSC AbstractFifo marshalling MIDI-thread pad presses; inert without hardware
│   ├── dsp/LoudnessAnalyzer       self-contained BS.1770-4 integrated-loudness + true-peak analyzer (K-weighting, 400ms/75% gating; W02 item 4)
│   ├── PluginHost                 list/add/remove built-in + external plugins; instrument seam (4OSC at chain head)
│   └── PluginScanner              VST3/AU scan dialog → engine known-plugin list (auto-persist)
└── ui/
    ├── ForgeLookAndFeel.h         dark amber theme (colour IDs)
    ├── ControlBar                 merged top strip + view-switch + region toggles + Plugins/Export menu
    ├── transport/TransportBar     play/stop/rec/loop + timecode/bars|beats
    ├── arrange/ArrangeView        ruler + snap-division selector, lanes (M/S/R + colour), clips (waveform, drag, snap), selection, playhead
    ├── mixer/MixerView            channel strips (fader/pan/M/S), insert slots (bypass + reorder), master strip + post-fader meter
    ├── plugins/PluginWindow       floating plugin editor windows (native / generated)
    ├── browser/BrowserView        left-region file browser (double-click → import)
    ├── detail/DetailView          bottom-drawer audio-clip inspector (name/gain/fades/waveform)
    ├── pianoroll/                 bottom-drawer MIDI editor: PianoRollView (Viewport grid) + MidiNoteComponent
    └── session/                   PRIMARY view: clip-launch grid — SessionView (fixed-pad vertical scroll; MIDI arm branch + Ctrl+Enter/"Record into slot") + Track/Scene columns + ClipSlot pad + SlotVisualState (incl. recording state)
docs/   ARCHITECTURE · FEATURE_CATALOG · INTERFACE · STATUS · devlog/ (per-wave writeups)
tests/  SELFTEST.md
```

---

## 4. Pending action items

### Rough edges / near-term
- [x] **Recording verification (blocker) — DONE.** Real-hardware capture is now **verified
  end-to-end**: `--selftest-record` records a real take to disk (clip + file + length + non-zero
  peak, `result=PASS`, stable across runs). The blocker turned out to be a **harness bug**, not a
  product bug — the old selftest ran open→arm→record synchronously in one blocking message callback,
  starving Tracktion's async wave-input-list rebuild (`handleAsyncUpdate`, driven by a posted
  message). Now event-driven (open → yield → arm/record → yield → verify), mirroring the real arm
  path. Root cause + the device-pairing nuance (lazy-open keeps the existing output; the captured
  endpoint may be the default pairing, not the named mic) in `docs/devlog/device-recording.md`.
- [x] **Synthetic-input record self-test — no longer needed.** The real-hardware `--selftest-record`
  now passes, so the hosted-`HostedAudioDeviceInterface` synthetic path isn't required for
  verification. `installSyntheticInputForSelftest` is kept for optional hardware-free CI.
- [x] **Device-override.** FIXED — the saved output is preserved (verified: playback selftest keeps
  the saved Bluetooth output; the startup `initialise()` stomp is gone). The preserve-output logic now
  lives in the engine's output-only construction + `ensureRecordingInputOpen()` (keeps the output when
  it lazily adds an input). `initialiseAudioForRecording` was removed in startup-latency hardening.
- [x] **Draggable resizer bars.** DONE — custom `ResizerBar` drags the Browser width + drawer
  height (clamped); collapse/expand buttons still work. (Sizes not persisted across launches yet.)
- [x] **Keyboard shortcuts.** DONE — B/E region toggles, F9/F11 view-switch, Space play/stop, R
  record, Ctrl+S/Shift+S/O/N/I bound via `keyPressed`. (`I` is Ctrl+Import; no Inspector yet.)

### Deferred from the Phase 1 review
- [x] Import / Open / Save-As async callbacks now guarded with `Component::SafePointer`.
- [x] `openDialog`/`saveAsDialog` now have separate `openChooser`/`saveChooser` members.
- [x] `ProjectSession::saveAs` assigns `editFile` only on success; `saveAsDialog` checks the bool.
- [x] `TimelineView::xToTime` now guards `span<=0` (matches `timeToX`).
- [x] Waveform draw handles seconds-based looped clips (tiled). *Beat-based loops still single-window
  (documented in `docs/devlog/arrange.md`); time-stretched edge cases remain.*
- [x] `formatTimecode` negative-position sign handling fixed.

### Build / hygiene
- [x] `.gitattributes` added (`* text=auto`). Optional `git add --renormalize .` not run (avoids a
  large diff; warnings already silenced for new commits).
- [ ] macOS build not yet attempted (only Windows verified). VST3 + AU hosting is macOS-relevant.
- [ ] **Interactive UI not auto-verified.** Shortcuts/resizers/menus/per-lane controls compile and
  construct (full shell builds in the playback selftest) but aren't covered headlessly — verify
  manually or via computer-use before relying on them.

### Phase 3 follow-ups (mixer / plugins / browser / inspector)
- [x] **Startup latency hardening.** DONE — the engine is constructed with a `ForgeEngineBehaviour`
  whose `shouldOpenAudioInputByDefault()` returns false, so `te::Engine`'s ctor opens OUTPUT only and
  never negotiates a capture device on the message thread at launch (that open could stall 25–77 s
  when the default device changed). The recording input opens lazily on the first arm/record via
  `EngineHelpers::ensureRecordingInputOpen()`. Measured: headless playback selftest startup dropped
  ~17 s → ~8 s. Adversarial review caught that the first attempt (reconfigure-after-ctor) didn't
  actually remove the ctor's input open, plus two record-path regressions — all fixed (see devlog).
- [x] **External plugin scanning UI.** DONE — `PluginScanner` hosts JUCE's `PluginListComponent`
  in a dialog (ControlBar **Plugins** button), bound to the engine's format manager + known-plugin
  list; scans persist automatically and surface in `PluginHost::getAvailablePluginNames`.
- [x] **Master-output metering.** DONE — the master strip reads `EditPlaybackContext::masterLevels`
  (the engine's post-fader master output measurer), re-bound each poll. Earlier approach (inserting a
  `LevelMeterPlugin` on the master chain) was rejected in review: it dirtied a clean Edit + metered
  pre-fader. Now no Edit mutation and post-fader, consistent with the track strips.
- [x] **Plugin insert reorder + per-insert bypass.** DONE — ▲/▼ reorder within a track's chain (the
  volume/meter tail stays last) + a bypass dot per insert (`te::Plugin::setEnabled`).
- [x] **Snap-division selector** — DONE — Off/Bar/½/¼/⅛/1⁄16 selector in the arrange ruler corner;
  grid math is denominator-aware (correct in any time signature, not just 4/4).
- [ ] **Async export + progress** — export (mixdown + stems) currently blocks the message thread
  (fine for short edits). Stem export added (`Exporter::renderStems`, ControlBar Export ▸ Stems).
- [ ] **Live cross-surface refresh** — Mixer/Inspector read engine state on `setEdit`/select only
  (manual-rebuild model); a value changed on another surface updates on re-select, not live.

### Later / feature-gated
- [x] **MIDI tracks + piano-roll — MVP + W6 polish DONE** (`9a24989`, `bb5b6bf`). Draw a MIDI clip and
  hear it via a default 4OSC; clips render polymorphically (`ClipComponent` base); piano-roll has
  velocity lane + multi-select + copy/paste.
- [x] **W7 — MIDI record into Session clip slots — DONE** (`160f6cc`). MIDI record-arm a track, capture an
  empty slot (Ctrl+Enter / right-click "Record into slot") into a born-audible `MidiClip`; transport-driven
  (verdict A), proven by `--selftest-midi`. Its own MIDI enable sequence in `RecordController`. **Post-MVP
  remaining:** horizontal auto-scroll-to-clip. Live GUI draw→play path still needs a manual smoke pass.
- [x] **W02 — MIDI-learn HW routing + control surface + offline LUFS — DONE** (`bb9ef5e`, pushed to
  `origin/main`). Focused-Edit `ForgeUIBehaviour` (item 2a, `--selftest-midiinput`); a Forge-native grid
  control-surface driver + a Novation Launchpad driver (item 3, `--selftest-controlsurface`; byte mapping needs a
  real device); offline BS.1770-4 LUFS on the export render → export-done status strip (item 4, `--selftest-lufs`).
  **Remaining:** a real-hardware Launchpad smoke test (proves both the physical-CC routing and the driver byte
  mapping — the one thing not headless-provable); an APC40 mkII driver (deferred); LUFS off the message thread for
  very large renders (QC minor). Details: [devlog/wave-02-features.md](devlog/wave-02-features.md).
- [ ] ASIO (needs Steinberg SDK + `JUCE_ASIO=1`); MP3 import (`JUCE_USE_MP3AUDIOFORMAT=1`).
- [ ] `rtcheck` RT-safety tool is macOS/Linux only — N/A on the Windows dev box.
- [ ] AGPLv3 obligations when distributing builds (share source — trivial for this repo).

---

## 5. Full plan

### Locked decisions
1. Build **on** Tracktion Engine (don't hand-roll the audio graph).
2. **Audio thread is sacred** · the Edit is the single source of truth · the UI observes.
3. UI: **Ableton look/feel; the Session clip grid is the PRIMARY surface**, Arrange a secondary view via
   `ViewMode`; **controller-driven** (Launchpad / APC-class grids); mixer = a full-window view-switch;
   **warm amber** accent on dark. *(Reverses the old "arrangement-first, Session deferred" decision —
   2026-06-30; see [DIRECTION.md](DIRECTION.md).)*
4. **Log fallible seams as you build them** (2026-06-30). Every new feature routes its failure paths through
   `src/core/Log.*` (`FORGE_LOG_*`) — never on the audio/RT thread, never per-tick in a poll/paint. Standing
   build principle; the principle + cheat-sheet + new-feature checklist live in **[LOGGING.md](LOGGING.md)**.

### Engine roadmap (from ARCHITECTURE.md §11)
| Phase | Goal | State |
|---|---|---|
| 0 — Toolchain | Build + first sound | ✅ done |
| 1 — The spine | Record & play a track (load/save, import, transport, playhead, record) | ✅ done (device-override fixed; **recording verified end-to-end on real hardware**) |
| 2 — Mixer & plugins | Volume/pan/mute/solo, buses, sends; **VST3/AU hosting**; built-in FX | ✅ done (strips/inserts/meters/master + insert bypass/reorder + plugin hosting + external scan UI + floating windows + **buses/sends (aux A/B, W01 P3)** done) |
| 3 — MIDI & editing | MIDI tracks + piano roll; built-in synth; non-destructive audio editing; automation | ⏳ (**MIDI MVP + W6 polish + W7 record built** — draw a clip + hear it via 4OSC, polymorphic `ClipComponent`, piano-roll with velocity/multi-select/copy-paste → `docs/devlog/midi-build.md`; **W7 MIDI record into Session slots done** (`160f6cc`, verdict A, `--selftest-midi` PASS → `docs/devlog/midi-record-design.md`); **MIDI-learn param mapping (W01 P2) + its HW-routing focused-Edit `ForgeUIBehaviour` (W02 item 2a, `--selftest-midiinput` PASS)** done; automation to do) |
| 4 — Polish | Comping, metering (LUFS), export (WAV/MP3/stems), markers, snap | ⏳ (peak meters + WAV mixdown + per-track stems + snap-division + **markers (W01 P5)** + **async export w/ progress (W01 P4)** + **metronome/count-in (W01 P1)** + **anti-click edge-fade (W01 P6)** + **offline BS.1770-4 LUFS on export (W02 item 4, `--selftest-lufs` PASS)** done; comping to do) |
| 5 — Deferred | Sidechain, warp, controller mapping, advanced routing, video | ⏳ (**controller mapping — a Forge-native grid control-surface driver + a Novation Launchpad driver built (W02 item 3, `--selftest-controlsurface` PASS; byte mapping needs a real device), APC40 mkII deferred**; sidechain/warp/routing/video to do) |

### Interface build order (from INTERFACE.md)
| Phase | Items | State |
|---|---|---|
| 1 — Shell refactor | ForgeShell, Control Bar, LookAndFeel, view-slot, collapsible regions, tooltips | ✅ done |
| 2 — Arrange polish | per-lane mute/solo/arm + color, context menus, bars\|beats ruler, selection, clip drag + snap + info hint ✅; snap-division selector (Off/Bar/½/¼/⅛/1⁄16, denominator-aware) ✅ | ✅ done |
| 3 — Browser/Inspector | left-column file Browser (double-click import) ✅; clip Inspector (props) ✅; drag-onto-track + value popups to do | ✅ mostly |
| 4 — Detail Drawer | clip inspector (name/gain/fades/waveform) ✅; **piano-roll (draw/move/resize/delete, audible via 4OSC) ✅**; automation later | ✅ (audio inspector + MIDI piano-roll) |
| 5 — Mixer + devices + plugins | Mixer view-switch ✅; channel strips + inserts + master + meters ✅; floating plugin windows ✅; device chain reorder + per-insert bypass ✅ | ✅ done |
| 6 — Config + delivery | tabbed Preferences (folds in Audio settings); Export/Render | ⏳ |
| 7 — Power-user + Session | **SessionView (primary clip-launch grid) ✅**; tear-off panels + saved layouts to do | ✅ (Session grid) |

### How they interleave (recommended path)
Phases 0–4 + startup-latency hardening are done: the spine, arrange surface (incl. snap-division
grid), mixer (incl. insert bypass/reorder + post-fader master meter), plugin hosting + scan UI,
browser, inspector, export (mixdown + stems), and output-only startup with lazy record-input open.
Practical next sequence — **direction reset 2026-06-30** (see [DIRECTION.md](DIRECTION.md)): the primary
surface is becoming the **Session clip grid**, so the sequence is reordered around that.
1. **Session-grid build — the pivot. ✅ DONE** (`def1193`). `SessionView` is the primary `ViewMode` (Session
   default) on Tracktion's `ClipSlot` / scenes / `LaunchHandle`; playable with mouse + keyboard, launch
   audible + bar-quantised; built + adversarially QC'd + fix-re-verified (see the Session-grid section in §2
   and [devlog/session-design.md](devlog/session-design.md)). The **16-scene vertical scroll** (Ableton-style,
   fixed ~46 px pads) is now **✅ DONE this session** (built + verified, committed as `8d15234` + pushed — see
   §2). **Remaining:** a manual GUI smoke pass of live mouse interaction.
2. **Control-surface layer ("one day"). ✅ Forge-native driver + Launchpad BUILT** (W02 item 3, `bb9ef5e`,
   `--selftest-controlsurface`). Chose **Option B (Forge-native)** — Tracktion's `ControlSurface` clip-launch is
   an unwired `std::function`, so the driver calls `ProjectSession` launch/stop directly and pushes LEDs from
   `SlotVisualState::toPadFeedback` (the same pad model the on-screen grid uses). Inert without hardware.
   **Remaining:** a **real-hardware Launchpad smoke test** (byte mapping / LED palette need a device); an **APC40
   mkII** driver (deferred — its faders/transport/metering are where the framework carries real plumbing).
3. **MIDI input roles.** ✅ **W7 note-record into slots DONE** (`160f6cc`); ✅ **MIDI-learn param mapping (W01 P2)
   + its HW-routing focused-Edit `ForgeUIBehaviour` (W02 item 2a, `--selftest-midiinput`)** DONE — real physical-CC
   drive is a real-hardware smoke item (a virtual device has no `controllerParser`). Still to do: **MIDI-clock /
   Ableton Link** sync.
4. **Done this slice (now reusable inside slots/scenes):** ✅ recording verified on real hardware; ✅ MIDI
   MVP + W6 piano-roll polish (draw + hear, velocity / multi-select / copy-paste); ✅ **W7 MIDI record into
   Session slots**. A **manual GUI smoke pass** of the draw→play + slot-record path is still worth doing.
5. **Carried-over polish.** ✅ buses/sends + async export + markers (W01), ✅ **offline LUFS on export (W02
   item 4)**; still to do: automation (volume/pan/plugin-param lanes); comping; an optional **live short-term LUFS
   meter** (would require forking the engine for a post-fader sample tap — no non-mutating tap exists today);
   off-thread record-input open.

---

## 6. How to build & run

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"            # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest # headless verify
```
