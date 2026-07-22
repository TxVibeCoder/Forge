# Wave 24 — Backlog burn-down (B1 + B2 + B3 + B5 + B7)

> The first wave driven straight off `docs/BACKLOG.md` (added at `80bae73`): five of its "Ready to build"
> items in one pass, following the backlog's own suggested fan-out (B1 + B5 + B7 file-disjoint in parallel;
> B2 + B3 orchestrator-owned in `main.cpp`). Preceded in the same session by two standalone landings —
> B6 groundwork (`13a1f0f`: the PluckBass / Pad / Bell / Clav self-rendered CC0 melodic voices +
> `InstrumentPreset` plumbing) and the B8 curated launch-quantise submenu (`14c2c61`).

## How it ran

Three parallel agents on disjoint territories, per the Wave Orchestration Rule (agents edit + self-review,
propose `main.cpp` wiring in results packets, never build, never commit); the orchestrator owned `main.cpp`
end to end (B2 + B3 built directly there), applied the three packets' proposed wiring, and ran the single
integration build + the full floor:

| partition | item | territory | outcome |
|---|---|---|---|
| P1 | B1 multi-track MIDI import | `ProjectSession.{h,cpp}`, `BrowserView.{h,cpp}` | DONE |
| P5 | B5 trim residuals | `AudioEditHelpers.h`, `MidiEditHelpers.h` | DONE |
| P7 | B7 Modulate UI polish | `ForgeMenuModel.{h,cpp}`, `MixerView.{h,cpp}`, `ChannelTray.{h,cpp}`, `SessionMixerStrip.{h,cpp}` | DONE |
| orch | B2 save→reload gate + B3 render legs + all wiring | `main.cpp` | DONE |

## What shipped

### B1 — MIDI import: multi-track files + browser support
- New seam `ProjectSession::importMidiFileMultiTrack(file, start, firstTrackIndex)`: parses the whole file
  first via **`te::readFileToMidiList`** — the engine's own reader (the exact parse `createClipFromFile`
  rides and then truncates to `lists.getFirst()`, which WAS the single-clip v1 limit) — so the
  tempo-independent ticks→beats mapping and the per-(source-track × channel) decomposition are identical to
  the legacy path by construction. One born-audible clip per non-empty part on consecutive tracks (created
  on demand); CC/sysex-only parts are skipped (no silent clips); a failed track resolve breaks (keeps what
  landed), a failed insert retries the same index (no gap lanes); fires `onTracksChanged` iff the track
  list grew. `importMidiFile` / `importMidiIntoSlot` keep their single-clip contracts untouched.
- Browser filter widened to `*.mid;*.midi` ("Audio & MIDI files"); the browser double-click and the arrange
  file-drop both route `.mid`/`.midi` through the new seam (the drop fans out downward from the dropped
  lane; a single-track file lands exactly one clip, as before).
- Gate: `--selftest-midifile` +6 legs (a 4-chunk file — 2/3/5 notes on channels 1/2/3 + a CC-only chunk
  that must be skipped → exactly 3 clips on consecutive tracks with the right counts; single-track
  regression control through the new seam).

### B2 — `--selftest-reload` (floor 46 → 47)
The first gate that ever re-reads state from **disk**. Seeds through the real seams (scene rename, a 2-note
slot clip, Toggle launch mode, per-clip launch quantise 1/8, a `trackNext` follow action — type before
duration per the W11 auto-plant gotcha — and a 5/4 time signature), `saveAs()` to a temp edit, then
**mutates every seeded value away in memory (unsaved)** before reopening through the REAL `swapProject`
path. The mutate-away step is the design's load-bearing wall: without it, a silently-failed reload leaves
the old in-memory edit whose values would satisfy every assert — the gate would pass whether or not
anything was ever read back from disk. Undo history intentionally unasserted (does not survive a swap).

### B3 — render-audibility legs (the "it's silent and we'd never know" class)
`--selftest-demo` and `--selftest-drumkit` were restructured into two phases: phase 1 keeps the structural
legs and seeds a short arrange clip on the Sampler track; a 600 ms `Timer::callAfterDelay` pumps the message
loop (the Sampler ingests its audio on an `AsyncUpdater` — rendering without the pump measures silence and
would call it a failure); phase 2 renders the stem via the synchronous `Exporter::renderStems`, samples its
peak with `readPeakMagnitude`, and folds in as three-state PASS/FAIL/SKIP (the `--selftest-sendarrange` W16
discipline: FAIL only on a produced-but-silent stem; SKIP honest + non-blocking; never a fictional PASS).
**Both legs verified `renderAudible=PASS`** — demo `renderPeak≈0.55`, drumkit `≈0.65` — closing the W09
Sampler-ingestion follow-up and the W22 drum-kit render deferral.

