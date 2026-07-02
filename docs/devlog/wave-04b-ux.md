# W04b — the UX wave, part 2: tear-offs · slide-outs · timecode zone · tray meter · accent sweep

> Wave record, 2026-07-02. Baseline `ecfd5e1`. The W04 charter's remainder, under Fable's design
> authority: floating tear-off panels, animated slide-outs, the timecode's return as the LCD's
> fourth zone, the shared PeakMeter + a tray meter, the Session-grid tray-follow seam, the
> playhead's move into the clock-colour family, and a window-level screenshot. **One new gate**
> (`--selftest-popout`) on a **fifteen-gate floor**; adversarial QC confirmed **9 distinct defects
> (1 blocker)** — all fixed and re-verified.

## Process

2 source-verify spikes (tear-off window mechanics — skeptic-verified with three recipe corrections
baked in pre-build; animation + window-capture facts) → 4 file-disjoint agents (popout window ·
timecode zone · accent sweep + Session seam · shared meter) + orchestrator shell work in parallel
(animated slide-outs, window capture) → integration (View-menu commands, gate, bindings) → 15-gate
floor + 18-agent adversarial QC → 9 fixes → full floor again.

## What shipped

- **Tear-off panels** (`ui/popout/PopoutWindow.{h,cpp}` + shell wiring): View ▸ **Pop Out Mixer /
  Pop Out Piano Roll** float the live SHELL MEMBERS into their own desktop windows (reparenting —
  the components are never recreated; engine bindings and timers ride along). Close returns them
  home. Ownership is structurally double-delete-proof (`setContentNonOwned` + JUCE's non-owned
  detach path); window destruction is DEFERRED via `callAsync` (the PluginWindow discipline —
  `closeButtonPressed` runs on the window's own stack); normal z-order (a deliberate divergence
  from PluginWindow's always-on-top — a full-size mixer must not occlude the shell); a project
  swap deliberately leaves tear-offs alone (views rebind in place). Keys unconsumed by the popout
  bubble to the shell (space/R/F-keys/B/E work from a second monitor). Selecting Mix while the
  mixer is out fronts the popout; the menu items tick while torn off. Gate: **`--selftest-popout`**
  (tear off both → parentage + live mixer sync while out → the REAL close path → next-turn asserts
  incl. **no ghost overlay before any rescue relayout**).
- **Animated slide-outs**: the B/E region toggles ease (~160 ms) instead of snapping — a dedicated
  timer lerps the width/height SCALARS and re-runs `resized()` per step (the drag-resizer path), so
  the whole layout chain moves together; `ComponentAnimator` was rejected (it fights `resized()`
  for child bounds, and MainComponent's own inherited Timer is load-bearing selftest machinery).
  Mid-flight re-toggles retarget from the current size; programmatic opens route through
  `revealDrawer()`; resizers are inert while sliding; persisted sizes untouched by animation.
- **The timecode LCD zone**: absolute time returns (the deliberate W04a drop) as a width-gated
  fourth zone — "M:SS.mmm" / "H:MM:SS.mmm", clamped ("0:00.000") for negative pre-roll positions,
  secondary styling (textSec — musical time stays the hierarchy's head). Sheds FIRST on narrow
  faces; `LcdState.timecodeText` is the struct's 4th member (a compile-time contract with the
  shell's positional demo-state inits). Four new gate rows. The default launch window widened to
  1200 px so all four zones render from first run.
- **The shared PeakMeter + the tray meter** (`ui/common/PeakMeter.h`): the W03 WeakReference meter
  extracted verbatim from MixerView.cpp (behaviour byte-identical) and consumed twice — the mixer
  strips and a new 10 px meter beside the ChannelTray fader (the track's Edit-owned
  `LevelMeterPlugin` measurer; attach-on-rebind; polled in the visibility-gated 10 Hz tick). The
  tray gate asserts meter ATTACHMENT (`meterSourceSeen` — a meter with no audio reads the floor).
- **Session tray-follow**: `SessionView::onTrackFocusChanged (int index)` — fired on real focus
  change only, index-based (R1: the grid caches no track pointers) — bound in the shell with the
  same pinned-Files guard as Arrange; switching into Session view seeds the tray from
  `getFocusTrackIndex()` (the change-only seam never fires for the default track 0 otherwise).
- **Accent sweep**: the Arrange playhead moved amber → **timeTempo** (it is a clock element),
  brightened with a 1 px dark edge so it stays distinct from the automation curve's neighbouring
  teal at 2 px stroke width. The contract's piano-roll playhead was **SKIPPED-STALE** — verified:
  no piano-roll playhead exists (a future feature, not a colour sweep). Amber = selection only.
- **The window-level screenshot** (`shell_window`): the menu bar is window chrome above the shell
  content, invisible to component snapshots — snapping the top-level window captures bar + shell
  (the OS-native title bar is peer chrome, expected absent). First frame ever showing the whole
  Forge shell: menu bar, transport, the four-zone LCD, the green-breathing grid.

## Adversarial QC — 9 distinct defects (1 blocker), all fixed

| Sev | Finding | Fix |
|---|---|---|
| blocker | Popout restore never re-ran the shell layout after the deferred window reset — the restored view came home VISIBLE at stale popout bounds, topmost, overlaying the whole shell and eating input; the roll also stole keyboard focus into a closed drawer. The gate was structurally blind (its asserts performed the rescue relayout a user never does) | Views come home hidden; the relayout AND the focus grab moved into the deferred lambda (after the pointer clears, so the guards re-own the view); the gate now asserts **noGhostOverlay** before any rescue |
| major | First E press after a clip-selection auto-open BOUNCED the drawer (slide target desynced from direct `drawerVisible` writes) | Settled-state-aware toggle predicate (both regions) + `revealDrawer()` routing all programmatic opens |
| major | F11 / Mix with the mixer torn off rendered a blank centre, nothing fronted the popout, no menu tick showed the state | `setViewMode` fronts the popout; pop-out menu items tick while out |
| major | Every shell shortcut was dead while a popout had focus | `PopoutWindow::onUnhandledKey` bubbles unconsumed keys to the shell (popout-local keys keep priority) |
| minor | A close-in-flight settle step clobbered a concurrent programmatic open — the requested editor never appeared | Subsumed by `revealDrawer()` (retargets the in-flight slide) |
| minor | Resizer bars stayed live during a slide (visually dead drags still rewrote the persisted size) | Resizers hidden while their slide is in flight |
| minor | The tray-follow seam never fires for the grid's default track 0 | Seeded from `getFocusTrackIndex()` on switch into Session view |
| minor | The timecode zone could not render at the 1040 px default launch width | Default launch 1200 px (the four-zone floor is ~1174) |
| minor | The recoloured playhead was near-indistinguishable from the automation curve's teal where they cross | 1 px dark edges + a brightness bump — "the bright clock line" |

## Verified

Clean MSVC Debug build (0 errors / 0 warnings; one integration error caught at compile — the tray
gate's early-error calls vs. the new meter parameter, fixed with a defaulted arg). **All FIFTEEN
selftests PASS** on the final binary; `--screenshot` renders 9 states including `shell_window`.

## Deferred follow-ups (W04c / later)

An empty-centre hint when Mix is somehow reached torn-off · popout window-position persistence ·
scene layout polish (the one W04 charter item not yet addressed) · shared strip-widget extraction
beyond the meter (fader/knob styling still duplicated tray↔mixer) · a piano-roll playhead (new
feature) · the state-matrix harness walking window SIZES (the current matrix varies views/states).
