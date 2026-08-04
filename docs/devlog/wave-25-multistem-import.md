# Wave 25 — Multi-stem audio import (drop N files → N tracks)

> A maintainer brief (2026-08-04), not a roadmap item: *"Dropping a folder's worth of stems onto the Arrange
> timeline should land one clip per file on consecutive tracks, named after the files."* Filed as **B9** on
> `docs/BACKLOG.md` and burned down in the same pass. Single-CLI wave — the Wave Orchestration Rule is
> skipped (one territory, one builder).

## The gap

A multi-file drop imported **only the first file and discarded the rest**: `TrackLaneComponent::filesDropped`
looped the incoming `StringArray` and `return`ed on the first accepted entry. Mixing a 6-stem separation meant
six separate drags, each aimed at a different lane.

Everything downstream was already in place and was **re-verified in the source before any code was written**
(the brief's own framing — "this is a small fix to an existing seam, not a feature"): the mixer's insert panel
offers a built-in 4-Band Equaliser (`te::EqualiserPlugin`, `src/engine/PluginHost.cpp:68`) plus
compressor/reverb/delay/chorus, faders/pan/mute/solo are live, and `renderEditToWavAsync`
(`src/services/export/Exporter.h:190`) exports the whole edit through the master chain. Only the import was
one-at-a-time.

## Reuse first

`ProjectSession::importMidiFileMultiTrack` (W24, B1) had already solved this shape for MIDI, so it was the
template rather than a new invention. Carried over verbatim:

- validate/parse **before** touching the edit, so a bad input mutates nothing;
- `EngineHelpers::getOrInsertAudioTrackAt` to grow the track list on demand;
- **do not advance the destination index on a per-file failure** — the next file retries that lane, so what
  landed stays on consecutive filled lanes with no silent gap;
- one `edit->markAsChanged()` at the end;
- fire `onTracksChanged()` only if `te::getAudioTracks(*edit).size()` actually grew (captured up front);
- return the created clips in destination-track order, empty + logged on total failure.

The per-file body is `ProjectSession::importAudioFile` called unchanged — so the P6 anti-click edge fades
(`ClipFades::applyDefaultEdgeFades`) come along for free and there is no second copy of the import recipe.

## What shipped

### The seam — `ProjectSession::importFilesMultiTrack`

```cpp
struct MultiFileImport {
    std::vector<te::WaveAudioClip::Ptr> audioClips;   // destination-track order
    std::vector<te::MidiClip::Ptr>      midiClips;
    int  filesFailed = 0;
    bool tracksGrew  = false;
};
MultiFileImport importFilesMultiTrack (const juce::Array<juce::File>&, te::TimePosition start, int firstTrackIndex = 0);
std::vector<te::WaveAudioClip::Ptr> importAudioFilesMultiTrack (const juce::Array<juce::File>&, te::TimePosition, int = 0);
```

`importAudioFilesMultiTrack` — the signature the brief named — is a one-line convenience returning
`.audioClips`. Two entry points, **one** walk: the mixed-drop rule below could not have been expressed
consistently if the audio fan-out and the MIDI dispatch were separate loops.

Three things the MIDI seam did not need:

**1. A deterministic filename sort.** The OS hands a multi-selection over in arbitrary order. Without a sort,
lane assignment is non-deterministic and *no selftest can assert anything about it*. `juce::String::compareNatural`
on the file name, so `stem2` precedes `stem10`.

**2. Destination-track naming — guarded.** Each lane is named after its file
(`getFileNameWithoutExtension`), which is most of the value of the change: it is the difference between a
mixer reading `vocals / drums / bass` and one reading `Track 1 / Track 2 / Track 3`. (`loadAudioFileAsClip`
already named the *clip*; the *track* was what was missing.) A lane is renamed **only** when this import
created it, or when it is still unnamed **and** holds no clips. See the engine gotcha below for why "still
unnamed" is not the obvious test.

**3. An explicit mixed audio + MIDI walk.** One sorted pass, each file taking the next destination index:
audio → `importAudioFile` (advances 1), `.mid`/`.midi` → `importMidiFileMultiTrack` at that index (advances by
however many clips it returned). No filtering, no special case. Settled deliberately rather than left
emergent, because "what happens if you drop a .mid in with your stems" is a question a user will ask by
accident on their first day.

### One save per drop

The shell handler previously called `session.save()` after each import. Six saves for one gesture would be six
undo steps — and every `session.save()` trips the known `FourOscPlugin` redo-wipe defect (BACKLOG item 0), so
save count is not merely a performance question here. Doing the loop **inside** `ProjectSession` is what makes
a single save natural. Two supporting details:

- `importMidiFileMultiTrack`'s own `onTracksChanged` fire is **suppressed** while the walker runs (a private
  RAII `ScopedTracksChangedDefer`); the walker fires once, at the end, for the whole gesture. Without this a
  mixed drop containing three `.mid` files would have triggered three full save+rebuild passes mid-walk.
- `MultiFileImport::tracksGrew` tells the shell whether the seam already fired `onTracksChanged` (which the
  shell binds to save + rebuild). It saves only when it did not — so the drop is exactly **one** save either
  way. `--selftest-stems` asserts the fire count is 1 for a four-lane drop.

### The drop path

