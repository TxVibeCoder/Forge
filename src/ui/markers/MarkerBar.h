/*
    MarkerBar — a thin horizontal strip of timeline markers (cue points) drawn over the arrange
    clip area, aligned to the SAME shared TimelineView the ArrangeView ruler / playhead use.

    Gestures (message-thread only):
      - left-click empty space  -> add a marker at that time
      - single-click a marker   -> jump the transport to it
      - drag a marker           -> move it (committed on mouse-up)
      - double-click a marker   -> rename it (its first click is an ordinary single click and still
                                   jumps, as in most DAWs; the redundant second-click jump is suppressed)
      - right-click             -> context menu (Add here / Rename / Delete)

    The bar makes NO raw te:: calls and caches NO te::MarkerClip* / Clip* — it holds only value rows
    {id, time, name} pulled from a getter the shell wires to ProjectSession::getMarkers(), and it
    drives every mutation through value-typed std::function callbacks the shell wires to the proposed
    ProjectSession markers seam (addMarker / removeMarker / moveMarker / renameMarker /
    jumpTransportTo). This mirrors the SessionView R1 threading rule (never store an engine pointer
    across a repaint) and the "views make no engine calls they don't have to" principle.

    Alignment: the bar maps time<->x through the shared TimelineView exactly like the ruler. It uses
    the ruler's placement contract — the shell positions the bar over the CLIP AREA (x = headerW,
    width = clipAreaW), so the bar's local coordinates already start at viewStart (headerInset stays
    0). If instead the bar is mounted spanning the full arrange width, call setHeaderInset(headerW)
    and the header offset is excluded from the timeToX span (markers draw / hit-test over the clip
    area only). Manual refresh() model — no ValueTree listener (mirrors ArrangeView::rebuild).

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/arrange/ArrangeView.h"   // TimelineView (shared time<->x mapping) — read-only

namespace te = tracktion;

//==============================================================================
class MarkerBar : public juce::Component
{
public:
    /** One marker row, mirroring the proposed ProjectSession::getMarkers() shape. `id` is the
        marker clip's te::EditItemID — the engine's `const`, edit-wide-unique handle (EditItem::itemID)
        — so a dragged / renamed marker matches back to its engine object across a refresh. It is
        deliberately NOT the marker NUMBER (te::MarkerClip::getMarkerID()): the NUMBER is mutable and
        gets reassigned by the engine's duplicate-resolution, so it is not a stable key. The bar
        caches ONLY these value rows — never a te::MarkerClip*. */
    struct Marker
    {
        te::EditItemID   id {};
        te::TimePosition time {};
        juce::String     name;
    };

    /** Preferred strip height in px; the shell uses this to carve the bar's slot in the arrange region. */
    static constexpr int preferredHeight = 20;

    explicit MarkerBar (TimelineView&);

    //==============================================================================
    // Data.

    /** Re-pulls the marker list from getMarkers() into the local cache and repaints. Call after an
        external edit change (open / new project); the bar also self-refreshes after its own gestures.
        Safe when getMarkers is unwired (clears to empty). */
    void refresh();

    /** Optional header inset in px. Leave 0 (default) when the shell positions the bar at x = headerW
        over the clip area (recommended — mirrors the ruler's `setBounds(headerW, …)`). Set to
        ArrangeLayout::headerW only when the bar spans the full arrange width, so the header offset is
        excluded from the timeToX span and markers stay aligned to the ruler. */
    void setHeaderInset (int px);

    //==============================================================================
    // Component.
    void paint (juce::Graphics&) override;
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;

    //==============================================================================
    // Callbacks — the shell wires each to the proposed ProjectSession markers seam. All optional;
    // a user gesture whose callback is unwired is logged once (WARN) and otherwise no-ops.

    /** Reads the current marker rows from the engine (shell: `return session.getMarkers();`). */
    std::function<std::vector<Marker>()> getMarkers;

    /** Add a marker at `time` with `name` (shell: `session.addMarker (time, name)`). */
    std::function<void (te::TimePosition time, const juce::String& name)> onAddMarker;

    /** Remove the marker with stable id `id` (shell: `session.removeMarker (id)`). */
    std::function<void (te::EditItemID id)> onRemoveMarker;

    /** Move marker `id` to `newTime` (shell: `session.moveMarker (id, newTime)`). */
    std::function<void (te::EditItemID id, te::TimePosition newTime)> onMoveMarker;

    /** Rename marker `id` (shell: `session.renameMarker (id, newName)`). */
    std::function<void (te::EditItemID id, const juce::String& newName)> onRenameMarker;

    /** Jump the transport to `time` (shell: `session.jumpTransportTo (time)`). */
    std::function<void (te::TimePosition time)> onJumpTransport;

    /** Fired after any successful marker mutation so the shell can persist (mirrors ArrangeView::onEditMutated). */
    std::function<void()> onEditMutated;

private:
    //==============================================================================
    // Geometry (local coords; the header inset is excluded from the timeToX span so the mapping
    // matches the ruler exactly).
    int              contentWidth() const;                 // px available for the time axis
    int              markerX (te::TimePosition) const;     // local x for a marker time, -1 if unmappable
    te::TimePosition timeAtX (int localX) const;           // inverse of markerX, clamped to >= 0
    int              markerIndexAtX (int localX) const;    // nearest marker within hit tol, else -1

    void addMarkerAt (te::TimePosition);
    void showMarkerContextMenu (int index);
    void showEmptyContextMenu (te::TimePosition atTime);
    void beginRename (te::EditItemID markerId, const juce::String& currentName);
    void afterMutation();                                  // fire onEditMutated + refresh
    juce::String defaultNewMarkerName() const;

    /** Logs a "callback unwired" WARN at most once per gesture kind (via the passed sentinel), so a
        user gesture whose seam the shell forgot to wire leaves a diagnostic without spamming the log. */
    void warnUnwired (bool& sentinel, const juce::String& gesture);

    TimelineView&       view;
    std::vector<Marker> markers;
    int                 headerInset = 0;

    // Drag state. The strip itself never moves (unlike a ClipComponent), so the anchor lives in this
    // component's own space. dragIndex >= 0 once a press lands on a marker; dragging flips true only
    // after the move threshold, so a plain click still jumps the transport.
    int              dragIndex   = -1;
    bool             dragging    = false;
    int              dragAnchorX = 0;
    te::TimePosition dragOriginTime {};   // marker time (or empty-click time) captured at mouse-down
    te::TimePosition dragLiveTime {};     // live position while dragging (drawn instead of the stored time)

    int hoverIndex = -1;                  // marker under the pointer (highlight), -1 = none

    // One-shot "callback unwired" sentinels so a repeated gesture doesn't spam the log.
    bool warnedAdd = false, warnedRemove = false, warnedMove = false, warnedRename = false, warnedJump = false;

    static constexpr int hitTolerancePx = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MarkerBar)
};
