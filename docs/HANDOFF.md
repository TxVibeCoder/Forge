# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-07-01**, end of the
> **"W03 — automation lanes · MIDI-clock out · async LUFS · live cross-surface refresh"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`**. **W03 shipped this session: four features in `ffa494d` (code) + a docs commit — COMMITTED +
PUSHED to `origin/main` (on top of the `cede941` baseline). Working tree clean; the pushed set was
sanitized.** Last build **clean** (MSVC Debug, 0 warnings) · **all ELEVEN selftests PASS** — `--selftest`,
`--selftest-record`, `--selftest-session`, `--selftest-midi`, `--selftest-midilearn`, `--selftest-midiinput`,
`--selftest-controlsurface`, `--selftest-lufs`, **`--selftest-automation`**, **`--selftest-sync`**,
**`--selftest-livesync`** — each on the final binary in its own process with clean self-exit; `--screenshot`
renders 5 PNGs. Shipped: **volume/pan automation lanes (Arrange), MIDI-clock out, LUFS analysis moved onto
the export worker, live cross-surface refresh**, the **INTERFACE.md Session-first rewrite**, and a **latent
product UAF fixed** (mixer master meter vs. a freed playback context). Full record in
[devlog/wave-03-features.md](devlog/wave-03-features.md).

**STANDING MAINTAINER CONSTRAINTS (stated this session):** the maintainer has **no physical MIDI hardware**
and **runs no manual tests** — every feature must be headless-provable (selftest gates + `--screenshot`).
Hardware smoke items (Launchpad byte mapping, physical-CC MIDI-learn, APC40) stay parked until hardware
exists; do NOT propose them as next steps. Autonomous multi-agent waves are standing-approved, with model
tiers matched to task complexity. **Fable holds UI/UX design authority** (layout/function, legibility, ease
of use, clean-in-all-states, sequence lighting, tempo visuals, clock accuracy); design freedom is total
EXCEPT: dark theme stays; "clean means organized, not minimal" (never cut features for sparseness —
structure them); accent colours are a small semantic wayfinding vocabulary (one colour = one meaning); and a
traditional top menu bar is a standing request. The full charter is in [INTERFACE.md](INTERFACE.md) §1.

---

## ⚠ READ THIS FIRST — what Forge is now

**Forge is a sample / scene-based, controller-driven DAW.** The **primary surface is an Ableton-style
Session clip grid** (tracks × scenes of launchable clips), meant to be played from real **grid controllers**
(Novation Launchpad, Akai APC40 mkII). The linear **Arrange** timeline is a **secondary** view.

This was a **direction reset** (a recent prior session). Everything built before it was *arrangement-first* — that
work is **not wasted** (clips, the 4OSC instrument, the piano-roll, the mixer, plugin hosting all become
building blocks that live *inside* slots and scenes), but the **primary identity and next build have
changed**. The authoritative brief is **[DIRECTION.md](DIRECTION.md)** — read it before planning anything.

**The controllers are EXTERNAL hardware** Forge connects to over MIDI — Forge does **not** draw a controller
on screen. The only on-screen surface is the Session grid. Hardware integration is a "hope to one day
connect" goal, **not an MVP gate**: the grid is fully playable with mouse + keyboard.

---

## What this session did — W03: automation lanes · MIDI-clock out · async LUFS · live refresh

Four features + a docs rewrite, selected for **headless provability** (see the standing constraints above),
landed in **`ffa494d` (code)** + a docs commit on the `cede941` baseline. Process: 4 parallel source-verify
spikes → 3 adversarial skeptics (one refuted + corrected the sync recipe **before any code existed**) → 6
file-disjoint implementation agents on **tiered models** (top tier on UI + verification, mid on recipe-driven
C++, small on docs) → orchestrator integration (3 new gates) → an 18-agent adversarial QC (**9 confirmed, 1
refuted — all 9 fixed**). Full record: [devlog/wave-03-features.md](devlog/wave-03-features.md).

- **Automation lanes** (`engine/AutomationHelpers.h` + `ui/arrange/AutomationLane.{h,cpp}` + ArrangeView): a
  header-only seam over each track's `VolumeAndPanPlugin` curves — units are fader position 0..1 / pan −1..+1,
  and **every mutator ends in `updateStream()`** (curve activation is otherwise deferred to a 10 ms engine
  timer). UI: a collapsible 46 px per-track lane (an **A** toggle beside M/S/R), click-to-add / drag / right-
  click-delete points, Volume|Pan selector, pixel-exact against clips via the shared `TimelineView` axis. The
  lane listens for `curveHasChanged` so external curve edits repaint live. Points persist in the
  `.tracktionedit` automatically. Gate: **`--selftest-automation`** (a 2-point falling curve demonstrably
  drives `getCurrentValue()` during playback).
- **MIDI-clock out** (`engine/MidiClockSync.h` + `engine/MidiClockProbe.h` + a TransportBar **Clock** toggle):
  per-device `setSendingClock` behind a small seam. Gate: **`--selftest-sync`** captures the engine's ACTUAL
  wire bytes via a `MidiOutputDevice` subclass overriding `sendMessageNow` — SPP + start + a 24 PPQN clock
  train (96/96 expected on this machine) + stop — with an honest SKIP-degrade on zero-MIDI-out boxes and a
  fully lossless teardown (device entry, scan interval, NAME-keyed persisted props all restored).
  **Ableton Link deferred:** the engine wrapper is compiled out and the Link library is NOT vendored —
  vendoring + license review is a maintainer decision (see Open decisions).
- **LUFS off the message thread** (`Exporter` + `dsp/LoudnessAnalyzer`): the W02 QC minor closed — analysis
  runs on the export render worker after the WAV is provably closed; the message thread receives only a
  finished value under the existing alive token; a per-chunk **abort predicate** keeps the dtor's 5 s join
  bound honest. The W02 `finishAll` callback invariant is preserved verbatim. `--selftest-lufs` gained
  file+thread and abort legs.
- **Live cross-surface refresh** (MixerView + DetailView): values changed anywhere (MIDI-learn, another view,
  automation) appear on the mixer/inspector without re-selecting — a structural-guard-first 28 Hz sync with
  drag/focus guards and `dontSendNotification` everywhere, allocation- and repaint-free at steady state;
  DetailView polls at 10 Hz. Gate: **`--selftest-livesync`**.
- **The teardown hang that became a product fix:** the first sync-gate run froze the app at shutdown (one
  core spinning). Marker-bisecting the destructor chain found the mixer master strip's `PeakMeter` holding a
  raw pointer into `EditPlaybackContext::masterLevels` — the gate was the first code ever to free the
  playback context with the mixer alive, and `removeClient` then walked freed memory. **Reachable in the real
  app via device restarts** (`restartAllTransports` frees contexts). Fixed by holding ALL meter sources as
  **`juce::WeakReference<te::LevelMeasurer>`** (the engine declares it weak-referenceable): the reference
  nulls itself when the owner dies, so detach skips `removeClient` exactly when it must — one mechanism that
  also closed QC's plugin-cull race on track/return meters.
- **Adversarial QC (4 dimensions × per-finding skeptics):** 9 confirmed / 1 refuted. Blockers/majors: the
  ReturnStrip 28 Hz UAF after deleting an aux-return track (fixed: re-resolve through the seam before every
  deref — the R1 rule); the **Clock toggle left unwired at integration** (the gate passed by driving the
  engine seam directly — exactly the class of gap selftests cannot see; fixed in `setupControlBar`); the
  meter cull race (fixed by the WeakReference); a stale lane on external single-point curve moves (fixed via
  the listener; the engine's move-the-single-point semantics are accepted + documented). Minors: sync-gate
  degrade path never rolled; probe props pollution (snapshot/restore); automation drag clamp; overlapping-
  handle hit-test z-order; INTERFACE.md tense drift.

> Prior session (W02, `bb9ef5e`, PUSHED): **MIDI-learn HW routing (focused-Edit `ForgeUIBehaviour`,
> `--selftest-midiinput`) · a Forge-native Launchpad control surface (`--selftest-controlsurface`; byte
> mapping still needs a real device) · offline BS.1770-4 LUFS on export (`--selftest-lufs`)**. Key facts that
> survive: a `VirtualMidiInputDevice` has no `controllerParser` (physical-CC drive is hardware-only proof);
> Tracktion's `ControlSurface` clip-launch is an unwired `std::function`, so the driver calls
> `ProjectSession` directly; no non-mutating post-fader master sample tap exists without forking the engine.
> Full record: [devlog/wave-02-features.md](devlog/wave-02-features.md).

> Prior session (Wave 01, `e3b8c7c`, PUSHED): Forge's **first flat parallel multi-CLI wave** — six file-disjoint
> feature CLIs (P1–P6) each committed a scoped commit, then the orchestrator (P7) implemented the two shared
> `ProjectSession` seams, wired everything into the single integration build, and ran adversarial QC. Baseline
> was `6100fb9`. Full record: [devlog/wave-01-features.md](devlog/wave-01-features.md).

- **P1 metronome + count-in** (`096c9bd`): `engine/Metronome` seam over the engine's `Edit::clickTrackEnabled`
  (persisted, OFF by default) + native `Edit::CountIn`; TransportBar gains a **Click** toggle + count-in
  selector. Count-in is native (`transport.record()` pre-rolls it) — no `RecordController` change.
- **P2 MIDI-learn** (`1ef4f37`): `engine/MidiLearn` — a thin driver over Tracktion's native
  `ParameterControlMappings` (persists on the Edit) + `PluginHost::getAutomatableParameters`. Wired **minimal**:
  a **Ctrl+L** track▸plugin▸param picker arms a learn, proven headlessly by the new **`--selftest-midilearn`**
  gate. **Deferred** (ticketed): a focused-edit `ForgeUIBehaviour` / MIDI-input listener so real controller CCs
  reach the seam.
- **P3 buses / sends** (`c5062a3`): per-track **A/B aux-send knobs** + two **aux-return strips** in the mixer,
  over a new `ProjectSession` aux seam. An aux bus = a plain `AudioTrack` + `AuxReturnPlugin`, **appended at the
  END** so absolute track indices stay stable; `onTracksChanged` rebuilds the grid/lanes on add.
- **P4 async export + progress** (`8d0afdf`): `Exporter::renderEditToWavAsync`/`renderStemsAsync` run off the
  message thread with progress + cancel behind an `ExportProgress` panel; the **sync API is preserved**.
- **P5 markers** (`fe1bfcb`): a `MarkerBar` timeline strip (keyed on the stable `EditItemID`) over a new
  `ProjectSession` markers seam, sharing the arrange `TimelineView`.
- **P6 clip edge-fade** (`975846e`): `engine/ClipFades` — a 5 ms linear anti-click fade (audio-only,
  idempotent), wired into `importAudioFile` + `importAudioIntoSlot`.
- **Consolidation:** implemented the aux + markers seams (source-verified engine APIs) + wired all 6 features.
  Caught + fixed a **nested-block-comment** bug in the committed `MarkerBar.h` (`te::MarkerClip*/Clip*` closed a
  `/* */` doc comment early — the CLAUDE.md gotcha; a build-less CLI can't catch it). **Adversarial QC**
  (5 dimensions → per-finding skeptic verify): **3 confirmed, 0 refuted** — two distinct lifetime blockers, both
  fixed in `swapProject` before the Edit is torn down: an **async-export UAF** (Edit freed under the render
  worker → `activeRender.reset()`) and a **MIDI-learn dangling `learningEdit`** (→ `midiLearn.cancelLearn()`).

> Prior session (`160f6cc`): **W7 — MIDI record into Session clip slots** — a track can be MIDI record-armed and
> an empty slot captured (**Ctrl+Enter** / right-click "Record into slot") straight into a born-audible
> `te::MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`
> ([devlog/midi-record-design.md](devlog/midi-record-design.md)). Before that (`8d15234`): **Session-grid vertical scroll +
> app-wide logging** — the 16-scene grid scrolls Ableton-style with fixed ~46 px pads and a pinned scene column
> that tracks the pads ([devlog/session-scroll-design.md](devlog/session-scroll-design.md)); and an app-wide
> logging + error-handling subsystem (`src/core/Log.*`, ~90 seams instrumented) with logging-at-the-seam a
> standing build principle ([LOGGING.md](LOGGING.md) · [devlog/logging-design.md](devlog/logging-design.md)).
> Before that (`06f3cf6`): **the Session clip-launch grid** — SessionView as the default `ViewMode`, on
> Tracktion's `ClipSlot` / `Scene` / `LaunchHandle`; source-grounded design + file-disjoint build → 5-lens
> adversarial QC → independent fix re-verify → `--selftest-session` + `--screenshot`
> ([devlog/session-design.md](devlog/session-design.md)). Before that: the **direction reset → DIRECTION.md** +
> the to-scale [mockups](../mockups/) (sheet 00 = the Session grid), and the **MIDI MVP (W1–W5) + W6 piano-roll
> polish** ([devlog/midi-build.md](devlog/midi-build.md)) — clips / 4OSC / piano-roll ride inside slots.

---

## What exists today (the building blocks)

Phases 0–4 + startup hardening + MIDI MVP/W6 + **W7 MIDI record into slots** + the **Session clip-launch grid**
(with vertical scroll) + the **logging subsystem** + the **Wave-01 feature seams** + the **W02 engine seams** +
the **W03 features**, all shipped, building clean, all ELEVEN selftests PASS:

- **Session grid (PRIMARY view)** — tracks × 16 scenes of launchable clips on `ClipSlot` / `Scene` /
  `LaunchHandle`; single-click launches (instant), right-click "Edit clip" (launch-free), double-click opens;
  keyboard arrows/Enter launch; **audible, bar-quantised** launch; pinned scene column + MASTER stop-all;
  25 Hz gated state poll. **Ableton-style vertical scroll** — fixed ~46 px pads, all 16 scene rows reachable,
  the pinned scene column tracks the pads. Default `ViewMode` (**F8**). Details:
  [devlog/session-design.md](devlog/session-design.md) + [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Logging + error-handling (`src/core/Log.*`)** — an app-wide `juce::Logger` sink (file at
  `%APPDATA%\Forge\logs\forge.log`, 1 MiB + `.1` rollover, + stderr echo) with a crash handler and
  `FORGE_LOG_*` macros; ~90 fallible seams instrumented. RT-thread- and poll-safe by rule. Logging fallible
  seams as you build them is a **standing build principle** — [LOGGING.md](LOGGING.md).
- **Project** save/load (`.tracktionedit`), **audio import**, an **arrange timeline** (waveforms, playhead,
  clip drag-to-move, selectable snap grid).
- **Transport** (play/stop/record/loop) and **recording** — verified end-to-end on real hardware
  (`--selftest-record` captures a real take); output-only startup, lazy capture-input open.
- **MIDI** — clips on any track, born audible via a default **4OSC** at chain index 0; a **piano-roll**
  (draw/move/resize/delete, velocity lane, multi-select, copy/paste); **W7 record into Session slots** — MIDI
  record-arm a track, capture an empty slot (**Ctrl+Enter** / right-click "Record into slot") straight into a
  born-audible `MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`. Details:
  [devlog/midi-record-design.md](devlog/midi-record-design.md).
- **Mixer** (strips, plugin inserts w/ bypass+reorder, master + post-fader meter, **A/B aux sends → aux-return
  strips**), **plugin hosting** (built-in + VST3/AU scan + floating editors), **Browser**, **clip Inspector**,
  **WAV export + stems** (now **async, off-thread, with a progress/cancel panel**).
- **Wave 01 additions** — **metronome + count-in** (TransportBar Click toggle + selector), **markers** (a
  timeline marker bar over the arrange view), **MIDI-learn** (a **Ctrl+L** param picker over Tracktion's native
  mapping store), and an automatic **anti-click edge fade** on imported audio.
- **W02 additions** — **MIDI-learn hardware routing** (a focused-Edit `ForgeUIBehaviour` so the engine's native
  CC→param routing reaches an actual Edit; real-hardware CC drive still a smoke item — a virtual device has no
  `controllerParser`), a **Forge-native grid control surface** (device-agnostic driver + a Novation Launchpad
  driver built to the MIDI spec; drives `ProjectSession` directly, pushes LEDs from `SlotVisualState`; inert
  without hardware — byte mapping needs a real device), and **offline LUFS** (a BS.1770-4 integrated-loudness +
  true-peak analyzer run on the export render; the integrated LUFS shows in the export-done status strip).
- **W03 additions** — **volume/pan automation lanes** in Arrange (an **A** toggle per track expands a 46 px
  lane; add/drag/delete points; live repaint on external curve edits; persisted in the `.tracktionedit`),
  **MIDI-clock out** (a TransportBar **Clock** toggle over the `MidiClockSync` seam), **LUFS analysis on the
  export render worker** (the UI never blocks; per-chunk abort guard), **live cross-surface refresh**
  (mixer/inspector reflect engine changes without re-selecting), and **WeakReference-sourced mixer meters**
  (the latent freed-context/plugin-cull UAF class is closed).

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

> W03 (`ffa494d` + docs) is **committed + PUSHED to `origin/main`.** Hardware smoke tests and manual GUI
> passes are **permanently parked** (the maintainer has no hardware and runs no manual tests — see the
> standing constraints at the top); the path forward is the headless-provable roadmap.

1. **W04 — the UX wave (the planned next mission; charter in [INTERFACE.md](INTERFACE.md) §4).** Under
   Fable's design authority: a **traditional top menu bar** (juce `MenuBarModel` — the discoverable command
   index, shortcuts shown beside items), **popout/tear-off panels**, **slide-out drawers**,
   **adjustable-section scaling with persisted sizes**, **scene layout polish**, **sequence lighting**
   (beat-accurate pad illumination derived from the engine transport — never free-running timers),
   **graphic tempo indicators**, the **semantic accent vocabulary** (one colour = one meaning: interactive,
   record red, play/launch green, time/tempo), and a **state-matrix screenshot harness** (view × window
   size × region states — the evidence base for all-states design review, and where the expanded-automation-
   lane visual proof lands).
2. **Deferred follow-ups (ticketed).** (a) **Ableton Link** — the engine wrapper is compiled out and the Link
   library is NOT vendored; enabling = vendor `github.com/Ableton/link` + asio-standalone + an AGPLv3
   license-compatibility review + two CMake lines (a maintainer dependency decision). (b) **Aux-send knobs +
   return-strip insert rows are not live-synced** (the W03 guard pattern extends to them). (c) **Plugin-param
   automation lanes** (vol/pan shipped; the seam generalizes). (d) **Audio-slot-record edge-fade** remains
   blocked — no audio slot-record path exists. (e) The export progress panel sits at ~100 % during a very
   long master-render loudness analysis (honest but unlabeled).
3. **Carried-over polish** — **comping**; an optional **live short-term LUFS meter** (requires forking the
   engine for a post-fader sample tap); macOS build (no Mac on hand).
4. **Parked until hardware exists** — the Launchpad byte-mapping smoke test, physical-CC MIDI-learn drive,
   and the APC40 mkII driver.

---

## Build, run, verify

```sh
# Full cmake path — winget doesn't refresh PATH in these shells.
& "C:\Program Files\CMake\bin\cmake.exe" --build ".\build" --config Debug

