# W20 — frontier Wave 10: Step Clips (the drum-grid clip type)

> Frontier build program, Wave 10 — the CAPSTONE. Baseline `64bbb39` (tip of W19). Adds one gate
> (`--selftest-stepclip`) → floor **36 → 37**, plus an 11th `--screenshot` state (`session_stepgrid`). Frontier
> Wave 9 (LFO modifiers) was **SKIPPED** at the maintainer's call — its fully source-verified recipe is preserved
> in `docs/wave-9-lfo-recipe.local.md` (gitignored) for a future pickup. Built as a 2-role wave: one file-disjoint
> UI agent authored the net-new `StepGridView` (`src/ui/stepclip/`, new dir) while the orchestrator built the
> `ProjectSession` seam + shell wiring + gate. Two thorough source-verification passes froze the engine recipe
> first; a 4-dimension adversarial QC swarm followed (fix-then-reverify — it caught 3 real defects).

## What shipped

A first-class in-app **drum step-sequencer clip type** — the single most on-brand capability for a beat/clip
Session-first DAW, with no external editor. Right-click an empty Session slot → **New Step Clip** → a born-audible
step clip appears and opens in a **16×8 drag-to-toggle grid** in the bottom drawer. Toggle cells to build a drum
pattern; launch the scene to hear it. The clip is a first-class launcher clip (launch handle, launch quantise,
follow actions all work) and persists/undoes like any clip.

## The engine seam — `ProjectSession::createStepClipInSlot`

Clones `createMidiClipInSlot` but inserts a step clip via the GENERIC free `te::insertNewClip(*slot,
TrackItem::Type::step, name, range)` (there is no step-specific inserter, unlike `insertMIDIClip`), then
`dynamic_cast<te::StepClip*>` the raw `Clip*`. The huge win from source-verification: the **StepClip constructor
auto-builds the default grid** — 8 GM-drum channels (kick/snare/hats/…) + a 16-step pattern[0] — so the seam
builds nothing; it just `ensureDefaultInstrument` (born-audible via the same `LoopingMidiNode` path a MidiClip
uses) and `markAsChanged`. A step clip in a slot is **born looping the full length** (the desired launcher-drum
behaviour — no one-shot dance, and the ClipOwner StepClip insert-arm never touches auto-tempo, so none of the
W5/W10/W13 slot-normalization footguns apply).

## The UI — `StepGridView` (new `src/ui/stepclip/`, file-disjoint agent)

An immediate-mode grid editor (the drum analogue of `PianoRollView`): a left gutter of channel display-names
(drum lane names), then proportional step cells — inactive `raisedBg`, **active `playGreen`** (the Session grid's
"will sound" semantic, no new colour), a brighter beat divider every 4 steps. **Drag-to-toggle** (Ableton-style
paint): `mouseDown` seeds the paint value, a drag lays it across crossed cells, and `onEditMutated` fires exactly
once per genuinely-changed cell (no undo flood). R1-disciplined: caches only the clip handle, re-resolves
`getChannels()`/`getPattern(0)`/`getCell` fresh every paint and gesture (a `Channel*`/`Pattern` is never stored).

## The shell wiring — the `isMidi()` drawer gotcha

`StepClip::isMidi()` returns **false** (it's a sibling `Clip`, not a `MidiClip`), so it would fall through to the
DetailView inspector. Fix: a `BottomMode::StepGrid` drawer mode + an explicit `dynamic_cast<te::StepClip*>` branch
BEFORE the `dynamic_cast<te::MidiClip*>` in BOTH `onSlotSelected` (Session) and `onClipSelected` (Arrange). Plus
the drawer layout (a third pane), `reconcileDrawerClip` (parent-loss unbind), `swapProject` unbind, and a "New
Step Clip" empty-slot menu action → new `onStepClipCreated` seam → opens the drawer.

## The gate + screenshot

`--selftest-stepclip` (floor 36 → 37) creates a step clip and proves the whole chain: the auto-built grid (8
channels × 16 steps), born-audibility (a synth on the track + `generateMidiSequence` emits note-ons when a cell is
on and none when empty), the `setCell/getCell` round-trip, undoability, AND the UAF regression (below). An 11th
`--screenshot` state (`session_stepgrid`) renders a populated drum pattern in the drawer — the visual proof the
model gate can't give (the drum-lane names + green cells + beat grouping all render correctly).

## QC — four dimensions, THREE defects caught + fixed (fix-then-reverify)

1. **Seam + gate** — one **conditional defect**: the clip length was hardcoded to 4 beats, correct only in 4/4.
   The default pattern is `getBeatsPerBar()` beats (= the time-sig numerator), and the ClipOwner arm sets the
   launcher loop to the inserted length — so in 3/4 the loop would have a silent beat, in 5/4 steps would be
   truncated. **Fixed**: derive the length from `getTimeSigAt(...).numerator` (correct in any meter). The QC also
   caught that the supporting comment cited `resizeClipForPatternInstances` (a dead, never-called engine
   function) — corrected to the real rationale (the ClipOwner loop-from-inserted-length).
2. **StepGridView UI** — one **high-severity UAF**: the view cached a **raw** `te::StepClip*`, but engine clips
   are refcounted — deleting a slot clip frees it synchronously, and `reconcileDrawerClip` then read `clip->state`
   on freed memory (PianoRollView avoids this exact bug by holding a `MidiClip::Ptr`). **Fixed**: hold a
   `te::StepClip::Ptr` (still R1 — R1 forbids caching a transient `Channel*`/`Pattern`, not a refcounted handle to
   the clip). Added a gate leg (`stepGridSurvivesDelete`) that binds a grid, drops every other ref, deletes the
   slot, and asserts the held clip is still readable + parentless (a raw pointer would dangle there).
3. **Shell routing / lifetime** — one **moderate defect**: undo/redo of a cell toggle left the grid painting
   STALE cells, because the StepClip `ChangeBroadcaster` only fires on *sequence* changes (not cell toggles), and
   `undoOrRedo()` refreshed the piano roll but never `stepGrid` (the exact W16 `refreshAfterExternalEdit` class).
   **Fixed**: a `stepGrid.repaint()` in the undo/redo fan-out (StepGridView is immediate-mode, so a repaint fully
   resyncs). The routing order, drawer-layout equivalence, `reconcileDrawerClip`, `swapProject` unbind, and
   member-destruction order all verified clean.
4. **Completeness / integration** — functionally clean: every end-to-end link built (launch handles a StepClip,
   filled-pad rendering shows a "STEP" pad, all seams reachable, ladder ordering + collision-freedom correct,
   CMakeLists correct). Only doc-sync items (the CLAUDE.md floor/list/matrix count + a W10-vs-W20 label slip),
   reconciled at consolidation.

**Documented v1 scope cut**: the born-audible instrument is the default 4OSC (a melodic synth), so the 8 drum
lanes sound as 8 distinct *pitches*, not real drum timbres. A drum sampler (mapping notes 36/38/42/… to
kick/snare/hat samples) for actual drum sounds is the clean follow-up.

## Frontier program — COMPLETE (with Wave 9 deferred)

Wave 10 is the capstone. Of the 10-wave frontier program, 9 are shipped (Waves 1–8 + 10); **Wave 9 (live
modulation / LFO) was deliberately skipped** and its recipe preserved for a future pickup.
