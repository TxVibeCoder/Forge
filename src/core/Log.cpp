/*
    src/core/Log.cpp — implementation of the Forge logging + error-handling facility.

    See src/core/Log.h for the frozen public contract and docs/devlog/logging-design.md
    for the full design (this file implements §3–§7).

    THREAD RULES (design §5):
      - ALLOWED:   the message/UI thread (primary) and background worker threads.
      - FORBIDDEN: the audio / real-time thread, and the 25 Hz / 28 Hz UI polls per-tick.
                   write() takes a lock, allocates a juce::String, and does buffered file
                   I/O — all real-time-hostile. Never introduce a FORGE_LOG_* call on an
                   RT/audio-callback path or inside a per-tick paint/timer.
      - The internal CriticalSection is held only for a single buffered stream write plus,
        on the rare size-cap boundary, one file move. flush() is NEVER called under the
        lock (see write()).
*/

#include "core/Log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace forge::log
{

namespace
{
    //==============================================================================
    // File-local state (single file-scope instance). Guarded by logLock except for
    // `installed`, which is atomic so isInstalled() is a lock-free, race-free read.
    //==============================================================================

    constexpr juce::int64 kMaxLogBytes = 1 * 1024 * 1024; // 1 MiB active-file cap
    constexpr const char* kRolledSuffix = ".1";           // single rolled generation: forge.log.1

    std::atomic<bool>                        installed { false };
    juce::CriticalSection                    logLock;
    std::unique_ptr<juce::FileOutputStream>  stream;                  // nullptr => file sink unavailable
    juce::File                               logFile;
    juce::Logger*                            previousLogger = nullptr; // captured at install, restored at shutdown
    juce::int64                              bytesWritten   = 0;       // tracked for the size-cap rollover

    //==============================================================================
    /** juce::Logger subclass so juce::Logger::writeToLog output (and JUCE's own DBG) is
        captured. MUST stay a file-scope/static object: juce::Logger does NOT own the
        pointer, and ~Logger() asserts currentLogger != this. shutdown() calls
        setCurrentLogger(previousLogger) before this static is destroyed at process exit,
        so the assertion never fires. Do NOT "improve" this to new/unique_ptr. */
    struct ForgeJuceLogger : public juce::Logger
    {
        void logMessage (const juce::String& message) override
        {
            // JUCE gives us no severity, so route at info level with a synthetic source.
            write (Level::info, "juce", 0, message);
        }
    };

    ForgeJuceLogger forgeJuceLogger;

    //==============================================================================
    /** Extracts the basename of a full source path. Scans from the end for '/' or '\\'
        (handles both; __FILE__ is Windows-backslashed under MSVC). */
    juce::String basename (const char* sourceFile)
    {
        if (sourceFile == nullptr)
            return {};

        const juce::String full (sourceFile);
        const int lastSlash     = full.lastIndexOfChar ('/');
        const int lastBackslash = full.lastIndexOfChar ('\\');
        const int cut = juce::jmax (lastSlash, lastBackslash);

        return cut >= 0 ? full.substring (cut + 1) : full;
    }

    /** Short tag identifying the calling thread WITHOUT locking the MessageManager. */
    juce::String threadTag()
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            if (mm->isThisTheMessageThread())
                return "msg";

        const auto id = reinterpret_cast<std::uintptr_t> (juce::Thread::getCurrentThreadId());
        return "bg#" + juce::String::toHexString ((juce::int64) id);
    }

    /** Builds the one-line record (with trailing '\n'), OUTSIDE any lock. */
    juce::String formatLine (Level level,
                             const char* sourceFile,
                             int sourceLine,
                             const juce::String& message)
    {
        const auto now = juce::Time::getCurrentTime();

        // "%Y-%m-%d %H:%M:%S." then zero-padded milliseconds (0–999) from getMilliseconds().
        juce::String ts = now.formatted ("%Y-%m-%d %H:%M:%S.")
                            + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);

        // <LEVEL>: fixed-width 5 chars for column alignment.
        juce::String levelStr = juce::String (levelTag (level)).paddedRight (' ', 5);

        // <file>[:<line>]: basename only; "juce" printed as-is with no line number;
        // line omitted (with the colon) when sourceLine <= 0.
        juce::String location;
        if (juce::String (sourceFile != nullptr ? sourceFile : "") == "juce")
            location = "juce";
        else
        {
            location = basename (sourceFile);
            if (sourceLine > 0)
                location += ":" + juce::String (sourceLine);
        }

        return ts + " " + levelStr + " [" + location + "] (" + threadTag() + ") " + message + "\n";
    }

    //==============================================================================
    /** Rolls the active log once when the size cap is hit. Called under logLock right
        after a successful write when bytesWritten >= kMaxLogBytes. Best-effort: move /
        delete failures (file locked by a viewer) are swallowed; if reopen fails the sink
        goes null and logging degrades to stderr + debugger. No exceptions escape. */
    void rotateIfNeeded()
    {
        if (stream == nullptr || bytesWritten < kMaxLogBytes)
            return;

        stream->flush();
        stream.reset();

        const juce::File rolled (logFile.getFullPathName() + kRolledSuffix); // forge.log.1
        rolled.deleteFile();          // best-effort
        logFile.moveFileTo (rolled);  // best-effort

        stream = std::make_unique<juce::FileOutputStream> (logFile);
        if (stream->failedToOpen())
            stream.reset();

        bytesWritten = 0;
    }

    //==============================================================================
    /** Crash handler: matches juce::SystemStats::CrashHandlerFunction = void (*)(void*).
        Runs in a crashed/undefined state — does the MINIMUM, takes NO logLock (the
        crashing thread may already hold it), best-effort persistence. */
    void forgeCrashHandler (void* /*platformSpecificData*/)
    {
        // 1) FIRST: a FIXED, allocation-free banner to stderr, so the "crashed" marker
        //    lands even if any heap-touching call below hangs.
        static const char* kBanner = "\nFATAL [crash] (crash) Application crashed\n";
        fputs (kBanner, stderr);
        fflush (stderr);

       #if JUCE_DEBUG
        juce::Logger::outputDebugString (kBanner);
       #endif

        // 2) THEN attempt the heap-touching backtrace. This MAY hang under
        //    heap-corruption crashes; that is accepted and documented (best-effort). No
        //    timestamp — computing one calls Time::formatted() which allocates/touches locale.
        const juce::String backtrace = juce::SystemStats::getStackBacktrace();
        fputs (backtrace.toRawUTF8(), stderr);
        fflush (stderr);

        // 3) Best-effort append to the log FILE (not the live stream, which may be
        //    mid-write). Uses the file-scope logFile captured at install(). After
        //    shutdown() logFile is empty, so this is skipped — the handler is inert
        //    post-teardown.
        const juce::File f = logFile; // plain copy; do not lock
        if (f != juce::File())
            f.appendText (juce::String (kBanner) + backtrace + "\n", false, false, "\n");
    }

} // anonymous namespace

