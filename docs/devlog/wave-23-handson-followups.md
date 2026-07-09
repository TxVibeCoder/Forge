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

## Verification (round 2)
Single integration build (0 warnings) · **46/46 selftest floor** · 12/12 screenshots. The two
implementation agents' files were disjoint, so consolidation was collision-free; each proposed its
`main.cpp`/CMake wiring, applied by the orchestrator.

---

## Round 3 — the follow-ups

The maintainer asked for the documented follow-ups to be knocked out. Three parallel implementation agents
on disjoint territories (ArrangeView + a new audio helper / PianoRollView / TapTempo + LcdDisplay), with
`main.cpp` + the build + the floor owned by the orchestrator. **No new gates** — all five items extend
existing ones, so the floor stays **46**.

### Time-signature: place a change from the arrange ruler
`TimeRulerComponent` now intercepts a **right-click** → `PopupMenu` "Insert time signature change here…" →
a `TimeSigPopup` in a `CallOutBox` anchored at the click. New public
`ArrangeView::insertTimeSigAtBar(TimePosition, num, den) -> BeatPosition`: **floor-snaps to the bar
containing the click** (`toBarsAndBeats(t).bars → toBeats({bar, 0})`), writes via `EngineHelpers::setTimeSigAt`,
then `notifyEditMutated()` + repaint, and returns the snapped beat so the gate can assert the snap. Gate leg
`rulerInsertAtBar` (a mid-bar beat 9.5 lands on beat 8, reads "7/8"). The ruler's `setInterceptsMouseClicks`
flipped to true — left-clicks are ignored, so no scrub/selection regression.

### Trim: audio clips + a piano-roll trigger
New header-only `src/engine/AudioEditHelpers.h` — `forge::audioedit::trimLeadingSilence(te::AudioClipBase&,
float thresholdDb = -60.0f)`: opens a `juce::AudioFormatReader` on the source and uses `searchForLevel`
(all-channel, block-buffered, bounded to the clip's source span) to find the first supra-threshold frame,
then applies the same `setStart(preserveSync=true, keepLength=false)` crop. `ArrangeView::trimClipStart` now
dispatches MIDI → `midiedit`, audio → `audioedit`, and the menu item shows for both.

> **A real source-verification catch.** The spec I handed the agent said the advance is
> `Δ = (Ts − offset) / speed`. Tracing the engine's own waveform draw (`sourceStartSecs * speed`) showed the
> clip **offset is in edit-seconds** while the silence scan yields a **source-second**, so the correct
> advance is `Δ = Ts/speed − offset` — the two agree only at `speed == 1`. Rather than ship a subtly wrong
> formula, the helper **guards on `speed == 1` and declines otherwise** (every non-stretched import/recording,
> i.e. everything the UI offers this on). Documented, not hidden.

The piano roll gained a **Trim** button in its nav strip. It fires a new `onTrimClipRequested` callback
rather than trimming itself: a trim is a **clip/arrange-level** edit (unlike the roll's note-level edits), so
the shell performs it, seals one undo step, saves, `arrangeView.rebuild()`s and re-fits the roll.

### Tap tempo: a true 3–4-tap average
`currentBpm()` now returns `nullopt` until **three** taps exist, so the first reported value always averages
≥2 intervals instead of a single-interval guess (`count < 2` → `count < 3`; capacity, gap-reset and clamps
unchanged). This **broke an existing gate leg** — `clampHigh` tapped only twice — which the gate caught: it
now taps three times (0/10/20 ms → 6000 BPM raw → clamps to 300). New `twoTapsNull` leg pins the raised floor
(`oneTapNull` alone can't distinguish a ≥2 floor from a ≥3 one). `TempoPopup.h`'s doc comment was corrected.

### Both proof gaps closed
- **Wheel routing.** `GridCanvas::mouseWheelMove`'s decode was extracted into a public
  `PianoRollView::handleWheel(mods, deltaX, deltaY, canvasX, canvasY) -> bool` (true = consumed; false = plain,
  caller forwards to the Viewport). Four new legs drive it directly: `wheelCtrlZoomsTime`,
  `wheelCtrlShiftZoomsPitch`, `wheelShiftPansTime`, `wheelPlainNotConsumed`. Real-mouse behaviour is unchanged.
- **LCD sig zone.** `LcdDisplay` exposes `getTempoZoneBounds()` / `getSigZoneBounds()`. The gate widens the
  LCD past its `keyZoneMinWidth` gate, forces a **synchronous** paint via `createComponentSnapshot` (the
  `--screenshot` idiom — `setSize` alone only marks it dirty), then asserts `sigZoneNonEmpty` + `sigSeamsWired`
  + `hitTest` at the zone centre. Only the `CallOutBox` launch itself remains interaction-territory.

## Verification (round 3)
Single integration build (0 warnings) · **46/46 floor** (every new leg green, incl. `audioStartMovedForward`
landing on the 1 s sine onset, `rulerInsertAtBar`'s bar snap, and all four wheel legs) · 12/12 screenshots
(the roll's nav strip renders the new Trim button without overflow).

**Residual (documented):** audio trim declines on a non-unity speed ratio; MIDI trim keys on the first
*note* (a controller-only clip no-ops); the `CallOutBox` launch and OS wheel delivery remain the only
interaction-territory surfaces.
