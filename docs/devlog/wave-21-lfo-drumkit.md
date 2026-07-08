# W21 — frontier Wave 9 (LFO modifiers) + a drum sampler for Step Clips

> Baseline **`cf0023d`** (W20 + the MIDI-import tip). Two **file-disjoint** features built in one **two-track
> wave** (two parallel build agents on disjoint territories + one orchestrator consolidation). Delivers the last
> unbuilt wave of the 10-wave **frontier build program** ([[forge-frontier-program]]) — **frontier Wave 9 (LFO
> modifiers)** — plus the documented **Step Clips drum-sampler follow-up**. Build **clean** (MSVC Debug, 0
> warnings) · **all FORTY selftests PASS** (floor **38 → 40** via `--selftest-modifier` + `--selftest-drumkit`) ·
> 11/11 `--screenshot` matrix renders · a **2-dimension adversarial QC** (both features **SHIP** — 0 blocker/major;
> 4 minor + doc findings applied). **Held for the maintainer's push OK.**

The frontier program is now **complete** — Waves 1–10 all shipped (Wave 9 was the last one outstanding).

---

## Why these two together

The maintainer asked to "execute both in parallel." They are genuinely file-disjoint, so they ran as a two-track
wave rather than a serial spine:

- **Track A (Wave 9 LFO):** a NEW header-only engine seam `src/engine/ModifierHelpers.h` + a gate. No
  `ProjectSession`, no UI (the "Modulate" affordance stays a deferred Fable follow-up).
- **Track B (drum sampler):** extends the existing `src/engine/dsp/InstrumentSamples.{h,cpp}` +
  `src/engine/PluginHost.{h,cpp}`, routed in via a **one-line** `ProjectSession::createStepClipInSlot` swap.

The orchestrator owned `main.cpp` (both gates + the two ladders), the `ProjectSession` one-liner, the single
integration build, and the 40-gate floor. Each build agent edited + self-reviewed for compile-safety and did NOT
build (one build dir + a device lock → concurrent builds collide).

---

## Feature 1 — frontier Wave 9: live plugin-param modulation (LFO)

Bitwig-style *modulate, don't draw a curve* — a live LFO on any automatable parameter, distinct from drawn
automation lanes. The engine ships a **unit-tested** modifier system; every `AudioTrack` has a non-null
`ModifierList`. The seam was built from the **frozen, source-verified recipe** preserved in
`docs/wave-9-lfo-recipe.local.md` (produced when Wave 9 was first deferred).

- **NEW `src/engine/ModifierHelpers.h`** — header-only `namespace forge::modifier`, all `inline`, mirrors
  `AutomationHelpers.h`'s house style: `LFOConfig` (rate/depth/bipolar/wave/tempoSync/offset); `addLFO(track,cfg)`
  (insert `IDs::LFO` at list index 0 — `insertModifier` auto-`initialise()`s + registers the ModifierTimer);
  `applyConfig`; `assign(param, lfo, depth)` → `ModifierAssignment::Ptr`; `unassign`; `removeLFO`. Message-thread
  only; the one fallible seam (`addLFO`) logs; the pure wrappers do not (matching the "log at the fallible seam"
  rule).
- **Load-bearing recipe gotchas honored** (all re-verified against `libs/tracktion_engine`): `rateType` defaults
  to **`bar`**, so `applyConfig` always writes it explicitly (hertz for free-running); the free-running phase
  advances by **`numSamples`** passed to `updateModifierTimers` (NOT editTime — so the gate ticks with
  `numSamples=512 > 0`); `updateToFollowCurve` reads the live value only when `editTime={}` (a positive editTime
  routes to the audio-thread ValueFifo → stale); `depth > 0` is required to oscillate.
- **Gate `--selftest-modifier`** (collision-free): create an LFO on a fresh track → sweep ~60 ticks → assert
  `getCurrentValue` spread > 0.3 (oscillates, measured **1.999**); assign to the track volume param → assert
  `getCurrentModifierValue` non-constant + `hasActiveModifierAssignments()`; unassign + removeLFO → assert
  `getAssignments().isEmpty()` + a final sweep is constant. Content-level asserts only (never `canUndo/canRedo` —
  the `FourOscPlugin` redo-wipe defect makes those unreliable).

## Feature 2 — a self-rendered CC0 drum sampler for Step Clips

Step Clips were born-audible via a default 4OSC, so the 8 drum lanes played as 8 **pitches**, not drum timbres.
Now a step clip is born with a `te::SamplerPlugin` whose 8 sounds are **self-rendered CC0** drum one-shots, one
per GM note a StepClip channel triggers — so the grid sounds like a **kit**.

- **`InstrumentSamples.{h,cpp}`** — new `DrumHit { file, midiNote, name }` + `ensureDrumKit()`: 8 deterministic
  procedural voices (kick/snare/closed-hat/open-hat/clap/low-tom/high-tom/ride) synthesized into
  `%APPDATA%\Forge\library\drum_<note>.wav`, one per GM note **36,38,42,46,39,45,50,51** — the exact
  `StepClip` default channel→note map. Same determinism/CC0 contract as the piano (self-owned `Xorshift32` seed,
  no `juce::Random`, temp-then-move, `looksValid` reuse). The write path (`writeMonoWav`) and normalise
  (`normalisePeak`) were **extracted and shared** with the piano — piano output verified **byte-identical**
  (same `0.89/peak` math, same `piano.wav.tmp` temp name, same 44100/1/16 format).
- **`PluginHost::ensureDrumKitInstrument(track)`** — mirrors `ensureDefaultInstrument`'s idempotent contract: a
  no-op returning **false** if the track already hosts a head synth/MIDI-input plugin (never clobbers a melodic
  instrument, never stacks); else inserts a `SamplerPlugin` at head and loads the 8 voices, each
  `setSoundParams(i, note, note, note)` (single-note range → natural pitch, no resampling).
