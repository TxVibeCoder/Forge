# Devlog — Mixer view

A real Mixer view replacing the Phase-2 placeholder Label: a horizontal row of channel
strips, one per audio track in the Edit. Each strip has the track name, a vertical dB
volume fader, a rotary pan knob, M/S toggles and a colour swatch matching the track.
Control moves push to the engine live.

## What changed

New files (only these — exclusive ownership):

- `src/ui/mixer/MixerView.h`
- `src/ui/mixer/MixerView.cpp`

`MixerView` is a `juce::Component` that, on `setEdit (te::Edit*)`, enumerates
`te::getAudioTracks(*edit)` and builds one `ChannelStrip` per track into a
`juce::OwnedArray`. The strips live inside a horizontally-scrolling `juce::Viewport`
(content holder `stripHolder`), so an Edit with more tracks than fit the width scrolls
rather than clipping. `setEdit(nullptr)` clears all strips and shows an empty-state hint.

`ChannelStrip` (a `Component` nested inside `MixerView`, defined entirely in the .cpp,
not exported) holds the track by reference and owns:

- a `juce::Label` for the name (seeded from `track.getName()`),
- a vertical `juce::Slider` (`LinearVertical`) fader, range **-60..+6 dB**, 0.1 step,
  double-click → 0 dB (unity), text box below in dB,
- a rotary `juce::Slider` (`RotaryHorizontalVerticalDrag`) pan, range **-1..+1**,
  double-click → 0 (centre), no text box,
- `M` / `S` `juce::TextButton` toggles,
- a colour swatch band painted across the top of the strip using `track.getColour()`.

Live engine wiring (all on the message thread):

- `fader.onValueChange` → `EngineHelpers::setTrackVolumeDb (track, (float) value)`
- `pan.onValueChange`   → `EngineHelpers::setTrackPan (track, (float) value)`
- `muteButton.onClick`  → `track.setMute (toggleState)`
- `soloButton.onClick`  → `track.setSolo (toggleState)`

Initial positions are seeded with `dontSendNotification` from the matching getters
(`EngineHelpers::getTrackVolumeDb / getTrackPan`, `track.isMuted(false) / isSolo(false)`)
so creating a strip never writes back to the engine.

Theming is via `ForgeLookAndFeel::Palette` colour IDs (accent on thumbs/fills, raisedBg /
hairline / textSec / panelBg / shellBg elsewhere) — read-only include, no L&F changes.

Strips are **rebuilt from scratch** on every `setEdit` (same manual-rebuild model as
ArrangeView — no ValueTree listeners), so the shell just calls `setEdit` again after any
structural change to refresh the row.

## Public API (exact signatures)

From `src/ui/mixer/MixerView.h`:

```cpp
class MixerView : public juce::Component
{
public:
    MixerView();
    ~MixerView() override;
    void setEdit (te::Edit*);          // rebuild strips for the edit's audio tracks (null clears)
    void resized() override;
    void paint (juce::Graphics&) override;
    int  getNumStrips() const;         // extra: strip count for diagnostics/self-tests
};
```

This is a superset of the required contract (the contract's four members are present with
the exact signatures; `~MixerView()` and `getNumStrips()` are additive and optional for the
shell). `ChannelStrip` is private/nested and not part of the public surface.

Depends on these `EngineHelpers` free functions (provided by another agent — this view only
*calls* them, never defines them):

```cpp
float EngineHelpers::getTrackVolumeDb (te::AudioTrack&);
void  EngineHelpers::setTrackVolumeDb (te::AudioTrack&, float db);
float EngineHelpers::getTrackPan      (te::AudioTrack&);   // -1..+1
void  EngineHelpers::setTrackPan      (te::AudioTrack&, float pan);
```

## How the shell wires this

Three edits, all by the orchestrator. **Do not** let two agents both edit main.cpp/CMakeLists.

### 1. CMakeLists.txt — add the new .cpp to `target_sources`

File `CMakeLists.txt`, the `target_sources(Forge PRIVATE ...)` block (currently lines 30-36).
Add the MixerView source line (e.g. right after the ArrangeView line):

```cmake
target_sources(Forge PRIVATE
    src/main.cpp
    src/services/files/ProjectSession.cpp
    src/engine/RecordController.cpp
    src/ui/transport/TransportBar.cpp
    src/ui/arrange/ArrangeView.cpp
    src/ui/mixer/MixerView.cpp        # <-- add this line
    src/ui/ControlBar.cpp)
```

(The header is included via the source's `#include "ui/mixer/MixerView.h"`; no separate
header registration is needed. The existing include dir setup already exposes `src/` as a
search root — `ArrangeView.cpp` includes `"ui/arrange/ArrangeView.h"` the same way.)

### 2. main.cpp — include the header

File `src/main.cpp`, near the other view includes (currently around line 18,
`#include "ui/arrange/ArrangeView.h"`). Add:

```cpp
#include "ui/mixer/MixerView.h"
```

### 3. main.cpp — replace the `mixerPanel` Label with a `MixerView`

The placeholder is the `Label mixerPanel;` member; it is created, styled, shown, sized and
toggled in several spots. Replace each as follows.

**a. Member declaration.** Currently (line 303):

```cpp
Label browserPanel, drawerPanel, mixerPanel;
```

