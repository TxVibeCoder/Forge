# Devlog — Mixer FX, master strip & peak meters

Extends the existing Mixer view (one channel strip per audio track) with three additions, all
inside the exclusively-owned `src/ui/mixer/MixerView.{h,cpp}`:

1. **Insert slots** per track strip — the track's insert plugins listed below the pan knob,
   with a `+` to add, click-to-open, right-click / `x` to remove.
2. **Master strip** — a fixed strip pinned to the right edge driving the edit master volume.
3. **Peak meters** — a thin vertical level bar beside every fader (tracks and master), polled
   by a single ~28 Hz timer owned by `MixerView`.

The pre-existing public contract is preserved verbatim: `MixerView()`, `setEdit(te::Edit*)`,
`resized()`, `paint()`, `getNumStrips()` all keep their exact signatures. Everything new is
additive.

## What changed

### MixerView.h
- `MixerView` now also derives from `private juce::Timer` (the meter poll). Added a forward
  decl `class MasterStrip;` and a `std::unique_ptr<MasterStrip> master;` member alongside the
  existing `OwnedArray<ChannelStrip> strips`. Added a private `timerCallback()`.
- `setEdit()` now starts the timer (`startTimerHz(28)`) when an Edit is bound and `stopTimer()`
  when it is null (per the brief: stop the timer when the edit is null). `~MixerView()` stops it.
- `MixerLayout::stripW` widened **84 → 92** (room for the meter + insert rows); added
  `MixerLayout::masterW = 96`.

### MixerView.cpp — new internal classes (all defined in the .cpp, none exported)
- **`PeakMeter`** (`juce::Component`): registers a `te::LevelMeasurer::Client` on a measurer via
  `attach(te::LevelMeasurer*)`. `poll(dt)` pulls `getAndClearAudioLevel(ch)` for up to 2 channels,
  takes the hotter side, applies instant-attack / timed-decay ballistics (18 dB/s fall-off), and
  repaints. Draws an amber bar (red above 0 dB), a 0 dB tick, and an outline. If no measurer is
  attached it draws **empty** — it never fabricates a level. `detach()` (and the destructor)
  remove the client.
- **`InsertPanel`** (`juce::Component`): lists `PluginHost::getTrackInserts(track)` as
  `InsertRow`s plus a `+` add button. `+` pops `PluginHost::getAvailablePluginNames(track.edit.engine)`
  and calls `PluginHost::addPluginToTrack(track, name)` on selection. Rebuilds its rows on change.
- **`InsertPanel::InsertRow`**: a name button (`PluginWindow::show(plugin)` on click) + an `x`
  button (`PluginHost::removePlugin(plugin)`); right-click on the row also removes. Removal
  notifies the panel **asynchronously** (`MessageManager::callAsync`) so the row is never deleted
  from inside its own click handler.
- **`MixerView::ChannelStrip`** gains an `InsertPanel` (below pan) and a `PeakMeter` (left of the
  fader, fed from `track.getLevelMeterPlugin()->measurer`). New `getDesiredHeight()` returns the
  strip's height including its variable insert-row count; `getMeter()` exposes the meter to the
  view's timer. The original name/fader/pan/M/S wiring is unchanged.
- **`MixerView::MasterStrip`**: `MASTER`-labelled strip driving `edit.getMasterVolumePlugin()`
  (`getVolumeDb()` to seed, `setVolumeDb()` on change, clamped to [-100,+12]). Its meter is fed
  from a `te::LevelMeterPlugin` found via
  `edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>()` **if one exists**;
  otherwise the meter draws empty (see Unfinished).

### MixerView.cpp — layout / lifecycle
- `rebuild()` builds the per-track strips (as before) and then creates the `MasterStrip`
  (added directly to the view, **outside** the scrolling viewport).
- `resized()` pins the master strip to the right (`removeFromRight(masterW)`), gives the viewport
  the rest, sizes `stripHolder` to the **tallest** strip (strips differ by insert count) so the
  row stays aligned, and lays strips left-to-right.
- `timerCallback()` calls `poll(1/28)` on every strip meter and the master meter.
- `paint()` empty-state text now shows only when there are no strips **and** no master.

### Engine APIs used (verified against the vendored headers — none guessed)
- `te::AudioTrack::getLevelMeterPlugin()` → `LevelMeterPlugin*` (may be null)
  — `tracktion_AudioTrack.h:60`, impl `tracktion_AudioTrack.cpp:260`.
- `te::LevelMeterPlugin::measurer` is a public `LevelMeasurer` — `tracktion_LevelMeter.h:46`.
- `te::LevelMeasurer::addClient/removeClient`, `Client::getAndClearAudioLevel(int)`,
  `Client::getNumChannelsUsed()`, `Client::reset()` — `tracktion_LevelMeasurer.h:62-98`,
  `.cpp:56-105` (the same Client idiom the engine's own `FourOscPlugin` uses,
  `tracktion_FourOscPlugin.cpp:899`).
- `te::Edit::getMasterVolumePlugin()` → `VolumeAndPanPlugin::Ptr` — `tracktion_Edit.h:588`;
  `VolumeAndPanPlugin::getVolumeDb()/setVolumeDb()` — `tracktion_VolumeAndPan.h:34-36`.
- `te::Edit::getMasterPluginList()` → `PluginList&` — `tracktion_Edit.h:494`;
  `PluginList::findFirstPluginOfType<T>()` — `tracktion_PluginList.h:55`.
- `te::EditItem::edit` (public `Edit&`, base of `Track`) → `edit.engine` for the plugin menu —
  `tracktion_EditItem.h:94`.

## Public API (exact signatures)

From `src/ui/mixer/MixerView.h` — unchanged public surface plus the timer base:

