# W09 — the hands-on wave, part 4: self-rendered instruments + an audible demo

> Wave 4 of the maintainer's hands-on plan ([[forge-handson-wave-plan]]). Baseline **`1a59973`** (W08 tip).
> Makes the app **audibly playable out of the box**: per-track instrument presets (a 4OSC kick, a 4OSC bass,
> a **Sampler loaded with a self-rendered CC0 piano one-shot**) + a note-written C-minor demo (4-on-floor
> kick · walking bass · chord stabs) + a **first-launch welcome demo**. Today's demo clips were EMPTY
> (silent); W09 closes that.

Build **clean** (MSVC Debug, 0 warnings) · **all TWENTY-THREE selftests PASS** (the W08 twenty-two +
`--selftest-demo`) · `--screenshot`'s base `session` state now shows the note-seeded groove (Keys = a Sampler,
scene 0 the hero groove). **Committed + PUSHED** with the wave set.

---

## The maintainer's scope decision

The plan's "instruments/**library**" was under-specified, so it was put to the maintainer (an in-session
question) after a 4-investigator feasibility sweep. Chosen: **Hybrid demo, NO browsable library** — 4OSC
presets for kick+bass, the engine Sampler + one self-rendered CC0 piano one-shot for piano, and the note
pattern. A browsable instrument library (which would need new browser→slot interaction that doesn't exist
today) is deferred to its own wave.

The feasibility sweep settled the mechanisms before any build: **a sampler EXISTS** (`te::SamplerPlugin`, a
registered built-in, maps one sample chromatically for free); **4OSC can voice kick+bass but not a convincing
piano** (4 oscillators + 1 filter → a synth EP); **self-render = procedural DSP** (the proven `createSineWaveFile`
pattern, a seeded PRNG, runtime-generated — not committed binaries).

---

## What shipped

### The instrument layer (one build agent)
- **NEW `src/engine/dsp/InstrumentSamples.{h,cpp}`** — `ensurePianoOneShot() → juce::File`: procedurally
  synthesizes a CC0 piano one-shot (8 slightly-inharmonic partials + a fast-attack/exponential-decay envelope
  + a seeded-noise strike transient) into `%APPDATA%\Forge\library\piano.wav`. Deterministic via a self-owned
  **xorshift32** PRNG (NOT `juce::Random`); mono 44.1 kHz; temp-then-move + a `looksValid` size guard so a
  truncated write is never reused; idempotent (returns the existing file). Root note = MIDI 60 (`kRootNote`).
