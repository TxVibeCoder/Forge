/*
    SessionLayout — geometry constants for the SessionView clip-launch grid.

    Single source of truth for the grid's fixed pixel dimensions (track column width,
    pinned scene column width, header / stop-row heights, slot inset, scene count) and the
    one sanctioned raw lane-background colour literal (matches ArrangeView's lane fill).

    Header-only (#pragma once, constexpr). Every SessionView component includes this so the
    grid stays pixel-consistent across columns, pads, and the pinned scene column.

    Mirrors sheet 00 (docs/devlog/session-design.md §b).
*/

#pragma once

#include <JuceHeader.h>

namespace SessionLayout
{
    constexpr int trackColW = 179;               // per-track column width (grid_w / 8 ≈ 179, sheet 00)
    constexpr int sceneColW = 168;               // pinned right-hand scene column width
    constexpr int headerH   = 78;                // track header band height
    constexpr int stopRowH  = 30;                // per-track clip-stop footer height
    constexpr int slotPad   = 3;                 // inset inside each pad
    constexpr int numScenes = 16;                // default visible scene rows (MVP grid height)
    constexpr int gap       = 0;                 // columns are flush (hairline-separated)
    constexpr int slotH     = 46;                // per-slot pad height used to SIZE the scrollable content
                                                 // (columns then divide their middle region evenly across
                                                 // numScenes); matches sheet 00 row pitch. Promoted from the
                                                 // former file-local kSlotH in SessionView.cpp.

    constexpr juce::uint32 laneBg = 0xff262626;  // lane background — matches ArrangeView lane literal

    /** Shared row partition so the track columns and the pinned scene column ALWAYS agree on each
        row's vertical band. `height` is the middle region (between the header and the stop-row footer).
        Integer-floor rows with the rounding remainder absorbed by the last row. Both
        TrackColumnComponent::resized and SceneColumnComponent::resized call this, so clip pad row N
        and scene launch row N share identical bounds at any window height (they no longer drift). */
    inline juce::Range<int> rowBand (int rowIndex, int rowCount, int height) noexcept
    {
        const int n    = juce::jmax (1, rowCount);
        const int rowH = juce::jmax (1, height / n);
        const int y0   = rowIndex * rowH;
        const int y1   = (rowIndex >= n - 1) ? height : (rowIndex + 1) * rowH;
        return { y0, y1 };
    }
}
