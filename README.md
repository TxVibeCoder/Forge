# Forge

A native desktop DAW (Digital Audio Workstation), built on **JUCE** + **Tracktion Engine**
in **C++20**. Targets Windows + macOS. Non-commercial; open-source under **AGPLv3**
(JUCE AGPLv3 + Tracktion Engine GPLv3 ⇒ Forge is AGPLv3).

**Forge is a sample / scene-based DAW** — an Ableton-style **Session clip grid** (tracks ×
scenes of launchable clips) as the *primary* surface, meant to be played from grid controllers
(Novation Launchpad, Akai APC40 mkII). A linear **Arrange** timeline is a secondary view.

> **Read the product direction first → [`docs/DIRECTION.md`](docs/DIRECTION.md).** It is the
> authoritative brief; the older docs are being realigned to it.

## Status

The **Session clip-launch grid** — the primary, scene-based surface from DIRECTION.md — is **built,
QC'd, and verified**: an Ableton-style tracks × scenes grid on Tracktion's `ClipSlot` / `Scene` /
`LaunchHandle`, playable with mouse + keyboard, with audible bar-quantised launch. It rides on top of
the arrangement-first foundation (Phases 0–4 + a MIDI MVP + piano-roll), whose clips / instruments /
piano-roll / mixer now live inside slots and scenes. Building clean; **all four headless self-tests
PASS** on Windows. Full roadmap → [`docs/STATUS.md`](docs/STATUS.md).

**What works today:**
- **Session clip grid (the primary view)** — tracks × 16 scenes of launchable clips; single-click
  launches, right-click "Edit clip", double-click opens, keyboard arrow/Enter launch; **audible,
  bar-quantised** launch; a pinned scene-launch column. The shipped clips / 4OSC / piano-roll / mixer
  ride inside its slots and scenes.
- **Project** save/load — a real `.tracktionedit` on disk (create / open / save / save-as).
- **Audio** import (WAV/AIFF/FLAC/OGG) onto tracks; a timeline with **waveform thumbnails**,
  a moving **playhead** (drag to scrub), clip **drag-to-move** + selectable **snap grid**.
- **Transport** — play / stop / record / loop + timecode and bars|beats.
- **Recording** — the verified Tracktion recipe; **verified end-to-end on real hardware**
  (`--selftest-record` captures a real take). Output-only startup; the capture input opens
  lazily on first arm.
- **MIDI** — MIDI clips on any track, **born audible** via a default **4OSC** synth at the head
  of the chain; a **piano-roll** editor (draw / move / resize / delete, **velocity lane**,
  **multi-select**, **copy/paste**). Draw a clip and hear it.
- **Mixer** — channel strips (dB fader, pan, mute/solo, colour), per-track **plugin inserts**
  (bypass + reorder), a master strip with a post-fader meter.
- **Plugins** — built-in effects + **VST3/AU** scanning & hosting, with floating editor windows.
- **Browser** (file tree, double-click to import) and a **clip Inspector** (name/gain/fades/waveform).
- **Export** — WAV mixdown + per-track **stems**.

**Coming next (per DIRECTION.md):** a device-agnostic **control-surface layer** so real grid
controllers (Launchpad, APC40 mkII) drive the grid — a "one day" hardware goal; the grid is fully
playable with mouse + keyboard without it, and the on-screen pad model already emits the LED encoding
a driver would push. Plus richer MIDI input (note-record into clips, MIDI-learn, MIDI-clock / Ableton
Link), and a decision on the 16-scene-rows-vs-window layout (scroll vs. shorter pads).

A to-scale **UI mockup set** of the envisioned product lives in [`mockups/`](mockups/) (open
`mockups/preview/forge-ui-storyboard.png`).

## Building (Windows)

Requires **Visual Studio 2022 Build Tools** (MSVC v143, C++20) and **CMake ≥ 3.22**.

```powershell
git submodule update --init --recursive        # one-time: pull the engine + JUCE
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"
```

### Headless self-tests
- `Forge --selftest` — imports a generated tone, plays it, verifies device + clip + arrange +
  playhead, writes a PASS/FAIL report to `%TEMP%\forge_phase0_selftest.log`, quits.
- `Forge --selftest-record` — opens the input lazily, arms + records ~1 s, verifies a real take
  was captured to disk (reports a device diagnostic if the box exposes no capture endpoint).

See [`tests/SELFTEST.md`](tests/SELFTEST.md) for the full field contract.

## Layout

```
forge/
├── CMakeLists.txt              # top-level build
├── libs/tracktion_engine/      # git submodule, pinned to v3.2.0 (bundles JUCE)
├── src/
│   ├── main.cpp                # ForgeApplication (owns the Engine) + MainComponent (shell)
│   ├── services/
│   │   ├── files/              # ProjectSession — owns the Edit; create/open/save/import; createMidiClip
│   │   └── export/             # Exporter — WAV mixdown + per-track stems
│   ├── engine/                 # EngineHelpers · RecordController · PluginHost (incl. 4OSC seam) · PluginScanner
│   └── ui/
│       ├── ForgeLookAndFeel.h  # dark amber theme (all colours via colour IDs)
│       ├── ControlBar · transport/ · arrange/ · mixer/ · plugins/ · browser/ · detail/
│       ├── pianoroll/          # PianoRollView + MidiNoteComponent + VelocityLane
│       └── session/            # PRIMARY view: SessionView clip grid + Track/Scene columns + ClipSlot pad
├── docs/                       # DIRECTION (the brief) · STATUS · ARCHITECTURE · INTERFACE · FEATURE_CATALOG · devlog/
├── mockups/                    # to-scale DXF/CAD UI mockups (Session-first) + generator
└── tests/                      # SELFTEST.md — the --selftest field contract
```

## Toolchain notes
- C++20 is mandatory (Tracktion Engine); MSVC v143 / VS2022 required (v142 / VS2019 too old).
- Early phases use **WASAPI** (no extra SDK). ASIO is a later add (Steinberg SDK + `JUCE_ASIO=1`).
- `rtcheck` (RT-safety checker) is macOS/Linux only — not available on Windows.
- A running `Forge.exe` holds the build output (`LNK1168`) and the WASAPI device — stop it
  (`Get-Process Forge | Stop-Process -Force`) before rebuilding or runtime-testing.

## Known limitations
- **macOS not yet built** (only Windows verified). VST3 + AU hosting is macOS-relevant.
- **Interactive UI not headlessly verified** — the full shell builds and constructs in the
  selftest, but menus / drag / the MIDI draw→play path need a manual pass (the dev-built window
  can't be GUI-driven headlessly).
- Export (mixdown + stems) currently blocks the message thread (fine for short edits).
- Beat-based looped-clip waveform rendering and timeline zoom/scroll are later adds.
