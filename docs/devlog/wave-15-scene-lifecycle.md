# W15 — frontier Wave 5: Session scene lifecycle (rename → delete → reorder)

> Frontier build program, Wave 5. Baseline **`6ca11cd`** (was `09c4928` before this session's history rewrite;
> see HANDOFF). Adds three gates (`--selftest-scenerename`,
> `--selftest-scenedelete`, `--selftest-scenereorder`) → floor **29 → 32**. A serial spine (ProjectSession
> seams + main.cpp gates + SessionView wiring, orchestrator-owned) + ONE file-disjoint UI agent
> (`SceneColumnComponent`, built on the Fable model — design authority). Every engine seam was source-verified
> against `libs/tracktion_engine` before the fan-out; a 5-dimension adversarial QC swarm followed.

## What shipped

The Session scene grid was **grow-only** (W07 +Scene). W15 makes scenes a live, editable set-list:

- **Rename** — double-click a scene-row name (or right-click → *Rename…*) opens an inline editor; commit on
  Return / focus-lost, Escape cancels. Blank persists (the row falls back to its 1-based number).
- **Delete** — right-click → *Delete scene* removes the row + every track's slot (and any clip) immediately;
  one Ctrl+Z restores it all (no confirm dialog — matches W07 Delete-clip).
- **Reorder** — right-click → *Move up* / *Move down* (edge-disabled at the ends).

