# Detail-drawer clip Inspector (DetailView)

## What changed

Added a clip Inspector for the bottom Detail-drawer region, replacing the placeholder Label
("Detail editor — double-click a clip…"). New files:

- `src/ui/detail/DetailView.h`
- `src/ui/detail/DetailView.cpp`

When a clip is selected in the arrange view, the shell calls `detailView.setClip(c)` and the
Inspector shows, for the selected clip:

- **Name** — an editable `juce::Label` (double-click to edit, commits on return / focus-loss) →
  `te::Clip::setName`.
- **Gain** — a horizontal dB `Slider` (-60…+6 dB, double-click = 0 dB) →
  `te::AudioClipBase::setGainDB` / `getGainDB`.
- **Mute** — a `ToggleButton` → `te::Clip::setMuted` / `isMuted`.
- **Fade In / Fade Out** — horizontal second-sliders, range capped at the clip length →
  `te::AudioClipBase::setFadeIn` / `setFadeOut` (return values ignored; the engine also clamps).
- **Read-only timing** — a label showing Start / Length / Offset from `clip->getPosition()`
  (`ClipPosition::getStart/getLength/getOffset`, in seconds).
- **Large waveform** — for a `te::WaveAudioClip`, a `te::SmartThumbnail` drawn across the right
  half of the drawer (same SOURCE-time-window technique as `AudioClipComponent` in the arrange
  view: scale offset/length by `getSpeedRatio()`).

Gain / mute / fades / waveform are only shown for audio clips (`AudioClipBase`); a non-audio or
non-wave clip shows just name + timing. `setClip(nullptr)` shows a centred hint
"Select a clip to inspect it". Any edit fires `onEditMutated()`.

**Lifetime safety:** the clip is held as a `te::Clip::Ptr` (the engine's reference-counted
handle), so the Inspector never dereferences a clip deleted out from under it. The shell also
re-calls `setClip(nullptr)` on structural rebuilds, so a stale clip is never shown across a
rebuild. All clip APIs were verified against the real engine headers
(`tracktion_Clip.h`, `tracktion_AudioClipBase.h`, `tracktion_WaveAudioClip.h`,
`tracktion_EditTime.h` for `ClipPosition`).

## Public API (exact signatures)

```cpp
class DetailView : public juce::Component
{
public:
    DetailView();
    ~DetailView() override;

    void setClip (te::Clip*);          // null -> centred "Select a clip to inspect it" hint
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onEditMutated;   // fired after a property edit, so the shell saves
};
```

This matches the contract exactly (the `~DetailView() override = default` is additive and does
not change the contract surface).

## How the shell wires this

All edits below are in files owned by the orchestrator — DetailView itself touches nothing else.

### 1. `CMakeLists.txt` — add the source file

In the `target_sources(Forge PRIVATE …)` list (currently lines 30–38), add the new `.cpp`
alongside the other UI sources, e.g. right after the mixer line:

```cmake
    src/ui/mixer/MixerView.cpp
    src/ui/detail/DetailView.cpp      # <-- add this line
    src/ui/ControlBar.cpp)
```

### 2. `src/main.cpp` — include + member

Add the include near the other UI includes:

```cpp
#include "ui/detail/DetailView.h"
```

Replace the `drawerPanel` Label member with a `DetailView`. Current line 307:

```cpp
    Label browserPanel, drawerPanel;
```

becomes:

```cpp
    Label browserPanel;
    DetailView detailView;
```

### 3. `src/main.cpp` — addAndMakeVisible

Current line 168:

```cpp
        addAndMakeVisible (drawerPanel);
```

becomes:

```cpp
        addAndMakeVisible (detailView);
```

### 4. `src/main.cpp` — `resized()` placement

In `resized()`, the Detail-drawer block (currently lines 271–278) references `drawerPanel`:

```cpp
        drawerPanel.setVisible (drawerVisible);
        drawerResizer.setVisible (drawerVisible);
        if (drawerVisible)
        {
            const int h = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);
            drawerPanel.setBounds (centre.removeFromBottom (h));
            drawerResizer.setBounds (centre.removeFromBottom (resizerThickness));
        }
```

Replace the two `drawerPanel.` lines with `detailView.`:

```cpp
        detailView.setVisible (drawerVisible);
        drawerResizer.setVisible (drawerVisible);
        if (drawerVisible)
        {
            const int h = jlimit (drawerMinHeight, drawerMaxHeight, drawerHeight);
            detailView.setBounds (centre.removeFromBottom (h));
            drawerResizer.setBounds (centre.removeFromBottom (resizerThickness));
        }
```

