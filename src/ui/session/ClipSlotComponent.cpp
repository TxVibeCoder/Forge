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

void ClipSlotComponent::setPulseAlpha (float newPulseAlpha)
{
    // Pushed every poll tick while this pad is animated (playing / queued), and parked at the
    // negative no-pulse sentinel otherwise — the change gate below is what keeps static pads
    // repaint-free (§e): a pad repaints per tick ONLY while its pulse value is actually moving.
    if (! approximatelyEqual (pulseAlpha, newPulseAlpha))
    {
        pulseAlpha = newPulseAlpha;
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

void ClipSlotComponent::mouseUp (const MouseEvent& e)
{
    // Plain LEFT release only. A popup (right-click) interaction went through the mouseDown early
    // return and must NOT fire the release path — guard it here too so a right-click drag that ends
    // over the pad can't trip Gate's "stop on release". Trigger/Toggle modes simply leave onReleased
    // unbound, so wiring this alongside mouseDown's launch is safe (R1: no engine pointer touched).
    if (e.mods.isPopupMenu())
        return;

    if (onReleased != nullptr)
        onReleased();
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
// OS-external file drop (W07): mirrors Tracktion's ClipLauncherDemo per-slot recipe. Audio-only —
// the Session import seam (importAudioIntoSlot) is wave-only, so MIDI/other files are rejected here.

bool ClipSlotComponent::isInterestedInFileDrag (const StringArray& files)
{
    // Cheap check only (no file opening, per the JUCE contract): accept if ANY dragged file has an
    // audio extension. filesDropped re-filters and imports only the first accepted file.
    for (const auto& f : files)
        if (File (f).hasFileExtension (te::soundFileExtensions))
            return true;

    return false;
}

void ClipSlotComponent::fileDragEnter (const StringArray&, int, int)
{
    if (! dragHover)
    {
        dragHover = true;
        repaint();
    }
}

void ClipSlotComponent::fileDragExit (const StringArray&)
{
    if (dragHover)
    {
        dragHover = false;
        repaint();
    }
}

void ClipSlotComponent::filesDropped (const StringArray& files, int, int)
{
    // Clear the hover FIRST — JUCE may not fire fileDragExit after a drop (the contract note), so
    // this is the guaranteed clear path. Always repaint so the drop-ring never lingers.
    dragHover = false;
    repaint();

    // Import the FIRST accepted audio file only (a slot holds one clip — no loop-replace). Re-filter
    // (the drag may have carried a mix); bubble UP so the parent runs the ProjectSession import seam.
    for (const auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension (te::soundFileExtensions))
        {
            if (onFilesDropped != nullptr)
                onFilesDropped (file);

            break;
        }
    }
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
        // Glyph + name. A filled playGreen disc + dark caret while playing; a play-family or
        // neutral hollow caret otherwise.
        auto textArea = b.toNearestInt().reduced (5, 3);

        const auto glyphArea = textArea.removeFromLeft (12);
        if (playing)
        {
            // Playing glyph disc: playGreen ("sound is happening here", W04a semantic accent) with
            // a dark caret on top (onAccent means text-on-AMBER, so a plain dark ink is used here).
            g.setColour (Colour (ForgeLookAndFeel::playGreen));
            g.fillEllipse (glyphArea.toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            g.setColour (Colours::black.withAlpha (0.8f));
        }
        else if (queued)
        {
            // Queued / stopping: a dim-green caret (the queued member of the play family, W04a) so
            // an about-to-fire (or about-to-stop) clip reads distinctly from a plain stopped clip —
            // and from a merely-selected one, whose caret stays neutral. (QC fix: previously queued
            // state was conveyed ONLY by an outline identical to the selection outline, so a queued
            // clip and a selected idle clip were the same.)
            g.setColour (Colour (ForgeLookAndFeel::playGreenDim));
        }
        else
        {
            g.setColour (Colour (ForgeLookAndFeel::textPrim));
        }
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6"), glyphArea, Justification::centredLeft);  // ▶

        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.drawText (label, textArea.removeFromLeft (textArea.getWidth()),
                    Justification::centredLeft, true);

        // playGreen progress sliver along the bottom while playing (fixed sliver, MVP — no timeline).
        if (playing)
        {
            auto sliver = b.withTrimmedTop (b.getHeight() - 2.0f).reduced (1.0f, 0.0f);
            g.setColour (Colour (ForgeLookAndFeel::playGreen));
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

    // Outlines LAST, semantic vocabulary (W04a): the play-family ring carries the LAUNCH state —
    // playGreen while playing, playGreenDim while queued (both beat-pulsed via the pushed-in
    // pulseAlpha) or stopping (static). AMBER now means ONLY selection / keyboard focus.
    if (playing || queued)
    {
        const bool isQueued = (state == SlotVisualState::queued);

        // pulseAlpha < 0 => no beat pulse flowing (transport not running / poll edge): fall back
        // to the state's peak so the ring never vanishes. Stopping is static at full strength.
        const float ringAlpha = playing  ? (pulseAlpha >= 0.0f ? pulseAlpha : 1.0f)
                              : isQueued ? (pulseAlpha >= 0.0f ? pulseAlpha : 0.75f)
                                         : 1.0f;

        g.setColour (Colour (playing ? ForgeLookAndFeel::playGreen
                                     : ForgeLookAndFeel::playGreenDim).withAlpha (ringAlpha));
        g.drawRoundedRectangle (b, corner, 2.0f);
    }

    // Selection / keyboard-focus cursor: the one remaining AMBER on the grid. Drawn over everything
    // else; when a play-family ring already occupies the outer edge the cursor drops to an inner
    // ring (the recArmed-style inset) so both meanings stay visible at once.
    if (selected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));

        if (playing || queued)
            g.drawRoundedRectangle (b.reduced (3.0f), corner, 1.5f);
        else
            g.drawRoundedRectangle (b, corner, 2.0f);
    }

    // File-drop hover ring (W07): drawn LAST so it's unmistakable over any pad state. Its colour is a
    // NEUTRAL bright gray (textPrim), deliberately OUTSIDE the semantic accent vocabulary the Fable charter
    // reserves (amber = selection, playGreen/Dim = playing/queued, recordRed = recording). It MATCHES the
    // Arrange-lane drop marker so the SAME gesture reads the SAME across both surfaces (one colour = one
    // meaning; QC harmonised this off the earlier teal, which doubled as automation's colour). A translucent
    // wash + ring, so "drop here" can never be confused with any launch/selection/record state. Cleared on
    // drop and on drag-exit.
    if (dragHover)
    {
        g.setColour (Colour (ForgeLookAndFeel::textPrim).withAlpha (0.15f));
        g.fillRoundedRectangle (b, corner);
        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.drawRoundedRectangle (b, corner, 2.5f);
    }
}
