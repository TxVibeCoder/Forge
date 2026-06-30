<!--
  Orchestrator's wave-by-wave record of the MIDI MVP build (engine Phase 3, the audible-MIDI slice).
  Built 2026-06-30 from the source-verified design in midi-design.md, as a file-disjoint 5-wave
  Workflow fan-out (W1–W4 authored in parallel, W5 integrated by the orchestrator), then an
  adversarial verify wave. Commit: feat MIDI MVP (9a24989).
-->

# Forge: MIDI Tracks + Piano-Roll — MVP Build Record

This is the build that realized [midi-design.md](midi-design.md). The MVP (W1–W5) ships **a drawn
MIDI clip that is audible end-to-end with zero recording code**: right-click an empty lane area →
**New MIDI Clip** → a `te::MidiClip` is created on that track, born audible via an auto-inserted
**4OSC** at chain index 0, and the **piano-roll** opens in the bottom drawer ready to draw. Notes
route through the 4OSC to the track output on play.

## How it was built

A **single file-disjoint Workflow fan-out**: four authoring agents (W1–W4) each with exclusive file
ownership, additive-only interfaces, and contract-first seams, run in parallel; the **orchestrator
alone** owned `CMakeLists.txt` + `main.cpp` and did the **one integration build**. Every load-bearing
API was source-verified against the vendored engine **before** launch, so the agents started from
facts, not prose — the integration build was **clean on the first try**.

| Wave | Owned (exclusive) | Delivered |
|---|---|---|
| **W1** | `src/engine/PluginHost.{h,cpp}` | `addInstrumentToTrack(track,name)` (own **insert-at-0** path, not the volume-index effect path) · `ensureDefaultInstrument(track)` (idempotent; detects an existing synth via `isSynth()‖takesMidiInput()`) · `makeBuiltIn` category parameterized so 4OSC reports "Instrument". |
| **W2** | `ProjectSession.{h,cpp}`, `EngineHelpers.h` | `ProjectSession::createMidiClip(trackIndex, range, name)` → the **AudioTrack member** `insertMIDIClip(name, range, nullptr)` (returns `MidiClip::Ptr` directly; dodges the `insertMIDIClip(ClipOwner&)` free-fn name collision) → `ensureDefaultInstrument` → `markAsChanged`. Additive `EngineHelpers::insertDrawnMidiClip`. |
| **W3** | `src/ui/arrange/ArrangeView.{h,cpp}` | Extract base **`ClipComponent`**; `AudioClipComponent` + new **`MidiClipComponent`** derive from it; `rebuildClips` picks the subclass by `dynamic_cast<te::MidiClip*>`; the six clip callbacks re-typed `AudioClipComponent&`→`ClipComponent&`; **"New MIDI Clip"** empty-lane-area + header menus → `onCreateMidiClipRequested(int,TimePosition)`. |
| **W4** | `src/ui/pianoroll/*` (new) | `PianoRollView(TimelineView&)` (shared time axis) + `MidiNoteComponent`: **Viewport**-scrolled 128-row grid + keybed gutter; draw/move/resize/delete on `getSequence()` with `&clip->edit.getUndoManager()`; content-relative beat math (`beatToX`/`xToBeat`, gutter excluded like the arrange header). |
| **W5** | `CMakeLists.txt`, `main.cpp` | Add the two piano-roll TUs; **selection routing** (`MidiClip`→`pianoRoll`, else `detailView`) via a `bottomMode` drawer that swaps editors in `resized()`; `onCreateMidiClipRequested`→`createMidiClip` (builds a 16-beat range, opens the roll on the new clip); `pianoRoll.onEditMutated`→`session.save()`; project-swap clip-drop. **One clean build.** |

## Deviations from the design (all confirmed improvements)

