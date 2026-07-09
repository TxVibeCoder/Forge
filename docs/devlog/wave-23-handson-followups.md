# Wave 23 — hands-on follow-ups (piano-roll · trim · tap-tempo · time-signature)

A hands-on-feedback wave: the maintainer drove the built app and returned four concrete requests. Built
across two rounds (a piano-roll round, then a fan-out round for the other three), each verified headless.

Floor **43 → 46** (`--selftest-pianoroll`, `--selftest-timesig`, `--selftest-trim`; `--selftest-taptempo`
gained a `windowDropsStale` leg). Screenshot matrix **11 → 12** (`session_pianoroll`). Build clean (0
warnings).

---

## Round 1 — the piano roll (zoom · playhead · global undo · grid)

Four asks against the drawer piano-roll (`src/ui/pianoroll/PianoRollView.{h,cpp}`):

- **Zoom.** The roll's horizontal axis was *shared* with the arrange `TimelineView` (fixed 0–60 s), so a
  clip was squished and unreachable off the right edge. Decoupled the roll onto its **own** linear
  beat→pixel scale (`pxPerBeat` + `hOffsetBeats`), independent of arrange. On bind it *fits the clip*.
  Controls: **Ctrl+wheel** = zoom time, **Ctrl+Shift+wheel** = zoom pitch, **Shift+wheel** = pan time,
  plain wheel = pitch scroll; plus a bottom nav strip (Time −/+, Pitch −/+, Fit) + a horizontal scrollbar.
  `keyHeight` became a live member; `pitchToY`/`yToPitch` went from static to instance (the one
  `MidiNoteComponent` ripple).
- **Playhead.** New `PlayheadOverlay` (30 Hz, mouse-transparent, `timeTempo` clock colour) mirroring the
  arrange `PlayheadComponent`; reads `clip->edit.getTransport()`, maps beat→x through the roll's axis.
- **Global Ctrl+Z.** The shell already handled undo, but it depended on focus-bubbling that was unreliable
  once the roll grabbed keyboard focus. Fix: `PianoRollView::onUnhandledKey` — the roll forwards any key it
  doesn't consume locally to the shell (the same hatch `PopoutWindow` uses), so undo/redo (and every shell
  shortcut) fire from inside the roll, docked or torn off, regardless of focus.
- **Grid definition.** Rebuilt the grid hierarchy — bold **bar** lines › **beat** lines › faint
  **sub-beat** (adaptive: hidden when too dense) + clear row/octave separators + a mini black/white keybed.
  Switched the lines off the near-invisible `hairline` onto a graduated `textSec` alpha ramp (hairline sits
  one shade off `raisedBg`, so it washed out).

Gate `--selftest-pianoroll` (11 legs): beatToX/xToBeat round-trip, zoom-in widens/holds-anchor,
zoom-out narrows, pitch-zoom widens rows, playhead tracks forward + reads −1 off-window, **end-to-end
Ctrl+Z through the roll's own keyPressed reverts a seeded note** (the reported bug), and routing discipline
(a local key isn't forwarded; Ctrl+Z is). New `session_pianoroll` screenshot state.

## Round 2 — the fan-out (trim · tap-tempo · time-signature)

Three requests dispatched as **three parallel source-verification agents** (read-only) → three
implementation-ready specs → **two parallel implementation agents** on file-disjoint territories +
orchestrator-owned `main.cpp`/CMake integration. The disjointness was the load-bearing design choice:
placing trim in a header-only helper and time-sig in `EngineHelpers` + a *new* `TimeSigPopup` (not the
tempo popup) kept all three off each other's files.

### Trim silent start
Right-click an Arrange MIDI clip → **"Trim silent start"** (`clip->isMidi()`-gated). New header-only
`forge::midiedit::trimLeadingSilence` (`src/engine/MidiEditHelpers.h`): moves the clip's left edge forward
to its first event via `Clip::setStart(newStart, /*preserveSync*/true, /*keepLength*/false)` — the boundary
moves, every note keeps its absolute timeline position (the lead-in is absorbed into the clip offset,
reversible), nothing is deleted. Guards empty / already-tight / **looping** (arrange one-shot only — the
W5/W13 re-normalisation footgun). `ArrangeView::trimClipStart` mirrors `deleteClip`
(`notifyEditMutated()` + `rebuild()`); undoable in one Ctrl+Z (`setStart` is UM-bound). Gate
`--selftest-trim` (10 legs): start moved forward, note position preserved, offset increased, end preserved,
no leading silence, undo reverts start **and** offset, no-op on a second (tight) call. **Known edge:** trims
to the first *note* — a controller-only clip (no notes) no-ops. **Follow-up:** a piano-roll trigger
(deferred so it didn't collide with Round 1's uncommitted work) and an audio-clip variant (needs a
sample-analysis pass).

### Tap tempo — already a rolling average
Source-verification found `TapTempo.h` **already** implements the requested behaviour: a capacity-4 ring
buffer averaging the last ≤3 adjacent intervals, evicting the oldest tap as new ones arrive. Per the
verify-before-implement tenet, no functional change was made — added a gate leg `windowDropsStale`: five
taps (0, 1000, 1300, 1700, 2000 ms) → the `t=0` lead-in is evicted → mean of the last three intervals
`(300+400+300)/3` → **180.0 BPM**, a value that rules out both the unbounded-average (120) and
last-2-taps-only (200) models. **Open option (not taken):** the estimate applies after just 2 taps (a
single, jittery interval); requiring ≥3 taps before applying is a one-line refinement if wanted.

### Time signature
Click the LCD's **"· 4/4"** signature zone → a `TimeSigPopup` (`src/ui/transport/TimeSigPopup.{h,cpp}`,
new, modelled on `TempoPopup`): numerator field [1,32] + a power-of-two denominator `ComboBox` {1,2,4,8,16}.
New seam `EngineHelpers::setTimeSigAt(edit, BeatPosition, num, den)` over the engine's `insertTimeSig`
(returns the existing sig at beat 0 → mutates the initial meter; inserts elsewhere; UM-bound), with a
`nearestPowerOfTwoDenominator` clamp and `getTimeSigStringAt` for seeding. `LcdDisplay` gained a second
clickable zone beside the tempo zone (`querySig`/`onSigChanged` seams, `sigZoneBounds`, hitTest/mouseUp/
paint branches — mirrors the proven tempo path). The arrange ruler, snap grid, and piano-roll `beatsPerBar()`
already read the meter live and follow on repaint; the shell wiring nudges them after a change. Gate
`--selftest-timesig` (5 legs): num/den round-trip (3/4, 6/8), independent num≠den, mid-arrangement insert at
beat 16 (5/4) resolves by position while beat 0 keeps its own, denominator clamp, undo restores (content-
level, never `canUndo/canRedo` — the W16 4OSC redo-wipe gotcha). **v1 scope:** the popup edits the song's
initial (beat-0) meter; the seam supports mid-arrangement changes but there's no UI to place one yet (a
ruler right-click is the follow-up). **Documented limitation:** existing Step Clips do not retro-resize on a
meter change (their length is baked from the numerator at creation — the W20 design).

## Verification
Single integration build (0 warnings) · **46/46 selftest floor** · 12/12 screenshots. The two
implementation agents' files were disjoint, so consolidation was collision-free; each proposed its
`main.cpp`/CMake wiring, applied by the orchestrator. Interaction-only surfaces not headlessly gated (same
class as the existing tempo popup): the LCD-click→time-sig popup, and the piano-roll mouse-wheel zoom
(buttons/scrollbar/keys/playhead/undo-fallback **are** gated).