& ".\build\Forge_artefacts\Debug\Forge.exe"                    # the app (opens on the Session grid)
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest         # headless playback check     → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-record  # headless recording check    → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-session # Session-grid audibility gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midi    # MIDI-record-into-slot gate  → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midilearn # MIDI-learn CC→param bind gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midiinput # focused-Edit HW-routing gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-controlsurface # virtual pad→launch + LED poll → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-lufs    # BS.1770-4 LUFS (buffer + worker-thread + abort legs) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-automation # volume-curve drives playback gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-sync    # MIDI-clock wire-byte capture gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-livesync # cross-surface live-refresh gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --screenshot       # render each view → %TEMP%\forge_shot_*.png
# Selftests write %TEMP%\forge_phase0_selftest.log.  First clone: git submodule update --init --recursive
```

Regenerate the mockups (needs Docker; the `forge-dxf` image exists on this box):
```sh
cd mockups/src && MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" forge-dxf:latest python build.py
# then copy out/*.dxf → mockups/ and out/*.png → mockups/preview/   (src/out/ is gitignored)
```

---

## ⚠ Gotchas

- **GUI can't be driven headlessly here** — computer-use can't grab the dev-built `Forge.exe` window by
  name. Use **`--screenshot`** to *see* the UI (renders each view to `%TEMP%\forge_shot_*.png`) and a headless
  selftest hook (like `--selftest-session`) to *exercise* it. (Manual passes are off the table per the
  standing constraints — the W04 state-matrix screenshot harness is the planned substitute.)
- **A `LevelMeasurer` can die under a UI meter (W03 — was a latent product UAF).** The master measurer lives
  ON `EditPlaybackContext` (freed by `freePlaybackContext` / device restarts); track/return measurers live on
  a `LevelMeterPlugin` the engine's plugin cull can reclaim after a track delete. Holding a raw
  `LevelMeasurer*` and calling `removeClient` later walks freed memory — the first `--selftest-sync` run spun
  forever in exactly that, in `~MixerView`. **Rule: hold measurer sources as
  `juce::WeakReference<te::LevelMeasurer>`** (the engine declares it weak-referenceable) and skip
  `removeClient` when the weak ref is null. And per the R1 rule, re-resolve cached engine object pointers
  through a seam before dereferencing in any poll (`ReturnStrip::pollMeter` was a deterministic 28 Hz UAF
  after deleting an aux-return track — its Arrange lane has a full header menu including Delete).
- **Engine device IDs are NOT juce identifiers (W03).** `MidiOutputDevice::getDeviceID()` returns an
  engine-minted ID (e.g. `out_81b0d7ef`), never equal to `juce::MidiDeviceInfo::identifier` — match devices by
  NAME. And `MidiOutputDevice` props (`enabled`, `sendMidiClock`) are **persisted keyed by device NAME**, so
  any same-named device object (e.g. a selftest probe) that calls `saveProps` (which `setSendingClock`,
  `closeDevice`, and the dtor all do) rewrites the REAL device's stored state — snapshot + restore around it.
- **A UI seam a gate can't see can ship unwired.** The W03 Clock toggle passed `--selftest-sync` while wired
  to nothing, because the gate drives the engine seam directly. Adversarial QC caught it; when a feature has
  both an engine seam and a UI affordance, verify the SHELL wiring explicitly at consolidation.
- **Single-point automation curves follow direct value sets (engine-intended, W03).** With exactly one point
  on a curve, `setParameterValue` (a mixer fader gesture) MOVES that point — one point and a static value are
  the same statement. Accepted semantics; the automation lane listens for `curveHasChanged` so it's visible.
- **Build file lock:** a running `Forge.exe` → `LNK1168` on the next build, and it can hold the WASAPI
  device. `Get-Process Forge | Stop-Process -Force` before building or runtime-testing; use a 45–90 s timeout.
- **Docker on this Windows box:** mount with the Windows path or Git Bash mangles it —
  `MSYS_NO_PATHCONV=1 docker run -v "$(pwd -W):/work" …`.
- **MIDI note beats are CONTENT-relative** (beat 0 = clip start at offset 0); always edit `getSequence()`,
  never `getSequenceLooped()`. Slot inserts use the **free** `insertMIDIClip(ClipOwner&, name, TimeRange)`
  (**name BEFORE range**) via `ClipSlot`'s upcast — NOT the AudioTrack member overload the linear path used.
- **Never arm recording synchronously in one blocking callback** — the device-list rebuild is async. The
  **same discipline now also guards the playback selftest**: yield to the loop, `dispatchPendingUpdates`,
  `blockUntilSyncPointChange` before checking (a hot-swapped output device is `isSuspended` until it drains).
- **Nested block comments corrupt doc comments (bit a THIRD time this project).** A `/* … */` block comment
  placed **inside** a `/** … */` doc comment closes the doc comment early — the first `*/` ends it, and everything
  after (up to the real close) leaks into the code stream and corrupts the following declarations. Bit
  `RecordController.h` (W7), `MarkerBar.h` (Wave 01), and now `ControlSurfaceHost.h` (W02 — `ClipSlot*/Clip*` in
  the header comment, whose `*/` closed the block early; the integration build caught it, a build-less agent
  could not). Fixed by not nesting `/* */` inside `/** */`. Use `//` for inline notes inside a doc comment.
