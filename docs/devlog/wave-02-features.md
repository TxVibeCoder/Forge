# Devlog — Wave 02: MIDI-learn HW routing · Forge-native control surface · offline LUFS

> Three engine-facing feature seams built against source-verified Tracktion facts, then proven headless: the
> deferred **MIDI-learn hardware routing** from Wave 01, a **Forge-native grid control-surface** driver
> (Novation Launchpad first), and an **offline BS.1770-4 LUFS** analyzer on the export render. Baseline before
> the wave: `1eb876d` (Wave 01 shipped + docs refreshed). Companion to [STATUS.md](../STATUS.md) /
> [HANDOFF.md](../HANDOFF.md).

---

## The shape

Wave 02 was a **single-commit** wave (`bb9ef5e`) but ran the same multi-phase agent process the project uses for
anything that can't be runtime-confirmed by inspection: **3 parallel design/source-verify spikes → an adversarial
verification pass (4 skeptics, default-refuted) → 3 parallel file-disjoint implementation agents → orchestrator
integration (`main.cpp` / `CMakeLists.txt` + the 3 new selftest gates) → adversarial QC (3 dimensions →
per-finding skeptic verify)**. The per-CLI packets live in the gitignored `wave-02-cli-prompts/` (local
operational scaffolding — never pushed); this devlog is the committed record.

All three items were roadmap follow-ups (items **2a**, **3**, **4**). The user's session decisions framed the
scope: no controller hardware on hand → **build to the published MIDI spec with headless proofs**; **Option B
(Forge-native)** for the control surface; **LUFS offline-only** (automation lanes + comping were offered for
item 4 but the user chose LUFS); **Launchpad first** (APC40 mkII deferred).

## The three features (one scoped commit — `bb9ef5e`)

### Item 2a — MIDI-learn hardware routing (`src/engine/ForgeUIBehaviour.{h,cpp}`)

The Wave-01 deferral: a real controller knob drove nothing because Forge shipped the default `te::UIBehaviour`,
whose `getLastFocusedEdit()` returns **null**. The engine's native CC→parameter routing (in
`PhysicalMidiInputDevice`'s `controllerParser` → `ParameterControlMappings::getCurrentlyFocusedMappings`) keys
off exactly that focused Edit — so with no focused Edit, no incoming CC ever reached a mapping.

`ForgeUIBehaviour` is a thin `te::UIBehaviour` subclass that returns the app's open Edit as the focused Edit. It
is installed at **Engine construction**; the `ProjectSession` is set into it once `MainComponent` exists and
cleared before teardown (the construction-ordering fix from verification — see below). The Wave-01
`MidiLearn::listenToRow` workaround **stays** (still needed for the learn-mode async trigger).

**Proven by** the new `--selftest-midiinput` gate: it asserts the focused Edit is reported through the behaviour
and that a CC→param bind lands.

**KEY LIMITATION (real-hardware smoke item).** A `VirtualMidiInputDevice` has **no** `controllerParser` — only
`PhysicalMidiInputDevice` does. So a real hardware CC actually driving a param **cannot be proven headlessly**;
the headless gate proves the focused-Edit plumbing, and the physical-CC path is a real-device smoke test.

### Item 3 — Forge-native control surface (`src/engine/GridControlDriver.h` + `LaunchpadDriver.{h,cpp}` + `ControlSurfaceHost.h`)

A device-agnostic grid-controller driver seam (`GridControlDriver`) + a **Novation Launchpad**
(programmer-mode) driver built to the published MIDI spec. **No hardware was on hand** — the byte mapping needs
real-device confirmation.

**Architecture decision — Option B (Forge-native), verified.** Tracktion's `ControlSurface` clip-launch path
forwards to an **UNWIRED** `std::function` (`ExternalControllerManager`'s `launchClip`), so the framework gives
no clip-launch for free. Rather than adopt the framework and wire that gap, the driver calls
`ProjectSession::launchSlot` / `launchScene` / `stopAllSlots` **directly** and pushes LEDs from the existing
`SlotVisualState::toPadFeedback` (the same pad-state model the on-screen grid uses — hardware-ready since the
Session-grid build).

`ControlSurfaceHost` runs a **view-decoupled ~30 Hz message-thread LED poll** (per-pad debounce) and marshals
incoming MIDI-thread pad presses to the message thread via a **lock-free SPSC `juce::AbstractFifo`**. It is
**inert without hardware** — the poll only starts if a device opens.

**Proven by** the new `--selftest-controlsurface` gate end-to-end: a virtual pad-press launches slot `(0,0)`, and
one LED poll emits the exact expected note-on.

**APC40 mkII is NOT built.** Its faders / transport / metering are where Tracktion's `ControlSurface` framework
*does* carry real plumbing, so an APC40 driver is a per-device architecture call for later.

### Item 4 — offline LUFS (`src/engine/dsp/LoudnessAnalyzer.{h,cpp}` + `src/services/export/Exporter.{h,cpp}`)

A self-contained **BS.1770-4** integrated-loudness + true-peak analyzer (K-weighting biquads, 400 ms / 75%-
overlap gating, absolute −70 LUFS + relative −10 LU gates), run on the **rendered WAV after export**. The
measured integrated LUFS is surfaced in the export-done status strip
(e.g. `Exporting foo.wav — done   ·   -14.2 LUFS`).

**Live master LUFS was ruled out (verified).** The read-only `tracktion_engine` submodule exposes **no
post-fader sample tap**: `LevelMeasurer` gives only reduced dB (not samples), and the master-tap node is
internal. JUCE's `AudioDeviceManager` **sums** secondary audio callbacks rather than letting them observe the
engine's output. And integrated loudness is inherently a **whole-program** measurement — so an on-render
analysis is the *correct* tool, not a compromise for a missing live meter.

**Proven by** the new `--selftest-lufs` gate: a mono full-scale 1 kHz sine measures **−3.00 LUFS within ±0.5 LU**.

## The process — spikes → adversarial verify → implement → integrate → QC

**Three source-verify spikes** produced the design claims; **four adversarial verifiers** (default-refuted,
evidence-required) then stress-tested them:

- **Control-surface unwired `launchClip`** — **CONFIRMED.** Tracktion's `ControlSurface` clip-launch forwards to
  an unwired `std::function`; the framework gives no clip-launch for free (⇒ Option B).
- **MIDI-input focused-Edit routing** — **PARTIAL / confirmed with a fix.** The focused-Edit theory held, but
  verification found a real **Engine-vs-`ProjectSession` construction-ordering** problem: the behaviour is
  installed at Engine construction (before `MainComponent`/`ProjectSession` exist), so the session must be set
  into it later and cleared before teardown.
- **LUFS non-mutating master sample tap** — **REFUTED by two independent agents.** There is no non-mutating
  post-fader sample tap in the read-only engine, and JUCE sums secondary callbacks. This is what turned item 4
  into an offline-on-render analyzer.

Then **3 parallel file-disjoint implementation agents** built the seams; the **orchestrator** owned the shared
spine (`main.cpp`, `CMakeLists.txt`, and the three new selftest gates) and the single integration build.

## Key source-verified findings (the load-bearing facts)

- **Tracktion's `ControlSurface` clip-launch is an unwired `std::function`** (`ExternalControllerManager`'s
  `launchClip`) — no free clip-launch ⇒ **Forge-native (Option B)**, driving `ProjectSession` directly.
