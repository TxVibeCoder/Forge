# Forge Logging & Error-Handling — Frozen Design

Status: **FROZEN CONTRACT**. Build agents implement `src/core/Log.h` / `src/core/Log.cpp`
exactly as specified below, wire it into `src/main.cpp`, then backfill the call sites in the
table at the end. Every verified adversarial issue has been folded in; corrections are called
out inline with `[FIX]`.

---

## 1. Principle

One process-wide file log under `%APPDATA%\Forge\logs\forge.log`, plus an unconditional stderr
echo so headless `--selftest*` runs surface diagnostics with no console attached. Four levels
(`error`/`warn`/`info`/`debug`); `debug` compiles to a true no-op in Release. Ergonomic, safe
macros — the caller builds a `juce::String` by `+` concatenation and passes it as one argument.
Thread-safe for the message thread and background threads; **forbidden** on the audio/RT thread
and in the 25 Hz / 28 Hz UI polls per-tick.

---

## 2. Frozen header — `src/core/Log.h`

Include as `#include "core/Log.h"` (`src/` is on the include path via
`target_include_directories(Forge PRIVATE src)`).

```cpp
/*
    src/core/Log.h — Forge application-wide logging + error-handling facility (FROZEN CONTRACT).

    Build agents write against THIS header. Include it as:  #include "core/Log.h"
    (src is on the include path via target_include_directories(Forge PRIVATE src)).

    Design goals:
      - One process-wide file log under %APPDATA%/Forge/logs, plus a stderr echo so
        headless --selftest* runs surface their diagnostics.
      - Four levels: error, warn, info, debug. DEBUG compiles to a no-op in Release.
      - ERGONOMIC, SAFE macros: the caller builds a juce::String by concatenation and
        passes it as a single argument. No streaming operator, no printf-format parsing.
      - Thread-safe for the message thread and background threads (export, plugin scan).
        FORBIDDEN on the audio/real-time thread and in the 25 Hz UI poll per-tick.
      - Installs a juce::Logger subclass (via juce::Logger::setCurrentLogger) and a crash
        handler (via juce::SystemStats::setApplicationCrashHandler) at app startup.

    Message construction example:
        FORGE_LOG_ERROR ("Failed to save project to " + file.getFullPathName());
        FORGE_LOG_WARN  ("Track count mismatch: " + juce::String (live) + " vs " + juce::String (cached));
*/

#pragma once

#include <JuceHeader.h>

namespace forge::log
{
    //==============================================================================
    /** Severity levels, most-severe first. Values are stable and may be compared. */
    enum class Level
    {
        error = 0,   // a user-visible operation failed or an invariant was violated
        warn  = 1,   // a recoverable problem; degraded but continuing
        info  = 2,   // notable lifecycle events (startup, mode, export begin/end)
        debug = 3    // developer diagnostics; compiled out in Release
    };

    /** Human-readable tag for a level ("ERROR"/"WARN"/"INFO"/"DEBUG").
        levelTag(Level) never returns nullptr. ("FATAL" is produced only by the internal
        crash-line path and is NOT reachable through this enum.) */
    const char* levelTag (Level level) noexcept;

    //==============================================================================
    /** Installs the process-wide logger and crash handler.

        MUST be called exactly once, as the FIRST statement of
        ForgeApplication::initialise(), on the message thread, before any other
        subsystem is touched. Safe to call when a prior logger exists: it captures
        and restores the previous juce::Logger on shutdown().

        Effects:
          - Opens (creating parent dirs as needed) the rotating log file under
            File::userApplicationDataDirectory / "Forge" / "logs" / "forge.log".
          - Registers this facility via juce::Logger::setCurrentLogger, so juce::Logger::
            writeToLog and JUCE's own DBG output route through us.
          - Registers a crash handler via juce::SystemStats::setApplicationCrashHandler.
          - Writes a banner line.

        If the file cannot be opened, logging still works: it falls back to stderr +
        debugger output only, and an error line is emitted saying so. install() never
        throws and never fails hard.

        @param appName        e.g. app.getApplicationName()
        @param appVersion     e.g. app.getApplicationVersion()
        @param commandLine    the raw command line passed to initialise()
        @param modeDescription short string naming the run mode ("normal",
                               "selftest-record", "screenshot", ...) — logged in the banner. */
    void install (const juce::String& appName,
                  const juce::String& appVersion,
                  const juce::String& commandLine,
                  const juce::String& modeDescription);

    /** Tears down the facility. MUST be called from ForgeApplication::shutdown() on the
        message thread. Restores the previously-installed juce::Logger (or nullptr),
        flushes and closes the file, and clears the crash-handler's file target so a
        post-shutdown crash degrades cleanly. Idempotent; safe if install() was never called. */
    void shutdown();

    /** True between a successful install() and shutdown(). Cheap; callable from any thread
        (backed by a std::atomic<bool>, so a lock-free status probe is race-free). */
    bool isInstalled() noexcept;

    //==============================================================================
    /** The core sink. Prefer the FORGE_LOG_* macros — they capture file/line and
        (for DEBUG) compile out in Release. Callable from the message thread and any
        background thread. NEVER call from the audio/RT thread or the 25/28 Hz polls per-tick.

        Thread-safe (guarded by an internal juce::CriticalSection). If the facility is not
        installed, the message is still echoed to stderr + debugger so early/late logs and
        unit contexts are not lost.

        @param level         severity
        @param sourceFile    __FILE__ (full path; basename is extracted for the line)
        @param sourceLine    __LINE__
        @param message       the fully-formed message (caller concatenated it) */
    void write (Level level,
                const char* sourceFile,
                int sourceLine,
                const juce::String& message);

    //==============================================================================
    /** Path to the active log file (empty if file logging is unavailable). Message-thread
        friendly; intended for surfacing "logs are here" to the user or selftest reports. */
    juce::File getLogFile();

} // namespace forge::log

//==============================================================================
// Ergonomic macros. Each takes a single juce::String expression (build it with +).
// __FILE__ / __LINE__ are captured at the call site.
//
// DEBUG is a genuine no-op in Release: the message expression is NOT evaluated, so any
// String concatenation cost disappears. Enable it in Release by defining
// FORGE_LOG_ENABLE_DEBUG=1 on the compile line.
//==============================================================================

#if JUCE_DEBUG
 #ifndef FORGE_LOG_ENABLE_DEBUG
  #define FORGE_LOG_ENABLE_DEBUG 1
 #endif
#else
 #ifndef FORGE_LOG_ENABLE_DEBUG
  #define FORGE_LOG_ENABLE_DEBUG 0
 #endif
#endif

#define FORGE_LOG_ERROR(messageExpr) \
    ::forge::log::write (::forge::log::Level::error, __FILE__, __LINE__, (messageExpr))

#define FORGE_LOG_WARN(messageExpr) \
    ::forge::log::write (::forge::log::Level::warn,  __FILE__, __LINE__, (messageExpr))

#define FORGE_LOG_INFO(messageExpr) \
    ::forge::log::write (::forge::log::Level::info,  __FILE__, __LINE__, (messageExpr))

#if FORGE_LOG_ENABLE_DEBUG
 #define FORGE_LOG_DEBUG(messageExpr) \
    ::forge::log::write (::forge::log::Level::debug, __FILE__, __LINE__, (messageExpr))
#else
 // No-op in Release. The do/while(false) keeps macro-as-statement semantics (works after
 // an if without braces, requires a trailing semicolon) while never evaluating messageExpr.
 #define FORGE_LOG_DEBUG(messageExpr) do { } while (false)
#endif
```

