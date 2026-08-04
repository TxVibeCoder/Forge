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
>
> **Update 2026-07-22 later (W24 wave):** **B1, B2, B3, B5, B7 ALL SHIPPED** in one backlog-burn-down wave
> (3 file-disjoint agents + orchestrator; build clean; **47/47 floor** — `+--selftest-reload`; 12/12
> screenshots; full record → `devlog/wave-24-backlog-burndown.md`). Their sections below are now stubs.
> **Still open: item 0 (Redo — maintainer decision), B4 (capture count-in), B6's remaining interaction, the
> B8 leftovers.** Push still held.
>
> **Update 2026-08-04 (W25):** **B9 — multi-stem audio import SHIPPED** (a maintainer brief, added to this
> page and burned down in the same pass). Build clean, **48/48 floor** (`+--selftest-stems`), 12/12
> screenshots. ✅ **PUSHED** — `origin/main` tip is **`062c248`**, local `main` == `origin/main` (verified by
> `git fetch` after the push). W24 turned out to have been pushed already, so every "push still held" note
> above is stale.
>
> **Update 2026-08-04 — SESSION CLOSE.** `origin/main` was **`f977596`** at the end of the W29 wave work
> (later doc commits move it — check `git log -1 origin/main`); local in sync, tree clean, build
> clean, **50/50 floor**, 12/12 screenshots — all verified, not assumed. **"Ready to build" is now EMPTY.**
> Remaining: **item 0** (Redo — a maintainer A/B/C decision, the only broken core interaction), the B8
> smalls, and optional B6 library growth. ⚠ Unresolved and carried forward: the `--selftest-popout`
> intermittency was never reproduced (65/65 PASS) — W29 hardened it and made failures self-diagnosing, but
> found no cause. ⚠ Wave-label note: **W28** (the LCD count-in face) was committed with the subject
> `[W27 f/u]`; the docs use W28 throughout.
>
> **Update 2026-08-04 latest (W27 + W28):** **B4 capture count-in SHIPPED** (W27) — and its inherited
> deferral rationale turned out to be WRONG (see the B4 section). Its LCD-readout follow-up **closed in W28**:
> the count-in face is no longer record-only. Build clean, **50/50 floor** (`+--selftest-countin` in W27; W28
> extended `--selftest-lcd` + `--selftest-countin` in place, no new gate). **"Ready to build" is now EMPTY** —
> what remains is item 0 (Redo, a maintainer decision), the B8 smalls, and optional CC0 library growth.
>
> **Update 2026-08-04 later (W26):** **B6 SHIPPED** — the missing browser→track interaction plus two more
> assignment surfaces, over one new `ProjectSession::setTrackInstrument` seam; the four W24 melodic voices
> are no longer dead code. **B3 is now fully closed too** (its leftover render slice rides the new gate).
> Build clean, **49/49 floor** (`+--selftest-instrument`), 12/12 screenshots. Still open: item 0 (Redo —
> maintainer decision), B4 (capture count-in), B8 leftovers, and B6's optional library growth.

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

