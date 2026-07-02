#include "ui/arrange/AutomationLane.h"
#include "ui/arrange/ArrangeView.h"
#include "ui/ForgeLookAndFeel.h"
#include "engine/AutomationHelpers.h"
#include "core/Log.h"

using namespace juce;

namespace
{
    constexpr int handleSize      = 7;   // square point handles (px)
    constexpr int handleGrabPad   = 2;   // extra hit-test margin around a handle (px)
    constexpr int valueInsetY     = 4;   // px kept clear at the lane's top/bottom so extreme-value handles stay visible
}

//==============================================================================
AutomationLane::AutomationLane (TimelineView& v, te::AudioTrack& t, Param initialParam)
    : view (v), track (t), param (initialParam)
{
    // Compact Volume | Pan selector living in the header column. Item IDs are (int) Param + 1 so
    // id 0 (no selection) is never a valid parameter; colours come from the shared LookAndFeel's
    // ComboBox colour IDs (same as ArrangeView's snap selector), so no per-instance overrides.
    paramSelector.addItem ("Volume", (int) Param::volume + 1);
    paramSelector.addItem ("Pan",    (int) Param::pan    + 1);

    paramSelector.setTooltip ("Parameter shown in this automation lane");
    paramSelector.setJustificationType (Justification::centredLeft);
    paramSelector.setSelectedId ((int) param + 1, dontSendNotification);

    paramSelector.onChange = [this]
    {
        const int id = paramSelector.getSelectedId();
        if (id <= 0)
            return;

        const auto newParam = static_cast<Param> (id - 1);
        if (newParam == param)
            return;

        param      = newParam;
        dragIndex  = -1;    // indices belong to the previous parameter's curve
        hoverIndex = -1;

        refreshCurveListener();   // follow the newly shown parameter's curve

        if (onParamChanged != nullptr)
            onParamChanged (param);

        repaint();
    };

    addAndMakeVisible (paramSelector);

    refreshCurveListener();
}

AutomationLane::~AutomationLane()
{
    if (listenedParam != nullptr)
        listenedParam->removeListener (this);
}

void AutomationLane::curveHasChanged (te::AutomatableParameter&)
{
    // Message-thread listener callback (curve edits are ValueTree-driven). Repaint-only: our own
    // gestures also land here, which is a harmless duplicate of their explicit repaint.
    repaint();
}

void AutomationLane::refreshCurveListener()
{
    if (listenedParam != nullptr)
        listenedParam->removeListener (this);

    listenedParam = resolveParam();

    if (listenedParam != nullptr)
        listenedParam->addListener (this);
}

//==============================================================================
te::AutomatableParameter::Ptr AutomationLane::resolveParam() const
{
    // Re-fetched on every use, never cached: structural edits can destroy the plugin (and with it
    // the parameter), so the lane resolves from its track on demand — the same discipline the
    // session pads apply to clip slots (the R1 rule).
    return param == Param::pan ? AutomationHelpers::getTrackPanParam (track)
                               : AutomationHelpers::getTrackVolumeParam (track);
}

Rectangle<int> AutomationLane::bodyArea() const
{
    return getLocalBounds().withTrimmedLeft (ArrangeLayout::headerW);
}

Range<float> AutomationLane::paramRange() const
{
    // Units from the AutomationHelpers contract: volume is fader slider position 0..1 (NOT dB),
    // pan is -1..+1.
    return param == Param::pan ? Range<float> (-1.0f, 1.0f)
                               : Range<float> (0.0f, 1.0f);
}

float AutomationLane::valueToY (float value, const Rectangle<int>& body) const
{
    const auto range     = paramRange();
    const float fraction = jlimit (0.0f, 1.0f,
                                   (value - range.getStart()) / jmax (1.0e-6f, range.getLength()));
    const float usable   = (float) jmax (1, body.getHeight() - 2 * valueInsetY);

    return (float) (body.getBottom() - valueInsetY) - fraction * usable;
}

float AutomationLane::yToValue (int y, const Rectangle<int>& body) const
{
    const auto range     = paramRange();
    const float usable   = (float) jmax (1, body.getHeight() - 2 * valueInsetY);
    const float fraction = ((float) (body.getBottom() - valueInsetY) - (float) y) / usable;

    return jlimit (range.getStart(), range.getEnd(),
                   range.getStart() + fraction * range.getLength());
}

Rectangle<int> AutomationLane::handleRect (te::AutomatableParameter& p, int index,
                                           const Rectangle<int>& body) const
{
    const auto t = te::TimePosition::fromSeconds (AutomationHelpers::getPointTime (p, index));
    const int  x = body.getX() + view.timeToX (t, body.getWidth());
    const int  y = roundToInt (valueToY (AutomationHelpers::getPointValue (p, index), body));

    return Rectangle<int> (handleSize, handleSize).withCentre ({ x, y });
}

