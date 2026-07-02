# W03 — Automation lanes · MIDI-clock out · async LUFS · live cross-surface refresh

> Wave record, 2026-07-01. Baseline `cede941`. Four engine/UI features + an INTERFACE.md rewrite, built by a
> tiered multi-agent wave and proven by **three new headless gates** (`--selftest-automation`,
> `--selftest-sync`, `--selftest-livesync`) on an **eleven-gate floor**. Adversarial QC confirmed 9 findings
> (1 blocker) — all fixed — and the sync gate's teardown exposed a **latent product UAF** in the mixer's
> master meter that had been waiting for any user to change audio devices with the mixer open.

## Scope + constraints

Maintainer constraints for this and future waves: **no physical MIDI hardware and no manual testing** — every
shipped feature must be provable headlessly. That parked the Launchpad smoke test and APC40 driver
indefinitely, and selected this wave's scope from the roadmap's headless-provable remainder:

1. **Volume/pan automation lanes** (engine roadmap phase 3's biggest gap),
2. **MIDI-clock out** (the remaining MIDI-input role; **Ableton Link deferred** — see below),
3. **LUFS analysis off the message thread** (the W02 QC minor),
4. **Live cross-surface refresh** (mixer/inspector values update without re-select),
5. **INTERFACE.md rewrite** (Session-first + the new design charter).

**Ableton Link is compiled out and NOT vendored** (verified: `TRACKTION_ENABLE_ABLETON_LINK` defaults 0, no
`ableton/` dir under the engine's third-party modules, `Edit::getAbletonLink()` returns a null-impl whose
every method no-ops). Enabling it means vendoring `github.com/Ableton/link` + asio-standalone plus a license
review — a maintainer dependency decision, defaulted to deferred.

## Process

Same shape as W02, plus model tiering (top-tier models on UI + verification, mid-tier on recipe-driven
implementation, small on docs):

1. **4 parallel source-verify spikes** (automation APIs · Link/clock feasibility · async-LUFS design ·
   refresh design) → **3 adversarial skeptics** re-derived the load-bearing claims. The skeptics earned their
   keep immediately: one refuted the sync recipe's device-conflict assumption and supplied the corrected
   eviction design before any code existed.
2. **6 file-disjoint implementation agents** (no builds, orchestrator-owned shared files) working from the
   verified dossiers.
3. **Orchestrator integration**: `main.cpp` gates + `CMakeLists` + the single build; then the debugging
   session below; then the 11-gate floor.
4. **Adversarial QC**: 4 dimensions (lifetime · threading · UX-correctness · docs), every finding
   independently verified by a default-refute skeptic. 18 agents total: **9 confirmed, 1 refuted**.

## What shipped

### 1. Automation lanes (`engine/AutomationHelpers.h` + `ui/arrange/AutomationLane.{h,cpp}` + ArrangeView)

- **Seam:** header-only `AutomationHelpers` over the track `VolumeAndPanPlugin`'s `volParam`/`panParam`
  curves — add/move/remove/clear points, value units = fader slider position 0..1 (volume) / −1..+1 (pan).
  **Every mutator ends with `updateStream()`** — source-verified: curve activation otherwise waits on a
  deferred 10 ms engine timer (and a first-point listener hole makes even that unreliable), so the synchronous
  rebuild is baked into the seam, not left to callers. `movePoint` is remove+re-add: the engine's own
  `AutomationCurve::movePoint` clamps to neighbour times, which can't express a free drag.
- **UI:** per-track collapsible 46 px lane under each Arrange track (an **A** toggle beside M/S/R; zero layout
  difference while collapsed). Volume|Pan selector; click adds a point, drag moves (clamped to the visible
  window), right-click deletes/clears. Pixel-exact against clips/playhead by construction — same shared
  `TimelineView::timeToX`. The lane registers as the shown parameter's listener and repaints on
  `curveHasChanged`, so external curve changes (mixer fader, MIDI-learn, undo) are visible live.
- **Engine semantics accepted + documented:** with exactly ONE point on a curve, setting the parameter value
  directly (a mixer fader gesture) intentionally MOVES that point — one point and a static value are the same
  statement. The lane makes it visible instead of silent.
- **Persistence is free:** points serialize as POINT children in the plugin's state inside the
  `.tracktionedit`; Edit load rebuilds all streams.
- **Gate `--selftest-automation`:** 2-point falling volume curve (0.8 @ 0 s → 0.2 @ 2 s), static preconditions
  (activation, count, exact interpolation at 1.5 s), then playback with a bounded 10 Hz poll of
  `getCurrentValue()` — early sample ≥ 0.7, late (1.5–2.4 s window) ≤ 0.45. PASS.

### 2. MIDI-clock out (`engine/MidiClockSync.h` + `engine/MidiClockProbe.h` + TransportBar)

- A **Clock** toggle beside Click routes through `MidiClockSync::setSendClockToAll` /
  `isSendingClockAny` (per-device `setSendingClock`, persisted by the engine).
- **Gate `--selftest-sync`** captures the engine's ACTUAL clock bytes with a `MidiOutputDevice` subclass
  whose overridden `sendMessageNow` logs under a lock — downstream of the real graph, generator (24 PPQN),
  and dispatcher, so a hollow pass is impossible. On this box: SPP=1, start=1, **96/96 expected clocks**,
  stop=1. Machines with zero MIDI outs take an honest SKIP-degrade path (property round-trip + no-crash roll).
- The gate leaves the box exactly as found: the engine's evicted device entry is restored, the periodic-scan
  interval restored, and the real device's persisted props (which the probe shares BY NAME) are snapshotted
  and losslessly restored — immediately after the probe's first write AND after its destructor's final
  `saveProps`, so even a killed run can't leave clock stuck on.

### 3. LUFS off the message thread (`services/export/Exporter` + `engine/dsp/LoudnessAnalyzer`)

- The BS.1770-4 analysis now runs **on the export render worker** after the WAV is finalized (provably closed
  before the render job returns) and before completion marshals — the message thread only ever receives a
  finished 4-float result by value under the existing alive-token. The W02 QC invariant in `finishAll`
  (snapshot `onComplete` before `onLoudness`) is preserved verbatim.
- The one new guard: an **abort predicate** checked per 32k-sample chunk inside `analyzeFile`, so
  `~AsyncRender`'s bounded `stopThread(5000)` can interrupt a multi-GB analysis instead of force-killing a
  thread holding an open reader.
- **Gate:** `--selftest-lufs` gained two legs — the file path run on a real worker thread must agree with the
  buffer-fed result within 0.1 LU, and an always-true abort predicate must return the silence sentinel
  promptly. Sync export path unchanged (blocking by design).

### 4. Live cross-surface refresh (MixerView + DetailView)

- The mixer's 28 Hz tick now runs a **structural guard first** (non-aux track count vs strips → rebuild),
  then guarded engine→widget sync per strip: fader/pan/mute/solo/name, master fader, live return strips.
  Guards: per-slider drag brackets (`onDragStart`/`onDragEnd`), text-box keyboard focus, button-press check —
  all writes `dontSendNotification`, steady-state ticks repaint- and allocation-free, zero tick logging.
- DetailView gained a 10 Hz poll (started/stopped in `setClip`, `Label::isBeingEdited` guard on the name,
  edge-triggered timing label).
- **Gate `--selftest-livesync`:** engine-side writes (volume −12 dB, mute, clip gain −6 dB) must appear on
  the mixer fader/mute and the inspector gain slider after one forced sync tick.

### 5. INTERFACE.md — Session-first rewrite

Replaced the superseded arrangement-first 7-phase plan: current-surface inventory (traced to STATUS facts),
the design charter (dark; **clean = organized, not minimal**; a small semantic accent vocabulary as
wayfinding; beat-accurate visuals derived from the engine transport, never free-running timers; a traditional
menu bar as the command index), W03 marked in-flight, the **W04 UX wave** marked planned (menu bar, popouts,
slide-outs, adjustable-section scaling + persistence, scene layout polish, sequence lighting, tempo
indicators, accent system, state-matrix screenshot harness).

## The debugging session: the sync gate's teardown hang → a latent product UAF

The first `--selftest-sync` run hung the app at shutdown (window frozen, one core spinning). Bisecting the
destructor chain with flushed stderr markers walked the hang down to **MixerView's member teardown**, and the
root cause was not the gate: **the master strip's `PeakMeter` held a raw `LevelMeasurer*` into
`EditPlaybackContext::masterLevels` — a measurer that LIVES ON the playback context.** The gate was the first
code path ever to free the playback context while the mixer was alive; the meter's destructor then called
`removeClient` on freed memory and spun in a corrupted lock. The same freed-context state is reachable in the
real app (`TransportControl::restartAllTransports` with `clearDevices` — device changes), so this was a
latent user-facing crash, found only because the gate exercised a state no UI path had yet.

**Fix (upgraded during QC):** `PeakMeter` holds its source as a `juce::WeakReference<te::LevelMeasurer>`
(the engine declares `LevelMeasurer` weak-referenceable) — the reference nulls itself exactly when the
owner dies, so `detach()` skips `removeClient` precisely when calling it would walk freed memory. One
mechanism covers the master meter (context-owned), the track/return meters (plugin-owned, reclaimable by
the engine's plugin cull after a track delete), and the recycled-address case.

Two more gate bugs fixed on the way: the device-eviction step compared **engine-generated device IDs against
raw JUCE identifiers** (never matched — the engine mints its own IDs like `out_81b0d7ef`; match by NAME), and
the message snapshot raced the dispatcher's 1 ms timer flushing `midiStop` (fixed with a stop → 300 ms yield
→ verify phase; first run captured 96/96 clocks but 0 stops).

## Adversarial QC — 9 confirmed (1 blocker), 1 refuted, all fixed

| # | Sev | Finding | Fix |
|---|---|---|---|
| 1 | blocker | Deleting an aux-return track (its Arrange lane has a full header menu) left `ReturnStrip::pollMeter` dereferencing the freed track at 28 Hz — the structural guard deliberately excludes aux tracks | Re-resolve through `getAuxReturnTrack` before any deref; on mismatch `refresh()` (flips to placeholder) |
| 2 | major | The TransportBar **Clock toggle was wired to nothing** — the gate passed by driving the engine seam directly | Wired `onMidiClockToggled`/`queryMidiClockEnabled` through `MidiClockSync` in `setupControlBar` |
| 3 | major | Track/return meters could `removeClient` on a measurer freed by the engine's 1 Hz plugin cull after a track delete | Subsumed by the `WeakReference` meter source (above) |
| 4 | major | Mixer fader gesture silently MOVES a single-point automation curve; the lane showed a stale curve | Lane is now a `curveHasChanged` listener (live repaint); engine semantics accepted + documented |
| 5 | minor | Sync gate's probe-open-failure degrade path never rolled the transport → could only FAIL | Roll added, mirroring the zero-outs path |
| 6 | minor | Probe `saveProps` (keyed by device NAME) permanently rewrote the real device's persisted props; a killed run left clock stuck ON | Snapshot + lossless restore (immediately after first write and after the probe dtor) |
| 7 | minor | Master-meter context token failed on same-address context reallocation (meter permanently dead) | Subsumed by the `WeakReference` source |
| 8 | minor | Automation handle dragged past the lane's right edge marooned the point beyond reachable time | Drag x clamped to the body width |
| 9 | minor | Overlapping handles: hit-test found the bottom point, paint showed the top one — Delete removed the wrong point | Hit-test iterates in paint z-order (descending) |
| — | refuted | "Delete+add within one poll tick defeats the count-only structural guard" | Skeptic showed the scenario cannot occur within one message-thread tick |

INTERFACE.md tense drift (planned visuals stated as present) also confirmed + fixed.

## Verified

Clean MSVC Debug build (0 errors / 0 warnings). **All ELEVEN selftests PASS** on the final binary —
`--selftest`, `-record`, `-session`, `-midi`, `-midilearn`, `-midiinput`, `-controlsurface`, `-lufs`,
**`-automation`**, **`-sync`**, **`-livesync`** — each in its own process with clean self-exit; `--screenshot`
renders 5 PNGs (Arrange shows the A toggles + Clock button; collapsed layout otherwise pixel-consistent).

## Deferred follow-ups

- **Ableton Link** — vendoring decision (library + asio + license review). The engine wrapper is ready.
- **Expanded-automation-lane visual proof** — no public expand seam yet; belongs to W04's state-matrix
  screenshot harness.
- **Aux-send knobs + return insert rows are not live-synced** (guard pattern extends later; deliberate scope).
- **Export progress panel sits at ~100% during a long master analysis** (honest "still working"; a caption
  would touch UI from the worker path — skipped as scope-creep).
- **W04 — the UX wave** (next): menu bar, popouts/slide-outs, section scaling + persistence, scene layout
  polish, sequence lighting, tempo indicators, semantic accents, state-matrix screenshots.