- **`VirtualMidiInputDevice` has no `controllerParser` (W02).** Only `PhysicalMidiInputDevice` carries the
  `controllerParser` that routes an incoming CC → `ParameterControlMappings`. A virtual device can't exercise
  that path — so the focused-Edit `ForgeUIBehaviour` HW-routing (item 2a) is proven headlessly only up to "the
  focused Edit is reported + a CC→param bind lands via the seam"; a **real hardware CC actually driving a param
  is a real-hardware smoke item**, not a headless one.
- **No non-mutating post-fader master sample tap without forking the engine (W02).** The read-only
  `tracktion_engine` submodule exposes no way to observe the master output as *samples*: `LevelMeasurer` gives
  only reduced dB, the master-tap node is internal, and JUCE's `AudioDeviceManager` **sums** secondary audio
  callbacks rather than letting one observe the engine's output. So there's **no live master LUFS** — integrated
  loudness is a whole-program measurement done on the export render (offline), which is the correct tool, not a
  workaround.
- **MIDI slot record is transport-driven, NOT launch-driven.** Recording is started by
  `transport.record(false)` over `isRecordingActive()` destinations — **never** `launchSlot` on the record path
  (an empty armed slot has no clip and no `LaunchHandle`, so a launch is a hard no-op). `isSlotRecording` is
  `isSlotMidiArmed(slot) && transport.isRecording()`, NOT "LaunchHandle playing".
