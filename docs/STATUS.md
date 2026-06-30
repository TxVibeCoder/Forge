# Forge — Project Status & Roadmap

*Living status document. Last updated 2026-06-30 (recording verified end-to-end on real hardware +
`--selftest-record` harness fix; MIDI tracks + piano-roll design complete & source-verified).*
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
| **Identity** | **Recording + arrangement** first (tracking, comping, MIDI, mixing). Not clip-launch. |
| **UI direction** | Ableton's *look + interaction* on an arrangement-first DAW; dark + **warm amber** accent; single-window. **Session clip-grid deferred** (seam reserved). |
| **Code size** | ~6,110 lines of Forge source (engine/JUCE excluded) across 27 files |

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

### Verified by `--selftest` (current)
`mode=playback`: device open · `importedClip=1` · `numClipComponents=1` · **result=PASS** when the
audio device is healthy (`playing=1`). *Caveat:* the build is the definitive signal. The old
input-negotiation startup balloon (25–77 s when the default device changed) is **fixed** — startup is
now output-only (see "Startup latency hardening" below), and measured headless startup dropped ~17 s
→ ~8 s. A genuinely contended OUTPUT device can still be slow to open or return `playing=0`; that is
environmental (affects the committed baseline identically), not a code regression.
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
├── services/
│   ├── files/ProjectSession       owns the Edit; create/open/save/import; isModified
│   └── export/Exporter            render the Edit → 24-bit WAV (whole-edit mixdown + per-track stems)
├── engine/
│   ├── EngineHelpers.h            track insert, clip load, file chooser, transport toggles, lazy record-input open, track vol/pan
│   ├── RecordController           recording recipe (enable/arm/record) + disarm + isTrackArmed
│   ├── PluginHost                 list/add/remove built-in + external plugins on a track
│   └── PluginScanner              VST3/AU scan dialog → engine known-plugin list (auto-persist)
└── ui/
    ├── ForgeLookAndFeel.h         dark amber theme (colour IDs)
    ├── ControlBar                 merged top strip + view-switch + region toggles + Plugins/Export menu
    ├── transport/TransportBar     play/stop/rec/loop + timecode/bars|beats
    ├── arrange/ArrangeView        ruler + snap-division selector, lanes (M/S/R + colour), clips (waveform, drag, snap), selection, playhead
    ├── mixer/MixerView            channel strips (fader/pan/M/S), insert slots (bypass + reorder), master strip + post-fader meter
    ├── plugins/PluginWindow       floating plugin editor windows (native / generated)
    ├── browser/BrowserView        left-region file browser (double-click → import)
    └── detail/DetailView          bottom-drawer clip inspector (name/gain/fades/waveform)
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
- [ ] **MIDI is not yet supported** — Forge is audio-only (`WaveAudioClip` only). MIDI tracks
  + piano-roll are a later add (gates INTERFACE Phase 4's piano-roll).
- [ ] ASIO (needs Steinberg SDK + `JUCE_ASIO=1`); MP3 import (`JUCE_USE_MP3AUDIOFORMAT=1`).
- [ ] `rtcheck` RT-safety tool is macOS/Linux only — N/A on the Windows dev box.
- [ ] AGPLv3 obligations when distributing builds (share source — trivial for this repo).

---

## 5. Full plan

### Locked decisions
1. Build **on** Tracktion Engine (don't hand-roll the audio graph).
2. **Audio thread is sacred** · the Edit is the single source of truth · the UI observes.
3. UI: **Ableton look/feel, arrangement-first**; Session deferred (seam reserved via
   `ViewMode`); mixer = a **full-window view-switch**; **warm amber** accent on dark.

### Engine roadmap (from ARCHITECTURE.md §11)
| Phase | Goal | State |
|---|---|---|
| 0 — Toolchain | Build + first sound | ✅ done |
| 1 — The spine | Record & play a track (load/save, import, transport, playhead, record) | ✅ done (device-override fixed; **recording verified end-to-end on real hardware**) |
| 2 — Mixer & plugins | Volume/pan/mute/solo, buses, sends; **VST3/AU hosting**; built-in FX | ✅ mostly (strips/inserts/meters/master + insert bypass/reorder + plugin hosting + external scan UI + floating windows done; buses/sends to do) |
| 3 — MIDI & editing | MIDI tracks + piano roll; built-in synth; non-destructive audio editing; automation | ⏳ (clip move/snap done; **MIDI design complete + source-verified → `docs/devlog/midi-design.md`**; build next; automation to do) |
| 4 — Polish | Comping, metering (LUFS), export (WAV/MP3/stems), markers, snap | ⏳ (peak meters + WAV mixdown + per-track stems + snap-division done; LUFS/markers/comping to do) |
| 5 — Deferred | Sidechain, warp, controller mapping, advanced routing, video | ⏳ |

### Interface build order (from INTERFACE.md)
| Phase | Items | State |
|---|---|---|
| 1 — Shell refactor | ForgeShell, Control Bar, LookAndFeel, view-slot, collapsible regions, tooltips | ✅ done |
| 2 — Arrange polish | per-lane mute/solo/arm + color, context menus, bars\|beats ruler, selection, clip drag + snap + info hint ✅; snap-division selector (Off/Bar/½/¼/⅛/1⁄16, denominator-aware) ✅ | ✅ done |
| 3 — Browser/Inspector | left-column file Browser (double-click import) ✅; clip Inspector (props) ✅; drag-onto-track + value popups to do | ✅ mostly |
| 4 — Detail Drawer | clip inspector (name/gain/fades/waveform) ✅; piano-roll → automation later | ✅ (audio clip stage) |
| 5 — Mixer + devices + plugins | Mixer view-switch ✅; channel strips + inserts + master + meters ✅; floating plugin windows ✅; device chain reorder + per-insert bypass ✅ | ✅ done |
| 6 — Config + delivery | tabbed Preferences (folds in Audio settings); Export/Render | ⏳ |
| 7 — Power-user + Session | tear-off panels, saved layouts; SessionView when wanted | ⏳ |

### How they interleave (recommended path)
Phases 0–4 + startup-latency hardening are done: the spine, arrange surface (incl. snap-division
grid), mixer (incl. insert bypass/reorder + post-fader master meter), plugin hosting + scan UI,
browser, inspector, export (mixdown + stems), and output-only startup with lazy record-input open.
Practical next sequence:
1. ✅ **Recording verification on real hardware — DONE.** `--selftest-record` captures a real take
   (`result=PASS`, non-zero peak); the prior FAIL was a harness bug, now fixed (event-driven harness).
   Remaining refinement: default-mic *selection* (lazy-open keeps the existing output, so the captured
   endpoint is the default pairing, not necessarily the listed mic) — see device-recording.md.
2. **MIDI tracks + piano-roll** (engine Phase 3) — the big remaining capability; gates a synth.
   **Design is done and source-verified** (`docs/devlog/midi-design.md`): a file-disjoint 7-wave plan
   (W1 instrument seam · W2 MIDI-clip create · W3 clip-component split · W4 piano-roll · W5 integrate
   = audible-MIDI MVP; W6 velocity/polish; W7 MIDI-input record). Build is the next effort.
3. **Automation** (volume/pan/plugin-param lanes) + **buses/sends** in the mixer.
4. **Polish** — async export + progress; LUFS metering; markers; comping; off-thread record-input
   open (so even the first arm never briefly blocks the message thread).

---

## 6. How to build & run

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"            # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest # headless verify
```