### 5. `src/main.cpp` — `setupPlaceholders()`

Remove the `drawerPanel` styling line (current line 354):

```cpp
        style (drawerPanel,  "Detail editor — double-click a clip\n(audio editor, then piano-roll) — Phase 4");
```

Delete it. `detailView` paints its own empty state (the "Select a clip to inspect it" hint), so
no placeholder is needed. The `style` lambda still styles `browserPanel`, so it stays.

### 6. `src/main.cpp` — wire selection + save

In the constructor where the arrange-view callbacks are wired (near line 191, where
`arrangeView.onEditMutated = …` is set), add:

```cpp
        // Drive the clip Inspector from arrange-view selection, opening the drawer on select.
        arrangeView.onClipSelected = [this] (te::Clip* c)
        {
            detailView.setClip (c);
            if (c != nullptr) { drawerVisible = true; resized(); }
        };

        // Persist any property the Inspector mutates.
        detailView.onEditMutated = [this] { session.save(); };
```

Note `arrangeView.onClipSelected` is currently unwired (`onClipSelected/onTrackSelected ->
Inspector are left unwired until that feature exists` — that comment in the ctor can be updated).
`ArrangeView::clearSelection()` calls `onClipSelected(nullptr)`, so clicking empty lane area
correctly resets the Inspector to its hint state.

**Structural-change guard (recommended):** wherever the shell triggers an `arrangeView.rebuild()`
that may delete clips (open/new/import, or after a delete-track/delete-clip mutation), call
`detailView.setClip(nullptr)` so the Inspector drops its handle and shows the hint. The
`Clip::Ptr` already prevents a dangling dereference, but clearing keeps the UI consistent with a
clip that no longer exists. If `onClipSelected(nullptr)` already fires on those paths (it does on
`clearSelection`), no extra call is needed.

## Unfinished

- **Fade curve type** (`setFadeInType/setFadeOutType`, `AudioFadeCurve::Type` =
  linear/convex/concave/sCurve) is not surfaced — only fade *lengths* are editable. Deferred to
  keep the first Inspector lean; the setters are verified and easy to add later as a ComboBox.
- **Pan** (`AudioClipBase::setPan/getPan`) is intentionally omitted from the clip Inspector to
  avoid confusion with the track-level pan in the mixer; can be added if desired.
- **Live external refresh:** the Inspector reads clip state on `setClip()`/edit only; it does not
  listen to the clip's ValueTree, so a property changed elsewhere (e.g. a future drag-fade handle
  on the clip itself) won't update the open Inspector until re-selected. Matches the project's
  manual-rebuild model (no ValueTree-listener storms) used by ArrangeView/MixerView.
- **Editable start/length/offset:** kept read-only. `setStart/setLength/setOffset` exist but take
  preserveSync/keepLength flags whose correct UI semantics need design; drag-to-move in the
  arrange view already covers start. Shown read-only and documented as intended.

## Risks to verify at build time

- **Module visibility:** DetailView.cpp includes only `"ui/detail/DetailView.h"` (which includes
  `<JuceHeader.h>`) and `"ui/ForgeLookAndFeel.h"`. `te::AudioClipBase`, `te::WaveAudioClip`,
  `te::SmartThumbnail`, `te::ClipPosition`, `te::TimeRange`, `te::TimeDuration` are all reachable
  transitively via the tracktion module the same way `ArrangeView.cpp` reaches them (ArrangeView
  uses the identical set with only `<JuceHeader.h>`). If a forward-decl issue surfaces, mirror
  ArrangeView's include set.
- **`Clip::Ptr` assignment from `te::Clip*`:** `te::Clip::Ptr` is
  `juce::ReferenceCountedObjectPtr<Clip>`; assigning a raw `Clip*` (incl. nullptr) is supported and
  is how the engine hands clips around. Verify no ambiguity warning under MSVC.
- **`AudioClipBase` colour/slider IDs:** all JUCE colour IDs used were confirmed present
  (`Label::backgroundWhenEditingColourId/textWhenEditingColourId/outlineColourId`,
  `ToggleButton::tickColourId/tickDisabledColourId`, the `Slider::*` IDs the mixer also uses).
- **SmartThumbnail repaint target:** constructed with `*this` as the component-to-repaint and
  `&wac->edit` as the Edit (same as ArrangeView). The thumbnail is reset/rebuilt on every
  `setClip()`, so it never outlives the inspected wave clip.
- **No build performed here** (shared build dir). The orchestrator builds after applying the
  integration edits above.
