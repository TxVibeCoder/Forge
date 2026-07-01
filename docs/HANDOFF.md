# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-07-01**, end of the
> **"Wave 01 — six parallel feature seams"** session (Forge's first multi-CLI wave).

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`**. **Wave 01 shipped this session: six file-disjoint feature CLIs (P1–P6) each landed a scoped commit,
and the orchestrator (P7) consolidation + the `--selftest-midilearn` gate are COMMITTED and PUSHED to
`origin/main` — **current tip `e3b8c7c`**; working tree clean, sanitized before the push. Baseline before the
wave was `6100fb9`.** Last
build **clean** (MSVC Debug, 0 warnings) · **all five selftests PASS** — `--selftest`, `--selftest-record`,
`--selftest-session`, `--selftest-midi`, `--selftest-midilearn` — on the final binary; `--screenshot` renders. Six features shipped:
**metronome + count-in, MIDI-learn, buses/sends (aux A/B), async export + progress, markers, anti-click
edge-fade** — full record in [devlog/wave-01-features.md](devlog/wave-01-features.md).

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

## What this session did — Wave 01: six parallel feature seams

Forge's **first flat parallel multi-CLI wave**: six file-disjoint feature CLIs (P1–P6) built against
contract-first seams and each committed a scoped commit, then the orchestrator (P7) implemented the two shared
`ProjectSession` seams, wired everything into the single integration build, and ran adversarial QC. All
**built + verified + COMMITTED (tip `e3b8c7c`) + PUSHED** to `origin/main` (working tree clean;
sanitized before the push). Full record: [devlog/wave-01-features.md](devlog/wave-01-features.md).

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
(with vertical scroll) + the **logging subsystem** + the **Wave-01 feature seams**, all shipped, building clean,
all five selftests PASS:

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
  mapping store — hardware-CC routing deferred), and an automatic **anti-click edge fade** on imported audio.

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

> Wave 01 (six feature seams + the `--selftest-midilearn` gate) is **committed (tip `e3b8c7c`) + pushed**
> (sanitized). The flagged
> next items are the manual GUI smoke pass of the new gestures, the **deferred Wave-01 follow-ups**, and then the
> control-surface layer.

1. **Manual GUI smoke pass — START HERE (functionally).** The one path that can't be driven headlessly here.
   Click through the Session grid live (launch a pad / a scene / right-click "Edit clip", scroll to scenes
   10–16), the MIDI draw→play + slot-record gesture, **and the new Wave-01 gestures** — the TransportBar **Click**
   toggle + count-in selector; **markers** (left-click the marker bar to add, drag to move, double-click to
   rename, click to jump); mixer **aux sends** (drag an A/B send knob, click "＋ Enable" on a return); the
   **async export** progress/cancel dialog (Export a longer edit); and **Ctrl+L** MIDI-learn (pick a plugin
   param). `--screenshot` covers rendering and the five selftests cover the headless paths, but a human should
   click these once.
2. **Deferred Wave-01 follow-ups (ticketed).** (a) **MIDI-learn hardware routing** — install a focused-edit
   `ForgeUIBehaviour` (or a Forge MIDI-input listener) so real controller CCs reach
   `MidiLearn::handleIncomingController`; today Ctrl+L **arms** a learn (proven by `--selftest-midilearn`) but
   completion from real hardware awaits this. (b) **Audio-slot-record edge-fade** — wire `ClipFades` into a
   future audio slot-record commit (today's slot record is MIDI-only, where the fade is a no-op). See
   [devlog/wave-01-features.md](devlog/wave-01-features.md) "Deferred follow-ups".
3. **Remaining MIDI input roles** — **MIDI-clock / Ableton Link** sync. (MIDI-learn param mapping shipped as
   Wave-01 P2, minus the deferred hardware-routing follow-up above.)
4. **Control-surface layer ("one day") — the next real feature build.** A device-agnostic driver on the
   `ControlSurface` seam so real grid controllers drive the grid (Launchpad first, then APC40 mkII). The
   on-screen pad model is already hardware-ready: `SlotVisualState::toPadFeedback` emits the exact
   `(colourIdx, state)` LED encoding a driver would push. External hardware over MIDI; not an MVP gate.
   Reference: [mockups](../mockups/) sheet 09.
5. **Carried-over polish** — automation (vol/pan/plugin-param) lanes; **LUFS** metering; comping; macOS build;
   interactive-UI verification. (Buses/sends, async export + progress, and markers **shipped in Wave 01**.)

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
  selftest hook (like `--selftest-session`) to *exercise* it. Live mouse interaction still needs a manual pass.
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
- **Nested block comments corrupt doc comments (this session's bit).** A `/* … */` block comment placed
  **inside** a `/** … */` doc comment closes the doc comment early — the first `*/` ends it, and everything
  after (up to the real close) leaks into the code stream and corrupts the following declarations. Bit
  `RecordController.h` during W7; fixed by not nesting `/* */` inside `/** */`. Use `//` for inline notes inside
  a doc comment.
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
- **Scrolled-viewport relayout (this session's bug):** for a `juce::Viewport`, the **viewed component's
  top-left IS the scroll offset** — so in `SessionView::resized()` size the scrolled `columnHolder` with
  `setSize(w, h)`, **never** `setBounds(0, 0, w, h)` (the latter yanks the grid back to the top on any relayout
  while scrolled). The pinned scene column is offset by `-getViewPositionY()` in `syncSceneColumnToScroll()`.
- **CriticalSection nested-lock type:** the logging file sink guards with a `juce::CriticalSection`; to take it
  use **`juce::CriticalSection::ScopedLockType`** (i.e. `ScopedLock`), NOT a bare `juce::ScopedLock` templated
  wrongly — get the member type from the lock. (Never log from the audio/RT thread regardless — see LOGGING.md.)
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake. (And a quoted
  `"C:\Program Files\..."` path in the same command as `Remove-Item` can trip the sandbox guard — split them.)
- **Latest work IS committed + pushed.** Wave 01 (six feature seams + the `--selftest-midilearn` gate) is at
  `origin/main @ e3b8c7c`; the working tree is **clean** and the pushed set was **sanitized** — and this is a
  live discipline, not a formality: the **P6 CLI caught + scrubbed a stray "ClipKit" sibling-project name in a
  comment before its commit**, which would have been the first private-project-name leak into the public repo.
  **Public repo = sanitize before every push** (pseudonymous TxVibeCoder — keep the real email / personal
  `C:\Users\…` paths / prior-project names out). `.gitignore` excludes `*.log` / `forge.log*` (the log sink never
  gets committed) and `wave-*-cli-prompts/` (the wave packets embed machine-local paths → stay local). History
  was rewritten once (a prior session) to scrub a stray path; `git-filter-repo` isn't on PATH, so run
  **`python -m git_filter_repo`** if ever needed (it drops the `origin` remote — re-add before pushing).
  Submodules are clean.

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
  **QC** (12 confirmed, 3 refuted), and a **fix re-verify**. **W7's QC** found 0 blocker/major and 2 minor
  (swapProject MIDI-teardown; slot-arm error-message fallback) — both fixed — and the new `--selftest-midi`
  gate is the empirical proof of the verdict-A record path. They earn their keep.
- **Log fallible seams as you build them — STANDING BUILD PRINCIPLE (new this session).** Every new feature
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
  ~46 px pads shipped this session (built + verified, committed as `8d15234` + pushed). See "What this session
  did" #1.
- **Double-click edit gesture** — currently double-click opens a clip AND launches it (first press launches);
  right-click "Edit clip" is the launch-free path. Kept as belt-and-suspenders this session; revisit if the
  double-launch bothers you.
- **The control-bar "Editor" button** — third view, drawer toggle, or drop it? (Unresolved across the mockups.)
- **`INTERFACE.md` body** — still the old arrangement-first 7-phase UI plan (banner-flagged as superseded;
  DIRECTION.md wins). A full Session-first rewrite is queued but not done.
- **Mockup refinements** — likely incoming (Session footer mixer, hard renumber 00→01, geometry tweaks).
- **Which controllers you actually own** — affects which control-surface driver to build first.

---

## Key references

- **[../CLAUDE.md](../CLAUDE.md)** — the working agreement: tenets, engineering principles, build discipline, the
  Forge gotchas, and the multi-CLI **Wave Orchestration Rule** (auto-loaded by Claude Code; linked here for humans).
- **[DIRECTION.md](DIRECTION.md)** — the authoritative product brief (read first).
- [STATUS.md](STATUS.md) — living roadmap. · [../mockups/](../mockups/) — the UI mockup set (sheet 00 = the target).
- [devlog/midi-record-design.md](devlog/midi-record-design.md) — **the W7 MIDI-slot-record design + the frozen recipe (this session)**.
- [devlog/session-design.md](devlog/session-design.md) — the Session-grid design + build-wave record (earlier session).
- [devlog/session-scroll-design.md](devlog/session-scroll-design.md) — the vertical-scroll design (prior session).
- [LOGGING.md](LOGGING.md) + [devlog/logging-design.md](devlog/logging-design.md) — the logging principle + subsystem design (prior session).
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the original W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
