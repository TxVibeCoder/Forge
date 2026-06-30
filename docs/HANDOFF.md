# Forge — Session Handoff

> Living handoff for picking the project back up cold. Pairs with [STATUS.md](STATUS.md) (the living
> roadmap). Last updated: **2026-06-30**, end of the **"MIDI MVP + W6 polish"** session.

> ## ⚠ READ FIRST — direction reset (2026-06-30)
> Forge's primary identity changed: it is a **sample / scene-based DAW** — an Ableton-style **Session clip
> grid** as the **primary** surface, **played from grid controllers** (Novation Launchpad / Akai APC40 mkII),
> with the linear **Arrange** view **secondary**. Plus full MIDI input (note-in/record, MIDI-learn,
> clock/Ableton Link). **Everything below this banner was built arrangement-first** and is still valid as
> reusable building blocks, but the *primary surface* is now the Session grid + a control-surface layer.
> **The authoritative brief is [DIRECTION.md](DIRECTION.md) — read it before planning new work.** The next
> build is the Session grid (`SessionView` + Tracktion `ClipSlot`/scenes) and the device-agnostic
> control-surface layer (on Tracktion's `ControlSurface` seam), not more linear-timeline features. A
> to-scale, Session-first UI mockup set is in [`mockups/`](../mockups/) (open `preview/forge-ui-storyboard.png`).

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) ·
branch `main` · MIDI MVP commit `9a24989` · **ahead of `origin/main` — NOT pushed** (this session's MIDI
MVP + docs, atop the earlier recording/design commits; `git status` for the exact count) ·
~5,900 lines / 31 source files · last build **clean** · both self-tests **PASS** · verify wave **clean**.

Forge is a **scene-based DAW** (Session clip grid primary, controller-driven — see [DIRECTION.md](DIRECTION.md)) on **JUCE + Tracktion Engine** (C++20, Windows-verified). Phases 0–4 +
startup-latency hardening + the **MIDI MVP + W6 piano-roll polish** are done and live: project/arrange/
transport/mixer/plugins/browser/inspector/export, output-only startup with lazy record-input open, and
**drawable, audible MIDI clips with a velocity lane, multi-select, and copy/paste**. See
[STATUS.md §2](STATUS.md) for the full feature list.

---

## What this session did

### MIDI tracks + piano-roll — MVP BUILT ✅ (`9a24989`)

Built the source-verified design ([midi-design.md](devlog/midi-design.md)) as the audible-MIDI slice.
**Outcome:** right-click an empty lane area → **New MIDI Clip** → a `te::MidiClip` is created on that
track, **born audible** via an auto-inserted **4OSC** at plugin-chain **index 0**, and the **piano-roll**
opens in the bottom drawer ready to draw. Draw / move / resize / delete notes → **play → hear it**. No
recording code (MIDI-input record is the later W7). Full per-wave record in
[devlog/midi-build.md](devlog/midi-build.md).

- **How:** a single **file-disjoint Workflow fan-out** — four authoring agents (W1–W4) with exclusive
  file ownership, additive-only interfaces, contract-first seams, run in parallel; the **orchestrator
  alone** owned `CMakeLists.txt` + `main.cpp` and did the **one integration build**. Every load-bearing
  engine API was source-verified **before** launch, so the agents started from facts — the integration
  build was **clean on the first try**.
- **W1** `PluginHost`: `ensureDefaultInstrument` (idempotent 4OSC at chain head, **own insert-at-0 path**,
  not the volume-index effect path) + `addInstrumentToTrack`; `makeBuiltIn` category parameterized.
- **W2** `ProjectSession::createMidiClip`: the **AudioTrack-member** `insertMIDIClip(name,range,nullptr)`
  (returns `MidiClip::Ptr` directly; dodges the `insertMIDIClip(ClipOwner&)` free-fn name collision) +
  `ensureDefaultInstrument`. Range in SECONDS; notes in BEATS.
- **W3** `ArrangeView`: base **`ClipComponent`** extracted; `AudioClipComponent` + new
  **`MidiClipComponent`** derive from it; `rebuildClips` branches by `dynamic_cast<te::MidiClip*>`; the
  six clip callbacks re-typed `AudioClipComponent&`→`ClipComponent&`; **"New MIDI Clip"** menus →
  `onCreateMidiClipRequested`. Wave-clip behaviour preserved byte-for-byte (verified against `git HEAD`).
