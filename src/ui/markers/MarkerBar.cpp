#include "ui/markers/MarkerBar.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

#include <cmath>

using namespace juce;

//==============================================================================
MarkerBar::MarkerBar (TimelineView& v)
    : view (v)
{
    setOpaque (true);            // paint() fills the full bounds (incl. any header inset)
    setWantsKeyboardFocus (false);
}

//==============================================================================
void MarkerBar::refresh()
{
    if (getMarkers != nullptr)
        markers = getMarkers();
    else
        markers.clear();

    // Keep transient indices valid after the list changed underneath us.
    if (hoverIndex >= (int) markers.size()) hoverIndex = -1;
    if (dragIndex  >= (int) markers.size()) { dragIndex = -1; dragging = false; }

    repaint();
}

void MarkerBar::setHeaderInset (int px)
{
    headerInset = jmax (0, px);
    repaint();
}

//==============================================================================
int MarkerBar::contentWidth() const
{
    return jmax (0, getWidth() - headerInset);
}

int MarkerBar::markerX (te::TimePosition t) const
{
    const int cw = contentWidth();
    if (cw <= 0)
        return -1;

    return headerInset + view.timeToX (t, cw);
}

te::TimePosition MarkerBar::timeAtX (int localX) const
{
    // Exclude the header inset from the mapped span so the axis matches the ruler exactly.
    auto t = view.xToTime (localX - headerInset, contentWidth());

    if (t < te::TimePosition())
        t = te::TimePosition();

    return t;
}

int MarkerBar::markerIndexAtX (int localX) const
{
    if (localX < headerInset)
        return -1;

    int best = -1;
    int bestDist = hitTolerancePx + 1;

    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const int mx = markerX (markers[(size_t) i].time);
        if (mx < 0)
            continue;

        const int d = std::abs (localX - mx);
        if (d <= hitTolerancePx && d < bestDist)
        {
            best = i;
            bestDist = d;
        }
    }

    return best;
}

//==============================================================================
void MarkerBar::paint (Graphics& g)
{
    const auto b = getLocalBounds();

    g.fillAll (Colour (ForgeLookAndFeel::panelBg));

    // Bottom hairline, drawn only across the content area so it lines up under the ruler.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (headerInset, b.getBottom() - 1, jmax (0, b.getRight() - headerInset), 1);

    const int right = b.getRight();

    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const bool isDrag = dragging && i == dragIndex;
        const auto t      = isDrag ? dragLiveTime : markers[(size_t) i].time;
        const int  x      = markerX (t);

        if (x < headerInset || x > right)     // off the visible content area
            continue;

        const bool hot = isDrag || i == hoverIndex;
        const auto tick = hot ? Colour (ForgeLookAndFeel::accent).brighter (0.25f)
                              : Colour (ForgeLookAndFeel::accent);

        // Vertical tick line spanning the strip.
        g.setColour (tick);
        g.fillRect (x, b.getY(), 2, b.getHeight());

        // Name label to the right of the tick, clamped and ellipsised so neighbours don't collide.
        const auto& m  = markers[(size_t) i];
        const auto  nm = m.name.isNotEmpty() ? m.name : String ("Marker");
        const int   labelX = x + 4;
        const int   labelW = jmin (120, right - labelX);

        if (labelW > 8)
        {
            g.setColour (Colour (hot ? ForgeLookAndFeel::textPrim : ForgeLookAndFeel::textSec));
            g.setFont (12.0f);
            g.drawText (nm, Rectangle<int> (labelX, b.getY(), labelW, b.getHeight()),
                        Justification::centredLeft, true);
        }
    }
}

//==============================================================================
void MarkerBar::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const int idx = markerIndexAtX (e.x);
        if (idx >= 0)
            showMarkerContextMenu (idx);
        else if (e.x >= headerInset)
            showEmptyContextMenu (timeAtX (e.x));

        return;
    }

    dragging    = false;
    dragAnchorX = e.x;

    if (e.x < headerInset)       // clicks in the header region are not ours
    {
        dragIndex = -1;
        return;
    }

    const int idx = markerIndexAtX (e.x);

    if (idx >= 0)
    {
        dragIndex      = idx;
        dragOriginTime = markers[(size_t) idx].time;
        dragLiveTime   = dragOriginTime;
    }
    else
    {
        dragIndex      = -1;
        dragOriginTime = timeAtX (e.x);   // remember where an empty-area click landed (added on up)
    }
}

void MarkerBar::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (! dragging)
    {
        // A small movement threshold keeps a plain click (jump / add) from being read as a drag.
        if (std::abs (e.x - dragAnchorX) < 3)
            return;

        dragging = true;
    }

    if (dragIndex < 0)           // an empty-area drag has nothing to move
        return;

    dragLiveTime = timeAtX (e.x);
    repaint();
}

