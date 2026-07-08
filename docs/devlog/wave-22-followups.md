# W22 — buildable-backlog follow-ups: nudge · retrospective capture · LFO Modulate UI · gate hardening

> Baseline **`87225da`** (W21 tip). A follow-up sweep through the "immediate follow-ups + buildable-now" backlog
> (the items surfaced after the frontier program completed). Process: four read-only source-verify spikes → two
> file-disjoint build agents (retrocapture seam · piano-roll nudge) + orchestrator-built items (Modulate UI · gate
> legs) → per-item build + gate verification → one adversarial QC pass. Build **clean** (0 warnings) · **all FORTY-TWO
> selftests PASS** (floor **40 → 42** via `--selftest-nudge` + `--selftest-retrocapture`) · 11/11 screenshots.
> Committed in three scoped commits (`a728f20`, `f91a5c5`, + this docs commit); **push held for the maintainer's OK.**

## What shipped (4 items)

### 1. Piano-roll keyboard nudge
Shift+Left/Right nudges the selected notes by the snap division (`gridBeats`, 1-bar fallback via the null-safe
`getTimeSigAt(...).numerator` idiom). New header-only `forge::midiedit::shiftNoteStarts` — a group-clamped
fixed-delta shift on the Edit UndoManager (mirrors `commitMoveSelection`'s edge clamp, generalized to a fixed
delta), so a chord keeps its internal gaps and never crosses beat 0. `PianoRollView::nudgeSelection` +
`keyPressed` (Shift+arrows, guarded off Cmd like the bare-`Q` handler); non-structural repaint (preserves
selection). Gate `--selftest-nudge` (right/undo/left/large-left-clamp legs).
- *Note:* the "snap off → 1-bar fallback" branch is currently unreachable through the live UI (`gridBeats` has no
  setter to 0 and `snapStartTime` is unwired) — the gate exercises the math directly; a future piano-roll snap
  toggle would light it up.

### 2. MIDI retrospective capture
`ProjectSession::commitRetrospectiveToSlot(trackIndex)` — the "I wasn't recording but just played something"
case. Over the engine's **per-instance** `InputDeviceInstance::applyRetrospectiveRecord(false)` (NOT the aggregate
`EditPlaybackContext` overload, which sweeps in wave inputs + a cross-device lock). The retrospective buffer is on
by default at 30 s and fills for virtual devices too, so it's headless-provable. The materialised clip (which the
engine lands on the arrangement) is relocated into a free launcher slot. Gate `--selftest-retrocapture`: arm track
0 for MIDI, inject 4 notes with **no transport roll anywhere**, commit, assert a slot MidiClip with exactly those 4
notes and `isRecording()==false` throughout.
- **QC-caught use-after-free (found by running it, fixed):** the relocate step held the clip as a raw `Clip*`;
  `removeFromParent()` drops it from the owning track's ClipList (decRefCount → free), so `slot->setClip(mc)` was a
  UAF (crash in `ClipSlot::setClip` → `ValueTree::getParent`). Fixed by holding a `te::MidiClip::Ptr` across the
  reparent. The engine's own relocation (`tracktion_MidiInputDevice.cpp:1463-1470`) only dodges this because it's
  gated on `playSlotClips` (false on a first capture — the very reason we relocate unconditionally). See the new
  CLAUDE.md gotcha.

### 3. The "Modulate" LFO UI
`MainComponent::showModulateMenu()` (Ctrl+M) — a track ▸ plugin ▸ param cascade cloned from the (Fable-approved)
MIDI-learn picker, whose terminal action attaches a fresh LFO (`forge::modifier::addLFO`) and assigns it to the
chosen parameter (`assign`), resolving the host track via `AutomatableParameter::getTrack()`. Makes the W21 LFO
seam usable from the app. The engine seam is gated by `--selftest-modifier`; the menu is UI (untested like
`showMidiLearnMenu`), but the callback's `getTrack()→AudioTrack` resolution is proven by a new `paramResolvesTrack`
gate leg. **Deferred to Fable:** menu-bar placement/wording, the Ctrl+M binding choice, and a "modulated-param"
visual indicator (no indicator exists yet; repeated Ctrl+M stacks independent LFOs — a reasonable v1).

