# W06 — the hands-on wave, part 1: control bar & HUD (view buttons · folder icon · free-trigger · tempo popup · Exit · splash)

> Wave record, 2026-07-02. Baseline `7f03974`. The first build wave off the maintainer's first hands-on
> session with the W05 build. 15 notes → 7 source-verified investigation agents → an adversarially-verified
> 5-wave plan (locked decisions: +session = scene · self-rendered CC0 samples · Session/Arrange stay
> separate) → **this wave = plan Wave 1**. One new gate (`--selftest-taptempo`) on a **seventeen-gate
> floor**; adversarial QC confirmed **2 defects (1 major)** — both fixed.

## Process

The plan was built and adversarially verified first (6 lenses + a completeness critic caught 3 structural
blockers before any code — folded into plan v2). Then Wave 1 ran the standard Forge shape: **5 file-disjoint
implementation agents** (each edited only its territory, self-reviewed for compile-safety, proposed but did
NOT make the `main.cpp`/CMake wiring, did not build) → **orchestrator consolidation** (applied the wiring +
the new gate + the CMake entry, owned the single integration build) → clean build (0 warnings) → the
17-gate floor + screenshots → a **4-dimension adversarial QC** (UI-interaction · free-trigger · tempo-write
· regression, each finding independently skeptic-verified, default-refute) → 2 fixes → floor again.

The agents' own self-reviews caught real integration hazards pre-build (a heterogeneous
`initializer_list` in ControlBar, a `ResizableWindow` content-ownership lifetime trap in the splash, the
`te::TimePosition` strong-type that rejects a literal `0`). One escaped to the integration build (the
`initializer_list` element-type deduction still failed with the mixed `FolderIconButton*`/`TextButton*`
pointers) and was fixed with per-button `addAndMakeVisible` calls.

## What shipped

- **View switch → top-left (1.1)** (`ui/ControlBar.cpp`): the Session/Arrange/Mix/Editor buttons moved from
  the right edge to the left, after the browser button; the transport bar + LCD claim the leftover
  centre/right span (the W04a carve-out intent preserved — the browser-icon shrink even frees left space).
- **Browser → folder icon (1.2)** (`ui/ControlBar.{h,cpp}`): the labeled "Browser" text button became a
  `juce::Path` folder-icon toggle (`FolderIconButton` — the **first vector icon in the codebase**), tinted
  amber when the browser is open. `ControlBar::setBrowserOpen(bool)` is synced from the shell at the open
  edge (`toggleSidebarAnimated`) and the close-settle (`slideStep`); the click callback is unchanged.
- **Free-trigger launch (1.3)** (`services/files/ProjectSession.{h,cpp}` + `ui/transport/TransportBar.{h,cpp}`):
  a launch-quantization `ComboBox` (None / 8 Bars / … / 1 Bar / 1/2 / …) over the Edit-level global
  `Edit::getLaunchQuantisation().type`. **`LaunchQType::none` = free trigger** (no bar-snap) for
  effects/jamming; default is 1 bar. The engine already implements the behavior — this wave only exposes it
  as a seam (`set/getGlobalLaunchQuantisation`) + a selector. Proven by the new `launchQRoundTrip` leg in
  `--selftest-session`.
- **Clickable tempo popup (1.4)** (`ui/transport/LcdDisplay.{h,cpp}` + NEW `TempoPopup.{h,cpp}` + NEW
  `TapTempo.h` + `engine/EngineHelpers.h`): the LCD's tempo zone is now clickable (the rest of the LCD stays
  click-through — `hitTest` returns true only inside the cached `tempoZoneBounds`, cleared when the readout
  isn't painted) and opens a `juce::CallOutBox` (**first in the codebase**) hosting a popup with an editable
  BPM field, ±0.1/±1.0 steppers, and a **tap-tempo** button over a pure, headless-testable `TapTempo`
  estimator. Writes route through `EngineHelpers::setTempoAt` (clamped [20,300]). Gate: **`--selftest-taptempo`**.
- **File ▸ Exit (1.5)** (`ui/menu/ForgeMenuModel.{h,cpp}`): a "Close Program" command (Ctrl+Q) wired to
  `JUCEApplication::systemRequestedQuit`; the `--selftest-menu` File count bumped 9 → 10.
- **Launch splash (1.7)** (NEW `ui/SplashWindow.h`): a cosmetic dark splash shown during startup and dropped
  when the main window is up. **Honestly scoped** (documented in the header): it does NOT mask the ~8 s
  engine construction — `te::Engine` is a `ForgeApplication` member whose ctor enumerates devices before
  `initialise()` runs — and it is **skipped under every `--selftest-*`/`--screenshot` flag** so the headless
  floor never spawns a window.
- **1.6 (Clock button)**: kept in the transport row (the baked-in default — a working W03 feature not worth
  removing on a possibly-incomplete verbal list).

## Adversarial QC — 2 confirmed (1 major), both fixed

| Sev | Finding | Fix |
|---|---|---|
| major | The new launch-quant combo, last in `TransportBar::resized()`'s squeeze chain behind the count-in combo, collapsed to **0 px (unclickable)** across the ~760–848 px window band (down to the 760 px window minimum) | The two combos now **share** the trailing space — split proportionally when squeezed so neither ever vanishes; both reach preferred width once there's room |
| minor | The `arrange_tray` screenshot left the new folder-icon tint stale (the harness sets `browserVisible` directly, bypassing `setBrowserOpen`) | `controlBar.setBrowserOpen(true/false)` added around the direct-set harness path |

The correctness-critical dimensions came back **clean**: the free-trigger enum↔ComboBox index mapping
(0-based enum ↔ 1-based item IDs) round-trips correctly, and the tempo-write clamp + tap math + the
editable-label re-entrancy guard all hold. The skeptics refuted the finders' other raw findings.

## Verified

Clean MSVC Debug build (0 warnings). **All SEVENTEEN selftests PASS** on the final binary
(`--selftest-taptempo` new; `--selftest-session` gained `launchQRoundTrip`; `--selftest-menu` count bumped);
`--screenshot` renders the 9-state matrix.

## Next (the hands-on plan)

Wave 2 (grid interactions: delete clip · +track · +scene · real file drag-drop), then Wave 3 (per-track
mixer strips), Wave 4 (self-rendered instruments + a note-writing demo), Wave 5 (deferred Send-to-Arrangement).
The full plan + the locked decisions live in the maintainer's hands-on wave-plan.