Change to leave `mixerPanel` out of the Label list and add a `MixerView` member:

```cpp
Label browserPanel, drawerPanel;
MixerView mixerView;
```

**b. `addAndMakeVisible`.** Currently (line 166): `addAndMakeVisible (mixerPanel);`
Change to:

```cpp
addAndMakeVisible (mixerView);
```

**c. Placeholder styling.** In `setupPlaceholders()` remove the mixer line (line 350):

```cpp
style (mixerPanel,   "Mixer view\n(channel strips, sends, meters) — Phase 5");
```

(Delete it — `mixerView` paints its own empty-state text. Leave the `browserPanel` /
`drawerPanel` style calls untouched.)

**d. `resized()`.** Currently (lines 277-280):

```cpp
arrangeView.setVisible (viewMode == ViewMode::Arrange);
mixerPanel.setVisible (viewMode == ViewMode::Mixer);
arrangeView.setBounds (centre);
mixerPanel.setBounds (centre);
```

Change the two `mixerPanel` references to `mixerView`:

```cpp
arrangeView.setVisible (viewMode == ViewMode::Arrange);
mixerView.setVisible (viewMode == ViewMode::Mixer);
arrangeView.setBounds (centre);
mixerView.setBounds (centre);
```

**e. Bind the Edit at ctor + every rebind/swap.** The shell already calls
`arrangeView.setEdit (session.getEdit())` in three places; add the parallel
`mixerView.setEdit (...)` call right next to each:

- In the constructor, after `arrangeView.setEdit (session.getEdit());` (line 218):
  ```cpp
  mixerView.setEdit (session.getEdit());
  ```
- In `swapProject()`, the `arrangeView.setEdit (nullptr);` clearing call (line 604) —
  add `mixerView.setEdit (nullptr);` beside it. (The subsequent `rebind()` re-binds both.)
- In `rebind()`, after `arrangeView.setEdit (session.getEdit());` (line 613):
  ```cpp
  mixerView.setEdit (session.getEdit());
  ```

That is the entire shell change. No ControlBar edit is required — the Mixer view toggle
(`onViewMode` → `setViewMode (ViewMode::Mixer)`, plus the F11 shortcut) already exists and
just needs `mixerView` to occupy the centre slot when the Mixer mode is active, which steps
d/e provide.

## Unfinished

- **No metering / peak / RMS bars.** Strips show controls only, no signal level. Deferred —
  needs a timer polling `track`'s level measurer and is out of scope for this contract
  ("No metering/peak yet").
- **No live external refresh.** If another surface (e.g. ArrangeView's lane M/S, or a future
  automation move) changes a track's mute/solo/volume/pan, the open Mixer strip will not
  update until the shell calls `setEdit` again. Acceptable for now (matches ArrangeView's
  manual-rebuild model); a shared refresh hook is future work.
- **No add/remove/reorder track from the Mixer.** Strips are read-from-track only; track
  structure is still edited from ArrangeView's context menus. The Mixer reflects the new
  set on the next `setEdit`.
- **No master/output strip.** Only per-audio-track strips, matching the contract.
- **Mute/solo changes are not auto-saved by the Mixer.** Unlike ArrangeView (which fires
  `onEditMutated` → `session.save()`), the Mixer has no persistence callback in its contract,
  so M/S/fader/pan changes take effect live but are saved with the next explicit save. If
  persistence-on-change is wanted later, add an `onEditMutated` std::function to MixerView
  (additive) and wire it like ArrangeView's.

## Risks to verify at build time

- **EngineHelpers volume/pan helpers must exist.** This view calls
  `EngineHelpers::getTrackVolumeDb / setTrackVolumeDb / getTrackPan / setTrackPan`. They are
  defined by another agent in `src/engine/EngineHelpers.h`. At the time of writing they were
  NOT yet present in that header — if the other agent's change has not landed, this .cpp will
  fail to compile with "no member named ...". Verify both changes are integrated together.
  Signatures assumed: `float get...(te::AudioTrack&)`, `void set...(te::AudioTrack&, float)`.
- **`getMaximumVisibleWidth/Height` on the Viewport** are used in `resized()` to size the
  content holder before any scrollbar is needed. Confirmed present in
  `juce_Viewport.h` (lines 180/187). They return the viewport's inner area minus any visible
  scrollbar; on first layout with no strips this is fine (content collapses to the view size).
- **Strip lifetime vs. track lifetime.** Each `ChannelStrip` holds `te::AudioTrack&`. The
  OwnedArray is fully cleared at the top of `rebuild()` *before* any track could be deleted by
  the shell's rebind path, and the shell always calls `setEdit` after structural changes, so a
  strip never references a freed track — provided the shell follows the wiring in step (e)
  (call `mixerView.setEdit` on every project swap/rebind, exactly mirroring `arrangeView`).
- **`AudioTrack::isMuted/isSolo` take a bool arg** (`isMuted(false)`, `isSolo(false)`) —
  confirmed against `tracktion_AudioTrack.h`. Matches ArrangeView's usage.
- **Slider colour IDs / styles** (`LinearVertical`, `RotaryHorizontalVerticalDrag`,
  `thumb/track/background/rotarySliderFill/rotarySliderOutline/textBoxText/textBoxOutline
  ColourId`) all confirmed present in `juce_Slider.h`.
