# W14 — frontier Wave 4: MIDI quantise (piano-roll)

> Frontier build program, Wave 4. Baseline **`2edf78a`**; code **`52b6e66`**. Adds one gate
> (`--selftest-quantise`) → floor **28 → 29**. A file-disjoint wave: a new header-only `MidiEditHelpers.h` +
> `PianoRollView` + the gate — NO ProjectSession touch. Built after a 5-reader source-verification swarm (which
> corrected a baked grid-mapping error before any code); a 6-dimension adversarial QC returned **ship** (one
> nit fixed).

## What shipped

The piano-roll — which had **no** quantise — gains destructive MIDI quantise: press **`q`** to snap the
selection (or the whole clip when nothing is selected) to the grid. Note **starts only**; each note's length is
preserved. One undoable step. A 1:1 lift of the engine's own unit-tested `QuantisationType` idiom
(`tracktion_QuantisationType.cpp:334-336`) → near-zero engine risk.

## The seam — `src/engine/MidiEditHelpers.h` (NEW, header-only)

`forge::midiedit::quantiseNoteStarts(MidiClip&, gridBeats, strength, selectedOnly, isSelected, UndoManager*)` —
a pure helper so both the view and the gate call ONE implementation and no raw `te::QuantisationType` leaks into
the view. Uses a **local** `QuantisationType` (never `clip.getQuantisation()`, the clip's persistent playback
setting); `setProportion` **is** the 0-100% strength (`roundBeatToNearest` folds it in — no hand-lerp); and
`setStartAndLength(newStart, getLengthBeats() unchanged, undo)` guarantees starts-only. **No CMakeLists edit** —
`target_sources` is an explicit `.cpp`-only list, so a header is pulled in via `#include` at its call sites.

## The corrected grid mapping (verify-swarm catch)

The baked assumption "gridBeats 0.25 → 1/16" was **wrong**. The engine's `QuantisationType` type-names are
fractions of a **beat** (`"1/4 beat".beatFraction == 0.25`, `tracktion_QuantisationType.cpp:33`), so
`gridBeatsToTypeName(0.25) → "1/4"` — notes snap to the **visible** grid (the piano-roll's `gridBeats = 0.25`),
not 4× finer. Following the wrong text would have silently over-snapped; the `snappedToGrid` gate leg
(`0.1 → 0.0` at the 0.25 grid) would have failed under "1/16" (`0.1 → 0.0625`).

## Proof — `--selftest-quantise` (floor 28 → 29)

Seeds 3 off-grid notes (0.1 / 1.1 / 2.1) with distinct lengths, then asserts: **snap-to-grid** (each → nearest
0.25), **length preserved** (starts-only), **partial-strength** (a note at 0.1 quantised at **50%** lands at
**0.05** — the halfway interpolation proof, not a full snap), and **undo reverts**. All within eps 1e-4 (FP
interpolation). `quantise` is substring-collision-free; `mode=quantise` verified. All 29 gates PASS; clean
0-warning build.

## Adversarial QC (6 dimensions — ship)

- **D5 (fixed nit)** — `isKeyCode('Q')` ignores modifiers, so **Ctrl+Q** (the File ▸ Exit shortcut) would be
  *swallowed* by quantise when the piano-roll has focus. Guarded the branch on `!isCommandDown()`.
- **D1 grid math** — REFUTED: `"1/4"` resolves to fraction 0.25 (not 0.0625); `roundBeatToNearest` folds
  proportion exactly once; `isEnabled()` makes 0% / `(none)` a clean no-op.
- **D2 length preservation** — REFUTED: the original `getLengthBeats()` rides through verbatim; the note end
  translates rigidly with the start.
- **D3 selection / degenerate** — REFUTED: `isSelected` only pointer-compares (never derefs a stale pointer);
  the helper iterates a fresh `getNotes()` snapshot; every structural edit clears the selection first; an empty
  clip is a clean no-op.
- **D4 undo** — REFUTED: the N moves run in one synchronous `keyPressed` → one `UndoManager` action set → one
  Ctrl+Z, mirroring the proven `commitMoveSelection` pattern.
- **D6 helper safety / regression** — REFUTED: local `QuantisationType` (no playback-quantise mutation); the
  `getNotes()` snapshot keeps every pointer valid; the change is purely additive.

## How it was built

A 5-reader source-verification swarm froze the header + the piano-roll wiring + the gate, and corrected two
contested facts before any code: the grid mapping (`0.25 → "1/4"`, not "1/16") and the refresh method
(`layoutNotes` keeps the selection, `rebuildNotes` would wipe it). The orchestrator implemented + owned the
build + the 29-gate floor; a 6-dimension adversarial QC returned ship with one fixed nit.

## Follow-up — groove (documented, not built)

Swing groove rides the **same** seam: the engine ships two built-in parameterized swing templates ("Basic 8th
Swing" / "Basic 16th Swing"); a sibling `applySwingToNoteStarts` using `GrooveTemplate::beatsTimeToGroovyTime`
would land it. **Gotcha:** the manager defaults `useParameterizedGrooves=false`, so those templates are hidden
until a one-time `getGrooveTemplateManager().useParameterizedGrooves(true)` at startup. Deferred to keep the
wave scope tight; near-zero risk. A strength UI (< 100% via a slider / second key) is also seam-ready.