---

## 3. `src/core/Log.cpp` behavior

Free functions declared in `Log.h` over a file-local anonymous-namespace state struct. No
public class is exposed; the `juce::Logger` subclass is an implementation detail.

### State (anonymous namespace, single file-scope instance)
- `std::atomic<bool> installed { false }` — `[FIX: threading/isInstalled race]` an atomic, so
  `isInstalled()` is a lock-free, race-free read. Written under `logLock` at install/shutdown;
  read anywhere.
- `juce::CriticalSection logLock` — guards the stream and rotation.
- `std::unique_ptr<juce::FileOutputStream> stream` (nullptr => file sink unavailable).
- `juce::File logFile`.
- `juce::Logger* previousLogger = nullptr` (captured at install, restored at shutdown).
- `int64 bytesWritten = 0` (tracked for the size-cap rollover).
- A **static** `juce::Logger` subclass `ForgeJuceLogger` whose `logMessage(const String&)` forwards
  to `forge::log::write(Level::info, "juce", 0, msg)` so `juce::Logger::writeToLog` output is
  captured. Level::info because JUCE gives us no severity.

  `[FIX: juce-api lifetime — CONFIRMED SAFE]` `ForgeJuceLogger` MUST stay a file-scope/static
  object. `juce::Logger` does **not** own the pointer (`juce_Logger.h:63`), and `~Logger()` asserts
  `currentLogger != this` (`juce_Logger.cpp:44`). Because `shutdown()` calls
  `setCurrentLogger(previousLogger)` before the static is ever destroyed at process exit, the
  assertion never fires. **Do NOT "improve" this to a `new`/`unique_ptr`** — that reintroduces the
  ownership/assert hazard.

### `levelTag(Level)`
Returns `"ERROR"/"WARN"/"INFO"/"DEBUG"` via a switch. A private internal path produces `"FATAL"`
for the crash line only. Never returns `nullptr`.

### Formatting (one line per record)
```
<ts> <LEVEL> [<file>:<line>] (<threadtag>) <message>\n
```
- `<ts>`: `juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S.")` plus zero-padded
  milliseconds. `[FIX: juce-api millisecond accessor]` milliseconds come from
  `juce::Time::getCurrentTime().getMilliseconds()` (returns 0–999, `juce_Time.h:183`), formatted to
  3 digits. **Do NOT use `getMillisecondsSinceEpoch()` — it is not a member of `juce::Time` and
  will not compile.**
- `<LEVEL>`: `levelTag(level)`, fixed-width 5 chars for column alignment.
- `<file>`: BASENAME of `sourceFile` only. Scan from the end for `/` or `\\` (handle both, `__FILE__`
  is Windows-backslashed under MSVC) and take the tail. If `sourceFile == "juce"`, print it as-is
  with no line number.
