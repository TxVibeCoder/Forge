# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-06-30**, end of the
> **"direction reset + UI mockups + doc audit"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch `main` ·
last pushed `600f80f` (mockups) · **one local doc-audit commit ahead of `origin/main`** (`git status` for the
count) · ~6,460 lines / 33 source files · last build **clean** · both self-tests **PASS**.

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

## What this session did

1. **Direction reset → [DIRECTION.md](DIRECTION.md).** Captured the new identity (Session-first,
   controller-driven, Arrange secondary; full MIDI input) and **realigned the whole doc set** to it
   (STATUS / INTERFACE / ARCHITECTURE / FEATURE_CATALOG / this handoff), killing the old "arrangement-first /
   Session deferred" framing.
2. **Verified the engine supports all of it** (greps against `libs/tracktion_engine`): clip-launch
   (`ClipSlot` / `getClipSlotList()` / scenes / `LaunchHandle`), the **`ControlSurface`** framework (clip
   grid + `padStateChanged` LED feedback + faders / encoders / transport), `MidiLearn`, and `AbletonLink`.
3. **UI mockups** — a to-scale DXF/CAD set in [`mockups/`](../mockups/): **10 sheets** led by the **Session
   clip grid (00, primary)**, with a **controller hardware-mapping reference (09)** (Launchpad + APC40 mkII →
   engine; clearly labelled *reference, not an app screen*). Generator + previews + storyboard, committed.
4. **Full documentation audit** — brought the root README and mockups README fully current; reconciled all
   "what's next" to lead with the Session build.

> Prior session (already committed + pushed before this one): the **MIDI MVP (W1–W5)** + **W6 piano-roll
> polish** — draw a MIDI clip and hear it via a default 4OSC; velocity lane, multi-select, copy/paste. See
> [devlog/midi-build.md](devlog/midi-build.md).

---

## What exists today (the building blocks)

Phases 0–4 + startup-latency hardening + MIDI MVP + W6, all shipped, building clean, both selftests PASS:

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

1. **Session-grid build — THE pivot, start here.** A new `SessionView` clip grid as the **primary**
   `ViewMode` (`Session ∣ Arrange ∣ Mix`, Session default) on Tracktion's `ClipSlot` / scenes /
   `LaunchHandle`. Fully playable with mouse + keyboard. The target is [mockups](../mockups/) **sheet 00**.
   The shipped clips / 4OSC / piano-roll / mixer ride inside slots and scenes. **Recommended approach:** a
   design pass (understand → design → adversarial-verify, source-grounded against the `ClipSlot`/scene API),
   then a file-disjoint build wave — the project's proven pattern (see "how the work gets done").
2. **Control-surface layer ("one day").** A device-agnostic driver layer on the `ControlSurface` seam so
   real grid controllers drive the grid (Launchpad first, then APC40 mkII). Same pad-colour/state model as
   the on-screen grid. External hardware over MIDI; not an MVP gate. Reference: [mockups](../mockups/) sheet 09.
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

& ".\build\Forge_artefacts\Debug\Forge.exe"                   # the app
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest        # headless playback check → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-record # headless recording check → PASS/FAIL
# Both write %TEMP%\forge_phase0_selftest.log.  First clone: git submodule update --init --recursive
```

Regenerate the mockups (needs Docker; the `forge-dxf` image exists on this box):
```sh
cd mockups/src && MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" forge-dxf:latest python build.py
# then copy out/*.dxf → mockups/ and out/*.png → mockups/preview/   (src/out/ is gitignored)
```

---

## ⚠ Gotchas

- **GUI can't be driven headlessly here** — computer-use can't grab the dev-built `Forge.exe` window by
  name. The MIDI draw→play path and any new `SessionView` UI need a **manual** smoke pass, or a headless
  selftest hook.
- **Build file lock:** a running `Forge.exe` → `LNK1168` on the next build, and it can hold the WASAPI
  device. `Get-Process Forge | Stop-Process -Force` before building or runtime-testing; use a 45–90 s timeout.
- **Docker on this Windows box:** mount with the Windows path or Git Bash mangles it —
  `MSYS_NO_PATHCONV=1 docker run -v "$(pwd -W):/work" …`.
- **MIDI note beats are CONTENT-relative** (beat 0 = clip start at offset 0); always edit `getSequence()`,
  never `getSequenceLooped()`. The clip-launcher path is the `insertMIDIClip(ClipSlot&, …)` *free function*
  (the one the linear MVP deliberately avoided) — the Session build will use it.
- **Never arm recording synchronously in one blocking callback** — the device-list rebuild is async (the
  W7 MIDI-input arm must follow the same discipline).
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake.
- **Submodules are clean.** **Not fully pushed:** one local doc-audit commit is ahead of `origin/main`
  (`git push` when ready; `git status` shows the count).

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. This session's MIDI MVP landed a **clean first-try integration build** this way, because every
  load-bearing engine API was **source-verified before** launching the fan-out.
- **Adversarial verify waves** (independent skeptics, default-refuted, evidence-required) — high ROI for
  anything that can't be runtime-confirmed here. Run one on the `SessionView` design + build.

---

## Open decisions (waiting on you)

- **The control-bar "Editor" button** — third view, drawer toggle, or drop it? (Unresolved across the mockups.)
- **`INTERFACE.md` body** — still the old arrangement-first 7-phase UI plan (banner-flagged as superseded;
  DIRECTION.md wins). A full Session-first rewrite is queued but not done.
- **Mockup refinements** — likely incoming (Session footer mixer, hard renumber 00→01, geometry tweaks).
- **Which controllers you actually own** — affects which driver to build first.

---

## Key references

- **[DIRECTION.md](DIRECTION.md)** — the authoritative product brief (read first).
- [STATUS.md](STATUS.md) — living roadmap. · [../mockups/](../mockups/) — the UI mockup set (sheet 00 = the target).
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