### 4. LFO config-coverage gate hardening
`--selftest-modifier` gained an **offset-sensitivity leg** (a unipolar `depth=0 offset=0.7` LFO reads a constant
≈0.7 → proves the offset + bipolar param writes take effect, complementing the W21 `depth=0` flat leg) — so the
gate now catches an `applyConfig` regression on depth/offset/bipolar, not just at the engine defaults.

## Verification
Build clean (0 warnings) · **42/42 selftest floor** (all 40 prior gates green — no regression — + the 2 new) ·
11/11 `--screenshot` states · scrub-clean (all paths via `getSpecialLocation`, no committed binaries).

## Adversarial QC
One opus dimension over the two non-trivial features (retrocapture seam + Modulate UI), default-refute — findings
folded in (see the commit trail / this file's history for the verdict).

## Deferred (with rationale — NOT built this sweep)
- **Capture count-in** — the spike revealed this is *not* the "trivial S follow-up" the backlog implied: the only
  audible-count-in path is `transport->record(…, allowRecordingIfNoInputsArmed=true)` in `startPerformanceCapture`,
  which puts the transport in **record mode for the whole Capture session** (`isRecording()==true` with nothing
  recording), lights the plain Rec button (a must-fix cross-wire), and its pre-roll click rides `setClickTrackRange`
  (interaction with the `clickTrackEnabled` toggle untraced). Grafting record-mode onto the carefully-timed W17
  `transport.play()`-based capture path risks the capture behaviour and needs its own focused wave with runtime QC
  of record-vs-play capture — deferred rather than rushed. Full recipe preserved in the spike notes.
- **Drum-kit render gate leg** — a real note-on → engine-render of the drum Sampler (pump the async load via
  `MessageManager::runDispatchLoopUntil`, then `renderStems` + non-silence with a SKIP-degrade). Experimental (the
  Sampler async-load pump has no working headless precedent; likely SKIPs) and the documented W09/W10 render-leg
  class; the existing `--selftest-drumkit` already proves file-level non-silence. Deferred.
- **Latent bug spotted in passing (spike 4):** `insertClipCopyOnTimeline` only loop-normalizes `AudioClipBase`/
  `MidiClip`, not `StepClip` — a real editing gap when sending a looping step clip to the arrangement (harmless for
  a render leg). Its own follow-up.

## Decision owed to the maintainer
- **Per-clip instrument** (the mixed step-clip + melodic-clip "first-instrument-wins" silence): verified NOT
  possible without an engine fork — `ClipSlot` has no plugin list and slot playback routes through the owning
  AudioTrack's chain (`tracktion_EditNodeBuilder.cpp:949,1091`; `ClipSlot` has no plugin members). The realistic
  v1 is "a step clip landing on a track that already hosts a melodic instrument auto-routes to its own new track"
  (and symmetrically) — a **UX behaviour change** that needs the maintainer's/Fable's sign-off, so it was not built.

## New gotcha (for CLAUDE.md)
- **The engine's retrospective-record relocate-to-slot snippet has a latent UAF outside its `playSlotClips`
  guard.** `InputDeviceInstance::applyRetrospectiveRecord` returns clips on the arrangement; relocating one into a
  slot via `removeFromParent()` + `slot->setClip()` frees the clip (its owning ClipList decRefCounts it) unless a
  `Clip::Ptr` is held across the two calls. The engine only avoids this because its own relocation runs solely when
  `playSlotClips` is already true; any code relocating unconditionally (like `commitRetrospectiveToSlot`) MUST hold
  a Ptr.