- **W4** `src/ui/pianoroll/*` (new): `PianoRollView(TimelineView&)` (shared time axis, mandatory
  `juce::Viewport` for 128 pitch rows, keybed gutter) + `MidiNoteComponent`. All edits go to the **live**
  `getSequence()` (never the looped copy) with `&clip->edit.getUndoManager()`. Content-relative beats.
- **W5** `main.cpp` + CMake: selection routes a `MidiClip`→piano-roll, any other clip→DetailView, via a
  `bottomMode` drawer that swaps editors in `resized()`; `onCreateMidiClipRequested`→`createMidiClip`
  (builds a 16-beat range, opens the roll on the new clip); `pianoRoll.onEditMutated`→save; project-swap
  drops the held clip safely.

**Deviations (all confirmed improvements):** W3 maps the note preview via the engine's
`getTimeOfContentBeat` (`te::Clip` has no clip-level `getStartBeat`); W2 keeps `createMidiClip` inlined to
hold the `AudioTrack*` for `ensureDefaultInstrument`; W4 delete is right-click-only + selection is
visual-only (Delete-key + multi-select are W6).

### Verification

- **Build:** clean first-try integration build (6 TUs recompiled + linked, 0 errors).
- **Selftests (no regression):** `--selftest` (playback) **PASS**; `--selftest-record` **PASS**
  (`recordedPeakMagnitude≈0.68` — real signal). The new wiring never touches the record/playback paths.
- **Adversarial verify wave** (3 skeptic agents over W3/W4/W5, default-refuted, evidence-required,
  read-only): **all three `correct`, zero blocker/major/minor findings.** Two highest-risk items cleared
  against engine source — (1) **`MidiNote&` lifetime:** `setStartAndLength`/`setNoteNumber` don't free the
  note, and right-click-delete is safe via JUCE's `HierarchyChecker::shouldBailOut()`; (2)
  **instrument-at-0:** the detection loop can't false-positive on the vol/meter tail, so a real 4OSC is
  always inserted and re-create can't stack synths.

### W6 — piano-roll polish ✅ (`bb5b6bf`)

Post-MVP editing, all inside `src/ui/pianoroll/` (one cohesive owner → a single authoring agent, not a
fan-out): **multi-select** (click / Shift-Ctrl-click / marquee — a plain click still draws a note),
**Delete key**, **multi-note move** (whole selection by one delta, **group-clamped** so chords keep their
shape at the beat-0 / pitch edges), **copy/paste** (Ctrl+C/V at the playhead, auto-selected), and a
**velocity lane** (new `VelocityLane.{h,cpp}` — draggable bar per note, top = loud) + velocity shading on
notes. The roll grabs keyboard focus on any interaction but `keyPressed` returns `false` for keys it
doesn't consume, so the shell shortcuts (Space/R/Ctrl+S) still work; the four shell-facing symbols are
unchanged (only CMake gained `VelocityLane.cpp`). Build clean; both selftests **PASS**. The verify wave (2
skeptics) returned `correct` on velocity/layout and caught two real low-severity issues, **both fixed** (the
multi-move group-clamp; a velocity-lane focus grab).

---

## ⚠️ The one thing not yet verified

**The live GUI draw→play→hear path has NOT been exercised in a running window** — the dev-built
`Forge.exe` can't be GUI-driven headlessly here. Everything is statically verified (build + selftests +
adversarial trace), but the very first manual action should be a **GUI smoke test**:

1. Run `Forge.exe`. Right-click the empty area of a track lane → **New MIDI Clip**.
2. The piano-roll should open in the bottom drawer. Click the grid to draw a few notes; drag to move/resize;
   right-click a note to delete.
3. **W6:** Shift/Ctrl-click or marquee-drag to multi-select; drag the velocity-lane bars (bottom strip);
   **Ctrl+C / Ctrl+V**; **Delete** the selection. Confirm **Space** still plays while the roll has focus.
4. Press **Space** → the notes should sound through the 4OSC. Save, reopen, confirm the clip persists.

If anything is off, the per-wave detail is in [devlog/midi-build.md](devlog/midi-build.md) and the
coordinate math + interaction logic live entirely in `src/ui/pianoroll/`.

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

Needs VS2022 / MSVC v143 (C++20). First clone: `git submodule update --init --recursive`. Both selftests
PASS on this box.

---

## ⚠️ Gotchas