void MarkerBar::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (dragIndex >= 0)
    {
        // The press started on a marker (never treated as an empty-area add, even if the row went
        // stale). The bounds guard is belt-and-suspenders: markers only change on our own committed
        // mutations, never mid-interaction, so dragIndex stays valid between mouse-down and up.
        if (dragIndex < (int) markers.size())
        {
            if (dragging)
            {
                // Commit the move (dragLiveTime is already clamped >= 0 by timeAtX).
                const auto id = markers[(size_t) dragIndex].id;

                if (onMoveMarker != nullptr)
                {
                    onMoveMarker (id, dragLiveTime);
                    afterMutation();
                }
                else
                {
                    warnUnwired (warnedMove, "move-marker");
                }
            }
            else if (e.getNumberOfClicks() <= 1)
            {
                // A single click on a marker jumps the transport to it. The second click of a
                // double-click (the rename gesture) also lands here with dragging==false, but its
                // getNumberOfClicks()==2 — skip it so a rename doesn't fire a redundant repeat jump
                // to the same time. (The double-click's FIRST click is an ordinary single click and
                // still jumps, as in most DAWs; only the redundant re-jump is suppressed.)
                const auto t = markers[(size_t) dragIndex].time;

                if (onJumpTransport != nullptr)
                    onJumpTransport (t);
                else
                    warnUnwired (warnedJump, "jump-transport");
            }
        }
    }
    else if (! dragging)
    {
        // A plain click on empty space adds a marker at the clicked time.
        addMarkerAt (dragOriginTime);
    }

    dragIndex = -1;
    dragging  = false;
}

void MarkerBar::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.x < headerInset)
        return;

    const int idx = markerIndexAtX (e.x);
    if (idx >= 0)
        beginRename (markers[(size_t) idx].id, markers[(size_t) idx].name);
}

void MarkerBar::mouseMove (const MouseEvent& e)
{
    const int idx = (e.x >= headerInset) ? markerIndexAtX (e.x) : -1;

    if (idx != hoverIndex)
    {
        hoverIndex = idx;
        repaint();
    }

    setMouseCursor (idx >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void MarkerBar::mouseExit (const MouseEvent&)
{
    if (hoverIndex != -1)
    {
        hoverIndex = -1;
        repaint();
    }
}

//==============================================================================
void MarkerBar::addMarkerAt (te::TimePosition t)
{
    if (onAddMarker != nullptr)
    {
        onAddMarker (t, defaultNewMarkerName());
        afterMutation();
    }
    else
    {
        warnUnwired (warnedAdd, "add-marker");
    }
}

void MarkerBar::showMarkerContextMenu (int index)
{
    if (index < 0 || index >= (int) markers.size())
        return;

    const auto   id = markers[(size_t) index].id;
    const String nm = markers[(size_t) index].name;

    Component::SafePointer<MarkerBar> safeThis (this);

    PopupMenu menu;
    menu.addItem ("Rename", [safeThis, id, nm]
    {
        if (safeThis != nullptr)
            safeThis->beginRename (id, nm);
    });

    menu.addItem ("Delete", [safeThis, id]
    {
        if (safeThis == nullptr)
            return;

        if (safeThis->onRemoveMarker != nullptr)
        {
            safeThis->onRemoveMarker (id);
            safeThis->afterMutation();
        }
        else
        {
            safeThis->warnUnwired (safeThis->warnedRemove, "remove-marker");
        }
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

void MarkerBar::showEmptyContextMenu (te::TimePosition atTime)
{
    Component::SafePointer<MarkerBar> safeThis (this);
    const auto t = atTime;

    PopupMenu menu;
    menu.addItem ("Add Marker Here", [safeThis, t]
    {
        if (safeThis != nullptr)
            safeThis->addMarkerAt (t);
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

void MarkerBar::beginRename (te::EditItemID markerId, const String& currentName)
{
    Component::SafePointer<MarkerBar> safeThis (this);

    // AlertWindow kept alive by a shared_ptr captured in the modal callback so it outlives the
    // dialog until the callback reads its text editor (mirrors ArrangeView::renameClip).
    auto aw = std::make_shared<AlertWindow> ("Rename Marker", "Enter a new marker name:",
                                             MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", currentName);
    aw->addButton ("OK",     1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    aw->enterModalState (true, ModalCallbackFunction::create ([safeThis, aw, markerId] (int result)
    {
        if (result == 1 && safeThis != nullptr)
        {
            const auto newName = aw->getTextEditorContents ("name").trim();
            if (newName.isNotEmpty())
            {
                if (safeThis->onRenameMarker != nullptr)
                {
                    safeThis->onRenameMarker (markerId, newName);
                    safeThis->afterMutation();
                }
                else
                {
                    safeThis->warnUnwired (safeThis->warnedRename, "rename-marker");
                }
            }
        }
    }), false);
}

void MarkerBar::afterMutation()
{
    if (onEditMutated != nullptr)
        onEditMutated();

    refresh();
}

String MarkerBar::defaultNewMarkerName() const
{
    return "Marker " + String ((int) markers.size() + 1);
}

void MarkerBar::warnUnwired (bool& sentinel, const String& gesture)
{
    if (! sentinel)
    {
        FORGE_LOG_WARN ("MarkerBar: " + gesture + " gesture ignored — its callback is not wired");
        sentinel = true;
    }
}