- **Slot capture must be slot-ONLY.** Arm the slot's `itemID` (`setTarget(slot.itemID, /*moveToTrack=*/false,
  …)`) and **disarm the track's MIDI target first** — if both a track and a slot are armed, notes
  double-capture into the arrangement as well as the slot. (Audio/MIDI arm are also mutually exclusive per
  track in v1.)
- **Injecting synthetic MIDI headlessly:** after `engine.getDeviceManager().createVirtualMidiDevice(name)`
  (async — **yield first**, then find the device by name via `getMidiInDevices()`), inject notes with
  `dev->handleIncomingMidiMessage(msg, dev->getMPESourceID())` (the **public** override — NOT the protected
  `handleNoteOn/handleNoteOff` keyboard-listener overrides). On teardown call
  `deleteVirtualMidiDevice(*dev)` or the device name leaks (persisted in engine PropertyStorage) and the next
  run's `createVirtualMidiDevice` fails with "Name already in use".
- **SessionView threading (load-bearing):** pads cache NO `te::ClipSlot*`/`Clip*` — only `(track,scene)`
  indices; the 25 Hz poll re-resolves via the **const** `getClipSlot` (never inserts). Any track-list change
  must rebuild the grid before a stale `TrackColumnComponent` derefs its `AudioTrack&` (the QC blocker).
- **Scrolled-viewport relayout (the session-scroll session's bug):** for a `juce::Viewport`, the **viewed component's
  top-left IS the scroll offset** — so in `SessionView::resized()` size the scrolled `columnHolder` with
  `setSize(w, h)`, **never** `setBounds(0, 0, w, h)` (the latter yanks the grid back to the top on any relayout
  while scrolled). The pinned scene column is offset by `-getViewPositionY()` in `syncSceneColumnToScroll()`.
- **CriticalSection nested-lock type:** the logging file sink guards with a `juce::CriticalSection`; to take it
  use **`juce::CriticalSection::ScopedLockType`** (i.e. `ScopedLock`), NOT a bare `juce::ScopedLock` templated
  wrongly — get the member type from the lock. (Never log from the audio/RT thread regardless — see LOGGING.md.)
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake. (And a quoted
  `"C:\Program Files\..."` path in the same command as `Remove-Item` can trip the sandbox guard — split them.)
- **Latest work is committed + PUSHED.** W03 (four features + 3 new gates, `ffa494d`) + its docs commit are
  on **`origin/main`**; the working tree is **clean** and the pushed set was **sanitized** (only placeholder
  `C:\Users\…` / `<user>` forms in doc text — no real machine paths / identity leaks). Prior history: W02
  (`bb9ef5e` → docs `2da5f16`), Wave 01 (`e3b8c7c`), all pushed.
  Sanitizing is a live discipline, not
  a formality: a prior wave's CLI caught + scrubbed a stray sibling-project name in a comment before its commit —
  it would have been the first private-project-name leak into the public repo. **Public repo = sanitize before
  every push** (pseudonymous TxVibeCoder — keep the real email / personal `C:\Users\…` paths / prior-project names
  out). `.gitignore` excludes `*.log` / `forge.log*` (the log sink never gets committed) and
  `wave-*-cli-prompts/` (the wave packets embed machine-local paths → stay local). History was rewritten once (a
  prior session) to scrub a stray path; `git-filter-repo` isn't on PATH, so run **`python -m git_filter_repo`** if
  ever needed (it drops the `origin` remote — re-add before pushing). Submodules are clean.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. An earlier session's **Session grid** landed a **clean first-try integration build** this way (2,920
  LOC, 18 files), because every load-bearing engine API was **source-verified before** the fan-out. **W7 this
  session** followed the same pattern — a frozen source-verified design
  ([devlog/midi-record-design.md](devlog/midi-record-design.md)) → file-disjoint build waves (record layer /
  session seam / pad state / session view) → orchestrator integration + the single build.
- **Adversarial verify waves** (independent skeptics, default-refuted, evidence-required) — high ROI for
  anything that can't be runtime-confirmed here. Earlier sessions ran them on the SessionView **design**, the
  **QC** (12 confirmed, 3 refuted), and a **fix re-verify**. **W03's QC** (18 agents, 4 dimensions ×
  per-finding skeptics) confirmed 9 real findings including a deterministic UAF and an unwired UI seam the
  gates could not see, and one pre-build skeptic refuted + corrected the sync recipe's device handling before
  any code existed. They earn their keep.
- **Model tiering (established W03):** match sub-agent models/effort to task complexity — top tier on UI work
  (Fable holds design authority) and on verification/QC (where wrongness is expensive), mid tier on
  recipe-driven C++ against frozen contracts, small models on docs drafting and mechanical sweeps.
- **Log fallible seams as you build them — STANDING BUILD PRINCIPLE (established in the logging session).** Every new feature
  routes its failure paths through `src/core/Log.*` (`FORGE_LOG_ERROR/WARN/INFO/DEBUG`) — **never** on the
  audio/RT thread, **never** per-tick in a poll/paint, autosave only on `save() == false`. The principle +
  cheat-sheet + a new-feature checklist live in [LOGGING.md](LOGGING.md); read it before adding a feature.
- **Multi-CLI parallel waves — the scaled build pattern (established this session).** For a file-disjoint feature
  set, the orchestrator writes per-CLI **handoff packets** in `wave-<N>-cli-prompts/` (gitignored — they embed
  local paths): a `README` control doc + one self-contained `P#-<slug>.md` per CLI (its territory, "take-as-given"
  facts, *propose-don't-edit* for shared files, scoped commits, and **CLIs do NOT build**) + a `P#-consolidation`
  packet for the orchestrator role (owns `main.cpp`/`CMakeLists`/`ProjectSession`; runs the single build + the
  five-selftest floor + adversarial QC + docs + sanitize + push). Full rule: **`CLAUDE.md` → Wave Orchestration
  Rule**. **Wave 01 shipped six features this way** (~35 min of parallel build vs. ~2 h serial); the CLAUDE.md
  nested-comment gotcha caught its own predicted bug at consolidation, and QC caught two integration UAFs no
  single CLI could see. No `.wave-active` sentinel (Forge has no auto-sync) — the load-bearing guard is **scoped
  commits** (`git add -- <paths>` **and** `git commit -- <paths>`, never `-A`).

