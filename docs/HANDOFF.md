# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-07-06**, end of **"W18 — frontier program
> Wave 7 fast-follows: whole-scene-send + send-as-loop"** — completing the **SEVENTH** wave of the 10-wave
> **frontier build program** ([[forge-frontier-program]] / `docs/frontier-program.local.md`). ✅ **Frontier
> Wave 7 is now COMPLETE** (all three items: capture core [W17] + whole-scene-send + send-as-loop [W18]).
> W18's 4-dimension adversarial QC swarm found **zero confirmed defects** (unlike W17, which caught and fixed
> one) — a clean pass, no fix-then-reverify cycle needed. **Built + gated + committed** (`4df11e1`) — **NOT YET
> PUSHED**, awaiting the maintainer's go-ahead.

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`**. **W07–W16 are PUSHED to `origin/main`** (tip `96b1037`, sanitize-clean; local `main` ==
`origin/main` as of that tip). W17 (`f89109c`) and W18 (this session) are committed **locally on top of
`96b1037`, NOT YET PUSHED**. Last build **clean** (MSVC Debug, 0 warnings) · **all THIRTY-FOUR selftests PASS**
(W18 adds ONE new gate, `--selftest-scenesend`, and extends `--selftest-sendarrange` with a send-as-loop leg;
floor **33 → 34**). ⚠ **History was rewritten in a prior session** (`git-filter-repo`) to scrub a real-identity
leak from an earlier commit's HANDOFF prose, then force-pushed — all pre-`9cc7f04` commit hashes at/after the
old `09c4928` changed (e.g. `09c4928` → `6ca11cd`).
Shipped (W18 — frontier Wave 7 fast-follows): **whole-scene "Send to Arrangement"** — a new
`SceneColumnComponent` row-menu item sends every filled clip in a scene to its own track, all aligned at ONE
shared start beat (the max append point across only the filled-slot tracks), in one undo transaction. New
`ProjectSession::sendSceneToArrangement` seam; the item was **appended** to W15's existing scene-row
`PopupMenu`, never a competing rewrite (the frontier program critic's flagged territory-collision point).
**Send-as-loop** — `sendSlotToArrangement` gained a `keepAsLoop` parameter (default false, every existing
caller unchanged); a second slot-menu item, "Send to Arrangement (as loop)", shown only when the source is
looping, keeps the sent copy's loop range instead of normalizing to a one-shot. Both reuse W17's
`insertClipCopyOnTimeline` helper verbatim. New gate `--selftest-scenesend` (floor 33→34); extends
`--selftest-sendarrange` with a loop-toggle leg (a control proves the default path still normalizes). **A
4-dimension adversarial QC swarm — engine logic, the signature-change ripple, the menu-append discipline, and
gate/wiring quality — came back CLEAN across the board**, no confirmed defects. Full record →
[devlog/wave-18-scene-send-loop.md](devlog/wave-18-scene-send-loop.md).
Shipped (W17 — frontier Wave 7, capture core): a new `ProjectSession` performance-capture subsystem
(`startPerformanceCapture` / `stopPerformanceCapture` / `performanceCaptureTick`, an owned ~30 Hz message-thread
`juce::Timer`) samples `LaunchHandle::getPlayedRange()` — a SINGLE current span, not a history buffer — to
accumulate one sealed span per launch→stop transition, then stamps a one-shot clip per span onto the
Arrangement at its **absolute captured Edit beat** (not append-at-end, which is what distinguishes this from
W10's `sendSlotToArrangement`). The W10 clone/normalize/audibility-flip/metadata-strip body was factored,
behaviour-preservingly, into a shared `insertClipCopyOnTimeline` helper — `--selftest-sendarrange` stays
byte-identical (confirmed by independent QC). A new **"Capture"** button joins the transport bar (distinct from
**Rec**, the MIDI-into-slot take path); `TransportBar::resized()` was rewritten to flex the whole 7-button+2-combo
strip proportionally so nothing starves at the 760 px window minimum (QC hand-verified the arithmetic).
**A 4-dimension adversarial QC swarm caught one confirmed defect, fixed before ship**: commit was resolving
each span's source clip by **cell index** (`getClipSlot(track,scene)->getClip()`) rather than by **identity** —
so clearing a clip from a slot mid-capture-session and dropping a *different* clip into the same cell would
silently stamp the replacement clip's content at the original clip's captured beat. Fixed via a captured
`te::EditItemID` (taken at span-OPEN time, never re-derived) resolved at commit through `te::findClipForID` (an
edit-wide lookup, so a since-moved clip still resolves; a genuinely deleted one degrades to a logged skip,
never a silent wrong-content stamp). Full record →
[devlog/wave-17-performance-capture.md](devlog/wave-17-performance-capture.md).
Shipped (W16 — frontier Wave 6): six dimensions, each proving the shell's **real** entry point
(`doUndo()`/`doRedo()`, the popout's actual `keyPressed` forward chain) rather than the `ed->undo()` bypass
every pre-existing gate used. **Building it surfaced a confirmed, severe engine defect**:
`FourOscPlugin::flushPluginStateToValueTree()` (`libs/tracktion_engine`) unconditionally performs an
`UndoManager`-tracked `ValueTree::addChild` on **every save** (even with an empty mod-matrix) — since
`doUndo()`/`doRedo()` always save, this discards the pending redo stack and plants a phantom top-of-stack entry,
on **any edit with a FourOscPlugin** (Forge's own default instrument, auto-created on every MIDI track). **Redo
is unavailable immediately after any Undo, in production, today** — not fixed this wave (vendored engine code,
a maintainer decision per the standing "do not fork the engine" default); documented + monitored via a
non-gating `redoAvailableAfterSingleUndo` field and a new CLAUDE.md gotcha. **A second, separate finding**: the
wave's own dimension-5 fix (piano-roll note-staleness) initially wiped the user's note selection + scroll
position on ANY unrelated undo/redo — caught by adversarial QC, fixed via a new
`PianoRollView::refreshAfterExternalEdit()` that only does a destructive rebuild on a genuine structural note
change. Full record → [devlog/wave-16-w05-qc-debt.md](devlog/wave-16-w05-qc-debt.md).
Shipped (W15 — frontier Wave 5): the Session scene grid becomes an editable set-list — **double-click a scene
name to rename** (blank falls back to the number); right-click a scene row → a **PopupMenu** (`Stop scene /
Rename… / Delete scene / Move up / Move down`) replacing the bare right-click-stop. All three (rename / delete /
reorder) ride the user undo stack (one Ctrl+Z per gesture) via new `ProjectSession` seams
(`setSceneName` / `deleteScene` / `moveScene`) + a file-disjoint `SceneColumnComponent` (inline `TextEditor` +
row menu, Fable). **No engine `moveScene` seam exists** — reorder is raw lockstep `ValueTree::moveChild` on the
SCENES tree AND every track's CLIPSLOTS tree; a 5-dimension adversarial QC caught + fixed a **MAJOR** (the
uneven-slot-count reorder desync — `moveScene` now pads all tracks to full width first) + 2 MINORs. Full record →
[devlog/wave-15-scene-lifecycle.md](devlog/wave-15-scene-lifecycle.md).
Shipped (W14 — frontier Wave 4): the piano-roll gains **MIDI quantise** — press **`q`** to snap the selection
(or the whole clip) to the grid, starts-only, one undoable step, via a new header-only `MidiEditHelpers.h`
(engine `QuantisationType`, 50%-strength interpolation proven). Prior wave (W13 — frontier Wave 3): right-click
a filled Session slot → **Duplicate clip** / **Move to next slot**; **Ctrl+D** move / **Ctrl+Shift+D** copy.
Before that (W12 — frontier Wave 2):
right-click a filled Session slot → **Launch quantise** → *Global (inherit)* or
any of the 23 launch-Q values; the clip snaps on its own grid via new `ProjectSession` seams + a
`resolveEffectiveLaunchQType` proof bridge (6-dimension adversarial QC: **ship**). Prior wave (frontier Wave 1 =
W11 — the maintainer-flagged **#1 gap**, DIRECTION's identity core): right-click a filled
Session slot → **Follow action** (None / Stop / Play-again / Next / Previous / First / Last / Round-robin; Random
deferred to v2), **Loop** (checkable one-shot↔loop), and **Launch mode** (Trigger / Gate / Toggle) submenus, over
new `ProjectSession` seams + the `ClipSlotComponent` `onReleased`/`mouseUp` (Role B, file-disjoint). Follow actions
are consumed by the engine at graph-build (`EditNodeBuilder` → `SlotControlNode`), so **zero per-tick work**
(R1-safe). The engine's **FOLLOWACTIONS auto-plant footgun** is defeated — writing a follow-action duration on an
empty action list auto-plants `currentGroupRoundRobin`, so both seams set the type explicitly (new CLAUDE.md
gotcha). Gates **`--selftest-followaction`** (11 legs, incl. the footgun re-checks + the KEY `createFollowAction`
functor proof) + **`--selftest-launchmode`** (7 legs); floor **24 → 26**. **W11 adversarial QC (5 dimensions):**
follow-action / lifetime-R1 / loop-toggle **REFUTED clean**; **Trigger launch is byte-identical**; **3 routing
findings fixed** — the **Toggle queued-race** (a click during the launch-quantise pre-roll re-launched instead of
stopping → `isSlotActive` now = playing **or** queued), **Enter** now routes through the shared `launchOrToggle`
(keyboard can toggle off), and the W10 send-to-arrange copy **no longer carries** the launcher-only follow-action /
launch-mode onto the Arrange clip — plus the **Gate quantise quick-click documented** (works under free-trigger
launch quant; immediate-launch for Gate is a follow-up). Full record →
[devlog/wave-11-launcher-expressiveness.md](devlog/wave-11-launcher-expressiveness.md). Prior: **W10 — the
Session → Arrangement "Send to" bridge** (`40eccaf`; →
[devlog/wave-10-send-to-arrangement.md](devlog/wave-10-send-to-arrangement.md)), which completed the 5-wave
hands-on plan; its locked decisions are in memory ([[forge-handson-wave-plan]]).

> **⚠ W08 deferred findings (found by QC, NOT fixed — maintainer said document-only):** ① **[LOW/cosmetic] the
> Ableton master-strip opportunity** — the scene column runs full-height while the band shortens only the pad
> viewport, leaving the scene column dangling ~96 px below the pad clip + a ~168×96 empty bottom-right corner;
> the clean fix is to shorten the scene column to the pad viewport and fill the corner with a **master strip**
> (a good self-contained follow-up). ② **[MINOR, latent] strip re-bind edge** — `refreshControls()` empty→bound
> doesn't re-show/re-attach (unreachable today; a trap for a future strip-reuse optimization). ③ **[MINOR,
> latent] absolute-index re-resolve** — the strip resolves `tracks[index]`, not by pointer identity; safe today
> (no reorder op), but a future drag-reorder would drive the wrong track. None is a W08 blocker.

**STANDING MAINTAINER CONSTRAINTS (stated this session):** the maintainer has **no physical MIDI hardware**
and **runs no manual tests** — every feature must be headless-provable (selftest gates + `--screenshot`).
Hardware smoke items (Launchpad byte mapping, physical-CC MIDI-learn, APC40) stay parked until hardware
exists; do NOT propose them as next steps. Autonomous multi-agent waves are standing-approved, with model
tiers matched to task complexity. **Fable holds UI/UX design authority** (layout/function, legibility, ease
of use, clean-in-all-states, sequence lighting, tempo visuals, clock accuracy); design freedom is total
EXCEPT: dark theme stays; "clean means organized, not minimal" (never cut features for sparseness —
structure them); accent colours are a small semantic wayfinding vocabulary (one colour = one meaning); and a
traditional top menu bar is a standing request. The full charter is in [INTERFACE.md](INTERFACE.md) §1.

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

## What the LATEST wave did — W14 (frontier Wave 4: MIDI quantise)

**W14** gives the piano-roll **destructive MIDI quantise** — press **`q`** to snap the selection (or the whole
clip when nothing is selected) to the grid; note starts only, length preserved, one undoable step. Full record →
[devlog/wave-14-midi-quantise.md](devlog/wave-14-midi-quantise.md). A file-disjoint wave: a new header-only
`src/engine/MidiEditHelpers.h` (`forge::midiedit::quantiseNoteStarts` over the engine's `QuantisationType` — a
1:1 lift of its own unit test) + a `PianoRollView` 'q' trigger + the gate; NO ProjectSession touch, no CMakeLists
edit. The 5-reader verify swarm corrected a baked grid-mapping error (`gridBeats 0.25 → "1/4"`, a fraction of a
BEAT, NOT "1/16"). Gate **`--selftest-quantise`** (floor 28 → 29): snap-to-grid, length preserved, **50%-strength
interpolation** (0.1→0.05), undo. **QC (6 dimensions) — ship:** one nit fixed (Ctrl+Q was swallowed by quantise —
now guarded on `!isCommandDown`); grid math / length / stale-pointer safety / single-transaction undo / no
playback-quantise mutation all refuted clean. Groove (2 built-in swing templates on the same seam) is a
documented fast-follow.

## What a prior wave did — W13 (frontier Wave 3: grid clip primitives)

**W13** adds **slot→slot clip primitives** — right-click a filled slot → **Duplicate clip** / **Move to next
slot** (copy or move to the first empty slot below, auto-growing a row when the column is full); **Ctrl+D** /
**Ctrl+Shift+D** on the focused slot. Full record →
[devlog/wave-13-grid-clip-primitives.md](devlog/wave-13-grid-clip-primitives.md). Three `ProjectSession` seams
(`copySlotClip` / `duplicateSlotClip` / `moveSlotClip`) compose one file-local `cloneClipIntoSlot` helper
(`state.createCopy()` → fresh `EditItemID` → `te::insertClipWithState`); replace-on-filled + slot normalization
are engine-automatic, MOVE = copy-then-`clearSlot` in ONE undo transaction. Gates **`--selftest-duplicate`** +
**`--selftest-slotmove`** (floor 26 → 28). **QC (6 dimensions) — fix-then-ship:** the adversarial pass caught a
MAJOR the verify swarm missed — the engine **re-loops** a freshly-inserted non-looping clip, so a duplicated
**one-shot** came back looping (a W11 regression); `cloneClipIntoSlot` now re-asserts `disableLooping()` and a
new gate leg guards it. The other five dimensions refuted clean (incl. the `ensureScenes` history-wipe being a
pre-W3 inherited behaviour, and the real-UI MOVE being atomic).

## What a prior wave did — W12 (frontier Wave 2: per-clip launch quantise)

**W12** adds a **per-clip launch-quantise override**: a filled Session slot can snap on its own grid (a 1/16 hat
fill vs a 1-bar bass) instead of only the Edit-global launch quant. Right-click a filled slot → **Launch
quantise** → *Global (inherit)* or any of the 23 `LaunchQType` values. Full record →
[devlog/wave-12-per-clip-launch-quantise.md](devlog/wave-12-per-clip-launch-quantise.md). The engine's launch
resolver **already** preferred a per-clip `LaunchQuantisation`, so the wave added only five `ProjectSession`
seams (`setClipLaunchQuantisation` / `getClipLaunchQuantisation` / `clearClipLaunchQuantisation` /
`clipInheritsGlobalLaunchQuantisation` + the **`resolveEffectiveLaunchQType`** bridge that lets
`--selftest-session` assert precedence through the *real* resolver, not a mirror), a `SessionView` "Launch
quantise" submenu (inline dispatch like the W11 launcher submenus — no new callback, no `ClipSlotComponent`
change), and a `perClipLaunchQ` leg on `--selftest-session` (**no new gate — floor stays 26**). New CLAUDE.md
gotcha: the engine method is spelled **`usesGlobalLaunchQuatisation`** (verbatim typo, missing the `n`) and is
**inverted** (`false` ENABLES the override); const readers never dirty the tree (unlike the FOLLOWACTIONS
footgun). Built orchestrator-serial after a **5-reader source-verification swarm** froze the spec; a
**6-dimension adversarial QC** returned **ship / 0 defects** (incl. confirming the change is fully undoable via
the engine's UndoManager binding).

## What a prior wave did — W11 (frontier Wave 1: launcher expressiveness)

**W11** — the FIRST wave of the frontier build program — summarised in the intro blockquote
above and recorded in full in
[devlog/wave-11-launcher-expressiveness.md](devlog/wave-11-launcher-expressiveness.md): per-clip **follow
actions** + **loop-toggle** + **launch modes** (Trigger/Gate/Toggle), built to a frozen source-verified spec by
the orchestrator (serial spine: `ProjectSession` seams · `SessionView` submenus · `main.cpp` gates) + one
file-disjoint agent (Role B: `ClipSlotComponent` `onReleased`) + a 5-dimension adversarial QC. The engine
FOLLOWACTIONS auto-plant footgun is defeated; follow actions ride the engine's graph-build with **zero per-tick
work**. Gates `--selftest-followaction` (11 legs) + `--selftest-launchmode` (7 legs), floor 24→26. QC: 3 clean
dimensions REFUTED, **Trigger byte-identical**, 3 routing findings fixed (Toggle queued-race, Enter mode-aware,
W10 arrange-clip metadata strip) + the Gate-quantise quick-click documented.

## What a prior wave did — W10 (the Session → Arrangement "Send to" bridge)

Recorded in [devlog/wave-10-send-to-arrangement.md](devlog/wave-10-send-to-arrangement.md): the explicit,
one-directional "Send to Arrangement" action (copy a filled Session slot's clip onto the same track's linear
Arrange timeline, appended at the end), which **completed the 5-wave hands-on plan**. New
`ProjectSession::sendSlotToArrangement` seam (Tracktion's own `insertClipWithState` clone idiom) + gate
`--selftest-sendarrange`; 5-dimension QC fixed 2 confirmed defects (the [HIGH] `playSlotClips` silence + the
[Medium] slot auto-tempo/loop carry-over).

## What a prior wave did — W09 (self-rendered instruments + an audible demo)

Recorded in [devlog/wave-09-instruments.md](devlog/wave-09-instruments.md): per-track instrument presets (4OSC
kick/bass + a Sampler with a self-rendered CC0 piano one-shot), a note-written C-minor demo, and a first-launch
welcome demo, built by one instrument-layer agent + orchestrator, then a 3-dimension adversarial QC (NO
blockers/majors; the instrument-layer finder refuted 10 candidate bugs).

## What a prior wave did — W08 (per-track Session mixer strips)

Recorded in [devlog/wave-08-session-mixer.md](devlog/wave-08-session-mixer.md): a fixed mixer band (meter ·
fader · pan · M/S per track column) under the Session grid + the ChannelTray made Arrange-only; a 4-dimension
QC found no blockers/majors (findings documented-not-fixed per maintainer direction).

## What a prior wave did — W07 (Session-grid interactions)

Recorded in [devlog/wave-07-handson-grid.md](devlog/wave-07-handson-grid.md): delete clip · +Track · +Scene
(dynamic scene count) · real file drag-drop, over three new `ProjectSession` seams; a 5-dimension QC fixed the
MAJOR scene/pad `rowBand` drift + the HIGH detached-drawer-clip on delete. The **W06** wave is detailed below.

## What a prior wave did — W06: the hands-on wave, part 1 (control bar & HUD)

A long, multi-phase session. The arc:

1. **Documentation audit** (`7f03974`): 4 read-only auditors + direct review brought README / INTERFACE /
   ARCHITECTURE / FEATURE_CATALOG / DIRECTION current (they'd drifted — the whole W04 UX wave was still
   marked "planned"; the counts said "five selftests"). LOGGING / mockups / all 26 devlogs verified clean.
2. **The maintainer's first hands-on session** with the built app → **15 UI/UX notes**. Scoped by **7
   source-verified investigation agents** (cited file:line + engine headers) into an **adversarially-
   verified 5-wave plan** — 6 QC lenses + a completeness critic caught 3 structural blockers before any
   code was written. **Locked decisions (load-bearing):** "+ session" means a new **scene row** (not a
   project — File ▸ New already does that); instruments/library v1 = **self-rendered CC0 one-shots** only
   (no third-party packs — public AGPLv3 repo); Session ↔ Arrange stay **separate** with an explicit
   "Send to Arrangement" action *later* (never auto-mirror). Plan + decisions: memory [[forge-handson-wave-plan]].
3. **W06 = plan Wave 1** (control bar & HUD), shipped in **`e670ab5` (code)** + docs on the `7f03974`
   baseline. Built by **5 file-disjoint implementation agents** (each edited only its territory and
   *proposed* the shared `main.cpp`/CMake wiring) → orchestrator consolidation (wiring + the new gate +
   the build) → clean build (0 warnings) → **17-gate floor** → a **4-dimension adversarial QC**
   (per-finding skeptics, default-refute) → 2 fixes. Full record: [devlog/wave-06-handson.md](devlog/wave-06-handson.md).
4. **Feature-backlog draft** (`758dbb1`): a candidate-feature list was drafted to feed planning, then **removed**
   from the repo for public-repo hygiene (it was a derivative index of a copyrighted third-party manual; the
   ideas remain in git history). Copyrighted reference PDFs stay `.gitignore`d (`*.pdf`), never committed.

**Wave 1 shipped:**
- **View buttons → top-left** (`ui/ControlBar.cpp`) and **Browser → a `juce::Path` folder icon** (the
  first vector icon in the repo; amber-tinted when open; `ControlBar::setBrowserOpen` synced from the
  shell at the open edge + the close-settle).
- **Free-trigger launch** (`services/files/ProjectSession.{h,cpp}` + `ui/transport/TransportBar.{h,cpp}`):
  a launch-quant `ComboBox` (None / … / 1 Bar / …) over the Edit-level global
  `Edit::getLaunchQuantisation().type`. **`LaunchQType::none` = free trigger** (no bar-snap) — the engine
  already implements the behavior; this only exposes the seam. New `launchQRoundTrip` leg in `--selftest-session`.
- **Clickable tempo popup** (`ui/transport/LcdDisplay.{h,cpp}` + NEW `TempoPopup.{h,cpp}` + NEW `TapTempo.h`
  + `engine/EngineHelpers.h`): the LCD's tempo zone opens a `juce::CallOutBox` (**first in the repo**) —
  editable BPM field, ±0.1/±1.0 steppers, and **tap tempo** over a pure `TapTempo` estimator; writes route
  through `EngineHelpers::setTempoAt` clamped [20,300]. Gate: **`--selftest-taptempo`**.
- **File ▸ Exit** (Ctrl+Q → `systemRequestedQuit`; menu-gate File count 9 → 10) and a **cosmetic launch
  splash** (`ui/SplashWindow.h` — honestly scoped: it can't mask the ~8 s engine ctor, and it's skipped
  under every `--selftest-*`/`--screenshot` flag). **Clock button kept in the transport row** (the default).
- **W06 QC (4 dims × per-finding skeptics):** 2 confirmed — the launch-quant combo collapsed to **0 px**
  (unclickable) in the ~760–848 px window band (**fixed**: the count-in + launch-quant combos now share the
  trailing space so neither vanishes); a stale folder-icon tint in the `arrange_tray` screenshot
  (**fixed**). The correctness-critical dimensions — free-trigger index mapping and tempo write — verified
  **clean**.

> Prior wave (W05, `5e5dcf2`, PUSHED — same continuous session): **global Undo/Redo** (Edit ▸ Undo/Redo +
> Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y over the Edit's own `UndoManager`; per-gesture transaction seals; a synchronous
> cross-surface refresh fan-out; undo blocked while recording; a piano-roll detached-clip guard; gate
> `--selftest-undo`), **scene-column polish**, the **tray↔mixer strip-widget extraction**
> (`ui/common/StripWidgets.h`), the **empty-centre hint**, and **popout placement persistence**. ⚠ **W05's
> adversarial QC was LIMIT-INTERRUPTED — its undo-correctness + shell-integration dimensions NEVER RAN and
> are STILL OWED** (only polish-regressions ran; its 2 findings were fixed). Full record:
> [devlog/wave-05-undo.md](devlog/wave-05-undo.md).

> Prior wave (W04b, `cc27300`, PUSHED — same session): **tear-off mixer/piano-roll windows**
> (`ui/popout/PopoutWindow.{h,cpp}`; reparented live shell members, never recreated; deferred close; keys
> bubble to the shell; Mix-while-out fronts the popout; gate `--selftest-popout` with its noGhostOverlay
> assert — the QC blocker was a restored view coming home visible at stale bounds, overlaying the shell),
> **animated B/E slide-outs** (scalar-lerp timer through `resized()`; all programmatic opens via
> `revealDrawer()` or the slide target desyncs), **the timecode LCD zone** (width-gated 4th zone; default
> launch width 1200), **the shared PeakMeter + a channel-tray meter**, **Session-grid tray-follow**, and
> **the Arrange playhead → timeTempo**. Full record: [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md).

> Prior wave (W04a, `41e3139`, PUSHED — same session): **the transport LCD** (pure LcdModel; count-in digits
> derive from the engine's CLICK GRID — the punch is never beat-snapped; the count-in latch arms only on a
> record rising-edge from a stopped transport, and `LcdDisplay::setEdit` early-returns on a same-edit call
> or a menu resync wipes the latch mid-pre-roll), **the channel tray** (Files | Channel sidebar tabs;
> per-tick track re-validation; visibility-gated poll), **the menu bar** (one command table; the control
> bar's eight file buttons moved INTO it — the QC blocker fix that also let the transport + LCD fit; the
> transport Rec button was wired to NOTHING since Phase 1, now fixed), **sequence lighting** (playing =
> pulsing playGreen, queued = playGreenDim, amber = selection only), persisted section sizes, the 8-state
> matrix. Gates `--selftest-lcd` / `--selftest-menu` / `--selftest-tray`. Full record:
> [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md).

> Prior session (W03, `ffa494d`, PUSHED): **volume/pan automation lanes** (`--selftest-automation`) ·
> **MIDI-clock out** (`--selftest-sync`, wire-byte capture; Ableton Link deferred — not vendored) · **LUFS on
> the export worker** (abort-guarded) · **live cross-surface refresh** (`--selftest-livesync`) · the
> INTERFACE.md Session-first rewrite · and a **latent product UAF fixed**: mixer meters held raw
> `LevelMeasurer*` into owners that can die (the playback context; cull-able plugins) — all meter sources are
> now `juce::WeakReference<te::LevelMeasurer>`. Key facts that survive: the engine's automation curve
> activation is deferred unless `updateStream()` is called (the seam does it); engine device IDs are NOT juce
> identifiers (match by NAME) and MidiOutputDevice props persist keyed by NAME; a UI seam a gate can't see
> can ship unwired — verify shell wiring explicitly. Full record:
> [devlog/wave-03-features.md](devlog/wave-03-features.md).

> Prior session (W02, `bb9ef5e`, PUSHED): **MIDI-learn HW routing (focused-Edit `ForgeUIBehaviour`,
> `--selftest-midiinput`) · a Forge-native Launchpad control surface (`--selftest-controlsurface`; byte
> mapping still needs a real device) · offline BS.1770-4 LUFS on export (`--selftest-lufs`)**. Key facts that
> survive: a `VirtualMidiInputDevice` has no `controllerParser` (physical-CC drive is hardware-only proof);
> Tracktion's `ControlSurface` clip-launch is an unwired `std::function`, so the driver calls
> `ProjectSession` directly; no non-mutating post-fader master sample tap exists without forking the engine.
> Full record: [devlog/wave-02-features.md](devlog/wave-02-features.md).

> Prior session (Wave 01, `e3b8c7c`, PUSHED): Forge's **first flat parallel multi-CLI wave** — six file-disjoint
> feature CLIs (P1–P6) each committed a scoped commit, then the orchestrator (P7) implemented the two shared
> `ProjectSession` seams, wired everything into the single integration build, and ran adversarial QC. Baseline
> was `6100fb9`. Full record: [devlog/wave-01-features.md](devlog/wave-01-features.md).

> Prior session (`160f6cc`): **W7 — MIDI record into Session clip slots** — a track can be MIDI record-armed and
> an empty slot captured (**Ctrl+Enter** / right-click "Record into slot") straight into a born-audible
> `te::MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`
> ([devlog/midi-record-design.md](devlog/midi-record-design.md)). Before that (`8d15234`): **Session-grid vertical scroll +
> app-wide logging** — the 16-scene grid scrolls Ableton-style with fixed ~46 px pads and a pinned scene column
> that tracks the pads ([devlog/session-scroll-design.md](devlog/session-scroll-design.md)); and an app-wide
> logging + error-handling subsystem (`src/core/Log.*`, ~90 seams instrumented) with logging-at-the-seam a
> standing build principle ([LOGGING.md](LOGGING.md) · [devlog/logging-design.md](devlog/logging-design.md)).
> Before that (`06f3cf6`): **the Session clip-launch grid** — SessionView as the default `ViewMode`, on
> Tracktion's `ClipSlot` / `Scene` / `LaunchHandle`; source-grounded design + file-disjoint build → 5-lens
> adversarial QC → independent fix re-verify → `--selftest-session` + `--screenshot`
> ([devlog/session-design.md](devlog/session-design.md)). Before that: the **direction reset → DIRECTION.md** +
> the to-scale [mockups](../mockups/) (sheet 00 = the Session grid), and the **MIDI MVP (W1–W5) + W6 piano-roll
> polish** ([devlog/midi-build.md](devlog/midi-build.md)) — clips / 4OSC / piano-roll ride inside slots.

---

## What exists today (the building blocks)

Phases 0–4 + startup hardening + MIDI MVP/W6 + **W7 MIDI record into slots** + the **Session clip-launch grid**
(with vertical scroll) + the **logging subsystem** + the **Wave-01 feature seams** + the **W02 engine seams** +
the **W03 features** + the **W04a/W04b UX waves** + **W05 undo/polish** (plus W06–W11 on top — see the wave
summaries above), all shipped, building clean, the full selftest floor (now **TWENTY-SIX** gates) passing:

- **Session grid (PRIMARY view)** — tracks × 16 scenes of launchable clips on `ClipSlot` / `Scene` /
  `LaunchHandle`; single-click launches (instant), right-click "Edit clip" (launch-free), double-click opens;
  keyboard arrows/Enter launch; **audible, bar-quantised** launch; pinned scene column + MASTER stop-all;
  25 Hz gated state poll. **Ableton-style vertical scroll** — fixed ~46 px pads, all 16 scene rows reachable,
  the pinned scene column tracks the pads. Default `ViewMode` (**F8**). Details:
  [devlog/session-design.md](devlog/session-design.md) + [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Logging + error-handling (`src/core/Log.*`)** — an app-wide `juce::Logger` sink (file at
  `%APPDATA%\Forge\logs\forge.log`, 1 MiB + `.1` rollover, + stderr echo) with a crash handler and
  `FORGE_LOG_*` macros; ~90 fallible seams instrumented. RT-thread- and poll-safe by rule. Logging fallible
  seams as you build them is a **standing build principle** — [LOGGING.md](LOGGING.md).
- **Project** save/load (`.tracktionedit`), **audio import**, an **arrange timeline** (waveforms, playhead,
  clip drag-to-move, selectable snap grid).
- **Transport** (play/stop/record/loop) and **recording** — verified end-to-end on real hardware
  (`--selftest-record` captures a real take); output-only startup, lazy capture-input open.
- **MIDI** — clips on any track, born audible via a default **4OSC** at chain index 0; a **piano-roll**
  (draw/move/resize/delete, velocity lane, multi-select, copy/paste); **W7 record into Session slots** — MIDI
  record-arm a track, capture an empty slot (**Ctrl+Enter** / right-click "Record into slot") straight into a
  born-audible `MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`. Details:
  [devlog/midi-record-design.md](devlog/midi-record-design.md).
- **Mixer** (strips, plugin inserts w/ bypass+reorder, master + post-fader meter, **A/B aux sends → aux-return
  strips**), **plugin hosting** (built-in + VST3/AU scan + floating editors), **Browser**, **clip Inspector**,
  **WAV export + stems** (now **async, off-thread, with a progress/cancel panel**).
- **Wave 01 additions** — **metronome + count-in** (TransportBar Click toggle + selector), **markers** (a
  timeline marker bar over the arrange view), **MIDI-learn** (a **Ctrl+L** param picker over Tracktion's native
  mapping store), and an automatic **anti-click edge fade** on imported audio.
- **W02 additions** — **MIDI-learn hardware routing** (a focused-Edit `ForgeUIBehaviour` so the engine's native
  CC→param routing reaches an actual Edit; real-hardware CC drive still a smoke item — a virtual device has no
  `controllerParser`), a **Forge-native grid control surface** (device-agnostic driver + a Novation Launchpad
  driver built to the MIDI spec; drives `ProjectSession` directly, pushes LEDs from `SlotVisualState`; inert
  without hardware — byte mapping needs a real device), and **offline LUFS** (a BS.1770-4 integrated-loudness +
  true-peak analyzer run on the export render; the integrated LUFS shows in the export-done status strip).
- **W03 additions** — **volume/pan automation lanes** in Arrange (an **A** toggle per track expands a 46 px
  lane; add/drag/delete points; live repaint on external curve edits; persisted in the `.tracktionedit`),
  **MIDI-clock out** (a TransportBar **Clock** toggle over the `MidiClockSync` seam), **LUFS analysis on the
  export render worker** (the UI never blocks; per-chunk abort guard), **live cross-surface refresh**
  (mixer/inspector reflect engine changes without re-selecting), and **WeakReference-sourced mixer meters**
  (the latent freed-context/plugin-cull UAF class is closed).
- **W04a additions** — **the transport LCD** (bars|beats / tempo / key·sig centre of the control bar; the
  face becomes a beat-locked count-in digit with a record-red pulse during record pre-roll), **the channel
  tray** (Files | Channel left-sidebar tabs; the selected track's pan/sends/inserts/fader/M-S in Arrange —
  clip selection follows to its track), **the traditional menu bar** (File/Edit/View/Transport/Help with
  shortcut labels; the control bar's file buttons moved into it; the dead transport Rec button fixed),
  **sequence lighting** (playing pads pulse playGreen on the beat, queued breathe playGreenDim; amber =
  selection only), **persisted browser/drawer sizes**, and the state-matrix screenshot expansion.
- **W04b additions** — **tear-off windows** (View ▸ Pop Out Mixer / Piano Roll — the live views float on
  their own desktop windows and return on close; shortcuts still reach the shell), **animated B/E
  slide-outs**, **the timecode LCD zone** (fourth zone, width-gated; default launch width 1200), **a
  channel-tray level meter** (via the shared `ui/common/PeakMeter.h`), **Session-grid tray-follow** (grid
  focus binds the tray), and the **window-level `shell_window` screenshot** (menu bar finally captured).
- **W05 additions** — **global Undo/Redo** (Edit ▸ Undo/Redo with live enablement + Ctrl+Z/Ctrl+Shift+Z/
  Ctrl+Y; per-gesture transaction seals; blocked with a status message while recording; a synchronous
  cross-surface refresh fan-out; a piano-roll detached-clip guard), **scene-column polish** (hover /
  tooltips / full-width STOP ALL / beat-pulse parity), **the strip-widget extraction**
  (`ui/common/StripWidgets.h` — tray↔mixer styling once), **the empty-centre hint**, and **popout
  placement persistence**.
- **W06 additions** (hands-on wave 1) — **view buttons top-left**, the **Browser folder icon** (first
  `juce::Path` icon in the repo), a **free-trigger launch-quant selector** (`LaunchQType::none` over the
  Edit-level global), a **clickable tempo popup** with tap-tempo (first `CallOutBox`; `EngineHelpers::setTempoAt`
  clamps [20,300]; gate `--selftest-taptempo`), **File ▸ Exit**, and a **cosmetic launch splash**.

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

> W07–W11 are **committed + PUSHED to `origin/main`** (through `0f9d5cc`, sanitize-clean; local `main` ==
> `origin/main`). Hardware smoke tests and manual GUI passes are **permanently parked** (standing
> constraints at the top); the path forward is the headless-provable roadmap. The **hands-on plan is COMPLETE**;
> the active track is now the **10-wave frontier build program** ([[forge-frontier-program]] /
> `docs/frontier-program.local.md`) — a discovery swarm's source-verified, file-disjoint plan for Pillar 1 + "The
> gap". **Wave 1 (W11) shipped**; standing call: build the program's waves autonomously (each: file-disjoint
> agents → orchestrator build + gates + adversarial QC) and hold each push for the maintainer's OK.

1. **✅ DONE: frontier Waves 2–6 (W12–W16)** — per-clip launch quantise + grid clip primitives + MIDI quantise +
   Session scene lifecycle + **W16 (owed W05 QC-debt discharge)**, all **PUSHED to `origin/main`** (tip
   `96b1037`). W16 discharges the last standing W05 debt (undo-correctness + shell-integration) and surfaced a
   confirmed, unfixed engine defect (redo wiped by `FourOscPlugin`'s mod-matrix flush — see the CLAUDE.md
   gotcha; a maintainer decision, not fixed this wave).
   **✅ DONE: frontier Wave 7 — COMPLETE (W17 + W18)** — performance capture (the real Session→Arrange
   bridge): a Global "Capture" toggle records which clips launched, when, and for how long, then stamps them
   onto each track's Arrangement at the captured beat (W17); whole-scene-send + send-as-loop (W18), both
   riding W17's `insertClipCopyOnTimeline` seam. **Built + gated (34/34 PASS) + committed locally
   (`f89109c` W17, this session's commit W18) — NOT YET PUSHED.** W17's QC caught + fixed one confirmed defect
   (identity-vs-cell-index resolution); W18's 4-dimension QC swarm came back clean across the board.
   **▶ NEXT: frontier Wave 8 — Session mixer polish** (master corner + peak-hold) — full ordered program + the
   critic's corrections in `docs/frontier-program.local.md`. **W17 follow-ups (documented):** a pumped-render
   audibility leg for `--selftest-capture` (parked, same class as the W09/W10 render-leg follow-up); the
   reseal heuristic's dependency on `LaunchHandle::nudge`/`setLooping`/`playSynced` staying uncalled (see the
   new CLAUDE.md gotcha — revisit if a future wave wires per-clip handle-level looping). **W16 follow-ups
   (documented):** the
   `FourOscPlugin`
   redo-wipe defect itself (fix requires patching vendored `libs/tracktion_engine` — explicitly a maintainer
   call, see the gotcha). **W15 follow-ups (documented):** a save→reload round-trip leg for the scene
   gates; drag-to-reorder (parked — no headless mouse-drag driver); scene colour / multi-select. **A benign W15
   QC note:** an unrelated grid rebuild landing mid-rename silently commits the partial name rather than
   discarding it (no UAF; Ctrl+Z reverses). **W12 follow-ups (documented):** a save→reload round-trip leg for
   `--selftest-session`; a curated launch-Q submenu subset (a Fable call). **W11 follow-ups (documented):**
   immediate-launch for Gate (instant click-hold under any quant); a disk save→reload leg for
   `--selftest-launchmode`; Random/weighted/group follow-actions (v2). **W10 follow-ups (documented, not built):**
   (a) **whole-scene "Send to Arrangement"** — send every filled clip in a scene to its track, aligned at one
   start (the natural next extension; deferred by the single-clip scope choice); (b) **send-as-loop** — the copy
   is normalized to a one-shot (the conventional default); a sent loop *staying* a tempo-locked loop is a one-line
   product toggle if wanted; (c) runtime-audio rendering of the arrangement (audibility is proven by the
   `playSlotClips` state assertion, not by sampling non-zero output — same class as the W09 render-leg follow-up).
2. **W09 follow-ups (documented, not fixed).** (a) **A render/ingestion gate leg** — `--selftest-demo` proves the
   piano one-shot exists on disk + the Sampler is inserted, but not that the Sampler *ingested* the sample (an
   async load); pumping the loop + asserting a note renders to non-zero audio would prove the final audible link.
   (b) The **browsable CC0 instrument library** (deferred from W09's scope choice) — needs the missing
   browser→Session-slot interaction; its own wave.
3. **W08 deferred QC follow-ups (documented, not fixed — maintainer direction).** Best next-wave candidates:
   (a) **the Ableton master-strip** — shorten the Session scene column to the pad viewport and fill the
   ~168×96 empty bottom-right corner with a master strip (fader/meter over the existing MASTER "stop all"); a
   clean self-contained ticket. (b) The two **latent strip traps** (the `refreshControls()` empty→bound re-bind
   edge; the absolute-index-not-identity re-resolve) — one-line invariant comments now, real fixes only if a
   strip-reuse or drag-reorder feature is added. (c) Carried from W07: **aux-return ordering** cosmetic + the
   **grid-local-only** +Track/+Scene affordances (menu parity is a Fable call).
3. **⚠ STILL PARTIALLY OWED: the W05 QC dimensions.** W07/W08 QC re-exercised undo-correctness + shell-integration
   on the *new* surfaces, but the BROADER W05 debt is **not** cleared: the full undo-correctness sweep across
   all five W05 mutation hooks (undo interleaved with launch/record/project-swap; the record gate) and
   shell-integration for **torn-off popouts** (refresh fan-out + key routing with popout focus) still want a
   dedicated adversarial pass.
4. **Later hands-on waves.** Wave 5 (deferred) = the explicit Session → Arrangement "Send to" bridge (the real
   answer to the "Session clip doesn't appear in Arrange" note).
4. **Planning source.** The active backlog is the **frontier build program** (`docs/frontier-program.local.md` /
   [[forge-frontier-program]]) — the discovery swarm's file-disjoint plan for Pillar 1 + "The gap". (A prior
   third-party-guide feature-mining note was removed for public-repo hygiene; its top pick shipped as W11.)
5. **Deferred/parked (unchanged).** Ableton Link (vendoring decision); aux-send/return live-sync;
   plugin-param automation lanes; comping; live short-term LUFS meter (needs an engine fork); macOS build;
   and — parked until hardware exists — the Launchpad byte-mapping smoke test, physical-CC MIDI-learn, and
   the APC40 mkII driver.

---

## Build, run, verify

```sh
# Full cmake path — winget doesn't refresh PATH in these shells.
& "C:\Program Files\CMake\bin\cmake.exe" --build ".\build" --config Debug

