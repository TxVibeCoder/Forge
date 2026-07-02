# Forge — Interface Plan

> **Session-first.** Forge's primary surface is the Session clip grid, played from mouse + keyboard today
> and — one day — from external grid controllers (Launchpad / APC40 mkII) over MIDI. This document describes
> **how the UI looks and is laid out**; it does not set product direction. Read
> **[DIRECTION.md](DIRECTION.md)** first (the authoritative product brief — Session-first, controller-driven),
> then **[STATUS.md](STATUS.md)** for what is actually shipped, then **[HANDOFF.md](HANDOFF.md)** for the
> current state. Companion to [ARCHITECTURE.md](ARCHITECTURE.md) (engine/design) and
> [FEATURE_CATALOG.md](FEATURE_CATALOG.md) (the feature landscape).

*This is a rewrite (W03) of the prior arrangement-first 7-phase plan, which is superseded per
[DIRECTION.md](DIRECTION.md). Everything here reflects only what STATUS.md records as shipped or explicitly
in-flight; anything else is marked planned. No counts, dates, or commit hashes are asserted beyond what those
docs already state.*

---

## 1. UI identity + design charter

Forge's screen surface is **Ableton-style**: single window, a Session clip grid as the primary lens over the
Edit, a secondary linear Arrange view, and a full-window Mixer — all driven by the same underlying project
state, never separate documents.

- **Dark theme, fixed.** No light mode. One shared `ForgeLookAndFeel` installed via
  `setDefaultLookAndFeel`; all colors route through LookAndFeel colour IDs (zero hard-coded colors in views)
  so any future theme pass is one centralized change.
- **"Clean means organized, not minimal."** Forge does not shrink its feature set to fit a sparse screen — it
  organizes full feature density behind structured navigation: menus, submenus, collapsible sidebars, and a
  bottom drawer that swaps modes by selection. A lean *default* layout (secondary regions collapsed) keeps the
  first impression uncluttered, but every feature stays reachable, not hidden behind a minimalist cut.