//==============================================================================
const char* levelTag (Level level) noexcept
{
    switch (level)
    {
        case Level::error: return "ERROR";
        case Level::warn:  return "WARN";
        case Level::info:  return "INFO";
        case Level::debug: return "DEBUG";
    }

    return "INFO"; // unreachable; keeps the contract "never returns nullptr"
}

//==============================================================================
void write (Level level,
            const char* sourceFile,
            int sourceLine,
            const juce::String& message)
{
    // 1) Build the formatted line — OUTSIDE the lock.
    const juce::String line = formatLine (level, sourceFile, sourceLine, message);

    // 2) Debugger echo (debug builds only). Use outputDebugString DIRECTLY, NEVER
    //    writeToLog — writeToLog routes through our installed ForgeJuceLogger, which
    //    calls write() again => infinite recursion.
   #if JUCE_DEBUG
    juce::Logger::outputDebugString (line.trimEnd());
   #endif

    // 3) stderr echo — unconditional (kept even in Release) so headless --selftest*
    //    runs surface logs with no console attached.
    fputs (line.toRawUTF8(), stderr);
    fflush (stderr);

    // If not installed, steps 2–3 still ran (debugger + stderr); the file sink is skipped.
    if (! installed.load())
        return;

    bool flushAfter = false;
    {
        const juce::CriticalSection::ScopedLockType sl (logLock);

        if (stream != nullptr)
        {
            const char* utf8   = line.toRawUTF8();
            const size_t nBytes = (size_t) line.getNumBytesAsUTF8();
            stream->write (utf8, nBytes);      // buffered copy into the FileOutputStream
            bytesWritten += (juce::int64) nBytes;
            rotateIfNeeded();                  // runs under the lock only at the size-cap boundary

            // error/warn get a crash-survivable immediate flush; info/debug rely on the
            // buffered write + the flush on the next warn/error or on shutdown().
            flushAfter = (level <= Level::warn);
        }
    }

    // Do NOT hold logLock across the blocking flush().
    if (flushAfter)
    {
        const juce::CriticalSection::ScopedLockType sl (logLock);
        if (stream != nullptr)
            stream->flush();
    }
}