& ".\build\Forge_artefacts\Debug\Forge.exe"                    # the app (opens on the Session grid)
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest         # headless playback check     → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-record  # headless recording check    → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-session # Session-grid audibility gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midi    # MIDI-record-into-slot gate  → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midilearn # MIDI-learn CC→param bind gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midiinput # focused-Edit HW-routing gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-controlsurface # virtual pad→launch + LED poll → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-lufs    # BS.1770-4 LUFS (buffer + worker-thread + abort legs) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-automation # volume-curve drives playback gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-sync    # MIDI-clock wire-byte capture gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-livesync # cross-surface live-refresh gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-lcd     # LCD model + pad-pulse curve (pure) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-menu    # menu-bar model walk (pure) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-tray    # channel-tray live-sync gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-popout  # tear-off round-trip gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-undo    # undo/redo round-trip gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-taptempo # tap-tempo model + tempo-write seam gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-slotdelete # clearSlot delete + undo-restore gate (W07) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-addtrack # appendAudioTrack gate (W07) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-scene    # dynamic scene count > 16 gate (W07) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-dragdrop # session+arrange file-import + replace-undo gate (W07) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-sessionmixer # per-track Session strip vol/pan/M-S sync gate (W08) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-demo     # audible demo: instrument presets + seeded notes (W09) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-sendarrange # Session→Arrange copy: fidelity + audibility + one-shot + undo (W10) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-followaction # per-clip follow-actions + loop-toggle (footgun defeated) (W11) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-launchmode  # per-clip launch mode Trigger/Gate/Toggle state+persist (W11) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-capture  # performance capture: accumulate + commit at absolute beat + identity-resolve + undo (W17) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-scenesend # whole-scene send: shared-start alignment across filled-slot tracks + atomic undo (W18) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --screenshot       # 10-state matrix (base session now shows the mixer band) → %TEMP%\forge_shot_*.png
# Selftests write %TEMP%\forge_phase0_selftest.log.  First clone: git submodule update --init --recursive
```

Regenerate the mockups (needs Docker; the `forge-dxf` image exists on this box):
```sh
cd mockups/src && MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" forge-dxf:latest python build.py
# then copy out/*.dxf → mockups/ and out/*.png → mockups/preview/   (src/out/ is gitignored)
```

---

## ⚠ Gotchas

- **`SessionLayout::rowBand` requires IDENTICAL heights across the columns it partitions (W07 — a MAJOR).**
  It floor-divides `height / rowCount`, so any per-column height difference — even the 8 px horizontal-scrollbar
  band the scene column had subtracted — changes the row PITCH and drifts scene-launch row N from pad row N
  (46 vs 45 px/row → ~19 px at 20 scenes, growing with scene count). The pinned scene column and the track
  columns MUST be sized to the same `contentH`. Pre-existing (present at 16 scenes whenever the H-scrollbar
  shows) but masked by the 1480×940 `--screenshot` size (no scrollbar there); render regression-prone layout
  states at a size that actually triggers the condition (the short-window `session_*` captures do).
- **A drawer holding a `Clip::Ptr` detaches on ANY structural delete, not just undo/redo (W07 — a HIGH, found
  by two independent QC finders).** The Ptr keeps a removed clip alive but PARENTLESS; further edits write to a
  dead state tree (silent no-op edits + undo-stack pollution). W05 guarded this only on the undo + swap paths;
  the new Session "Delete clip" reopened it. Reconcile the drawer (piano-roll AND `DetailView`) after every
  mutation that can delete a clip — now centralised in `MainComponent::reconcileDrawerClip()`, called from
  `undoOrRedo` + `sessionView.onEditMutated`.
- **Two independent build agents can each pick a clashing "neutral" accent (W07 — a QC minor).** Colours are a
  shared vocabulary; when fanning UI work out, PIN the drop/hover/feedback colours in the brief or QC will
  (correctly) flag the divergence — here teal vs gray for the same file-drop gesture (harmonised to `textPrim`).
- **GUI can't be driven headlessly here** — computer-use can't grab the dev-built `Forge.exe` window by
  name. Use **`--screenshot`** to *see* the UI (renders each view to `%TEMP%\forge_shot_*.png`) and a headless
  selftest hook (like `--selftest-session`) to *exercise* it. (Manual passes are off the table per the
  standing constraints — the W04 state-matrix screenshot harness is the planned substitute.)
- **A splash can't mask Forge's startup (W06).** `te::Engine` is a `ForgeApplication` DATA MEMBER, so its
  ctor (which enumerates audio devices, ~8 s) runs BEFORE `ForgeApplication::initialise()` — the earliest a
  window can show. A splash added in `initialise()` is therefore cosmetic only. Anything that genuinely
  wants to shorten perceived startup must move `te::Engine` out of member-init into a lazy path (a bring-up
  order change). Also: gate any startup window behind `mode == SelfTest::none` so the headless floor never
  spawns one.
- **A crowded flexible strip starves its LAST element (W06).** `TransportBar::resized()` gave the count-in
  combo `removeFromLeft` priority, so the new launch-quant combo (last) collapsed to 0 px across the
  ~760–848 px window band. Fix pattern: when two flexible controls share leftover space, **split it** (each
  keeps a floor, both grow to preferred once there's room) rather than first-come-first-served. No gate
  caught it (harness windows are 1040/1200/1480 px) — QC did. The window minimum is 760 px.
- **Click-through-except-a-zone for an overlay display (W06).** `LcdDisplay` must stay click-through except
  its tempo readout: it caches the painted `tempoZoneBounds` in a member, sets
  `setInterceptsMouseClicks(true, false)`, and overrides `hitTest` to return true ONLY inside that rect
  (and clears the rect in the branches where the readout isn't painted, so a stale zone can't stay live).
  `juce::CallOutBox::launchAsynchronously` (first use in the repo) hosts the popup — non-modal, self-owning.
- **A heterogeneous braced-init-list won't deduce a common base pointer (W06).** `for (juce::Component* c :
  { &folderIconBtn, &textBtnA, ... })` fails when the elements are different subclasses (`FolderIconButton*`
  vs `TextButton*`) — MSVC can't deduce the `initializer_list` element type. Call `addAndMakeVisible` per
  element instead of looping over a braced list of mixed pointer types.
- **`te::TimePosition` is a strong type (W06).** It is NOT implicitly constructible from the literal `0` —
  use `te::TimePosition()` (== position 0). Bit the tap-tempo gate's engine-write leg.
- **`juce::String`'s `char*` ctor is ASCII-only (W05).** A non-ASCII byte (em-dash) renders mojibake +
  jasserts under a debugger. Use plain ASCII in `char*` literals or `String::fromUTF8` with escaped bytes.
- **A `LevelMeasurer` can die under a UI meter (W03 — was a latent product UAF).** The master measurer lives
  ON `EditPlaybackContext` (freed by `freePlaybackContext` / device restarts); track/return measurers live on
  a `LevelMeterPlugin` the engine's plugin cull can reclaim after a track delete. Holding a raw
  `LevelMeasurer*` and calling `removeClient` later walks freed memory — the first `--selftest-sync` run spun
  forever in exactly that, in `~MixerView`. **Rule: hold measurer sources as
  `juce::WeakReference<te::LevelMeasurer>`** (the engine declares it weak-referenceable) and skip
  `removeClient` when the weak ref is null. And per the R1 rule, re-resolve cached engine object pointers
  through a seam before dereferencing in any poll (`ReturnStrip::pollMeter` was a deterministic 28 Hz UAF
  after deleting an aux-return track — its Arrange lane has a full header menu including Delete).
- **Engine device IDs are NOT juce identifiers (W03).** `MidiOutputDevice::getDeviceID()` returns an
  engine-minted ID (e.g. `out_81b0d7ef`), never equal to `juce::MidiDeviceInfo::identifier` — match devices by
  NAME. And `MidiOutputDevice` props (`enabled`, `sendMidiClock`) are **persisted keyed by device NAME**, so
  any same-named device object (e.g. a selftest probe) that calls `saveProps` (which `setSendingClock`,
  `closeDevice`, and the dtor all do) rewrites the REAL device's stored state — snapshot + restore around it.
- **A UI seam a gate can't see can ship unwired.** The W03 Clock toggle passed `--selftest-sync` while wired
  to nothing, because the gate drives the engine seam directly. Adversarial QC caught it; when a feature has
  both an engine seam and a UI affordance, verify the SHELL wiring explicitly at consolidation.
- **Single-point automation curves follow direct value sets (engine-intended, W03).** With exactly one point
  on a curve, `setParameterValue` (a mixer fader gesture) MOVES that point — one point and a static value are
  the same statement. Accepted semantics; the automation lane listens for `curveHasChanged` so it's visible.
- **`JUCE_DECLARE_NON_COPYABLE` suppresses the implicit default ctor (W04a).** The macro declares a deleted
  COPY constructor — which is still a user-declared constructor, so the implicit default one vanishes. Any
  class using the macro needs an explicit `ClassName() = default;` (every older Forge class already declares
  a ctor, which is why this never bit before; a build-less agent shipped it, the compiler caught it).
- **Count-in clicks land on WHOLE TIMELINE BEATS; the punch point is NOT beat-snapped (W04a).** Recording
  from a mid-beat stop is the common case, so any count-in UI must derive its display from the CLICK GRID
  (`firstClick = ceil(punchBeat − N)`), never from whole-beat distances to the punch — the distance form
  leads the audible click by up to a full beat. Also: the count-in latch must only arm on a record
  rising-edge from a STOPPED transport, and `LcdDisplay::setEdit` deliberately early-returns on a SAME-edit
  call — the shell uses `controlBar.setEdit` as a generic toggle resync, and an unconditional reseed wiped
  the in-flight latch (and left demo faces frozen into snapshots).
- **The menu bar is window chrome, not shell content (W04a).** `DocumentWindow::setMenuBar` hosts the bar
  ABOVE the content component, so `createComponentSnapshot (getLocalBounds())` on the shell never captures
  it. Its pixels come from the window-level `shell_window` capture (W04b); the OS-native title bar is peer
  chrome outside the component tree — expected absent from every snapshot.
- **A reparented-home view arrives VISIBLE at its old window bounds, topmost (W04b — was the QC blocker).**
  `ResizableWindow::setContent*` made it visible; `addChildComponent` preserves the flag and inserts on
  top. Any tear-off restore must (a) hide the view on reparent, and (b) re-run the shell layout — AND any
  dependent focus grab — on the SAME deferred turn that clears the popout pointer, because the guards in
  `resized()` skip a view whose popout pointer is still live. A gate for this class must assert the
  no-ghost state BEFORE performing any state-driving of its own (the original gate rescued the bug away).
- **Slide/visible-flag desync (W04b).** Any programmatic `drawerVisible = true` must go through
  `revealDrawer()` (it retargets an in-flight close and keeps the slide target in sync) — a direct write
  made the next E toggle bounce the drawer instead of closing it, and a close-in-flight settle step could
  flip the flag back off underneath the open.
- **What is and isn't on the undo stack (W05).** Track **mute/solo are NOT undoable** — the engine binds
  those `CachedValue`s with a null UndoManager (its choice, not Forge's; do not "fix" by re-binding).
  **Record-arm targets ARE on the stack** (`setTarget`/`removeTarget` write through the UM) — which is why
  the shell BLOCKS undo while recording (an undo mid-take would silently retarget the capture), and why an
  undo while merely armed can disarm a track. Undo history is per-Edit and does NOT survive a project swap.
  `ensureScenes` is deliberately off the stack. And undo/redo fires no `onEditMutated` — any new surface
  must be added to the explicit refresh fan-out in `undoOrRedo`, or it shows stale state.
- **`juce::String`'s `char*` ctor is ASCII-only (W05 — the recovered QC finding).** A raw em-dash (or any
  non-ASCII byte) in a `const char*` literal renders mojibake and jasserts every paint under a debugger.
  Use plain ASCII in string literals, or `String::fromUTF8` with escaped bytes (the ClipSlotComponent
  precedent) when the glyph is worth it. MSVC compiles this repo without `/utf-8`, so the bytes reach the
  ctor raw.
- **A component's hover state excludes its children by default (W05).** `isMouseOverOrDragging()` goes
  false while the pointer is over a CHILD (e.g. a row's own launch button), and
  `setRepaintsOnMouseActivity` only fires for the component's OWN enter/exit — a hover fill flickers off
  exactly over the primary click target. Use `isMouseOverOrDragging (true)` + route the child's mouse
  events through the parent (`addMouseListener`) with explicit enter/exit repaints.
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
- **Nested block comments corrupt doc comments (bit a THIRD time this project).** A `/* … */` block comment
  placed **inside** a `/** … */` doc comment closes the doc comment early — the first `*/` ends it, and everything
  after (up to the real close) leaks into the code stream and corrupts the following declarations. Bit
  `RecordController.h` (W7), `MarkerBar.h` (Wave 01), and now `ControlSurfaceHost.h` (W02 — `ClipSlot*/Clip*` in
  the header comment, whose `*/` closed the block early; the integration build caught it, a build-less agent
  could not). Fixed by not nesting `/* */` inside `/** */`. Use `//` for inline notes inside a doc comment.
