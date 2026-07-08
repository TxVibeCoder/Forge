# Forge — Project Status & Roadmap

*Living status document. Last updated 2026-07-08 (latest: **W21 — frontier Wave 9 (LFO modifiers) + a self-rendered drum sampler for Step Clips** — a two-track wave that **completes the 10-wave frontier build program** (Wave 9 was the last one outstanding): a header-only `src/engine/ModifierHelpers.h` LFO seam (`--selftest-modifier`), and Step Clips now born with a `te::SamplerPlugin` of 8 self-rendered CC0 drum one-shots (`--selftest-drumkit`); build clean, **40/40 floor** (38 → 40), 11/11 screenshots, a 2-dimension adversarial QC (both SHIP, 4 minor/doc fixes); **PUSHED to origin/main (W21 tip `87225da`)**; devlog `devlog/wave-21-lfo-drumkit.md`. ⚠ This top-block history was stale at W16 — W17–W20 (frontier Waves 7/8/10 incl. the Step Clips CAPSTONE) and the MIDI-file-import feature also shipped in between; **[HANDOFF.md](HANDOFF.md) is the current cross-wave source**. Prior: **W16 — frontier program Wave 6: W05 QC debt discharge (undo + popout + lifetime hardening)** (PUSHED to origin/main, tip `96b1037`; floor stays **32** — all 6 dimensions extend 4 existing gates, zero new gate names; **all THIRTY-TWO selftests PASS**; building it surfaced a confirmed, unfixed engine defect — `FourOscPlugin`'s mod-matrix flush wipes the redo stack on every save, see the CLAUDE.md gotcha — and QC caught + fixed a real regression in the wave's own piano-roll fix (selection/scroll wiped on unrelated undo/redo); devlog `devlog/wave-16-w05-qc-debt.md`); prior: **W15 — frontier program Wave 5: Session scene lifecycle (rename → delete → reorder)** (PUSHED to origin/main, tip `9cc7f04`; floor 29 → 32 via `--selftest-scenerename` / `-scenedelete` / `-scenereorder`; a 5-dim adversarial QC fixed a MAJOR reorder desync + 2 MINORs; devlog `devlog/wave-15-scene-lifecycle.md`); prior: **W14 — frontier program Wave 4: MIDI quantise** (pushed to origin/main); prior: W13 grid clip primitives, W12 per-clip launch quantise, **W11 — frontier program Wave 1: launcher
expressiveness (follow-actions · loop-toggle · launch-modes)** on the W10 tip `90449ce` — the FIRST wave of the
10-wave frontier build program. Per-clip follow actions + loop-toggle + launch modes (Trigger/Gate/Toggle) via
new ProjectSession seams + SessionView submenus + the ClipSlotComponent onReleased (Role B); the engine
FOLLOWACTIONS auto-plant footgun defeated; follow actions ride graph-build with zero per-tick work. Gates
`--selftest-followaction` + `--selftest-launchmode` (floor 24→26); 5-dimension QC — 3 dims REFUTED clean, Trigger
byte-identical, 3 routing findings fixed. **All TWENTY-SIX selftests PASS.** Full record →
[devlog/wave-11-launcher-expressiveness.md](devlog/wave-11-launcher-expressiveness.md). Prior: **W10 — the
hands-on wave, part 5 (the last): the
Session → Arrangement "Send to" bridge** on the W09 tip `76d8f38` — the fifth and FINAL build wave off the
maintainer's first hands-on session; the hands-on plan is now **COMPLETE**. An explicit one-directional "Send to
Arrangement" copies a filled Session slot's clip onto the same track's linear timeline (append-at-end ·
single-clip); a 5-dimension adversarial QC fixed 2 confirmed defects (the [HIGH] `playSlotClips` silence + the
[Medium] slot auto-tempo/loop carry-over) and refuted 3 clean; **all TWENTY-FOUR selftests PASS**. Full record →
[devlog/wave-10-send-to-arrangement.md](devlog/wave-10-send-to-arrangement.md). Prior this session: **W09 — the
hands-on wave, part 4:
self-rendered instruments + an audible demo** on the W08 tip `1a59973` — the fourth build wave off the
maintainer's first hands-on session. Makes the app **audibly playable out of the box**: per-track instrument
presets (a 4OSC kick, a 4OSC bass, a **Sampler** loaded with a **self-rendered CC0 piano one-shot** —
`InstrumentSamples.{h,cpp}`, xorshift32-deterministic, into `%APPDATA%\Forge\library`), a note-written C-minor
demo (4-on-floor kick · walking bass · Cm–Ab–Bb–Cm chord stabs), and a **first-launch welcome demo**. The
maintainer chose **HYBRID, no browsable library**. Built by one instrument-layer agent + orchestrator → clean
build (0 warnings) → the **TWENTY-THREE-gate floor** (new `--selftest-demo`) → a **3-dimension adversarial QC**
confirming **NO blockers/majors** (the instrument finder refuted 10 candidate bugs; one doc-drift fixed,
hardening notes documented). **all TWENTY-THREE selftests PASS**; the base `session` screenshot shows the
note-seeded groove. Committed + **PUSHED to `origin/main`**. Full record →
[devlog/wave-09-instruments.md](devlog/wave-09-instruments.md). Prior wave: **W08** — per-track Session mixer
strips (`0ad7abc`, → [devlog/wave-08-session-mixer.md](devlog/wave-08-session-mixer.md)); **W07** — Session-grid
interactions (`fc0fdbe`, → [devlog/wave-07-handson-grid.md](devlog/wave-07-handson-grid.md)); **W06** — control
bar & HUD (`e670ab5`, → [devlog/wave-06-handson.md](devlog/wave-06-handson.md)). Earlier same continuous effort: **W05**
(`5e5dcf2`; → [devlog/wave-05-undo.md](devlog/wave-05-undo.md)); **W04b** (`cc27300`; →
[devlog/wave-04b-ux.md](devlog/wave-04b-ux.md)); **W04a** (`41e3139`; →
[devlog/wave-04a-ux.md](devlog/wave-04a-ux.md)); **W03** (`ffa494d`; →
[devlog/wave-03-features.md](devlog/wave-03-features.md)); **W02** (`bb9ef5e`); **Wave 01** (`e3b8c7c`);
**W7** (`160f6cc`); **scroll + logging** (`8d15234`).)*
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
| **Code size** | ~26,200 lines of Forge source (engine/JUCE excluded) across 87 files |

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

