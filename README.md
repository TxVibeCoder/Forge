# Forge

A native desktop DAW (Digital Audio Workstation), built on **JUCE** + **Tracktion Engine**
in **C++20**. Targets Windows + macOS. Non-commercial; open-source under **AGPLv3**
(JUCE AGPLv3 + Tracktion Engine GPLv3 ⇒ Forge is AGPLv3).

> **Status:** Phase 0 — proving the toolchain and getting first sound out of the engine.
> See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design and the build
> roadmap (Phases 0–5), and [`docs/FEATURE_CATALOG.md`](docs/FEATURE_CATALOG.md) for the
> feature landscape.

## Layout

```
forge/
├── CMakeLists.txt          # top-level build (added in Phase 0, Step 4)
├── libs/
│   └── tracktion_engine/   # git submodule, pinned to v3.2.0 (bundles JUCE)
├── src/                    # Forge source
│   └── main.cpp            # app entry — creates the Engine, plays first sound
├── docs/                   # architecture + feature catalog
└── tests/
```

## Building (Windows)

Requires **Visual Studio 2022 Build Tools** (MSVC v143, C++20) and **CMake ≥ 3.22**.

```powershell
# one-time: pull the engine + JUCE
git submodule update --init --recursive

# configure + build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run the produced `Forge` executable; Phase 0's goal is a window that opens the audio device
and plays a test sine through WASAPI.

## Toolchain notes
- C++20 is mandatory (Tracktion Engine); MSVC v143 / VS2022 required (v142 / VS2019 is too old).
- Audio backend for early phases is **WASAPI** (no extra SDK). ASIO is a later add (needs the
  Steinberg ASIO SDK + `JUCE_ASIO=1`).
- `rtcheck` (RT-safety checker) is macOS/Linux only — not available on Windows.
