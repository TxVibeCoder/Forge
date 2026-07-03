# W11 — Frontier program Wave 1: launcher expressiveness (follow-actions · loop-toggle · launch-modes)

*The first wave of the **frontier build program** (the 10-wave plan a discovery swarm produced after W10 — see
[[forge-frontier-program]] / `docs/frontier-program.local.md`). Baseline `90449ce` (W10 tip). This is the
maintainer-flagged **#1 gap** and DIRECTION.md's identity core: turn the Session grid from a bank of one-shots
into a self-driving, performable instrument. Built to a **frozen, source-verified spec** (the wave-1 freeze in
the frontier program), by the orchestrator (serial spine) + one file-disjoint agent (Role B: ClipSlotComponent)
+ a 5-dimension adversarial QC swarm.*

---

## What shipped

Right-click any filled Session slot → three new submenus:
- **Follow action** — what the launcher does after the clip plays its follow-action duration: `None · Stop ·
  Play again · Next clip · Previous clip · First clip · Last clip · Round robin` (the deterministic v1
  vocabulary; `Random` is present-but-disabled, deferred to v2). Picking one sets a default duration of
  **after 1 loop**.
- **Loop** — a checkable toggle: looping clip vs one-shot (the "after N loops" precondition).
- **Launch mode** — `Trigger` (today's one-shot launch) · `Gate` (plays while the pad is held, stops on
  release) · `Toggle` (each click toggles launch/stop).

Follow actions are **consumed by the engine at graph-build** (`EditNodeBuilder` passes `createFollowAction(clip)`
into the `SlotControlNode`), so Forge does **zero per-tick work** — R1-safe.

## The engine footgun (the wave's highest-risk point, defeated)

Writing a clip's `followActionBeats`/`followActionNumLoops` > 0 on an **empty** action list makes the engine
**auto-plant a `currentGroupRoundRobin` action** (`tracktion_Clip.cpp:524-545` → `FollowActions.cpp:498`). The
seams defeat it two ways (both proven by gate legs): `setFollowAction` always sets the action type
**explicitly** after ensuring exactly one action exists; `setFollowActionDuration` **pre-creates** an action
before writing the duration. QC confirmed both seams are independently self-defending across all call orderings.

## Seams (ProjectSession)

`setFollowAction` · `getFollowAction` (const, guards the FOLLOWACTIONS-child-exists check so a pure read never
lazily grows the tree) · `setFollowActionDuration` · `setSlotClipLooping` (a real `[0, getLengthInBeats()]`
range — **never** `setLoopRangeBeats({})`, the W5/W10 gotcha) · `isSlotClipLooping` · `setLaunchMode` /
`getLaunchMode` (an int `"forgeLaunchMode"` property on `clip->state`; absence reads Trigger==0, so every
pre-W11 clip + gate is unchanged) · `isSlotActive` (playing **or** queued-to-play — see QC).

## UI wiring

- **ClipSlotComponent** (Role B, file-disjoint): a new `onReleased` callback + `mouseUp` override (fires on a
  left non-popup release; `mouseDown`/`onClicked` byte-identical) — the release Gate needs.
- **TrackColumnComponent**: forwards the pad's `onReleased` → `onSlotReleased`.
- **SessionView**: the three submenus in `handleSlotRightClicked`; a shared **`launchOrToggle`** helper used by
  both the mouse click and keyboard Enter (Toggle stops an active clip, else launches; Trigger/Gate launch);
  `handleSlotReleased` (Gate: stop on mouse-up). **Trigger stays the byte-identical proven launch path**
  (`getLaunchMode` short-circuits before `isSlotActive`).

## Gates (floor 24 → 26)

- **`--selftest-followaction`** (11 legs): both clips created · set/read-back · **exactly one action** (footgun:
  no stray auto-plant) · duration persists **and the action survived** (footgun re-check) · **KEY PROOF** — a
  non-null `createFollowAction` functor for `trackNext` (with a filled sibling) + an empty functor for `none` ·
  ValueTree round-trip (serialization) · undo revert · loop toggle on/off + never-empty-loop-range.