### W03 — automation lanes · MIDI-clock out · async LUFS · live refresh — SHIPPED  (this session; baseline `cede941`)
Four engine/UI features + the INTERFACE.md Session-first rewrite, selected for **headless provability** (the
maintainer has no MIDI hardware and runs no manual tests — a standing constraint now, so hardware smoke items
stay parked). Process: 4 source-verify spikes → 3 adversarial skeptics (one refuted + corrected the sync
recipe pre-build) → 6 file-disjoint implementation agents on **tiered models** → orchestrator integration
(3 new gates) → an 18-agent adversarial QC (9 confirmed, 1 refuted, all fixed). Full record:
[devlog/wave-03-features.md](devlog/wave-03-features.md).
- **Automation lanes** (`engine/AutomationHelpers.h` + `ui/arrange/AutomationLane.{h,cpp}` + ArrangeView): a
  header-only seam over each track's VolumeAndPanPlugin curves (units = fader position 0..1 / pan −1..+1;
  every mutator ends in `updateStream()` — activation is otherwise async) + a collapsible 46 px per-track
  lane in Arrange (an **A** toggle beside M/S/R; click adds, drag moves, right-click deletes/clears; Volume|Pan
  selector; pixel-exact via the shared `TimelineView` axis). The lane listens for `curveHasChanged` so
  external curve edits (mixer fader single-point move — accepted engine semantics — MIDI-learn, undo) repaint
  live. Points persist in the `.tracktionedit` automatically. Proven by **`--selftest-automation`**.
- **MIDI-clock out** (`engine/MidiClockSync.h` + `engine/MidiClockProbe.h` + TransportBar **Clock** toggle):
  per-device `setSendingClock` through a small seam. Proven by **`--selftest-sync`** — a probe subclass
  captures the ACTUAL wire bytes (SPP, start, 24 PPQN clock train within 0.5–1.5× expected, stop), with an
  honest SKIP-degrade on zero-MIDI-out machines and a fully lossless teardown (device entry, scan interval,
  and NAME-keyed persisted props all restored). **Ableton Link deferred** — the wrapper is compiled out and
  the library is NOT vendored (a dependency + license decision).
- **LUFS off the message thread** (`Exporter` + `LoudnessAnalyzer`): analysis now runs on the export render
  worker after the WAV closes; the message thread receives only a finished value under the existing alive
  token; a per-chunk **abort predicate** keeps `~AsyncRender`'s 5 s join bound honest. The W02 QC callback
  invariant is preserved verbatim. `--selftest-lufs` gained file+thread and abort legs.
- **Live cross-surface refresh** (MixerView + DetailView): the 28 Hz mixer tick runs a structural guard then
  guarded engine→widget sync (drag brackets, focus, `dontSendNotification`; repaint/allocation-free steady
  state; zero tick logging); DetailView polls at 10 Hz. Proven by **`--selftest-livesync`**.
- **The hang that became a product fix:** the first sync-gate run froze at shutdown — bisected to the master
  strip's PeakMeter holding a raw pointer into `EditPlaybackContext::masterLevels`, whose owner (the playback
  context) the gate freed; `removeClient` then walked freed memory. Reachable in the real app via device
  restarts. Fixed by holding meter sources as **`juce::WeakReference<te::LevelMeasurer>`** (self-nulling on
  owner death) — one mechanism also covering the plugin-cull race on track meters (QC major).
- **QC blockers/majors fixed:** a deterministic ReturnStrip 28 Hz UAF after deleting an aux-return track
  (re-resolve-before-deref, the R1 rule); the Clock toggle left unwired at integration (the gate drove the
  seam directly — exactly the gap adversarial QC exists to catch); the meter cull race; the stale automation
  lane on external curve edits. Plus 5 minors (degrade-path roll, props restore, drag clamp, hit-test z-order,
  INTERFACE.md tense).

### W04a — the UX wave, part 1: LCD · channel tray · menu bar · sequence lighting — SHIPPED  (this session; baseline `9a28845`)
The first slice of the W04 UX charter (INTERFACE.md §4), under Fable's design authority. Process: 3 spikes
(+ an adversarial skeptic on the count-in mechanics) → frozen design contracts (accent colour IDs pre-laid by
the orchestrator) → 4 file-disjoint Fable agents → shell integration → 14-gate floor → 22-agent adversarial QC
(10 confirmed incl. 1 blocker, 2 refuted — all fixed). Full record: [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md).
- **The transport LCD** (`ui/transport/LcdModel.h` + `LcdDisplay.{h,cpp}`): a GarageBand-style inset screen,
  centre of the control bar — bars|beats, tempo, key · time-sig — whose face becomes a large beat-locked
  **count-in digit with a record-red pulse** during record pre-roll. Pure model; digits derive from the
  engine's CLICK GRID (whole timeline beats) so they land on the audible clicks even recording from a
  mid-beat stop (the QC major). Supersedes the TransportBar's old readout; the HH:MM:SS timecode is a
  **deliberate drop** (returns as a width-gated LCD zone in W04b). Gate: **`--selftest-lcd`**.
