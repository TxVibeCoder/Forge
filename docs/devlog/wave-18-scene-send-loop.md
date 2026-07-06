# W18 — frontier Wave 7 fast-follows: whole-scene-send + send-as-loop

> Frontier build program, Wave 7 (continued from W17's capture core, same session). Baseline `f89109c` (W17,
> committed). Adds one gate (`--selftest-scenesend`) → floor **33 → 34**; extends the existing
> `--selftest-sendarrange` gate with a send-as-loop leg (no new gate name). Both fast-follows ride the
> `insertClipCopyOnTimeline` seam W17 factored out of the W10 `sendSlotToArrangement` body. A 4-dimension
> adversarial QC swarm ran against both features + the shared-menu discipline + the gate quality — **all four
> dimensions came back clean, no confirmed defects, no fix-then-reverify cycle needed** (unlike W17, which
> caught and fixed one).

## What shipped

Wave 7's two remaining items, completing the frontier program's Wave 7:

- **Whole-scene "Send to Arrangement"** — a new `SceneColumnComponent` row-menu item, **"Send scene to
  Arrangement"**, sends every filled clip in a scene to its own track's linear Arrangement in one gesture. All
  copies land at **one shared start beat** — the max of the current append point across only the tracks that
  actually have a filled slot in that scene — so the scene's vertical (same-instant) relationship survives the
  move, and nothing overlaps a target track's existing content. One undo transaction removes every copy
  atomically.
- **Send-as-loop** — a second slot-menu item, **"Send to Arrangement (as loop)"**, shown only when the source
  clip is actually looping. The sent copy keeps its loop range + auto-tempo instead of being normalized to a
  one-shot (the existing "Send to Arrangement" item's behavior, unchanged and still the default).

## The seams — `ProjectSession`

Two additions, both reusing W17's `insertClipCopyOnTimeline(track, src, destPos, keepAsLoop)` helper verbatim
(no changes to that helper were needed):

- `sendSlotToArrangement(trackIndex, sceneIndex, bool keepAsLoop = false)` — the existing W10/W17 seam gained
  one parameter. The default preserves every prior caller's behavior untouched (C++ default-arg semantics); the
  UI's new menu item is the only caller that ever passes `true`.
- `sendSceneToArrangement(int sceneIndex)` — new. Two passes: pass 1 walks every track, resolves
  `getClipSlot(t, sceneIndex)->getClip()` fresh, collects `{trackIndex, clip}` for each filled cell, and tracks
  `sharedStart = max(sharedStart, track->getTotalRange().getEnd())` **gated on that same track having a filled
  clip** — an empty-for-this-scene track never contributes to the max. Pass 2 inserts one copy per collected
  item at the shared start, preserving each clip's own length/offset, with no `beginNewTransaction` between
  inserts (the caller brackets the whole gesture in one transaction, same discipline as W17's capture commit).
  Returns the count sent (0 for an empty scene / no edit / out-of-range index — logged as a `WARN`, not an
  `ERROR`, since "nothing to send" is an ordinary outcome, not a failure).

## The UI

- **`SceneColumnComponent`** — the new item is **appended** to the existing W15 row `PopupMenu` (`Stop scene /
  Rename… / — / Delete scene / Move up / Move down`), never a competing rewrite. This was the frontier
  program critic's flagged territory-collision point (finding #1: two waves both wanting to convert the same
  bare `mouseDown`→menu gesture) — W15 built the scaffold, this wave appends one item + a separator, exactly as
  prescribed. A new `onSceneSentToArrangement` seam threads row → column → `SessionView` (a public seam, since —
  unlike rename/delete/reorder, which `SessionView` handles internally via `afterSceneMutation()` — a scene-send
  needs `arrangeView.rebuild()`, which `SessionView` can't reach) → the shell.
- **`SessionView`**'s filled-slot right-click menu gained one conditional item, "Send to Arrangement (as loop)",
  shown only when `session.isSlotClipLooping()` is true (re-derived fresh on every right-click, never cached).
  Routes through the existing `onSendToArrangement` callback, whose signature grew a third `bool keepAsLoop`
  parameter — the one signature change this wave made, threaded end-to-end (declaration → the one shell wiring
  site → the one call site) and verified by a clean build + QC.
- **Shell wiring** (`main.cpp`) mirrors the existing single-slot handler's discipline exactly: seam call → guard
  on failure/empty (already logged by the seam) → `sealUndoTransaction()` → `session.save()` → `arrangeView.rebuild()`
  → a status message.

## The gate — `--selftest-scenesend` (new) + the `--selftest-sendarrange` loop leg (extended)

`--selftest-scenesend`'s fixture is built specifically to make "shared start" a meaningful assertion rather than
a vacuous one: it seeds different-length MIDI clips in two tracks' scene-2 slots, then **pre-seeds track 0's
arrangement** with an unrelated send so track 0's append point is pushed forward while track 1's stays at 0.
Without the shared-max fix, track 1's copy would land at its own (zero) append point instead of matching track
0's — the gate's `sharedStartMatches` + `sharedStartNonZero` assertions catch exactly that failure mode (and
guard against a degenerate sentinel-coincidence false-pass, per the QC swarm's Q1). The extended
`--selftest-sendarrange` loop leg proves `keepAsLoop=true` keeps a copy looping AND that a second send of the
*same* slot with the default `false` still normalizes — a control that would catch a broken always-keep-loop
implementation.

## QC — four dimensions, all clean

Unlike W17 (which caught and fixed a confirmed identity-vs-cell-index defect), this fast-follow pass ran clean
end to end:

1. **Whole-scene-send engine logic** — shared-start gating, per-clip fidelity, transaction atomicity, edge
   cases (out-of-range scene, the W15 uneven-slot-tree gotcha), R1 discipline, and idiom parity with
   `sendSlotToArrangement` all refuted clean.
2. **Send-as-loop signature change** — every reference to `onSendToArrangement` consistent (one declaration, one
   wiring site, one call site); menu-item conditionality re-derives fresh per right-click; no ID collision in the
   slot-menu's several id ranges; the `keepAsLoop` gate in `insertClipCopyOnTimeline` correctly wraps only the
   loop-clearing lines, leaving the audibility flip and metadata strip unconditional.
3. **`SceneColumnComponent` menu-append discipline** — the W15 architectural collision-point constraint held:
   all five original items unchanged in order/behavior, the new item's id and dispatch don't collide, the full
   seam chain (row → column → `SessionView` → shell) is genuinely wired at every hop, and lifetime/SafePointer
   discipline is applied uniformly even though this specific gesture never rebuilds the scene column.
4. **Gate quality + shell wiring** — the scene-send gate's shared-start assertion is substantively (not
   coincidentally) meaningful; the empty-scene and undo-atomicity legs check real return values and transaction
   boundaries, not just "didn't crash"; the loop leg's control assertion is load-bearing; both new shell handlers
   match the shipped single-slot handler's ordering exactly; the UI-to-seam wiring chain has no dangling seam.

## Wave 7 — now complete

All three items of frontier Wave 7 have shipped: performance capture (W17), whole-scene-send, and send-as-loop
(both W18). Full record of the capture core → [devlog/wave-17-performance-capture.md](wave-17-performance-capture.md).
