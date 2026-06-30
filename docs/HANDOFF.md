# Forge — Session Handoff

> Living handoff for picking the project back up cold. Pairs with [STATUS.md](STATUS.md) (the living
> roadmap). Last updated: **2026-06-30**, end of the "recording-verification + MIDI-design" session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) ·
HEAD: `79a75d5` · branch `main` · **2 commits ahead of `origin/main` — NOT pushed** (`e293424`, `79a75d5`) ·
~6,180 lines / 27 source files · last build clean · both self-tests **PASS**.

Forge is an arrangement-first DAW on **JUCE + Tracktion Engine** (C++20, Windows-verified). Phases 0–4 +
startup-latency hardening are done and live: project/arrange/transport/mixer/plugins/browser/inspector/
export, output-only startup with lazy record-input open. See [STATUS.md §2](STATUS.md) for the full feature list.

---

## What this session did

### 1. Recording — VERIFIED end-to-end on real hardware ✅ (`e293424`)

The recording path was **correct all along**. The `--selftest-record` FAIL in the previous handoff was a
**harness bug, not a product bug.**

- **Root cause (fully traced).** The old harness ran open → enable → arm → record **synchronously inside one
  blocking `MessageManager::callAsync` callback**. Tracktion rebuilds its wave-input list in
  `DeviceManager::handleAsyncUpdate`, driven by an **async posted message**. While the single callback blocked
  the message thread, `postMessageToSystemQueue` failed app-wide (`InternalMessageQueue` unavailable →
  `AsyncUpdater::post()` returns false → `cancelPendingUpdate()`), so the rebuild was silently dropped,
  `getNumWaveInDevices()` stayed 0, and `armFirstInputToTrack` bailed. The **real app never hits this** — it
  arms on a UI click and the message loop keeps pumping, so `enableInputs()`'s `dispatchPendingUpdates()`
  force-runs the (successfully-posted, still-pending) rebuild synchronously before the arm.
- **Fix.** Made `--selftest-record` **event-driven** (phase 1 open input + yield → phase 2 arm/record after the
  rebuild delivers → phase 3 stop + verify), mirroring the real arm path. It now captures a **real take to disk**:
  `inputDeviceCount=8, trackArmed=1, recordingStarted=1, recordedClipCount=1, recordedFileExists=1,
  recordedClipLengthSecs≈1.44, recordedPeakMagnitude>0` (non-zero ⇒ real samples reached disk), `result=PASS`,
  stable across runs. Playback selftest still PASS.
- **Code touched (2 files):** [EngineHelpers.h](../src/engine/EngineHelpers.h) (`ensureRecordingInputOpen` now
  returns its device-open error string — informational; real-app callers ignore it, the selftest reports it) and
  [main.cpp](../src/main.cpp) (phased record-selftest state machine + a recorded-file peak-magnitude content
  check). All temporary submodule instrumentation was reverted — submodules are clean.
- **Carry-forward lesson:** **never arm recording synchronously in one blocking callback** — the engine's
  device-list rebuild is async and must be allowed to deliver. This applies directly to MIDI-input recording (W7).
- **Device-pairing nuance (documented, not a bug):** the lazy open keeps the working OUTPUT device and adds the
  current type's DEFAULT capture input. On this box that combined device reports as "Speakers (Realtek)" with its
  own 8 input channels rather than the listed "Microphone Array (Intel)". The record PATH is verified regardless;
  picking a specific mic is done via the Audio Settings dialog. Full writeup in
  [device-recording.md](devlog/device-recording.md).

### 2. MIDI tracks + piano-roll — DESIGN complete & source-verified ✅ (`79a75d5`)

A multi-agent design pass (understand → design → adversarial-verify → synthesize) produced a **build-ready,
source-grounded design**: [docs/devlog/midi-design.md](devlog/midi-design.md). **5 of 6 load-bearing assumptions
confirmed against the actual source; 1 refuted and corrected.**

- **Key facts (confirmed):** Tracktion has **no MIDI track type** — a `te::AudioTrack` hosts MIDI + wave clips,
  and audibility comes from a built-in **4OSC** synth at plugin-chain **index 0**. A drawn MIDI clip is audible
  with just `createNewPlugin("4osc") → insertPlugin(synth,0) → insertMIDIClip → addNote → play` (no input/record
  code — matches three shipped engine demos). The `getSequence()` vs `getSequenceLooped()` data-loss trap is real
  (always edit `getSequence()`). The arrange selection/drag/snap/persist seams are base-typed on `te::Clip*`, so
  MIDI rides them; the piano-roll reuses the shared `TimelineView` + snap grid in the bottom drawer.