- **The channel tray** (`ui/tray/ChannelTray.{h,cpp}`): the GarageBand/Logic inspector — a left-sidebar
  **Files | Channel** tab band; selecting a track (or one of its clips — selection follows to the owner)
  shows its strip: pan, A/B sends, insert rows (click opens editors), fader, M/S. Track identity re-validated
  before every dereference; poll gated on visibility; auto-reveal never overrides an explicitly pinned Files
  tab. The standalone Mix view stays (all-tracks overview vs one-track focus). Gate: **`--selftest-tray`**.
- **The menu bar** (`ui/menu/ForgeMenuModel.{h,cpp}` + `MainWindow::setMenuBar`): File/Edit/View/Transport/
  Help from one command table, shortcut labels beside items, live tick marks, every callback null-guarded.
  **The control bar's file-command buttons moved here** (the charter's division of labor — and the QC blocker
  fix: the freed ~500 px lets the transport and the LCD both fit from the default width). Bonus: the
  transport **Rec button was wired to nothing since Phase 1** — now fixed. Gate: **`--selftest-menu`**.
- **Sequence lighting + the accent vocabulary**: playing pads pulse **playGreen** (beat-locked, from the same
  bars|beats chain as the LCD, inside the existing 25 Hz poll), queued pads breathe **playGreenDim**, and
  **amber now means selection only** across the grid and scene column. Pure `padPulseAlpha` curve asserted by
  the LCD gate.
- **Shell**: browser/drawer sizes persist (engine PropertiesFile); the screenshot matrix grew to 8 states
  (`arrange_automation` via a new `ArrangeView::setAutomationLaneExpanded` seam, `arrange_tray`,
  `lcd_countin` via the LCD's demo seam). The menu bar renders as window chrome above the shell content, so
  it is NOT in the component snapshots — a window-level capture is a W04b harness item.

### W04b — the UX wave, part 2: tear-offs · slide-outs · timecode · tray meter — SHIPPED  (this session; baseline `ecfd5e1`)
The W04 charter's remainder. Process: 2 spikes (tear-off mechanics skeptic-corrected pre-build) → 4
file-disjoint agents + orchestrator shell work → integration → 15-gate floor → 18-agent QC (9 confirmed
incl. 1 blocker, all fixed). Full record: [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md).
- **Tear-off panels** (`ui/popout/PopoutWindow.{h,cpp}`): View ▸ Pop Out Mixer / Piano Roll float the live
  shell members into desktop windows (reparented, never recreated; double-delete structurally impossible via
  `setContentNonOwned`; deferred close; normal z-order; keys bubble to the shell; Mix-while-out fronts the
  window; menu items tick while out; a project swap leaves tear-offs alone). Gate: **`--selftest-popout`**
  with the no-ghost-overlay hardening.
- **Animated slide-outs**: B/E ease ~160 ms via a dedicated scalar-lerp timer through `resized()`
  (`ComponentAnimator` rejected — it fights the layout chain); `revealDrawer()` routes programmatic opens;
  resizers inert mid-slide; persisted sizes untouched.
- **The timecode LCD zone**: absolute time back as the width-gated 4th zone (sheds first; clamps negative
  pre-roll to 0:00.000); default launch width now 1200 so all four zones show from first run.
- **Shared PeakMeter** (`ui/common/PeakMeter.h`, extracted verbatim) + a **tray meter** (Edit-owned track
  measurer; attachment asserted by the tray gate).
- **Session tray-follow** (`SessionView::onTrackFocusChanged`, index-based) + a view-switch seed; **the
  Arrange playhead → timeTempo** (brightened, dark-edged against the automation teal); the **window-level
  screenshot** (`shell_window`).

### W05 — global Undo/Redo + the polish sweep — SHIPPED  (this session; baseline `7034955`)
Two tracks: global Undo/Redo, and the W04c polish list. Process: 2 source-verify dossiers (undo mechanics ·
polish inventory) → 2 parallel file-disjoint agents (scene polish · strip widgets) + orchestrator-serial
main.cpp work (the undo track + the two main.cpp polish items) → integration → **16-gate floor** →
adversarial QC (**LIMIT-INTERRUPTED** — see below). Full record: [devlog/wave-05-undo.md](devlog/wave-05-undo.md).
- **Global Undo/Redo** (`main.cpp` + `ui/menu/ForgeMenuModel.{h,cpp}`): **Edit ▸ Undo / Redo** with live
  enablement (a new `enabledWhen` command-table column over `canUndo`/`canRedo`) + Ctrl+Z / Ctrl+Shift+Z /
  Ctrl+Y. A thin shell over the Edit's own `UndoManager`: the five `onEditMutated` hooks seal a transaction
  eagerly (per-gesture units on top of the engine's 350 ms auto-seal); `Edit::undo()/redo()` (never the raw
  UM); undo is a **no-op with a status message while recording** (record-arm targets sit ON the stack —
  an undo mid-take would retarget the capture); after undo/redo the shell saves then synchronously fans a
  refresh out (arrange/session rebuild, mixer structural refresh, tray, markers) and closes the piano-roll
  if its clip came back PARENTLESS (a redo-of-delete leaves a live but detached `Ptr`). `ensureScenes` stays
  deliberately off the stack. Track **mute/solo are NOT undoable** (engine binds them with a null UM — by
  design). Gate: **`--selftest-undo`** (create/delete/undo/redo round-trip + a note-level leg, with
  `canUndo`/`canRedo` transition asserts).
- **Scene-column polish** (the last W04 charter item): hover lifts rows to `raisedBg`, tooltips name the
  hidden right-click stop, full-width **"■ STOP ALL"**, and the queued/playing ring gains beat-pulse parity
  with the pads (change-gated, repaint-free when static).
- **Strip-widget extraction** (`ui/common/StripWidgets.h`): the tray↔mixer fader/knob/send styling +
  range constants + `busLetter` now live once in `forge::strip` (style-only helpers; header-only, no CMake
  edit, zero behaviour change).
- **W04b deferrals closed**: the empty-centre hint (tearing the mixer off while IN Mix view now explains
  itself) and popout placement persistence (`getWindowStateAsString` per window, saved at restore/teardown,
  restored on tear-off).
- **QC — LIMIT-INTERRUPTED (honest record):** of the three planned dimensions, only **polish-regressions**
  ran before the session's agent limit; its 2 findings were recovered from the workflow transcript,
  self-adjudicated, and fixed (an em-dash rendered as mojibake through `juce::String`'s ASCII-only `char*`
  ctor; the scene-row hover fill dropped out over the ▶ launch button). The **undo-correctness** and
  **shell-integration** dimensions NEVER RAN and are owed as the next session's first action — the
  orchestrator's inline self-review found nothing, but that is not adversarial verification.

### W06 — the hands-on wave, part 1: control bar & HUD — SHIPPED  (`e670ab5`; PUSHED)
The first build wave off the maintainer's first hands-on session (15 notes → an adversarially-verified 5-wave
plan). View buttons → top-left; the Browser button → a `juce::Path` folder icon (first vector icon in the
repo); a free-trigger launch-quant selector (`LaunchQType::none`); a clickable tempo popup + tap-tempo (gate
`--selftest-taptempo`); File ▸ Exit; a cosmetic launch splash. 5 file-disjoint agents → QC (2 defects fixed,
1 major — a launch-quant combo collapsing to 0 px in a narrow window band). Record:
[devlog/wave-06-handson.md](devlog/wave-06-handson.md).