- **`VirtualMidiInputDevice` has no `controllerParser` (W02).** Only `PhysicalMidiInputDevice` carries the
  `controllerParser` that routes an incoming CC → `ParameterControlMappings`. A virtual device can't exercise
  that path — so the focused-Edit `ForgeUIBehaviour` HW-routing (item 2a) is proven headlessly only up to "the
  focused Edit is reported + a CC→param bind lands via the seam"; a **real hardware CC actually driving a param
  is a real-hardware smoke item**, not a headless one.
- **No non-mutating post-fader master sample tap without forking the engine (W02).** The read-only
  `tracktion_engine` submodule exposes no way to observe the master output as *samples*: `LevelMeasurer` gives
  only reduced dB, the master-tap node is internal, and JUCE's `AudioDeviceManager` **sums** secondary audio
  callbacks rather than letting one observe the engine's output. So there's **no live master LUFS** — integrated
  loudness is a whole-program measurement done on the export render (offline), which is the correct tool, not a
  workaround.
- **MIDI slot record is transport-driven, NOT launch-driven.** Recording is started by
  `transport.record(false)` over `isRecordingActive()` destinations — **never** `launchSlot` on the record path
  (an empty armed slot has no clip and no `LaunchHandle`, so a launch is a hard no-op). `isSlotRecording` is
  `isSlotMidiArmed(slot) && transport.isRecording()`, NOT "LaunchHandle playing".