- **Refuted (the valuable catch):** MIDI-input recording is **NOT** a mechanical clone of the audio arm —
  `enableInputs()` is wave-only (`setStereoPair` doesn't exist on MIDI), MIDI needs its own enable sequence
  **before** `ensureContextAllocated()` (else the device is dropped from `getAllInputs()`), a different flush
  (`rescanMidiDeviceList()`), and a multi-value device-type filter. → re-scoped to a higher-risk later wave (W7),
  de-risking the MVP.
- **The plan — file-disjoint 7-wave build** (matches the project's established workflow):
  - **MVP = W1–W5:** W1 instrument seam (`PluginHost`) · W2 MIDI-clip create (`ProjectSession`) · W3
    clip-component split (`ArrangeView` — the one heavily-wired file, single-owner) · W4 piano-roll
    (`src/ui/pianoroll/*`, new) · W5 integrate (orchestrator: `CMakeLists`/`main.cpp`/ControlBar) → **a drawn
    MIDI clip you can hear.**
  - **Post-MVP:** W6 velocity/polish · W7 MIDI-input recording (needs a runtime test with a physical controller).
  - Six source-grounded corrections are already folded into the design doc (instrument insert-at-0 vs. the
    volume-index effect path; `ResizerBar` max-height baked at construction; the `insertMIDIClip(ClipOwner&)`
    free-function name collision; the two-arg `createNewPlugin` overload; W3 callback re-typing; the
    Viewport-mandatory piano-roll). A builder starts from verified facts.

---

## Build, run, verify

```sh
# Full cmake path — winget doesn't refresh PATH in these shells.
& "C:\Program Files\CMake\bin\cmake.exe" --build ".\build" --config Debug

& ".\build\Forge_artefacts\Debug\Forge.exe"                 # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest        # headless playback check → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-record # headless recording check → PASS/FAIL
# Both selftests write %TEMP%\forge_phase0_selftest.log
```

Needs VS2022 / MSVC v143 (C++20). First clone: `git submodule update --init --recursive`. The record selftest
writes a real take and reports `recordedPeakMagnitude` (>0 ⇒ signal flowed). Both selftests PASS on this box.

---

## ⚠️ Gotchas

- **Recording is verified working** — the previous "input-gated / unverified" caveat is resolved. The only
  remaining refinement is default-mic *selection* (device-pairing nuance above), not the record path itself.
- **Never arm recording synchronously in one blocking callback.** The wave-device-list rebuild is async; the
  selftest is event-driven for exactly this reason. MIDI-input recording (W7) must follow the same discipline.
- **Build file lock:** a running `Forge.exe` → `LNK1168` on the next build. `Get-Process Forge | Stop-Process
  -Force` first. Also kill it before runtime tests (a killed instance can still briefly hold the WASAPI output
  device); a 45–90 s timeout is prudent.
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake.
- **Submodules are clean** — all the temporary engine/JUCE instrumentation used to root-cause the record bug was
  reverted. Don't be surprised by a clean `git submodule status`.
- **Not pushed:** `main` is 2 commits ahead of `origin/main`. Push when ready (`git push`).

---

## What's next (prioritized)

1. **MIDI MVP build (W1–W5)** — the clear next effort. Design is done and source-verified in
   [midi-design.md](devlog/midi-design.md); follow the file-disjoint wave plan in §6 there. Outcome: draw a MIDI
   clip on a track and **hear it** via the default 4OSC. The handoff's earlier advice still holds — start it with
   a **fresh context budget**, using the Workflow tool with file-disjoint agents (the project's proven pattern).
2. **MIDI post-MVP:** W6 velocity lane + selection/copy-paste polish; **W7 MIDI-input recording** (carries the
   most risk — needs its own enable sequence + a runtime test with a physical MIDI controller; see midi-design.md
   §5 + the residual-uncertainty note).
3. **Automation** (vol/pan/plugin-param lanes) + **buses/sends** in the mixer (engine Phase 3 + 2 remainder).
4. **Polish** — async export + progress (mixdown & stems both block the message thread); LUFS metering; markers;
   comping; off-thread record-input open (so even the first arm never briefly blocks the message thread).
5. **macOS build** (only Windows verified) and interactive-UI verification of the Phase-4 surfaces (snap selector,
   scan dialog, mixer bypass/reorder, stem output construct + compile but aren't headlessly exercised).

> Note: computer-use cannot grab the dev-built `Forge.exe` window by name (it's not a Start-menu app), so
> GUI-driven verification of in-app surfaces needs either a manual pass or a headless selftest hook.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces + contract-first
  seams; the orchestrator does the `CMakeLists`/`main.cpp`/ControlBar wiring and the single integration build.
- **Adversarial-review / verify waves** (design → independent skeptic-verify, default-refuted) — extremely high
  ROI. This session a verify wave caught that MIDI-input recording is *not* a mechanical clone of the audio arm
  (which would have produced a broken W7), and the recording root-cause was found by progressively instrumenting
  the engine until the async-post failure was undeniable. **Run a verify wave on any design or change you can't
  runtime-confirm here.**

---

## Key references

- [STATUS.md](STATUS.md) — living roadmap (refreshed this session).
- [docs/devlog/midi-design.md](devlog/midi-design.md) — the build-ready MIDI design + 7-wave plan (**read before
  starting the MIDI build**).
- [docs/devlog/device-recording.md](devlog/device-recording.md) — recording root cause, the harness-bug writeup,
  the device-pairing nuance, and "verifying a real take".
- [docs/devlog/integration.md](devlog/integration.md) — orchestrator's wave-by-wave record (incl. review findings).
- [docs/ARCHITECTURE.md](ARCHITECTURE.md) · [docs/INTERFACE.md](INTERFACE.md) · [docs/FEATURE_CATALOG.md](FEATURE_CATALOG.md)
- [tests/SELFTEST.md](../tests/SELFTEST.md) — both selftests' fields + pass criteria (record test now event-driven).
