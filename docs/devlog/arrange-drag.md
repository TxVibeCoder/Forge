# Devlog — Arrange clip drag-to-move + bar snap + info/help hint

Scope: `src/ui/arrange/ArrangeView.{h,cpp}` only. **Additive** — every existing public method the
shell relies on keeps its exact signature and behaviour: `ArrangeView(TimelineView&)`, `setEdit`,
`rebuild`, `resized`, `paint`, `getNumClipComponentsOnTrack0`, `getPlayheadX`, `refreshArmStates`,
and the public callbacks `onClipSelected`/`onTrackSelected`/`onEditMutated`/`onArmToggled`/
`isTrackArmed`. Clip enumeration in `TrackLaneComponent::rebuildClips()` is unchanged, so the
playback selftest invariant `getNumClipComponentsOnTrack0() == 1` after import is preserved.

## What changed

1. **Drag-to-move a clip along the timeline** (`AudioClipComponent`).
   - New `mouseDrag`/`mouseUp` overrides; `mouseDown` extended to capture the drag anchor.
   - `mouseDown` (left button) selects the clip (unchanged) and records the anchor: the pointer x
     in **parent (lane) coordinates** via `e.getEventRelativeTo(getParentComponent()).x`, plus the
     clip's original start `clip.getPosition().getStart()`. Parent-space anchoring is deliberate —
     the live drag moves this component, so an anchor in the component's own space would shift under
     us each frame and the delta would jitter; the parent doesn't move.
   - `mouseDrag` ignores movement until the pointer crosses a 3px threshold (so a plain click never
     nudges), then fires `onDragStarted`. Each frame it converts the pixel delta to a time delta via
     the shared `TimelineView` (`xToTime(dx) - xToTime(0)`), derives a candidate start, optionally
     snaps it (see below), clamps to `>= 0`, and moves the component live with
     `setTopLeftPosition(headerW + timeToX(candidate), getY())` — **horizontal only** (cross-track
     move is future). No `rebuild()` mid-drag: the held `te::Clip&` stays valid for the component's
     lifetime and only its bounds change.
   - `mouseUp` recomputes the final start the same way, snaps/clamps, and commits to the engine with
     `clip.setStart(newStart, /*preserveSync*/ false, /*keepLength*/ true)` — `keepLength=true`
     moves the clip horizontally without resizing; `preserveSync=false` is a plain timeline move
     (verified in `libs/.../model/clips/tracktion_Clip.h:267`). It then fires `onDragCommitted`.

2. **Bar snap** (`ArrangeView::snapToBar`, default ON).
   - New public `setSnapEnabled(bool)` / `isSnapEnabled()` and a private `bool snapEnabled = true`.
   - `snapToBar(TimePosition)` mirrors the ruler's robust idiom: `edit->tempoSequence.toBarsAndBeats(t)`
     gives `{bars, beats, numerator}`; the fraction of the bar already elapsed is
     `bb.beats.inBeats() / numerator`; `nearestBar = bb.bars + lround(fraction)`; the snapped time is
     `tempoSequence.toTime(te::tempo::BarsAndBeats{ nearestBar, BeatDuration() })` (zero beats = the
     exact bar start; `toTime` reads tempo sections so the `numerator` field is not consulted).
   - **Never round-trips a negative time** (which can trip engine asserts): `t <= 0` returns `0`
     immediately, and `nearestBar`/result are clamped to `>= 0`.
   - The seam is wired down per-lane (`TrackLaneComponent::snapStartTime`) into each clip
     (`AudioClipComponent::snapStartTime`). When snapping is off, `snapToBar` returns its input, so
     drag is free-moving.
   - **Ctrl/Cmd bypass**: the clip checks `e.mods.isCommandDown()` and skips the snap callback for
     that drag (live and on commit), the standard DAW temporary-bypass gesture.

3. **Info/help hint strip.** A themed one-line strip across the very bottom of the arrange surface
   (`ArrangeLayout::hintH = 20` px), painted in `panelBg` with a `hairline` top edge and `textSec`
   text. It shows `"Drag a clip to move it - hold Ctrl to bypass snap"` at rest, updates while
   dragging (`"Moving clip - snapping to bars - hold Ctrl to bypass snap"`, or `"... - snap off"`),
   and clears when there is no Edit. `resized()` reserves the strip and stops the playhead overlay
   above it.

4. **Playhead no longer shadows clips** (`PlayheadComponent::hitTest`, new override). The playhead is
   a full-width overlay added last (topmost) with `setInterceptsMouseClicks(true,false)`, so it
   previously intercepted **every** mouse event over the clip area — clips could never receive a
   `mouseDown`, so drag (and click-selection by clip body) was impossible. The new `hitTest` makes
   the overlay grab the mouse only within ~5px of the current playhead line
   (`abs(x - timeToX(transport.getPosition())) <= 5`); everywhere else it is transparent to clicks,
   so events fall through to the clips (drag) and lanes beneath. Dragging the playhead line still
   scrubs across the full width because JUCE captures the mouse to the playhead for the duration of
   the drag once `mouseDown` lands in the band.
   - To preserve the old *click-anywhere-in-the-clip-area-to-scrub* behaviour now that empty-area
     clicks reach the lane, `TrackLaneComponent` got a new `onLaneAreaScrub(clipAreaX, clipAreaWidth)`
     callback; `ArrangeView` wires it to `edit->getTransport().setPosition(view.xToTime(...))`.

