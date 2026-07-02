# W04a — the UX wave, part 1: LCD · channel tray · menu bar · sequence lighting

> Wave record, 2026-07-01. Baseline `9a28845`. The first slice of the W04 UX charter
> (INTERFACE.md §4), under Fable's design authority: the GarageBand-style transport LCD, the
> left-sidebar channel tray, the traditional menu bar, beat-accurate Session-grid sequence
> lighting, the semantic accent vocabulary's first application, persisted section sizes, and the
> state-matrix screenshot expansion. **Three new gates** (`--selftest-lcd`, `--selftest-menu`,
> `--selftest-tray`) on a **fourteen-gate floor**; a 22-agent adversarial QC confirmed 10 distinct
> defects (1 blocker) — all fixed.

## Process

4-agent spike phase (LCD/lighting transport facts · menu-bar approach · tray/shell groundwork)
with an adversarial skeptic on the count-in mechanics → design contracts frozen by the
orchestrator (who also pre-laid the accent colour IDs in ForgeLookAndFeel.h so no two agents
shared the file) → 4 file-disjoint Fable implementation agents → orchestrator shell integration →
14-gate floor + adversarial QC (4 dimensions × per-finding skeptics) → fixes → full floor again.

## What shipped

### The LCD (`ui/transport/LcdModel.h` + `LcdDisplay.{h,cpp}`; the old TransportBar readout retired)

A GarageBand-style inset screen, centre of the control bar: **bars|beats** (large, the timeTempo
accent), **tempo** with a BPM tag, **key · time-sig** ("C · 4/4" — the engine has a real pitch
sequence; fresh edits read C). During record count-in the whole face becomes one large **digit
with a record-red pulse bar** that peaks on each click; the lead-in half-beat renders a dimmed dot,
never a zero, and the raw (negative) pre-roll bars never surface. All strings come from a **pure
LcdModel** (plain ints/doubles in), so `--selftest-lcd` asserts the whole acceptance table with no
device. Narrow faces shed the key zone, then tempo; the position always survives.

