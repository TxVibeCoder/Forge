# Forge — Project Status & Roadmap

*Living status document. Last updated 2026-06-29.*
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
| **Code size** | ~1,570 lines of Forge source (engine/JUCE excluded) across 16 files |

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
docs/   ARCHITECTURE · FEATURE_CATALOG · INTERFACE · STATUS
tests/  SELFTEST.md
```

---

## 4. Pending action items

### Rough edges / near-term
- [ ] **Recording verification (blocker).** Wired and correct, but unverified end-to-end: on
  the dev machine no input device reaches the engine (`inputDeviceCount=0`,
  `deviceTypesInputs` empty) even with a mic present. Needs: investigate Tracktion wave-in
  enumeration vs. the JUCE device-type/input pairing (and Windows mic-privacy), then confirm
  a take captures once an input is selected via **Audio**.
- [ ] **Device-override.** `engine.getDeviceManager().initialise()` at startup selects the
  default device, overriding the user's saved output (observed Bose → Realtek). Fix: open
  inputs without resetting the chosen output (init from saved settings / request inputs only).
- [ ] **Draggable resizer bars.** Regions collapse/expand via buttons but don't drag-resize
  yet. Add nested `StretchableLayoutManager` + `StretchableLayoutResizerBar` per INTERFACE §layout.
- [ ] **Keyboard shortcuts.** `B`/`I`/`E` region toggles and `F9`/`F11` view-switch are
  planned but not bound — currently buttons only.

### Deferred from the Phase 1 review (real, low-priority)
- [ ] Import / Open / Save-As callbacks capture raw `this` → guard with
  `juce::Component::SafePointer` (UAF window if the app closes during an async dialog).
- [ ] `openDialog`/`saveAsDialog` share one `fileChooser` member → per-dialog members.
- [ ] `ProjectSession::saveAs` sets `editFile` before the write succeeds; `saveAsDialog`
  ignores the returned bool → assign on success only.
- [ ] `TimelineView::xToTime` guards `width<=0` but not `span<=0` → add when zoom/scroll lands.
- [ ] Waveform draw assumes non-looped clips → handle looped/time-stretched clips later.
- [ ] `formatTimecode` mixes signed/abs ms (cosmetic; only matters for negative positions).

### Build / hygiene
- [ ] Add `.gitattributes` to normalize line endings (silences the CRLF/LF warnings).
- [ ] macOS build not yet attempted (only Windows verified). VST3 + AU hosting is macOS-relevant.
- [ ] `--selftest-record` can't self-verify without a real input; consider a synthetic-input path.

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
| 1 — The spine | Record & play a track (load/save, import, transport, playhead, record) | ✅ done (recording input-gated) |
| 2 — Mixer & plugins | Volume/pan/mute/solo, buses, sends; **VST3/AU hosting**; built-in FX | ⏳ next |
| 3 — MIDI & editing | MIDI tracks + piano roll; built-in synth; non-destructive audio editing; automation | ⏳ |
| 4 — Polish | Comping, metering (LUFS), export (WAV/MP3/stems), markers, snap | ⏳ |
| 5 — Deferred | Sidechain, warp, controller mapping, advanced routing, video | ⏳ |

### Interface build order (from INTERFACE.md)
| Phase | Items | State |
|---|---|---|
| 1 — Shell refactor | ForgeShell, Control Bar, LookAndFeel, view-slot, collapsible regions, tooltips | ✅ done |
| 2 — Arrange polish | per-lane mute/solo/arm + color, right-click context menus, Info/Help box, bars|beats ruler, snap, selection state | ⏳ next |
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