### W07 — the hands-on wave, part 2: Session-grid interactions — SHIPPED (PUSHED)  (baseline `aa45ad7`)
Wave 2 of the hands-on plan. Built the W06 way: 5 source-verify investigators → 3 orchestrator `ProjectSession`
seams → one serial `src/ui/session/*` spine agent (the four grid features) + one parallel `ArrangeView` agent
(the disjoint lane drop) → orchestrator gates + wiring + single build → 5-dimension adversarial QC → fixes.
Full record: [devlog/wave-07-handson-grid.md](devlog/wave-07-handson-grid.md).
- **Delete clip** — filled-only pad right-click "Delete clip" + Delete/Backspace → `clearSlot` (stops the
  launch, `removeFromParent` through the Edit UndoManager → **undoable via the W05 stack**) → `onEditMutated`
  + rebuild. No dialog. Gate `--selftest-slotdelete`.
- **+ Track** — a trailing neutral "+" column stub (`AddTrackColumnComponent`) in the grid → `appendAudioTrack`
  (end-append keeps absolute indices stable; fires `onTracksChanged`; no 4OSC — the synth arrives lazily on the
  first MIDI clip). Gate `--selftest-addtrack`.
- **+ Scene** — the former `SessionLayout::numScenes` (`constexpr=16`) became a runtime
  `gridScenes = jmax(16, getNumScenes())` threaded through every fixed site (pad ctor, the 25 Hz poll's flat
  stride `t*gridScenes+s`, diff buffers, content height, focus clamps); a "+ Scene" footer button →
  `ensureScenes(+1) → save() → rebuild()`. Persists; deliberately **not** undoable (`ensureScenes` is off the
  stack). Gate `--selftest-scene`; the `session_scenes` screenshot proves the >16-row render + row alignment.
- **Real file drag-drop** — `ClipSlotComponent` + `TrackLaneComponent` implement `juce::FileDragAndDropTarget`
  (audio-only via `te::soundFileExtensions`); Session pads route to `importAudioIntoSlot`, Arrange lanes to the
  new track-aware `importAudioFile(file, time, trackIndex)`. Neutral `textPrim` drop feedback on both surfaces.
  Gate `--selftest-dragdrop` (both import legs + the replace-on-drop undo contract).
- **QC (5 dimensions × per-finding skeptics):** 2 real defects fixed — a **MAJOR** pre-existing scene/pad
  `rowBand` drift (the pinned scene column was sized to a different height than the track columns; ~19 px at 20
  scenes; masked at the 1480×940 screenshot size) and a **HIGH** detached-drawer-clip on delete (found by TWO
  finders; fixed with a shared `MainComponent::reconcileDrawerClip`). Minors: drop-colour harmonised across
  surfaces; the drag-drop gate gained a replace-undo leg. A low-confidence delete-while-recording guard was
  **considered and deliberately skipped** (unproven corruption; blocking a legitimate op is not conservative);
  the aux-return-ordering cosmetic is a Wave-3 note. Refuted with evidence: the +Track re-entrant self-destruct,
  `getNumColumns` off-by-one, hover-stick, R1/R4 teardown, new-callback null-safety.
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-ONE selftests PASS**; the 10-state screenshot
  matrix renders. **Committed + PUSHED to `origin/main`** (`fc0fdbe` code + `3652168` docs, with W08).

### W08 — the hands-on wave, part 3: per-track Session mixer strips — SHIPPED (PUSHED)  (baseline `3652168`)
Wave 3 of the hands-on plan. Built the W06/W07 way: 5 source-verify investigators → one `src/ui/session/*` spine
agent → orchestrator (gate + tray + PeakMeter mode + build) → 4-dimension adversarial QC. Full record:
[devlog/wave-08-session-mixer.md](devlog/wave-08-session-mixer.md).
- **The strip** (`SessionMixerStrip.{h,cpp}`, new): a compact per-track strip under each Session column —
  maintainer-chosen control set **meter · horizontal fader · pan · M/S** (no sends). Modeled on `ChannelTray`
  (R1 `(edit,index)` identity re-resolve; `PeakMeter` `WeakReference` measurer; `forge::strip` styling reused;
  vol/pan via `EngineHelpers`, M/S via `track.setMute/setSolo`). Aux returns render **in-place** (tinted via
  `setIsReturn`; NO grid filtering — that would break ~9 absolute-index sites incl. the hot poll).
