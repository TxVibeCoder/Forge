# W17 — frontier Wave 7: performance capture (the real Session → Arrange bridge)

> Frontier build program, Wave 7. Baseline **`96b1037`** (tip of W16, pushed). Adds one gate
> (`--selftest-capture`) → floor **32 → 33**. Single-CLI serial spine — `ProjectSession` (capture seam + a
> behaviour-preserving refactor of the W10 `sendSlotToArrangement` clone body), `main.cpp` (gate + ladders),
> `TransportBar`/`ControlBar` (the Global-Rec "Capture" toggle). Scoped this session to the capture core only
> (item 1 of 3); the two fast-follows — whole-scene-send and send-as-loop — are a clean follow-up wave riding
> the same `insertClipCopyOnTimeline` seam, per the maintainer's scope call.

## What shipped

DIRECTION.md's literal promise — "compose in Session, then arrange linearly" — finally has teeth. A new
**Global "Capture"** toggle in the transport bar (distinct from **Rec**, which is the MIDI-into-slot take path)
arms **performance capture**: while armed, launching clips/scenes as usual gets recorded — *which* clip fired,
*when* (its absolute Edit beat), and *for how long* — and toggling Capture off stamps one one-shot clip per
captured span onto each track's linear Arrangement, at the beat it actually fired, all in **one undo
transaction** (one Ctrl+Z removes the whole take).

This is a different feature from W10's `sendSlotToArrangement` ("Send to Arrangement", a static single-clip
append-at-end): capture preserves the **timing of a live performance**, not just the notes.

## The mechanism (load-bearing)

`te::LaunchHandle::getPlayedRange()` is a **single current span**, not a history buffer — `BeatRange(startBeat,
duration)` (Edit beats) while a clip plays, `nullopt` once it stops. So capture **samples-and-accumulates** on
the message thread: `ProjectSession` is now a `private juce::Timer` (~30 Hz while armed), and
`performanceCaptureTick()` walks every filled `(track, scene)` cell each tick, re-resolving fresh (R1 — no
`te::` pointer is ever cached across ticks):

- **Fresh play** (no open span for this cell) → open one, capturing the clip's `te::EditItemID` right then.
- **Same play, still growing** → extend the open span's end beat.
- **StartBeat jumped** (a stop+relaunch happened between ticks) → seal the old span, open a new one.
- **Stopped** (`getPlayedRange()` now `nullopt`) → seal and close.

On disarm, any still-open spans are sealed, then each sealed span stamps a clip via a shared clone/normalize
helper (below) at its **absolute captured beat** — converted beats→seconds via `edit->tempoSequence.toTime`.

## The shared-start refactor (behaviour-preserving, per the frontier-program critic's finding #5)

