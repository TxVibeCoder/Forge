# Session Grid Vertical Scroll — Frozen Design

Status: **FROZEN**. Fixes the "pads stretch when the window is tall / rows 10–16 are clipped
(unreachable) when the window is short" bug by making the grid content a **fixed** height and
enabling vertical scroll, while keeping the pinned scene column's launch rows aligned with the pads.
Every verified adversarial issue is folded in with `[FIX]` notes.

---

## 1. Header-pin decision

**SCROLL-EVERYTHING-TOGETHER.** The track header band and clip-stop footer live INSIDE each
`TrackColumnComponent`, which lives inside `columnHolder` inside the viewport, so they scroll with the
grid for free — zero extra structural layers. Pinning the header would require splitting each column
into an out-of-viewport header strip plus an in-viewport pad strip (or a second synchronized
viewport) and would force `rowBand` to take a grid-relative Y offset — exactly the drift risk the
handoff says to avoid by NOT touching `rowBand`. Cost of scroll-together: the track name + M/S/R
buttons scroll off the top when scrolled far down — acceptable for MVP, revisitable later.

The scene column is the deliberate exception: **horizontally pinned** (right edge, outside the
viewport) but **vertically synced** by translating it `-viewport.getViewPositionY()`, so its launch
rows stay glued to the pads. It is not a pinned header; it is a horizontally-pinned, vertically-synced
twin.

---

## 2. Root cause & core fix

`resized()` currently computes:
```cpp
const int contentH = jmax (viewport.getMaximumVisibleHeight(), columnH);
```
`jmax` stretches the pads when the window is tall and, with vertical scroll OFF, clips rows 10–16 when
the window is short. The fix: **fix `contentH = columnH` (844)** and **enable vertical scroll**. With a
fixed content height, a short window scrolls to reach the bottom rows, and a tall window shows empty
space below (no stretch).

---

## 3. Exact edits

### 3.1 `src/ui/session/SessionView.h`

**(a)** Replace the member `juce::Viewport viewport;` (line ~128) with a tiny nested subclass instance:
```cpp
    // Viewport subclass whose only job is to surface visibleAreaChanged as an onScroll callback,
    // so SessionView can translate the pinned scene column to match the vertical scroll offset.
    struct ScrollingViewport : public juce::Viewport
    {
        std::function<void()> onScroll;
        void visibleAreaChanged (const juce::Rectangle<int>& newArea) override
        {
            juce::Viewport::visibleAreaChanged (newArea);
            if (onScroll) onScroll();
        }
    };

    ScrollingViewport viewport;
```
Rationale: `juce::Viewport` is non-copyable/non-movable (`JUCE_DECLARE_NON_COPYABLE`), so any
`viewport = something` assignment cannot compile. Subclassing the member and overriding the virtual
`visibleAreaChanged` (`juce_Viewport.h:195`) is the only correct scroll intercept. Keeping the member
named `viewport` means every existing call site (`setViewedComponent`, `setBounds`,
`getMaximumVisibleWidth/Height`, `getViewPositionY`) compiles unchanged.

**(b)** In the private section, after the `repaintPad` declaration (~line 114), add:
```cpp
    void syncSceneColumnToScroll();  // translate the pinned scene column by -viewport.getViewPositionY()
```

**(c)** In the public section, after `int getNumColumns() const { return columns.size(); }` (~line 77):
```cpp
    /** Exposes the scrolling viewport so the headless screenshot harness can drive setViewPosition
        to prove vertical scroll. Message-thread only; UI-geometry accessor (does NOT let callers
        cache slots — R1 is preserved). */
    juce::Viewport& getViewport() { return viewport; }
```

### 3.2 `src/ui/session/SessionLayout.h`

After `constexpr int gap = 0;` (line 26), add:
```cpp
    constexpr int slotH = 46;                 // per-slot pad height used to SIZE the scrollable content
                                              // (columns then divide their middle region evenly across
                                              // numScenes); matches sheet 00 row pitch. Promoted from the
                                              // former file-local kSlotH in SessionView.cpp.
```
`columnH = headerH + numScenes*slotH + stopRowH = 78 + 16*46 + 30 = 844` (unchanged numerically).
`rowBand` (already present, `SessionLayout.h:35-42`) is **NOT touched**.

### 3.3 `src/ui/session/SessionView.cpp`

**(a)** Delete the anonymous-namespace block (lines 7–13) that defines `constexpr int kSlotH = 46;`.
It is superseded by `SessionLayout::slotH`.