- **The band** (`SessionView` + `SessionLayout::mixerBandH=96`): the pinned scene column **rotated 90°** — a
  sibling of the viewport, synced to `-getViewPositionX()`, shrinking ONLY the viewport so `contentH` + the
  scene-column height are untouched. **No W07 scene-drift regression** (QC-confirmed byte-identical).
- **ChannelTray → Arrange-only** (`main.cpp`, 2 guards): the Session grid no longer drives the tray (the strips
  replace it); Arrange tray + Mix view untouched; `--selftest-tray` unaffected (drives `setTrack` directly).
- **Shared `PeakMeter` horizontal mode** (`common/PeakMeter.h`): a backward-compatible `setHorizontal(bool)` +
  paint branch (all four legacy callers render byte-identically).
- **QC (4 dimensions):** **NO blockers/majors.** Band/anti-drift, returns/tray/shell, and the PeakMeter change
  all CLEAN; the strip dimension found only defused latent traps. Per maintainer direction the surviving
  findings were **DOCUMENTED, not fixed** — the top one is the Ableton **master-strip** opportunity (shorten the
  scene column to the pad viewport + fill the empty bottom-right corner with a master strip). A **gate-ladder
  substring bug** was caught pre-ship: `--selftest-sessionmixer` ⊃ `--selftest-session` shadowed it in the
  ternary ladder (false `mode=session` PASS) — fixed by ordering longest-first; lesson: verify the `mode=` line.
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-TWO selftests PASS** (`--selftest-sessionmixer`:
  `bound/faderOk/panOk/muteOk/soloOk` all 1); the base `session` screenshot shows the band. **Committed +
  PUSHED to `origin/main`** (`0ad7abc` code + `1e1a798` docs, with W07, sanitize-clean).

### W09 — the hands-on wave, part 4: self-rendered instruments + an audible demo — SHIPPED (PUSHED)  (baseline `1a59973`)
Wave 4 of the hands-on plan — makes the app **audibly playable out of the box**. Built the established way:
4 source-verify feasibility investigators → a maintainer scope decision (HYBRID, no browsable library) → one
instrument-layer agent → orchestrator (demo builder / gate / first-launch hook / build) → 3-dimension QC. Full
record: [devlog/wave-09-instruments.md](devlog/wave-09-instruments.md).
- **The instrument layer** (`src/engine/dsp/InstrumentSamples.{h,cpp}` new + `PluginHost.{h,cpp}`): a procedural
  CC0 **piano one-shot** generator (8 inharmonic partials + strike transient, xorshift32-deterministic, mono
  44.1 kHz, temp-then-move into `%APPDATA%\Forge\library\piano.wav`, idempotent) + the **Sampler** registered as
  a built-in + `applyInstrumentPreset(track, {Kick|Bass|Piano})` (removes any head synth, then Kick/Bass = a
  programmed 4OSC, Piano = the Sampler loaded with the one-shot, mapped chromatically).
- **The demo** (`main.cpp`): `seedDemoNotes` writes per-instrument patterns (kick 4-on-floor · walking C-minor
  bass · Cm–Ab–Bb–Cm chord stabs); `populateDemoContent` applies the presets BEFORE creating clips (so
  `ensureDefaultInstrument` no-ops) + seeds notes + adds a Keys(2,0) hero cell so **scene 0** is a coherent
  kick+bass+piano groove; `launchScene(0)` for the screenshot.
- **First-launch welcome demo** (`main.cpp:206`): a brand-new user (fresh default project) opens into the
  playable demo — in-memory only, does not auto-play, File > New still gives empty, and it does not reappear on
  the 2nd launch. A meaningful launch-behavior change, flagged for the maintainer.
- **Gate `--selftest-demo`** (floor 22→23): the Kick preset is a 4OSC, the Piano preset is a real Sampler, the
  piano one-shot exists on disk, and a seeded clip holds notes.
- **QC (3 dimensions): NO blockers/majors — all CLEAN.** The instrument-layer finder refuted 10 candidate bugs
  against the engine source; the demo/first-launch finder refuted the preset-stacking + launch-behavior worries;
  hygiene/determinism/CC0 all confirmed. One LOW doc-drift fixed (SELFTEST scene 3→0). Non-blocking hardening
  notes documented: a render/ingestion gate leg (prove the Sampler *ingested* the one-shot), and `looksValid`
  size-vs-decodability.
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-THREE selftests PASS** (`--selftest-demo`:
  `kickIsSynth/pianoIsSampler/pianoFileExists/clipHasNotes` all 1, `noteCount=16`); the base `session`
  screenshot shows the note-seeded groove (Keys = a Sampler). **Committed + PUSHED to `origin/main`**
  (`573170c` code + docs, with W07/W08, sanitize-clean).

### W10 — the hands-on wave, part 5 (the last): the Session → Arrangement "Send to" bridge — SHIPPED (PUSHED)  (baseline `76d8f38`)
Wave 5 of the hands-on plan — the **final** one. An explicit, one-directional "Send to Arrangement" action; the
real answer to the maintainer's *"Session clip doesn't appear in Arrange"* note (intended behavior — Session ↔
Arrange stay separate, nothing auto-mirrors). Single-CLI wave (a tight spine across the shared/serial files):
3 source-verify agents → frozen design → 2 maintainer product calls → orchestrator build + the 24th gate →
5-dimension adversarial QC → 2 fixes. Full record: [devlog/wave-10-send-to-arrangement.md](devlog/wave-10-send-to-arrangement.md).
- **The seam** (`ProjectSession::sendSlotToArrangement`): reads the slot clip via the const `getClipSlot`, copies
  it onto the **same** track's linear timeline via Tracktion's own `insertClipWithState(state.createCopy(), …)`
  clone idiom (re-IDs + repositions in one call; carries the wave source / MIDI sequence faithfully — a **copy,
  not a move**, so the slot is untouched). **Appended at the end** of the track's arrange content
  (`getTotalRange().getEnd()`; 0 for an empty lane). Maintainer calls: **append-at-end · single-clip**.
