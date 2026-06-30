# Forge — UI Vision Mockups (CAD / DXF)

A to-scale set of the Forge DAW interface, drawn as **DXF** so you can open them in CAD, measure
them, toggle layers, re-theme, and tweak the geometry directly. Grounded in the real code's
dimensions and the project's product direction.

> **Direction:** Forge is a **sample / scene-based, controller-driven DAW** — the **Session clip
> grid is the primary surface** (sheet **00**), the linear Arrange view is secondary, and the grid
> is *played from real hardware grid controllers* (a mapping reference is sheet **09** — that is
> external hardware over MIDI, **not** an app screen). The authoritative brief is
> [`../docs/DIRECTION.md`](../docs/DIRECTION.md).

Generated with **ezdxf** (run in Docker). 1 drawing unit = 1 logical UI pixel.

---

## What's here

```
mockups/
├── 00-session-clip-grid-primary.dxf          THE PRIMARY SURFACE — tracks × scenes of launchable clips
├── 01-arrange-default-lean.dxf               the (secondary) linear arrange view
├── 02-compose-browser-piano-roll.dxf
├── 03-audio-clip-inspector-drawer.dxf
├── 04-mixer-view.dxf
├── 05-recording.dxf
├── 06-device-chain-plugin-window.dxf
├── 07-anatomy-theme-spec.dxf                 dimensioned spec + palette + CAD layer key
├── 08-overlays-modals.dxf
├── 09-controllers-hardware-mapping-reference.dxf   REFERENCE — physical Launchpad/APC40 → engine (not a screen)
├── forge-ui-storyboard.dxf                   all sheets tiled in one drawing (overview)
├── preview/*.png                             a PNG of each sheet + a contact sheet
└── src/                                       the generator (edit + re-run to tweak)
```

Start with **`preview/forge-ui-storyboard.png`** for the at-a-glance set, then open the DXF of
whichever sheet you want to mark up.

---

## Design direction (what you're looking at)

- **Sample / scene-based, controller-driven.** The **Session clip grid** (sheet 00) is the primary
  surface — tracks across, scenes down, every cell a launchable clip with launch / queue / stop
  states. The linear **Arrange** timeline (sheet 01) is a *secondary* view via the
  `Session ∣ Arrange ∣ Mix` switch. The grid is meant to be played from **external grid
  controllers** (Launchpad / APC40 mkII — sheet 09); the *same pad-colour/state model* drives both
  the on-screen grid and the hardware LEDs.
- **Ableton-style shell.** Single window, collapsible regional skeleton, one bottom "drawer" that
  swaps modes (piano-roll / inspector / device chain), colour as a first-class attribute.
- **Dark + warm amber.** Three background tints (`shell #1A1C1E` / `panel #232629` /
  `raised #2D3135`), one configurable accent (amber `#E0902F`) for playhead · arm · selection ·
  launch, and **red `#E24B4A` reserved strictly for active recording / clipping**.
- **Faithful metrics.** Control bar 46px · status strip 24px · track lane 76px (header 150px) ·
  mixer strip 92px (master 96px) · piano-roll keybed 28px × 12px/key · inspector rows 24px. These
  trace `ForgeShell` / `ArrangeView` / `MixerView` / `PianoRollView` / `DetailView`.

---

## Opening & tweaking in CAD

**Layers mirror the theme.** Every Forge palette colour is a CAD layer carrying that colour as its
true-colour (e.g. `UI-ACCENT`, `UI-PANEL-BG`, `UI-WAVEFORM`, `UI-RECORD`, `UI-MIDI-NOTE`,
`FORGE-DIM`, `FORGE-NOTE`). So you can:

- **Freeze** the `*-BG` fill layers → drop to a clean wireframe for editing geometry.
- **Freeze** `FORGE-NOTE` / `FORGE-DIM` → hide the call-outs and dimensions.
- **Edit one layer's colour** → re-theme the whole UI at once (mirrors Forge's "all colours via
  LookAndFeel colour IDs" rule).

See sheet **07 — Anatomy** for the full palette → layer table and dimensioned components.

**To regenerate after edits to the source**, from `mockups/src/`:

```bash
# one-time: build the tiny render image (ezdxf + matplotlib + fonts)
docker build -t forge-dxf:latest .
# render all sheets + previews into ./out
docker run --rm -v "$PWD":/work forge-dxf:latest python build.py
# or a single sheet, e.g. the Session grid:  python build.py 00
```

Layout/state logic lives in `screens.py`; the drawing toolkit + palette/layers in `forge_ui.py`;
the spec sheet in `anatomy.py`; the overlays in `overlays.py`. `src/out/` is transient (gitignored).

---

## The sheets

| # | Sheet | Shows | Build state |
|---|---|---|---|
| **00** | **Session — Clip Grid (primary)** | The **primary surface**: tracks × scenes of launchable clips, scene-launch column, launch / queue / stop states, clip-stop row, per-track instrument chips. | Net-new (the next build) |
| **01** | **Arrange — default (lean)** | The **secondary** linear view: control bar, marker lane, ruler + snap, colour-coded lanes (audio waveforms + MIDI note blocks), playhead. | Shipped (markers = vision) |
| **02** | **Compose — Browser + Piano-roll** | Left Browser + a MIDI clip live-edited in the bottom piano-roll drawer (keybed, note grid, velocity lane). | Shipped (Inspect tab = vision) |
| **03** | **Audio clip — Inspector drawer** | An audio clip selected with the Detail drawer in clip-inspect mode (name / gain / mute / fades / waveform). | Shipped |
| **04** | **Mixer view** | Channel strips (pan, insert chain w/ bypass + reorder, fader, meter, M/S), a Returns group, master + LUFS. | Shipped (sends/returns + LUFS = vision) |
| **05** | **Recording** | Mid-take: red transport, armed track, red take growing under the playhead. The red-reserved rule. | Shipped |
| **06** | **Device chain + plugin window** | Drawer in Device mode — instrument+effects chain (4OSC → EQ → Comp → Reverb) + a floating editor. | Shipped engine; device-chain *drawer* = vision |
| **07** | **Anatomy & theme spec** | Dimensioned components · palette → CAD-layer table · the drawing-layer key · glyph legend. | n/a (spec) |
| **08** | **Overlays & modals** | Context menu · snap dropdown · colour-swatch palette · tabbed Preferences. | Context menu/snap shipped; Preferences = vision |
| **09** | **Controllers — hardware mapping (reference)** | How **physical** grid controllers (Launchpad + APC40 mkII) drive the clip grid over MIDI — pad-colour/state legend + the `ControlSurface` API mapping. **Reference, not an app screen; external hardware.** | Reference (hardware integration = "one day") |

---

## Vision vs shipped

Drawn as the envisioned product and flagged on the sheets / in the table above: the **Session grid
itself** (the next build), **marker lane**, **automation lanes**, **sends + return buses**, **LUFS
metering**, **device-chain drawer mode**, **Browser "Inspect" tab**, **tabbed Preferences**, and the
**controller integration** (real hardware over MIDI — a "hope to one day connect" capability; the
grid is fully playable with mouse + keyboard without it). Everything else reflects surfaces that
exist in the current build.

> Mark anything up in CAD and send it back — the geometry is fully parametric in `src/screens.py`,
> so changes (sizes, colours, what each sheet shows) are quick to fold in and re-render.