```cpp
class MixerView : public juce::Component,
                  private juce::Timer
{
public:
    MixerView();
    ~MixerView() override;
    void setEdit (te::Edit*);     // rebuild strips + master; start/stop the meter timer
    void resized() override;
    void paint (juce::Graphics&) override;
    int  getNumStrips() const;    // track strips only (excludes master)
};
```

No public method was changed or removed; the four contract members keep their exact signatures.
All new types (`PeakMeter`, `InsertPanel`, `InsertPanel::InsertRow`, `MixerView::ChannelStrip`,
`MixerView::MasterStrip`) are internal to the .cpp / private nested and not part of the surface.

### Contracts this file CALLS (defined by other agents — never defined here)
```cpp
juce::StringArray      PluginHost::getAvailablePluginNames (te::Engine&);
te::Plugin::Ptr        PluginHost::addPluginToTrack (te::AudioTrack&, const juce::String&);
juce::Array<te::Plugin*> PluginHost::getTrackInserts (te::AudioTrack&);
void                   PluginHost::removePlugin (te::Plugin&);
void                   PluginWindow::show (te::Plugin&);
```
Plus the existing `EngineHelpers::get/setTrackVolumeDb`, `get/setTrackPan`.

## How the shell wires this

**Nothing new in main.cpp.** The shell already constructs `MixerView`, calls
`mixerView.setEdit(session.getEdit())` at ctor / `rebind()` and `setEdit(nullptr)` on swap, and
places it in the centre slot (see `docs/devlog/mixer.md`). The meter timer is driven internally
off `setEdit`, so no extra shell call is required. No ControlBar edit is required.

### CMakeLists.txt — the only required integration
`src/ui/mixer/MixerView.cpp` is **already** in the `target_sources(Forge PRIVATE ...)` block
(line 37). What is NOT yet there, and MUST be added by the orchestrator for this file to link,
are the PluginHost / PluginWindow translation units it now depends on (owned by the plugins
agent, who also notes this):

```cmake
target_sources(Forge PRIVATE
    ...
    src/ui/mixer/MixerView.cpp
    src/engine/PluginHost.cpp          # <-- add (plugins agent's file)
    src/ui/plugins/PluginWindow.cpp    # <-- add (plugins agent's file)
    ...)
```

(If those agents ship header-only, no source line is needed — but `MixerView.cpp` includes
`"engine/PluginHost.h"` and `"ui/plugins/PluginWindow.h"`, so both headers must exist on the
`src/` include root before this compiles.)

## Unfinished
- **Master meter data may be absent.** The master meter is fed only if the master plugin list
  already contains a `LevelMeterPlugin`. Tracktion does not guarantee one on the master chain by
  default, so on edits without it the master meter **draws but stays at the floor**. Deferred
  rather than faked: a reliable hookup would mean inserting a `LevelMeterPlugin` into the master
  list (a structural edit to the Edit that belongs in an engine/PluginHost helper, not the view),
  or reading the Edit's output node level. Documented as the data hookup to finish. The master
  **fader** is fully functional regardless.
- **Pan on the master strip is not shown.** Only master volume + meter (per the brief: "volume
  fader + meter"). `getMasterPanParameter()` exists if a master pan control is wanted later.
- **No insert reordering / drag.** Inserts can be added, opened and removed, but not reordered;
  add appends (per the PluginHost contract). Reorder is future work (needs a PluginHost move API).
- **No live external refresh** (inherited): if another surface changes a track's inserts/volume,
  the open strip refreshes only on the next `setEdit` — except inserts changed *from this panel*,
  which refresh themselves. Matches the existing manual-rebuild model.
- **Meters reflect post-fader, post-meter-plugin level**, i.e. wherever the track's
  `LevelMeterPlugin` sits in the chain (Tracktion's default position). Not separately switchable
  to pre-fader this pass.

## Risks to verify at build time
- **PluginHost.h / PluginWindow.h must exist on the include root** with the exact signatures
  above. They are written by another agent and were NOT present when this was authored — if the
  names/signatures differ, `MixerView.cpp` will not compile. In particular `getTrackInserts` is
  iterated as `for (auto* p : ...)` so it must be a container of `te::Plugin*`, and it must
  exclude the built-in volume/level-meter plugins (else they appear as removable rows).
- **`getMasterPluginList()` / `findFirstPluginOfType<LevelMeterPlugin>()`** — confirmed in
  `tracktion_Edit.h:494` and `tracktion_PluginList.h:55`. `findFirstPluginOfType` is a const
  template; `LevelMeterPlugin` must be a complete type (it is, via the engine umbrella include).
- **`Component::SafePointer<InsertPanel>`** is used to guard the async add/remove callbacks
  against the strip being torn down by `setEdit` while a popup menu / async rebuild is pending.
  Requires `InsertPanel` to be a `Component` (it is).
- **Meter thread-safety**: `addClient`/`removeClient` and `getAndClearAudioLevel` are all called
  on the message thread (ctor/dtor + the 28 Hz timer); the audio thread feeds the measurer under
  the engine's own SpinLock. No new locking added.
- **Strip height vs. viewport**: `resized()` sizes `stripHolder` to the tallest strip's
  `getDesiredHeight()` (falls back to the viewport height when shorter), so a vertical scrollbar
  is never shown (the viewport is horizontal-only, as before) and short strips still fill the
  column. Verify the insert panel doesn't push controls off the bottom on edits with many
  inserts — the fader region is fixed and the strip simply grows taller, scrolling horizontally
  is unaffected.
- **`track.edit.engine`** relies on `EditItem::edit` being public (`tracktion_EditItem.h:94`) —
  same access EngineHelpers uses.
