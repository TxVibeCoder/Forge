# Logging in Forge

The full frozen spec is `docs/devlog/logging-design.md`. This is the working doc: the principle, a
cheat-sheet, and the checklist you run for every new feature.

---

## Principle

> **Every fallible operation either succeeds or leaves a diagnostic.** A user-visible operation that
> can fail must, on failure, produce exactly one log line at the right severity — no silent `return
> false`, no swallowed `nullptr`, no ignored `Result`. Logs go to one process-wide file
> (`%APPDATA%\Forge\logs\forge.log`) and always echo to stderr so headless selftests surface them.

Four levels, most-severe first:
- **ERROR** — a user-visible operation failed or an invariant was violated (save failed, plugin
  wouldn't load, clip slot unresolved).
- **WARN** — recoverable/degraded, execution continues (couldn't delete a stale file, ensure-track
  returned false but we press on).
- **INFO** — notable lifecycle events (startup banner, run mode, export begin/end, selftest phases).
- **DEBUG** — developer diagnostics. **Compiled out in Release** — free to be verbose.

---

## Cheat-sheet

```cpp
#include "core/Log.h"   // src/ is on the include path

// Build the message by + concatenation. One juce::String argument. No printf, no <<.
FORGE_LOG_ERROR ("Failed to save project to " + file.getFullPathName());
FORGE_LOG_WARN  ("Track count mismatch: " + juce::String (live) + " vs " + juce::String (cached));
FORGE_LOG_INFO  ("Export begin: " + juce::String (clipCount) + " clips");
FORGE_LOG_DEBUG ("Scanned " + juce::String (n) + " input devices");   // no-op in Release
```

- Wrap non-`juce::String` values with `juce::String (x)`. Paths: `file.getFullPathName()`.
- One line per failure. Don't also `FORGE_LOG_INFO` the success — success is the default, silence is
  correct.
- Getting the log file path (for a "logs are here" message or a selftest report):
  `forge::log::getLogFile()`.

---

## Hard rules (do not break these)

1. **Never log on the audio / real-time thread.** `write()` locks, allocates a `juce::String`, and
   does file I/O. Forge has no audio-callback code today; the rule is *never introduce one*.
2. **Never log per-tick in a poll or paint.** The 25 Hz `SessionView::timerCallback`, the 28 Hz
   `MixerView` meter poll, `TransportBar`'s 25 Hz poll, and every `*::paint` are hot paths.
   - **Only** allowed poll log: an **edge-triggered, one-shot** event (e.g. the "track count
     mismatch → rebuilding" case). Gate it with a member sentinel so it fires only when the value
     *changes*, at `warn`, never `debug`:
     ```cpp
     if (live != lastLoggedTrackCount) {
         FORGE_LOG_WARN ("Track count mismatch in poll: " + juce::String (live)
                         + " live vs " + juce::String (cached) + " (rebuilding)");
         lastLoggedTrackCount = live;
     }
     ```
3. **Autosave: log failure only.** `onEditMutated -> session.save()` fires on every note draw / clip
   move. Log `FORGE_LOG_ERROR` **only when `save() == false`**; never log the success path (it would
   flood the file). Reserve INFO save logging for explicit user Save / Save-As. If autosave keeps
   failing, gate the error so it logs once per failure transition, not per mutation.
4. **In `write()`/crash handler internals, use `juce::Logger::outputDebugString`, never
   `writeToLog`.** `writeToLog` routes back through our installed logger → infinite recursion.

---

## New-feature checklist

For any new feature that touches disk, the engine, plugins, devices, or the edit graph:

- [ ] **Find the fallible seams.** Every call that returns `bool`, a possibly-null pointer, a
      `juce::Result`, or an error out-param `String&`. Each is a seam.
- [ ] **Pick the level** per the definitions above: user-visible failure → ERROR; recoverable →
      WARN; lifecycle → INFO; dev-only detail → DEBUG.
- [ ] **Log at the point of failure**, not three call frames up. Include the *why*: the path, the
      track/scene index, the engine error string.
- [ ] **Do not swallow.** Replace `if (! ok) return {};` with
      `if (! ok) { FORGE_LOG_ERROR (...); return {}; }`. Same for ignored return values —
      `session.save();` becomes `if (! session.save()) FORGE_LOG_ERROR (...);`.
- [ ] **Check the thread.** Message thread or a (present/future) background worker → fine. Audio/RT
      or a per-tick poll/paint → do NOT log (see hard rules). If it's a poll edge event, gate it.
- [ ] **No secrets, but paths are OK in the file.** The file lives under `%APPDATA%` and carries
      absolute paths + the command line by design (it's a local diagnostic). Don't dump buffer
      contents or credentials.
- [ ] **Selftest surfacing.** If the feature has a `--selftest*` path, add INFO at phase boundaries
      and ERROR/WARN on the failure flags the harness already tracks — stderr echo makes them visible
      headless.
- [ ] **Release check (belt-and-suspenders).** Confirm no `FORGE_LOG_DEBUG` string survives a Release
      build and that no new log call landed on a hot path.

---

## Where things live

- Contract header: `src/core/Log.h` (frozen — see `docs/devlog/logging-design.md`).
- Implementation: `src/core/Log.cpp` (listed in `CMakeLists.txt target_sources`, after `src/main.cpp`).
- Runtime file: `%APPDATA%\Forge\logs\forge.log` (rolls once to `forge.log.1` at 1 MiB).
- Install/teardown: first line of `ForgeApplication::initialise()`, last line of `shutdown()`.
- The legacy selftest report (`%TEMP%\forge_phase0_selftest.log`) is unchanged — the logger is
  additive.