- **`VirtualMidiInputDevice` has no `controllerParser`** (only `PhysicalMidiInputDevice` does) ⇒ physical-CC
  routing **cannot** be proven headlessly; it is a real-hardware smoke item.
- **No non-mutating post-fader master sample tap** in the read-only `tracktion_engine` submodule
  (`LevelMeasurer` = reduced dB only; master-tap node internal) ⇒ **no live master LUFS** without forking the
  engine.
- **JUCE `AudioDeviceManager` sums secondary audio callbacks** rather than letting them observe the engine's
  output ⇒ a second callback can't be used as a covert sample tap either.

## The nested-comment gotcha — a third time

The [CLAUDE.md](../../CLAUDE.md) nested-`*/`-in-a-doc-comment bug bit for the **third time** this project.
`ControlSurfaceHost.h`'s header comment contained `ClipSlot*/Clip*`, whose `*/` **closed the block comment
early** — the rest parsed as garbage. The single **integration build caught it** (a build-less implementation
agent could not). Same class of failure as `RecordController.h` (W7) and `MarkerBar.h` (Wave 01). The rule
holds: never nest `/* … */` inside `/** … */`; use `//` for inline notes.

## Adversarial QC (3 dimensions → per-finding skeptic verify)

- **Control-surface** dimension — **ZERO findings.**
- **`ForgeUIBehaviour`** dimension — **ZERO findings.**
- **LUFS** dimension — **two findings, both confirmed:**
  1. **`AsyncRender::onLoudness` latent use-after-free — FIXED.** `finishAll` now **snapshots `onComplete`
     before firing `onLoudness`**, so a handle destroyed from inside its own completion callback can't be touched
     again through the loudness callback.
  2. **Synchronous whole-file loudness analysis on the message thread blocks the UI on very large renders —
     documented minor follow-up, NOT fixed.** The async fix would re-introduce the exact lifetime surface item 1
     just cleaned up, so it is deferred rather than swapped in blind.

## Verification (orchestrator's single integration build + run)

- **Clean MSVC Debug build, 0 warnings.**
- **All EIGHT selftests PASS** — `--selftest`, `--selftest-record`, `--selftest-session`, `--selftest-midi`,
  `--selftest-midilearn`, **`--selftest-midiinput`**, **`--selftest-controlsurface`**, **`--selftest-lufs`**.
- **`--screenshot`** renders 5 PNGs.
- A **normal interactive launch is clean** — the control surface constructs inert and logs
  `no Launchpad input found — control surface inactive`.

The new user-visible surfaces (the export-done LUFS readout; **Ctrl+L** MIDI-learn now that focused-Edit routing
is in) still want a **manual GUI smoke pass** — the dev window can't be GUI-driven headlessly here.

## Deferred follow-ups (ticketed, out of Wave-02 scope)

- **Real-hardware smoke test with an actual Launchpad.** The one thing that can't be verified without a device —
  it proves **both** the MIDI-learn physical-CC routing (2a) **and** the control-surface driver's byte mapping /
  LED palette (item 3).
- **APC40 mkII driver.** Its faders / transport / metering are where Tracktion's `ControlSurface` framework adds
  real value — a per-device architecture call.
- **LUFS off the message thread for very large renders** (the QC minor). Optional **live short-term meter** would
  require forking the engine for a sample tap.
- **Manual GUI smoke** of the new user-visible surfaces (the export-done LUFS readout; Ctrl+L MIDI-learn).
- **Still deferred / unchanged:** item **2b** (audio slot-record + its `ClipFades` wiring) remains blocked (no
  audio slot-record path exists); **automation lanes + comping** were not built (the user chose LUFS for item 4).