The bare right-click-stop gesture on a scene row is replaced by a real **PopupMenu**
(`■ Stop scene / Rename… / — / Delete scene / Move up / Move down`) — the scaffold W7's whole-scene
"Send to Arrangement" will later append one item to (the critic's territory-collision finding #1).

## The seams — `ProjectSession` (orchestrator-owned)

Three new seams, all **undoable** (unlike grow-only `ensureScenes`, which is off-stack). The shell owns the
per-gesture transaction boundary (via `onEditMutated`), so one Ctrl+Z = one gesture; the seams never
`beginNewTransaction` themselves. Scenes resolve fresh via `edit->getSceneList().getScenes()[index]` — no
`te::Scene*` cached (R1). Null-edit / out-of-range paths `FORGE_LOG_WARN` + no-op.

- `setSceneName(index, name)` / `getSceneName(index)` — `te::Scene::name` is a `CachedValue<String>` bound to
  the Edit UndoManager (`tracktion_Scene.cpp:19-20`), so the write is undoable and dirties the tree. No
  `markAsChanged` — the shell seals + saves.
- `deleteScene(index)` → `SceneList::deleteScene` — removes the SCENE row **and** every track's slot + clip.
  Verified: the scene-child removal (`tracktion_Scene.cpp:228`), `clip->removeFromParent()`
  (`tracktion_Clip.cpp:397-401`) **and** `ClipSlotList::deleteSlot` (`tracktion_ClipSlot.cpp:179-183`) are **all**
  UndoManager-bound → one transaction restores scene + slots + clips atomically.
- `moveScene(from, to)` — **the load-bearing one: no engine `moveScene` seam exists.** Both `SceneList` and
  `ClipSlotList` are `ValueTreeObjectList`s whose `objectOrderChanged()` is an empty override (the base
  auto-resyncs its `objects` array on a raw child move), and both expose a public `state` ValueTree. So reorder
  is a raw `ValueTree::moveChild(from, to, um)` on the SCENES tree **and** every track's CLIPSLOTS tree, same
  `from`/`to`, in one transaction — **the scene tree and every slot tree move in lockstep** (the desync guard),
  else scene-row N drifts from slot-row N.

## The UI — `SceneColumnComponent` (Fable, file-disjoint)

`SceneRow` gains a hidden `juce::TextEditor` (shown on double-click over the name bounds — gated so a
double-click on the ▶ button doesn't both launch and edit), a commit-once `editing` flag (Return→commit then
focus-lost re-enters and no-ops), and the row PopupMenu. Four new upward seams (`onSceneRenamed`,
`onSceneDeleted`, `onSceneMovedUp`, `onSceneMovedDown`) the shell binds in `wireScenes()`. Neutral chrome per
the Fable charter — the editor uses panel/raised/text tones, the amber highlight is the *existing* selection
accent (no new colour); the stop item uses the **■** glyph, not a play triangle (one glyph = one meaning).

## The UAF guard — deferred rebuild (`SessionView::afterSceneMutation`)

A rename commit fires from **inside** the scene row's own `TextEditor` callback, and the natural
`onEditMutated() + rebuild()` response would `rows.clear()` — destroying that very row + its editor while its
method is live on the stack (a use-after-free). Both sides defer past it: the UI async-fires the seam (the
captured `std::function` binds the *column*, which survives rebuilds), and `afterSceneMutation` seals undo
**synchronously** but defers `rebuild()` via `MessageManager::callAsync` + a `SafePointer<SessionView>`. The
25 Hz poll watches TRACK count only (never scene count), so a direct `rebuild()` is the required refresh after a
scene mutation — and deferring it one message hop lets the callback unwind first.

## Proof — three gates (floor 29 → 32)

Synchronous runners mirroring `runSlotDeleteSelftest`. `-scenerename` / `-scenedelete` / `-scenereorder` all
CONTAIN `-scene`, so both ladders order them **longest-first before `--selftest-scene`** (verified the report
`mode=` line for each — no mis-dispatch).

- **`--selftest-scenerename`** — name reads back; blank persists; a sealed rename reverts on undo; other scenes
  untouched.
- **`--selftest-scenedelete`** — delete drops the count; scenes AND their per-track slots shift down in lockstep
  (fixture fills (0,3) with a marker clip); one undo restores count + names + the clip (proving the clip rode the
  UndoManager); out-of-range is a no-op.
- **`--selftest-scenereorder`** — `moveScene(0,2)` reorders names AND the per-track clips in lockstep (the desync
  guard: (0,0)→(0,2), (0,2)→(0,1), (0,0) now empty); undo reverts; equal / out-of-range indices no-op.

All **32 gates PASS**, clean 0-warning Debug build, `--screenshot` matrix renders all 10 states.

## Adversarial QC (5 dimensions)

Five parallel skeptics (default-refute, evidence-required), one per risk axis. **One MAJOR found + fixed**,
two MINORs fixed, one benign MINOR documented; two dimensions clean.

- **D3 reorder lockstep — CONFIRMED MAJOR (fixed).** The uneven-slot-count desync: a freshly `+Track`'d track
  materialises slots only on demand (up to the touched row), so it can hold a **filled** slot yet have fewer
  total slots than the move target. The original per-track `moveChild` **skipped** any track shorter than `to` —
  leaving that track's clip behind while the scene moved, a silent scene↔clip desync that **persists to disk**.
  **Fix:** `moveScene` now pads every track to the scene count (`ensureNumberOfSlots`, a no-op for full tracks)
  *before* the move, in the same transaction, so every CLIPSLOTS tree has identical child count and the same
  `(from,to)` aligns everywhere — the skip branch is gone. Proven by the new `unevenLockstep` gate leg (fails
  under the old guard, passes only with the fix).
- **D2 delete — CONFIRMED MINOR (fixed).** `deleteScene` omitted the **stop-before-delete** that `clearSlot`
  and the duplicate path both perform, so deleting a row with a *playing* clip left it briefly sounding until
  the async graph rebuild (not a UAF — the `LaunchHandle` is shared_ptr-owned by the graph). **Fix:** stop the
  deleted row's clips across all tracks first. Atomicity, index-shift, R1 lifetime all REFUTED clean; the gate
  now also deletes a row that *contains* a clip (`deletedRowClipRemoved`).
- **D5 gate integrity — CONFIRMED MINOR (fixed).** The rename gate's `otherUntouched` leg was a vacuous
  disjunction (`isEmpty() || != "Solo"`) that passed trivially. **Fix:** capture scene 4's (seeded) name before
  renaming its neighbour and assert it's unchanged — a real index-isolation check. Ladder ordering, enum/dispatch
  wiring, report format, and the delete/reorder assertions all REFUTED clean.
- **D1 rename UAF — REFUTED (1 benign MINOR documented).** The `editing`-flag exactly-once guard, the by-value
  async seam capture, and both SafePointers are correct; the menu-rename focus ordering is safe by JUCE's
  callback registration order (userCallback fires before focus-restore, so the editor keeps focus). The one
  behavioural note: an *unrelated* rebuild landing mid-edit **silently commits** the partial rename (synchronous
  focus-loss on editor destruction — no UAF) rather than discarding it; benign under W5 semantics (blank allowed,
  Ctrl+Z reverses). No code change.
- **D4 threading / seal / save / floor — REFUTED clean.** Seal runs synchronously before the deferred rebuild
  (save captures post-mutation truth); `save()` forces a write regardless of `markAsChanged` so a rename can't be
  lost; undo granularity is one-Ctrl+Z-per-op; the async-rebuild gap is bounds-safe (the poll caches no engine
  pointer, every array access is `isPositiveAndBelow`-guarded); the below-16 floor holds (`jmax(16, N)`).

## Follow-ups (documented, not built)

- **Whole-scene "Send to Arrangement" (W7)** appends one item to the PopupMenu this wave scaffolds — never a
  competing `mouseDown` rewrite (the critic's finding #1).
- **Drag-to-reorder** — parked: no headless mouse-drag driver; the keyboard/menu Move path + the seam ship.
- **Scene colour / multi-select / a save→reload round-trip gate leg** — later grid-polish.
- **Menu wording** is a Fable call; the defaults ship.