**(b)** Ctor — replace `viewport.setScrollBarsShown (false, true);` (line ~20) with:
```cpp
    viewport.setScrollBarsShown (true, true);   // (vertical, horizontal): BOTH scrollbars enabled
    viewport.onScroll = [this] { syncSceneColumnToScroll(); };
```
Arg order is `(showVertical, showHorizontal)` (`juce_Viewport.h:210`). `setViewedComponent` (line 19)
and `addAndMakeVisible(viewport)` (line 21) are untouched. The lambda captures `this` and touches
`scenes`, which is null until `rebuild()` — `syncSceneColumnToScroll()` null-guards `scenes`, so an
early `visibleAreaChanged` during `setBounds` is safe.

**(c)** `resized()` — replace the `columnH / contentW / contentH` block (lines ~170–176):
```cpp
    const int columnH  = SessionLayout::headerH
                       + SessionLayout::numScenes * SessionLayout::slotH
                       + SessionLayout::stopRowH;                       // FIXED 844: 78 + 16*46 + 30

    const int contentW = jmax (viewport.getMaximumVisibleWidth(),
                               nTracks * SessionLayout::trackColW);
    const int contentH = columnH;   // fixed height => pads stay slotH tall; short window scrolls,
                                     // tall window shows empty space below (no stretch)
```
`contentW` keeps its `jmax` so few tracks still fill the width (horizontal behaviour unchanged).

**(d)** `resized()` — the scene column bounds (lines ~187–190). `[FIX: scroll-alignment — H-scrollbar
occlusion]` The horizontal scrollbar occludes the bottom ~8px of the viewport (track columns) but not
the out-of-viewport scene column, so the bottom launch rows/footer would misalign along the bottom
edge whenever the H-bar shows (it DOES show in the 6-track demo: viewport width 1040-168=872 < 6*179).
Reserve the same band on the scene column:
```cpp
    if (scenes != nullptr)
    {
        // Same contentH as the track columns (shared rowBand partition), translated up by the
        // vertical scroll offset so scene launch row N stays glued to pad row N.
        // Reserve the horizontal-scrollbar band (when shown) so the scene column's bottom stop
        // band lines up with the H-bar-occluded track footers instead of overhanging them.
        const int hBar = viewport.isHorizontalScrollBarShown()
                       ? viewport.getScrollBarThickness() : 0;
        scenes->setBounds (sceneArea.getX(),
                           -viewport.getViewPositionY(),
                           sceneArea.getWidth(),
                           contentH - hBar);
    }
```
> Build-agent note: confirm the accessors on the vendored `juce::Viewport` —
> `isHorizontalScrollBarShown()` and `getScrollBarThickness()`. If either is absent under this JUCE
> version, fall back to `viewport.getHeight() - viewport.getViewHeight()` (the visible-area shrink
> caused by the bar) as the reserved band, or hard-reserve `8` (the ForgeLookAndFeel_V4 default from
> `getDefaultScrollbarWidth()`). The alignment requirement is: the scene column's usable height equals
> the track columns' **visible** content height, so the bottom stop band matches.

**(e)** New method — insert immediately after `resized()` ends (~line 191):
```cpp
void SessionView::syncSceneColumnToScroll()
{
    // Message-thread only (called from the viewport's visibleAreaChanged). Cheap: one move.
    if (scenes != nullptr)
        scenes->setTopLeftPosition (scenes->getX(), -viewport.getViewPositionY());
}
```
Fast path on every drag/wheel: `visibleAreaChanged -> onScroll -> this` moves only the scene column's
Y (its X/size are already correct from `resized()`). Not on any hot 25 Hz path; fires only on actual
scroll.

**(f)** `rebuild()` — `[FIX: scroll-alignment — stale scroll offset on edit swap]` after clearing the
columns/scenes and before `resized()`, reset the scroll to the top so a freshly-opened (possibly
shorter) project never appears pre-scrolled with the top rows hidden:
```cpp
    viewport.setViewPosition (0, 0);   // new/rebuilt edit always starts at the top
```
Then, after the existing `resized();` call (line ~97) and before `repaint()`, add:
```cpp
    syncSceneColumnToScroll();   // re-glue the fresh scene column to the (now-reset) scroll offset
```
`setViewPosition(0,0)` fires `visibleAreaChanged -> onScroll`, but `scenes` may be reconstructed after;
the explicit call guarantees the fresh scene column is placed correctly even if a rebuild happens
mid-scroll (e.g. a track added). Safe no-op when `scenes == nullptr`.

### 3.4 `src/main.cpp` — screenshot hook

