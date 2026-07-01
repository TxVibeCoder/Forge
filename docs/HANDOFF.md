# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-06-30**, end of the
> **"Session clip-launch grid — build + QC"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`session-grid`** (`main` last pushed `600f80f`) · **local commits ahead of `origin/main`, not yet pushed**
(`git log --oneline main..session-grid`) · ~8,400 lines / 43 source files · last build **clean** ·
**all four self-tests PASS** (`--selftest`, `--selftest-session`, `--selftest-record`).

---

## ⚠ READ THIS FIRST — what Forge is now

**Forge is a sample / scene-based, controller-driven DAW.** The **primary surface is an Ableton-style
Session clip grid** (tracks × scenes of launchable clips), meant to be played from real **grid controllers**
(Novation Launchpad, Akai APC40 mkII). The linear **Arrange** timeline is a **secondary** view.

This is a **direction reset** made this session. Everything built before it was *arrangement-first* — that
work is **not wasted** (clips, the 4OSC instrument, the piano-roll, the mixer, plugin hosting all become
building blocks that live *inside* slots and scenes), but the **primary identity and next build have
changed**. The authoritative brief is **[DIRECTION.md](DIRECTION.md)** — read it before planning anything.

**The controllers are EXTERNAL hardware** Forge connects to over MIDI — Forge does **not** draw a controller
on screen. The only on-screen surface is the Session grid. Hardware integration is a "hope to one day
connect" goal, **not an MVP gate**: the grid is fully playable with mouse + keyboard.

---

## What this session did — built the Session clip-launch grid

The pivot from DIRECTION.md is **built, integrated, adversarially QC'd, and verified**. SessionView is the
new default view. Committed on branch `session-grid` (`def1193` = the feature; a follow-up docs commit).

1. **Design pass (workflow).** understand → source-verify the `ClipSlot` / `Scene` / `LaunchHandle` API
   against `libs/tracktion_engine` → design → 4-lens adversarial verify → reconcile. Output:
   [devlog/session-design.md](devlog/session-design.md) (build-ready, every engine call cited to file:line).
2. **File-disjoint build (workflow).** 10 new files in [`src/ui/session/`](../src/ui/session/) authored by
   agents against fixed header contracts; the orchestrator did the `CMakeLists` / `main.cpp` / `ControlBar`
   wiring + the single clean integration build. All engine ops go through additive `ProjectSession` methods.
