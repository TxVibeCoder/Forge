# Forge — Project Status & Roadmap

*Living status document. Last updated 2026-06-29 (Phase 2 fan-out integrated; uncommitted).*
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
| **Code size** | ~2,660 lines of Forge source (engine/JUCE excluded) across 16 files |

---

## 2. Accomplished

Seven green commits, each compiling and passing the headless `--selftest`:

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
  wave inputs → `ensureContextAllocated` → `setTarget`/arm → `record`/`stop`). *Wired but
  input-gated — see §4.*
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
  channels while preserving the saved output device; the playback selftest now reports
  `device=Bose QC35 II` (saved output survives startup, no Bose→Realtek stomp).
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

### Verified by `--selftest` (current)
`mode=playback`: device open · `editLoaded=0` (fresh) · `importedClip=1` ·
`numClipComponents=1` · `playing=1` · **result=PASS**.
`mode=record`: `inputDeviceCount=0` → **FAIL** (honest: no input device available).

---

## 3. Code map

```
src/
├── main.cpp                       ForgeApplication (owns Engine + LookAndFeel) + MainComponent (the shell)
├── services/files/ProjectSession  owns the Edit; create/open/save/import
├── engine/
│   ├── EngineHelpers.h            track insert, clip load, file chooser, transport toggles, audio settings
│   └── RecordController           the recording recipe (enable/arm/record)
└── ui/
    ├── ForgeLookAndFeel.h         dark amber theme (colour IDs)
    ├── ControlBar                 merged top strip + view-switch + region toggles
    ├── transport/TransportBar     play/stop/rec/loop + timecode/bars|beats
    └── arrange/ArrangeView        TimelineView, TrackLane, AudioClip (waveform), Playhead
docs/   ARCHITECTURE · FEATURE_CATALOG · INTERFACE · STATUS · devlog/ (per-area Phase 2 writeups)
tests/  SELFTEST.md
```

---

## 4. Pending action items

### Rough edges / near-term
- [ ] **Recording verification (blocker, partially advanced).** Real-hardware capture is still
  unverified end-to-end: this dev box exposes no capture endpoint (`inputDeviceCount=0`,
  `deviceTypesInputs` empty), so a take can only be confirmed once an input is picked via
  **Audio** (steps in `docs/devlog/device-recording.md`). Root cause was diagnosed: the saved
  device opened output-only (empty input name), now addressed by `initialiseAudioForRecording`.
- [ ] **Synthetic-input record self-test (new, future work).** `installSyntheticInputForSelftest`
  exists (hosted `HostedAudioDeviceInterface`) but wiring it into `--selftest-record` did not yet
  land: with just-in-time install the hosted input arms and recording starts, but the transport
  doesn't advance under synthetic `processBlock` driving (`posAtFinish=0`, no take captured). Full
  investigation + next steps in `docs/devlog/integration.md`. Wiring was reverted; helper kept.
- [x] **Device-override.** FIXED — `initialiseAudioForRecording()` preserves the saved output
  (verified: playback selftest keeps Bose). The startup `initialise()` stomp is gone.
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
| 1 — The spine | Record & play a track (load/save, import, transport, playhead, record) | ✅ done (device-override fixed; recording still input-gated for real HW) |
| 2 — Mixer & plugins | Volume/pan/mute/solo, buses, sends; **VST3/AU hosting**; built-in FX | ⏳ next |
| 3 — MIDI & editing | MIDI tracks + piano roll; built-in synth; non-destructive audio editing; automation | ⏳ |
| 4 — Polish | Comping, metering (LUFS), export (WAV/MP3/stems), markers, snap | ⏳ |
| 5 — Deferred | Sidechain, warp, controller mapping, advanced routing, video | ⏳ |

### Interface build order (from INTERFACE.md)
| Phase | Items | State |
|---|---|---|
| 1 — Shell refactor | ForgeShell, Control Bar, LookAndFeel, view-slot, collapsible regions, tooltips | ✅ done |
| 2 — Arrange polish | per-lane mute/solo/arm + color, context menus, bars\|beats ruler, selection state ✅; **snap + Info/Help box** still to do | ✅ mostly |
| 3 — Browser/Inspector | left column: Browse (drag-onto-track) + Inspect (selection props); value popups; CallOutBox clusters | ⏳ |
| 4 — Detail Drawer | audio clip editor → piano-roll → automation; color-swatch palette | ⏳ |
| 5 — Mixer + devices + plugins | Mixer view-switch; device chain; floating VST/AU windows | ⏳ |
| 6 — Config + delivery | tabbed Preferences (folds in Audio settings); Export/Render | ⏳ |
| 7 — Power-user + Session | tear-off panels, saved layouts; SessionView when wanted | ⏳ |

### How they interleave (recommended path)
The interface and engine roadmaps are two views of the same work. Practical next sequence:
1. **Interface Phase 2 (arrange polish)** + engine selection/track plumbing — makes the
   arrange view a real working surface (per-lane controls, context menus, ruler).
2. **Device-override fix** + finish **recording verification** (small, unblocks tracking).
3. **Mixer view** (interface Phase 5 / engine Phase 2) — channel strips over the same Edit.
4. **Plugin hosting** (VST3/AU) — the headline capability; floating plugin windows + scan.
5. **Detail Drawer** content (audio editor first), then **MIDI + piano-roll**.

---

## 6. How to build & run

```powershell
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"            # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest # headless verify
```
