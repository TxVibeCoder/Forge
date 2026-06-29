# Forge

A native desktop DAW (Digital Audio Workstation), built on **JUCE** + **Tracktion Engine**
in **C++20**. Targets Windows + macOS. Non-commercial; open-source under **AGPLv3**
(JUCE AGPLv3 + Tracktion Engine GPLv3 ⇒ Forge is AGPLv3).

> **Status: Phase 1 (the spine) — substantially complete.** Forge opens/creates a real
> project on disk, imports audio onto a track, shows it on a timeline with a waveform and a
> moving playhead, plays it through the transport, and saves/reloads the project. Recording
> from a live input is wired (the verified Tracktion recipe) and works once an input device
> is selected via **Audio** settings. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for
> the full design + roadmap (Phases 0–5) and [`docs/FEATURE_CATALOG.md`](docs/FEATURE_CATALOG.md)
> for the feature landscape.

## Using Forge

Launch the app (see Building). The toolbar: **New / Open / Save / Save As / Import / Audio**.
A transport bar (play · stop · record · loop + timecode/bars|beats) sits above the arrange
view (track lanes, clip waveforms, a draggable yellow playhead).

- **Import** a WAV/AIFF/FLAC/OGG → it lands as a clip on track 1; press **Play** to hear it
  and watch the playhead sweep. Drag the playhead to scrub.
- **Save / Open** round-trips the project (`.tracktionedit`).
- **Record**: click **Audio** and select an input device first (a microphone / line-in /
  interface), then arm with **Rec**. Without a selected input the engine sees no input
  device (e.g. when output is a Bluetooth A2DP headset, which exposes no mic).

## Layout

```
forge/
├── CMakeLists.txt              # top-level build
├── libs/
│   └── tracktion_engine/       # git submodule, pinned to v3.2.0 (bundles JUCE)
├── src/
│   ├── main.cpp                # ForgeApplication (owns the Engine) + MainComponent (UI host)
│   ├── services/files/         # ProjectSession — owns the Edit; create/open/save/import
│   ├── engine/                 # EngineHelpers (tracks/transport/import); RecordController
│   └── ui/
│       ├── transport/          # TransportBar
│       └── arrange/            # ArrangeView (lanes, clips, waveforms, playhead)
├── docs/                       # architecture + feature catalog
└── tests/                      # SELFTEST.md — the --selftest field contract
```

## Building (Windows)

Requires **Visual Studio 2022 Build Tools** (MSVC v143, C++20) and **CMake ≥ 3.22**.

```powershell
git submodule update --init --recursive        # one-time: pull the engine + JUCE
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
& ".\build\Forge_artefacts\Debug\Forge.exe"
```

### Headless self-tests
- `Forge --selftest` — imports a generated tone, plays it, verifies device + clip + arrange
  + playhead, writes a PASS/FAIL report to `%TEMP%\forge_phase0_selftest.log`, quits.
- `Forge --selftest-record` — arms + records ~1s, verifies a take was captured (requires an
  input device; reports a device diagnostic otherwise).

## Toolchain notes
- C++20 is mandatory (Tracktion Engine); MSVC v143 / VS2022 required (v142 / VS2019 too old).
- Early phases use **WASAPI** (no extra SDK). ASIO is a later add (Steinberg SDK + `JUCE_ASIO=1`).
- `rtcheck` (RT-safety checker) is macOS/Linux only — not available on Windows.

## Deferred / known limitations (tracked for later phases)
- Recording end-to-end is unverified on the dev machine (Bluetooth output → no input device);
  the code path follows the verified recipe and needs an input selected.
- Waveform draw assumes non-looped clips (correct for imported/recorded clips; looped-clip
  rendering is a later add). Timeline view window is fixed at 0–60s until zoom/scroll lands.
- File dialogs capture `this`; hardened paths (SafePointer guards) are a later polish item.
