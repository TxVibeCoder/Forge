# Devlog — Wave 01: six parallel feature seams (the first multi-CLI wave)

> Forge's first **flat parallel multi-CLI wave**: six file-disjoint feature CLIs (P1–P6) build against
> contract-first seams, an orchestrator (P7) wires them into the single integration build. Baseline before the
> wave: `6100fb9` (W7 shipped). Companion to [STATUS.md](../STATUS.md) / [HANDOFF.md](../HANDOFF.md).

---

## The shape

Prior Forge features shipped as file-disjoint fan-outs within one CLI. Wave 01 was the first time **six separate
Claude Code CLIs** each owned a disjoint file territory and committed their own scoped commit, with the
orchestrator owning the shared spine (`main.cpp`, `CMakeLists.txt`, `ProjectSession.{h,cpp}`) and the single
build + selftest floor. The per-CLI packets live in the gitignored `wave-01-cli-prompts/` (local operational
scaffolding — never pushed); this devlog is the committed record.

Each CLI **could not build** (one build dir + a WASAPI device lock ⇒ concurrent builds collide), so every CLI
self-reviewed for compile-safety + ran its own adversarial verification against Tracktion source, then wrote a
`P#-results.md` proposing its spine wiring. The orchestrator reconciled those, implemented the two
`ProjectSession` seams the CLIs designed but couldn't own, wired everything, built once, and ran QC.

## The six features (each a scoped commit on `main`)

| Commit | Feature | Territory |
|--------|---------|-----------|
| `096c9bd` | **P1 metronome + count-in** | `engine/Metronome.{h,cpp}` (new) + `ui/transport/TransportBar.*` |
| `1ef4f37` | **P2 MIDI-learn** (CC→param) | `engine/MidiLearn.{h,cpp}` (new) + `engine/PluginHost.*` |
| `c5062a3` | **P3 buses / sends** (aux returns) | `ui/mixer/MixerView.*` |
| `8d0afdf` | **P4 async export** + progress | `services/export/Exporter.*` + `ui/export/ExportProgress.{h,cpp}` (new) |
| `fe1bfcb` | **P5 markers** | `ui/markers/MarkerBar.{h,cpp}` (new) |
| `975846e` | **P6 clip edge-fade** | `engine/ClipFades.{h,cpp}` (new) |

- **P1** — a stateless `Metronome` namespace over the engine's authoritative `Edit::clickTrackEnabled` (per-Edit,
  persisted, OFF by default) + native `Edit::CountIn` (a **global** engine setting; whole-bar count-in tops out
  at 2). Count-in needs **no** `RecordController` change — `transport.record()` pre-rolls `getNumCountInBeats()`
  itself. TransportBar gains a **Click** toggle + a count-in ComboBox, each a `std::function` seam.
- **P2** — a thin `MidiLearn` driver over Tracktion's native `ParameterControlMappings` (one store per Edit,
  **persists on the Edit**, applies on the message thread). `PluginHost::getAutomatableParameters` supplies the
  target picker. The seam forces the learn to complete with no focused Edit (Forge uses the default
  `UIBehaviour`).
- **P3** — per-track **A/B aux-send knobs** + two **aux-return strips** (placeholder → live) in the mixer. An aux
  bus is the Tracktion way: a plain `AudioTrack` hosting an `AuxReturnPlugin`, fed by an `AuxSendPlugin` with a
  matching `busNumber`.
- **P4** — `renderEditToWavAsync` / `renderStemsAsync` run the existing render recipe on a worker thread with
  progress + cancel, returning an `AsyncRender` handle; a small `ExportProgress` panel drives it. **The sync
  functions are preserved.**
- **P5** — a thin `MarkerBar` timeline strip over the shared `TimelineView`; caches only value rows keyed on the
  stable `EditItemID` (never the reassignable marker number).
- **P6** — a `ClipFades` helper applying a 5 ms linear anti-click edge fade to audio clips (idempotent,
  grow-only, MIDI = no-op).

## Consolidation (P7 — the orchestrator)

**Two `ProjectSession` seams** the CLIs designed but don't own, implemented against source-verified engine APIs:

- **Aux seam** (`ensureAuxBus`, `getAuxReturnTrack`, `isAuxReturnTrack`, `getAuxBusName`, `setTrackSendLevel`,
  `getTrackSendLevel`). The load-bearing rule: an aux return is **appended at the END** of the track list
  (`TrackInsertPoint::getEndOfTracks`) so every existing absolute `getAudioTracks()` index stays stable (mixer
  sends + the Session grid address tracks by absolute index). A new `ProjectSession::onTracksChanged` hook fires
  on a real add so the shell rebuilds the SessionView columns / ArrangeView lanes (no stale `TrackColumnComponent`
  deref) and persists. Sends are inserted **post-fader** (just after the `VolumeAndPanPlugin`).
  `AuxSendPlugin`/`AuxReturnPlugin` are registered built-in types, created via `PluginCache::createNewPlugin`.
- **Markers seam** (`addMarker`, `removeMarker`, `moveMarker`, `renameMarker`, `getMarkers`, `jumpTransportTo`,
  private `findMarkerById`). Keyed on `EditItemID`; delete = `Clip::removeFromParent()` (MarkerManager has no
  delete); move = `Clip::setStart(t, false, true)`.