- **Menu + refresh** (`SessionView` + `main.cpp`): a "Send to Arrangement" item on a filled slot → a new
  `onSendToArrangement` shell callback → seam + `sealUndoTransaction` + `save` + **`arrangeView.rebuild()`** (the
  load-bearing refresh — ArrangeView has no clip-add listener) + a status message.
- **QC (5 dimensions): 2 CONFIRMED fixed, 3 REFUTED clean.** ① **[HIGH]** the sent clip was **silent** in Arrange
  (`AudioTrack::playSlotClips` latches true on slot-launch and nothing in the engine clears it → arranger output
  gated off) — fixed by the seam flipping the flag false (the engine's Session→Arrange handoff). ② **[Medium,
  latent]** the copy inherited the slot's auto-tempo + full-length loop range → would **re-tile on an edge-drag**
  — fixed by normalizing to a plain one-shot (`disableLooping()` + `setAutoTempo(false)`; **not**
  `setLoopRangeBeats({})`, which re-asserts auto-tempo). REFUTED clean: wave-source & field fidelity,
  undo/lifetime, placement/refresh.
- **Gate `--selftest-sendarrange`** (floor 23→24): a MIDI leg (fidelity · source-intact · append · audibility ·
  non-looping · undo round-trip) + a **wave leg** (import a sine WAV → send → non-looping, non-auto-tempo
  WaveAudioClip whose source survived the copy) — both fixes proven headlessly.
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-FOUR selftests PASS**; the 10-state screenshot
  matrix renders. After W10 the hands-on plan is **complete**; the frontier build program is the next
  planning source. **Committed + PUSHED to `origin/main`** (`40eccaf` code + `ea3c7a3` docs, sanitize-clean).

### W11 — frontier program Wave 1: launcher expressiveness — SHIPPED (PUSHED)  (baseline `90449ce`)
The **first** wave of the frontier build program (the 10-wave plan a discovery swarm produced after W10). Per-clip
**follow actions** + **loop-toggle** + **launch modes** — the maintainer-flagged #1 gap, turning the Session grid
into a performable instrument. Built to a frozen source-verified spec: orchestrator serial spine + one
file-disjoint agent (Role B) + a 5-dimension adversarial QC. Full record:
[devlog/wave-11-launcher-expressiveness.md](devlog/wave-11-launcher-expressiveness.md).
- **Seams** (`ProjectSession`): `setFollowAction` / `getFollowAction` / `setFollowActionDuration` (defeat the
  engine FOLLOWACTIONS auto-plant footgun — always set the action type explicitly), `setSlotClipLooping` /
  `isSlotClipLooping`, `setLaunchMode` / `getLaunchMode` (int `forgeLaunchMode` on `clip->state`; absence reads
  Trigger), `isSlotActive` (playing OR queued). Follow actions are consumed at graph-build (`EditNodeBuilder` →
  `SlotControlNode`): zero per-tick work, R1-safe.
- **UI**: three submenus in `SessionView::handleSlotRightClicked` (Follow action / Loop / Launch mode); a shared
  `launchOrToggle` (mouse + Enter); `handleSlotReleased` (Gate stop-on-release); `ClipSlotComponent` gained an
  `onReleased` / `mouseUp` (Role B), forwarded via `TrackColumnComponent::onSlotReleased`.
- **Gates** `--selftest-followaction` (11 legs: footgun re-checks + the KEY `createFollowAction` functor proof +
  ValueTree round-trip + undo + loop-toggle) and `--selftest-launchmode` (7 legs). Floor **24 → 26**.
- **QC (5 dimensions):** follow-action / lifetime-R1 / loop-toggle **REFUTED clean**; **Trigger launch
  byte-identical**; 3 routing findings **fixed** — the Toggle queued-race (`isSlotActive` includes queued), Enter
  mode-aware, and the W10 send-to-arrange copy no longer carries launcher-only metadata onto the Arrange clip; the
  Gate quantise quick-click **documented** (immediate under free-trigger quant; immediate-launch is a follow-up).
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-SIX selftests PASS**; the FOLLOWACTIONS
  auto-plant footgun banked in CLAUDE.md. **Committed + PUSHED to `origin/main`** (`5c3738e` code + `0f9d5cc`
  docs, sanitize-clean).

### W12 — frontier program Wave 2: per-clip launch quantise override — SHIPPED (local)  (baseline `2a366e9`)
A filled Session slot can carry its **own** launch quantisation (a 1/16 hat fill vs a 1-bar bass) instead of only
the Edit-global launch quant. The engine's launch resolver already preferred a per-clip `LaunchQuantisation`, so
the wave added only the seams + UI + proof. Full record:
[devlog/wave-12-per-clip-launch-quantise.md](devlog/wave-12-per-clip-launch-quantise.md).
- **Seams** (`ProjectSession`): `setClipLaunchQuantisation` / `getClipLaunchQuantisation` /
  `clearClipLaunchQuantisation` / `clipInheritsGlobalLaunchQuantisation`, plus **`resolveEffectiveLaunchQType`** —
  a bridge into the file-local resolver so the gate asserts precedence through the *real* launch path, not a
  mirror. Engine seam `setUsesGlobalLaunchQuatisation` (verbatim typo, inverted: `false` enables the override);
  const readers never dirty the tree.