//==============================================================================
void install (const juce::String& appName,
              const juce::String& appVersion,
              const juce::String& commandLine,
              const juce::String& modeDescription)
{
    bool sinkFailed = false;

    {
        const juce::CriticalSection::ScopedLockType sl (logLock);

        if (installed.load())
        {
            // Already installed — emit a WARN (after unlock) and return.
            // Do it inline here to avoid re-locking; write() handles its own state.
            const juce::CriticalSection::ScopedUnlockType su (logLock);
            FORGE_LOG_WARN ("forge::log::install() called more than once — ignoring the second call");
            return;
        }

        logFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("Forge")
                      .getChildFile ("logs")
                      .getChildFile ("forge.log");

        // Ensure the parent directory exists; on failure leave the sink null.
        if (! logFile.getParentDirectory().createDirectory())
        {
            sinkFailed = true;
        }
        else
        {
            // Pre-roll: if an inherited file is already at/over the cap, roll it once now
            // so this launch starts within budget.
            if (logFile.existsAsFile() && logFile.getSize() >= kMaxLogBytes)
            {
                const juce::File rolled (logFile.getFullPathName() + kRolledSuffix);
                rolled.deleteFile();         // best-effort
                logFile.moveFileTo (rolled); // best-effort
            }

            // FileOutputStream opens for append and seeks to end by default (verified
            // against juce_FileOutputStream.h — no setPosition needed).
            stream = std::make_unique<juce::FileOutputStream> (logFile);
            if (stream->failedToOpen())
            {
                stream.reset();
                sinkFailed = true;
            }
            else
            {
                bytesWritten = logFile.getSize();
            }
        }

        previousLogger = juce::Logger::getCurrentLogger();
        juce::Logger::setCurrentLogger (&forgeJuceLogger);
        juce::SystemStats::setApplicationCrashHandler (&forgeCrashHandler);

        installed.store (true);
    }

    // Banner (INFO), written after the lock is released.
    FORGE_LOG_INFO ("=== " + appName + " " + appVersion + " ===");
    FORGE_LOG_INFO ("Command line: " + (commandLine.isEmpty() ? juce::String ("(none)") : commandLine));
    FORGE_LOG_INFO ("Run mode: " + modeDescription);
    FORGE_LOG_INFO ("Log file: " + logFile.getFullPathName()
                        + (sinkFailed ? " (file sink UNAVAILABLE — stderr/debugger only)"
                                      : " (file sink active)"));

    if (sinkFailed)
        FORGE_LOG_ERROR ("Log file sink could not be opened at "
                             + logFile.getFullPathName()
                             + " — logging degraded to stderr + debugger output only");
}

//==============================================================================
void shutdown()
{
    const juce::CriticalSection::ScopedLockType sl (logLock);

    if (! installed.load())
        return;

    // Closing line written directly to the stream (lock held) before flipping state.
    if (stream != nullptr)
    {
        const juce::String line = "=== logging shutdown ===\n";
        stream->write (line.toRawUTF8(), (size_t) line.getNumBytesAsUTF8());
    }

    juce::Logger::setCurrentLogger (previousLogger);
    previousLogger = nullptr;

    if (stream != nullptr)
        stream->flush();
    stream.reset();
    bytesWritten = 0;

    // Clear the crash handler's file target so a post-shutdown crash degrades cleanly
    // (the handler self-guards on an empty logFile). The handler stays registered — JUCE
    // offers no unregister API — but is now inert.
    logFile = juce::File();

    installed.store (false);
}

//==============================================================================
bool isInstalled() noexcept
{
    return installed.load();
}

//==============================================================================
juce::File getLogFile()
{
    const juce::CriticalSection::ScopedLockType sl (logLock);
    return logFile;
}

} // namespace forge::log