int AutomationLane::hitTestHandle (Point<int> pos) const
{
    auto p = resolveParam();
    if (p == nullptr)
        return -1;

    const auto body = bodyArea();
    if (body.getWidth() <= 0)
        return -1;

    // Descending order so the hit test matches paint z-order: overlapping handles paint last-on-
    // top, so the LAST index under the cursor is the one the user sees (and must be the one a
    // right-click Delete removes — QC caught the ascending scan targeting the hidden point).
    for (int i = AutomationHelpers::getNumPoints (*p); --i >= 0;)
        if (handleRect (*p, i, body).expanded (handleGrabPad).contains (pos))
            return i;

    return -1;
}

//==============================================================================
void AutomationLane::paint (Graphics& g)
{
    using namespace ArrangeLayout;

    // Body background: a step darker than the clip lanes so the sub-lane reads as an attachment
    // of the track above, not another track; the header column matches the track header strip.
    g.setColour (Colour (ForgeLookAndFeel::automationBg));
    g.fillRect (getLocalBounds());

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (getLocalBounds().removeFromLeft (headerW));

    const auto body = bodyArea();

    if (body.getWidth() > 0 && body.getHeight() > 0)
    {
        // Pan is bipolar: a subtle centre line marks value 0 so the curve reads against it.
        if (param == Param::pan)
        {
            const int midY = roundToInt (valueToY (0.0f, body));
            g.setColour (Colour (ForgeLookAndFeel::hairline));
            g.fillRect (body.getX(), midY, body.getWidth(), 1);
        }

        auto p = resolveParam();

        if (p != nullptr)
        {
            const int n = AutomationHelpers::getNumPoints (*p);

            if (n == 0)
            {
                // No points yet: a dimmed flat line at the parameter's current value shows where
                // the curve sits and hints that a click adds the first point.
                const float y = valueToY (p->getCurrentValue(), body);

                g.setColour (Colour (ForgeLookAndFeel::automationCurve).withAlpha (0.35f));
                g.drawLine ((float) body.getX(), y, (float) body.getRight(), y, 1.5f);
            }
            else
            {
                // Point-to-point polyline (lane-authored points are linear, curveShape 0), held
                // flat out to the lane edges to match the engine's outside-the-points behaviour.
                // x mapping is the shared TimelineView::timeToX over the body width — exactly the
                // clips' mapping, so curve / clip / playhead alignment is pixel-exact.
                Path curve;
                curve.startNewSubPath ((float) body.getX(),
                                       valueToY (AutomationHelpers::getPointValue (*p, 0), body));

                for (int i = 0; i < n; ++i)
                {
                    const auto t = te::TimePosition::fromSeconds (AutomationHelpers::getPointTime (*p, i));
                    curve.lineTo ((float) (body.getX() + view.timeToX (t, body.getWidth())),
                                  valueToY (AutomationHelpers::getPointValue (*p, i), body));
                }

                curve.lineTo ((float) body.getRight(),
                              valueToY (AutomationHelpers::getPointValue (*p, n - 1), body));

                g.setColour (Colour (ForgeLookAndFeel::automationCurve));
                g.strokePath (curve, PathStrokeType (1.5f));

                // Square point handles, hover-highlighted. The outline is the shell colour so a
                // handle stays crisp over both the curve and the lane background.
                for (int i = 0; i < n; ++i)
                {
                    const auto r = handleRect (*p, i, body);

                    g.setColour (i == hoverIndex ? Colour (ForgeLookAndFeel::accent)
                                                 : Colour (ForgeLookAndFeel::automationCurve));
                    g.fillRect (r);
                    g.setColour (Colour (ForgeLookAndFeel::shellBg).withAlpha (0.8f));
                    g.drawRect (r);
                }
            }
        }
        else
        {
            // Track has no volume/pan plugin. Paint stays silent (no logging on a paint path);
            // the mutating seams log when an edit is actually attempted.
            g.setColour (Colour (ForgeLookAndFeel::textSec));
            g.setFont (Font (FontOptions (11.0f)));
            g.drawText ("No automatable parameter", body, Justification::centred, false);
        }
    }

    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.drawRect (getLocalBounds());
}

void AutomationLane::resized()
{
    using namespace ArrangeLayout;

    // Compact param selector in the header column, aligned past the track header's swatch column.
    auto header = getLocalBounds().removeFromLeft (headerW);
    header.removeFromLeft (10);
    paramSelector.setBounds (header.withSizeKeepingCentre (header.getWidth() - 8, 20));
}