- **UI**: a "Launch quantise" submenu in `SessionView::handleSlotRightClicked` (Global-inherit + 23 `LaunchQType`
  values), dispatched inline like the W11 launcher submenus — no new callback, no `ClipSlotComponent` change.
- **Proof**: a `perClipLaunchQ` leg on **`--selftest-session`** (set an override distinct from global → assert the
  real resolver returns it → revert to inherit). **No new gate — floor stays 26.**
- **QC (6 dimensions):** override-semantics / R1-const-read / undo / menu / persistence / regression all **REFUTED
  clean** — verdict **ship, 0 defects** (the change is fully undoable via the engine's UndoManager binding).
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-SIX selftests PASS**. **PUSHED to `origin/main`**
  (`03f6efd` code + `c32b8f1` docs, in the W12–W14 stack through `9f5cc62`).

### W13 — frontier program Wave 3: grid clip primitives (duplicate → move/copy) — SHIPPED (local)  (baseline `c32b8f1`)
Slot→slot clip movement on the Session grid: right-click a filled slot → **Duplicate clip** / **Move to next
slot** (copy or move to the first empty slot below, auto-growing a row when full); **Ctrl+D** move /
**Ctrl+Shift+D** copy on the focused slot. Full record:
[devlog/wave-13-grid-clip-primitives.md](devlog/wave-13-grid-clip-primitives.md).
- **Seams** (`ProjectSession`): `copySlotClip` / `duplicateSlotClip` / `moveSlotClip` over one file-local
  `cloneClipIntoSlot` helper (`state.createCopy()` → fresh `EditItemID` → `te::insertClipWithState`).
  Replace-on-filled + slot normalization are engine-automatic; MOVE = copy-then-`clearSlot` in ONE undo
  transaction; the launcher metadata (follow-action / launch-mode / launch-Q) rides the clone.
- **UI**: two menu items + a `SessionView::keyPressed` Ctrl+D / Ctrl+Shift+D branch (the keyboard path lives in
  SessionView, not ClipSlotComponent). Same-track-below in the UI; the seams are cross-track (gate-exercised).
- **Gates** `--selftest-duplicate` + `--selftest-slotmove` (floor **26 → 28**): auto-grow, note-count identity,
  one-shot preservation, replace-on-filled, MOVE atomic undo, empty-source no-op.
- **QC (6 dimensions) — fix-then-ship:** the adversarial pass caught a MAJOR the verify swarm missed — the engine
  re-loops a freshly-inserted non-looping clip, so a duplicated **one-shot** came back looping (a W11 regression);
  `cloneClipIntoSlot` now re-asserts `disableLooping()`, gate-guarded. Five dimensions refuted clean (the
  `ensureScenes` history-wipe is pre-W3 inherited; the real-UI MOVE is atomic).
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-EIGHT selftests PASS**. **PUSHED to `origin/main`**
  (`2f804a2` code + `2edf78a` docs, in the W12–W14 stack through `9f5cc62`).

### W14 — frontier program Wave 4: MIDI quantise (piano-roll) — SHIPPED (local)  (baseline `2edf78a`)
The piano-roll gains destructive MIDI quantise: press **`q`** to snap the selection (or the whole clip when
nothing is selected) to the grid — note starts only, length preserved, one undoable step. Full record:
[devlog/wave-14-midi-quantise.md](devlog/wave-14-midi-quantise.md).
- **Seam** (NEW, header-only `src/engine/MidiEditHelpers.h`): `forge::midiedit::quantiseNoteStarts` over the
  engine's `QuantisationType` (a LOCAL instance — never the clip's persistent playback quantise); `setProportion`
  is the 0-100% strength (`roundBeatToNearest` folds it in — no hand-lerp); `setStartAndLength` preserves length.
  No CMakeLists edit (header-only; `target_sources` is explicit `.cpp`-only). Corrected grid mapping:
  `gridBeats 0.25 → "1/4"` (a fraction of a BEAT), NOT "1/16".
- **UI**: `PianoRollView` 'q' (no modifier — Ctrl+Q Exit propagates) → `quantiseSelectionOrClip`; `layoutNotes`
  keeps the selection. No raw `te::` quantise in the view.
- **Gate** `--selftest-quantise` (floor **28 → 29**): snap-to-grid, length preserved, **50%-strength
  interpolation** (0.1→0.05), undo revert.
- **QC (6 dimensions) — ship:** one nit fixed (the Ctrl+Q swallow); grid math / length / stale-pointer safety /
  single-transaction undo / no playback-quantise mutation all refuted clean.