- **`--selftest-launchmode`** (7 legs): default is Trigger · Toggle/Gate set-read-back · ValueTree persistence ·
  **absence reads Trigger** · the `isSlotActive` query is sound.

Both collision-free in the ladder (before bare `--selftest`); floor is **24 named + bare `--selftest` = 24**,
Wave 1 takes it to **26** (reconciling the earlier 23-vs-24 wording — CLAUDE.md now pins 24 as of W10).

## Adversarial QC — 5 dimensions

- **Follow-action footgun + correctness** — REFUTED clean (both seams self-defending; single-action invariant;
  const read non-mutating; all v1 actions resolve or degrade to a guarded empty functor; undo atomic).
- **Lifetime / R1 / threading** — REFUTED clean (static menu table safe in the async lambda; pad caches no
  engine pointer; stale slot after a project swap → logged no-op, no UAF).
- **Loop-toggle + W10 interaction** — REFUTED clean (no empty-range gotcha; a looping slot clip still sends to
  Arrangement as a correct one-shot).
- **Launch-mode routing + hot-path** — **Trigger byte-identical (REFUTED)**; CONFIRMED (all fixed/documented):
  (a) **Toggle queued-race** — a click during the launch-quantise pre-roll saw `isSlotPlaying==false` (queued ≠
  playing) and re-launched instead of stopping → **FIXED** by `isSlotActive` = playing **or** queued; (b) **Enter
  bypassed mode routing** → **FIXED** (Enter now routes through `launchOrToggle`, so Toggle toggles off from the
  keyboard); (c) **Gate quick-click / double-click under quantised launch** — a click releases before the
  quantise bar, so the still-queued clip is stopped (silent under bar-quant) → **DOCUMENTED** as a v1 limitation
  (Gate responds immediately under free-trigger launch quant; an immediate-launch path for Gate is a follow-up).
- **Persistence / undo / interactions** — REFUTED (disk persistence is guaranteed by the engine's whole-tree XML
  serializer; absence==Trigger holds; undo of setLaunchMode reverts cleanly incl. restoring absence; delete+undo
  restores the state). One CONFIRMED-but-inert item **FIXED**: the W10 send-to-arrange copy carried the
  launcher-only FOLLOWACTIONS child + `forgeLaunchMode` onto the (never-launched) Arrange clip — harmless dead
  data (the engine's node-builder reads them only on the launcher path), now **stripped** in the send
  normalization for hygiene + future-proofing.

## Verify

- Clean MSVC Debug build (0 warnings). **All TWENTY-SIX selftests PASS** (24 prior + the 2 new); every `mode=`
  line matches its flag; `--selftest-session` confirms Trigger launch is byte-identical.

## Gotcha banked (added to CLAUDE.md)

- **The FOLLOWACTIONS auto-plant footgun** — writing a follow-action duration (`followActionBeats`/`NumLoops` > 0)
  on an **empty** action list auto-adds a `currentGroupRoundRobin` action (`Clip.cpp:524-545` /
  `FollowActions.cpp:498`). Always set the action type explicitly after ensuring an action exists.

## Follow-ups (documented, not built)

- **Immediate launch for Gate** (bypass launch-quant so a click-hold is instant under any quant setting) — the
  clean fix for the Gate quick-click limitation; touches the launch/stop helpers.
- **A disk save→reload leg** for `--selftest-launchmode` (the gate proves the property is on the tree via an
  in-memory copy; disk persistence is guaranteed-by-construction but not yet round-tripped in the gate).
- **Random follow-actions** (`trackAny`/`trackOther`) + weighted multi-action lists + group actions (v2).
- **Audible-chain / audible-gate proof** — needs a pumped playback loop the harness doesn't run (W09/W10
  render-leg convention); the behavioral routing (Gate hold, Toggle timing) is manual-verify per the frozen spec.

*Next in the frontier program: Wave 2 (per-clip launch quantise — extends `--selftest-session`). Program +
critic corrections in `docs/frontier-program.local.md`.*