- `<line>`: the integer; omitted (with the colon) when `sourceLine <= 0`.
- `<threadtag>`: computed WITHOUT locking the MessageManager. If
  `juce::MessageManager::getInstanceWithoutCreating() != nullptr` AND `->isThisTheMessageThread()`
  => `"msg"`. Otherwise `"bg#"` + short hex of `juce::Thread::getCurrentThreadId()` (cast the
  `ThreadID` pointer to `uintptr_t`, format with `juce::String::toHexString`). Never assert/block
  if the MessageManager is absent (selftest teardown).

### `write(level, file, line, message)`
1. Build the formatted line — OUTSIDE the lock.
2. Debugger echo: in `JUCE_DEBUG`, call `juce::Logger::outputDebugString(line-without-newline)`.
   `[FIX: juce-api recursion]` Use `outputDebugString` directly, **never `writeToLog`** — `writeToLog`
   routes through our installed `ForgeJuceLogger`, which calls `write()` again => infinite recursion.
   Add a code comment at this line reinforcing that.
3. stderr echo: `fputs(line, stderr); fflush(stderr);` — unconditional, kept even in Release, so
   headless `--selftest*` runs surface logs.
4. Acquire `juce::CriticalSection::ScopedLock` on `logLock`.
5. If `stream != nullptr`: write UTF-8 bytes (`line.toRawUTF8()`, length
   `line.getNumBytesAsUTF8()`), increment `bytesWritten`, then call `rotateIfNeeded()`.
   `[FIX: threading — lock-held flush stall]` Do **not** hold `logLock` across the blocking
   `flush()`. Copy the bytes into the `FileOutputStream` buffer under the lock, then **release the
   lock and call `stream->flush()` only for `level <= warn`** (error/warn get crash-survivable
   immediate flush; info/debug rely on the buffered write + the flush on the next warn/error or on
   `shutdown()`). Rotation (`moveFileTo`/`deleteFile`/reopen) runs inside the lock but only on the
   rare size-cap boundary. Document max lock-hold time as one buffered `write` + (rarely) one file
   move — not an fsync.
6. Release lock (RAII).

If not installed, steps 2–3 still run (debugger + stderr); steps 4–5 are skipped. This is the
"not-installed still echoes" contract.

`[FIX: juce-api stream status]` Everywhere the design refers to checking the stream open status, use
`stream->failedToOpen()` (or `! stream->openedOk()`), **not** `failedToOpenFile()` — that symbol does
not exist on `juce::FileOutputStream` (`juce_FileOutputStream.h:99/105`).

### `install(appName, appVersion, commandLine, modeDescription)`
- `ScopedLock`. If already `installed`, emit a WARN and return.
- `logFile = getSpecialLocation(userApplicationDataDirectory).getChildFile("Forge")
  .getChildFile("logs").getChildFile("forge.log")`.
- Ensure parent dir: `logFile.getParentDirectory().createDirectory()`; on failure leave `stream`
  null and remember to emit a fallback error after unlock.
- Pre-roll: if `logFile` exists and `getSize() >= kMaxLogBytes`, run the roll-once sequence now
  (see rotation) so a launch inheriting a large file starts within budget.
- `stream = std::make_unique<FileOutputStream>(logFile)` (append mode; `FileOutputStream` opens for
  append and seeks to end by default). If `stream->failedToOpen()`, reset to `nullptr`. Seed
  `bytesWritten = logFile.getSize()`.
  `[open question]` A build agent should confirm the `FileOutputStream(File)` ctor +
  `failedToOpen()` in `juce_FileOutputStream.h`; if append is not default, `setPosition(getSize())`
  after open.
- `previousLogger = juce::Logger::getCurrentLogger();` `juce::Logger::setCurrentLogger(&forgeJuceLogger);`
- `juce::SystemStats::setApplicationCrashHandler(&forgeCrashHandler);`
- `installed.store(true);`
- Release lock, then write the banner via `write()` (INFO): app name+version, `commandLine` (or
  "(none)"), `modeDescription`, `logFile` path, and whether the file sink is active. If the sink
  failed to open, also `FORGE_LOG_ERROR` that fact.

### `shutdown()`
- `ScopedLock`. If `! installed`, return.
- Write a closing INFO "logging shutdown" line to the stream directly (lock held) before flipping
  `installed`.
- `juce::Logger::setCurrentLogger(previousLogger); previousLogger = nullptr;`
- `if (stream) stream->flush(); stream.reset(); bytesWritten = 0;`
- `[FIX: threading — post-shutdown crash sink]` **`logFile = juce::File();`** (clear it) so the crash
  handler's own validity guard short-circuits after teardown, matching the stated "degrades
  gracefully if called after shutdown" intent.
- `installed.store(false);`
- The crash handler stays registered (JUCE offers no unregister API) but is now inert because
  `logFile` is empty. See §7.

### `isInstalled()`
Return `installed.load()` — lock-free, race-free (`[FIX]`).

### `getLogFile()`
Return `logFile` under lock (empty `File` if the sink is unavailable).

---

## 4. File location & rotation