//==============================================================================
void AutomationLane::mouseDown (const MouseEvent& e)
{
    const auto body = bodyArea();

    if (e.mods.isPopupMenu())
    {
        const int handle = hitTestHandle (e.getPosition());

        if (handle >= 0)
            showHandleContextMenu (handle);
        else if (body.contains (e.getPosition()))
            showLaneContextMenu();

        return;
    }

    dragIndex   = hitTestHandle (e.getPosition());
    dragMutated = false;

    // Left-click on the empty body adds a point at the cursor time/value; the fresh point is
    // immediately draggable (a plain click just leaves it where it was added).
    if (dragIndex < 0 && body.getWidth() > 0 && body.contains (e.getPosition()))
    {
        auto p = resolveParam();

        if (p == nullptr)
        {
            FORGE_LOG_ERROR ("Automation: track '" + track.getName()
                             + "' has no volume/pan plugin — cannot add point");
            return;
        }

        const double seconds = jmax (0.0, view.xToTime (e.x - body.getX(), body.getWidth()).inSeconds());
        const float  value   = yToValue (e.y, body);

        dragIndex   = AutomationHelpers::addPoint (*p, seconds, value);
        dragMutated = true;
        repaint();
    }
}

void AutomationLane::mouseDrag (const MouseEvent& e)
{
    if (dragIndex < 0 || e.mods.isPopupMenu())
        return;

    auto p = resolveParam();
    if (p == nullptr)
        return;

    const auto body = bodyArea();
    if (body.getWidth() <= 0)
        return;

    // Live engine move (the curve is the single source of truth paint() reads back): value is
    // clamped to the parameter range by yToValue, time to >= 0 AND to the visible window's right
    // edge (an unclamped drag past the lane edge would maroon the point beyond the timeline where
    // no handle can ever grab it back — QC minor). movePoint returns the point's resulting sorted
    // index, which we adopt so dragging past a neighbour keeps tracking the same point.
    const int    clampedX = jlimit (0, body.getWidth(), e.x - body.getX());
    const double seconds  = jmax (0.0, view.xToTime (clampedX, body.getWidth()).inSeconds());
    const float  value    = yToValue (e.y, body);

    dragIndex   = AutomationHelpers::movePoint (*p, dragIndex, seconds, value);
    dragMutated = true;
    repaint();
}

void AutomationLane::mouseUp (const MouseEvent&)
{
    dragIndex = -1;

    // One save per completed gesture (mirrors the clips' drag-commit): the intermediate helper
    // calls already committed the engine stream; this only fires the shell's autosave hook.
    if (dragMutated)
    {
        dragMutated = false;
        notifyEditMutated();
    }
}

void AutomationLane::mouseMove (const MouseEvent& e)
{
    // Edge-gated hover tracking for the handle highlight (repaint only on change, never logged).
    const int hit = hitTestHandle (e.getPosition());

    if (hit != hoverIndex)
    {
        hoverIndex = hit;
        repaint();
    }
}

void AutomationLane::mouseExit (const MouseEvent&)
{
    if (hoverIndex != -1)
    {
        hoverIndex = -1;
        repaint();
    }
}

//==============================================================================
void AutomationLane::selectParam (Param p)
{
    // Route through the selector so its shown text and the switch logic stay on one path; the
    // onChange handler no-ops when the parameter is unchanged.
    paramSelector.setSelectedId ((int) p + 1, sendNotificationSync);
}

void AutomationLane::showHandleContextMenu (int pointIndex)
{
    Component::SafePointer<AutomationLane> safeThis (this);

    PopupMenu menu;
    menu.addItem ("Delete Point", [safeThis, pointIndex]
    {
        if (safeThis == nullptr)
            return;

        auto p = safeThis->resolveParam();

        if (p == nullptr)
        {
            FORGE_LOG_ERROR ("Automation: track '" + safeThis->track.getName()
                             + "' has no volume/pan plugin — cannot delete point");
            return;
        }

        if (! AutomationHelpers::removePoint (*p, pointIndex))
        {
            FORGE_LOG_WARN ("Automation: point index " + String (pointIndex)
                            + " out of range on track '" + safeThis->track.getName()
                            + "' — delete skipped");
            return;
        }

        safeThis->hoverIndex = -1;
        safeThis->notifyEditMutated();
        safeThis->repaint();
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

void AutomationLane::showLaneContextMenu()
{
    Component::SafePointer<AutomationLane> safeThis (this);

    PopupMenu menu;
    menu.addItem ("Volume", true, param == Param::volume, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->selectParam (Param::volume);
    });

    menu.addItem ("Pan", true, param == Param::pan, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->selectParam (Param::pan);
    });

    menu.addSeparator();

    menu.addItem ("Clear Automation", [safeThis]
    {
        if (safeThis == nullptr)
            return;

        auto p = safeThis->resolveParam();

        if (p == nullptr)
        {
            FORGE_LOG_ERROR ("Automation: track '" + safeThis->track.getName()
                             + "' has no volume/pan plugin — cannot clear automation");
            return;
        }

        AutomationHelpers::clearAutomation (*p);
        safeThis->hoverIndex = -1;
        safeThis->notifyEditMutated();
        safeThis->repaint();
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

void AutomationLane::notifyEditMutated()
{
    if (onEditMutated != nullptr)
        onEditMutated();
}