3. **Adversarial QC (workflow) + fixes.** A 5-lens QC (lifecycle/threading · engine-semantics · shell ·
   ui/playability · perf) confirmed **12 real issues** — a use-after-free blocker (a deleted track dangled a
   column's `AudioTrack&`), dead keyboard focus, a 25 Hz poll doing ~6,400 tree-walks/s, scene/pad row drift,
   a double-click that also launched — all fixed.
4. **Fix re-verify (workflow).** An independent pass re-checked all 12 fixes and **caught two regressions
   they introduced** (a too-short double-click window; a device-race in the playback selftest) — both fixed.
5. **Made it visible + audible headlessly.** New `--selftest-session` audibility gate (**PASS**) and a
   `--screenshot` mode that renders each view to PNG (the grid matches [mockups](../mockups/) sheet 00).
6. **Root-caused a playback-selftest failure (workflow)** that surfaced when the audio device hot-swapped
   (headset unplug → onboard fallback) — **test-only** fragility (the real Play button was fine); hardened
   the selftest to yield + wait-for-stream + bounded-poll. All four selftests now PASS.

> Prior session: **direction reset → DIRECTION.md** + the to-scale [mockups](../mockups/) (sheet 00 = the
> Session grid) + a full doc audit. Before that: the **MIDI MVP (W1–W5) + W6 piano-roll polish**
> ([devlog/midi-build.md](devlog/midi-build.md)) — clips / 4OSC / piano-roll now ride inside slots.

---

## What exists today (the building blocks)

Phases 0–4 + startup hardening + MIDI MVP/W6 + the **Session clip-launch grid**, all shipped, building clean,
all four selftests PASS:

- **Session grid (PRIMARY view)** — tracks × 16 scenes of launchable clips on `ClipSlot` / `Scene` /
  `LaunchHandle`; single-click launches (instant), right-click "Edit clip" (launch-free), double-click opens;
  keyboard arrows/Enter launch; **audible, bar-quantised** launch; pinned scene column + MASTER stop-all;
  25 Hz gated state poll. Default `ViewMode` (**F8**). Details: [devlog/session-design.md](devlog/session-design.md).
- **Project** save/load (`.tracktionedit`), **audio import**, an **arrange timeline** (waveforms, playhead,
  clip drag-to-move, selectable snap grid).
- **Transport** (play/stop/record/loop) and **recording** — verified end-to-end on real hardware
  (`--selftest-record` captures a real take); output-only startup, lazy capture-input open.
- **MIDI** — clips on any track, born audible via a default **4OSC** at chain index 0; a **piano-roll**
  (draw/move/resize/delete, velocity lane, multi-select, copy/paste).
- **Mixer** (strips, plugin inserts w/ bypass+reorder, master + post-fader meter), **plugin hosting**
  (built-in + VST3/AU scan + floating editors), **Browser**, **clip Inspector**, **WAV export + stems**.

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

1. **Session-grid build — ✅ DONE this session** (see above; `def1193`). Carried-over pieces: the
   **16-scene-rows-vs-window layout decision** (vertical scroll vs. shorter pads — the row *alignment* is
   fixed; this sizing choice is not), and a **manual GUI smoke pass** of live mouse interaction (rendering is
   already covered by `--screenshot`).
2. **Control-surface layer ("one day") — now the next real build.** A device-agnostic driver on the
   `ControlSurface` seam so real grid controllers drive the grid (Launchpad first, then APC40 mkII). The
   on-screen pad model is already hardware-ready: `SlotVisualState::toPadFeedback` emits the exact
   `(colourIdx, state)` LED encoding a driver would push. External hardware over MIDI; not an MVP gate.
   Reference: [mockups](../mockups/) sheet 09.
3. **MIDI input roles** — note-record into clips (**W7**: own MIDI enable sequence + a runtime test with a
   physical controller, see [midi-design.md §5](devlog/midi-design.md)); **MIDI-learn** param mapping;
   **MIDI-clock / Ableton Link** sync.
4. **GUI smoke test of the MIDI MVP + W6** — the one statically-verified-but-unclicked path (draw → play →
   hear; multi-select / velocity / copy-paste). Worth a manual pass.
5. **Carried-over polish** — automation (vol/pan/plugin-param) + buses/sends; async export + progress; LUFS;
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
- **SessionView threading (load-bearing):** pads cache NO `te::ClipSlot*`/`Clip*` — only `(track,scene)`
  indices; the 25 Hz poll re-resolves via the **const** `getClipSlot` (never inserts). Any track-list change
  must rebuild the grid before a stale `TrackColumnComponent` derefs its `AudioTrack&` (the QC blocker).
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake. (And a quoted
  `"C:\Program Files\..."` path in the same command as `Remove-Item` can trip the sandbox guard — split them.)
- **Submodules are clean.** **Not pushed:** the Session-grid work is on branch **`session-grid`**, several
  commits ahead of `origin/main` (`git log --oneline main..session-grid`). `git push -u origin session-grid`
  (or merge to `main`) when ready.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. This session's **Session grid** landed a **clean first-try integration build** this way (2,920 LOC,
  18 files), because every load-bearing engine API was **source-verified before** the fan-out.
- **Adversarial verify waves** (independent skeptics, default-refuted, evidence-required) — high ROI for
  anything that can't be runtime-confirmed here. This session ran them on the SessionView **design**, the
  **QC** (12 confirmed, 3 refuted), and a **fix re-verify** — the last one caught two regressions the fixes
  themselves introduced. Also a **root-cause workflow** for the playback-selftest failure. They earn their keep.

---

## Open decisions (waiting on you)

- **Session grid: 16 scene rows don't fit a short window** — vertical scroll (keep big pads) vs.
  fit-to-window (shorter pads)? The row *alignment* is fixed; this sizing choice is not.
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
- [devlog/session-design.md](devlog/session-design.md) — **the Session-grid design + build-wave record (this session)**.
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
