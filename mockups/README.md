# Forge — UI Vision Mockups (CAD / DXF)

A to‑scale set of the Forge DAW interface in eight states, drawn as **DXF** so you can open
them in CAD, measure them, toggle layers, re‑theme, and tweak the geometry directly. This is
the *envisioned final product* — grounded in the real code's dimensions and the
[INTERFACE.md](../docs/INTERFACE.md) plan, with the roadmap's vision elements drawn in and
clearly flagged.

Generated with **ezdxf** (run in Docker). 1 drawing unit = 1 logical UI pixel.

---

## What's here

```
mockups/
├── 01-arrange-default-lean.dxf        each state, standalone (open one at a time)
├── 02-compose-browser-piano-roll.dxf
├── 03-audio-clip-inspector-drawer.dxf
├── 04-mixer-view.dxf
├── 05-recording.dxf
├── 06-device-chain-plugin-window.dxf
├── 07-anatomy-theme-spec.dxf          dimensioned spec + palette + CAD layer key
├── 08-overlays-modals.dxf
├── forge-ui-storyboard.dxf            all eight tiled in one drawing (overview)
├── preview/*.png                      a PNG of each sheet + a contact sheet
└── src/                               the generator (edit + re‑run to tweak)
```

Start with **`preview/forge-ui-storyboard.png`** for the at‑a‑glance set, then open the DXF
of whichever state you want to mark up.

---

## Design direction (what you're looking at)

- **Ableton‑style, arrangement‑first.** Single window, collapsible regional skeleton, one
  bottom "drawer" that swaps modes, colour as a first‑class attribute. Session clip‑grid stays
  deferred (the seam is reserved via the view‑switch).
- **Dark + warm amber.** Three background tints (`shell #1A1C1E` / `panel #232629` /
  `raised #2D3135`), one configurable accent (amber `#E0902F`) for playhead · arm · selection ·
  focus, and **red `#E24B4A` reserved strictly for active recording / clipping**.
- **Faithful metrics.** Control bar 46px · status strip 24px · track lane 76px (header 150px) ·
  mixer strip 92px (master 96px) · piano‑roll keybed 28px × 12px/key · inspector rows 24px.
  These trace `ForgeShell` / `ArrangeView` / `MixerView` / `PianoRollView` / `DetailView`.

---

## Opening & tweaking in CAD

**Layers mirror the theme.** Every Forge palette colour is a CAD layer carrying that colour as
its true‑colour (e.g. `UI-ACCENT`, `UI-PANEL-BG`, `UI-WAVEFORM`, `UI-RECORD`, `FORGE-DIM`,
`FORGE-NOTE`). So you can:

- **Freeze** the `*-BG` fill layers → drop to a clean wireframe for editing geometry.
- **Freeze** `FORGE-NOTE` / `FORGE-DIM` → hide the call‑outs and dimensions.
- **Edit one layer's colour** → re‑theme the whole UI at once (this mirrors Forge's "all colours
  via LookAndFeel colour IDs" rule — change the layer, change the product).

See sheet **07 — Anatomy** for the full palette → layer table and dimensioned components.

**To regenerate after edits to the source** (or to have me iterate), from `mockups/src/`:

```bash
# one‑time: build the tiny render image (ezdxf + matplotlib + fonts)
docker build -t forge-dxf:latest .
# render all sheets + previews into ./out
docker run --rm -v "$PWD":/work forge-dxf:latest python build.py
# or a single sheet, e.g. the mixer:  python build.py 04
```

Layout/state logic lives in `screens.py`; the drawing toolkit + palette/layers in `forge_ui.py`;
the spec sheet in `anatomy.py`; the overlays in `overlays.py`.

---

## The eight states

| # | State | Shows | Build state |
|---|---|---|---|
| **01** | **Arrange — default (lean)** | The clean default: Browser + Drawer collapsed. Control bar, transport, marker lane, bars\|beats ruler + snap, 6 colour‑coded lanes (audio waveforms + MIDI note blocks), playhead, status strip. | Shipped (markers = vision) |
| **02** | **Compose — Browser + Piano‑roll** | Dense compose layout: left Browser (file tree, Browse/Inspect tabs) + a MIDI clip live‑edited in the bottom piano‑roll drawer (keybed, note grid, velocity lane). | Shipped (Inspect tab = vision) |
| **03** | **Audio clip — Inspector drawer** | An audio clip selected (accent outline) with the Detail drawer in clip‑inspect mode: name / gain / mute / fade‑in / fade‑out / timing + waveform with fade handles. | Shipped |
| **04** | **Mixer view** | Full‑window mixer: 8 channel strips (pan knob, insert chain with bypass + reorder, dB fader, peak meter, M/S), a **Returns** group, and a master strip with LUFS. Bass muted · Vox soloed. | Shipped (sends/returns + LUFS = vision) |
| **05** | **Recording** | Mid‑take: red transport, armed track (R amber + red edge), red take growing under the playhead, red "RECORDING" readout. Demonstrates the red‑reserved rule. | Shipped |
| **06** | **Device chain + plugin window** | Drawer in **Device** mode — the track's instrument+effects chain (4OSC → EQ → Comp → Reverb) as knob‑grid cards with bypass dots, plus a floating Compressor editor with GR meter. | Shipped engine; device‑chain *drawer* = vision |
| **07** | **Anatomy & theme spec** | Reference sheet: palette → CAD‑layer table, the full drawing‑layer key, a dimensioned control bar, track lane, mixer strip, piano‑roll, and a states/glyph legend. | n/a (spec) |
| **08** | **Overlays & modals** | Non‑persistent surfaces: clip right‑click context menu, snap‑division dropdown, colour‑swatch palette call‑out, and a tabbed Preferences dialog (Audio tab). | Context menu/snap shipped; Preferences = vision |

---

## Vision additions (drawn here, not yet built)

These appear in the mockups as the envisioned product and are flagged above / on sheet 07:
**marker lane**, **automation lanes**, **sends + return buses**, **LUFS metering**,
**device‑chain drawer mode**, **Browser "Inspect" tab**, **tabbed Preferences dialog**. Everything
else reflects surfaces that exist in the current build.

> Mark anything up in CAD and send it back — the geometry is fully parametric in `src/screens.py`,
> so changes (sizes, colours, what each state shows) are quick to fold in and re‑render.
