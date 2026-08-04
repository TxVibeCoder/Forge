# Wave 26 — Instrument assignment (B6: the browsable CC0 library)

> The maintainer asked what was left after W25; the honest answer was **B6's dead code**. W24 (`13a1f0f`)
> had shipped four self-rendered CC0 melodic voices — PluckBass / Pad / Bell / Clav — and a grep showed
> every reference living in `src/engine/` with **zero in `src/ui/` and zero in `main.cpp`**: the voices were
> compiled into the binary and unreachable. This wave built the path to them. Single-CLI wave.

## The gap

`PluginHost::applyInstrumentPreset(track, preset)` already did the whole engine job — inserted the voice at
the track head, removed any existing head synth first so presets never stack. Nothing was missing at the
engine layer. What was missing was every route to it: no menu, no list, no seam. B6 had sat open since W09
on exactly one blocker — *"there is no way to drop/assign a library instrument onto a track."*

**Two decisions were put to the maintainer before building**, because they changed the work materially:
which surface(s) to build, and whether slots should be assignable. Answers: **both surfaces, seam first**,
and **tracks only**.

## What shipped

### One seam

`ProjectSession::setTrackInstrument(trackIndex, preset)` → bool. Resolves/grows the track via
`getOrInsertAudioTrackAt`, calls `applyInstrumentPreset`, `markAsChanged()`. Every surface routes through
it; **no view touches the plugin chain**. Failure paths log and return false.

### One catalogue — and one name table

`PluginHost::getInstrumentChoices()` returns `{preset, name}` for all seven voices;
`getInstrumentPresetName(preset)` is the single source of the user-facing strings.

The names had existed in **three** places: a chained ternary for the melodic voices, a hardcoded `"Piano"`
in the Sampler `addSound` call, and nothing at all for a UI to read. `applyInstrumentPreset` now takes its
Sampler sound name from `getInstrumentPresetName`, so **the label a picker shows and the sound loaded on the
track cannot drift** — the gate asserts this equality rather than assuming it (`namesMatchTable`).

Adding a voice is now: one enum value + one name + one `InstrumentSamples` renderer. Every surface picks it
up with no further edits — the catalogue is what all three render from.

### Three surfaces

| surface | gesture | notes |
|---|---|---|
| **Arrange lane header** | right-click → `Instrument ▸` | **Appended** to the existing menu (Add/Rename/Delete Track, New MIDI Clip), never a competing rewrite. Target track is unambiguous — you right-clicked its lane. |
| **Session track header** | right-click → section `Instrument` | `TrackColumnComponent` had **no** header mouse handler; it gained one plus a static `isInHeaderBand` hit test. A section header rather than a one-item submenu — the whole menu is track-scoped, so an extra hop would buy nothing. |
| **Browser** | double-click (or Enter) an `INSTRUMENTS` row | A `ListBox` above the file tree, sized to its natural height but capped at ⅓ of the panel so a short sidebar still leaves the tree usable. |

The Browser list is **track-agnostic by design** — it knows nothing about tracks. The shell picks the target
via `instrumentTargetTrack()`: the focused Session column (Session is the primary view and owns its own
focus index), else the last-selected Arrange lane, clamped to the live track list so a since-deleted
selection can never address past the end. The status strip then names **both** the voice and the track
(`Pad -> Bass`), because loading an instrument changes nothing visible on the grid — and from the Browser
the user never picked the track at all, so without that line the gesture would be a silent state change.

### Not built, deliberately

**Per-slot assignment.** The engine is track-level: a slot clip plays through whatever its *track* hosts.
The W21 "first-instrument-wins" gotcha and the W22 "Move to its own track" fix both exist because of exactly
this, so a per-slot picker would have been a lie about what the engine does.

## Gate — `--selftest-instrument` (floor 48 → 49)

Both surfaces are driven through the **real public entry point a mouse would reach**, never a mirror:
`SessionView::applyInstrumentToTrack` (what the header menu's callback invokes) and
`BrowserView::activateInstrumentRow` (what a row double-click invokes). The browser leg deliberately does
**not** stub the shell callback — it asserts the shell's own binding landed the instrument on the target
track, so it covers browser → shell → seam end to end.

Swaps are observable by plugin **type** (a seeded 4OSC displaced by a Sampler-backed voice), not by "something
changed". Full field contract → `../../tests/SELFTEST.md`.

**Negative control (run during the build, then reverted):** stubbing out the shell's
`browserPanel.onInstrumentChosen` binding flips `browserActivateApplied` to 0 **with `sessionRouteOk` and
`browserRowsOk` still 1**. The gate therefore catches an unwired UI seam — the exact failure class CLAUDE.md
warns about ("a UI seam a gate can't see can ship unwired"), and the one this wave existed to fix.

### B3's leftover closed in the same pass

The gate's phase 2 renders **all four melodic voices** — one note each on their own named tracks, all four in
a single `Exporter::renderStems` pass after a 900 ms ingestion pump (the Sampler loads on an `AsyncUpdater`;
rendering without the pump measures silence and would call it a bug). Three-state PASS/FAIL/SKIP, same
semantics as `-demo` / `-drumkit`. **First run was a genuine PASS**, `weakestPeak ≈ 0.31` — not a SKIP. B3 is
now fully done.

## Verification

- Build **clean (0 warnings)**, MSVC Debug.
- **49/49 selftest floor.**
- **12/12 screenshots** — the new surfaces are menus and a sidebar list; no new screenshot state was added.

## Follow-ups (small, honest)

- **Library growth** is the only part of B6 left, and it is no longer blocked on anything: one enum value +
  one name + one renderer. Self-rendered CC0 only — a public AGPLv3 repo, a locked decision.
- The Browser's INSTRUMENTS section is a **fixed** band. If the library grows past ~10 voices a collapsible
  section (or a filter box) will read better than a ⅓-height scroller.
- The Session track-header menu currently holds exactly one section. It is the natural home for future
  track-level items (colour, rename), which is why it was built as a menu rather than a bare action.