- **The reroute** — `ProjectSession::createStepClipInSlot` now calls `ensureDrumKitInstrument` instead of
  `ensureDefaultInstrument` (the ONE step-clip site; the two melodic-MIDI sites in `createMidiClip` /
  `createMidiClipInSlot` are untouched). `--selftest-stepclip` stays green: `SamplerPlugin::isSynth()==true`, so
  its born-audible probe still passes.
- **Gate `--selftest-drumkit`** (collision-free): a fresh track → `ensureDrumKitInstrument` → assert a
  `SamplerPlugin` with 8 sounds each keyed (key==min==max) to the expected GM note; idempotency (2nd call →
  false, still 8); the 8 wavs exist > 1 KB **and decode to non-silent PCM** (the QC hardening leg below).

---

## Verification

- **Build:** clean, MSVC Debug, 0 warnings.
- **Selftest floor: 40 / 40 PASS** (all 38 pre-existing gates green — no regression from the instrument reroute or
  the shared `InstrumentSamples` refactor — plus the 2 new gates). Floor **38 → 40**.
- **`--screenshot`:** all 11 states render (incl. `session_stepgrid` — step clips now render/sound as drum kits;
  no new visual state needed).
- **Public-repo hygiene:** scrub-clean (all paths via `File::getSpecialLocation` — no hardcoded user paths; no
  committed binaries — the drum/piano wavs are runtime-generated under `%APPDATA%`). Author identity
  `TxVibeCoder` confirmed.

## Adversarial QC — 2 dimensions, both SHIP

Two parallel opus skeptics (one per feature), default-refute, source-verified. **0 BLOCKER, 0 MAJOR.** Both
returned **SHIP**; the findings were gate-completeness + doc drift, and the worthwhile ones were fixed before this
record was written.

- **Wave 9 LFO — SHIP** (0/0/2 MINOR/1 NIT). REFUTED clean: threading/R1, lifetime/UAF (the `Ptr` stays valid
  through `removeLFO`), gate vacuity/determinism (oscillation is genuine + deterministic), all recipe gotchas,
  FourOsc-redo interaction, dirty-read, nested-comments, ladder ordering. **Fixed:** (1) the gate only tested the
  DEFAULT config, and the engine's default already oscillates, so an `applyConfig` regression could pass — added a
  **`depth=0` config-sensitivity leg** that asserts a flat sweep (proves the config writes take; measured
  `depthZeroSpread=0.0`). (2) `unassign()` was documented as a no-op but the engine's `removeModifier(source&)`
  hits **`jassertfalse` in Debug** when the source isn't assigned — a real trap for the future Modulate UI; now
  **guarded** with `getModifiers().contains(&lfo)`.
- **Drum sampler — SHIP** (0/0/2 MINOR/3 NIT). REFUTED clean: **piano byte-identical** (diff-verified), determinism,
  the `getNumSounds()-1` indexing under partial failure, partial-kit-fails-the-gate, **born-audible**
  (`SamplerPlugin::isSynth()` genuinely true in this engine source), the channel↔note map (exact), idempotency/
  clobber, gate-name collision, hygiene, nested-comments. **Fixed:** the gates were structural-only and on a warm
  library cache the generators weren't exercised at all — added a **hermetic non-silence decode leg** (decode each
  generated wav, assert peak > 0.05; `audioNonSilent=1`), which on a cold cache exercises the generate path end to
  end. **Fixed doc drift:** two stale `ensureDefaultInstrument`/`4OSC` comments (`main.cpp` stepclip gate +
  `SessionView.cpp`).

---

## Follow-ups (documented, not built)

- **Mixed step + melodic clips on one track (v1 limitation, QC-B MINOR-2):** the instrument is "first one wins."
  A step clip on a fresh track → drum kit; a *later* melodic MIDI clip on that same track routes through the drum
  Sampler, and because each drum sound is a single-note range, **any melodic pitch outside {36,38,39,42,45,46,50,
  51} is silent**. Acceptable v1 (a step clip normally owns its track); a future "Convert to drum kit" / per-clip
  instrument would resolve it.
- **Full engine-render / Sampler-ingestion gate leg (parked, W09/W10 precedent):** the drum gate proves the wavs
  contain audio; it does not pump the Sampler's async load + render a note to non-zero output. Same class as the
  W09 piano-ingestion follow-up.
- **Cache version stamp (NIT):** `looksValid` reuses any on-disk wav > 1 KB, so a future generator change serves
  stale audio until the library dir is cleared (pre-existing for the piano). A version suffix / sidecar would fix.
- **Fuller LFO config-matrix coverage (NIT):** the gate now proves `depth` round-trips; rate/rateType/wave/bipolar/
  offset are still only exercised at defaults.
- **The "Modulate" UI (Fable):** Wave 9 shipped engine-only; a mixer-strip / plugin-param context affordance
  reusing the MIDI-learn param picker is the deferred UI follow-up.

## New gotchas (for CLAUDE.md)

- **`AutomatableParameter::removeModifier(ModifierSource&)` `jassertfalse`s in Debug** if the source has no
  assignment on that param — a `unassign`-style wrapper must guard on `getModifiers().contains(&source)` first.
- **Step clips are now born with a drum-kit `SamplerPlugin`, and instrument assignment is idempotent
  ("first-instrument-wins").** A clip that's visibly on a track can be silent (or wrong-timbre) because the track's
  head instrument was decided by whatever clip type landed there first — a step clip and a melodic MIDI clip
  sharing a track do NOT each get their own instrument in v1.
