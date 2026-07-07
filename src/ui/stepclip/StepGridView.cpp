#include "ui/stepclip/StepGridView.h"
#include "ui/ForgeLookAndFeel.h"

#include <cmath>   // std::round for crisp cell edges

using namespace juce;

namespace
{
    // Left label column: shows each channel's drum-lane name. Wide enough for "Bass Drum" etc.
    constexpr int kGutterW = 120;

    // Every-N-step grouping divider (a 4-step beat in a 16-step / 4-4 pattern).
    constexpr int kBeatGroup = 4;

    // Live grid geometry, derived FRESH from the clip each paint / gesture (R1 — nothing cached).
    struct GridGeometry
    {
        int   rows    = 0;          // channels
        int   cols    = 0;          // pattern steps
        int   gridX   = kGutterW;   // left edge of the step grid (right of the label gutter)
        int   gridW   = 0;          // step-grid width (bounds width minus the gutter)
        int   height  = 0;
        float cellW   = 0.0f;
        float cellH   = 0.0f;
        bool  isValid = false;      // true only when there is a paintable grid (no div-by-zero)
    };

    // Re-resolves rows/cols and cell sizes from the LIVE clip. Never stores a Channel*/Pattern.
    GridGeometry computeGeometry (te::StepClip* clip, Rectangle<int> bounds)
    {
        GridGeometry g;
        g.gridX  = kGutterW;
        g.gridW  = jmax (0, bounds.getWidth() - kGutterW);
        g.height = bounds.getHeight();

        if (clip == nullptr)
            return g;

        g.rows = clip->getChannels().size();
        if (! clip->getPatterns().isEmpty())               // StepClip::getNumPatterns() is declared-but-undefined
            g.cols = clip->getPattern (0).getNumNotes();   // in the vendored engine — use getPatterns(). Pattern
                                                           // is BY VALUE — used, not stored (R1).

        if (g.rows > 0)
            g.cellH = (float) g.height / (float) g.rows;
        if (g.cols > 0 && g.gridW > 0)
            g.cellW = (float) g.gridW / (float) g.cols;

        g.isValid = (g.rows > 0 && g.cols > 0 && g.gridW > 0 && g.height > 0);
        return g;
    }

    // Maps a pointer position to a (channel, step) cell. Returns false for the gutter / outside the
    // grid / an empty clip — the caller treats that as a no-op.
    bool cellAtPoint (te::StepClip* clip, Rectangle<int> bounds, Point<int> pos,
                      int& outChannel, int& outStep)
    {
        const auto g = computeGeometry (clip, bounds);
        if (! g.isValid)
            return false;

        if (pos.x < g.gridX || pos.y < 0 || pos.y >= g.height)
            return false;   // label gutter or above/below the grid -> no cell

        const int step = (int) ((float) (pos.x - g.gridX) / g.cellW);
        const int chan = (int) ((float)  pos.y            / g.cellH);

        if (! isPositiveAndBelow (step, g.cols) || ! isPositiveAndBelow (chan, g.rows))
            return false;

        outChannel = chan;
        outStep    = step;
        return true;
    }
}

//==============================================================================
StepGridView::StepGridView()
{
    // Every pixel is painted (fillAll + gutter + cells + dividers), so opaque paints are safe & cheap.
    setOpaque (true);
}

StepGridView::~StepGridView()
{
    // Never outlive the broadcaster: drop our listener registration FIRST (clip is the only handle).
    if (clip != nullptr)
        clip->removeChangeListener (this);
}

//==============================================================================
void StepGridView::setStepClip (te::StepClip* c)
{
    if (clip.get() == c)
        return;   // already bound to this clip (or both null) -> nothing to re-register

    if (clip != nullptr)
        clip->removeChangeListener (this);

    clip = c;   // raw -> Ptr: retains the clip (keeps a since-deleted clip alive-but-parentless)

    if (clip != nullptr)
        clip->addChangeListener (this);

    // Reset any in-flight paint gesture so a rebind never continues a drag onto the new clip.
    painting = false;
    lastPaintedChannel = -1;
    lastPaintedStep    = -1;

    repaint();
}

te::StepClip* StepGridView::getStepClip() const noexcept
{
    return clip.get();
}

//==============================================================================
void StepGridView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // An external edit / undo / redo mutated the bound clip -> reflect it. Geometry + cell states are
    // re-resolved live in paint(), so a repaint is the whole story (nothing cached to invalidate).
    repaint();
}

void StepGridView::resized()
{
    // Geometry is derived live from the clip in paint()/the mouse handlers, so a resize only needs a
    // repaint — there is no cached layout to rebuild and no child components to position.
    repaint();
}

