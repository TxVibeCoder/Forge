# Forge — Session Handoff

> Pick-up-cold handoff. Pairs with **[DIRECTION.md](DIRECTION.md)** (the authoritative product brief) and
> [STATUS.md](STATUS.md) (the living roadmap). Last updated **2026-07-02**, end of the
> **"W06 — the hands-on wave, part 1: control bar & HUD"** wave — the first build wave off the maintainer's
> first hands-on session with the app. ⚠ **W05's owed adversarial-QC dimensions (undo-correctness +
> shell-integration) were NOT run this session — still owed** (W06's own QC did run; see below).

Repo: [github.com/TxVibeCoder/Forge](https://github.com/TxVibeCoder/Forge) (public, AGPLv3) · branch
**`main`**. **W06 shipped in `e670ab5` (code) + a docs commit on the `7f03974` baseline — sanitized.** Last build **clean**
(MSVC Debug, 0 warnings) · **all SEVENTEEN selftests PASS** — the W05 sixteen plus **`--selftest-taptempo`**
— each on the final binary; `--screenshot` renders the **9-state matrix**.
Shipped (the hands-on plan's Wave 1): **view buttons → top-left**, the **Browser button → a `juce::Path`
folder icon** (first vector icon in the repo), a **free-trigger launch-quant selector** (`LaunchQType::none`
over the Edit-level global), a **clickable tempo popup** with ±steppers + **tap-tempo** (first `CallOutBox`
in the repo; pure `TapTempo` estimator; `EngineHelpers::setTempoAt` clamps [20,300]), **File ▸ Exit**, and a
**cosmetic launch splash** (honest: cannot mask the ~8 s engine ctor; skipped under every headless flag).
W06 adversarial QC (4 dimensions × per-finding skeptics) confirmed **2 defects (1 major — the launch-quant
combo collapsed to 0 px in the ~760–848 px window band; fixed by sharing the trailing space)**. Full record
in [devlog/wave-06-handson.md](devlog/wave-06-handson.md). The full hands-on plan (Waves 2–5) + the locked
decisions (+session = scene · self-rendered CC0 · Session/Arrange stay separate) are in the maintainer's
memory ([[forge-handson-wave-plan]]).

**STANDING MAINTAINER CONSTRAINTS (stated this session):** the maintainer has **no physical MIDI hardware**
and **runs no manual tests** — every feature must be headless-provable (selftest gates + `--screenshot`).
Hardware smoke items (Launchpad byte mapping, physical-CC MIDI-learn, APC40) stay parked until hardware
exists; do NOT propose them as next steps. Autonomous multi-agent waves are standing-approved, with model
tiers matched to task complexity. **Fable holds UI/UX design authority** (layout/function, legibility, ease
of use, clean-in-all-states, sequence lighting, tempo visuals, clock accuracy); design freedom is total
EXCEPT: dark theme stays; "clean means organized, not minimal" (never cut features for sparseness —
structure them); accent colours are a small semantic wayfinding vocabulary (one colour = one meaning); and a
traditional top menu bar is a standing request. The full charter is in [INTERFACE.md](INTERFACE.md) §1.

---

## ⚠ READ THIS FIRST — what Forge is now

**Forge is a sample / scene-based, controller-driven DAW.** The **primary surface is an Ableton-style
Session clip grid** (tracks × scenes of launchable clips), meant to be played from real **grid controllers**
(Novation Launchpad, Akai APC40 mkII). The linear **Arrange** timeline is a **secondary** view.

This was a **direction reset** (a recent prior session). Everything built before it was *arrangement-first* — that
work is **not wasted** (clips, the 4OSC instrument, the piano-roll, the mixer, plugin hosting all become
building blocks that live *inside* slots and scenes), but the **primary identity and next build have
changed**. The authoritative brief is **[DIRECTION.md](DIRECTION.md)** — read it before planning anything.

**The controllers are EXTERNAL hardware** Forge connects to over MIDI — Forge does **not** draw a controller
on screen. The only on-screen surface is the Session grid. Hardware integration is a "hope to one day
connect" goal, **not an MVP gate**: the grid is fully playable with mouse + keyboard.

---

## What this session did — W05: global Undo/Redo + the polish sweep

Landed in **`5e5dcf2` (code)** + a docs commit on the `7034955` baseline. Process: 2 source-verify
dossiers (undo mechanics · polish inventory, both with cited line evidence) → 2 parallel file-disjoint
agents (scene polish · strip widgets) + orchestrator-serial main.cpp work (the whole undo track + the two
main.cpp polish items) → integration → **16-gate floor** → adversarial QC (**LIMIT-INTERRUPTED** — the
session's agent limit hit mid-wave; 2 findings recovered from the transcript and fixed by hand; two
dimensions owed). Full record: [devlog/wave-05-undo.md](devlog/wave-05-undo.md).

- **Global Undo/Redo** (`main.cpp` + `ui/menu/ForgeMenuModel.{h,cpp}`): **Edit ▸ Undo / Redo** with live
  enablement (a new `enabledWhen` command-table column over `canUndo`/`canRedo`) + Ctrl+Z / Ctrl+Shift+Z /
  Ctrl+Y. A thin shell over the Edit's own `UndoManager`:
  - the five `onEditMutated` hooks (arrange/detail/piano-roll/session/markers) seal a transaction eagerly
    (`beginNewTransaction`) on top of the engine's 350 ms auto-seal → per-gesture undo units;
  - **`Edit::undo()`/`redo()`**, never the raw UM (keeps the engine's selection refresh);
  - **undo is a no-op with a status message WHILE RECORDING** — record-arm targets sit ON the undo stack
    and `removeTarget` fails while recording, so an undo mid-take would silently retarget the capture;
  - undo fires no `onEditMutated` and no view listens to the state tree, so the shell **saves first**
    (disk must never stay newer than memory) then SYNCHRONOUSLY fans the refresh out: arrange rebuild,
    session rebuild, mixer structural refresh, tray, markers — no timer can interleave a stale deref;
  - a **detached-clip guard**: a redo-of-delete leaves the piano-roll holding a live but PARENTLESS
    `MidiClip::Ptr` (edits would write to a dead state tree); the fan-out closes the roll back to Detail;
  - `ensureScenes` stays deliberately OFF the stack (inhibitor + `clearUndoHistory` in `ProjectSession`).
  - Gate: **`--selftest-undo`** — create/delete/undo/redo round-trip + a note-level leg, with
    `canUndo`/`canRedo` transition asserts from a `clearUndoHistory` baseline.
- **Scene-column polish** (the last W04 charter item): hover lifts rows to `raisedBg` (child-inclusive —
  the fill holds over the ▶ button), tooltips name the hidden right-click stop (which now also works OVER
  the button), full-width **"■ STOP ALL"**, and the queued/playing ring beat-pulses in parity with the
  pads (change-gated + no-pulse sentinel, so static rows stay repaint-free).
- **Strip-widget extraction** (`ui/common/StripWidgets.h`, NEW): tray↔mixer fader/knob/send styling +
  range constants + `busLetter` once, in `forge::strip` — style-only free helpers (no `addAndMakeVisible`
  inside), header-only (no CMake edit), zero behaviour change.
- **The empty-centre hint** (W04b deferral): tearing the mixer off WHILE IN Mix view now paints an
  explanation instead of a blank centre (W04b only covered selecting Mix while already torn off).
- **Popout placement persistence** (W04b deferral): `getWindowStateAsString` per window
  (`forgeMixerPopoutState`/`forgePianoRollPopoutState`), saved at restore-time AND shell teardown,
  restored on tear-off. Saved while the window is still alive — restore defers destruction.
- **QC — LIMIT-INTERRUPTED:** of three planned dimensions only **polish-regressions** ran; its 2 findings
  were recovered from the workflow transcript, self-adjudicated, and fixed (the hint's raw em-dash through
  `juce::String`'s ASCII-only `char*` ctor rendered mojibake; the scene-row hover fill dropped out over
  the ▶ launch button). **undo-correctness + shell-integration NEVER RAN** — owed (see What's next).

> Prior wave (W04b, `cc27300`, PUSHED — same session): **tear-off mixer/piano-roll windows**
> (`ui/popout/PopoutWindow.{h,cpp}`; reparented live shell members, never recreated; deferred close; keys
> bubble to the shell; Mix-while-out fronts the popout; gate `--selftest-popout` with its noGhostOverlay
> assert — the QC blocker was a restored view coming home visible at stale bounds, overlaying the shell),
> **animated B/E slide-outs** (scalar-lerp timer through `resized()`; all programmatic opens via
> `revealDrawer()` or the slide target desyncs), **the timecode LCD zone** (width-gated 4th zone; default
> launch width 1200), **the shared PeakMeter + a channel-tray meter**, **Session-grid tray-follow**, and
> **the Arrange playhead → timeTempo**. Full record: [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md).

> Prior wave (W04a, `41e3139`, PUSHED — same session): **the transport LCD** (pure LcdModel; count-in digits
> derive from the engine's CLICK GRID — the punch is never beat-snapped; the count-in latch arms only on a
> record rising-edge from a stopped transport, and `LcdDisplay::setEdit` early-returns on a same-edit call
> or a menu resync wipes the latch mid-pre-roll), **the channel tray** (Files | Channel sidebar tabs;
> per-tick track re-validation; visibility-gated poll), **the menu bar** (one command table; the control
> bar's eight file buttons moved INTO it — the QC blocker fix that also let the transport + LCD fit; the
> transport Rec button was wired to NOTHING since Phase 1, now fixed), **sequence lighting** (playing =
> pulsing playGreen, queued = playGreenDim, amber = selection only), persisted section sizes, the 8-state
> matrix. Gates `--selftest-lcd` / `--selftest-menu` / `--selftest-tray`. Full record:
> [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md).

> Prior session (W03, `ffa494d`, PUSHED): **volume/pan automation lanes** (`--selftest-automation`) ·
> **MIDI-clock out** (`--selftest-sync`, wire-byte capture; Ableton Link deferred — not vendored) · **LUFS on
> the export worker** (abort-guarded) · **live cross-surface refresh** (`--selftest-livesync`) · the
> INTERFACE.md Session-first rewrite · and a **latent product UAF fixed**: mixer meters held raw
> `LevelMeasurer*` into owners that can die (the playback context; cull-able plugins) — all meter sources are
> now `juce::WeakReference<te::LevelMeasurer>`. Key facts that survive: the engine's automation curve
> activation is deferred unless `updateStream()` is called (the seam does it); engine device IDs are NOT juce
> identifiers (match by NAME) and MidiOutputDevice props persist keyed by NAME; a UI seam a gate can't see
> can ship unwired — verify shell wiring explicitly. Full record:
> [devlog/wave-03-features.md](devlog/wave-03-features.md).

> Prior session (W02, `bb9ef5e`, PUSHED): **MIDI-learn HW routing (focused-Edit `ForgeUIBehaviour`,
> `--selftest-midiinput`) · a Forge-native Launchpad control surface (`--selftest-controlsurface`; byte
> mapping still needs a real device) · offline BS.1770-4 LUFS on export (`--selftest-lufs`)**. Key facts that
> survive: a `VirtualMidiInputDevice` has no `controllerParser` (physical-CC drive is hardware-only proof);
> Tracktion's `ControlSurface` clip-launch is an unwired `std::function`, so the driver calls
> `ProjectSession` directly; no non-mutating post-fader master sample tap exists without forking the engine.
> Full record: [devlog/wave-02-features.md](devlog/wave-02-features.md).

> Prior session (Wave 01, `e3b8c7c`, PUSHED): Forge's **first flat parallel multi-CLI wave** — six file-disjoint
> feature CLIs (P1–P6) each committed a scoped commit, then the orchestrator (P7) implemented the two shared
> `ProjectSession` seams, wired everything into the single integration build, and ran adversarial QC. Baseline
> was `6100fb9`. Full record: [devlog/wave-01-features.md](devlog/wave-01-features.md).

> Prior session (`160f6cc`): **W7 — MIDI record into Session clip slots** — a track can be MIDI record-armed and
> an empty slot captured (**Ctrl+Enter** / right-click "Record into slot") straight into a born-audible
> `te::MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`
> ([devlog/midi-record-design.md](devlog/midi-record-design.md)). Before that (`8d15234`): **Session-grid vertical scroll +
> app-wide logging** — the 16-scene grid scrolls Ableton-style with fixed ~46 px pads and a pinned scene column
> that tracks the pads ([devlog/session-scroll-design.md](devlog/session-scroll-design.md)); and an app-wide
> logging + error-handling subsystem (`src/core/Log.*`, ~90 seams instrumented) with logging-at-the-seam a
> standing build principle ([LOGGING.md](LOGGING.md) · [devlog/logging-design.md](devlog/logging-design.md)).
> Before that (`06f3cf6`): **the Session clip-launch grid** — SessionView as the default `ViewMode`, on
> Tracktion's `ClipSlot` / `Scene` / `LaunchHandle`; source-grounded design + file-disjoint build → 5-lens
> adversarial QC → independent fix re-verify → `--selftest-session` + `--screenshot`
> ([devlog/session-design.md](devlog/session-design.md)). Before that: the **direction reset → DIRECTION.md** +
> the to-scale [mockups](../mockups/) (sheet 00 = the Session grid), and the **MIDI MVP (W1–W5) + W6 piano-roll
> polish** ([devlog/midi-build.md](devlog/midi-build.md)) — clips / 4OSC / piano-roll ride inside slots.

---

## What exists today (the building blocks)

Phases 0–4 + startup hardening + MIDI MVP/W6 + **W7 MIDI record into slots** + the **Session clip-launch grid**
(with vertical scroll) + the **logging subsystem** + the **Wave-01 feature seams** + the **W02 engine seams** +
the **W03 features** + the **W04a/W04b UX waves** + **W05 undo/polish**, all shipped, building clean, all
SIXTEEN selftests PASS:

- **Session grid (PRIMARY view)** — tracks × 16 scenes of launchable clips on `ClipSlot` / `Scene` /
  `LaunchHandle`; single-click launches (instant), right-click "Edit clip" (launch-free), double-click opens;
  keyboard arrows/Enter launch; **audible, bar-quantised** launch; pinned scene column + MASTER stop-all;
  25 Hz gated state poll. **Ableton-style vertical scroll** — fixed ~46 px pads, all 16 scene rows reachable,
  the pinned scene column tracks the pads. Default `ViewMode` (**F8**). Details:
  [devlog/session-design.md](devlog/session-design.md) + [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Logging + error-handling (`src/core/Log.*`)** — an app-wide `juce::Logger` sink (file at
  `%APPDATA%\Forge\logs\forge.log`, 1 MiB + `.1` rollover, + stderr echo) with a crash handler and
  `FORGE_LOG_*` macros; ~90 fallible seams instrumented. RT-thread- and poll-safe by rule. Logging fallible
  seams as you build them is a **standing build principle** — [LOGGING.md](LOGGING.md).
- **Project** save/load (`.tracktionedit`), **audio import**, an **arrange timeline** (waveforms, playhead,
  clip drag-to-move, selectable snap grid).
- **Transport** (play/stop/record/loop) and **recording** — verified end-to-end on real hardware
  (`--selftest-record` captures a real take); output-only startup, lazy capture-input open.
- **MIDI** — clips on any track, born audible via a default **4OSC** at chain index 0; a **piano-roll**
  (draw/move/resize/delete, velocity lane, multi-select, copy/paste); **W7 record into Session slots** — MIDI
  record-arm a track, capture an empty slot (**Ctrl+Enter** / right-click "Record into slot") straight into a
  born-audible `MidiClip`; transport-driven (verdict A), proven by `--selftest-midi`. Details:
  [devlog/midi-record-design.md](devlog/midi-record-design.md).
- **Mixer** (strips, plugin inserts w/ bypass+reorder, master + post-fader meter, **A/B aux sends → aux-return
  strips**), **plugin hosting** (built-in + VST3/AU scan + floating editors), **Browser**, **clip Inspector**,
  **WAV export + stems** (now **async, off-thread, with a progress/cancel panel**).
- **Wave 01 additions** — **metronome + count-in** (TransportBar Click toggle + selector), **markers** (a
  timeline marker bar over the arrange view), **MIDI-learn** (a **Ctrl+L** param picker over Tracktion's native
  mapping store), and an automatic **anti-click edge fade** on imported audio.
- **W02 additions** — **MIDI-learn hardware routing** (a focused-Edit `ForgeUIBehaviour` so the engine's native
  CC→param routing reaches an actual Edit; real-hardware CC drive still a smoke item — a virtual device has no
  `controllerParser`), a **Forge-native grid control surface** (device-agnostic driver + a Novation Launchpad
  driver built to the MIDI spec; drives `ProjectSession` directly, pushes LEDs from `SlotVisualState`; inert
  without hardware — byte mapping needs a real device), and **offline LUFS** (a BS.1770-4 integrated-loudness +
  true-peak analyzer run on the export render; the integrated LUFS shows in the export-done status strip).
- **W03 additions** — **volume/pan automation lanes** in Arrange (an **A** toggle per track expands a 46 px
  lane; add/drag/delete points; live repaint on external curve edits; persisted in the `.tracktionedit`),
  **MIDI-clock out** (a TransportBar **Clock** toggle over the `MidiClockSync` seam), **LUFS analysis on the
  export render worker** (the UI never blocks; per-chunk abort guard), **live cross-surface refresh**
  (mixer/inspector reflect engine changes without re-selecting), and **WeakReference-sourced mixer meters**
  (the latent freed-context/plugin-cull UAF class is closed).
- **W04a additions** — **the transport LCD** (bars|beats / tempo / key·sig centre of the control bar; the
  face becomes a beat-locked count-in digit with a record-red pulse during record pre-roll), **the channel
  tray** (Files | Channel left-sidebar tabs; the selected track's pan/sends/inserts/fader/M-S in Arrange —
  clip selection follows to its track), **the traditional menu bar** (File/Edit/View/Transport/Help with
  shortcut labels; the control bar's file buttons moved into it; the dead transport Rec button fixed),
  **sequence lighting** (playing pads pulse playGreen on the beat, queued breathe playGreenDim; amber =
  selection only), **persisted browser/drawer sizes**, and the state-matrix screenshot expansion.
- **W04b additions** — **tear-off windows** (View ▸ Pop Out Mixer / Piano Roll — the live views float on
  their own desktop windows and return on close; shortcuts still reach the shell), **animated B/E
  slide-outs**, **the timecode LCD zone** (fourth zone, width-gated; default launch width 1200), **a
  channel-tray level meter** (via the shared `ui/common/PeakMeter.h`), **Session-grid tray-follow** (grid
  focus binds the tray), and the **window-level `shell_window` screenshot** (menu bar finally captured).
- **W05 additions** — **global Undo/Redo** (Edit ▸ Undo/Redo with live enablement + Ctrl+Z/Ctrl+Shift+Z/
  Ctrl+Y; per-gesture transaction seals; blocked with a status message while recording; a synchronous
  cross-surface refresh fan-out; a piano-roll detached-clip guard), **scene-column polish** (hover /
  tooltips / full-width STOP ALL / beat-pulse parity), **the strip-widget extraction**
  (`ui/common/StripWidgets.h` — tray↔mixer styling once), **the empty-centre hint**, and **popout
  placement persistence**.

Full feature list + roadmap in [STATUS.md](STATUS.md).

---

## What's next (the path forward)

> W05 (`5e5dcf2` + docs) is **committed + PUSHED to `origin/main`.** Hardware smoke tests and manual GUI
> passes are **permanently parked** (see the standing constraints at the top); the path forward is the
> headless-provable roadmap.

1. **⚠ FIRST: the owed W05 QC dimensions.** W05's adversarial QC was cut off by the session agent limit —
   only polish-regressions ran (2 findings recovered + fixed). Run **undo-correctness** (transaction
   granularity across all five mutation hooks; undo interleaved with launches/recording/project swaps;
   the record gate; the detached-clip guard) and **shell-integration** (the refresh fan-out against every
   surface incl. torn-off popouts; menu enablement; key routing with popout focus) as adversarial
   dimensions with per-finding skeptics. The orchestrator's inline self-review found nothing, but that is
   NOT adversarial verification — treat those areas as unverified until this runs.
2. **UX leftovers (small).** A DetailView detached-clip guard (the piano-roll got one in W05; the audio
   inspector edits a live-but-parentless clip as a safe no-op after a redo-of-delete); a piano-roll
   playhead (a NEW feature — verified none exists); a window-SIZE dimension for the state matrix.
3. **Deferred follow-ups (ticketed).** (a) **Ableton Link** — engine wrapper compiled out, library NOT
   vendored; enabling = vendor `github.com/Ableton/link` + asio-standalone + an AGPLv3 license review (a
   maintainer dependency decision). (b) **Aux-send knobs + return-strip insert rows are not live-synced**.
   (c) **Plugin-param automation lanes** (vol/pan shipped; the seam generalizes). (d) **Audio-slot-record
   edge-fade** remains blocked — no audio slot-record path exists. (e) The export progress panel sits at
   ~100 % during a very long master-render loudness analysis (honest but unlabeled).
4. **Carried-over polish** — **comping**; an optional **live short-term LUFS meter** (requires forking the
   engine for a post-fader sample tap); macOS build (no Mac on hand).
5. **Parked until hardware exists** — the Launchpad byte-mapping smoke test, physical-CC MIDI-learn drive,
   and the APC40 mkII driver.

---

## Build, run, verify

```sh
# Full cmake path — winget doesn't refresh PATH in these shells.
& "C:\Program Files\CMake\bin\cmake.exe" --build ".\build" --config Debug

& ".\build\Forge_artefacts\Debug\Forge.exe"                    # the app (opens on the Session grid)
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest         # headless playback check     → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-record  # headless recording check    → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-session # Session-grid audibility gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midi    # MIDI-record-into-slot gate  → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midilearn # MIDI-learn CC→param bind gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-midiinput # focused-Edit HW-routing gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-controlsurface # virtual pad→launch + LED poll → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-lufs    # BS.1770-4 LUFS (buffer + worker-thread + abort legs) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-automation # volume-curve drives playback gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-sync    # MIDI-clock wire-byte capture gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-livesync # cross-surface live-refresh gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-lcd     # LCD model + pad-pulse curve (pure) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-menu    # menu-bar model walk (pure) → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-tray    # channel-tray live-sync gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-popout  # tear-off round-trip gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-undo    # undo/redo round-trip gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --selftest-taptempo # tap-tempo model + tempo-write seam gate → PASS/FAIL
& ".\build\Forge_artefacts\Debug\Forge.exe" --screenshot       # 9-state matrix (incl. shell_window) → %TEMP%\forge_shot_*.png
# Selftests write %TEMP%\forge_phase0_selftest.log.  First clone: git submodule update --init --recursive
```

Regenerate the mockups (needs Docker; the `forge-dxf` image exists on this box):
```sh
cd mockups/src && MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" forge-dxf:latest python build.py
# then copy out/*.dxf → mockups/ and out/*.png → mockups/preview/   (src/out/ is gitignored)
```

---

## ⚠ Gotchas

- **GUI can't be driven headlessly here** — computer-use can't grab the dev-built `Forge.exe` window by
  name. Use **`--screenshot`** to *see* the UI (renders each view to `%TEMP%\forge_shot_*.png`) and a headless
  selftest hook (like `--selftest-session`) to *exercise* it. (Manual passes are off the table per the
  standing constraints — the W04 state-matrix screenshot harness is the planned substitute.)
- **A `LevelMeasurer` can die under a UI meter (W03 — was a latent product UAF).** The master measurer lives
  ON `EditPlaybackContext` (freed by `freePlaybackContext` / device restarts); track/return measurers live on
  a `LevelMeterPlugin` the engine's plugin cull can reclaim after a track delete. Holding a raw
  `LevelMeasurer*` and calling `removeClient` later walks freed memory — the first `--selftest-sync` run spun
  forever in exactly that, in `~MixerView`. **Rule: hold measurer sources as
  `juce::WeakReference<te::LevelMeasurer>`** (the engine declares it weak-referenceable) and skip
  `removeClient` when the weak ref is null. And per the R1 rule, re-resolve cached engine object pointers
  through a seam before dereferencing in any poll (`ReturnStrip::pollMeter` was a deterministic 28 Hz UAF
  after deleting an aux-return track — its Arrange lane has a full header menu including Delete).
- **Engine device IDs are NOT juce identifiers (W03).** `MidiOutputDevice::getDeviceID()` returns an
  engine-minted ID (e.g. `out_81b0d7ef`), never equal to `juce::MidiDeviceInfo::identifier` — match devices by
  NAME. And `MidiOutputDevice` props (`enabled`, `sendMidiClock`) are **persisted keyed by device NAME**, so
  any same-named device object (e.g. a selftest probe) that calls `saveProps` (which `setSendingClock`,
  `closeDevice`, and the dtor all do) rewrites the REAL device's stored state — snapshot + restore around it.
- **A UI seam a gate can't see can ship unwired.** The W03 Clock toggle passed `--selftest-sync` while wired
  to nothing, because the gate drives the engine seam directly. Adversarial QC caught it; when a feature has
  both an engine seam and a UI affordance, verify the SHELL wiring explicitly at consolidation.
- **Single-point automation curves follow direct value sets (engine-intended, W03).** With exactly one point
  on a curve, `setParameterValue` (a mixer fader gesture) MOVES that point — one point and a static value are
  the same statement. Accepted semantics; the automation lane listens for `curveHasChanged` so it's visible.
- **`JUCE_DECLARE_NON_COPYABLE` suppresses the implicit default ctor (W04a).** The macro declares a deleted
  COPY constructor — which is still a user-declared constructor, so the implicit default one vanishes. Any
  class using the macro needs an explicit `ClassName() = default;` (every older Forge class already declares
  a ctor, which is why this never bit before; a build-less agent shipped it, the compiler caught it).
- **Count-in clicks land on WHOLE TIMELINE BEATS; the punch point is NOT beat-snapped (W04a).** Recording
  from a mid-beat stop is the common case, so any count-in UI must derive its display from the CLICK GRID
  (`firstClick = ceil(punchBeat − N)`), never from whole-beat distances to the punch — the distance form
  leads the audible click by up to a full beat. Also: the count-in latch must only arm on a record
  rising-edge from a STOPPED transport, and `LcdDisplay::setEdit` deliberately early-returns on a SAME-edit
  call — the shell uses `controlBar.setEdit` as a generic toggle resync, and an unconditional reseed wiped
  the in-flight latch (and left demo faces frozen into snapshots).
- **The menu bar is window chrome, not shell content (W04a).** `DocumentWindow::setMenuBar` hosts the bar
  ABOVE the content component, so `createComponentSnapshot (getLocalBounds())` on the shell never captures
  it. Its pixels come from the window-level `shell_window` capture (W04b); the OS-native title bar is peer
  chrome outside the component tree — expected absent from every snapshot.
- **A reparented-home view arrives VISIBLE at its old window bounds, topmost (W04b — was the QC blocker).**
  `ResizableWindow::setContent*` made it visible; `addChildComponent` preserves the flag and inserts on
  top. Any tear-off restore must (a) hide the view on reparent, and (b) re-run the shell layout — AND any
  dependent focus grab — on the SAME deferred turn that clears the popout pointer, because the guards in
  `resized()` skip a view whose popout pointer is still live. A gate for this class must assert the
  no-ghost state BEFORE performing any state-driving of its own (the original gate rescued the bug away).
- **Slide/visible-flag desync (W04b).** Any programmatic `drawerVisible = true` must go through
  `revealDrawer()` (it retargets an in-flight close and keeps the slide target in sync) — a direct write
  made the next E toggle bounce the drawer instead of closing it, and a close-in-flight settle step could
  flip the flag back off underneath the open.
- **What is and isn't on the undo stack (W05).** Track **mute/solo are NOT undoable** — the engine binds
  those `CachedValue`s with a null UndoManager (its choice, not Forge's; do not "fix" by re-binding).
  **Record-arm targets ARE on the stack** (`setTarget`/`removeTarget` write through the UM) — which is why
  the shell BLOCKS undo while recording (an undo mid-take would silently retarget the capture), and why an
  undo while merely armed can disarm a track. Undo history is per-Edit and does NOT survive a project swap.
  `ensureScenes` is deliberately off the stack. And undo/redo fires no `onEditMutated` — any new surface
  must be added to the explicit refresh fan-out in `undoOrRedo`, or it shows stale state.
- **`juce::String`'s `char*` ctor is ASCII-only (W05 — the recovered QC finding).** A raw em-dash (or any
  non-ASCII byte) in a `const char*` literal renders mojibake and jasserts every paint under a debugger.
  Use plain ASCII in string literals, or `String::fromUTF8` with escaped bytes (the ClipSlotComponent
  precedent) when the glyph is worth it. MSVC compiles this repo without `/utf-8`, so the bytes reach the
  ctor raw.
- **A component's hover state excludes its children by default (W05).** `isMouseOverOrDragging()` goes
  false while the pointer is over a CHILD (e.g. a row's own launch button), and
  `setRepaintsOnMouseActivity` only fires for the component's OWN enter/exit — a hover fill flickers off
  exactly over the primary click target. Use `isMouseOverOrDragging (true)` + route the child's mouse
  events through the parent (`addMouseListener`) with explicit enter/exit repaints.
- **Build file lock:** a running `Forge.exe` → `LNK1168` on the next build, and it can hold the WASAPI
  device. `Get-Process Forge | Stop-Process -Force` before building or runtime-testing; use a 45–90 s timeout.
- **Docker on this Windows box:** mount with the Windows path or Git Bash mangles it —
  `MSYS_NO_PATHCONV=1 docker run -v "$(pwd -W):/work" …`.
- **MIDI note beats are CONTENT-relative** (beat 0 = clip start at offset 0); always edit `getSequence()`,
  never `getSequenceLooped()`. Slot inserts use the **free** `insertMIDIClip(ClipOwner&, name, TimeRange)`
  (**name BEFORE range**) via `ClipSlot`'s upcast — NOT the AudioTrack member overload the linear path used.
- **Never arm recording synchronously in one blocking callback** — the device-list rebuild is async. The
  **same discipline now also guards the playback selftest**: yield to the loop, `dispatchPendingUpdates`,
  `blockUntilSyncPointChange` before checking (a hot-swapped output device is `isSuspended` until it drains).
- **Nested block comments corrupt doc comments (bit a THIRD time this project).** A `/* … */` block comment
  placed **inside** a `/** … */` doc comment closes the doc comment early — the first `*/` ends it, and everything
  after (up to the real close) leaks into the code stream and corrupts the following declarations. Bit
  `RecordController.h` (W7), `MarkerBar.h` (Wave 01), and now `ControlSurfaceHost.h` (W02 — `ClipSlot*/Clip*` in
  the header comment, whose `*/` closed the block early; the integration build caught it, a build-less agent
  could not). Fixed by not nesting `/* */` inside `/** */`. Use `//` for inline notes inside a doc comment.
- **`VirtualMidiInputDevice` has no `controllerParser` (W02).** Only `PhysicalMidiInputDevice` carries the
  `controllerParser` that routes an incoming CC → `ParameterControlMappings`. A virtual device can't exercise
  that path — so the focused-Edit `ForgeUIBehaviour` HW-routing (item 2a) is proven headlessly only up to "the
  focused Edit is reported + a CC→param bind lands via the seam"; a **real hardware CC actually driving a param
  is a real-hardware smoke item**, not a headless one.
- **No non-mutating post-fader master sample tap without forking the engine (W02).** The read-only
  `tracktion_engine` submodule exposes no way to observe the master output as *samples*: `LevelMeasurer` gives
  only reduced dB, the master-tap node is internal, and JUCE's `AudioDeviceManager` **sums** secondary audio
  callbacks rather than letting one observe the engine's output. So there's **no live master LUFS** — integrated
  loudness is a whole-program measurement done on the export render (offline), which is the correct tool, not a
  workaround.
- **MIDI slot record is transport-driven, NOT launch-driven.** Recording is started by
  `transport.record(false)` over `isRecordingActive()` destinations — **never** `launchSlot` on the record path
  (an empty armed slot has no clip and no `LaunchHandle`, so a launch is a hard no-op). `isSlotRecording` is
  `isSlotMidiArmed(slot) && transport.isRecording()`, NOT "LaunchHandle playing".
- **Slot capture must be slot-ONLY.** Arm the slot's `itemID` (`setTarget(slot.itemID, /*moveToTrack=*/false,
  …)`) and **disarm the track's MIDI target first** — if both a track and a slot are armed, notes
  double-capture into the arrangement as well as the slot. (Audio/MIDI arm are also mutually exclusive per
  track in v1.)
- **Injecting synthetic MIDI headlessly:** after `engine.getDeviceManager().createVirtualMidiDevice(name)`
  (async — **yield first**, then find the device by name via `getMidiInDevices()`), inject notes with
  `dev->handleIncomingMidiMessage(msg, dev->getMPESourceID())` (the **public** override — NOT the protected
  `handleNoteOn/handleNoteOff` keyboard-listener overrides). On teardown call
  `deleteVirtualMidiDevice(*dev)` or the device name leaks (persisted in engine PropertyStorage) and the next
  run's `createVirtualMidiDevice` fails with "Name already in use".
- **SessionView threading (load-bearing):** pads cache NO `te::ClipSlot*`/`Clip*` — only `(track,scene)`
  indices; the 25 Hz poll re-resolves via the **const** `getClipSlot` (never inserts). Any track-list change
  must rebuild the grid before a stale `TrackColumnComponent` derefs its `AudioTrack&` (the QC blocker).
- **Scrolled-viewport relayout (the session-scroll session's bug):** for a `juce::Viewport`, the **viewed component's
  top-left IS the scroll offset** — so in `SessionView::resized()` size the scrolled `columnHolder` with
  `setSize(w, h)`, **never** `setBounds(0, 0, w, h)` (the latter yanks the grid back to the top on any relayout
  while scrolled). The pinned scene column is offset by `-getViewPositionY()` in `syncSceneColumnToScroll()`.
- **CriticalSection nested-lock type:** the logging file sink guards with a `juce::CriticalSection`; to take it
  use **`juce::CriticalSection::ScopedLockType`** (i.e. `ScopedLock`), NOT a bare `juce::ScopedLock` templated
  wrongly — get the member type from the lock. (Never log from the audio/RT thread regardless — see LOGGING.md.)
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake. (And a quoted
  `"C:\Program Files\..."` path in the same command as `Remove-Item` can trip the sandbox guard — split them.)
- **Latest work is committed + PUSHED.** W05 (`5e5dcf2`) + its docs commit are on **`origin/main`**; the
  working tree is **clean** and the pushed set was **sanitized** (only placeholder `C:\Users\…` / `<user>`
  forms in doc text — no real machine paths / identity leaks). Prior history: W04b (`cc27300`), W04a
  (`41e3139`), W03 (`ffa494d`), W02 (`bb9ef5e`), Wave 01 (`e3b8c7c`), all pushed.
  Sanitizing is a live discipline, not
  a formality: a prior wave's CLI caught + scrubbed a stray sibling-project name in a comment before its commit —
  it would have been the first private-project-name leak into the public repo. **Public repo = sanitize before
  every push** (pseudonymous TxVibeCoder — keep the real email / personal `C:\Users\…` paths / prior-project names
  out). `.gitignore` excludes `*.log` / `forge.log*` (the log sink never gets committed) and
  `wave-*-cli-prompts/` (the wave packets embed machine-local paths → stay local). History was rewritten once (a
  prior session) to scrub a stray path; `git-filter-repo` isn't on PATH, so run **`python -m git_filter_repo`** if
  ever needed (it drops the `origin` remote — re-add before pushing). Submodules are clean.

---

## How the work gets done (what's working)

- **Workflow tool with file-disjoint agents** — exclusive file ownership + additive-only interfaces +
  contract-first seams; the orchestrator does the `CMakeLists`/`main.cpp` wiring and the single integration
  build. An earlier session's **Session grid** landed a **clean first-try integration build** this way (2,920
  LOC, 18 files), because every load-bearing engine API was **source-verified before** the fan-out. **W7 this
  session** followed the same pattern — a frozen source-verified design
  ([devlog/midi-record-design.md](devlog/midi-record-design.md)) → file-disjoint build waves (record layer /
  session seam / pad state / session view) → orchestrator integration + the single build.
- **Adversarial verify waves** (independent skeptics, default-refuted, evidence-required) — high ROI for
  anything that can't be runtime-confirmed here. Earlier sessions ran them on the SessionView **design**, the
  **QC** (12 confirmed, 3 refuted), and a **fix re-verify**. **W03's QC** (18 agents, 4 dimensions ×
  per-finding skeptics) confirmed 9 real findings including a deterministic UAF and an unwired UI seam the
  gates could not see, and one pre-build skeptic refuted + corrected the sync recipe's device handling before
  any code existed. They earn their keep.
- **Model tiering (established W03):** match sub-agent models/effort to task complexity — top tier on UI work
  (Fable holds design authority) and on verification/QC (where wrongness is expensive), mid tier on
  recipe-driven C++ against frozen contracts, small models on docs drafting and mechanical sweeps.
- **Log fallible seams as you build them — STANDING BUILD PRINCIPLE (established in the logging session).** Every new feature
  routes its failure paths through `src/core/Log.*` (`FORGE_LOG_ERROR/WARN/INFO/DEBUG`) — **never** on the
  audio/RT thread, **never** per-tick in a poll/paint, autosave only on `save() == false`. The principle +
  cheat-sheet + a new-feature checklist live in [LOGGING.md](LOGGING.md); read it before adding a feature.
- **Multi-CLI parallel waves — the scaled build pattern (established this session).** For a file-disjoint feature
  set, the orchestrator writes per-CLI **handoff packets** in `wave-<N>-cli-prompts/` (gitignored — they embed
  local paths): a `README` control doc + one self-contained `P#-<slug>.md` per CLI (its territory, "take-as-given"
  facts, *propose-don't-edit* for shared files, scoped commits, and **CLIs do NOT build**) + a `P#-consolidation`
  packet for the orchestrator role (owns `main.cpp`/`CMakeLists`/`ProjectSession`; runs the single build + the
  five-selftest floor + adversarial QC + docs + sanitize + push). Full rule: **`CLAUDE.md` → Wave Orchestration
  Rule**. **Wave 01 shipped six features this way** (~35 min of parallel build vs. ~2 h serial); the CLAUDE.md
  nested-comment gotcha caught its own predicted bug at consolidation, and QC caught two integration UAFs no
  single CLI could see. No `.wave-active` sentinel (Forge has no auto-sync) — the load-bearing guard is **scoped
  commits** (`git add -- <paths>` **and** `git commit -- <paths>`, never `-A`).

---

## Open decisions (waiting on you)

- **Session grid layout — RESOLVED + BUILT (vertical scroll).** No longer open: vertical scroll with fixed
  ~46 px pads shipped (a prior session, `8d15234` + pushed); see
  [devlog/session-scroll-design.md](devlog/session-scroll-design.md).
- **Double-click edit gesture** — currently double-click opens a clip AND launches it (first press launches);
  right-click "Edit clip" is the launch-free path. Kept as belt-and-suspenders this session; revisit if the
  double-launch bothers you.
- **The control-bar "Editor" button** — third view, drawer toggle, or drop it? Now a **Fable design-authority
  call** slated for the W04 menu-bar/layout work (preserved as open in INTERFACE.md).
- **`INTERFACE.md` body — RESOLVED (W03):** rewritten Session-first with the design charter + the W04 UX
  charter; the superseded-banner is gone.
- **Ableton Link — vendoring decision.** The engine wrapper is ready but compiled out, and the Link library
  is NOT in the repo. Enabling = vendor `github.com/Ableton/link` (+ asio-standalone) + an AGPLv3
  license-compatibility review + two CMake lines. Default: deferred until the maintainer approves the new
  dependency.
- **Mockup refinements** — likely incoming (Session footer mixer, hard renumber 00→01, geometry tweaks).
- **Controllers — maintainer has NONE (stated 2026-07-01).** Hardware smoke items are parked, not queued; a
  controller in hand would unblock the Launchpad byte-mapping test and the physical-CC MIDI-learn path.

---

## Key references

- **[../CLAUDE.md](../CLAUDE.md)** — the working agreement: tenets, engineering principles, build discipline, the
  Forge gotchas, and the multi-CLI **Wave Orchestration Rule** (auto-loaded by Claude Code; linked here for humans).
- **[DIRECTION.md](DIRECTION.md)** — the authoritative product brief (read first).
- [STATUS.md](STATUS.md) — living roadmap. · [../mockups/](../mockups/) — the UI mockup set (sheet 00 = the target).
- [INTERFACE.md](INTERFACE.md) — the Session-first UI plan + design charter + the W04 UX charter (rewritten in W03).
- [devlog/wave-05-undo.md](devlog/wave-05-undo.md) — **W05: global Undo/Redo + the polish sweep (this session; QC partially owed)**.
- [devlog/wave-04b-ux.md](devlog/wave-04b-ux.md) / [devlog/wave-04a-ux.md](devlog/wave-04a-ux.md) — the W04 UX waves (same session).
- [devlog/wave-03-features.md](devlog/wave-03-features.md) — W03: automation · MIDI-clock out · async LUFS · live refresh (same session).
- [devlog/wave-02-features.md](devlog/wave-02-features.md) — W02: MIDI-learn HW routing · control surface · offline LUFS (prior session).
- [devlog/wave-01-features.md](devlog/wave-01-features.md) — Wave 01: the six parallel feature seams (prior session).
- [devlog/midi-record-design.md](devlog/midi-record-design.md) — the W7 MIDI-slot-record design + the frozen recipe.
- [devlog/session-design.md](devlog/session-design.md) — the Session-grid design + build-wave record (earlier session).
- [devlog/session-scroll-design.md](devlog/session-scroll-design.md) — the vertical-scroll design (prior session).
- [LOGGING.md](LOGGING.md) + [devlog/logging-design.md](devlog/logging-design.md) — the logging principle + subsystem design (prior session).
- [devlog/midi-build.md](devlog/midi-build.md) — the MIDI MVP + W6 build record.
- [devlog/midi-design.md](devlog/midi-design.md) — MIDI design + the original W7 (input-record) plan.
- [devlog/device-recording.md](devlog/device-recording.md) — recording root-cause + device-pairing nuance.
- [ARCHITECTURE.md](ARCHITECTURE.md) · [INTERFACE.md](INTERFACE.md) · [FEATURE_CATALOG.md](FEATURE_CATALOG.md) ·
  [../tests/SELFTEST.md](../tests/SELFTEST.md).