## Public API (exact signatures)

Added to `ArrangeView` (header, additive — nothing removed/changed):

```cpp
void setSnapEnabled (bool shouldSnap);     // default state is ON (snapEnabled == true)
bool isSnapEnabled() const;
```

Added to `AudioClipComponent`:

```cpp
void mouseDrag (const juce::MouseEvent&) override;
void mouseUp   (const juce::MouseEvent&) override;
std::function<void (AudioClipComponent&)> onDragStarted;
std::function<void (AudioClipComponent&)> onDragCommitted;
std::function<te::TimePosition (te::TimePosition)> snapStartTime;
```

Added to `TrackLaneComponent`:

```cpp
std::function<void (AudioClipComponent&)> onClipDragStarted;
std::function<void (AudioClipComponent&)> onClipDragCommitted;
std::function<te::TimePosition (te::TimePosition)> snapStartTime;
std::function<void (int clipAreaX, int clipAreaWidth)> onLaneAreaScrub;
```

Added to `PlayheadComponent`:

```cpp
bool hitTest (int x, int y) override;
```

All of these are wired internally inside `ArrangeView::rebuild()` / `TrackLaneComponent::rebuildClips()`.
The shell does **not** need to set any of them.

## How the shell wires this

**Nothing is required.** The feature is self-contained: `onEditMutated` is already wired by the
shell (`src/main.cpp`: `arrangeView.onEditMutated = [this]{ session.save(); };`), and the drag-commit
path calls `notifyEditMutated()` → that existing callback → save. Snap is default-ON and the
Ctrl-bypass is handled entirely inside the clip. No CMakeLists change (no new files). No ControlBar
change.

**Optional (future) — a snap toggle in the shell**, if/when desired:
- File: `src/main.cpp`, in `MainComponent` where the arrange callbacks are wired (around the
  `arrangeView.onEditMutated = ...` block, ~line 189). Add a toolbar/ControlBar toggle button whose
  `onClick` calls:
  ```cpp
  arrangeView.setSnapEnabled (snapButton.getToggleState());
  ```
  Seed its initial state from `arrangeView.isSnapEnabled()` (true). This is purely additive and not
  needed for the feature to work.

## Unfinished (with why)

- **Cross-track (vertical) move** — drag is horizontal-only by design for this wave (the prompt
  scopes cross-track as future). The clip stays on its own lane; `setStart` only changes time.
- **Snap resolution is fixed to BAR** — no beat/grid-division selector yet. `snapToBar` is the only
  granularity; a future grid setting would parameterise the `toTime(BarsAndBeats{...})` target.
- **Hint strip occlusion under heavy track counts** — the hint is painted by `ArrangeView::paint`
  (the parent); opaque lane children paint on top of the parent. If enough tracks are laid out to
  reach the bottom `hintH` strip, a lane would draw over the hint. In the common case (few tracks,
  or the shell scrolls) the hint is clear. Left as-is to keep the change small; promoting the hint to
  a last-added child component would fix it if it ever matters.
- **No multi-clip / marquee drag** — single clip per gesture.

## Risks to verify at build time

- **`te::tempo::BarsAndBeats` aggregate init** — constructed as `{ nearestBar, te::BeatDuration() }`
  (third member `numerator` defaults to 0; `toTime` does not consult it). The struct lives in
  `tracktion::core::tempo` (an inline namespace), so `te::tempo::BarsAndBeats` resolves. Confirmed
  against `libs/.../tracktion_core/utilities/tracktion_Tempo.h:30` and the existing ruler code that
  already reads `bb.bars`/`getWholeBeats()`/`getFractionalBeats()`.
- **`Clip::setStart(TimePosition, bool, bool)`** — three-arg overload confirmed at
  `tracktion_Clip.h:267`; `keepLength=true` preserves length. (There is also `setPosition(ClipPosition)`
  and `setStart`-via-`setPosition`; the three-arg `setStart` is the simplest correct horizontal move.)
- **`MouseEvent::getEventRelativeTo(Component*)`** — confirmed at
  `libs/.../juce_gui_basics/mouse/juce_MouseEvent.h:352`; returns a `MouseEvent` by value.
- **Playhead `hitTest` vs. scrub** — verify by build+run: (a) dragging a clip body moves it and the
  new start persists (project re-save); (b) clicking empty clip area still moves the playhead;
  (c) grabbing the playhead line (±5px) still scrubs across the full width during a drag; (d) snap
  lands clip starts exactly on bar lines, and Ctrl bypasses it. `getPlayheadX()` (selftest) is
  unaffected — it reads `PlayheadComponent::getCurrentX()`, which the 30Hz timer still maintains.
- **TimePosition arithmetic/compare** — uses `operator-(TimePosition,TimePosition)->TimeDuration`,
  `operator+(TimePosition,TimeDuration)`, and `operator<`/`<=`, all confirmed in
  `tracktion_core/utilities/tracktion_Time.h`. Mirrors the existing `TimelineView::xToTime` math.
- **Negative-time guard** — `snapToBar` returns early for `t <= 0` and clamps, so no negative
  time/beat is ever round-tripped through the tempo sequence.