- **Slot capture must be slot-ONLY.** Arm the slot's `itemID` (`setTarget(slot.itemID, /*moveToTrack=*/false,
  …)`) and **disarm the track's MIDI target first** — if both a track and a slot are armed, notes
  double-capture into the arrangement as well as the slot. (Audio/MIDI arm are also mutually exclusive per
  track in v1.)
- **Injecting synthetic MIDI headlessly:** after `engine.getDeviceManager().createVirtualMidiDevice(name)`
  (async — **yield first**, then find the device by name via `getMidiInDevices()`), inject notes with
  `dev->handleIncomingMidiMessage(msg, dev->getMPESourceID())` (the **public** override — NOT the protected
  `handleNoteOn/handleNoteOff` keyboard-listener overrides). On teardown call
  `deleteVirtualMidiDevice(*dev)` or the device name leaks (persisted in engine PropertyStorage) and the next
  run's `createVirtualMidiDevice` fails with "Name already in use".
- **SessionView threading (load-bearing):** pads cache NO `te::ClipSlot*`/`Clip*` — only `(track,scene)`
  indices; the 25 Hz poll re-resolves via the **const** `getClipSlot` (never inserts). Any track-list change
  must rebuild the grid before a stale `TrackColumnComponent` derefs its `AudioTrack&` (the QC blocker).
- **Scrolled-viewport relayout (the session-scroll session's bug):** for a `juce::Viewport`, the **viewed component's
  top-left IS the scroll offset** — so in `SessionView::resized()` size the scrolled `columnHolder` with
  `setSize(w, h)`, **never** `setBounds(0, 0, w, h)` (the latter yanks the grid back to the top on any relayout
  while scrolled). The pinned scene column is offset by `-getViewPositionY()` in `syncSceneColumnToScroll()`.
- **CriticalSection nested-lock type:** the logging file sink guards with a `juce::CriticalSection`; to take it
  use **`juce::CriticalSection::ScopedLockType`** (i.e. `ScopedLock`), NOT a bare `juce::ScopedLock` templated
  wrongly — get the member type from the lock. (Never log from the audio/RT thread regardless — see LOGGING.md.)
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake. (And a quoted
  `"C:\Program Files\..."` path in the same command as `Remove-Item` can trip the sandbox guard — split them.)
- **Latest PUSHED work is on `origin/main` at tip `96b1037` (W16); local `main` == `origin/main`.**
  Pushed: W07–W11, the docs sanitization, W12 per-clip launch quantise, W13 grid clip primitives, W14 MIDI
  quantise, W15 scene lifecycle (`9cc7f04`), and W16 W05 QC-debt discharge (`96b1037`). ⚠ **History was rewritten
  + force-pushed in a prior session** to scrub
  a real-identity leak from an earlier HANDOFF commit's prose (`git-filter-repo` → `python -m git_filter_repo`,
  which drops `origin` — re-added before pushing); the old `09c4928` became `6ca11cd`, and hashes below it are
  unchanged. The sanitize scan (the real-identity denylist — see CLAUDE.md §Public-repo hygiene; a 3-lens
  audit before both the W15 and W16 pushes) ran clean. Prior pushed history: W08 (`0ad7abc`), W07
  (`fc0fdbe`), W06 (`e670ab5` / `aa45ad7`),
  W05 (`5e5dcf2`), doc audit (`7f03974`), W04b (`cc27300`), W04a (`41e3139`), W03 (`ffa494d`), W02 (`bb9ef5e`),
  Wave 01 (`e3b8c7c`). The working tree is otherwise **clean** (the local `Waveform User Guide.pdf` is
  `.gitignore`d — copyrighted, never committed).
  Sanitizing is a live discipline, not
  a formality: a prior wave's CLI caught + scrubbed a stray sibling-project name in a comment before its commit —
  it would have been the first private-project-name leak into the public repo. **Public repo = sanitize before
  every push** (pseudonymous TxVibeCoder — keep the real email / personal `C:\Users\…` paths / prior-project names
  out). `.gitignore` excludes `*.log` / `forge.log*` (the log sink never gets committed) and
  `wave-*-cli-prompts/` (the wave packets embed machine-local paths → stay local). History was rewritten once (a
  prior session) to scrub a stray path; `git-filter-repo` isn't on PATH, so run **`python -m git_filter_repo`** if
  ever needed (it drops the `origin` remote — re-add before pushing). Submodules are clean.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. An earlier session's **Session grid** landed a **clean first-try integration build** this way (2,920
  LOC, 18 files), because every load-bearing engine API was **source-verified before** the fan-out. **W7 this
  session** followed the same pattern — a frozen source-verified design
  ([devlog/midi-record-design.md](devlog/midi-record-design.md)) → file-disjoint build waves (record layer /
  session seam / pad state / session view) → orchestrator integration + the single build.
- **Adversarial verify waves** (independent skeptics, default-refuted, evidence-required) — high ROI for
  anything that can't be runtime-confirmed here. Earlier sessions ran them on the SessionView **design**, the
  **QC** (12 confirmed, 3 refuted), and a **fix re-verify**. **W03's QC** (18 agents, 4 dimensions ×
  per-finding skeptics) confirmed 9 real findings including a deterministic UAF and an unwired UI seam the
  gates could not see, and one pre-build skeptic refuted + corrected the sync recipe's device handling before
  any code existed. They earn their keep.
- **Model tiering (established W03):** match sub-agent models/effort to task complexity — top tier on UI work
  (Fable holds design authority) and on verification/QC (where wrongness is expensive), mid tier on
  recipe-driven C++ against frozen contracts, small models on docs drafting and mechanical sweeps.
- **Log fallible seams as you build them — STANDING BUILD PRINCIPLE (established in the logging session).** Every new feature
  routes its failure paths through `src/core/Log.*` (`FORGE_LOG_ERROR/WARN/INFO/DEBUG`) — **never** on the
  audio/RT thread, **never** per-tick in a poll/paint, autosave only on `save() == false`. The principle +
  cheat-sheet + a new-feature checklist live in [LOGGING.md](LOGGING.md); read it before adding a feature.
- **Multi-CLI parallel waves — the scaled build pattern (established this session).** For a file-disjoint feature
  set, the orchestrator writes per-CLI **handoff packets** in `wave-<N>-cli-prompts/` (gitignored — they embed
  local paths): a `README` control doc + one self-contained `P#-<slug>.md` per CLI (its territory, "take-as-given"
  facts, *propose-don't-edit* for shared files, scoped commits, and **CLIs do NOT build**) + a `P#-consolidation`
  packet for the orchestrator role (owns `main.cpp`/`CMakeLists`/`ProjectSession`; runs the single build + the
  full selftest floor + adversarial QC + docs + sanitize + push). Full rule: **`CLAUDE.md` → Wave Orchestration
  Rule**. **Wave 01 shipped six features this way** (~35 min of parallel build vs. ~2 h serial); the CLAUDE.md
  nested-comment gotcha caught its own predicted bug at consolidation, and QC caught two integration UAFs no
  single CLI could see. No `.wave-active` sentinel (Forge has no auto-sync) — the load-bearing guard is **scoped
  commits** (`git add -- <paths>` **and** `git commit -- <paths>`, never `-A`).

