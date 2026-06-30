# Devlog — File Browser (left region)

## What changed

Replaced the placeholder `browserPanel` Label in the collapsible LEFT shell region with a real
file Browser. New component `BrowserView` (src/ui/browser/BrowserView.{h,cpp}):

- A small "Browser" header label above a `juce::FileTreeComponent`.
- The tree is backed by a `juce::DirectoryContentsList`, filtered with a
  `juce::WildcardFileFilter` to common audio extensions (`*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3`),
  and scanned on an owned background `juce::TimeSliceThread` ("Forge Browser Scanner").
- Rooted at `userMusicDirectory` (falls back to `userHomeDirectory` if Music doesn't exist).
- Directories are included so the user can drill into sub-folders; the wildcard filter lets all
  directories through but only audio files appear as leaves.
- Themed via `ForgeLookAndFeel::Palette` — both the `TreeView` colour IDs (background/lines/
  selected-row/drag-indicator) and the `DirectoryContentsDisplayComponent` text/highlight IDs.
- **Double-clicking an existing audio file** fires `onImportFile(file)`; the shell imports it.

The component is `private juce::FileBrowserListener`; all four pure-virtual callbacks are
implemented (only `fileDoubleClicked` does work). Listener is added in the ctor and removed in
the dtor; the scan thread is stopped (and the list cleared) in the dtor before teardown.

API usage was verified against the real JUCE headers under
`libs/tracktion_engine/modules/juce/...` and against the working example
`modules/juce/examples/GUI/ImagesDemo.h`, which uses the identical
`WildcardFileFilter` → `TimeSliceThread` → `DirectoryContentsList` → `FileTreeComponent`
member-declaration order and `addListener(this)` / `setDirectory(...)` /
`startThread(Thread::Priority::background)` wiring.

## Public API (exact signatures)

In `src/ui/browser/BrowserView.h`:

```cpp
class BrowserView : public juce::Component,
                    private juce::FileBrowserListener
{
public:
    BrowserView();
    ~BrowserView() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    // Fired when the user activates an audio file (double-click). The shell imports it.
    std::function<void (const juce::File&)> onImportFile;
};
```

This matches the shell contract exactly (the destructor is an additive override needed to remove
the listener / stop the scan thread — it does not change the public surface the shell relies on).

## How the shell wires this

### 1. CMakeLists.txt — add the new .cpp to target_sources

File: `CMakeLists.txt`, in the `target_sources(Forge PRIVATE ...)` block (currently lines 30–38).
Add the browser source next to the other UI sources, e.g. after `src/ui/mixer/MixerView.cpp`:

```cmake
target_sources(Forge PRIVATE
    src/main.cpp
    src/services/files/ProjectSession.cpp
    src/services/export/Exporter.cpp
    src/engine/RecordController.cpp
    src/ui/transport/TransportBar.cpp
    src/ui/arrange/ArrangeView.cpp
    src/ui/mixer/MixerView.cpp
    src/ui/browser/BrowserView.cpp        // <-- add this line
    src/ui/ControlBar.cpp)
```

(`target_include_directories(Forge PRIVATE src)` already on line 40 means the
`"ui/browser/BrowserView.h"` include path resolves with no further change.)

### 2. src/main.cpp — include the header

File: `src/main.cpp`, with the other UI includes (currently lines 19–22). Add:

```cpp
#include "ui/browser/BrowserView.h"
```

### 3. src/main.cpp — replace the browserPanel Label member with a BrowserView

File: `src/main.cpp`, line 307:

```cpp
Label browserPanel, drawerPanel;
```

becomes (keep `drawerPanel` as a Label — only the browser changes):

```cpp
BrowserView browserPanel;
Label drawerPanel;
```

The member name `browserPanel` is intentionally kept so the existing `addAndMakeVisible
(browserPanel)` (line 167) and all `browserPanel.setVisible(...)` / `browserPanel.setBounds(...)`
calls in `resized()` (lines 259, 264) continue to compile unchanged — `BrowserView` is a
`juce::Component`, so those calls are valid as-is.

### 4. src/main.cpp — drop the browserPanel placeholder styling

File: `src/main.cpp`, in `setupPlaceholders()` (lines 343–361). Remove the browser line:

```cpp
style (browserPanel, "Browser\n(files, instruments, plug-ins) — Phase 3");
```

(`style()` takes a `Label&`; `browserPanel` is no longer a Label, so this line must go. Leave the
`drawerPanel` styling line as-is.)

### 5. src/main.cpp — wire the import callback

File: `src/main.cpp`, in the `MainComponent` constructor, anywhere after `browserPanel` is a live
member (e.g. right after `setupPlaceholders();` on line 162, or grouped with the other view
wiring near lines 191–221). Mirror `importDialog()`'s callback body (lines 480–482):

```cpp
browserPanel.onImportFile = [this] (const File& f)
{
    clip = session.importAudioFile (f, te::TimePosition());
    session.save();
    rebind();
};
```

No `SafePointer` guard is needed here: `browserPanel` is a member of `MainComponent`, so the
callback never outlives `this` (unlike the async FileChooser dialogs).

## Unfinished (with why)

- **Drag-from-browser-onto-a-track** — deferred. `FileTreeComponent::setDragAndDropDescription`
  exists and would let tree items act as drag sources, but a usable drop requires the ArrangeView
  (another agent's file) to implement `DragAndDropTarget` and translate a drop X-position into an
  edit time + target track. That's cross-file work outside this agent's ownership. Double-click
  import (the must-have) is fully wired; drag is documented as future.
- **Bookmarks / favourite folders / a path bar** — not implemented; the tree starts at Music and
  the user navigates via the tree. Future polish.
- **Inline waveform preview / audition on click** — not implemented (would need a preview
  transport). Future.
- **Persisting the last-browsed folder across launches** — not implemented (matches the shell's
  existing "region sizes not persisted yet" stance).

## Risks to verify at build time

- **Member-declaration order is load-bearing.** `audioFilter` and `scanThread` must be declared
  before `contents` (which takes `&audioFilter` and `scanThread` by reference in its ctor), and
  `contents` before `tree` (which holds a `DirectoryContentsList&`). The header declares them in
  exactly this order — do not reorder. This mirrors juce's ImagesDemo.
- **`FileBrowserListener` is pure-virtual on all four methods** (`selectionChanged`,
  `fileClicked`, `fileDoubleClicked`, `browserRootChanged`). All four are implemented; if a future
  JUCE bump adds a method, the build will flag it.
- **Colour IDs**: `TreeView::backgroundColourId/linesColourId/selectedItemBackgroundColourId/
  dragAndDropIndicatorColourId` and `DirectoryContentsDisplayComponent::textColourId/
  highlightColourId/highlightedTextColourId` were all confirmed present in the vendored headers
  (juce_TreeView.h lines 879–884, juce_DirectoryContentsDisplayComponent.h lines 102–107).
- **`TimeSliceThread` lifetime**: the dtor calls `contents.clear()` then `scanThread.stopThread
  (2000)` so the background scan is stopped before the thread object is destroyed — avoids the
  "deleting a running Thread" warning. Verify clean shutdown (no assertion) when the window closes
  while a scan is in flight.
- **`Thread::Priority::background`** is the enum used by ImagesDemo in this same JUCE version;
  confirmed valid.
- **main.cpp integration** must change `browserPanel`'s type AND remove its `style(browserPanel,
  ...)` placeholder line in `setupPlaceholders()` — otherwise `style()` (which takes `Label&`)
  won't bind to a `BrowserView`. This is the one easy-to-miss edit.