- **P6 call sites**: `ClipFades::applyDefaultEdgeFades(*clip)` in the success branch of `importAudioFile` +
  `importAudioIntoSlot`. (The record path is MIDI-only today; a fade there is a no-op, so it is deferred.)

**`main.cpp` wiring**: P1 metronome seams in `setupControlBar`; P2 `MidiLearn` member + `setActiveEdit`
(ctor/rebind/swapProject) + a **Ctrl+L** track▸plugin▸param picker (`showMidiLearnMenu`); P3
`mixerView.setSession(&session)` + `session.onTracksChanged`; P4 `beginAsyncExport` holding the `AsyncRender`
handle + owning an `ExportProgress` overlay, both export dialogs swapped to the async factories; P5 `MarkerBar`
sharing the shell's `TimelineView`, carved off the top of the arrange region (`setHeaderInset(headerW)` for
ruler-aligned mapping). **CMake**: 5 new sources added.

### Restart recovery (P4)

The P4 CLI was killed by a forced PC restart before writing its `P4-results.md`. Its spine wiring was **derived**
from the `P4-async-export.md` packet + the committed `Exporter.h` async API (the factory returns an already-
`begin()`-called handle; `finishAll` moves `onComplete` out before invoking, so destroying the handle from inside
its own `onComplete` is safe — both verified against `Exporter.cpp` before wiring).

### Integration bug caught at the build (the CLAUDE.md gotcha, again)

The first integration build failed only in `MarkerBar.cpp` with a cascade of `va_list`/`__va_start` errors deep
in a CRT header — the signature of a preprocessor-state corruption earlier in the TU. Root cause: `MarkerBar.h`
line 13 contained `te::MarkerClip*/Clip*` **inside a `/* … */` doc comment** — the `*/` closed the doc comment
early, so the entire class body parsed as garbage and "lost" all its members. This is exactly the
[CLAUDE.md](../../CLAUDE.md) nested-comment gotcha; a build-less CLI cannot catch it. Fixed by breaking the `*/`
(`te::MarkerClip* / Clip*`).

### Adversarial QC (Workflow: 5 dimensions → per-finding skeptic verify)

A fan-out of one review dimension per feature area → per-finding adversarial verify (default-refuted,
evidence-required). **3 CONFIRMED, 0 refuted** (two distinct bugs — the MIDI-learn one was independently caught
by two dimensions). Both **blockers**, both lifetime defects in the consolidation wiring that a passing
build+selftest can't reach, both fixed in `swapProject()` **before** `doSwap()` destroys the Edit:

1. **Async-export UAF** — starting New/Open during an in-flight async export destroyed the `te::Edit` under the
   still-running render worker (cross-thread use-after-free). Fix: `activeRender.reset()` (joins the worker while
   the Edit is alive) + hide the overlay, before the swap.
2. **MIDI-learn dangling `learningEdit`** — `swapProject` cleared `activeEdit` but not an in-flight learn, so
   `learningEdit` dangled into the freed Edit → UAF on the next `beginLearn` re-entry. Fix: `midiLearn.cancelLearn()`
   (guarded no-op when idle) before the swap.

The **aux-index-stability** and **markers-alignment** dimensions returned clean (empty findings) — the two
new-seam correctness concerns held up under scrutiny.

## Deferred follow-ups (ticketed, out of Wave-01 scope)

- **P2 `ForgeUIBehaviour` + hardware CC routing.** The MIDI-learn seam + Ctrl+L picker are wired, but real
  controller CCs don't yet reach `handleIncomingController` — that needs a focused-edit `te::UIBehaviour` (so the
  engine's own `MidiControllerParser` routes CC) or a Forge MIDI-input listener. Both touch engine construction /
  device management; deferred to keep the wave file-disjoint. Until then Ctrl+L **arms** a learn but completion
  from hardware awaits this follow-up.
- **`--selftest-midilearn` gate.** The seam has no headless runtime proof yet (a virtual MIDI device can't carry
  a CC to the store, so the gate must inject via the seam directly). Adding it means a new selftest mode +
  report; ticketed rather than expanding the consolidation.
- **Audio-slot-record edge-fade.** Only MIDI slot-record exists today; `ClipFades` is a no-op on MIDI, so there
  is no active audio-record commit site to fade. Wire the call in `commitSlotRecord` when audio slot-recording
  lands.
- **Count-in default.** Not force-seeded at launch — it is a global persisted engine setting and seeding it would
  perturb `--selftest-record` / `--selftest-midi`. First run reflects engine truth (no count-in).

## Verification

- **Clean MSVC Debug build, 0 warnings**; single integration target links `Forge.exe`.
- **All four selftests PASS** — `--selftest`, `--selftest-record`, `--selftest-session`, `--selftest-midi` — no
  regression vs. baseline `6100fb9`.
- **`--screenshot`** renders all five views. Confirmed visually: the mixer shows the **A/B send knobs** per strip
  + **Return A / Return B** strips (placeholder "＋ Enable") before MASTER; the arrange control bar shows the
  **Click** toggle + **count-in** selector; the Session grid is unregressed.
- Live GUI gestures (marker add/drag/rename, send-knob drag + "＋ Enable", the export progress/cancel dialog, the
  Ctrl+L learn picker) still need the **manual smoke pass** — the dev window can't be GUI-driven headlessly.
