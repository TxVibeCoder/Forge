# W10 — the hands-on wave, part 5 (the last): the Session → Arrangement "Send to" bridge

*Build wave off the maintainer's first hands-on session — **Wave 5 of 5**, the final item on the hands-on
plan. Baseline `76d8f38` (W09 tip). Single-CLI wave (no multi-CLI fan-out): the change is a tight, interlocking
spine across the shared/serial files (`ProjectSession`, `SessionView`, `main.cpp`), so the orchestrator built it
directly, then ran a 5-dimension adversarial QC. After this wave the hands-on plan is **complete.***

---

## What shipped

An explicit, **one-directional "Send to Arrangement"** action: right-click a **filled** Session clip slot →
**"Send to Arrangement"** copies that clip onto the **same track's** linear (Arrange) timeline, **appended at the
end** of that track's existing arrange content. This is the real answer to the maintainer's first-hands-on note
*"Session clip doesn't appear in Arrange"* — which was **intended behavior** (a locked decision: Session ↔ Arrange
stay separate, nothing auto-mirrors), now given the explicit bridge it was always meant to have.

**Locked product decisions (from the maintainer, this wave):**
- **Landing spot:** append at the end of the target track's timeline (0:00 for an empty lane). Never overlaps;
  you build an arrangement left-to-right by sending clips. (The alternative — drop at the playhead — was declined.)
- **Scope:** single clip only. A whole-scene "send" is a clean follow-up, deferred.

**Not changed** (by design): the source slot is a **copy source** — it is never moved, cleared, or mirrored.
Sending the same slot twice appends two independent arrange clips.

## The mechanism (engine-blessed, source-verified before any code)

Three parallel source-verification agents froze the design against engine + Forge source before implementation.
The copy uses Tracktion's **own clip-duplication idiom** (the one `te::split` uses):

```cpp
auto* src   = getClipSlot (t, s)->getClip();               // const, non-mutating resolve (R1/R2)
auto* track = te::getAudioTracks (*edit)[t];               // the clip's OWN track (keeps instrument/mixer routing)
const auto appendAt = track->getTotalRange().getEnd();     // end of this track's ARRANGE clips (0 if empty)
const auto srcPos   = src->getPosition();
const te::ClipPosition destPos { { appendAt, appendAt + srcPos.getLength() }, srcPos.getOffset() };
auto* newClip = track->insertClipWithState (src->state.createCopy(), src->getName(), src->type,
                                            destPos, /*deleteExistingClips*/ false, /*allowSpotting*/ false);
```

`insertClipWithState` (multi-arg) **re-IDs** the cloned state via `Edit::createNewItemID()` and **stamps** the
append position in one call, so a single `state.createCopy()` carries **everything** — the wave source/loop/gain/
fades, or the MIDI note sequence — identically for a `WaveAudioClip` or a `MidiClip`. `te::Clip::moveTo` was
rejected: it re-parents the live clip and would **empty the slot**.

Post-insert, the seam **normalizes the copy to a plain linear one-shot** and **makes it audible** (see QC §③/§①).

### Seam / wiring
- **`ProjectSession::sendSlotToArrangement (trackIndex, sceneIndex) → te::Clip*`** — the new seam, alongside the
  linear-timeline seams. Logs every fallible sub-step (`FORGE_LOG_ERROR`; `FORGE_LOG_WARN` for an empty slot).
- **`SessionView`** — a new "Send to Arrangement" item in the `filled` block of `handleSlotRightClicked` (id
  appended after `idDelete`), dispatched to a new null-guarded `onSendToArrangement` shell callback.
- **`main.cpp`** — the shell callback runs the seam, then `sealUndoTransaction()` + `session.save()` +
  **`arrangeView.rebuild()`** (ArrangeView has no clip-add listener — this is the load-bearing refresh) +
  a `setStatusMessage("Sent clip to Arrangement")`. Mirrors the existing `onCreateMidiClipRequested` precedent.
- **Gate `--selftest-sendarrange`** — new; the **24th** selftest gate.

## Adversarial QC — 5 dimensions, per-finding skeptics, default-refute

Two CONFIRMED defects (both fixed, both now headlessly proven); three dimensions REFUTED clean with citations.

### ③ [CONFIRMED — **HIGH**] The sent clip was **silent** in Arrange playback — `playSlotClips`
`AudioTrack::playSlotClips` (`CachedValue<AtomicWrapper<bool>>`) gates slot-vs-arrange audibility per track:
the arranger node computes `playArranger = ! track->playSlotClips.get()`
(`tracktion_ArrangerLauncherSwitchingNode.cpp:151`). The flag **latches TRUE** the moment any of the track's
slots plays (`:333`) and **nothing in the engine's live path ever clears it** (grep: the only `= false` is the
renderer's save/restore). So the natural sequence — launch a slot, send it to Arrangement, stop the slots, play
the arrangement — leaves the copy **silent**: the clip is visibly on the timeline (the gate passed) but the
arranger output is gated off. **Fix:** the seam sets `track->playSlotClips = false` after a successful send — the
engine's defined Session→Arrange handoff (it stops any still-playing slot on that track, harmless when none is),
which is exactly what "Send to Arrangement" means. **Proven:** the gate latches the flag TRUE before the send and
asserts it is FALSE after (`arrangeAudible=1`).

### ①+② [CONFIRMED — Medium, latent] The copy inherited the slot's **auto-tempo + loop range**
A clip placed in a `ClipSlot` is force-normalized by the engine (`setAutoTempo(true)`, a full-length loop range,
`start=0`); `state.createCopy()` carries that onto the timeline. Consequences the skeptics traced:
- **Loop-tiling (confirmed latent):** the arrange copy has `isLooping()==true`. Benign at send time (loop length
  == clip length → exactly one iteration, which is why the demo + gate looked fine), but the moment the user
  **drags the clip's right edge longer** it **re-tiles** its content instead of revealing more source — a
  one-shot silently becomes a repeat.