- Header: `src/core/Log.h` (resolved via `#include "core/Log.h"`).
- Impl: `src/core/Log.cpp`. **Must be added to CMake.** No glob is used.
  `[verified]` Insert `src/core/Log.cpp` into the `target_sources(Forge PRIVATE ...)` block right
  after `src/main.cpp` (currently `CMakeLists.txt:31`). No CMake change needed for the `.h`
  (`target_include_directories(Forge PRIVATE src)` already covers it, `CMakeLists.txt:52`).
- Runtime file: `File::getSpecialLocation(File::userApplicationDataDirectory) / "Forge" / "logs" /
  "forge.log"` → on Windows `C:\Users\<user>\AppData\Roaming\Forge\logs\forge.log`.
- Rolled generation: `forge.log.1` (single generation).
- Deliberately SEPARATE from the existing selftest report at `%TEMP%\forge_phase0_selftest.log`,
  which is retained unchanged.

### Rotation — dependency-free single-generation size cap
Constants (file-local `constexpr` in `Log.cpp`):
- `kMaxLogBytes  = 1 * 1024 * 1024;`  // 1 MiB active-file cap
- `kRolledSuffix = ".1";`

`rotateIfNeeded()` — called under `logLock` right after a successful write when
`bytesWritten >= kMaxLogBytes`:
1. `stream->flush(); stream.reset();`
2. `rolled = File(logFile.getFullPathName() + kRolledSuffix);`  // forge.log.1
3. `rolled.deleteFile();`  // best-effort
4. `logFile.moveFileTo(rolled);`  // best-effort
5. `stream = std::make_unique<FileOutputStream>(logFile); if (stream->failedToOpen()) stream.reset();`
6. `bytesWritten = 0;`

At most ~2 MiB on disk. Best-effort: move/delete failures (file locked by a viewer) are swallowed;
if reopen fails the sink goes null and logging degrades to stderr+debugger. No exceptions escape.
Append mode + seeded `bytesWritten = logFile.getSize()` enforces the cap across runs.

`[open question]` Debug lines currently route to the file. If dev debug volume bloats the 1 MiB cap,
consider excluding `debug` from the file sink (debugger/stderr only). Left as-is for v1.

---

## 5. Thread rules

**ALLOWED**
- Message/UI thread — the primary caller (ProjectSession via UI, `main.cpp` startup/selftest, all
  view event handlers that are NOT per-tick).
- Background worker threads — kept lock-safe for forward compatibility.

`[FIX: backfill — accurate rationale]` **Today all fallible seams that log run on the message
thread.** Exporter render is synchronous on the message thread (`Exporter.h:11`); PluginScanner
blocks the message thread (`PluginScanner.h:23,44`); BrowserView's scan uses a `TimeSliceThread`
but its logged callbacks arrive on the message thread. So the `bg#<id>` thread-tag path is currently
forward-looking, not exercised. Keep the `CriticalSection` + bg tag anyway (`Exporter.h` says render
"may" move off-thread later) — but do NOT relax the lock on the false premise that everything is
single-threaded today.

**FORBIDDEN (hard rule — build agents must NOT introduce these)**
- The audio / real-time thread. Forge has no audio-callback code of its own today; the rule is
  "never introduce a `FORGE_LOG_*` call on an RT/audio-callback path." `write()` takes a lock,
  allocates a `juce::String`, and does buffered file I/O — all RT-hostile.
