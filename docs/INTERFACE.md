# Forge — Interface Plan

*How Forge looks and is laid out. Companion to ARCHITECTURE.md (the engine/design) and
FEATURE_CATALOG.md (the feature landscape). Informed by a study of Ableton Live, Logic,
Pro Tools, Cubase, and Reaper interfaces.*

## Direction

Adopt **Ableton Live's look + interaction model** on an **arrangement-first** DAW. The
**Session clip-grid stays deferred** — what makes Ableton feel clean for recording/arranging
is the single-window discipline, the collapsible regional skeleton, the "one bottom panel,
many modes" detail view, keyboard-driven collapse, hover help, and color as a first-class
attribute — none of which require the clip grid.

**Forward-compatible seam (design now, don't build):** the center is a *view slot* driven by
a `ViewMode` enum — today `{Arrange, Mixer}`, later `{Session, Arrange, Mixer}`. Views render
the same Edit, so a future Session view is "add a third center view + a scenes column"
(Tracktion already ships a clip-launcher), not a re-architecture. Reserve a view-switch
control + hotkeys (e.g. F9 Arrange / F10 *reserved* Session / F11 Mix) rather than overloading
one Tab key.

## Window layout — `ForgeShell` (evolves from today's `MainComponent`)

One resizable `juce::DocumentWindow` → a single shell Component with a fixed regional
skeleton. Nothing spawns OS windows except plugin GUIs (and explicit tear-offs later).
Layout uses **nested `StretchableLayoutManager`s** (one per axis — never one manager for a
2-D grid) with draggable `StretchableLayoutResizerBar`s; collapse = set a region's size to 0
and re-lay-out.

| Region | Location | Nature | Default | Toggle |
|---|---|---|---|---|
| **Control Bar** | top, full width, ~44px | docked, always on | open | — (clusters via View menu) |
| **Browser / Inspector** | left of work zone | collapsible slide-out (Browse + Inspect tabs) | collapsed early | `B` / `I` + edge button, resizable |
| **Center view slot** | center | tabbed view (Arrange / Mixer / Session-later) | open, Arrange | F9 / F11 segmented control |
| **Detail Drawer** | bottom of center | bottom drawer (clip / piano-roll / automation / device chain) | collapsed | `E`, Shift+Tab swaps content, Esc collapses, resizable |
| **Right rack** | right of work zone | reserved (meters / returns / monitor) | hidden (0-width) | — until populated |
| **Status strip** | very bottom, full width, ~24px | docked, always on | open | — |

The work zone is split **Left | Center | Right** (horizontal manager); the Center is split
**Top (arrange) | Bottom (drawer)** (a *separate* vertical manager).

## View model

- **Center** = one view via `ViewMode`. Switching is a *lens change* over the same Edit —
  selection, transport, and mixer values are shared, so Arrange→Mix→Arrange loses no state.
- **Detail Drawer** = independent, selection-driven, with two sub-modes (Shift+Tab):
  *Clip-edit* (piano-roll for MIDI, waveform/warp/fades for audio, automation) vs
  *Device-chain* (the track's horizontal instrument+effects chain). Optional vertical split
  shows notes + device knobs together (fixing Ableton's "can't see both" cost).
- Meaningful combos all work: Arrange+piano-roll (compose), Arrange+device-chain (sound-design
  while arranging), Mixer+device-chain (mix a channel), Mixer+drawer-collapsed (pure faders).

## Non-persistent surfaces

| Surface | Type | Trigger | Phase |
|---|---|---|---|
| Right-click context menu | `PopupMenu::showMenuAsync` | right-click an object | P2 |
| Tooltips + Info/Help hover box | `TooltipWindow` + bottom-left text area | hover dwell | P2 |
| Value popups / dropdowns | `PopupMenu` / inline editable label | click a combo/value | P3 |
| Control callouts (quantize, snap, groove) | `CallOutBox` | click a compact control | P3 |
| Color-swatch palette + "clip color = track color" | popup / `CallOutBox` grid | "Color…" in a menu | P4 |
| Open / Save As / Import | `FileChooser::launchAsync` | file commands | exists |
| Audio device settings | `AudioDeviceSelectorComponent` | Audio button | exists → folds into Preferences |
| Preferences (tabbed) | `DialogWindow` async | Ctrl+, | P6 |
| Export / Render | `DialogWindow` async | Ctrl+Shift+B | P6 |
| Plugin (VST/AU) editor | floating `ResizableWindow` (per the engine's PluginWindow.h) | double-click a device | P5 |
| Tear-off / detached panel | reparent into a `ResizableWindow` | drag-out | P7 (optional) |

## Theme

Dark, low-contrast, accent-driven. One shared `ForgeLookAndFeel` (installed via
`setDefaultLookAndFeel`); **all colors routed through LookAndFeel colour IDs** (zero
hard-coded colors) so theming/high-contrast is one centralized pass.

- Backgrounds in three tints: shell `#1A1C1E`, panels `#232629`, raised `#2D3135`.
- Text: primary `#D6D9DC`, secondary `#8A9095`; hairlines a notch lighter than their panel.
- **One configurable accent** (default warm amber `#E0902F`) for playhead, record-arm,
  selection, focus. **Red reserved** strictly for active recording + clip-clipping.
- Per-track/clip **color is first-class** (saturated header/clip tint over the dark base).
- Typography: one clean sans; ~13px labels, ~11px metadata; **monospaced digits** for
  timecode/tempo so they don't jitter. Compact ~24–28px rows; 150px header, 76px lane.
- **Default to a lean layout** (Browser + Drawer collapsed) — the dense, fully-expanded
  view reads as cluttered, so it's opt-in via toggles, never the default.

## Build order

1. **Shell refactor** (no new features): `MainComponent` → `ForgeShell`; nested layout
   managers + resizer bars; merge title strip + toolbar + TransportBar into one Control Bar;
   keep Status strip; re-seat the existing ArrangeView in the center slot; install
   `ForgeLookAndFeel` (dark) + an app-wide `TooltipWindow`; `ViewMode{Arrange}` wired.
2. **Arrange polish + discoverability**: per-lane inline controls (name, color chip,
   mute/solo/arm, height); right-click context menus; bottom-left Info/Help box; top
   bars|beats ruler + snap/grid selector; central selection state.
3. **Left Browser/Inspector column**: Browse (file/sample/instrument tree, drag-onto-track) +
   Inspect (selection properties); value popups; first `CallOutBox` clusters.
4. **Detail Drawer** (keystone): audio clip editor first, then piano-roll, then automation;
   double-click-to-open, `E`/Esc, Shift+Tab clip/device swap; color-swatch palette.
5. **Mixer view + device chain + plugins**: Mixer as a center view-switch (channel strips);
   device/effect chain as the drawer's Device mode; floating plugin editor windows.
6. **Config + delivery modals**: tabbed Preferences (Ctrl+,, folds in Audio settings);
   Export/Render (Ctrl+Shift+B).
7. **Power-user + Session seam** (optional): tear-off panels / multi-monitor; saved layouts;
   and — only when non-linear launching is wanted — `SessionView` as the third center view.

## Open questions (to confirm)

1. Session view stays deferred (build arrangement-first, reserve only the seam)? *(plan assumes yes)*
2. Mixer placement: a center view-switch (full-window, Logic/Cubase-style) vs a docked
   bottom/right mixer that coexists with the arrange timeline?
3. Accent color: warm amber, or a distinct Forge brand hue? Dark-only for now?
4. Keyboard scheme: single-letter toggles (B/I/E, Shift+Tab) + F9/F11 view-switch — OK on Windows?
5. Multi-monitor / tear-off priority, plugin-window strategy, saved layouts — defer to P7?

> **Note (current code):** the **Detail Drawer (P4) is built for both clip types** — the audio clip
> inspector (`DetailView`) AND the **MIDI piano-roll** (`PianoRollView`: draw/move/resize/delete +
> velocity lane, multi-select, copy/paste), routed by clip type. MIDI tracks are live (a `te::AudioTrack`
> hosts MIDI clips audible via a default 4OSC). See [STATUS.md §2](STATUS.md) + [devlog/midi-build.md](devlog/midi-build.md).
> Still to come on the drawer: automation lanes, and device-chain (Device mode).