---

## Open decisions (waiting on you)

- **Session grid layout — RESOLVED + BUILT (vertical scroll).** No longer open: vertical scroll with fixed
  ~46 px pads shipped (a prior session, `8d15234` + pushed); see
  [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Double-click edit gesture** — currently double-click opens a clip AND launches it (first press launches);
  right-click "Edit clip" is the launch-free path. Kept as belt-and-suspenders this session; revisit if the
  double-launch bothers you.
- **The control-bar "Editor" button** — third view, drawer toggle, or drop it? Now a **Fable design-authority
  call** slated for the W04 menu-bar/layout work (preserved as open in INTERFACE.md).
- **`INTERFACE.md` body — RESOLVED (W03):** rewritten Session-first with the design charter + the W04 UX
  charter; the superseded-banner is gone.
- **Ableton Link — vendoring decision.** The engine wrapper is ready but compiled out, and the Link library
  is NOT in the repo. Enabling = vendor `github.com/Ableton/link` (+ asio-standalone) + an AGPLv3
  license-compatibility review + two CMake lines. Default: deferred until the maintainer approves the new
  dependency.
- **Mockup refinements** — likely incoming (Session footer mixer, hard renumber 00→01, geometry tweaks).
- **Controllers — maintainer has NONE (stated 2026-07-01).** Hardware smoke items are parked, not queued; a
  controller in hand would unblock the Launchpad byte-mapping test and the physical-CC MIDI-learn path.

---

## Key references

- **[../CLAUDE.md](../CLAUDE.md)** — the working agreement: tenets, engineering principles, build discipline, the
  Forge gotchas, and the multi-CLI **Wave Orchestration Rule** (auto-loaded by Claude Code; linked here for humans).
- **[DIRECTION.md](DIRECTION.md)** — the authoritative product brief (read first).
- [STATUS.md](STATUS.md) — living roadmap. · [../mockups/](../mockups/) — the UI mockup set (sheet 00 = the target).
- [INTERFACE.md](INTERFACE.md) — the Session-first UI plan + design charter + the W04 UX charter (rewritten in W03).
- [devlog/wave-03-features.md](devlog/wave-03-features.md) — **W03: automation · MIDI-clock out · async LUFS · live refresh (this session)**.
- [devlog/wave-02-features.md](devlog/wave-02-features.md) — W02: MIDI-learn HW routing · control surface · offline LUFS (prior session).
- [devlog/wave-01-features.md](devlog/wave-01-features.md) — Wave 01: the six parallel feature seams (prior session).
- [devlog/midi-record-design.md](devlog/midi-record-design.md) — the W7 MIDI-slot-record design + the frozen recipe.
- [devlog/session-design.md](devlog/session-design.md) — the Session-grid design + build-wave record (earlier session).
- [devlog/session-scroll-design.md](devlog/session-scroll-design.md) — the vertical-scroll design (prior session).
- [LOGGING.md](LOGGING.md) + [devlog/logging-design.md](devlog/logging-design.md) — the logging principle + subsystem design (prior session).
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the original W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
