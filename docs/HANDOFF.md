# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-07-01**, end of the
> **"W7 — MIDI record into Session clip slots"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`** (`origin/main` @ `160f6cc` + this docs commit). **This session's feature — W7 MIDI record into
Session clip slots — is COMMITTED as `160f6cc` and PUSHED to `origin/main` (working tree clean; sanitized
before the push; previous tip was `2589c42`).** Last build **clean** (MSVC Debug, 0 warnings) · **all four
selftests PASS** — `--selftest`, `--selftest-record`, `--selftest-session`, `--selftest-midi` — on the final
binary.

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

## What this session did — W7: MIDI record into Session clip slots

One feature shipped this session. It's **built + verified, and now COMMITTED (`160f6cc`) + PUSHED** to
`origin/main` (working tree clean; sanitized before the push — see "Gotchas").

**W7 — MIDI record into Session clip slots.** A track can now be **MIDI record-armed** (the Session arm button
branches MIDI vs audio), and an **empty slot on a MIDI-armed track** can be captured via **Ctrl+Enter** or a
right-click **"Record into slot"** item → a live-played MIDI loop is recorded straight into that slot as a new
**born-audible (4OSC) `te::MidiClip`**. Recording is **transport-driven** (`transport.record()`), **NOT**
launch-driven — the armed slot's `itemID` is the record target, and on stop the engine materialises the clip in
the slot. This is **verdict (A): direct `ClipSlot` recording**, now **empirically proven** (the design had it
"untested in-engine"). Frozen design record: [devlog/midi-record-design.md](devlog/midi-record-design.md).

- **Record layer** (`src/engine/RecordController.{h,cpp}`): new MIDI methods — `enableMidiInputs`,
  `armFirstMidiInputToSlot` / `…ToTrack`, `disarmSlot` / `disarmMidiTrack`,
  `isSlotMidiArmed` / `isTrackMidiArmed` — with **its own MIDI enable sequence** (`getMidiInDevices` +
  `setMonitorMode(automatic)` + `rescanMidiDeviceList`), **not** a clone of the wave path.
- **Session record seam** (`src/services/files/ProjectSession.{h,cpp}`): `recordArmSlot` / `beginSlotRecord` /
  `commitSlotRecord` / `isSlotRecording`, delegating to the recorder via injected `std::function`s (no hard
  `RecordController` dependency). Slot capture is **slot-ONLY** — it drops any track record target first so
  notes never double-capture to the arrangement. Audio/MIDI arm are **mutually exclusive per track** (v1).
- **Pad state** (`src/ui/session/SlotVisualState.h` + `ClipSlotComponent.cpp`): new `recording` state (pulsing
  red), dominating all clip/queue states for the one capturing pad.
- **Headless gate** (NEW): `--selftest-midi` creates a `VirtualMidiInputDevice`, arms slot (0,0), rolls the
  transport, injects 4 synthetic notes, and asserts the slot's committed clip holds **exactly** those notes
  (`capturedNoteCount == notesInjected`, `preExistingNotes==0`) → **PASS**.
- **QC:** adversarial pass found 0 blocker/major, 2 minor (swapProject MIDI-teardown; slot-arm error-message
  fallback) — both fixed.

> Prior session (published at `8d15234`, the tip this session built on): **Session-grid vertical scroll +
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
(with vertical scroll) + the **logging subsystem**, all shipped, building clean, all four selftests PASS:

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
- **Mixer** (strips, plugin inserts w/ bypass+reorder, master + post-fader meter), **plugin hosting**
  (built-in + VST3/AU scan + floating editors), **Browser**, **clip Inspector**, **WAV export + stems**.

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

> This session's work (**W7 MIDI record into slots**) is already **committed (`160f6cc`) + pushed** (sanitized)
> — the commit + push step is done, so the flagged next items are the manual GUI smoke pass and then the
> remaining MIDI-input roles + the control-surface layer.

1. **Manual GUI smoke pass — START HERE (functionally).** The one path that can't be driven headlessly here.
   Click through the Session grid live (launch a pad / a scene / the right-click "Edit clip" + double-click
   gestures, scroll to scenes 10–16), the MIDI MVP draw→play path, **and the new W7 slot-record gesture** —
   MIDI-arm a track, Ctrl+Enter (or right-click "Record into slot") on an empty slot, play a few notes, stop,
   confirm the born-audible clip lands in the slot and plays back. `--screenshot` covers rendering and
   `--selftest-session` / `--selftest-midi` cover audibility + capture, but a human should click it once with a
   real controller.
2. **Remaining MIDI input roles** — **MIDI-learn** param mapping; **MIDI-clock / Ableton Link** sync. (W7
   note-record into slots is now **DONE** — see "What this session did".)
3. **Control-surface layer ("one day") — the next real feature build.** A device-agnostic driver on the
   `ControlSurface` seam so real grid controllers drive the grid (Launchpad first, then APC40 mkII). The
   on-screen pad model is already hardware-ready: `SlotVisualState::toPadFeedback` emits the exact
   `(colourIdx, state)` LED encoding a driver would push. External hardware over MIDI; not an MVP gate.
   Reference: [mockups](../mockups/) sheet 09.
4. **Carried-over polish** — automation (vol/pan/plugin-param) + buses/sends; async export + progress; LUFS;
   markers; comping; macOS build; interactive-UI verification.

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
- **This session IS committed + pushed.** W7 (MIDI record into Session slots) is committed as `160f6cc` and
  pushed to `origin/main` (previous tip `2589c42`); the working tree is **clean** and the pushed set was
  **sanitized** (a privacy scan found no real email / personal `C:\Users\…` paths / private side-project names).
  **Public repo = sanitize before every push** (pseudonymous TxVibeCoder — keep the real email / personal
  `C:\Users\…` paths / prior-project names
  out; note `.gitignore` now excludes `*.log` / `forge.log*` so the log sink never gets committed). History was
  rewritten once (a prior session) to scrub a stray path; `git-filter-repo` isn't on PATH here, so run
  **`python -m git_filter_repo`** if it's ever needed again (it drops the `origin` remote as a safety step —
  re-add it before pushing). Submodules are clean.

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