- The 25 Hz `SessionView::timerCallback`, the 28 Hz `MixerView` meter poll, and every
  `hotPath=true` paint (`TrackColumnComponent::paint`, `ClipSlotComponent::paint`,
  `VelocityLane::paint`, `ArrangeView` waveform/note paint, `TransportBar` 25 Hz poll) —
  **PER TICK**.
  - Permitted exception: an **edge-triggered, one-shot** log inside a poll (the "track count
    mismatch, rebuilding" case). `[FIX: backfill — pin the guard]` It MUST be gated by a member
    (e.g. `int lastLoggedTrackCount = -1;`) and only emit when the mismatch **value changes**, at
    `warn`, never `debug`, never per-tick. Explicitly forbidden: logging inside the per-track poll
    loop and inside any `hotPath=true` paint.

`[FIX: backfill — autosave rule]` **Autosave guardrail.** `onEditMutated -> session.save()` fires on
every edit mutation (note draw, clip move) at a near-hot rate. Autosave call sites MUST
`FORGE_LOG_ERROR` **only on `save() == false`** (data-loss risk) and MUST NOT log the success path.
Reserve INFO save logging for explicit user Save / Save-As. A persistently-failing autosave should
be gated (log once per failure transition, not per mutation) — see the table.

---

## 6. Init / teardown wiring (`src/main.cpp`)

`install()` is the FIRST executable line of `initialise(const String& commandLine)`, before
`LookAndFeel::setDefaultLookAndFeel` and before constructing `MainComponent`:

```cpp
void initialise (const String& commandLine) override
{
    const auto modeDesc = commandLine.contains ("--screenshot")       ? "screenshot"
                        : commandLine.contains ("--selftest-record")  ? "selftest-record"
                        : commandLine.contains ("--selftest-session") ? "selftest-session"
                        : commandLine.contains ("--selftest")         ? "selftest-playback"
                                                                      : "normal";
    forge::log::install (getApplicationName(), getApplicationVersion(), commandLine, modeDesc);
    FORGE_LOG_INFO ("Forge starting");

    LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    const auto mode = /* existing SelfTest enum parse, unchanged */;
    mainWindow.reset (new MainWindow ("Forge", new MainComponent (engine, mode), *this));

    // [FIX: backfill nit] Engine-device INFO — reuse the EXISTING describeAudioState() helper
    // (main.cpp:1124-1127 already calls engine.getDeviceManager().deviceManager
    //  .getCurrentAudioDevice()->getName() with a null-guard). No new API.
    FORGE_LOG_INFO ("Engine device: " + describeAudioState (engine));
}
```

`[FIX: threading — engine open happens before install()]` `te::Engine engine { ... }` is a
`ForgeApplication` **member**, so its output-only device open runs during app-object construction,
BEFORE `initialise()` and therefore BEFORE `install()`. Consequence: an engine device-open **failure**
cannot be captured. Two acceptable resolutions — a build agent picks one and records it:
- **(A, minimal)** Keep the member engine; the `describeAudioState` INFO above is an explicit
  **post-open snapshot** (comment it as "cannot report open failure"). Additionally, right after
  `install()`, add a startup self-check: if `engine.getDeviceManager().deviceManager
  .getCurrentAudioDevice() == nullptr`, `FORGE_LOG_ERROR ("No output audio device after startup")`.
- **(B, refactor)** Move `engine` to a `std::unique_ptr` constructed inside `initialise()` AFTER
  `install()`, so its startup is logged. Larger `main.cpp` change; out of scope unless the human asks.

`shutdown()`:
```cpp
void shutdown() override
{
    FORGE_LOG_INFO ("Forge shutting down");
    mainWindow = nullptr;
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    forge::log::shutdown();   // restores previous juce::Logger, clears logFile, flushes + closes
}
```

Add `#include "core/Log.h"` to `main.cpp`'s include block (after the existing project includes).

---

## 7. Crash handler

File-scope function matching `juce::SystemStats::CrashHandlerFunction = void (*)(void*)`, registered
in `install()`.

```cpp
static void forgeCrashHandler (void* /*platformSpecificData*/)
{
    // Runs in a crashed/undefined state. Do the MINIMUM. No logLock (the crashing thread may
    // already hold it). Best-effort persistence.

    // 1) FIRST: a FIXED, allocation-free banner to stderr, so the "crashed" marker lands even
    //    if any heap-touching call below hangs. [FIX: crash-handler heap ordering]
    static const char* kBanner = "\nFATAL [crash] (crash) Application crashed\n";
    fputs (kBanner, stderr);
    fflush (stderr);

   #if JUCE_DEBUG
    juce::Logger::outputDebugString (kBanner);
   #endif

    // 2) THEN attempt the heap-touching backtrace. This MAY hang under heap-corruption crashes;
    //    that is accepted and documented (best-effort, not guaranteed). No timestamp — computing
    //    one calls Time::formatted() which allocates/touches locale. [FIX: crash-handler alloc]
    const juce::String backtrace = juce::SystemStats::getStackBacktrace();
    fputs (backtrace.toRawUTF8(), stderr);
    fflush (stderr);

    // 3) Best-effort append to the log FILE (not the live stream, which may be mid-write).
    //    Uses the file-scope logFile captured at install(). After shutdown() logFile is empty,
    //    so this is skipped — the handler is inert post-teardown. [FIX: post-shutdown guard]
    const juce::File f = logFile;   // plain copy; do not lock
    if (f != juce::File())
        f.appendText (juce::String (kBanner) + backtrace + "\n", false, false, "\n");
}
```

`[FIX: crash-handler self-deadlock — folded]` The fixed stderr banner is emitted BEFORE any
allocation, so the crash marker is durable even when `getStackBacktrace()`/`String` allocation
deadlocks against a CRT heap lock held by another thread. The backtrace is **best-effort** — the
design does not promise it is persisted under heap-corruption crashes.

`[open question — human decision]` `juce::SystemStats` has no verified unregister API, so the handler
stays registered after `shutdown()`. It self-guards on the now-empty `logFile` (steps 1–2 still run).
Confirm this is acceptable, or mandate install-once-per-process (no re-install after shutdown).

---

## 8. Selftest integration (additive — existing report retained)

The `%TEMP%\forge_phase0_selftest.log` report (`File::replaceWithText`, then `systemRequestedQuit()`)
is RETAINED verbatim; downstream harness/CI may read it. The logger is purely additive:
- Because `write()` unconditionally `fflush`es stderr, every `FORGE_LOG_*` during a headless
  `--selftest*` run is visible on the console.
- Emit INFO at each selftest phase boundary ("selftest-record: opening input", "armed track 0",
  "captured Ns, peak=<x>"). Failures stored only in flags/lastError today
  (`rcOpenError`, `rcTrackArmed`, `ssClipCreated`, `launchSlot` result) get a
  `FORGE_LOG_ERROR`/`WARN` at the point of failure per the table.
- When the selftest writes its `%TEMP%` report, ALSO log an INFO with the report path + one-line
  pass/fail summary, and `FORGE_LOG_ERROR` if `File::replaceWithText` fails
  (`main.cpp:940 / 1008`, currently silent).
- The screenshot harness logs INFO per PNG and `FORGE_LOG_ERROR` if
  `createOutputStream()`/`writeImageToStream` fails (`main.cpp:1073`, currently silent).
- Append `getLogFile()` to the selftest report text so a reader is pointed at the full log.

---

## 9. Backfill call-site table

Levels reflect the audit's `proposedLevel`. Message text below is the intended `juce::String`
expression — a build agent adapts variable names to what's in scope. **Hot-path rows are marked and
must be gated/one-shot, never per-tick.** Autosave-success paths are explicitly *no-log*.

### 9a. `src/services/files/ProjectSession.cpp`
| Line | Level | Call to insert |
|---|---|---|
| 13 | WARN  | `FORGE_LOG_WARN ("Failed to create project parent directory: " + path.getFullPathName() + " — the edit may not persist correctly")` |
| 15 | ERROR | `FORGE_LOG_ERROR ("Failed to create empty edit in memory")` (guard the null before save at 19) |
| 19 | ERROR | `FORGE_LOG_ERROR ("Failed to save new project to disk at " + file.getFullPathName())` |
| 26/28 | ERROR | `FORGE_LOG_ERROR ("Failed to load project from " + path.getFullPathName() + " — file may be corrupted or unsupported")` |
| 33 | WARN  | `FORGE_LOG_WARN ("Failed to ensure track 0 exists in the loaded project — the edit may be malformed")` |
| 51 | ERROR | `FORGE_LOG_ERROR ("Failed to save project to " + file.getFullPathName())` (on `save()==false`) |
| 61 | ERROR | `FORGE_LOG_ERROR ("Failed to save project to " + file.getFullPathName())` (Save As failure branch) |
| 73/75 | ERROR | `FORGE_LOG_ERROR ("Failed to import audio file " + path.getFullPathName() + " — format may be unsupported")` |
| 94/96 | ERROR | `FORGE_LOG_ERROR ("Failed to create MIDI clip on track " + juce::String (trackIndex))` |
| 414/416 | ERROR | `FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex))` |
| 420 | WARN  | `FORGE_LOG_WARN ("Failed to ensure scene " + juce::String (sceneIndex) + " has a slot on track " + juce::String (trackIndex))` |
| 422/424 | ERROR | `FORGE_LOG_ERROR ("Clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " could not be resolved after grid growth")` |
| 435/437 | ERROR | `FORGE_LOG_ERROR ("Failed to insert MIDI clip into slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex))` |
| 454 | ERROR | `FORGE_LOG_ERROR ("Failed to save project before importing audio (path must serialize relative)")` |
| 458/460 | ERROR | `FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex))` |
| 471/473 | ERROR | `FORGE_LOG_ERROR ("Failed to load audio file " + path.getFullPathName() + " — format may be unsupported")` |
| 481/484 | ERROR | `FORGE_LOG_ERROR ("Failed to insert audio clip into slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex))` |

### 9b. `src/services/export/Exporter.cpp`
| Line | Level | Call to insert |
|---|---|---|
| 61 | WARN  | `FORGE_LOG_WARN ("Failed to create output directory: " + dir.getFullPathName())` |
| 62 | WARN  | `FORGE_LOG_WARN ("Failed to delete existing file " + file.getFullPathName() + " — it may be locked or read-only")` |
| 104/106 | ERROR | `FORGE_LOG_ERROR ("Couldn't initialise the renderer.")` |
| 114 (start) | INFO | `FORGE_LOG_INFO ("Exporting audio: " + juce::String (clipCount) + " clips, " + juce::String (editLengthSeconds) + "s")` (once, before the loop — NOT per iteration) |
| 120 | ERROR | `FORGE_LOG_ERROR ("Export render failed: " + task->errorMessage)` |
| 127 | ERROR | `FORGE_LOG_ERROR ("The render finished but no file was produced.")` |
| 170 | ERROR | `FORGE_LOG_ERROR ("Couldn't create the output folder: " + dir.getFullPathName())` |
| 216 | WARN  | `FORGE_LOG_WARN ("Failed to delete existing stem file " + file.getFullPathName())` |
| 233/235 | WARN  | `FORGE_LOG_WARN ("Couldn't initialize renderer for track '" + trackName + "' — skipping this stem")` |
| 246 | WARN  | `FORGE_LOG_WARN ("Stem render failed for '" + trackName + "': " + task->errorMessage)` |
| 253 | WARN  | `FORGE_LOG_WARN ("Render for track '" + trackName + "' finished but no file was produced")` |

### 9c. `src/engine/*`
| File:Line | Level | Call to insert |
|---|---|---|
| PluginHost.cpp:167/169 | ERROR | `FORGE_LOG_ERROR ("Failed to create plugin '" + displayName + "' — may be corrupted or unsupported")` |
| PluginHost.cpp:212/214 | ERROR | `FORGE_LOG_ERROR ("Failed to create instrument '" + displayName + "' — may be corrupted")` |
| PluginHost.cpp:234 | ERROR | `FORGE_LOG_ERROR ("Failed to ensure default instrument on track — MIDI clips may be inaudible")` |
| RecordController.cpp:84 | ERROR | `FORGE_LOG_ERROR ("Failed to allocate playback context for recording")` |
| RecordController.cpp:116 | WARN | `FORGE_LOG_WARN ("Failed to enable recording on input device '" + deviceName + "'")` |
| RecordController.cpp:117 | ERROR | `FORGE_LOG_ERROR ("Failed to restart playback after arming input — input may not record")` |
| RecordController.cpp:162/164 | ERROR | `FORGE_LOG_ERROR ("Could not disarm input device '" + deviceName + "' from track: " + errorDetail)` |
| RecordController.cpp:171 | ERROR | `FORGE_LOG_ERROR ("Failed to restart playback after disarming input")` |
| EngineHelpers.h:32 | ERROR | `FORGE_LOG_ERROR ("Failed to validate audio file " + path.getFullPathName() + " — format may be unsupported")` |
| EngineHelpers.h:35 | ERROR | `FORGE_LOG_ERROR ("Failed to insert audio clip from " + path.getFullPathName() + " onto track " + juce::String (trackIndex))` |
| EngineHelpers.h:51 | ERROR | `FORGE_LOG_ERROR ("Failed to insert MIDI clip onto track " + juce::String (trackIndex))` |
| EngineHelpers.h:212/216 | ERROR | `FORGE_LOG_ERROR ("Failed to open input device '" + deviceName + "': " + errorDetail + " — attempting output-only restore")` |
| EngineHelpers.h:217 | ERROR | `FORGE_LOG_ERROR ("Recovery: failed to restore output-only device — playback may be unavailable")` |
| EngineHelpers.h:256 | INFO | `FORGE_LOG_INFO ("Initialized synthetic audio device: " + juce::String (inCh) + " in, " + juce::String (outCh) + " out @ " + juce::String (sampleRate) + "Hz")` |
| EngineHelpers.h:268 | ERROR | `FORGE_LOG_ERROR ("Engine refused to switch to synthetic audio device — record selftest cannot proceed")` |

DEBUG-level engine seams (RecordController:108 target failure, PluginScanner scan progress,
EngineHelpers device-scan lines, hosted prepare/dispatch/rescan) get `FORGE_LOG_DEBUG (...)` —
compiled out in Release, so free to be verbose. None are on the audio thread.

### 9d. `src/main.cpp`
| Line | Level | Call to insert |
|---|---|---|
| 42 | WARN | `FORGE_LOG_WARN ("Failed to create temp WAV file for sine wave (createOutputStream returned null)")` |
| 46 | WARN | `FORGE_LOG_WARN ("Failed to create WAV writer for sine wave")` |
| 64 | ERROR | `FORGE_LOG_ERROR ("Failed to write audio samples to WAV file — I/O error or disk full")` |
| 168 | ERROR | `FORGE_LOG_ERROR ("Failed to open or create project file: " + projectFile.getFullPathName())` (on `!editLoaded`) |
| 204 | ERROR | `FORGE_LOG_ERROR ("Failed to save project — I/O error")` (on `save()==false`) |
| 243 | ERROR | `FORGE_LOG_ERROR ("Arm/Disarm failed: " + recorder.getLastError())` |
| 287 | ERROR | `FORGE_LOG_ERROR ("Failed to create MIDI clip in track " + juce::String (trackIndex))` |
| 302 | ERROR | `FORGE_LOG_ERROR ("Failed to import audio file: " + file.getFullPathName())` |
| 533 | ERROR | `FORGE_LOG_ERROR ("Failed to save project — I/O error")` (on `save()==false`) — **autosave/user save: log failure only** |
| 646 | ERROR | `FORGE_LOG_ERROR ("Failed to open project: " + file.getFullPathName())` |
| 669 | ERROR | `FORGE_LOG_ERROR ("Save As failed: " + file.getFullPathName())` |
| 687 | ERROR | `FORGE_LOG_ERROR ("Failed to import audio file: " + file.getFullPathName())` |
| 723/725 | ERROR | `FORGE_LOG_ERROR ("Export failed: " + err)` |
| 758/760 | ERROR | `FORGE_LOG_ERROR ("Stem export failed: " + err)` |
| 769 | ERROR | `FORGE_LOG_ERROR ("Failed to import test tone file for playback selftest")` |
| 777 | ERROR | `FORGE_LOG_ERROR ("Transport null after verifying clip import — engine state error")` |
| 815 | ERROR | `FORGE_LOG_ERROR ("Failed to open recording input: device unavailable or hardware error")` |
| 817 | ERROR | `FORGE_LOG_ERROR ("Failed to arm input to track: " + recorder.getLastError())` |
| 834 | WARN | `FORGE_LOG_WARN ("Record selftest: failed to open recording input: " + rcOpenError)` |
| 859 | WARN | `FORGE_LOG_WARN ("Record selftest: failed to arm input to track 0")` |
| 940 | ERROR | `FORGE_LOG_ERROR ("Failed to write record selftest report to: " + reportFile.getFullPathName())` |
| 959 | ERROR | `FORGE_LOG_ERROR ("Session selftest: failed to create MIDI clip in slot (0,0)")` |
| 963 | ERROR | `FORGE_LOG_ERROR ("Session selftest: failed to launch clip in slot (0,0)")` |
| 1008 | ERROR | `FORGE_LOG_ERROR ("Failed to write session selftest report to: " + reportFile.getFullPathName())` |
| 1048 | WARN | `FORGE_LOG_WARN ("Screenshot harness: failed to create demo MIDI clip at (" + juce::String (t) + "," + juce::String (s) + ")")` |
| 1051 | WARN | `FORGE_LOG_WARN ("Screenshot harness: failed to launch scene 3")` |
| 1073 | ERROR | `FORGE_LOG_ERROR ("Failed to create/write PNG snapshot: " + file.getFullPathName())` |
| initialise / phase INFO | INFO | banner + "Forge starting" + phase boundaries per §8 |

### 9e. `src/ui/session/*`
| File:Line | Level | Note |
|---|---|---|
| SessionView.cpp:42 | ERROR | `FORGE_LOG_ERROR ("Failed to ensure " + juce::String (SessionLayout::numScenes) + " scenes in edit")` |
| SessionView.cpp:263 | ERROR | `FORGE_LOG_ERROR ("Failed to create MIDI clip in slot (" + juce::String (trackIdx) + "," + juce::String (sceneIdx) + ")")` |
| SessionView.cpp:348 | ERROR | `FORGE_LOG_ERROR ("Failed to import audio into slot (" + juce::String (trackIdx) + "," + juce::String (sceneIdx) + ")")` |
| SessionView.cpp:375 | ERROR | `FORGE_LOG_ERROR ("Failed to launch scene " + juce::String (focusScene) + " via keyboard")` |
| SessionView.cpp:377 | ERROR | `FORGE_LOG_ERROR ("Failed to launch slot (" + juce::String (focusTrack) + "," + juce::String (focusScene) + ") via keyboard")` |
| **SessionView.cpp:435–440** | WARN | **HOT PATH (25 Hz). One-shot only.** Gate with member `int lastLoggedTrackCount = -1;` — emit `FORGE_LOG_WARN ("Track count mismatch in poll: " + juce::String (live) + " live vs " + juce::String (cached) + " (rebuilding)")` **only when `live != lastLoggedTrackCount`**, then set `lastLoggedTrackCount = live`. Reset the sentinel to `-1` in `rebuild()`. Never log inside the per-track loop (455–493). |
| TrackColumnComponent.cpp:169/185 | — | **HOT PATH paint. DO NOT LOG.** |
| ClipSlotComponent.cpp:16/131 | — | **HOT PATH paint. DO NOT LOG.** |

### 9f. Other UI (mixer / pianoroll / arrange / detail / browser / transport / plugins / ControlBar)
| File:Line | Level | Note |
|---|---|---|
| MixerView.cpp:684 | ERROR | master volume plugin not found |
| MixerView.cpp:465 | WARN | add plugin to track (may fail async) |
| MixerView.cpp:796 | — | **HOT PATH 28 Hz meter poll. DO NOT LOG per-tick.** |
| BrowserView.cpp:50 | ERROR | `FORGE_LOG_ERROR ("Failed to start file-scanner thread")` |
| BrowserView.cpp:51 | WARN | failed to open directory |
| DetailView.cpp:171 | ERROR | failed to create waveform thumbnail |
| ArrangeView.cpp:308 | ERROR | failed to create waveform thumbnail |
| ArrangeView.cpp:325/453/460 | — | **HOT PATH paint. DO NOT LOG.** |
| TransportBar.cpp:92/93 | — | **HOT PATH 25 Hz poll. DO NOT LOG per-tick.** |
| PianoRollView.cpp:213/234/420 | ERROR/WARN | note-commit failures (not per-frame; on user action) — safe to log |
| VelocityLane.cpp:41 | — | **HOT PATH paint. DO NOT LOG.** |
| ControlBar.cpp:62 | INFO | export menu displayed (optional) |

**Level totals across the whole backfill: ERROR ≈ 52, WARN ≈ 19, INFO ≈ 9, DEBUG ≈ 12** (DEBUG =
engine/UI diagnostics compiled out in Release; the ~10 explicitly-marked hot-path sites are
**no-log** and excluded from these counts).

---

## 10. Open questions for the human
1. **Crash handler after shutdown** — stays registered (no JUCE unregister API); made inert by
   clearing `logFile`. Accept, or mandate install-once-per-process?
2. **Engine device-open failure** — engine is a member constructed before `install()`. Accept the
   post-open snapshot + startup null-device check (option A), or refactor engine to a
   `unique_ptr` built inside `initialise()` (option B)?
3. **Log-file location** — `%APPDATA%\Forge\logs\forge.log` (roaming). Confirm, vs. `%TEMP%` or
   `%LOCALAPPDATA%`.
4. **Debug lines in the file sink** — currently routed to file; exclude from file (debugger-only)
   if dev debug volume bloats the 1 MiB cap?
5. **`.gitignore`** — add `*.log` / `forge.log*` as defense-in-depth (logs carry absolute user
   paths + commandLine) so a redirected stderr dump can't be committed to the public repo.
