# Export — render the current Edit to a WAV file

## What changed

New service `Exporter` that renders the active Edit to a 24-bit stereo WAV on disk,
synchronously on the calling thread. Files added (the only files this task owns):

- `src/services/export/Exporter.h`
- `src/services/export/Exporter.cpp`

No existing files were modified. The shell wiring (Control-bar button + file chooser +
CMake source entry) is documented below for the orchestrator to apply.

## Public API (exact signatures)

In `src/services/export/Exporter.h`:

```cpp
namespace te = tracktion;

namespace Exporter
{
    bool renderEditToWav (te::Edit& edit, const juce::File& outFile, juce::String& error);
}
```

Behaviour:
- Renders the range `[0, edit.getLength()]` (start of edit to end of last clip), all tracks
  mixed to stereo, 24-bit WAV, at the engine device sample rate (falls back to 44100 if the
  device reports < 8000).
- Synchronous/blocking: drives `Renderer::RenderTask::runJob()` to completion on the calling
  thread. No `UIBehaviour` progress bar, no message-loop dependency.
- Returns `true` on success. On failure returns `false` and sets `error`.
- Empty edit (no clips on any audio track, or zero length) returns `false` with
  `"Nothing to export: the project has no audio clips."`.
- Overwrites any existing file at `outFile` (deletes it first). On render failure the partial
  output file is deleted.

## How it works (renderer entry point used)

The render does NOT use `Renderer::renderToFile(...)` because the convenience overloads call
`engine.getUIBehaviour().runTaskWithProgressBar(...)` when `useThread == true`, and the
`useThread == false` overload still uses the engine *default* format + ACID metadata. For full
control (24-bit, WAV specifically, stereo, calling-thread-only) the service builds the params
by hand and runs the task directly — the same low-level pattern the engine itself uses:

1. Build `te::Renderer::Parameters params (edit)` and set:
   `destFile`, `audioFormat = engine.getAudioFileFormatManager().getWavFormat()`,
   `bitDepth = 24`, `sampleRateForAudio`, `blockSizeForAudio = 512`,
   `time = te::TimeRange (te::TimePosition(), edit.getLength())`,
   `tracksToDo = te::toBitSet (te::getAllTracks (edit))`,
   `usePlugins = true`, `useMasterPlugins = true`, `canRenderInMono = false`.
2. `te::TransportControl::stopAllTransports (engine, false, true);`
   `te::Renderer::turnOffAllPlugins (edit);`
   `const te::Edit::ScopedRenderStatus renderStatus (edit, true);`
3. `auto task = te::render_utils::createRenderTask (params, "Forge export", nullptr, nullptr);`
4. `while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain) {}`
5. `te::Renderer::turnOffAllPlugins (edit);` then check `task->errorMessage` and
   `outFile.existsAsFile()`.

APIs verified against the vendored headers under `libs/tracktion_engine`:
- `Renderer::Parameters`, `RenderTask::runJob()`/`errorMessage`, `render_utils::createRenderTask`
  in `model/export/tracktion_Renderer.h`.
- `getAudioTracks`, `getAllTracks`, `toBitSet` in `model/edit/tracktion_EditUtilities.h`.
- `Edit::getLength()` (-> `TimeDuration`), `Edit::ScopedRenderStatus(Edit&, bool)` in
  `model/edit/tracktion_Edit.h`.
- `Engine::getAudioFileFormatManager()` -> `AudioFormatManager::getWavFormat()`,
  `Engine::getDeviceManager().getSampleRate()` in `utilities/tracktion_Engine.h` /
  `audio_files/tracktion_AudioFormatManager.h`.
- `TransportControl::stopAllTransports(Engine&, bool, bool)` in
  `playback/tracktion_TransportControl.h`.
- `Track::getNumTrackItems()` (clip count per track) in `model/tracks/tracktion_Track.h`.
- `TimeRange(Position, Duration)` ctor in `core/utilities/tracktion_TimeRange.h`;
  `TimePosition()` / `TimeDuration()` default ctors + `operator<=` in
  `core/utilities/tracktion_Time.h`.

## How the shell wires this

Three edits, all in files owned by the orchestrator. Verbatim below.

### 1. `CMakeLists.txt` — add the source to `target_sources`

Find the `target_sources(...)` list that already contains
`src/services/files/ProjectSession.cpp` and add, alongside it:

```cmake
    src/services/export/Exporter.cpp
```

(`Exporter.h` is header-only-included; only the `.cpp` needs to be in `target_sources`. The
existing include dirs already cover `src/`, since other code uses `#include "services/..."`.)

### 2. `src/ui/ControlBar.h` — add an Export button + callback

`ControlBar` is a dumb view that forwards intent via `std::function` callbacks. Add an Export
button and an `onExport` callback (additive — no existing signature changes).

In the public callbacks block (currently):
```cpp
    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onAudioSettings;
```
add `onExport`:
```cpp
    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onExport, onAudioSettings;
```

In the private members, alongside the file buttons (currently):
```cpp
    juce::TextButton newBtn { "New" }, openBtn { "Open" }, saveBtn { "Save" },
                     saveAsBtn { "Save As" }, importBtn { "Import" }, audioBtn { "Audio" };
```
add an `exportBtn`:
```cpp
    juce::TextButton newBtn { "New" }, openBtn { "Open" }, saveBtn { "Save" },
                     saveAsBtn { "Save As" }, importBtn { "Import" },
                     exportBtn { "Export" }, audioBtn { "Audio" };
```

