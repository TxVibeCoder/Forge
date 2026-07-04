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
piano-roll / mixer now live inside slots and scenes. On top of the grid, later waves added the engine
seams (MIDI-learn + HW routing, a Forge-native control-surface driver, offline LUFS, volume/pan
automation, MIDI-clock out, live cross-surface refresh) and a full **UX layer** (a traditional menu
bar, a GarageBand-style transport LCD, a channel tray, tear-off windows, animated slide-outs, sequence
lighting + a semantic accent vocabulary, and **global Undo/Redo**). Building clean; **all sixteen
headless self-tests PASS** on Windows. Full roadmap → [`docs/STATUS.md`](docs/STATUS.md).

**What works today:**
- **Session clip grid (the primary view)** — tracks × 16 scenes of launchable clips; single-click
  launches, right-click "Edit clip", double-click opens, keyboard arrow/Enter launch; **audible,
  bar-quantised** launch; a pinned scene-launch column; **Ableton-style vertical scroll** (fixed-height
  pads, all 16 scene rows reachable, the scene column tracks the pads). The shipped clips / 4OSC /
  piano-roll / mixer ride inside its slots and scenes.
- **Project** save/load — a real `.tracktionedit` on disk (create / open / save / save-as).
- **Audio** import (WAV/AIFF/FLAC/OGG) onto tracks; a timeline with **waveform thumbnails**,
  a moving **playhead** (drag to scrub), clip **drag-to-move** + selectable **snap grid**.
- **Transport** — play / stop / record / loop + timecode and bars|beats.
- **Recording** — the verified Tracktion recipe; **verified end-to-end on real hardware**
  (`--selftest-record` captures a real take). Output-only startup; the capture input opens
  lazily on first arm.
- **MIDI** — MIDI clips on any track, **born audible** via the engine's built-in **4OSC** synth at the head
  of the chain; a **piano-roll** editor (draw / move / resize / delete, **velocity lane**,
  **multi-select**, **copy/paste**). Draw a clip and hear it. **Record MIDI straight into a Session
  slot** — arm a track, then **Ctrl+Enter** (or right-click ▸ "Record into slot") on an empty pad
  captures a live loop into it as a born-audible `MidiClip`. **MIDI-learn** (**Ctrl+L**) binds a CC
  to a track ▸ plugin ▸ param over Tracktion's native mappings.
- **Mixer** — channel strips (dB fader, pan, mute/solo, colour), per-track **plugin inserts**
  (bypass + reorder), per-track **A/B aux sends** feeding **aux-return strips**, a master strip with
  a post-fader meter.
- **Plugins** — built-in effects + **VST3/AU** scanning & hosting, with floating editor windows.
- **Browser** (file tree, double-click to import) and a **clip Inspector** (name/gain/fades/waveform).
- **Metronome** — an engine **click** with a native **count-in** (TransportBar toggle + count-in selector).
- **Markers** — a timeline marker strip: add / move / rename / delete cue points, click to jump the transport.
- **Export** — WAV mixdown + per-track **stems**, now **async / off the message thread** with a
  progress + cancel panel (mixdown and stems both).
- **Automation** — collapsible per-track **volume/pan automation lanes** in Arrange (an **A** toggle
  beside M/S/R; click-add / drag / right-click-delete points; live repaint on external curve edits;
  persisted in the `.tracktionedit`).
- **MIDI-clock out** — a TransportBar **Clock** toggle streams 24-PPQN MIDI clock (SPP + start/stop) to a
  selected output. **Metering** — offline **BS.1770-4 integrated LUFS** measured on the export render
  (shown in the export-done status strip).
- **Control surface (built, inert without hardware)** — a device-agnostic grid-controller driver + a
  Novation **Launchpad** driver (to the published MIDI spec) that drive the grid directly and push LED
  feedback from the on-screen pad model; the byte-mapping still needs a real device to confirm.
