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
