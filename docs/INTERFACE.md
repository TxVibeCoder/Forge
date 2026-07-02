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
  always be found. *(Planned — see §4.)*

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
| **Control Bar** | Top, docked | File commands, embedded transport, Session/Arrange/Mixer view-switch, Browser/drawer region toggles, Plugins + Export menus. |
| **Transport Bar** | Embedded in Control Bar | Play/stop/record/loop, timecode/bars\|beats, a Click (metronome) toggle, and a count-in selector. |
| **Floating plugin windows** | Non-persistent | Native editor for external VST3/AU plugins, a generated parameter panel for built-ins; auto-close with their Edit. |
| **Export** | Async, off the message thread | WAV mixdown + per-track stems with a progress/cancel panel; the export-done status strip shows the render's offline BS.1770-4 integrated LUFS. |

## 3. In-flight (W03)

Being built this wave — not yet in STATUS.md as shipped:

- **Per-track volume/pan automation lanes in Arrange.** Editable automation curves alongside clips, the first
  step of the deferred "automation" item in the engine roadmap.
- **A MIDI-clock-out toggle in the transport bar.** Lets Forge drive external gear's clock from its own
  transport — a step toward the DIRECTION.md MIDI-clock/Link sync role, scoped to clock-out only.
- **Live cross-surface value refresh.** Today the Mixer and clip Inspector read engine state on `setEdit`/
  selection only (a manual-rebuild model) — a value changed on one surface doesn't reflect on another until
  re-selected. This wave adds live refresh so mixer and inspector values stay in sync across surfaces without
  a re-select.

## 4. Planned (W04 UX wave)

Not yet built. Sequencing within W04 is not fixed by this document.

- **Menu bar.** The traditional File/Edit/View/Track/Transport/Options/Help `MenuBarModel` bar described in
  §1, as the exhaustive discoverable command index, shortcuts shown beside items.
- **Popout / tear-off panels.** Detach a region (e.g. a plugin chain, the piano-roll) into its own window for
  multi-monitor workflows.
- **Slide-out drawers.** Evolve the Browser/Detail regions from fixed collapse-to-zero toggles toward
  animated slide-out panels.
- **Adjustable-section scaling with persisted sizes.** Resizer-bar drags already work for the Browser width
  and drawer height; this adds persistence across launches (today sizes reset each run) and likely broader
  per-section scaling.
- **Scene layout polish.** Refinements to the Session grid's scene-row presentation beyond the shipped
  vertical scroll.
- **Sequence lighting.** Beat-accurate pad-lighting animation on the Session grid (per the dynamic-visuals
  charter in §1), driven by transport/tempo state.
- **Graphic tempo indicators.** Visual tempo/metronome elements beyond the existing Click toggle, using the
  time/tempo accent colour.
- **The semantic accent system.** Formalizing the accent vocabulary in §1 (interactive, record red, play/
  launch green, time/tempo) as LookAndFeel colour IDs applied consistently across all views — today's shipped
  UI uses a single interactive accent plus record-red; the play/launch-green and time/tempo accents are not
  yet distinct.
- **A state-matrix screenshot review harness.** A headless review tool that walks a matrix of UI states
  (view × selection × transport state, etc.) and captures screenshots for systematic visual review, building
  on the existing `--screenshot` mode (which renders each view once, not a state matrix).

---

## Open questions (carried over, unresolved)

- **The Control Bar "Editor" button** — third view, a drawer toggle, or dropped entirely? Unresolved across
  the mockup set; see [HANDOFF.md](HANDOFF.md) open decisions.
- **Mixer placement long-term** — currently a full-window view-switch (Logic/Cubase-style); whether a docked
  bottom/right mixer that coexists with Session/Arrange is ever wanted remains open.
- **Multi-monitor / tear-off priority** and plugin-window strategy beyond the floating windows already
  shipped — deferred to the W04 popout/tear-off work in §4.

---

*Companion docs: [DIRECTION.md](DIRECTION.md) (product identity) · [STATUS.md](STATUS.md) (living roadmap) ·
[HANDOFF.md](HANDOFF.md) (pick-up-cold state) · [ARCHITECTURE.md](ARCHITECTURE.md) (engine/design) ·
[FEATURE_CATALOG.md](FEATURE_CATALOG.md) (feature landscape) · [../mockups/](../mockups/) (to-scale UI
mockups, sheet 00 = the Session grid).*