- **GUI not auto-verified (above).** The MIDI draw→play path needs a manual smoke pass; computer-use can't
  grab the dev-built `Forge.exe` window by name (it's not a Start-menu app).
- **Build file lock:** a running `Forge.exe` → `LNK1168` on the next build. `Get-Process Forge | Stop-Process
  -Force` first. Also kill it before runtime tests (a killed instance can still briefly hold the WASAPI output
  device); a 45–90 s timeout is prudent.
- **Never arm recording synchronously in one blocking callback** — the wave-device-list rebuild is async
  (carry-forward from the recording session). MIDI-**input** recording (W7) must follow the same discipline.
- **MIDI note beats are CONTENT-relative** (beat 0 = clip start at offset 0). The piano-roll's `beatToX`/
  `xToBeat` ignore clip offset — fine for the MVP (created clips have offset 0); revisit if MIDI clips ever
  gain a non-zero offset. **Always edit `getSequence()`, never `getSequenceLooped()`** (its edits are dropped).
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake.
- **Submodules are clean.** Don't be surprised by a clean `git submodule status`.
- **Not pushed:** `main` is ahead of `origin/main` (this session's MIDI MVP `9a24989` + W6 `bb5b6bf` +
  docs, atop the earlier recording/design commits). `git push` when ready; `git status` shows the count.

---

## What's next (prioritized)

> **Direction reset this session** (see the banner + [DIRECTION.md](DIRECTION.md)). The next build is the
> **Session clip grid**, not more linear-timeline features.

1. **Session-grid build — the pivot.** A new `SessionView` clip grid as the **primary** `ViewMode`
   (`Session ∣ Arrange ∣ Mix`, Session default), on Tracktion's `ClipSlot` / `getClipSlotList()` / scenes /
   `LaunchHandle`. Fully playable with mouse + keyboard. Start from [DIRECTION.md](DIRECTION.md); the
   [mockups](../mockups/) sheet **00** shows the target. The shipped clips / 4OSC / piano-roll / mixer ride
   inside slots and scenes.
2. **Control-surface layer ("one day").** A device-agnostic driver layer on Tracktion's `ControlSurface`
   seam so **real** grid controllers (Launchpad, then APC40 mkII) drive the grid; the same pad-colour/state
   model feeds the on-screen grid and the hardware LEDs. **Not an MVP gate** — the controllers are external
   hardware (mockups sheet **09** is the mapping reference, not an app screen).
3. **MIDI input roles.** Note-record into clips (**W7** — its own MIDI enable sequence + a
   physical-controller runtime test, see [midi-design.md §5](devlog/midi-design.md)); MIDI-learn param
   mapping (`MidiLearn`); MIDI-clock / Ableton Link sync.
4. **GUI smoke test of the MIDI MVP + W6** — the one statically-verified-but-unclicked path (draw → play →
   hear; multi-select / velocity / copy-paste). Worth a manual pass before building atop it.
5. **Carried-over polish** — automation (vol/pan/plugin-param) + buses/sends in the mixer; async export +
   progress; LUFS; markers; comping; off-thread record-input open; macOS build; interactive-UI verification.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. This session: 4 authoring agents → one clean first-try build. **Source-verify every load-bearing
  API before launching the fan-out** (it's what made the integration clean).
- **Adversarial verify waves** (independent skeptic-verify, default-refuted, evidence-required) — extremely
  high ROI. This session's wave traced the two scariest correctness questions (note-pointer lifetime,
  instrument audibility) to a confident *correct* against engine source. **Run one on any change you can't
  runtime-confirm here.**

---

## Key references

- [STATUS.md](STATUS.md) — living roadmap (refreshed this session).
- [docs/devlog/midi-build.md](devlog/midi-build.md) — **this session's** wave-by-wave MIDI MVP build record
  (deviations + verify results).
- [docs/devlog/midi-design.md](devlog/midi-design.md) — the build-ready MIDI design + 7-wave plan (read
  before W6/W7).
- [docs/devlog/device-recording.md](devlog/device-recording.md) — recording root cause + device-pairing nuance.
- [docs/devlog/integration.md](devlog/integration.md) — earlier orchestrator wave-by-wave record.
- [docs/ARCHITECTURE.md](ARCHITECTURE.md) · [docs/INTERFACE.md](INTERFACE.md) · [docs/FEATURE_CATALOG.md](FEATURE_CATALOG.md)
- [tests/SELFTEST.md](../tests/SELFTEST.md) — both selftests' fields + pass criteria.