W10's `sendSlotToArrangement` inlined its clone→normalize→audibility-flip→metadata-strip body. Capture needs
the identical recipe at an **arbitrary** beat instead of "append at end", so the body was extracted verbatim
into a new private `ProjectSession::insertClipCopyOnTimeline(track, src, destPos, keepAsLoop)` — same engine
calls, same order, same arguments, `markAsChanged()` correctly hoisted OUT (into each caller, so capture's
N-clip commit doesn't fire it N times). `sendSlotToArrangement` now calls the helper with its own `appendAt`
position and `keepAsLoop=false`; **`--selftest-sendarrange` stays byte-identical green** as the regression
guard (independently re-verified by adversarial QC — see below).

## The QC-caught defect: resolve by identity, never by cell index

A 4-dimension adversarial QC swarm ran against the new capture logic, the refactor, and the transport-bar
layout change. **Three dimensions came back clean** (lifetime/threading/R1 discipline; the refactor's
byte-identity + beat↔time math; the transport-bar layout arithmetic at the 760 px window floor — worked by hand,
worst-case control width 40 px, no starvation of the W06 regression class). **One confirmed defect** in the
accumulation logic:

`stopPerformanceCapture`'s original commit loop resolved each span's source clip via
`getClipSlot(span.track, span.scene)->getClip()` — i.e. "whatever occupies this cell **now**". But a capture
session can span real time, during which an ordinary jam move — clearing a clip from a slot and dropping a
*different* clip into it — silently swaps what that resolve returns. The result: a span captured from clip A's
performance would, at commit, clone clip **B**'s content and stamp it at A's captured beat.

**Fix:** each span now carries the `te::EditItemID` of the clip that was **actually playing when the span
opened** (captured once, at open time, from the freshly-verified-non-null clip — never re-derived at seal or
commit). Commit resolves via `te::findClipForID(*edit, span.clipID)`, an edit-wide identity lookup that still
finds a since-*moved* clip and cleanly degrades (a logged skip, never a silent wrong-content stamp) if the clip
was genuinely deleted. The subtler part of the fix: the *reseal* branch (a stop+relaunch inside one tick gap)
seals the **old** span using its own already-stored identity, not the freshly-resolved clip — resolving the old
span's identity from "whatever's there now" would just move the same bug earlier, since the freshly-resolved
clip in that branch may already be the *new* occupant. Full re-verify after the fix: all 33 gates pass,
`--selftest-sendarrange` unchanged, `--selftest-capture`'s `absoluteBeatPreserved` leg confirms the beat-preserve
round-trip end-to-end.

A second, lower-severity finding (documented, not code-changed): the stop/relaunch reseal heuristic ("a changed
`startBeat` always means a new play") is correct **only because** Forge never calls
`LaunchHandle::nudge()`/`setLooping()`/`playSynced()` — all three would also mutate `startBeat` mid-play if
wired. None has a caller today; the header comment now states the dependency explicitly so a future wave that
wires per-clip handle-level looping or nudge revisits this heuristic alongside it.

## The gate — `--selftest-capture`

Deterministic (not wall-clock-timed): seeds a 4-note MIDI clip in slot (0,0), rolls the transport to a **known**
beat 4 via `setPosition` (not a pre-roll race), arms capture, launches, samples in lockstep with
`blockUntilSyncPointChange` (the same pump `--selftest`/`--selftest-session` use), stops, and commits bracketed
in one undo transaction — mirroring the shell's Global-Rec handler exactly. Asserts: a span accumulates; commit
stamps a one-shot `MidiClip`; it lands at its captured beat (≈4.0, clearly not 0 — the point of capture vs.
append-at-end); the source slot stays filled (a copy); one undo removes the whole take. `-capture` is
collision-free (no substring overlap with any existing gate name) — placed before bare `--selftest` in both
ladders. Does **not** prove the captured arrangement renders audibly (needs a pumped render — parked per the
W09/W10 render-leg convention; audibility rides the same `playSlotClips` flip `--selftest-sendarrange` already
proves).

## The UI — a distinct Global-Rec toggle

`TransportBar` gained a 7th button, **"Capture"** (record-red when armed, matching **Rec**'s family but
distinguished by label + tooltip — a deliberate choice, not an oversight: overloading the existing **Rec**
button would collide semantically with the MIDI-into-slot take path). `preferredWidth` grew 650→718;
`resized()` was rewritten to flex the **whole** 7-button+2-combo strip proportionally (with per-element floors)
rather than the old two-combo-only split, so no control starves at the 760 px window minimum — QC hand-verified
the arithmetic (7 buttons @ 40 px, count-in @ 75 px, launch-quant @ 57 px at the floor; nothing under 20 px).

## Scope note

This session built item 1 of the frontier program's 3-item Wave 7 (capture core only, per the maintainer's
explicit scope call — "capture core first"). The two fast-follows — **whole-scene-send** (send every filled
clip in a scene to the Arrangement, aligned at one shared start) and **send-as-loop** (a `keepAsLoop=true`
variant of the now-shared `insertClipCopyOnTimeline`) — are natural, low-risk follow-ups on the seams this wave
built, and per the frontier-program critic's finding #1, whole-scene-send's `SceneColumnComponent` menu item
must be **appended** to W15's existing scene-row PopupMenu, never a competing rewrite.