### 3. `src/ui/ControlBar.cpp` — register, wire, and lay out the button

In the constructor's `addAndMakeVisible` loop, add `&exportBtn`:
```cpp
    for (auto* b : { &browserBtn, &newBtn, &openBtn, &saveBtn, &saveAsBtn, &importBtn,
                     &exportBtn, &audioBtn, &arrangeBtn, &mixBtn, &drawerBtn })
        addAndMakeVisible (b);
```

After the `importBtn.onClick` wiring, add:
```cpp
    exportBtn.onClick  = [this] { if (onExport) onExport(); };
```

In `resized()`, add `&exportBtn` to the left-hand file-button loop:
```cpp
    for (auto* b : { &newBtn, &openBtn, &saveBtn, &saveAsBtn, &importBtn, &exportBtn, &audioBtn })
    {
        b->setBounds (r.removeFromLeft (60));
        r.removeFromLeft (3);
    }
```

### 4. `src/main.cpp` — chooser + call the service

Add the include near the other service includes:
```cpp
#include "services/export/Exporter.h"
```

Wire the callback where the other `controlBar.on...` handlers are set (next to `onImport`).
Pattern mirrors the existing async `FileChooser` usage in the shell. `session` is the
`ProjectSession`; replace `statusStrip`/`setStatus(...)` with whatever the shell uses to show
status text:

```cpp
controlBar.onExport = [this]
{
    if (session.getEdit() == nullptr)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Export to WAV",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile (session.getEditFile().getFileNameWithoutExtension() + ".wav"),
        "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;   // user cancelled

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            juce::String error;
            const bool ok = Exporter::renderEditToWav (*session.getEdit(), file, error);

            // Show result in the status strip:
            setStatus (ok ? ("Exported " + file.getFileName())
                          : ("Export failed: " + error));
        });
};
```

Notes for the orchestrator:
- `renderEditToWav` blocks the message thread for the duration of the render. For short edits
  this is fine (matches the contract: "Synchronous/blocking … acceptable for now"). A future
  task can move it to `EditRenderer::render` (async) — see Unfinished.
- The chooser must outlive the async callback; the `std::make_shared<FileChooser>` captured into
  the lambda handles that (same idiom the shell already uses for Open/Import — match the exact
  local pattern if it differs).
- `setStatus(...)` is a placeholder for the shell's status-strip setter. Use the real one.

## Unfinished (with why)

- **Asynchronous render / progress UI.** The contract explicitly asked for synchronous/blocking,
  so this is intentional. The engine offers `EditRenderer::render(...)` (background thread +
  progress + completion callback) for a future non-blocking version; not done to keep this change
  minimal and to match the agreed contract.
- **Format/bit-depth/range options.** Hard-coded to 24-bit WAV, full edit range, stereo, as
  specified. No dialog for sample rate / bit depth / partial-range / mono export.
- **Normalisation, dithering, tail/decay allowance.** Left at engine defaults (off). Reverb/delay
  tails beyond the last clip's end are not captured because `endAllowance` is left at zero, matching
  the "[0, end-of-last-clip]" contract.

## Risks to verify at build time

- **Header reachability.** `Exporter.cpp` includes only `"services/export/Exporter.h"`, which
  includes `<JuceHeader.h>`. All `te::Renderer`, `te::render_utils::createRenderTask`,
  `te::getAudioTracks/getAllTracks/toBitSet`, `te::TransportControl`, `te::TimeRange/TimePosition`
  symbols come transitively from the tracktion_engine module in `<JuceHeader.h>`. If the project's
  JuceHeader does not pull the engine module into scope the way `ProjectSession.cpp` relies on, add
  the same engine include `ProjectSession.cpp` uses. (ProjectSession.cpp compiles today using only
  `"services/files/ProjectSession.h"` + `"engine/EngineHelpers.h"`, both of which resolve te::
  symbols, so this should match.)
- **`render_utils::createRenderTask` linkage.** It is a free function declared in
  `tracktion_Renderer.h` under `namespace render_utils`; confirm it links (it is part of the engine
  module that is already linked for playback/recording). If unresolved at link time, fall back to
  `te::Renderer::renderToFile ({}, outFile, edit, range, tracksToDo, true, false, {}, false)` which
  is the public 8-arg overload that internally does the same `useThread=false` loop and also yields
  24-bit (it sets `bitDepth = 24`), at the cost of using the engine *default* format and adding ACID
  info. (Pass `useACID=false` to suppress ACID.)
- **Sample rate when device is closed.** In `--selftest`/headless or before the audio device opens,
  `getSampleRate()` may be 0; the code falls back to 44100. Verify the render still succeeds in a
  headless context if the orchestrator exercises export there.
- **`canRenderInMono = false` forcing stereo.** Confirms a 2-channel WAV even for a mono-only edit.
  Verify the output is stereo as intended.
- **`ScopedRenderStatus` + plugin state.** The service stops transports and calls
  `turnOffAllPlugins` before and after, mirroring `Renderer::renderToFile`. If export is triggered
  while playing, playback is stopped first (expected). Verify no plugin-state assertion fires.
