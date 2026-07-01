# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-06-30**, end of the
> **"Session clip-launch grid — build, QC, publish"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`**, pushed to **`origin/main` @ `06f3cf6`** (the Session grid is merged + live) · working tree
**clean** · ~8,400 lines / 43 source files · last build **clean** · **all four self-tests PASS**
(`--selftest`, `--selftest-session`, `--selftest-record`).

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

## What this session did — built the Session clip-launch grid

The pivot from DIRECTION.md is **built, integrated, adversarially QC'd, verified, and published to `main`**.
SessionView is the new default view (`origin/main` tip `06f3cf6`).

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
7. **Committed, docs updated, merged + pushed.** `feat` + `docs` commits; STATUS / HANDOFF / README brought
   current, then merged to `main` and pushed. **Sanitized first** (pseudonymous public repo): author identity
   is the `TxVibeCoder` noreply, a token scan + a 3-agent semantic audit came back clean, and a stray absolute
   `C:\Users\…` build path in HANDOFF was scrubbed from ALL git history (`git filter-repo`, run as
   `python -m git_filter_repo`) then force-pushed.
8. **16-row layout — DECIDED, not yet built.** 16 scene rows at a readable pad height (~46 px) exceed a normal
   window (~844 px of content). **Decision: vertical scroll** (keep full-size pads, Ableton-style) over
   fit-to-window (shrink pads). Deferred by request — the implementation note is in "What's next" #1.

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

1. **Vertical scroll for the 16-scene grid — DECIDED, START HERE.** The layout call is made (see "what this
   session did" #8): **vertical scroll** — keep full-size ~46 px pads and scroll to reach scenes 10–16 —
   NOT fit-to-window. Rationale: Ableton-idiomatic, pads stay readable, and it scales to 24/32/… scenes
   (fit-to-window shrinks pads toward unusable). **Implementation is isolated to `SessionView` (~a few hours):**
   - The grid `juce::Viewport` scrolls HORIZONTAL only today (`viewport.setScrollBarsShown(false, true)` in
     `SessionView.cpp`). Turn on vertical scrolling and let `columnHolder` keep its natural content height
     (`headerH + numScenes*kSlotH + stopRowH`, ~844 px) instead of the current clamp to the viewport height.
   - **The load-bearing bit:** the `SceneColumnComponent` is **pinned OUTSIDE the viewport**, so it won't move
     when the grid scrolls. Add a `juce::Viewport::Listener` (`visibleAreaChanged`) and offset the pinned scene
     column's Y to match `viewport.getViewPositionY()` so scene launch rows stay aligned with their pads.
     (Row *alignment within a frame* is already solved by the shared `SessionLayout::rowBand` — don't touch it.)
   - Decide whether the track **header + stop footer** stay pinned while pads scroll (Ableton pins the track
     row) or scroll with the column. Simplest first pass: scroll the whole column together, then refine.
   - Verify with a fresh `--screenshot` at a normal window height (~700 px): all 16 rows must be reachable and
     the scene column must track the pads while scrolling. Same file-disjoint-design-then-build pattern works,
     but this is small enough to do directly.
2. **Manual GUI smoke pass** — the one path that can't be driven headlessly here. Click through the Session
   grid live (launch a pad / a scene / the right-click "Edit clip" + double-click gestures) and the MIDI
   MVP draw→play path. `--screenshot` covers rendering and `--selftest-session` covers audibility, but a
   human should click it once.
3. **Control-surface layer ("one day") — the next real feature build.** A device-agnostic driver on the
   `ControlSurface` seam so real grid controllers drive the grid (Launchpad first, then APC40 mkII). The
   on-screen pad model is already hardware-ready: `SlotVisualState::toPadFeedback` emits the exact
   `(colourIdx, state)` LED encoding a driver would push. External hardware over MIDI; not an MVP gate.
   Reference: [mockups](../mockups/) sheet 09.
4. **MIDI input roles** — note-record into clips (**W7**: own MIDI enable sequence + a runtime test with a
   physical controller, see [midi-design.md §5](devlog/midi-design.md)); **MIDI-learn** param mapping;
   **MIDI-clock / Ableton Link** sync.
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
- **Submodules are clean. Published:** the Session grid is merged to `main` and pushed (`origin/main` @
  `06f3cf6`); working tree clean. **Public repo = sanitize before every push** (pseudonymous TxVibeCoder —
  keep the real email / personal `C:\Users\…` paths / prior-project names out). History was rewritten once
  this session to scrub a stray path; `git-filter-repo` isn't on PATH here, so run **`python -m git_filter_repo`**
  (it drops the `origin` remote as a safety step — re-add it before pushing).

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

- **Session grid layout — DECIDED (vertical scroll), implementation deferred.** No longer an open question;
  see "What's next" #1 for the how. Just not built yet (deferred by request).
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