### B5 — both W23 trim limits closed
- **Speed-correct audio trim.** The unity-speed guard is gone; the helper now applies `Δ = Ts/speed −
  offset` with a speed-mapped scan window. The formula was **derived from the engine, not trusted from the
  note**: `AudioClipBase::clipTimeToSourceFileTime` (non-looping branch) gives `sourceTime = (t + offset) ·
  speed`, so the left edge plays `offset·speed` and moving it to source-onset `Ts` needs `offset' = Ts/speed`.
  Bit-identical behavior at `speed == 1`. The gate leg **pre-seeds offset = 0.25 s** — essential, since at
  offset 0 the correct and the dimensionally-wrong `(Ts − offset)/speed` coincide; the leg's ±0.02 windows
  fail the naive answer. Bonus hardening: **auto-tempo clips are now explicitly declined with a logged
  WARN** — their offset is reinterpreted in beats, and the old code would have let one with `speedRatio == 1`
  slip past the guard and mis-trim (a latent W23 hole, closed).
- **CC-only MIDI trim.** The empty-guard relaxed from `getNumNotes() == 0` to "no notes AND no CC AND no
  sysex" — `MidiList::getFirstBeatNumber()` already folds all three in, so a controller-only clip now trims
  to its first event. Gate leg: one mod-wheel event at content beat 3 (14-bit value convention).

### B7 — Modulate (LFO) UI polish (Fable calls, implemented)
- **Edit ▸ Modulate… (Ctrl+M)** in the menu bar, grouped with MIDI Learn (both are parameter-picker cascades
  that attach an external driver to a plugin parameter). The shortcut label is display-only per the model's
  contract; `keyPressed` remains the single key owner.
- **Modulated-parameter indicator**: a ~5px accent dot on a panelBg backing disc (the disc is the legibility
  detail — a bare accent dot vanishes over an accent-filled slider track) at the top-right of the vol/pan
  controls on MixerView's ChannelStrip + ReturnStrip, ChannelTray, and SessionMixerStrip. Query is the cheap
  `hasActiveModifierAssignments()` atomic read; results are edge-compared into cached bools on each
  surface's EXISTING poll (no new timers), `paintOverChildren` reads only the cache, repaint/tooltips fire
  only on transitions. Tooltips gain "modulated (LFO)" while active.
- Gates: `--selftest-menu` (Edit count 3→4 + a Ctrl+M shortcut pin) and `--selftest-sessionmixer` (+3 legs:
  off-baseline → on-after-assign → cleared-after-`removeLFO`; the full-teardown `removeLFO` avoids the
  `removeModifier` asserts-on-no-op footgun by construction). The `--screenshot` demo content now assigns a
  gentle LFO to the Bass track's pan so the dot renders in `mix` / `arrange_tray` / the Session states
  (visually verified in `forge_shot_mix.png`).

## Verification

- Build **clean (0 warnings)**, single integration build after consolidation.
- **47/47 selftest floor PASS** (every `mode=` line verified; `--selftest-reload` new).
- Both B3 render legs re-run standalone: genuine `renderAudible=PASS` (peaks 0.55 / 0.65), not SKIP.
- **12/12 `--screenshot` states** render; the B7 dot confirmed visible in the mix state.
- Sanitize scan clean over the full diff.

## Not done / follow-ups

- **B4 capture count-in** — untouched (its own wave; risks the W17 capture path).
- **B6 remaining** — the browser→slot interaction + a UI to assign `InstrumentPreset`s (the four W24
  melodic voices are still API-only); fold their render proof into the B3 leg pattern when surfaced.
- **Item 0 (Redo)** — still blocked on the maintainer's A/B/C decision.
- P1 flagged honest edges: a single format-1 source track using multiple channels yields one clip per
  channel (the engine's canonical decomposition); the file's own tempo/meter map is NOT imported (a product
  decision — it would rewrite the project tempo); slot-side multi-track fan-out is out of scope.
- P7 skipped the master strip (its volume plugin is unreachable from `showModulateMenu`, so a dot there
  could never light) and left the two file-local indicator helpers unextracted (`StripWidgets.h` is
  style-only by that file's own doc; lift-verbatim recipe recorded in the P7 packet).
