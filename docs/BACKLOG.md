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
> above is stale. **Nothing is currently unpushed.** Still open: item 0, B4, B6-remaining, B8 leftovers.

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

### B3 — Render-audibility gate legs — ✅ SHIPPED (W24) for demo + drumkit
Deferred render phases on `--selftest-demo` / `--selftest-drumkit` (600 ms async-ingestion pump →
`Exporter::renderStems` → peak; three-state PASS/FAIL/SKIP). Verified genuine PASS (peaks ≈0.55/0.65).
**Remaining slice:** the four W24 melodic voices (`13a1f0f` — PluckBass/Pad/Bell/Clav) are still ungated —
apply the same leg pattern when B6 surfaces them (they are API-only until then).

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

### B5 — Finish the W23 trim residuals — ✅ SHIPPED (W24)
Speed-correct audio trim (`Δ = Ts/speed − offset`, source-verified against `clipTimeToSourceFileTime`;
gate leg pre-seeds a non-zero offset so the naive formula fails) + CC-only MIDI trim. Bonus hardening:
**auto-tempo clips are now a logged decline** (their offset is beat-domain — the old unity-speed guard could
let one slip through and mis-trim; a latent W23 hole, closed). Remaining documented decline: auto-tempo.

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
| `--selftest-popout` is intermittent | W25 | Failed **once** in a full-floor run (6 PASS / 1 FAIL on the same binary; PASS on the pre-change baseline), with no WARN/ERROR — a leg assertion flipped. Its window-visibility / focus legs read live OS state behind a fixed 300 ms yield. Widen the yield **only if it recurs**; also archive per-gate reports on failure so the failing leg is knowable. Details → `devlog/wave-25-multistem-import.md`. |

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
| B4 capture count-in | `RecordController`, capture path, `TransportBar` | — |
| B6 instrument library (remaining) | `BrowserView`, `PluginHost`, `SessionView` | — |

*(The W24 wave consumed the rest of this table — B1 + B5 + B7 ran as the suggested 3-agent fan-out with
B2/B3 in the orchestrator's `main.cpp` pass, exactly as planned. B4 and B6-remaining don't conflict and
could even run together, but B4 wants its own focused wave per its risk note.)*

---

## The process (unchanged)

Build → **one** integration build (`cmake --build .\build --config Debug`; kill `Forge.exe` first) → the
**full selftest floor** (currently 48 gates; see `tests/SELFTEST.md`) → `--screenshot` → adversarial QC →
docs (`HANDOFF` / `STATUS` / `CLAUDE.md` counts / `SELFTEST.md` / a devlog) → **sanitize scan** → scoped
commit → **hold the push for the maintainer's OK**.

*Rules and gotchas → `../CLAUDE.md` · product brief → `DIRECTION.md` · state → `HANDOFF.md` · history →
`STATUS.md` · selftest contract → `../tests/SELFTEST.md`.*
