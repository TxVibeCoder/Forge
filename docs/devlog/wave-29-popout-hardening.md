# Wave 29 — `--selftest-popout`: hardening, not a fix

> **Read this heading literally.** The flake was **never reproduced**, so nothing here is a confirmed fix. What
> shipped is (a) removal of a genuinely fragile pattern that is wrong on its own merits, and (b) the
> instrumentation that will make the *next* occurrence diagnosable. Claiming more than that would be worse than
> claiming nothing.

## What was known going in

W25's floor run reported `--selftest-popout` FAIL exactly once. Everything about it was thin:

- 6 PASS / 1 FAIL on the same binary, plus PASS on the pre-change baseline.
- The gate's own log showed a **normal** run — `beginPopoutSelftest` at 07:46:15.212, report written 309 ms
  later — with **no `WARN`/`ERROR`**. So a leg assertion flipped; nothing crashed or hung.
- **Which** leg was unknown, because every gate writes to the same `%TEMP%\forge_phase0_selftest.log` and the
  next gate in the floor had already overwritten it.

## The reproduction attempt

65 dedicated runs, in the two configurations that could plausibly differ:

| configuration | runs | result |
|---|---|---|
| `--selftest-popout` isolated, back to back | 40 | **40 PASS** |
| `--selftest-tray` → `--selftest-popout` (the floor's actual ordering, in case device/state contention from the preceding gate mattered) | 25 | **25 PASS** |

**0 failures.** So the honest position is: unreproduced, undiagnosed, and therefore unfixable by direct means.

## What shipped anyway, and why each is defensible

### 1. The fixed sleep became a condition wait

`beginPopoutSelftest` ended with `startTimer (300)` and the timer callback went straight to
`finishPopoutSelftest()`. But the thing it was waiting for — `restoreMixer` / `restorePianoRoll` each deferring
their `popout.reset()` to a `MessageManager::callAsync` — has a **directly checkable** completion condition
(`mixerPopout == nullptr && pianoRollPopout == nullptr`).

Waiting a fixed duration for an event you can simply *observe* is a race by construction, independent of
whether it caused this particular failure. It now polls at 25 ms up to ~2 s and proceeds when the condition
holds. On timeout it logs a `WARN` and **verifies anyway**, so a genuine hang still produces a FAIL report with
the offending legs instead of a mystery.

Side benefit: the common case got ~6–12× faster, because the resets are one `callAsync` away.

### 2. A failure now names its own legs in the persistent log

The root reason the original failure escaped diagnosis was the shared report path. The gate now, **on failure
only**, extracts the `=0` field names from the report it just built and writes them to the persistent log
(`%APPDATA%\Forge\logs\forge.log`) — which is append-only with rollover and therefore survives the remaining 35
gates of a floor run. It also logs how long the condition wait actually took.

Verified by inducing a failure (`poNoGhostOverlay = false`, then reverted):

```
ERROR [main.cpp:4546] Popout selftest FAILED legs: noGhostOverlay (waited 50 ms for the deferred resets)
```

## A datum worth keeping

That induced run also measured the real thing: **the deferred resets complete in ~25–50 ms.** The old 300 ms
sleep therefore already carried roughly 6× margin, which makes "the message loop was briefly starved past
300 ms" a *less* likely explanation for the original failure than it looked at the time. That doesn't identify
the real cause — but it does narrow it, and it is recorded here so the next investigation starts warmer than
this one did.

## Verification

- Build **clean (0 warnings)**.
- **50/50 floor** · **12/12 screenshots**.
- The gate itself: PASS, now completing its deferred phase in ~50 ms instead of a flat 300 ms.

## If it recurs

The log will name the leg. Start there — and note that the leg names divide cleanly into two classes with very
different implications: the window/visibility/focus legs (`mixerWindowSeen`, `rollWindowSeen`,
`noGhostOverlay`, `mixerVisibleAfter`, `rollVisibleAfter`) point at OS window-manager state, while the
undo/redo routing legs (`keyRoutedToShell`, `undoFiredThroughPopoutKey`, `redoFiredThroughPopoutKey`) would
point at the FourOsc redo-wipe defect's blast radius instead — which is BACKLOG item 0 and a different problem
entirely.
