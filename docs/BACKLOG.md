# Forge — Ready-to-Build Backlog

> **What this is.** The actionable, source-verified work queue. `STATUS.md` records *history*;
> `HANDOFF.md` records *where we are*; **this file records *what to build next*.** Pick an item, read its
> packet, build it. Each packet is self-contained enough to paste into a fresh CLI.
>
> **Verification discipline.** Every "Verified" line below was checked against the source on **2026-07-10**
> at tip **`af0bd78`** and carries a `file:line`. Lines marked **[inherited]** are rationale carried from a
> prior wave's devlog and were **not** re-verified — re-check them before relying on them.
>
> **Baseline at time of writing:** tip `af0bd78` · build clean (0 warnings) · **46/46 selftest floor** ·
> 12/12 screenshots · local `main` == `origin/main`.
>
> **Update 2026-07-22 (W24):** two items landed — **B6 groundwork** (`13a1f0f`: the four self-rendered CC0
> melodic voices + `InstrumentPreset` plumbing; the browser→slot interaction is still open, see B6) and the
> **B8 curated launch-Q submenu** (`14c2c61`). Build clean, **46/46 floor re-verified** at `14c2c61`.
> Local `main` is ahead of `origin/main` (push held for the maintainer's OK).

---

## Context: there is no queued "next wave"

Both roadmaps are **complete**. The 10-wave frontier program finished at W21; the 5-wave hands-on plan
finished at W10. W22–W23 were follow-up sweeps. So the items below are a *menu*, not a sequence — nothing
here is blocking anything else except where noted.

**Standing constraints** (from `HANDOFF.md`, still in force): the maintainer has **no MIDI hardware** and
**runs no manual GUI tests** — every item must be **headless-provable** (a `--selftest-*` gate and/or a
`--screenshot` state). Autonomous multi-agent waves are standing-approved. Fable holds UI/UX authority.

---

## 0. BLOCKED ON A MAINTAINER DECISION — fix Redo (highest value on this page)

**This is the only broken *core interaction* on the list; everything else is a missing feature.**

`FourOscPlugin::flushPluginStateToValueTree()` performs an unconditional, UndoManager-tracked
`state.addChild(mm, -1, um)` on **every** flush — i.e. every `session.save()`, which the shell's
`doUndo()`/`doRedo()` always call — even when the mod matrix is empty. `ValueTree::addChild` has no
equality gate, so each undo (a) discards the pending redo stack and (b) plants a phantom top-of-stack
action, meaning the *next* Ctrl+Z can consume the phantom instead of the user's real prior step.

- **Reachable on:** any edit containing a `FourOscPlugin` — Forge's own default instrument, auto-created on
  every MIDI track. In practice: **every real project.**
- **Verified:** the defect is already monitored, non-gating —
  `const bool redoAvailableAfterSingleUndo = um.canRedo();` (`src/main.cpp:4456`), which logs
  `FORGE_LOG_WARN` when false. It reads **0** today.
- **Why unfixed:** the fix lives in vendored `libs/tracktion_engine`, against the project's standing
  "do not fork the engine" default. Carrying a submodule patch has real maintenance cost.

**The decision is the maintainer's, not a builder's.** Options:

| option | cost | consequence |
|---|---|---|
| **A. Patch the vendored engine** — gate the `addChild` on a non-empty / changed mod matrix | carry a submodule patch; re-apply on engine bumps | Redo works. Best user outcome. |
| **B. Stop `doUndo`/`doRedo` calling `save()`** | changes the "disk is never staler than memory" invariant the shell deliberately holds | Sidesteps the flush without touching the engine — but a crash after an undo would "restore" pre-undo state. |
| **C. Leave it** | zero | Redo stays broken. Document it in-app so the user isn't confused. |

**If A or B is chosen:** the gate must assert **content-level** state (marker/note/slot counts), never
`um.canUndo()`/`canRedo()` — see the CLAUDE.md gotcha and `--selftest-undo`'s existing discipline. A real
fix should also flip `redoAvailableAfterSingleUndo` to a **gating** field, which is the clean regression test.

---

## Ready to build

Ordered by value. **Effort** is a rough build+gate+QC estimate. Items marked **⇄ parallel-safe** touch
disjoint files and can be fanned out in one wave (see the territory map at the bottom).

---

### B1 — MIDI import: multi-track files + browser support ⇄ parallel-safe
**Value: high (a papercut you hit on the first real `.mid` you drag in). Effort: medium. Risk: low.**

Two related gaps in the MIDI-file import shipped post-W20.

- **Verified — import collapses to ONE clip.** Both `ProjectSession::importMidiIntoSlot`
  (`src/services/files/ProjectSession.cpp:1104`) and `importMidiFile` (`:1149`) call
  `te::createClipFromFile (file, *track, false)` (`:1165`), which returns a single `te::MidiClip::Ptr` onto a
  single track. A multi-track / multi-channel `.mid` therefore does **not** fan out — you get one clip.
- **Verified — the browser cannot see `.mid` at all.**
  `juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", … }`
  (`src/ui/browser/BrowserView.h:44`). No `.mid`/`.midi`, so browser double-click import is impossible.

**Build:**
1. A `ProjectSession::importMidiFileMultiTrack(file, start, firstTrackIndex)` seam that reads the file's
   track/channel structure (`juce::MidiFile` — parse *before* handing to the engine) and lands **one clip per
   source track/channel** on consecutive Forge tracks, each born-audible.
2. Widen the browser filter to include `*.mid;*.midi` and route a double-click / drag to the MIDI import path
   (the dispatch layer already branches audio-vs-MIDI by extension — reuse it).

**Gate `--selftest-midifile` (extend, no new gate):** write a **3-track** `.mid` (distinct note counts per
track), import it, assert 3 clips on 3 consecutive tracks with the right per-track note counts; assert the
single-track path still yields exactly 1 clip (the regression control).

**Footguns:** MIDI beats are content-relative (beat 0 = clip start); the engine's `createClipFromFile` is
tempo-**independent** (ticks→beats), so notes land on file beats regardless of edit tempo — preserve that.
Don't `setLoopRangeBeats({})` (re-asserts auto-tempo).

**Territory:** `ProjectSession.{h,cpp}`, `BrowserView.{h,cpp}`, `main.cpp` (gate + dispatch).

---

### B2 — Prove persistence: save→reload round-trip gate legs ⇄ parallel-safe
**Value: high (closes a whole silent-failure class). Effort: low. Risk: low.**

- **Verified — no gate ever reloads from disk.** `openProject` / `session.saveAs` appear in `src/main.cpp`
  only at `:2095` and `:2120`, inside the `FileChooser` dialog callbacks. Every gate asserts **in-memory**
  state only.

Several shipped features are proven in memory but never proven to survive a write-and-reload of the
`.tracktionedit`: scene names/order, `forgeLaunchMode`, per-clip launch quantise, follow actions, step-clip
cells, time signatures. Disk persistence is *probably* guaranteed by the engine's whole-tree serializer —
but "probably" is exactly what a gate is for.

**Build:** one new gate `--selftest-reload`: seed a distinctive state (rename a scene, set a per-clip launch
Q, a Toggle launch mode, a follow action, a 5/4 time sig), `session.saveAs(tempFile)`, then
`session.openProject(tempFile)`, and assert **every** value reads back. Cheap, high-leverage.

**Footguns:** the project-swap path tears down views + drops the piano-roll/step-grid clip Ptrs
(`swapProject`); drive it through the **real** seams, not a bypass. Undo history does not survive a swap (by
design — don't assert on it).

**Territory:** `main.cpp` only.

---

### B3 — Render-audibility gate legs (does it actually make sound?) ⇄ parallel-safe
**Value: high (closes the "it's silent and we'd never know" class). Effort: medium. Risk: low.**

- **Verified — the demo gate proves insertion, never ingestion.**
  `pass = kickIsSynth && pianoIsSampler && pianoFileExists && clipHasNotes` (`src/main.cpp:7346`). It asserts
  the `SamplerPlugin` is *inserted* (`:7332`) and the CC0 one-shot *exists on disk* (`:7334`) — but never that
  the Sampler **ingested** the sample (an async load), i.e. never that a note renders audio.

The precedent for the fix already exists: `--selftest-sendarrange`'s W16 leg renders a stem via the
synchronous `Exporter::renderStems` and samples its peak with `readPeakMagnitude`, folding in as a three-state
`PASS`/`FAIL`/`SKIP` (SKIP is honest and non-blocking).

**Build:** apply that same render+peak leg to (a) `--selftest-demo` (the Sampler path — proves ingestion),
(b) `--selftest-drumkit` (the deferred W22 leg), and (c) the four W24 melodic voices (`13a1f0f` — PluckBass /
Pad / Bell / Clav, currently ungated). Reuse the existing helpers verbatim.

**Footguns:** the Sampler loads on an `AsyncUpdater` — you must yield/pump the message loop before rendering
or you'll measure silence and call it a real failure. Keep the three-state `SKIP` semantics; never fabricate a
`PASS` when the render infrastructure can't produce a file.

**Territory:** `main.cpp` only. **⚠ Conflicts with B2** (both are `main.cpp`-only) — serialize those two, or
give them to the same CLI.

---

### B4 — Capture count-in
**Value: medium. Effort: medium-high. Risk: medium — its own focused wave.**

- **[inherited]** (W22 deferral rationale, **not re-verified**): the only audible-count-in path puts the
  transport into **record-mode for the whole Capture session**, which risks the carefully-timed W17
  performance-capture path and would light the Rec button for a non-recording action.

**Build:** find a count-in that does not hold the transport in record-mode for the capture duration (a
metronome pre-roll driven independently, or a transient record-mode window that ends before capture arms).
Re-verify the W17 `LaunchHandle::getPlayedRange()` span timing is unaffected.

**Footguns:** W17's capture reseal heuristic depends on `LaunchHandle::nudge`/`setLooping`/`playSynced`
staying uncalled (CLAUDE.md gotcha). The count-in digits derive from the engine's **click grid**, not the
time-sig numerator (verified during W23's time-sig work) — don't "fix" the count-in to follow the meter.

**Territory:** `RecordController` / `ProjectSession` capture path + `TransportBar` + `main.cpp`. **Not
parallel-safe** with anything touching the capture path.

---

### B5 — Finish the W23 trim residuals ⇄ parallel-safe
**Value: medium. Effort: low. Risk: low.**

Two documented limits shipped knowingly in `af0bd78`:

1. **Audio trim declines on a non-unity speed ratio.** A clip's `offset` is in **edit**-seconds while the
   silence scan yields a **source**-second, so the advance is `Δ = Ts/speed − offset`, *not*
   `(Ts − offset)/speed`; they agree only at `speed == 1`. `forge::audioedit::trimLeadingSilence`
   (`src/engine/AudioEditHelpers.h`) guards on `speed == 1` and returns false otherwise.
   **Build:** implement the speed-correct formula + a gate leg with a stretched clip.
2. **MIDI trim keys on the first *note*.** A controller-only clip (CC/sysex, zero notes) no-ops, even though
   `MidiList::getFirstBeatNumber()` already accounts for CC/sysex. **Build:** relax the
   `getNumNotes() == 0` guard to "no events at all" and add a CC-only gate leg.

**Territory:** `AudioEditHelpers.h`, `MidiEditHelpers.h`, `main.cpp` (gate legs).

---

### B6 — Browsable CC0 instrument library
**Value: medium (the "give me a sound" workflow). Effort: high. Risk: medium.**

- **[inherited]** (W09 scope deferral, **not re-verified**): blocked on a missing **browser → Session-slot**
  interaction — there is no way to drop/assign a library instrument onto a track from the browser.
- **Groundwork SHIPPED (W24, `13a1f0f`):** four self-rendered CC0 melodic voices — PluckBass / Pad / Bell /
  Clav (`InstrumentSamples::ensureMelodicOneShot`, `src/engine/dsp/InstrumentSamples.{h,cpp}`), each a
  deterministic one-shot rendered at `kRootNote`, plus `PluginHost::InstrumentPreset::{PluckBass,Pad,Bell,
  Clav}` wired through a shared `insertSamplerOneShot` helper (the Piano's chromatic-Sampler recipe,
  factored). **Nothing user-facing assigns these yet**, and no gate leg renders them — fold the render proof
  into the B3 pass.

**Build (remaining):** the interaction (browser item → track/slot) + a UI surface that assigns an
`InstrumentPreset` to a track, then any further library growth. Self-rendered CC0 one-shots only — a public
AGPLv3 repo; **no third-party packs**, a locked decision. Extend `InstrumentSamples`, never vendor.

**Territory:** `BrowserView`, `PluginHost`/`InstrumentSamples`, `SessionView`, `main.cpp`. **⚠ Conflicts with
B1** on `BrowserView`.

---

### B7 — Modulate (LFO) UI polish — Fable's call
**Value: low-medium (discoverability). Effort: low. Risk: low.**

- **Verified — the feature exists but is keyboard-only.** Ctrl+M → `showModulateMenu()`
  (`src/main.cpp:2054` → `:2363`). There is no menu-bar entry and no indicator that a parameter is modulated.

**Build (Fable owns the calls):** a menu-bar entry, a shortcut review, and a modulated-parameter indicator on
the affected control. Purely additive.

**Territory:** `ForgeMenuModel`, the mixer/tray strip widgets, `main.cpp`.

---

### B8 — Opportunistic small items (fold into any wave)

| item | source | note |
|---|---|---|
| ~~Curated launch-Q submenu subset~~ | W12 f/u | **SHIPPED W24 (`14c2c61`)** — 8 straight divisions + a "More..." child menu; ids stay enum-keyed. |
| Immediate-launch for Gate mode | W11 f/u | Instant click-hold under any quantise. |
| Random / weighted / group follow-actions | W11 f/u | v2 of the follow-action set. |
| Scene colour / multi-select | W15 f/u | |
| Strip re-bind edge + absolute-index re-resolve | W08 QC | **Latent traps, not live bugs.** Add invariant comments now; only fix if a strip-reuse or drag-reorder feature lands. |
| Aux-return ordering (cosmetic) | W07 f/u | |

---

## Parked (do not propose these)

| item | why |
|---|---|
| Launchpad byte mapping · physical-CC MIDI-learn · APC40 | **No MIDI hardware.** Permanently parked until hardware exists. |
| Drag-to-reorder scenes | No headless mouse-drag driver — unprovable under the standing constraints. |
| Manual GUI smoke passes | The maintainer runs none. Use gates + `--screenshot`. |
| The `CallOutBox` launch + OS wheel delivery | Interaction-territory. The hit-test precondition *is* gated (`sigZoneClickable`); the wheel *routing* *is* gated (`handleWheel`). Only the OS event delivery isn't. |

---

## Territory map (for a fan-out wave)

`main.cpp` and `CMakeLists.txt` are **always orchestrator-owned** — a CLI *proposes* its gate/wiring, never
edits them (CLAUDE.md, Wave Orchestration Rule, Pillar 3).

| item | owns | conflicts with |
|---|---|---|
| B1 MIDI import | `ProjectSession.{h,cpp}`, `BrowserView.{h,cpp}` | B6 (BrowserView) |
| B2 save→reload | *(gate only)* | B3 (both main.cpp-only) |
| B3 render legs | *(gate only)* | B2 |
| B4 capture count-in | `RecordController`, capture path, `TransportBar` | — |
| B5 trim residuals | `AudioEditHelpers.h`, `MidiEditHelpers.h` | — |
| B6 instrument library | `BrowserView`, `InstrumentSamples`, `SessionView` | B1 (BrowserView) |
| B7 Modulate polish | `ForgeMenuModel`, strip widgets | — |

**A clean 3-agent wave:** B1 + B5 + B7 (fully disjoint), with B2/B3 folded into the orchestrator's own
`main.cpp` pass at consolidation.

---

## The process (unchanged)

Build → **one** integration build (`cmake --build .\build --config Debug`; kill `Forge.exe` first) → the
**full selftest floor** (currently 46 gates; see `tests/SELFTEST.md`) → `--screenshot` → adversarial QC →
docs (`HANDOFF` / `STATUS` / `CLAUDE.md` counts / `SELFTEST.md` / a devlog) → **sanitize scan** → scoped
commit → **hold the push for the maintainer's OK**.

*Rules and gotchas → `../CLAUDE.md` · product brief → `DIRECTION.md` · state → `HANDOFF.md` · history →
`STATUS.md` · selftest contract → `../tests/SELFTEST.md`.*
