# Plugin hosting + floating editor windows

Engine-side effect hosting for a track's plugin chain, plus a floating editor window so a user
can add an effect to a track and tweak it. Built-in engine effects are prioritised (always
available, no scanning) and scanned externals are appended when present.

## What changed

Four new files, no edits to any existing file:

- `src/engine/PluginHost.h` / `PluginHost.cpp` — insert / list / remove effects on an
  `AudioTrack`, by display name. Knows the engine's built-in effects and any scanned externals.
- `src/ui/plugins/PluginWindow.h` / `PluginWindow.cpp` — a floating `DocumentWindow` per plugin.
  External plugins host their own editor; built-in effects (which have no native editor) get a
  generic, theme-styled panel of sliders driven by the plugin's automatable parameters.

### How plugins are created (the engine recipe)

Every plugin is created through the Edit's plugin cache:

    edit.getPluginCache().createNewPlugin (xmlType, pluginDescription)

- **Built-in effects:** `xmlType` is the plugin class's `xmlTypeName`; the `PluginDescription`'s
  `pluginFormatName` is `te::PluginManager::builtInPluginFormatName` and `fileOrIdentifier` is the
  same `xmlTypeName`. `PluginHost::makeBuiltIn<PluginClass>()` fills this in. This mirrors the
  engine's own menu (`examples/common/Components.cpp` `createBuiltInItems`).
- **Scanned externals:** `xmlType` is `te::ExternalPlugin::xmlTypeName` and the description is the
  one already stored in `engine.getPluginManager().knownPluginList`.

The created plugin is inserted with `track.pluginList.insertPlugin (plugin, index, nullptr)`. We
insert at the **volume plugin's index** so the effect lands at the end of the user's insert
section but BEFORE the always-present `[volume&pan, level-meter]` tail (so it processes audio
ahead of the fader/meter). Default chain order confirmed in `tracktion_PluginList.cpp`
`addDefaultTrackPlugins`.

### Built-in effects offered (menu order)

4-Band Equaliser, Compressor/Limiter, Reverb, Delay, Chorus, Phaser, Pitch Shifter,
Low/High-Pass Filter. (Routing/utility internals — patch bays, aux send/return, freeze point,
text, MIDI modifiers — and synths are deliberately excluded; this list is track effect inserts.)
Names come from each class's `getPluginName()`, matching what the engine's own plugin tree shows.

### Why PluginWindow builds its own window

`Plugin::showWindowExplicitly()` routes through `Engine::getUIBehaviour().createPluginWindow()`,
whose default returns `nullptr` (verified in `tracktion_PluginWindowState.cpp` line 138). Wiring
that up needs a custom `UIBehaviour` installed at engine construction, which is outside this
module. To stay self-contained and own the windows here (per the contract), `PluginWindow` builds
a `juce::DocumentWindow` directly and keeps them in a static registry keyed by plugin pointer.

The engine's built-in effects do NOT override `Plugin::createEditor()` (only `ExternalPlugin`
does), so for them the window builds a **generic editor**: one labelled `Slider` per
`AutomatableParameter`, two-way bound — `setParameter()` on drag, a 15 Hz timer pulls the
authoritative value back from `getCurrentValue()` so automation / other views stay in sync.

