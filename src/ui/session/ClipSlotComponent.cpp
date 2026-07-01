#include "ui/session/ClipSlotComponent.h"

using namespace juce;

//==============================================================================
ClipSlotComponent::ClipSlotComponent (int trackIndexIn, int sceneIndexIn, Colour trackColourIn)
    : trackIndex (trackIndexIn), sceneIndex (sceneIndexIn), trackColour (trackColourIn)
{
}

//==============================================================================
void ClipSlotComponent::setVisualState (SlotVisualState newState, String newLabel)
{
    // Push-only: store and repaint just this pad, and only when something actually changed
    // (keeps the 25 Hz poll cheap — no full-grid repaints).
    if (state != newState || label != newLabel)
    {
        state = newState;
        label = std::move (newLabel);
        repaint();
    }
}

void ClipSlotComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void ClipSlotComponent::setTrackColour (Colour newColour)
{
    if (trackColour != newColour)
    {
        trackColour = newColour;
        repaint();
    }
}

//==============================================================================
void ClipSlotComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClicked != nullptr)
            onRightClicked (e);

        return;
    }

    // Left button: launch immediately (responsive — no defer). A double-click therefore ALSO launches on
    // its first press; that is accepted because the LAUNCH-FREE edit path is the right-click "Edit clip"
    // item. mouseDoubleClick additionally opens the clip in the drawer (a convenience second edit
    // gesture). The parent (which holds the resolved engine state) decides launch vs. create/import.
    if (onClicked != nullptr)
        onClicked();
}

void ClipSlotComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // Convenience edit gesture (alongside the right-click "Edit clip" item): open the filled slot's clip
    // in the drawer. The first press already launched via mouseDown — accepted (right-click avoids it).
    if (onDoubleClicked != nullptr)
        onDoubleClicked();
}

//==============================================================================
void ClipSlotComponent::paint (Graphics& g)
{
    // Pad chrome per §(d), mirroring ClipComponent::paint's rounded-rect template. Inset by
    // slotPad so adjacent pads read as discrete cells. We draw on float bounds for the rounded
    // corners (3 px radius, matching the design recipe).
    const auto b = getLocalBounds().toFloat().reduced ((float) SessionLayout::slotPad);
    constexpr float corner = 3.0f;

    const bool hasClip  = (state != SlotVisualState::empty && state != SlotVisualState::recArmed
                                                           && state != SlotVisualState::recording);
    const bool queued   = (state == SlotVisualState::queued || state == SlotVisualState::stopping);
    const bool playing  = (state == SlotVisualState::playing);

    // Base lane fill (the one sanctioned raw literal, via SessionLayout::laneBg).
    g.setColour (Colour (SessionLayout::laneBg));
    g.fillRoundedRectangle (b, corner);

    // Track-colour body fill when the pad has a clip (§d: track colour @ 0.55 over laneBg).
    if (hasClip)
    {
        g.setColour (trackColour.withAlpha (0.55f));
        g.fillRoundedRectangle (b, corner);
    }
    else if (state == SlotVisualState::recArmed)
    {
        // Empty pad on an armed track: a record-tinted target so the column reads as "armed".
        g.setColour (Colour (ForgeLookAndFeel::recordRed).withAlpha (0.85f));
        g.drawRoundedRectangle (b.reduced (3.0f), corner, 1.5f);
    }
    else if (state == SlotVisualState::recording)
    {
        // The ONE slot currently capturing: a FILLED pulsing-red body (distinct from recArmed's
        // thin outline) so a mid-capture pad reads unmistakably "hot". Solid fill + a heavier ring.
        g.setColour (Colour (ForgeLookAndFeel::recordRed).withAlpha (0.45f));
        g.fillRoundedRectangle (b, corner);
        g.setColour (Colour (ForgeLookAndFeel::recordRed));
        g.drawRoundedRectangle (b.reduced (1.5f), corner, 2.0f);
    }

    // Border (black @ 0.6, mirrors ClipComponent).
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (b, corner, 1.0f);

    if (hasClip)
    {
        // Glyph + name. A filled disc + caret while playing (onAccent over the accent outline);
        // a hollow caret otherwise.
        auto textArea = b.toNearestInt().reduced (5, 3);

        const auto glyphArea = textArea.removeFromLeft (12);
        if (playing)
        {
            g.setColour (Colour (ForgeLookAndFeel::accent));
            g.fillEllipse (glyphArea.toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            g.setColour (Colour (ForgeLookAndFeel::onAccent));
        }
        else if (queued)
        {
            // Queued / stopping: an amber caret so an about-to-fire (or about-to-stop) clip reads
            // distinctly from a plain stopped clip — and from a merely-selected one, whose caret stays
            // neutral. (QC fix: previously queued state was conveyed ONLY by the accent outline, which is
            // identical to the selection outline, so a queued clip and a selected idle clip were the same.)
            g.setColour (Colour (ForgeLookAndFeel::accent));
        }
        else
        {
            g.setColour (Colour (ForgeLookAndFeel::textPrim));
        }
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6"), glyphArea, Justification::centredLeft);  // ▶

        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.drawText (label, textArea.removeFromLeft (textArea.getWidth()),
                    Justification::centredLeft, true);

        // Amber progress sliver along the bottom while playing (fixed sliver, MVP — no timeline).
        if (playing)
        {
            auto sliver = b.withTrimmedTop (b.getHeight() - 2.0f).reduced (1.0f, 0.0f);
            g.setColour (Colour (ForgeLookAndFeel::accent));
            g.fillRect (sliver);
        }
    }
    else if (state == SlotVisualState::empty)
    {
        // Empty pad: a centred secondary-text ring (the "○" affordance) so the cell reads as
        // clickable-but-empty without a heavy fill.
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText (juce::String::fromUTF8 ("\xe2\x97\x8b"), b.toNearestInt(), Justification::centred);  // ○
    }

    // Accent outline LAST (§d): 2 px for playing / queued / stopping, or selection / keyboard
    // focus. Same colour, drawn over everything else.
    if (playing || queued || selected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRoundedRectangle (b, corner, 2.0f);
    }
}