- **Verified:** clean MSVC Debug build (0 warnings); **all TWENTY-NINE selftests PASS**. **PUSHED to `origin/main`**
  (`52b6e66` code + `9f5cc62` docs). Next: frontier Wave 5 (scene lifecycle: rename → delete → reorder).

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
│   ├── AutomationHelpers.h        header-only automation seam over VolumeAndPanPlugin curves — add/move/remove/clear points, every mutator commits the stream (W03)
│   ├── MidiClockSync.h            MIDI-clock-out seam (setSendClockToAll/isSendingClockAny over the device manager; W03)
│   ├── MidiClockProbe.h           selftest-only MidiOutputDevice subclass capturing wire bytes for --selftest-sync (W03)
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
    ├── arrange/ArrangeView        ruler + snap-division selector, lanes (M/S/R/A + colour), clips (waveform, drag, snap), selection (clip follows to track), playhead
    ├── arrange/AutomationLane     collapsible per-track volume/pan automation sub-lane (point add/drag/delete, curve-change listener; W03)
    ├── transport/LcdDisplay       the control-bar transport LCD: bars|beats / tempo / key·sig + the beat-locked count-in digit face (W04a)
    ├── transport/LcdModel.h       pure display model behind the LCD — headlessly gated (W04a)
    ├── tray/ChannelTray           left-sidebar selected-track channel strip: pan, A/B sends, inserts, fader, M/S (W04a)
    ├── menu/ForgeMenuModel        the top menu-bar model (File/Edit/View/Transport/Help, shortcut labels, live ticks; W04a)
    ├── common/                    shared widgets: PeakMeter.h (WeakReference-sourced level meter, W04b) + StripWidgets.h (tray↔mixer fader/knob/send styling, W05)
    ├── popout/PopoutWindow        tear-off desktop windows for the mixer / piano-roll (reparent + deferred close; W04b)
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
- [x] **W05 — global Undo/Redo + the polish sweep — DONE** (this session). Edit ▸ Undo/Redo + shortcuts
  over the Edit's UndoManager (`--selftest-undo`), scene-column polish, the strip-widget extraction, the
  empty-centre hint, popout placement persistence. **Remaining:** the OWED QC dimensions (undo-correctness +
  shell-integration — the wave was limit-interrupted); a matching detached-clip guard for DetailView; a
  piano-roll playhead (new feature); a window-SIZE state matrix.
  Details: [devlog/wave-05-undo.md](devlog/wave-05-undo.md).
- [x] **W04b — the UX wave, part 2 — DONE** (prior wave, same session). Tear-off mixer/piano-roll windows
  (`--selftest-popout`), animated B/E slide-outs, the timecode LCD zone, the shared PeakMeter + tray meter,
  Session tray-follow, the playhead accent move. **Remaining (W04c/later):** ~~an empty-centre hint, popout
  position persistence, scene layout polish, further strip-widget extraction~~ (all closed in W05); a
  piano-roll playhead; a window-SIZE state matrix. Details: [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md).
- [x] **W04a — the UX wave, part 1 — DONE** (prior wave, same session). Transport LCD (`--selftest-lcd`), channel tray
  (`--selftest-tray`), menu bar (`--selftest-menu`, incl. the dead-Rec-button fix + the control-bar
  de-clutter), Session sequence lighting + the semantic accent switch (amber = selection only), persisted
  section sizes, the 8-state screenshot matrix. **Remaining (W04b):** popouts/tear-offs, animated slide-outs,
  the timecode LCD zone, a window-level screenshot for the menu bar, the tray meter, Session-grid tray
  follow, scene layout polish, the accent sweep across remaining views.
  Details: [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md).
- [x] **W03 — automation lanes + MIDI-clock out + async LUFS + live refresh — DONE** (prior session).
  Volume/pan automation lanes in Arrange (`--selftest-automation`); MIDI-clock out with a wire-byte capture
  gate (`--selftest-sync`); LUFS analysis on the render worker with an abort guard (extended
  `--selftest-lufs`); live cross-surface refresh (`--selftest-livesync`); INTERFACE.md rewritten
  Session-first. QC: 9 confirmed findings fixed incl. a latent master-meter UAF (now
  `WeakReference`-sourced meters) and a ReturnStrip 28 Hz UAF. **Remaining:** Ableton Link (vendoring
  decision); aux-send knobs/return inserts not live-synced; the W04 UX wave (menu bar, popouts, slide-outs,
  section scaling, sequence lighting, tempo indicators, semantic accents, state-matrix screenshots).
  Details: [devlog/wave-03-features.md](devlog/wave-03-features.md).
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
| 3 — MIDI & editing | MIDI tracks + piano roll; built-in synth; non-destructive audio editing; automation | ⏳ (**MIDI MVP + W6 polish + W7 record built** — draw a clip + hear it via 4OSC, polymorphic `ClipComponent`, piano-roll with velocity/multi-select/copy-paste → `docs/devlog/midi-build.md`; **W7 MIDI record into Session slots done** (`160f6cc`, verdict A, `--selftest-midi` PASS → `docs/devlog/midi-record-design.md`); **MIDI-learn param mapping (W01 P2) + its HW-routing focused-Edit `ForgeUIBehaviour` (W02 item 2a, `--selftest-midiinput` PASS)** done; **volume/pan automation lanes (W03, `--selftest-automation` PASS)** done — plugin-param lanes to do) |
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
   drive is a real-hardware smoke item (a virtual device has no `controllerParser`). ✅ **MIDI-clock OUT DONE
   (W03, `--selftest-sync`)**. Still to do: **Ableton Link** — the engine wrapper is compiled out and the Link
   library is NOT vendored; enabling it is a dependency + license decision, deferred.
4. **Done this slice (now reusable inside slots/scenes):** ✅ recording verified on real hardware; ✅ MIDI
   MVP + W6 piano-roll polish (draw + hear, velocity / multi-select / copy-paste); ✅ **W7 MIDI record into
   Session slots**. A **manual GUI smoke pass** of the draw→play + slot-record path is still worth doing.
5. **Carried-over polish.** ✅ buses/sends + async export + markers (W01), ✅ **offline LUFS on export (W02
   item 4, moved onto the render worker in W03)**, ✅ **volume/pan automation lanes + live cross-surface
   refresh (W03)**; still to do: plugin-param automation lanes; comping; an optional **live short-term LUFS
   meter** (would require forking the engine for a post-fader sample tap — no non-mutating tap exists today);
   off-thread record-input open. ✅ **The W04 UX wave (a/b) + the W05 undo/polish wave are DONE** (menu bar,
   LCD, tray, popouts/slide-outs, sequence lighting, semantic accents, scene polish, global Undo/Redo —
   see the wave devlogs). **Next: the owed W05 QC dimensions first** (undo-correctness + shell-integration),
   then the next feature wave.

---

## 6. How to build & run

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"            # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest # headless verify
```
