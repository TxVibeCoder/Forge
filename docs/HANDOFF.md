# Forge — Session Handoff

> Living handoff for picking the project back up cold. Pairs with [STATUS.md](STATUS.md) (the living
> roadmap). Last updated: **2026-06-30**, end of the **"MIDI MVP build"** session.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) ·
branch `main` · MIDI MVP commit `9a24989` · **ahead of `origin/main` — NOT pushed** (this session's MIDI
MVP + docs, atop the earlier recording/design commits; `git status` for the exact count) ·
~5,900 lines / 31 source files · last build **clean** · both self-tests **PASS** · verify wave **clean**.

Forge is an arrangement-first DAW on **JUCE + Tracktion Engine** (C++20, Windows-verified). Phases 0–4 +
startup-latency hardening + the **MIDI MVP** are done and live: project/arrange/transport/mixer/plugins/
browser/inspector/export, output-only startup with lazy record-input open, and **drawable, audible MIDI
clips**. See [STATUS.md §2](STATUS.md) for the full feature list.

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

---

## ⚠️ The one thing not yet verified

**The live GUI draw→play→hear path has NOT been exercised in a running window** — the dev-built
`Forge.exe` can't be GUI-driven headlessly here. Everything is statically verified (build + selftests +
adversarial trace), but the very first manual action should be a **GUI smoke test**:

1. Run `Forge.exe`. Right-click the empty area of a track lane → **New MIDI Clip**.
2. The piano-roll should open in the bottom drawer. Click the grid to draw a few notes; drag to move/resize;
   right-click a note to delete.
3. Press **Space** → the notes should sound through the 4OSC. Save, reopen, confirm the clip persists.

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
- **Not pushed:** `main` is ahead of `origin/main` (this session's MIDI MVP `9a24989` + docs, atop the
  earlier recording/design commits). `git push` when ready; `git status` shows the exact count.

---

## What's next (prioritized)

1. **GUI smoke test of the MIDI MVP** (above) — the one unverified path. Do this before building on it.
2. **MIDI post-MVP:** **W6** velocity lane + multi-select/marquee + copy/paste + **Delete-key** + horizontal
   auto-scroll to the clip (all inside `src/ui/pianoroll/*`, disjoint from engine files). **W7** MIDI-input
   recording — the higher-risk wave: its own enable sequence (`getMidiInDevices()` + `setEnabled` +
   `setMonitorMode` + `rescanMidiDeviceList()`) **before** `ensureContextAllocated()`, a different device-type
   filter, and a **runtime test with a physical MIDI controller**. See [midi-design.md §5](devlog/midi-design.md).
3. **Automation** (vol/pan/plugin-param lanes) + **buses/sends** in the mixer (engine Phase 3 + 2 remainder).
4. **Polish** — async export + progress (mixdown & stems both block the message thread); LUFS metering;
   markers; comping; off-thread record-input open.
5. **macOS build** (only Windows verified) and interactive-UI verification of the Phase-4 surfaces.

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