Source-verified count-in mechanics (the wave's make-or-break): the engine pre-rolls
~(N+0.5) beats with `isRecording()` true throughout and the punch point exposed as
`getTimeWhenStarted()`; count-in clicks land on **whole timeline beats**. Two skeptic guards are
load-bearing: the count-in state latches ONLY at a record rising-edge from a **stopped** transport
(a mid-playback `record()` punches in with no pre-roll and a stale start time — an unguarded latch
renders a phantom count-in), and an epsilon inside the floor/ceil keeps arbitrary tempi from
glitching the digit at exact click boundaries.

### The channel tray (`ui/tray/ChannelTray.{h,cpp}` + the multi-modal left sidebar)

The GarageBand/Logic inspector: select a track in Arrange (or click one of its clips — selection
follows to the owning track) and the left sidebar's **Channel** tab shows that track's strip —
colour band, pan, A/B send knobs, compact insert rows (click opens the plugin editor; "+" adds),
a full-height fader, M/S. The **Files | Channel** tabs share the band the Browser owned; track
selection auto-reveals the Channel tab **unless the user explicitly pinned Files** (QC). The
standalone Mix view stays — it is the all-tracks overview; the tray is the one-track focus tool.
Lifetime discipline is the ReturnStrip lesson applied from birth: the bound track is an identity,
re-validated against the live track list before every dereference, self-clearing to the empty
state; the 10 Hz guarded sync poll runs only while visible. NO meter in v1 (the measurer-lifetime
surface stays out; the WeakReference pattern brings one later).

### The menu bar (`ui/menu/ForgeMenuModel.{h,cpp}` + `MainWindow::setMenuBar`)

A traditional top bar — File / Edit / View / Transport / Help — built from one command table
(single source of truth for id, name, shortcut label, placement), shortcut labels displayed beside
items, tick marks read live from query functions, every callback null-guarded. Plain
`MenuBarModel` with display-only shortcut strings; the ApplicationCommandManager refactor was
evaluated and rejected (the shell's `keyPressed` remains the one key owner). The menu shares the
very `std::function`s the toolbar used, so menu and button cannot drift. `--selftest-menu` walks
the bare model (null-safety), pins the tree shape + known shortcut pairs, and fires flag-capturing
callbacks. **Bonus fix:** the spike found `TransportBar::onRecord` had never been wired — the
transport Rec button was a silent no-op since Phase 1; menu item, R key, and button now all route
through one handler.

### Sequence lighting + the accent vocabulary (Session grid)

Playing pads now pulse **playGreen** (peak on the beat, decaying across it), queued pads breathe
**playGreenDim** on a two-beat cycle, recording keeps its red — and **amber now means
selection/focus only**, on the pads AND the scene column. The beat phase is read once per tick
from the same bars|beats chain the LCD uses, inside the existing 25 Hz poll; only animated pads
repaint per tick. The pulse curve is a pure function (`padPulseAlpha`) asserted by the LCD gate's
lighting leg. New colour IDs: `playGreen`, `playGreenDim`, `timeTempo`, `lcdBg`, `lcdFrame`.

### Shell: de-cluttered control bar · persisted sizes · state-matrix screenshots

- **The file-command buttons are GONE from the control bar** (QC blocker fix, and the charter's
  division of labor): New/Open/Save/Save As/Import/Export/Plugins/Audio live in the menu bar; the
  bar keeps the performance surface — Browser toggle, transport, LCD, view switch. This freed the
  ~500 px that let the transport strip and the LCD both fit from the default window width; the
  centre layout now gives the transport first claim and hides the LCD below its floor rather than
  starving buttons.
- **Browser width + drawer height persist** across launches (engine PropertiesFile; clamped on load).
- **Screenshot matrix grew to 8 states**: `arrange_automation` (the W03 lane, expanded via a new
  public `ArrangeView::setAutomationLaneExpanded` seam, showing a real curve), `arrange_tray`
  (Channel tab bound to a track), `lcd_countin` (the digit face via the LCD's demo seam — a real
  count-in needs a capture device). The menu bar itself renders as window chrome ABOVE the shell
  content, which `createComponentSnapshot` does not include — a window-level capture is a W04b
  harness item.

## Adversarial QC — 10 distinct defects confirmed (1 blocker), 2 refuted, all fixed

| Sev | Finding | Fix |
|---|---|---|
| blocker | The LCD's reserved floor starved the transport strip — at the default window only half a Play button rendered | File buttons removed (menu owns them; ~500 px freed) + centre priority inverted (transport first, LCD hides below floor) |
| major | Count-in digit desynced from the audible click whenever the punch wasn't beat-aligned (recording from a mid-beat stop — the common case) | Digit now derives from the CLICK GRID (whole timeline beats): firstClick = ceil(punchBeat − N); six non-aligned gate rows added |
| major | Track selection auto-switched the sidebar off an explicitly chosen Files pane | Files-tab click pins the pane; the auto-reveal respects the pin |
| minor ×3 (one root) | Any Transport menu command mid-count-in wiped the LCD latch via the same-edit `setEdit` resync → the forbidden negative bars rendered | `setEdit` early-returns on a same-edit call (latch preserved) and both paths refresh synchronously |
| minor | The frozen demo face leaked into every snapshot taken after `lcd_countin` | The synchronous refresh in `setEdit` (same fix) |
| minor | The tray's 10 Hz poll ran forever while hidden | `visibilityChanged` gates the timer; re-show resyncs immediately |
| minor | Tray insert rows missed same-count replaces/renames (size-only compare) | Identity+name chain signature |
| minor | Clip selection didn't rebind the tray; deselection never notified — gestures could land on the wrong channel | Clip selection follows to its track (lane highlight too); `clearSelection` fires the null notify |
| minor | LCD shed thresholds didn't match the published contract | Thresholds are the header constants (`keyZoneMinWidth` 210, floor 150), derived from the zone widths |
| minor | The playback report's readout probe was true from construction | Probe now requires a live poll tick to have fed the face |

Refuted (correctly): a tray ABA wrong-track rebind claim and a tray insert-overflow-unreachable
claim. **Deliberate design ruling recorded:** the HH:MM:SS absolute-time readout is GONE with the
old TransportBar label — bars|beats is the musical readout; absolute time returns as a width-gated
LCD zone in W04b (flagged by QC, decided by the design authority, not an oversight).

## Verified

Clean MSVC Debug build (0 errors / 0 warnings; one agent defect caught at compile: the
non-copyable macro's deleted copy ctor suppressed the menu model's implicit default ctor).
**All FOURTEEN selftests PASS** on the final binary; `--screenshot` renders 8 states. The control
bar at 1480 px now shows the full transport, the complete count-in selector, and the LCD with all
three zones.

## Deferred follow-ups (W04b)

Popout/tear-off panels · animated slide-outs · the timecode LCD zone · a window-level screenshot
(the menu bar's visual proof) · tray meter (WeakReference pattern) · Session-grid tray follow
(needs a SessionView selection seam) · shared strip-widget extraction (tray/mixer duplication) ·
scene layout polish · the accent sweep across remaining views (arrange playhead → timeTempo, etc.).