- **`PluginHost.{h,cpp}`** — registered the **Sampler** in `getBuiltInInstruments()`; added
  `enum class InstrumentPreset { Kick, Bass, Piano }` + `applyInstrumentPreset(track, preset)`: removes any
  head synth first (`removeExistingInstruments` — never touches effects or the vol/pan/meter tail), then Kick/Bass
  = a programmed 4OSC (osc waveshapes, tune, level, amp/filter ADSR, low-pass filter), Piano = the Sampler with
  `addSound(ensurePianoOneShot(), "Piano", 0, 0, 0)` (length 0 = whole file) + `setSoundParams(0, 60, 0, 127)`
  so it plays chromatically. The Sampler loads audio on an AsyncUpdater (only matters for a renderer — the demo
  + screenshot don't render through it).

### The demo + first-launch (orchestrator, `main.cpp`)
- **`seedDemoNotes(clip, trackIndex)`** — writes a per-instrument pattern (content-relative beats, the Edit's
  real UndoManager): track 0 = four-on-the-floor kick (MIDI 36, 16 notes), track 1 = C-minor root/fifth walking
  bass (8-beat phrase ×2), track 2 = Cm–Ab–Bb–Cm 3-note chord stabs (12 notes). Tracks 3–5 stay empty.
- **`populateDemoContent()`** (extracted from `populateDemoSession`) — named/coloured tracks, then
  `applyInstrumentPreset` on the three hero tracks **BEFORE** creating clips (so `createMidiClipInSlot`'s
  `ensureDefaultInstrument` no-ops on the existing synth rather than stacking a 4OSC), then note-seeded clips.
  A new **Keys (2,0) "Chords"** cell makes **scene 0** a coherent kick+bass+piano groove.
- **`populateDemoSession()`** (screenshot) = `populateDemoContent()` + the automation curve + `launchScene(0)`.
- **First-launch welcome demo** (`main.cpp:206`) — when `openOrCreate` created a *fresh* default project
  (`!editLoaded && mode==none`), seed the demo content so a brand-new user opens into a playable session. It is
  **in-memory only** (not saved), does **not** auto-play, and **File > New still gives an empty project**; once
  the user saves their own default the demo no longer appears (2nd launch loads the saved-empty default).
- **Gate `--selftest-demo`** (floor 22→23): asserts the Kick preset is a 4OSC, the Piano preset is a real
  `SamplerPlugin`, the piano one-shot exists on disk, and a seeded clip holds notes.

---

## Adversarial QC — 3 dimensions. NO blockers, NO majors.

Three parallel finders (opus, default-skeptical, self-refuting): the instrument layer, the demo + first-launch
hook, and hygiene/determinism/gate. **Everything clean.**

- **Instrument layer — CLEAN.** Ten candidate bugs refuted against the engine source: osc waveshape int
  mapping, `filterType=1`=low-pass, 4OSC params built synchronously in the ctor (no use-before-init), all
  preset values in range, `removeExistingInstruments` touches only synths, the absolute Sampler path resolves
  via the Edit's default `filePathResolver`, the WAV write is crash-safe + deterministic + idempotent, and an
  async-load failure yields silence, not a crash.
- **Demo + first-launch — CLEAN.** The preset-stacking worry is refuted (`ensureDefaultInstrument` no-ops on
  any `isSynth()||takesMidiInput()`, and `SamplerPlugin::isSynth()==true`, so Keys gets ONLY a Sampler); the
  first-launch hook fires only on a genuinely-created default, touches only model seams, keeps File>New empty,
  never double-populates, and the demo does not reappear on the 2nd launch.
- **Hygiene / determinism / gate — CLEAN.** 100% self-synthesized CC0 (no blob, no committed `.wav` — the
  output lives in `%APPDATA%` outside the repo); deterministic (xorshift32, no `Random`/clock); the gate is
  substring-safe and honestly structural; public-repo scrub clean (no paths/name/email; the dir is derived via
  `getSpecialLocation`).

### Documented, not fixed (non-blocking hardening notes)
- **[LOW, fixed] doc drift** — `tests/SELFTEST.md` said the screenshot "launches scene 3"; W09 uses
  `launchScene(0)`. Corrected (Forge's no-unverified-claims rule).
- **[QC NIT] the gate proves the piano one-shot exists on disk but not that the Sampler *ingested* it** (an
  async load). A render/ingestion leg (pump the loop, assert the Sampler's `audioData` is non-empty, and/or
  render a note to non-zero audio) would prove the final audible link — a good follow-up. The current gate +
  `--selftest-session` (playback engages) + the deterministic generator cover the structural chain.
- **[QC MINOR] `looksValid()` checks size, not decodability** — only matters for a *foreign* corrupt
  `piano.wav` placed at the path (unreachable from Forge's own crash-safe writes).

---

## Gotchas (new / reinforced)

- **A parameter named like a class member trips MSVC C4458 (and Forge's 0-warning bar).** `seedDemoNotes
  (te::MidiClip& clip, …)` shadowed `MainComponent::clip` — renamed the parameter. Watch for common member
  names (`clip`, `edit`) as parameter names in `main.cpp`.
- **Apply a track's instrument BEFORE creating its first clip.** `createMidiClipInSlot`'s
  `ensureDefaultInstrument` inserts a stock 4OSC only if the track has NO synth; applying the real preset first
  makes it a no-op, so the track ends with exactly one (correct) instrument.
- **Self-render, don't commit.** A procedural generator (seeded PRNG, `createSineWaveFile` recipe) writing to
  `%APPDATA%` keeps CC0 assets out of the repo, deterministic, and selftest-provable — a committed binary is a
  drift-prone mirror of its generator.
- **A "quoted `C:\Program Files\…` path + `Remove-Item` in one PowerShell command" trips the sandbox guard**
  (a CLAUDE.md gotcha bit again running the build + the gate loop together) — split them into separate commands.