- **UX layer** — a traditional **menu bar** (File/Edit/View/Transport/Help with shortcut labels), a
  GarageBand-style **transport LCD** (bars\|beats / tempo / key·sig + an animated count-in digit), a
  left-sidebar **channel tray** (the selected track's strip inside Arrange), **tear-off windows** for the
  mixer + piano-roll, animated **slide-out** drawers, **sequence lighting** (playing pads pulse green on
  the beat), and a small **semantic accent vocabulary** (amber = selection, green = playing/queued,
  red = recording, one clock colour for time/tempo).
- **Undo / Redo** — global **Edit ▸ Undo/Redo** (Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y) over the Edit's own
  UndoManager, with live menu enablement and a cross-surface refresh after every step.
- **Logging + error handling** — an app-wide logger (`src/core/Log.*`) with a crash handler, level macros,
  and a rolling file sink at `%APPDATA%\Forge\logs\forge.log` (plus stderr); logging fallible seams as you
  build them is a standing build principle ([`docs/LOGGING.md`](docs/LOGGING.md)).

**Coming next (per DIRECTION.md):** connect the **control-surface layer** to *real* grid hardware —
the device-agnostic driver + the Launchpad driver are built and inert without a device, so the remaining
work is the on-hardware byte-mapping/LED smoke test (needs a controller on the desk) plus an **APC40 mkII**
driver. Plus **Ableton Link** sync (the engine wrapper is compiled out and the library isn't vendored — a
dependency/license decision; MIDI-clock out already ships). *(The 16-scene-rows-vs-window layout question is
settled — vertical scroll shipped; MIDI-learn, note-record-into-clips, automation, and the whole UX layer
have shipped — see "What works today".)*

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

The verification floor is **sixteen** headless PASS/FAIL gates (each writes a report to
`%TEMP%\forge_phase0_selftest.log` and quits), plus `--screenshot` (renders a 9-state UI matrix incl. the
window-level `shell_window` to `%TEMP%\forge_shot_*.png` — not a gate). A representative few:

- `Forge --selftest` — imports a generated tone, plays it, verifies device + clip + arrange + playhead.
- `Forge --selftest-record` — opens the input lazily, arms + records ~1 s, verifies a real take
  was captured to disk (reports a device diagnostic if the box exposes no capture endpoint).
- `Forge --selftest-session` — grows the grid, launches a born-audible clip in a slot, verifies the
  launch handle reaches `playing` with the transport rolling (proves clip launch is audible).
- `Forge --selftest-midi` — arms an empty slot, injects notes while rolling, verifies they land
  **exactly** in a new `MidiClip` in that slot (W7 slot-record gate; zero hardware).
- `Forge --selftest-undo` — create/delete/undo/redo round-trip on a slot clip + a note-level leg, with
  `canUndo`/`canRedo` transition asserts (W05).

The rest cover MIDI-learn (`-midilearn`, `-midiinput`), the control surface (`-controlsurface`), offline
LUFS (`-lufs`), automation (`-automation`), MIDI-clock (`-sync`), live cross-surface refresh (`-livesync`),
and the UX layer (`-lcd`, `-menu`, `-tray`, `-popout`). See
[`tests/SELFTEST.md`](tests/SELFTEST.md) for the full field contract of every gate.

## Layout

```
forge/
├── CMakeLists.txt              # top-level build
├── libs/tracktion_engine/      # git submodule, pinned to v3.2.0 (bundles JUCE)
├── src/
│   ├── main.cpp                # ForgeApplication (owns the Engine) + MainComponent (shell)
│   ├── core/                   # Log — app-wide logger + crash handler (file sink + stderr, FORGE_LOG_* macros)
│   ├── services/
│   │   ├── files/              # ProjectSession — owns the Edit; create/open/save/import; createMidiClip; aux + marker seams
│   │   └── export/             # Exporter — WAV mixdown + per-track stems (sync + async)
│   ├── engine/                 # EngineHelpers · RecordController · PluginHost (incl. 4OSC seam) · PluginScanner
│   │                           #   · Metronome · MidiLearn · ClipFades · AutomationHelpers · MidiClockSync/Probe
│   │                           #   · ForgeUIBehaviour (MIDI-learn HW routing) · GridControlDriver/LaunchpadDriver/ControlSurfaceHost · dsp/LoudnessAnalyzer (LUFS)
│   └── ui/
│       ├── ForgeLookAndFeel.h  # dark theme; semantic accent colour IDs (selection / play / record / time·tempo)
│       ├── ControlBar · transport/ (incl. LcdDisplay) · arrange/ (incl. AutomationLane) · mixer/ · plugins/ · browser/ · detail/
│       ├── menu/               # ForgeMenuModel — the traditional top menu bar (File/Edit/View/Transport/Help)
│       ├── tray/               # ChannelTray — left-sidebar selected-track channel strip (Arrange)
│       ├── popout/             # PopoutWindow — tear-off desktop windows (mixer / piano-roll)
│       ├── common/             # PeakMeter.h (WeakReference-sourced meter) + StripWidgets.h (shared fader/knob styling)
│       ├── markers/            # MarkerBar — timeline cue-point strip (add/move/rename/delete, jump transport)
│       ├── export/             # ExportProgress — async-export progress + cancel panel
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
- **MIDI-learn** arms and binds (Ctrl+L), but routing a *real* hardware CC to the bound param is a
  deferred follow-up (the seam binds; the live-hardware listener is not yet wired).
- Beat-based looped-clip waveform rendering and timeline zoom/scroll are later adds.
