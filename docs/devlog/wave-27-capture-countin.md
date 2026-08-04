# Wave 27 — Capture count-in (B4)

> The last "ready to build" item on the backlog, and the one carrying a **wrong inherited rationale**.
> Checking that rationale was most of the work; building the feature took less code than reading the engine
> did. Single-CLI wave.

## The deferral was based on a misreading

W22 deferred B4 with this note, explicitly flagged **`[inherited]` … not re-verified**:

> *the only audible-count-in path puts the transport into **record-mode for the whole Capture session**,
> which risks the carefully-timed W17 performance-capture path and would light the Rec button for a
> non-recording action.*

The premise is false. Tracktion's count-in **is** only reachable through `TransportControl::record()` — but
that is not the same as being a *property* of record mode. What `record()` actually does with it
(`tracktion_TransportControl.cpp:1483-1489`):

```cpp
auto prerollStart = transportState->startTime.get();
double numCountInBeats = edit.getNumCountInBeats();
if (numCountInBeats > 0)
{
    auto currentBeat = ts.toBeats (transportState->startTime);
    prerollStart = ts.toTime (currentBeat - BeatDuration::fromBeats (numCountInBeats + 0.5));
}
```

A **position offset**, plus a forced click range over the pre-roll. Both are reproducible on a plain
`transport.play()` with `clickTrackEnabled` on. So the count-in never needed record mode, the Rec button, or
an armed input — and the W17 capture path is never at risk, because nothing about it changes.

This is now a CLAUDE.md gotcha, because the general shape recurs: *an engine behaviour reachable only through
one API is not necessarily a property of that API.* Read what the API does before designing around it.

## What shipped

### The planner — pure, engine-free, exhaustively gated

`forge::capture::planCountIn(currentBeat, countInBeats)` in a new header-only
`src/engine/CaptureCountIn.h` (the `forge::meter::advanceMeterHold` / `forge::midiedit::*` precedent: keep the
decision Component-free so a gate can exhaust it with no device and no transport).

| input | output | why |
|---|---|---|
| `(12, 0)` | inactive, preroll = capture = 12 | no count-in configured → arm now |
| `(16, 8)` | preroll 8, capture 16 | with room, the transport jumps **back** by exactly the count-in and capture keeps its beat |
| `(0, 8)` | preroll 0, capture **8** | there is no room to roll before beat 0, so the **capture point moves later** rather than the count-in being silently shortened — the user always gets the full count they asked for, and the transport still starts where it was parked |
| `(4, 8)` | preroll 0, capture 8 | same clamp, mid-case |

Forge deliberately drops the engine's `+ 0.5` beat fudge: that exists only to align the record click *range*,
and without it the count math stays whole-numbered and assertable.

### The state machine

`ProjectSession` gained a `countingIn` state alongside `capturing`, and the two are **mutually exclusive** —
while counting in, the sampler tick does not run at all. That is what guarantees no span can open before the
downbeat, without `performanceCaptureTick` needing to know a count-in exists.