`TrackLaneComponent::filesDropped` now collects **every** accepted file into a `juce::Array<juce::File>` instead
of returning on the first; `TrackLaneComponent::onFilesDropped` and `ArrangeView::onFilesDropped` both changed
from `const juce::File&` to the collection (doc comments updated — the old one still said "the first accepted
audio file"); the lane lambda forwards it unchanged; the shell dispatches through the one seam. No raw `te::`
import call was added to the UI layer — `ArrangeView` still only bubbles the drop up.

### Gate — `--selftest-stems` (floor 47 → 48)

24 legs over five phases: the core fan-out, partial failure, the naming discipline, the mixed walk, and the
nothing-importable guard. Full field contract → `../../tests/SELFTEST.md`.

Two design choices make it hard to fake:

- **Phase A feeds the four fixture stems in a deliberately unsorted order** (vocals, bass, keys, drums) and
  asserts the sorted result — so the sort is load-bearing, not incidental.
- **Each stem has a distinct duration as well as a distinct name**, and order is asserted on the *resolved
  source file* (`Clip::getCurrentSourceFile()`) with clip length as an independent content-level cross-check.
  A seam that paired the right lane name with the wrong audio would still fail.

**Negative controls (run before the gate was trusted, then reverted):**

| mutation | effect |
|---|---|
| delete the filename `std::sort` | `orderOk` / `namesOk` / `lengthsOk` → 0 |
| make the per-file failure path `++destIndex` | `partialLanesOk` → 0, **everything else still green** — the no-gap-lane rule isolated |

## Engine gotcha found (new CLAUDE.md entry)

**`AudioTrack::getName()` can never tell you whether the USER named a track.** It returns the stored name only
if non-empty, and otherwise **synthesises** `"Track N"` from the track's position
(`tracktion_AudioTrack.cpp:213` → `getNameAsTrackNumber`). It is therefore never empty, and a
`getName().isEmpty()` test is dead code. Worse, `AudioTrack::sanityCheckName()` **resets** a literal
`"Track <digits>"` name back to empty, so string-matching `"Track "` is wrong too. The only honest read is the
raw property backing the `trackName` CachedValue (`tracktion_Track.cpp:19`):

```cpp
track.state[te::IDs::name].toString().isEmpty()
```

Paired with an "is this lane in use?" check that spans **both** clip lists — `getClips()` (arrange) and
`getClipSlotList().getClipSlots()` (Session) are deliberately disjoint (the W10 gotcha), so a `getClips()`-only
emptiness test reads a slot-filled track as empty and would rename it. `slotOnlyNameKept` gates exactly that.

## Verification

- Build **clean (0 warnings)**, MSVC Debug.
- **48/48 selftest floor** — the 47 pre-existing gates were run against the pre-change binary first to
  establish the baseline honestly (47/47), then the full 48 after.
- **12/12 screenshots** — unchanged; this wave adds no new UI state (the drop is an interaction, and its
  outcome is ordinary clips on ordinary lanes).

### ⚠ Observed once: `--selftest-popout` is intermittent

The **first** full-floor run after the change reported `--selftest-popout` FAIL; the second full run and four
isolated runs all PASS (6 PASS / 1 FAIL total on this binary, plus PASS on the pre-change baseline). The gate's
own log line shows it ran normally — `beginPopoutSelftest` at 07:46:15.212, report written 309 ms later — with
no `WARN`/`ERROR`, so a leg assertion flipped rather than anything crashing. **Not attributable to this wave:**
the gate exercises tear-off `PopoutWindow`s, undo/redo key routing and the piano roll, none of which this diff
touches; the only shared-path change is the `deferTracksChanged` guard inside `importMidiFileMultiTrack`, which
is inert unless `importFilesMultiTrack` is on the stack, and popout calls neither.

Most likely a real-window timing sensitivity — several of its legs (`mixerWindowSeen` / `rollWindowSeen` /
`noGhostOverlay` / `hasKeyboardFocus`) read live OS window state behind a fixed 300 ms yield, which the
window manager can miss under load. **Left unfixed and undiagnosed** because the failing report was overwritten
by the next gate before it could be read, and hardening a gate that is not this wave's subject would be scope
creep. *Actionable follow-up if it recurs:* the floor runner should archive each gate's report on failure (the
scratch runner used here now does), and the popout gate's 300 ms yield is the first thing to widen.

## Out of scope (all deliberate, all from the brief)

Browser multi-select (`browserPanel.onImportFile` stays single-file double-click — drag-drop is the stem
workflow); folder drops; auto-grouping stems into a folder/group track or auto-routing them to an aux bus;
Session-grid (clip-slot) multi-import — Arrange lanes only; gain-staging / normalization / auto-levelling; and
any change to the mixer, EQ or exporter, which were verified working rather than modified.

## Adjacent doc fixes

`README.md` claimed a **sixteen**-gate floor and a 9-state screenshot matrix; reality is 48 and 12. Corrected
while updating the same count elsewhere — a knowingly-false number left standing next to a corrected one is
worse than either.

**Push state corrected too.** `HANDOFF.md` / `BACKLOG.md` both carried "⚠ NOT pushed — push HELD" for W24. A
`git fetch` on 2026-08-04 shows `origin/main` at **`5ae8418`** (W24's docs commit), so W24 *is* published and
local `main` is exactly **one** commit ahead. Those notes were true when written and are now stale; they have
been marked as such rather than silently deleted. **W25 itself is unpushed, held for the maintainer's OK.**
