# W12 — frontier Wave 2: per-clip launch quantise override

> Frontier build program, Wave 2 (the second wave after the hands-on plan completed at W10). Baseline
> **`2a366e9`**; code **`03f6efd`**. Small, low-risk, headless-provable — **extends `--selftest-session`,
> adds NO new gate** (floor stays **26**). Built orchestrator-serial (single spine: `ProjectSession` +
> `SessionView` menu + `main.cpp`) after a 5-reader source-verification swarm froze the spec, then a
> 6-dimension adversarial QC returned **ship / zero defects**.

## What shipped

A filled Session slot can now carry its **own launch quantisation** — a 1/16 hat fill can snap on the
sixteenth while a 1-bar bass snaps on the bar — instead of every slot obeying only the Edit-global launch
quant. Right-click a filled slot → **Launch quantise** → *Global (inherit — &lt;name&gt;)* or any of the 23
`LaunchQType` values. The tick shows whether the clip inherits or overrides.

**The launch path already honoured a per-clip override.** The file-local resolver
`getLaunchQuantisation(te::Clip&)` (`ProjectSession.cpp:318-324`) already returns the clip's own
`LaunchQuantisation` when `! usesGlobalLaunchQuatisation()`, else the Edit-global. So this wave only added the
seams to *write/read* that state, the submenu, and a headless proof — no engine change, and the candidate's
flagged dependency was already satisfied (the discovery swarm's S/low re-scoping was correct).

## Seams (ProjectSession)

- `setClipLaunchQuantisation (track, scene, LaunchQType)` — activates the override:
  `setUsesGlobalLaunchQuatisation(false)` **then** writes `lq->type`. Both are required (the resolver gates on
  the flag first, so a type-only write is a silent no-op).
- `getClipLaunchQuantisation (track, scene) const` — the clip's own type while overriding, else the global.
- `clearClipLaunchQuantisation (track, scene)` — reverts to global (`setUsesGlobalLaunchQuatisation(true)`);
  leaves the stored clip type intact for a later re-enable.
- `clipInheritsGlobalLaunchQuantisation (track, scene) const` — the has-override test (drives the menu tick).
- `resolveEffectiveLaunchQType (track, scene) const` — **the proof bridge.** Delegates straight into the
  file-local resolver the live launch feeds, so `--selftest-session` asserts precedence through the *real*
  code path, not a re-derived mirror.

## The engine gotcha (new, load-bearing)

The engine method is spelled **`usesGlobalLaunchQuatisation`** — a verbatim engine typo (missing the *n*) —
and is **inverted**: `setUsesGlobalLaunchQuatisation(false)` **ENABLES** the per-clip override; `true`
inherits the global. Auto-correcting the spelling to "Quantisation" fails to compile / fails to override. And
unlike the FOLLOWACTIONS lazy-create footgun, `Clip::getLaunchQuantisation()` builds its `LaunchQuantisation`
over the clip's **existing** state node (not `getOrCreateChildWithName`) and its ctor only `referTo`s (a
non-writing read), so the const readers never dirty the edit — the const inherit-check reads only the flag and
never constructs the member.

## Proof

`--selftest-session` gains a `perClipLaunchQ` leg: from the fixture clip in slot (0,0) (global == `bar`), set
a `none` override → assert `getClipLaunchQuantisation == none`, the inherit flag flips, and
**`resolveEffectiveLaunchQType == none` while the global still reads `bar`** → clear → assert it inherits again
and the resolver falls back to `bar`. Proving through `resolveEffectiveLaunchQType` (the real resolver) is the
load-bearing choice: a get-seam-only assert could pass while the launch path diverged (the "assert parity no
build step enforces" anti-pattern). No new gate name → no ladder hazard; floor stays **26**. All 26 gates
PASS; clean MSVC Debug build (0 warnings).

## Adversarial QC (6 dimensions — ship / 0 defects)

Six independent skeptics (default-refute, evidence-required), all REFUTED with source citations:

- **Inverted-flag polarity** — correct; `useClipLaunchQuantisation = !useGlobal` across Midi/Audio/Step clips.
- **R1 const-read purity** — no const reader dirties the tree; `getLaunchQuantisation()` uses the existing
  state node, ctor `referTo` is non-writing (contrast the FOLLOWACTIONS `getOrCreateChildWithName` footgun).
- **Undo integrity** — both CachedValues bind `&edit.getUndoManager()`; the two writes seal as one atomic
  Ctrl+Z step via `onEditMutated`→`beginNewTransaction`; unchanged-value writes are skipped → no phantom step.
  **Fully undoable** (a stylistic-only difference from W11's explicit-UM `setProperty`).
- **Menu correctness** — ids 400 / 401–423 disjoint; dispatch bound (`< 401 + choices.size()`) covers exactly;
  `static_cast<LaunchQType>(i) == choices[i]` is 1:1 in enum order; tick distinguishes override-equal-to-global
  from inherit; filled-only; ASCII label (no mojibake).
- **Persistence** — writes land on the persisted clip state node; `toBarFraction`/`fromBarFraction` bijective
  across all 23 values; MidiClip + WaveAudioClip (the only slot clip types) are both override-capable.
- **Regression** — a never-overridden clip resolves byte-identically to pre-W12 (flag absent → global);
  `createMidiClipInSlot` writes no launch-Q state.

## How it was built

Two workflows bracketed a serial main-loop implementation: (1) a **5-reader source-verification swarm** over
`libs/tracktion_engine` + `src/` → a frozen buildable spec (which caught the typo, the inverted semantics, and
the resolver-bridge requirement before any code was written); (2) the orchestrator implemented the single
spine + owned the build + the 26-gate floor; (3) a **6-dimension adversarial-QC swarm** returned
ship / 0-defects. No `ClipSlotComponent` / `SessionView.h` change; the menu dispatches the seam inline like the
W11 launcher submenus.

## Follow-ups (documented, not built)

- A **save → reload ValueTree round-trip** leg for `--selftest-session` (QC proved persistence by source
  reasoning + the bijective-fraction check; a disk round-trip would gate it directly).
- The submenu lists the **full 23 `LaunchQType` values** (engine parity with the global TransportBar combo);
  a curated subset is a one-line UI change (a Fable call) with no seam impact.