- **A small semantic accent-colour vocabulary, used as wayfinding.** One colour = one meaning, everywhere it
  appears, applied conservatively (accents mark state, they don't decorate):
  - **Interactive / selected accent** — the default UI accent (currently warm amber): focus, selection,
    active toggle.
  - **Record red** — reserved strictly for active recording and clip-clipping. Never repurposed for generic
    "danger" or "delete" affordances.
  - **Play / launch green** *(planned — today only the interactive accent and record red are distinct; see
    §4)* — playing and queued-to-play clip/scene states in the Session grid, distinct from the interactive
    accent so "this is live" reads at a glance.
  - **A time/tempo accent** *(planned — see §4)* — playhead, tempo/metronome indicators, and other
    transport-clock elements share one colour family so anything tempo-derived is visually grouped.
- **Dynamic, beat-accurate visuals** *(planned — see §4 sequence lighting / tempo indicators)*. Every
  beat-synced element on screen — Session-grid sequence lighting on pads, tempo indicators, metronome flash —
  derives its timing from the **engine transport/tempo**, never from a free-running UI timer. A UI poll may
  *read* the engine's beat/tempo position at a display rate, but the beat boundary itself is always computed
  from transport state, so visuals never drift from what's actually playing.
- **A traditional top menu bar as the discoverable command index.** File / Edit / View / Track / Transport /
  Options / Help via JUCE's `MenuBarModel`, with keyboard shortcuts displayed beside each item. This is the
  exhaustive, discoverable index of everything Forge can do — the Control Bar and context menus remain the
  fast path for the same commands, but the menu bar is the one place a new user (or a rarely-used command) can
  always be found. *(Shipped — W04a; see §4.)*

## 2. Current surface inventory (shipped)

This section describes only what [STATUS.md](STATUS.md) records as built and verified. See STATUS §2 for the
full build record and §3 for the code map.

| Surface | Role | Notes |
|---|---|---|
| **Session grid** | Primary view (default `ViewMode`, **F8**) | Tracks × scenes clip-launch grid on Tracktion's `ClipSlot`/`Scene`/`LaunchHandle`; single-click launches, right-click "Edit clip", double-click opens; keyboard launch; vertical scroll with fixed-height pads so all scene rows are reachable; MIDI slot recording (arm a track, capture an empty slot into a born-audible clip). |
| **Arrange** | Secondary view | Bars\|beats ruler, a denominator-aware snap-division selector, clip drag-to-move with snap, markers, per-lane mute/solo/arm + colour, selection, waveform + playhead. |
| **Mixer** | Full-window view-switch | Channel strips (fader/pan/mute/solo), per-track plugin insert slots (bypass + reorder), aux A/B sends per track + two aux-return strips, a master strip with a post-fader meter. |
| **Browser** | Left region, collapsible | File tree; double-click an audio file to import. |
| **Detail drawer** | Bottom region, selection-driven | Audio clip inspector (name/gain/fades/waveform) for audio clips; a full piano-roll editor (draw/move/resize/delete notes, velocity lane, multi-select, copy/paste) for MIDI clips — routed by clip type. |
| **Menu bar** | Top window chrome (W04a) | Traditional File/Edit/View/Transport/Help via `MenuBarModel`; shortcut labels, live ticks + enablement (incl. Undo/Redo). The exhaustive discoverable command index. |
| **Transport LCD** | Centre of the Control Bar (W04a/b) | GarageBand-style inset screen: bars\|beats / tempo / key·sig + a width-gated timecode zone; an animated 1·2·3·4 count-in digit (record-red pulse) during pre-roll. |
| **Channel tray** | Left sidebar, Arrange (W04a) | Files \| Channel tabs; the selected track's strip (pan / A/B sends / inserts / fader / M-S / meter) without leaving Arrange. Session-grid focus drives it too. |
| **Control Bar** | Top, docked | Embedded transport, Session/Arrange/Mixer view-switch, Browser/drawer region toggles, Plugins + Export menus. (File commands moved to the menu bar in W04a.) |
| **Transport Bar** | Embedded in Control Bar | Play/stop/record/loop, a Click (metronome) toggle, a count-in selector, and a MIDI-Clock-out toggle (W03). |
| **Tear-off windows** | Detached desktop windows (W04b) | View ▸ Pop Out Mixer / Piano Roll float the live views onto their own windows and back; placement persists (W05). |
| **Floating plugin windows** | Non-persistent | Native editor for external VST3/AU plugins, a generated parameter panel for built-ins; auto-close with their Edit. |
| **Export** | Async, off the message thread | WAV mixdown + per-track stems with a progress/cancel panel; the export-done status strip shows the render's offline BS.1770-4 integrated LUFS. |

## 3. Engine/UX features — SHIPPED (W03)

Shipped in W03 (see `devlog/wave-03-features.md`); recorded in STATUS.md §2:

- **Per-track volume/pan automation lanes in Arrange.** Editable automation curves alongside clips (an **A**
  toggle beside M/S/R; add/drag/delete points; live repaint on external curve edits; persisted). Gate:
  `--selftest-automation`. *(Plugin-param lanes remain a future generalization of the same seam.)*
- **A MIDI-clock-out toggle in the transport bar.** Drives external gear's clock from Forge's transport
  (clock-out only). Gate: `--selftest-sync` (wire-byte capture). *(Ableton Link is still deferred — not
  vendored.)*
- **Live cross-surface value refresh.** The Mixer and clip Inspector now reflect engine values changed on
  another surface (MIDI-learn, automation, another view) without a re-select. Gate: `--selftest-livesync`.

## 4. The UX wave — SHIPPED (W04a · W04b · W05)

Everything in this section is **built and gate-proven** (see the wave devlogs: `devlog/wave-04a-ux.md`,
`devlog/wave-04b-ux.md`, `devlog/wave-05-undo.md`). It was the W04 charter; it landed across W04a, W04b, and
the W05 polish sweep. Kept here as the design record — the "still open" tail follows.

- **Menu bar (W04a).** The traditional File/Edit/View/Transport/Help `MenuBarModel` bar from §1 — the
  exhaustive discoverable command index, shortcut labels beside items, live tick marks + enablement, every
  callback null-guarded. The control bar's file buttons moved into it. Gate: `--selftest-menu`.
- **Popout / tear-off panels (W04b).** View ▸ Pop Out Mixer / Pop Out Piano Roll reparent the live shell
  members into their own desktop windows and back (never recreated; deferred close; keys bubble to the shell;
  Mix-while-out fronts the popout). Placement persists across launches (W05). Gate: `--selftest-popout`.
- **Slide-out drawers (W04b).** The Browser/Detail region toggles animate (~160 ms scalar-lerp through
  `resized()`) instead of snapping; all programmatic opens route through `revealDrawer()`.
- **The channel tray (W04a — maintainer request 2026-07-01).** The GarageBand/Logic "inspector": without
  leaving Arrange, a left-sidebar tray shows the SELECTED track's channel controls — fader, pan, mute/solo,
  A/B sends, the insert chain (click opens the plugin editor) + a level meter (W04b). The left sidebar is
  multi-modal (Files | Channel tabs). The standalone Mix view STAYS (all-tracks overview vs. one-track focus).
  Session-grid focus also drives the tray (W04b). Gate: `--selftest-tray`.
- **The LCD (W04a/b — maintainer request 2026-07-01).** A GarageBand-style inset screen, centre of the
  control bar: bars|beats, tempo, key · time-sig, and a fourth width-gated absolute-timecode zone (W04b).
  During record count-in the face becomes an animated **1·2·3·4** digit with a record-red pulse, its beats
  derived from the engine's CLICK GRID (whole timeline beats, never a free-running animation). Legible but
  visually quiet, in the time/tempo accent. Gate: `--selftest-lcd` (pure model).
- **Adjustable-section scaling with persisted sizes (W04a).** Resizer-bar drags on the Browser width + drawer
  height, now persisted across launches via the engine PropertiesFile.
- **Scene layout polish (W05).** Scene-row hover affordance, tooltips naming the right-click stop, a
  full-width "■ STOP ALL", and beat-pulse parity with the pads.
- **Sequence lighting (W04a).** Beat-accurate pad lighting on the Session grid — playing pads pulse
  playGreen on the beat, queued breathe playGreenDim, recording pulses red — inside the existing 25 Hz poll.
- **Graphic tempo indicators (W04a).** The LCD is the concrete form; the Wave-01 native count-in provides the
  audio the countdown animates; the Arrange playhead moved into the time/tempo accent family (W04b).
- **The semantic accent system (W04a).** The accent vocabulary from §1 is now distinct LookAndFeel colour
  IDs applied consistently: **amber = selection/interactive only**, **playGreen/playGreenDim = playing/
  queued**, **recordRed = recording**, **timeTempo = the clock family** (LCD, playhead). One colour, one
  meaning.
- **The state-matrix screenshot harness (W04a/b).** `--screenshot` walks a 9-state matrix (view × selection ×
  transport/drawer state) incl. the window-level `shell_window` (which captures the menu bar — window chrome
  invisible to component snapshots), the headless stand-in for a live visual review.
- **Global Undo/Redo (W05).** Edit ▸ Undo/Redo with live enablement + Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y over the
  Edit's own UndoManager; per-gesture transaction seals; a synchronous cross-surface refresh after every step;
  blocked with a status message while recording. Gate: `--selftest-undo`.

### Still open (UX)

- **A tabbed Preferences panel** (folds in Audio Settings) — the one INTERFACE region not yet built.
- **The Control Bar "Editor" button** — see Open questions below (a Fable design-authority call).
- **A piano-roll playhead** (a new feature — verified none exists today) and a **window-SIZE dimension** for
  the state matrix (the current matrix varies view/state, not window size).
- **A live design/legibility eyeball pass** — everything above is verified via the headless render matrix +
  gates, never a live human GUI pass (a standing maintainer constraint).

---

## Open questions (carried over, unresolved)

- **The Control Bar "Editor" button** — third view, a drawer toggle, or dropped entirely? Unresolved across
  the mockup set; see [HANDOFF.md](HANDOFF.md) open decisions.
- **Mixer placement long-term** — currently a full-window view-switch (Logic/Cubase-style); whether a docked
  bottom/right mixer that coexists with Session/Arrange is ever wanted remains open.
- **Multi-monitor / tear-off priority** — the mixer + piano-roll tear-off windows shipped (W04b, §4); an
  open question remains whether more regions (e.g. a plugin chain) should be independently detachable.

---

*Companion docs: [DIRECTION.md](DIRECTION.md) (product identity) · [STATUS.md](STATUS.md) (living roadmap) ·
[HANDOFF.md](HANDOFF.md) (pick-up-cold state) · [ARCHITECTURE.md](ARCHITECTURE.md) (engine/design) ·
[FEATURE_CATALOG.md](FEATURE_CATALOG.md) (feature landscape) · [../mockups/](../mockups/) (to-scale UI
mockups, sheet 00 = the Session grid).*
