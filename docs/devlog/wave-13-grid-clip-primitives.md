# W13 — frontier Wave 3: grid clip primitives (duplicate → move / copy)

> Frontier build program, Wave 3. Baseline **`c32b8f1`**; code **`2f804a2`**. Adds two gates
> (`--selftest-duplicate`, `--selftest-slotmove`) → floor **26 → 28**. Built orchestrator-serial (single
> spine: `ProjectSession` + `SessionView` menu/keyboard + `main.cpp`) after a 6-reader source-verification
> swarm froze the spec; a 6-dimension adversarial QC returned **fix-then-ship** — one MAJOR defect caught +
> fixed, five dimensions refuted clean.

## What shipped

Slot→slot clip movement on the Session grid:
- **Right-click a filled slot** → **Duplicate clip** (copy to the first empty slot below) or **Move to next
  slot** (move to the first empty slot below). Both auto-grow a new scene row when nothing is empty below.
- **Keyboard** on the focused slot: **Ctrl+D** = move, **Ctrl+Shift+D** = copy; focus follows the clip to its
  (possibly grown) row.
- The seams are cross-track capable (`copySlotClip` / `moveSlotClip` take an explicit destination and are
  exercised cross-track by the gate); the UI surfaces same-track-below only for now.

## Seams (ProjectSession) — one clone point

Three public seams compose a single file-local `cloneClipIntoSlot` helper (the ONLY engine-insert in the path):
- `copySlotClip (srcT, srcS, dstT, dstS)` — the core: guards, materialise the destination on demand
  (`getOrInsertAudioTrackAt` + `ensureScenes` + `ensureNumberOfSlots`), re-resolve BOTH slots after the grow
  (R1), clone. A filled destination is replaced.
- `duplicateSlotClip (track, srcScene)` — scans for the first empty slot below (else grows a new last row),
  delegates to `copySlotClip`, returns the destination scene index (or -1).
- `moveSlotClip (…)` = `copySlotClip` **then** `clearSlot(src)`, with NO `beginNewTransaction` between, so one
  Ctrl+Z reverses the whole move.

`cloneClipIntoSlot` = `srcClip.state.createCopy()` (parentless — satisfies the engine assert) →
`e.createNewItemID().writeID(...)` (the single-arg `insertClipWithState` does **not** re-ID) → stop any live
launch on a filled target → `te::insertClipWithState(targetSlot, newState)`. Replace-on-filled and slot
normalization are engine-automatic; the launcher metadata (follow-action / launch-mode / launch-Q) rides the
deep copy, so a duplicate is a faithful launcher clone — **not** the Arrange one-shot normalization of W10.

## The QC-caught defect (MAJOR) — one-shot re-looping

The verify swarm concluded "the engine re-imposes slot normalization, so no fix-up needed." The adversarial QC
found the hole: the engine's ClipSlot normalization (`tracktion_ClipOwner.cpp:372-381`) **re-imposes a
full-length loop on any freshly-inserted clip that reads `!isLooping()`** — so duplicating a **one-shot** clip
silently brought it back **looping**, regressing the W11 one-shot launcher state. The 28 gates missed it
because `createMidiClipInSlot` clips are born *looping*, so the fixture never exercised the `!isLooping()`
branch. **Fix:** `cloneClipIntoSlot` captures `wasOneShot = !srcClip.isLooping()` before the insert and
re-asserts `clip->disableLooping()` after — the correct inverse (never `setLoopRangeBeats({})`, which
re-triggers the W5/W10 auto-tempo gotcha). A new gate leg sets the source one-shot and asserts the duplicate
stays one-shot (`sourceOneShot=1`, `oneShotPreserved=1`).

## Proof — two new gates (floor 26 → 28)

- **`--selftest-duplicate`**: seed a 4-note clip → duplicate into the empty slot below (source stays filled,
  4 notes ride the clone) → **one-shot preservation** → undo removes just the duplicate → the grow branch
  (fill the column, duplicate grows a new row: `growDst=16`, scenes 16→17) → empty-source no-op.
- **`--selftest-slotmove`**: copy keeps the source, move empties it, replace-on-filled keeps one clip,
  **MOVE atomic undo** (one Ctrl+Z restores the source AND empties the dest — the load-bearing atomicity
  proof), empty-source no-op. Cross-track, note-count faithful.

Both gate names are substring-collision-free (`slotmove` vs `slotdelete` share only the `slot` prefix). Every
`mode=` line verified. All 28 gates PASS; clean 0-warning Debug build.

## Adversarial QC (6 dimensions — fix-then-ship)

- **D4 normalization** — the one-shot re-loop bug (above). **Fixed + gate-guarded.**
- **D1 clone fidelity / itemID** — REFUTED: top-level re-ID is sufficient (no id-bearing children reachable on
  slot clips); metadata rides the copy; runtime LaunchHandle state isn't in the ValueTree.
- **D2 undo** — REFUTED: the `ensureScenes` history-wipe-on-grow **predates W3** (matches
  `createMidiClipInSlot`); the real-UI MOVE is atomic (one synchronous callback, one trailing `onEditMutated`).
- **D3 R1 / lifetime** — REFUTED: append-only grows never reallocate existing objects; both slots re-resolved
  after the grow; the filled-target replace stops the old launch handle first.
- **D5 menu / keyboard** — REFUTED: ids 50/51 in the free 9..99 gap; `Ctrl+D` has no accelerator collision;
  `getKeyCode()` correct. (One cosmetic note: a *menu* Move leaves focus on the emptied source, vs the
  *keyboard* Move which follows the clip — not a correctness bug.)
- **D6 gate integrity** — REFUTED (the coverage gap it flagged — one-shot — is now closed by the new leg).

## How it was built

Two workflows bracketed a serial main-loop implementation: (1) a **6-reader source-verification swarm** froze
the spec (the clone idiom, the auto-grow pair, the engine insert, the menu/keyboard territories, the ladder);
(2) the orchestrator implemented the single spine + owned the build + the 28-gate floor; (3) a **6-dimension
adversarial-QC swarm** caught the one-shot regression the verify swarm missed — the pattern earning its keep.
The keyboard path collapsed into `SessionView::keyPressed` (the pad has no key handler); `ClipSlotComponent`
was untouched.

## Follow-ups (documented, not built)

- Cross-track Duplicate/Move in the **UI** (the seams already support it; the menu/keyboard are
  same-track-below).
- A non-history-clearing `ensureScenes` variant so an auto-grow duplicate doesn't wipe the prior undo stack
  (inherited from `createMidiClipInSlot`; bounded to the grow edge case).
- Menu-Move focus-follow parity with keyboard-Move (cosmetic).
- Mouse DRAG (parked — no headless driver).