- **W3 / note preview:** `te::MidiClip`/`te::Clip` have **no** clip-level `getStartBeat()/getEndBeat()`
  (only `te::StepClip` does). `MidiClipComponent::paint` maps notes via the engine's documented
  content-beat→time mapper **`getTimeOfContentBeat`** ([tracktion_Clip.h:197](../../libs/tracktion_engine/modules/tracktion_engine/model/clips/tracktion_Clip.h)) — offset/tempo/speed-correct.
- **W2 / helper:** added `insertDrawnMidiClip` but kept `createMidiClip` inlined, because it must hold
  the `AudioTrack*` to call `ensureDefaultInstrument` (delegating would force a second track lookup).
- **W4 / delete:** right-click-delete only (no Delete-key shortcut yet — needs drawer keyboard focus the
  orchestrator owns) and visual-only selection. Both are MVP-sufficient; flagged for W6.

## Verification

- **Build:** clean on the first integration build (6 TUs recompiled + linked, 0 errors).
- **Selftests (no regression):** `--selftest` (playback) **PASS**; `--selftest-record` **PASS**
  (`recordedPeakMagnitude≈0.68` — real signal). The new wiring never touches the record/playback paths
  (verified by diff).
- **Adversarial verify wave** (3 skeptic agents, default-refuted, evidence-required, read-only) over
  W3 / W4 / W5: **all three `correct`, zero blocker/major/minor findings.** The two highest-risk items
  were traced and cleared against engine source:
  - **`MidiNote&` lifetime:** `setStartAndLength`/`setNoteNumber` do **not** free the note, so the raw
    reference held across a commit is safe; right-click-delete is safe because JUCE's `internalMouseDown`
    bails via `HierarchyChecker::shouldBailOut()` after the component deletes itself.
  - **Instrument-at-0 / audibility:** the detection loop can't false-positive on the always-present
    `VolumeAndPanPlugin`/`LevelMeterPlugin` (neither overrides `isSynth()`/`takesMidiInput()`), so a real
    4OSC is always inserted at the chain head on the clip's host track → the clip sounds; re-create can't
    stack synths.
- **Latent (out of scope, offset always 0 in the MVP):** `beatToX`/`xToBeat` anchor content-beat-0 at
  the clip start and ignore clip offset; relevant only if MIDI clips later gain a non-zero offset.

## W6 — velocity + selection/clipboard polish  (`bb5b6bf`; single-owner agent + verify)

Post-MVP editing, all within `src/ui/pianoroll/` (one cohesive owner, so a single authoring agent rather
than a fan-out). **Multi-select** (click / Shift-Ctrl-click / marquee rubber-band — a plain click still
draws a note) · **Delete key** + delete-selection · **multi-note move** (whole selection by one delta,
**group-clamped** against the selection's headroom so chord intervals survive the beat-0 / pitch edges) ·
**copy/paste** (Ctrl+C/V, pasted at the snapped playhead, auto-selected) · **velocity** via a new
`VelocityLane.{h,cpp}` bottom strip (draggable bar per note, top = loud, 1..127) + velocity shading on the
grid notes. The roll grabs keyboard focus on any interaction and `keyPressed` returns `false` for keys it
doesn't consume, so the shell shortcuts (Space/R/Ctrl+S) still work; the four shell-facing symbols are
unchanged (only CMake gains `VelocityLane.cpp`). Build clean; both selftests **PASS**. The verify wave (2
skeptics) returned `correct` on velocity/layout and caught two real low-severity issues, both **fixed**:
the multi-move group-clamp above, and a missing keyboard-focus grab on velocity-lane clicks.

## Not yet done (post-MVP)

- **W7 — MIDI-input recording:** the higher-risk wave — its own MIDI enable sequence
  (`getMidiInDevices()` + `setEnabled` + `setMonitorMode` + `rescanMidiDeviceList()`) **before**
  `ensureContextAllocated()`, a different device-type filter, and a **runtime test with a physical MIDI
  controller**. See [midi-design.md §5](midi-design.md).
- **GUI smoke test:** the draw→play→hear path is statically verified but not yet exercised in the live
  window (the dev-built `Forge.exe` can't be GUI-driven headlessly here).