Windows auto-close when their plugin is deleted: `Window` registers as a `te::SelectableListener`
on the plugin and erases itself from the registry on `selectableObjectAboutToBeDeleted` (deferred
via `MessageManager::callAsync` so the window isn't destroyed mid-callback).

## Public API (exact signatures)

```cpp
// src/engine/PluginHost.h
namespace PluginHost
{
    juce::StringArray      getAvailablePluginNames (te::Engine&);
    te::Plugin::Ptr        addPluginToTrack (te::AudioTrack& track, const juce::String& displayName);
    juce::Array<te::Plugin*> getTrackInserts (te::AudioTrack& track);
    void                   removePlugin (te::Plugin&);
}

// src/ui/plugins/PluginWindow.h
namespace PluginWindow
{
    void show (te::Plugin&);       // open or focus a floating editor for this plugin
    void closeFor (te::Plugin&);   // close the window for one plugin if open (additive helper)
    void closeAll();               // close every open window (project swap / shutdown)
}
```

Notes for callers:
- `getAvailablePluginNames` returns built-ins first, then de-duplicated externals; the names are
  exactly what `addPluginToTrack` expects.
- `addPluginToTrack` returns the created `te::Plugin::Ptr` (nullptr on failure, chain unchanged).
- `getTrackInserts` returns the user inserts only (volume&pan + level-meter filtered out), in
  chain order. Label a returned plugin with `p->getName()`.
- `removePlugin` closes any editor window for the plugin, then `deleteFromParent()`.

## How the shell wires this

These are edits OWNED BY OTHER AGENTS (the orchestrator / mixer agent), listed here precisely.
PluginHost/PluginWindow change none of them.

### 1. CMakeLists.txt — add the two new translation units

File: `CMakeLists.txt`, in the `target_sources(Forge PRIVATE ...)` block (currently lines 30-38).
Add after `src/ui/mixer/MixerView.cpp`:

```cmake
    src/engine/PluginHost.cpp
    src/ui/plugins/PluginWindow.cpp
```

(Header-only `.h` files need no listing. `src` is already on the include path via
`target_include_directories(Forge PRIVATE src)`, so `#include "engine/PluginHost.h"` and
`#include "ui/plugins/PluginWindow.h"` resolve.)

### 2. main.cpp — close all plugin windows on project swap and shutdown

File: `src/main.cpp`. Add the include near the other UI includes (around line 20):

```cpp
#include "ui/plugins/PluginWindow.h"
```

Call `PluginWindow::closeAll();` in two places so windows never outlive their Edit:
- In `~MainComponent()` (around line 237), before the Edit/session is torn down.
- Anywhere the Edit is swapped for a new/opened project (New / Open handlers, before
  `session` adopts the new Edit and before `arrangeView.setEdit(...)` / `mixerView.setEdit(...)`).

### 3. Mixer view / control bar — the actual insert UI (mixer agent's surface)

The mixer agent owns the surface that lets the user add/remove/edit inserts. Typical flow on a
channel strip (it already holds a `te::AudioTrack&`):

```cpp
// Build the "+" insert menu:
juce::PopupMenu m;
const auto names = PluginHost::getAvailablePluginNames (track.edit.engine);
for (int i = 0; i < names.size(); ++i)
    m.addItem (i + 1, names[i]);
m.showMenuAsync ({}, [this, names] (int r)
{
    if (r > 0)
        if (auto plugin = PluginHost::addPluginToTrack (track, names[r - 1]))
            PluginWindow::show (*plugin);   // pop the editor immediately
});

// List existing inserts (e.g. one button per insert, labelled p->getName()):
for (auto* p : PluginHost::getTrackInserts (track))
    /* add a row; clicking it -> PluginWindow::show (*p); right-click Remove -> below */;

// Remove an insert:
PluginHost::removePlugin (*p);   // closes its window, deletes from the chain
```

After `addPluginToTrack` / `removePlugin`, persist the Edit (the shell already saves on
`arrangeView.onEditMutated`; the mixer agent should trigger the same save path).

## Unfinished

- **No per-plugin bypass / enable toggle** is surfaced. The engine supports it
  (`te::Plugin::setEnabled(bool)` / `isEnabled()`); a bypass button can be added to the insert
  row by the mixer agent without touching this module.
- **No drag-to-reorder** of inserts. `pluginList.insertPlugin(plugin, index, ...)` +
  `removeFromParent()` make this possible later; left out to keep this pass small and correct.
- **Generic editor is read/write of scalar parameters only.** Built-in effects with non-scalar
  UI (e.g. the EQ's frequency-response graph) are shown as plain sliders, not their bespoke
  native graphs — the engine doesn't expose those graphs as `EditorComponent`s, so a faithful
  graph would mean reimplementing each effect's UI. Sliders are fully functional and audible.
- **External-plugin scanning is not triggered here.** `getAvailablePluginNames` lists whatever is
  already in `knownPluginList`; kicking off a VST3/AU scan is a separate feature (engine has
  `PluginManager` scan plumbing) and belongs in audio-settings, not the insert path.

## Risks to verify at build time

- **`createNewPlugin` overload:** confirmed `PluginCache::createNewPlugin (const juce::String& type,
  const juce::PluginDescription&)` exists (`tracktion_PluginManager.h` line 142). We call it via
  `edit.getPluginCache()`.
- **`insertPlugin` index semantics:** we pass the volume plugin's index. Verified in
  `tracktion_PluginList.cpp` (`insertPlugin(const Plugin::Ptr&, int, SelectionManager*)` ->
  `insertPlugin(ValueTree, int)`); index in range inserts BEFORE that sibling, which is what we
  want (effect lands just before volume&pan). If `getVolumePlugin()` is ever null we fall back to
  appending (-1).
- **Built-ins have no `createEditor`:** verified — only `ExternalPlugin`/base `Plugin` declare it,
  and base returns `{}`. The generic-panel fallback path is therefore the one exercised for all
  built-in effects. If a build links a JUCE where `Plugin::createEditor` is pure/renamed, the
  `if (auto editor = plugin.createEditor())` branch is where it would surface.
- **`AutomatableParameter` API:** uses `getAutomatableParameters()`, `getParameterName()`,
  `getValueRange()`, `getCurrentValue()`, `setParameter(float, NotificationType)`,
  `getCurrentValueAsStringWithLabel()`, `parameterChangeGestureBegin/End()` — all verified in
  `tracktion_AutomatableParameter.h` / `tracktion_AutomatableEditItem.h`.
- **`SelectableListener` is pure-virtual on both methods:** we implement both
  (`selectableObjectChanged`, `selectableObjectAboutToBeDeleted`). `Window` privately inherits it;
  `addSelectableListener(this)` is called from inside `Window`, where the upcast is accessible.
- **`approximatelyEqual` / `getMaximumVisibleWidth`:** both exist in the vendored JUCE
  (`juce_MathsFunctions.h`, `juce_Viewport.h`).
- **Window lifetime on plugin delete:** the deferred `MessageManager::callAsync` erase assumes a
  running message loop (true in the app; in a headless self-test no windows are opened). Verify no
  plugin window is left open across an Edit swap — that's what the `closeAll()` wiring above is for.