`[FIX: scroll — screenshot ordering / clamped 520]` In `captureScreenshots()`, AFTER the three
existing `captureView` calls and BEFORE the transport stop, add a scrolled Session pass. **Resize
first, then scroll** so the position clamps against the current (short) content, and scroll to the
true bottom via a proportion rather than a magic 520:
```cpp
    // Prove vertical scroll headlessly: force a SHORT window so the vertical scrollbar appears and
    // the bottom rows are only reachable by scrolling, then scroll to the bottom and snap again.
    setViewMode (ViewMode::Session);
    setSize (1040, 360);                                   // ~5-6 of 16 rows visible => scroll required
    sessionView.resized();                                 // re-layout at 360px FIRST
    sessionView.getViewport().setViewPosition (0, 0);      // top reference
    captureView ("session_top");
    sessionView.getViewport().setViewPositionProportionately (0.0, 1.0);  // unambiguously scroll to bottom
    sessionView.resized();                                 // re-run layout at scrolled offset
    captureView ("session_scrolled");
```
Produces `forge_shot_session_top.png` and `forge_shot_session_scrolled.png` at 360px tall. Comparing
them proves, at one window height: (a) the vertical scrollbar is present (scroll enabled), (b) the
bottom rows + stop-row footer become visible only after scrolling (reachable, not clipped, pads did
not stretch), (c) the pads are the same 46px in both (no stretch), and (d) the pinned scene column's
launch rows stay aligned with the pads at the scrolled offset. `setViewPositionProportionately(0,1)`
scrolls to the true maximum regardless of the exact content/viewport delta, so no order-dependent
magic number.

`getViewport()` (§3.1c) is required — `main.cpp` exposes no viewport accessor today.

---

## 4. Preserved invariants (verified, do NOT regress)
- **R1 / message-thread-only / teardown order.** No new timer, no background-thread access.
  `syncSceneColumnToScroll` and the `onScroll` lambda run only on the message thread
  (`visibleAreaChanged` is message-thread). The strict `~SessionView` / `setEdit(nullptr)` order
  (`stopTimer` FIRST) is untouched; the `ScrollingViewport` member destructs normally after children
  clear. `getViewport()` is a UI-geometry accessor and is kept out of any poll path.
- **Row alignment under scroll (REFUTED attack — confirmed safe).** Both `TrackColumnComponent::resized`
  and `SceneColumnComponent::resized` compute `midH = contentH - headerH - stopRowH` and call the
  identical integer `SessionLayout::rowBand`. Under this edit both use `contentH = columnH` (844)…
  except the scene column now subtracts the H-bar band `hBar` from its total height so its **visible**
  bottom matches the occluded track footers. **Alignment requirement:** the scene column and track
  columns must share the same `midH` **partition origin and row pitch**. Reserving `hBar` only trims
  the very bottom stop-band region, which is below the last pad row; the 16 pad rows themselves still
  partition an identical `midH` because the header/pad region is unaffected by the bottom trim.
  Pad clicks carry a baked-in `sceneIndex` captured at construction (index-based, not
  coordinate-based), so scroll offset cannot mis-route a hit.

  > Build-agent verification step: after implementing, confirm in `session_scrolled.png` that scene
  > launch row 16 aligns with track pad row 16 along the bottom edge. If the `hBar` trim visibly
  > shifts the scene rows relative to the pads, prefer reserving the band **inside the scene column's
  > footer only** (shrink the stop-row band, not the pad partition) so the 16 pad rows keep an
  > identical `rowBand` on both sides.

---

## 5. Residual risks (accepted for MVP)
- Enabling the vertical scrollbar steals ~8–17px of width, so `contentW`'s `jmax` floor drops slightly
  and at few tracks a vertical bar can induce a horizontal one. Cosmetic; grid content unaffected. If
  undesirable later, pass `allowVerticalScrollingWithoutScrollbar` — out of scope here.
- At a TALL window (`contentH 844 < window`), `getViewPositionY()` is forced to 0, the scene offset is
  0, nothing moves — correct.

---

## 6. Open question for the human
- **H-scrollbar band reservation** — the `hBar` subtraction on the scene column (§3.3d) assumes the
  vendored `juce::Viewport` exposes `isHorizontalScrollBarShown()` / `getScrollBarThickness()`. If a
  build agent finds those absent, confirm the fallback (visible-area delta, or hard-reserve 8px) is
  acceptable, and confirm the alignment verification in §4 passes. Alternatively, decide the bottom
  ~8px track-footer occlusion is acceptable and drop the `hBar` reservation entirely (simplest, but
  the scene column's bottom stop band would overhang the H-bar by ~8px).
  **RESOLVED:** the `hBar` accessors are VERIFIED PRESENT in the vendored JUCE
  (`isHorizontalScrollBarShown()`, `getScrollBarThickness()`) and used directly — NO fallback needed.
  Also confirmed a post-implementation fix: `resized()` uses `columnHolder.setSize(...)` (not
  `setBounds(0, 0, ...)`) so a relayout while scrolled no longer resets the scroll to the top —
  verified via `--screenshot` (the `session_top` / `session_scrolled` pair).
