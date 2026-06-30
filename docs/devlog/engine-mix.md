# engine-mix — track volume/pan helpers

Additive inline helpers added to `src/engine/EngineHelpers.h` so the new MixerView (and
anyone else) can read/write per-track volume (dB) and pan (-1..+1) without touching the
Tracktion plugin internals. ADDITIVE ONLY — every pre-existing inline function in the header
is untouched, so the other includers (main.cpp, ProjectSession.cpp, TransportBar.cpp,
RecordController.cpp) are unaffected.

## What changed

- Appended four inline free functions to the `EngineHelpers` namespace in
  `src/engine/EngineHelpers.h`, just before the namespace's closing brace. No existing code
  was modified, no includes were added (everything needed is already pulled in via
  `<JuceHeader.h>` + the `te = tracktion` alias and the engine module that ships
  `tracktion_AudioTrack.h` / `tracktion_VolumeAndPan.h`).

## Public API (exact signatures)

```cpp
namespace EngineHelpers
{
    inline float getTrackVolumeDb (te::AudioTrack& track);          // current volume in dB (0.0f if no plugin)
    inline void  setTrackVolumeDb (te::AudioTrack& track, float db); // clamps db to [-100, +12]
    inline float getTrackPan      (te::AudioTrack& track);          // -1..+1 (0.0f if no plugin)
    inline void  setTrackPan      (te::AudioTrack& track, float pan); // clamps pan to [-1, +1]
}
```

These match the mixer contract exactly (no renames, no signature changes).

## Implementation / API provenance (what I verified against the real headers)

- **Plugin accessor:** `te::AudioTrack::getVolumePlugin()` returns `VolumeAndPanPlugin*`
  (may be null). Declared at `tracktion_AudioTrack.h:59`
  (`libs/tracktion_engine/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.h`).
  All four helpers guard the null case (return `0.0f` / no-op).

- **Volume (dB):** the plugin exposes a direct dB API —
  `float VolumeAndPanPlugin::getVolumeDb() const;` (`tracktion_VolumeAndPan.h:34`) and
  `void VolumeAndPanPlugin::setVolumeDb (float vol);` (`tracktion_VolumeAndPan.h:36`), in
  `libs/tracktion_engine/modules/tracktion_engine/plugins/internal/tracktion_VolumeAndPan.h`.
  Internally these convert through `volumeFaderPositionToDB` / `decibelsToVolumeFaderPosition`
  (`tracktion_AudioUtilities.cpp:138-146`). The engine's dB floor is **-100 dB** (its own
  silence sentinel — see `volumeFaderPositionToDB` returning `-100.0f` for pos <= 0, and
  `muteOrUnmute()` driving to `-100.0f`), and the fader curve reaches **+6 dB** at slider
  position 1.0. I clamp to **[-100, +12]** per the contract via `juce::jlimit`; the floor
  matches the engine's silence value and +12 stays within what `setSliderPos` accepts.

- **Pan:** `float VolumeAndPanPlugin::getPan() const` (`tracktion_VolumeAndPan.h:39`) and
  `void VolumeAndPanPlugin::setPan (float pan)` (`tracktion_VolumeAndPan.h:40`) are already in
  the **-1..+1** range (-1 = hard left, 0 = centre, +1 = hard right). I clamp to **[-1, +1]**
  with `juce::jlimit`. No conversion needed.

I did NOT use the slider-position API (`getSliderPos`/`setSliderPos`) — the dB and pan
accessors are the right level for a mixer fader/pan control and avoid double conversion.

## How the shell wires this

Nothing for the orchestrator to wire. These are header-only inline helpers in a header the
project already compiles and that MixerView will include directly:

```cpp
#include "engine/EngineHelpers.h"   // already on the include path used by the other UI files
```

There is **no** CMakeLists change (no new translation unit), **no** main.cpp change, and **no**
ControlBar change required for this deliverable. The only consumer is MixerView.cpp (owned by
the mixer agent), which calls the four functions above on a `te::AudioTrack&` obtained from
`te::getAudioTracks(edit)`.

## Unfinished

- Nothing in scope is unfinished. The four contract functions are implemented and verified.
- Out of scope (intentionally not added): mute/solo helpers (`VolumeAndPanPlugin::muteOrUnmute()`
  exists but mute/solo on AudioTrack is a separate API surface), gain-linear variants, and
  master-bus volume. The contract asked only for per-track volume-dB and pan; adding more would
  be speculative.

## Risks to verify at build time

- **Null-plugin path:** a freshly created `AudioTrack` normally has its VolumeAndPanPlugin, but
  the helpers tolerate null (return 0.0f / no-op) so a track without one won't crash. Low risk.
- **Clamp ceiling:** +12 dB is above the +6 dB the fader curve produces at slider pos 1.0;
  `setVolumeDb` routes through `decibelsToVolumeFaderPosition` which yields a slider pos > 1.0
  for +12 dB, then `setSliderPos` clamps to the parameter's value range. If the parameter caps
  at unity the effective max may be lower than +12 — cosmetic only, no compile/runtime risk.
- **No new includes:** the helpers rely on `te::AudioTrack` and `te::VolumeAndPanPlugin` being
  visible. Both come from the tracktion_engine module that `<JuceHeader.h>` already brings in
  for the existing helpers in this same file (which already use `te::AudioTrack`,
  `te::WaveAudioClip`, etc.), so no include was needed. Confirm the build still resolves
  `VolumeAndPanPlugin` from the `AudioTrack::getVolumePlugin()` return type (it is forward-usable
  because we only dereference member functions declared in the engine headers).