//==============================================================================
void StepGridView::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.fillAll (Colour (ForgeLookAndFeel::panelBg));

    // --- Empty state (no clip bound) -------------------------------------------------------------
    if (clip == nullptr)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.65f));
        g.setFont (FontOptions (14.0f));
        g.drawText ("No step clip - create one from a Session slot",
                    bounds, Justification::centred, false);
        return;
    }

    const auto  geo      = computeGeometry (clip.get(), bounds);
    const auto& channels = clip->getChannels();

    // Label gutter background (a step darker than the grid so the lanes read as a header column).
    g.setColour (Colour (ForgeLookAndFeel::shellBg));
    g.fillRect (0, 0, kGutterW, bounds.getHeight());

    // --- Bound but empty (no channels / no steps): a dim hint, no grid ----------------------------
    if (! geo.isValid)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.6f));
        g.setFont (FontOptions (13.0f));
        g.drawText ("Empty step clip",
                    bounds.withTrimmedLeft (kGutterW), Justification::centred, false);

        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (kGutterW - 1, 0, 1, bounds.getHeight());
        return;
    }

    // --- Step cells: inactive = raisedBg, ACTIVE (getCell true) = playGreen ("will sound") -------
    // A 1px inset per cell leaves the darker panelBg backdrop showing through as a hairline border.
    for (int row = 0; row < geo.rows; ++row)
    {
        const int y  = (int) std::round ((float)  row       * geo.cellH);
        const int y2 = (int) std::round ((float) (row + 1)  * geo.cellH);
        const int rh = jmax (1, y2 - y);

        for (int col = 0; col < geo.cols; ++col)
        {
            const int x  = geo.gridX + (int) std::round ((float)  col       * geo.cellW);
            const int x2 = geo.gridX + (int) std::round ((float) (col + 1)  * geo.cellW);
            const int cw = jmax (1, x2 - x);

            const bool active = clip->getCell (0, row, col);
            g.setColour (active ? Colour (ForgeLookAndFeel::playGreen)
                                : Colour (ForgeLookAndFeel::raisedBg));
            g.fillRect (Rectangle<int> (x, y, cw, rh).reduced (1));
        }
    }

    // --- Beat-group dividers: a slightly brighter line every kBeatGroup steps ---------------------
    g.setColour (Colour (ForgeLookAndFeel::hairline).brighter (0.35f));
    for (int col = 0; col <= geo.cols; col += kBeatGroup)
    {
        const int x = geo.gridX + (int) std::round ((float) col * geo.cellW);
        g.fillRect (x, 0, 1, bounds.getHeight());
    }

    // --- Row labels: channel (drum-lane) names + faint per-row separators in the gutter ----------
    g.setFont (FontOptions (12.0f));
    for (int row = 0; row < geo.rows; ++row)
    {
        const int y  = (int) std::round ((float)  row      * geo.cellH);
        const int y2 = (int) std::round ((float) (row + 1) * geo.cellH);
        const int rh = jmax (1, y2 - y);

        g.setColour (Colour (ForgeLookAndFeel::hairline));
        g.fillRect (0, y, kGutterW, 1);

        if (auto* ch = channels[row])
        {
            g.setColour (Colour (ForgeLookAndFeel::textPrim));
            g.drawText (ch->getDisplayName(),
                        Rectangle<int> (8, y, kGutterW - 12, rh),
                        Justification::centredLeft, true);
        }
    }

    // Gutter right border separates the label column from the grid.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (kGutterW - 1, 0, 1, bounds.getHeight());
}

//==============================================================================
void StepGridView::mouseDown (const MouseEvent& e)
{
    if (clip == nullptr)
        return;

    int channelIndex = -1, stepIndex = -1;
    if (! cellAtPoint (clip.get(), getLocalBounds(), e.getPosition(), channelIndex, stepIndex))
    {
        painting = false;   // a press on the gutter / outside the grid is inert
        return;
    }

    // Ableton-style paint: the pressed cell's NEW value seeds the whole drag.
    paintValue = ! clip->getCell (0, channelIndex, stepIndex);
    painting   = true;
    lastPaintedChannel = -1;
    lastPaintedStep    = -1;

    paintCell (channelIndex, stepIndex);
    repaint();
}

void StepGridView::mouseDrag (const MouseEvent& e)
{
    if (clip == nullptr || ! painting)
        return;

    int channelIndex = -1, stepIndex = -1;
    if (! cellAtPoint (clip.get(), getLocalBounds(), e.getPosition(), channelIndex, stepIndex))
        return;

    if (channelIndex == lastPaintedChannel && stepIndex == lastPaintedStep)
        return;   // still over the same cell -> nothing to apply (and no onEditMutated re-fire)

    paintCell (channelIndex, stepIndex);
    repaint();
}

void StepGridView::mouseUp (const MouseEvent&)
{
    painting = false;
    lastPaintedChannel = -1;
    lastPaintedStep    = -1;
}

void StepGridView::paintCell (int channelIndex, int stepIndex)
{
    if (clip == nullptr)
        return;

    // Mark the cell as painted regardless, so the drag never re-visits / re-fires on it.
    lastPaintedChannel = channelIndex;
    lastPaintedStep    = stepIndex;

    // Only write + seal an undo step when the value actually changes, so a paint drag crossing cells
    // already at paintValue does NOT flood the undo stack with no-op transactions.
    if (clip->getCell (0, channelIndex, stepIndex) == paintValue)
        return;

    clip->setCell (0, channelIndex, stepIndex, paintValue);   // undoable, message-thread
    notifyMutated();
}

void StepGridView::notifyMutated()
{
    if (onEditMutated != nullptr)
        onEditMutated();
}