### B1 — MIDI import: multi-track files + browser support — ✅ SHIPPED (W24)
`ProjectSession::importMidiFileMultiTrack` (over the engine's own `te::readFileToMidiList` parse); browser
filter + double-click + arrange drop serve `.mid;.midi`; `--selftest-midifile` +6 legs. Honest edges (flagged,
not built): a single format-1 source track using multiple channels yields one clip per channel (the engine's
canonical decomposition); the file's tempo/meter map is NOT imported (product decision); slot-side multi-track
fan-out out of scope. Details → `devlog/wave-24-backlog-burndown.md`.

---

### B2 — Prove persistence: save→reload round-trip gate legs — ✅ SHIPPED (W24)
`--selftest-reload` (floor 46 → 47): seed via real seams → `saveAs` → **mutate-away in memory** → reopen via
the real `swapProject` → assert everything reads back. The mutate-away step is the load-bearing wall against
a false pass. Contract → `../tests/SELFTEST.md`.

---

### B3 — Render-audibility gate legs — ✅ SHIPPED (W24 demo + drumkit; W26 melodic voices)
Deferred render phases on `--selftest-demo` / `--selftest-drumkit` (600 ms async-ingestion pump →
`Exporter::renderStems` → peak; three-state PASS/FAIL/SKIP). Verified genuine PASS (peaks ≈0.55/0.65).
**Remaining slice CLOSED in W26:** the four melodic voices (PluckBass/Pad/Bell/Clav) now render in
`--selftest-instrument`'s phase 2 — one note each on their own named tracks, all four in ONE `renderStems`
pass after a 900 ms pump; first run `weakestPeak ≈ 0.31`. **B3 is fully done.**

---

### B4 — Capture count-in — ✅ SHIPPED (W27)

**The inherited deferral rationale was WRONG, and checking it was most of the work.** W22 recorded (flagged
"not re-verified") that *"the only audible-count-in path puts the transport into record-mode for the whole
Capture session"*. Reading `TransportControl::record()` (`tracktion_TransportControl.cpp:1483-1489`) shows the
count-in is only a **position offset** (`startTime − countInBeats`) plus a forced click range — both
reproducible on a plain `play()`. No record mode, no Rec-button lie, no risk to the W17 capture path. See the
new CLAUDE.md gotcha.

**Shipped:** `forge::capture::planCountIn` (`src/engine/CaptureCountIn.h` — pure, engine-free, exhaustively
gate-able) decides where to roll and where to arm; `ProjectSession` gained a `countingIn` state that rolls the
transport from the pre-roll beat with the click forced on, arms capture on the downbeat, and restores the
user's click setting exactly. `isPerformanceCaptureArmed()` reads true throughout so the Capture toggle stays
lit; `isCountingIn()` exposes the pending state; the status strip says "Counting in". Cancelling mid-count-in
disarms, restores the click and stops the transport Forge started. Two skip paths keep prior behaviour intact:
already-rolling arms at once, and **no count-in configured is byte-identical to pre-B4** (arm now, never touch
the transport — the W17 passive-observer contract, pinned by its own gate leg).

**No new UI:** it reads `Edit::getNumCountInBeats()`, so the existing transport-bar count-in selector serves
record and capture alike — one setting, one answer.

**Gate:** `--selftest-countin` (floor 49 → 50), 19 legs, both load-bearing rules independently
negative-controlled. The W17 footgun holds: `LaunchHandle::nudge`/`setLooping`/`playSynced` stay uncalled, and
the capture tick is untouched (it simply starts later).

**Follow-up CLOSED in W28:** the LCD count-in face is no longer record-only. `LcdInput` gained a
`captureCountIn` flag; the model's active test is now `recordCountIn || captureCountIn` and **shares the
click-grid digit derivation verbatim** (the capture pre-roll rolls the ordinary transport with the ordinary
click, so its clicks land on the same whole-beat grid; the arm beat stands in for the punch, which does not
exist when nothing is recording). `LcdDisplay` gained a `queryCaptureCountIn` seam wired by the shell, with the
same rising-edge latch the record path uses so `getNumCountInBeats()` stays off the 25 Hz poll. Gated by 10 new
pure-model legs on `--selftest-lcd` **plus 3 wiring legs on `--selftest-countin`** driving the real
`pollNow()` — a negative control confirmed that unwiring the seam leaves `--selftest-lcd` fully green while the
wiring legs fail, which is exactly why they live on the engine-backed gate. No floor change (both gates
extended in place).

---

### B5 — Finish the W23 trim residuals — ✅ SHIPPED (W24)
Speed-correct audio trim (`Δ = Ts/speed − offset`, source-verified against `clipTimeToSourceFileTime`;
gate leg pre-seeds a non-zero offset so the naive formula fails) + CC-only MIDI trim. Bonus hardening:
**auto-tempo clips are now a logged decline** (their offset is beat-domain — the old unity-speed guard could
let one slip through and mis-trim; a latent W23 hole, closed). Remaining documented decline: auto-tempo.

---

### B6 — Browsable CC0 instrument library — ✅ SHIPPED (W26)
The W24 groundwork (`13a1f0f`) had shipped four melodic voices as **dead code** — every reference lived in
`src/engine/`, none in `src/ui/`, so nothing user-facing could assign them. W26 built the missing path:

- **One seam:** `ProjectSession::setTrackInstrument(trackIndex, preset)` over the existing
  `PluginHost::applyInstrumentPreset` (which already cleared the head synth first, so re-assignment replaces
  rather than stacks). Every surface routes through it; none touches the plugin chain.
- **One catalogue:** `PluginHost::getInstrumentChoices()` / `getInstrumentPresetName()` — the single name
  table. `applyInstrumentPreset` now names the loaded Sampler sound from the same function, so a menu label
  and the sound on the track cannot drift; two inline copies of those strings were removed.
- **Three surfaces**, all rendered from that catalogue: an `Instrument ▸` submenu appended to the Arrange
  lane-header menu; a **new** right-click menu on the Session track header (`TrackColumnComponent` gained a
  header hit-test + `onHeaderRightClicked`); and an **INSTRUMENTS** list in the Browser above the file tree
  (double-click or Enter). The Browser list is track-agnostic — the shell picks the target
  (`instrumentTargetTrack()`: the focused Session column, else the last-selected Arrange lane, clamped) and
  the status strip names both voice and track so the gesture is never a silent state change.
- **Gate `--selftest-instrument`** (floor 48 → 49). Also **closes the B3 leftover**: all four melodic voices
  now render non-silent in a deferred pass (first run `weakestPeak ≈ 0.31` — a genuine PASS, not a SKIP).

**Deliberately not built:** per-slot assignment. The engine is track-level — a slot clip plays through
whatever its track hosts (the W21 "first-instrument-wins" gotcha; the W22 "Move to its own track" fix exists
precisely because of it), so a per-slot picker would lie about what the engine does.

**Still open (library growth, not interaction):** more CC0 voices. Self-rendered one-shots only — a public
AGPLv3 repo; **no third-party packs**, a locked decision. Extend `InstrumentSamples`, never vendor. Adding a
voice is now: one enum value + one name + one `InstrumentSamples` renderer — every surface picks it up.

---

### B7 — Modulate (LFO) UI polish — ✅ SHIPPED (W24, Fable calls implemented)
Edit ▸ Modulate… (Ctrl+M, grouped with MIDI Learn) + a modulated-parameter accent dot (5px on a panelBg
backing disc, `paintOverChildren`, edge-compared cache riding each surface's existing poll) on MixerView
Channel/Return strips, ChannelTray, and SessionMixerStrip. Gated (`-menu` count+shortcut pin;
`-sessionmixer` +3 indicator legs) and lit in the screenshot demo. Skipped by design: the master strip
(unreachable from `showModulateMenu`); send knobs / per-plugin-param indicators (additive later).

---

### B9 — Multi-stem audio import (drop N files → N tracks) — ✅ SHIPPED (W25)
*Maintainer brief, 2026-08-04: "the last gap between Forge and a complete stem-mixing workflow".*

New `ProjectSession::importFilesMultiTrack` (+ the audio-only `importAudioFilesMultiTrack` convenience)
mirrors W24's `importMidiFileMultiTrack` structure: validate/order before touching the edit, grow tracks via
`getOrInsertAudioTrackAt`, never advance the destination index on a per-file failure (no gap lanes), one
`markAsChanged`, `onTracksChanged` only if the list actually grew. Adds three things the MIDI seam did not
need: a **deterministic filename sort** (the OS drop order is arbitrary), **destination-track naming** after
the file (guarded — never renames a user-named lane, nor one already holding arrange **or** Session-slot
clips), and an explicit **mixed audio+MIDI walk** (each file takes the next index; a `.mid` consumes as many
lanes as it lands clips). The Arrange drop path now carries every accepted file instead of returning on the
first, and the shell does **one** save per drop. Gate `--selftest-stems` (floor 47 → 48, 24 legs, both
load-bearing rules negative-controlled). Details → `devlog/wave-25-multistem-import.md`.

**Deliberately out of scope (unchanged):** Browser multi-select (stays single-file double-click), folder
drops, auto-grouping / aux-routing stems, Session-grid (clip-slot) multi-import, and anything in the mixer /
EQ / exporter — all verified already working before the brief was written.

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
| `--selftest-popout` intermittency | W25 → hardened W29 | **NOT REPRODUCED.** 65 dedicated runs (40 isolated + 25 in the floor's tray→popout ordering) came back 65/65 PASS, so the single observed failure was never diagnosed — its report had already been overwritten by the next gate. W29 did the two things that ARE defensible without a repro: replaced the fixed 300 ms deferred-phase sleep with a **condition wait** (poll until both popout resets have actually run, 25 ms × 80 ≈ 2 s ceiling, verify anyway on timeout) and made a failure **self-diagnosing** — the failing leg names now go to the PERSISTENT log, which survives the rest of a floor run. Measured: the resets complete in ~25–50 ms, so the old 300 ms already had ~6× margin — which makes plain timing starvation an unlikely cause and is itself a useful datum. **If it recurs, the log will now say which leg.** Details → `devlog/wave-29-popout-hardening.md`. |

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
| ~~B4 capture count-in~~ | ~~`RecordController`, capture path, `TransportBar`~~ | **SHIPPED W27**; its LCD-readout follow-up **closed in W28**. Nothing left. |
| ~~B6 instrument library~~ | ~~`BrowserView`, `PluginHost`, `SessionView`~~ | **SHIPPED W26** — only optional library growth remains, and that is `InstrumentSamples` + the enum alone. |

*(The W24 wave consumed the rest of this table — B1 + B5 + B7 ran as the suggested 3-agent fan-out with
B2/B3 in the orchestrator's `main.cpp` pass, exactly as planned. B4 and B6-remaining don't conflict and
could even run together, but B4 wants its own focused wave per its risk note.)*

---

## The process (unchanged)

Build → **one** integration build (`cmake --build .\build --config Debug`; kill `Forge.exe` first) → the
**full selftest floor** (currently 50 gates; see `tests/SELFTEST.md`) → `--screenshot` → adversarial QC →
docs (`HANDOFF` / `STATUS` / `CLAUDE.md` counts / `SELFTEST.md` / a devlog) → **sanitize scan** → scoped
commit → **hold the push for the maintainer's OK**.

*Rules and gotchas → `../CLAUDE.md` · product brief → `DIRECTION.md` · state → `HANDOFF.md` · history →
`STATUS.md` · selftest contract → `../tests/SELFTEST.md`.*