- **Arm** with a count-in from a stopped transport → click forced on (a silent count-in is useless, and the
  user's setting is remembered), transport repositioned to `prerollBeat` and rolled with a plain `play(false)`.
- **Downbeat** → `capturing = true`, click restored to exactly what the user had.
- **Cancel mid-count-in** → disarm, restore the click, and **stop the transport Forge started** — leaving it
  rolling would be a side effect the user never asked for.
- `isPerformanceCaptureArmed()` reads **true throughout**, so the Capture toggle stays lit instead of popping
  back off for the length of the pre-roll; `isCountingIn()` exposes the pending state and the status strip
  says *"Counting in — capture arms on the downbeat"*.

### Two skip paths that protect existing behaviour

- **Transport already rolling** → arm at once. Repositioning would cut off whatever is playing, and a user who
  is already playing wants capture *now*.
- **No count-in configured** → byte-identical to pre-B4: arm now and **never touch the transport**. That is
  W17's passive-observer contract, and it has its own gate leg precisely because this wave is the first thing
  that ever gave `startPerformanceCapture` a reason to touch the transport at all.

### No new UI

It reads `Edit::getNumCountInBeats()` — the same value `record()` uses — so the existing transport-bar
count-in selector serves record and capture alike. One setting, one answer, nothing to keep in sync.

## Gate — `--selftest-countin` (floor 49 → 50)

19 legs: five on the pure planner, fourteen on the state machine. Fully synchronous — the gate positions the
transport itself rather than waiting on real time. `countInBeats` is read from the engine (4 at the default
4/4), never hardcoded.

**Negative controls (each run, then reverted):**

| mutation | effect |
|---|---|
| drop the beat guard in `countInTick` so it arms instantly | `stillCountingBeforeBeat` → 0, **alone** |
| make `startPerformanceCapture` roll the transport on the no-count-in path | `noCountInLeavesTransport` → 0, **alone** |

So both load-bearing rules — *the count-in actually waits* and *W17's passive-observer contract is intact* —
are independently pinned, not merely co-asserted.

## The W17 footgun, re-checked

The packet asked to re-verify that `LaunchHandle::getPlayedRange()` span timing is unaffected. It is:

- `performanceCaptureTick` is **unchanged** — it simply starts later.
- Spans remain absolute Edit beats, so a captured performance still stamps where it actually played.
- `LaunchHandle::nudge` / `setLooping` / `playSynced` remain uncalled, so the reseal heuristic's precondition
  (a changed `startBeat` always means a new play) still holds.

`--selftest-capture`, which exercises the real launch→span→stamp path, stays green.

## Verification

- Build **clean (0 warnings)**, MSVC Debug.
- **50/50 selftest floor.**
- **12/12 screenshots** — no new UI state (the count-in is transport behaviour plus a status line).

---

# Wave 28 — the LCD count-in readout (W27's follow-up round)

> **There is no `wave-28-*.md`** — W28's record lives here, appended to the wave it closes out (the same
> pattern W23's follow-up round used). ⚠ Its commit subject reads `[W27 f/u]`, predating the W28 label;
> the docs use **W28** throughout.

> W27 shipped with one documented limit: the count-in was **audible but not shown**, because the LCD's
> count-in face latched only on a *record* rising edge (W04a). Closed immediately after, in the same
> territory. No new gate — both affected gates were extended in place, so the floor stays **50**.

## The model change is one OR

`LcdModel` already derived the count-in digit from the **click grid**:
`firstClickBeat = ceil(punchBeat − N)`, `digit = floor(currentBeat) − firstClickBeat + 1`. The capture
pre-roll rolls the *ordinary* transport with the *ordinary* click, so its clicks land on that same whole-beat
grid — which means the derivation needed no change at all, only a second way to switch it on:

```cpp
const bool recordCountIn  = in.recording && in.startedFromStopped && in.countInTotal > 0
                            && in.positionSeconds < in.punchTimeSeconds;
const bool captureCountIn = in.captureCountIn && in.countInTotal > 0
                            && in.currentBeat < in.punchBeat;

if (recordCountIn || captureCountIn) { /* …shared digit maths, verbatim… */ }
```

The one real difference: a capture has **no punch time**, because nothing is recording. The arm beat stands in
for it (`punchBeat`), and the caller — which already owns the "is it running" decision via
`ProjectSession::isCountingIn()` — supplies the flag. A mid-beat arm point therefore behaves exactly like a
mid-beat punch, which the W04a QC pass had already proved was the case the naive distance form gets wrong.

## Wiring

`LcdDisplay` gained a `queryCaptureCountIn` seam returning `{active, armBeat}` — the LCD still knows nothing
about `ProjectSession`, exactly like the existing tempo/sig seams. The beat total is latched on the capture
count-in's **rising edge**, mirroring the record path, so `getNumCountInBeats()` (a PropertyStorage read)
stays off the 25 Hz poll.

## The gating decision worth recording

Ten new legs went on `--selftest-lcd` (the pure model): the click-grid table for an aligned arm beat, the
same table for a **mid-beat** arm beat, and two "must stay off" cases. Those legs deliberately set
`recording = false` **and a positive `positionSeconds`** — an input the record path's seconds guard would
veto — so they prove the capture face is genuinely not gated on record mode.

But a pure model cannot see whether the seam is *connected*. So three more legs went on `--selftest-countin`,
driving the real `LcdDisplay::pollNow()` through the shell's actual binding, asserting digit **1** at the
first click beat (proving the arm beat arrived correctly, not merely that something lit up), the final digit
at the last click beat, and the face going dark once capture arms.

**The negative control makes the split concrete.** Leaving `lcd.queryCaptureCountIn` unwired:

| gate | result |
|---|---|
| `--selftest-lcd` | **fully green**, `checksFailed=0` — the model is fine; it simply cannot see wiring |
| `--selftest-countin` | `lcdCountInLit` → 0, `lcdDigitAdvances` → 0, **FAIL** |

That is the standing *"a UI seam a gate can't see can ship unwired"* lesson demonstrated rather than asserted
— and the reason the wiring legs live on the engine-backed gate instead of the model one.

## Verification

Build **clean (0 warnings)** · **50/50 floor** (no new gate — `--selftest-lcd` and `--selftest-countin` both
extended in place) · **12/12 screenshots** (the count-in face already had a screenshot state, driven through
`LcdDisplay::setDemoState`).