- **Time-stretch (REFUTED for the real case):** auto-tempo is a mathematical no-op on un-authored content
  (`getBeatsPerSecond = bpm/60` exactly → zero stretch); only ACID-tagged loops warp, which is correct and
  Forge's CC0 / MIDI-record content carries no such metadata. The divergence surfaces only as a **tempo-change**
  behavioral difference from a plain import.

**Fix:** the seam normalizes the copy to a plain linear one-shot — `disableLooping()` (the engine's
position-preserving loop clear, on both `AudioClipBase` and `MidiClip`) plus `setAutoTempo(false)` for the audio
path. **NB:** `setLoopRangeBeats({})` is deliberately **not** used — it re-asserts `setAutoTempo(true)`
(`AudioClipBase.cpp:1013-1014`), which would undo the normalization. **Proven:** `copyNotLooping=1` (MIDI) and the
wave leg's `waveNotLooping=1` + `waveNoAutoTempo=1`.

### ① [REFUTED] Wave-clip source & field fidelity
The audio source is `IDs::source` (a relative path string); `state.createCopy()` deep-copies it and the re-ID
rewrites only name/position/EditItemID — never the source — so the copy resolves to the identical file (edit-wide
resolver, same edit). gain / fades / loop / offset / speed are all `CachedValue`s in the state tree → copied
verbatim; offset is cleanly re-stamped. No dangling source, no lost field. **Proven** additionally by the gate's
new wave leg (`waveSourceMatches=1`).

### ④ [REFUTED] Undo + object lifetime
The insert is undoable (`ClipOwner.cpp:325` passes the UndoManager to `addChild`) and correctly scoped
(seal-after-insert); undo works on a **detached deep copy**, so it can't corrupt the source slot. The missing
`reconcileDrawerClip()` is provably harmless — the send is **add-only** and never re-parents a held clip
(reconcile is scoped to *deletes*). Both views are `MainComponent` members (no lifetime window); the menu path
holds no `ClipComponent`. **Proven:** the gate's undo leg (`undoRemovedCopy=1`, `sourceIntactAfterUndo=1`).

### ⑤ [REFUTED] Placement math + refresh completeness
`getTotalRange().getEnd()` is exactly `TimePosition(0)` for an empty lane (no sentinel), excludes slot clips
(`getClips()` is the track's own list), and `deleteExistingClips=false` never truncates a neighbor. Looping
doesn't corrupt the append point (`getPosition()` reads raw length). `arrangeView.rebuild()` refreshes the lanes
+ automation lanes regardless of the current view; no other surface goes stale (no track added → mixer/tray
untouched); correctly does **not** fire `sessionView.onEditMutated` (the source slot is unchanged).

## The gate (`--selftest-sendarrange`, the 24th)

Synchronous (one `callAsync` yield). 26 assertions across two legs, all green:
- **MIDI leg (track 0):** seed 4 notes into slot (0,0) → send → `sourceIntact` (copy, not move) ·
  `arrangeClipAppeared` · `noteCountPreserved` (4/4 — the sequence rode along) · `landedAtStart` (append 0) ·
  `copyNotLooping` (loop cleared) · `arrangeAudible` (playSlotClips latched TRUE → cleared FALSE) ·
  `secondAppended` (2nd send → 2 clips, appended past the 1st) · `undoRemovedCopy` + `sourceIntactAfterUndo`.
- **Wave leg (track 1):** a self-generated sine WAV → import into slot → send → `waveIsAudioClip` ·
  `waveNotLooping` · `waveNoAutoTempo` · `waveSourceMatches` (the AudioClipBase normalization + source fidelity
  the MIDI leg can't reach).

## Verify

- Clean MSVC Debug build (0 warnings on the changed TUs; the rest was already clean).
- **All TWENTY-FOUR selftests PASS** (the 23-gate floor + `--selftest-sendarrange`); every `mode=` line matches
  its flag — `sendarrange` neither shadows nor is shadowed by any existing gate (ordered before the bare
  `--selftest` in both command-line ladders).
- The 10-state `--screenshot` matrix renders; Session view unchanged (the menu item is a popup, not in static
  renders).

## Gotchas banked (see CLAUDE.md / HANDOFF.md)

- **`playSlotClips` is a sticky, one-way-latching per-track flag** — TRUE once a slot plays, cleared by nothing
  in the live path. Any code that wants a track's **arrange** clips audible must set it false. It is the engine's
  Session↔Arrange playback switch.
- **A clip copied out of a ClipSlot carries slot-normalized state** (auto-tempo on, full-length loop range,
  start 0). To place it on the linear timeline as a plain one-shot, `disableLooping()` + `setAutoTempo(false)` —
  **never** `setLoopRangeBeats({})` (it re-asserts auto-tempo).

## Follow-ups (documented, not built)

- **Whole-scene "Send to Arrangement"** — send every filled clip in a scene to its track, aligned at one start
  (a vertical Session slice → a bar of arrangement). Deferred by the scope decision; the natural next extension.
- **Send-as-loop** — the copy is normalized to a one-shot (the conventional default, matching a direct import).
  If the maintainer later wants a sent loop to *stay* a tempo-locked loop, that's a one-line product toggle.
- **Runtime audio** could not be sampled headlessly; audibility is proven by the `playSlotClips` state assertion,
  not by rendering the arrangement to non-zero samples. (Same class as the standing W09 render-leg follow-up.)

*After W10 the hands-on plan is complete. Next planning source: the Waveform feature-mining backlog
([waveform-feature-mining.md](waveform-feature-mining.md)).*