---

## Open decisions (waiting on you)

- **Session grid layout — RESOLVED + BUILT (vertical scroll).** No longer open: vertical scroll with fixed
  ~46 px pads shipped (a prior session, `8d15234` + pushed); see
  [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Double-click edit gesture** — currently double-click opens a clip AND launches it (first press launches);
  right-click "Edit clip" is the launch-free path. Kept as belt-and-suspenders this session; revisit if the
  double-launch bothers you.
- **The control-bar "Editor" button** — third view, drawer toggle, or drop it? Now a **Fable design-authority
  call** slated for the W04 menu-bar/layout work (preserved as open in INTERFACE.md).
- **`INTERFACE.md` body — RESOLVED (W03):** rewritten Session-first with the design charter + the W04 UX
  charter; the superseded-banner is gone.
- **Ableton Link — vendoring decision.** The engine wrapper is ready but compiled out, and the Link library
  is NOT in the repo. Enabling = vendor `github.com/Ableton/link` (+ asio-standalone) + an AGPLv3
  license-compatibility review + two CMake lines. Default: deferred until the maintainer approves the new
  dependency.
- **Mockup refinements** — likely incoming (Session footer mixer, hard renumber 00→01, geometry tweaks).
- **Controllers — maintainer has NONE (stated 2026-07-01).** Hardware smoke items are parked, not queued; a
  controller in hand would unblock the Launchpad byte-mapping test and the physical-CC MIDI-learn path.

---

## Key references

- **[../CLAUDE.md](../CLAUDE.md)** — the working agreement: tenets, engineering principles, build discipline, the
  Forge gotchas, and the multi-CLI **Wave Orchestration Rule** (auto-loaded by Claude Code; linked here for humans).
- **[DIRECTION.md](DIRECTION.md)** — the authoritative product brief (read first).
- [STATUS.md](STATUS.md) — living roadmap. · [../mockups/](../mockups/) — the UI mockup set (sheet 00 = the target).
- [INTERFACE.md](INTERFACE.md) — the Session-first UI plan + design charter + the W04 UX charter (rewritten in W03).
- [devlog/wave-18-scene-send-loop.md](devlog/wave-18-scene-send-loop.md) — **W18: frontier Wave 7 fast-follows — whole-scene-send + send-as-loop (this session, completes Wave 7)**.
- [devlog/wave-17-performance-capture.md](devlog/wave-17-performance-capture.md) — **W17: frontier Wave 7 capture core — performance recording**.
- [devlog/wave-06-handson.md](devlog/wave-06-handson.md) — **W06: hands-on wave 1 — control bar & HUD**.
- [devlog/wave-05-undo.md](devlog/wave-05-undo.md) — **W05: global Undo/Redo + the polish sweep (QC partially owed)**.
- [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md) / [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md) — the W04 UX waves (same session).
- [devlog/wave-03-features.md](devlog/wave-03-features.md) — W03: automation · MIDI-clock out · async LUFS · live refresh (same session).
- [devlog/wave-02-features.md](devlog/wave-02-features.md) — W02: MIDI-learn HW routing · control surface · offline LUFS (prior session).
- [devlog/wave-01-features.md](devlog/wave-01-features.md) — Wave 01: the six parallel feature seams (prior session).
- [devlog/midi-record-design.md](devlog/midi-record-design.md) — the W7 MIDI-slot-record design + the frozen recipe.
- [devlog/session-design.md](devlog/session-design.md) — the Session-grid design + build-wave record (earlier session).
- [devlog/session-scroll-design.md](devlog/session-scroll-design.md) — the vertical-scroll design (prior session).
- [LOGGING.md](LOGGING.md) + [devlog/logging-design.md](devlog/logging-design.md) — the logging principle + subsystem design (prior session).
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the original W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
