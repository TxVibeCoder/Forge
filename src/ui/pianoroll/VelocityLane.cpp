#include "ui/pianoroll/VelocityLane.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

namespace
{
    using namespace VelocityLaneLayout;
}

//==============================================================================
VelocityLane::VelocityLane (PianoRollView& o)
    : owner (o)
{
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

//==============================================================================
int VelocityLane::yToVelocity (int localY) const
{
    // The usable band runs from padTop (=127) to the lane bottom (=1); clamp outside it.
    const int top    = padTop;
    const int bottom = getHeight();
    const int span   = jmax (1, bottom - top);
    const double frac = 1.0 - (double) (localY - top) / (double) span;
    return jlimit (1, 127, (int) std::round (frac * 127.0));
}

te::MidiNote* VelocityLane::noteAtX (int localX) const
{
    auto* clip = owner.getClip();
    if (clip == nullptr)
        return nullptr;

    te::MidiNote* best = nullptr;
    int bestVel = -1;

    // Among bars whose x is within half a bar-width of the pointer, pick the loudest (topmost) so
    // the most visible bar is the one edited when several notes stack on the same column.
    for (auto* note : clip->getSequence().getNotes())
    {
        const int x = owner.beatToX (note->getStartBeat().inBeats());
        if (std::abs (x - localX) <= barWidth / 2 + 2)
        {
            const int v = note->getVelocity();
            if (v > bestVel)
            {
                bestVel = v;
                best = note;
            }
        }
    }

    return best;
}

void VelocityLane::editVelocityAt (const MouseEvent& e)
{
    if (auto* note = noteAtX (e.x))
    {
        owner.setNoteVelocity (*note, yToVelocity (e.y));
        repaint();
    }
}

//==============================================================================
void VelocityLane::paint (Graphics& g)
{
    const auto bounds = getLocalBounds();

    // Strip background + a hairline along the top edge to separate it from the grid above.
    g.fillAll (Colour (ForgeLookAndFeel::panelBg));
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, bounds.getWidth(), 1);

    auto* clip = owner.getClip();
    if (clip == nullptr || clip->getSequence().getNumNotes() == 0)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.5f));
        g.setFont (FontOptions (11.0f));
        g.drawText (clip == nullptr ? "" : "Velocity",
                    Rectangle<int> (PianoRollLayout::gutterW + 4, 0,
                                    jmax (0, bounds.getWidth() - PianoRollLayout::gutterW - 4),
                                    bounds.getHeight()),
                    Justification::centredLeft, false);
        return;
    }

    // A small "Velocity" caption in the gutter column to mirror the keybed labels above.
    g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.7f));
    g.setFont (FontOptions (8.0f));
    g.drawText ("vel", Rectangle<int> (2, 2, PianoRollLayout::gutterW - 3, 10),
                Justification::centredLeft, false);

    const int bottom = bounds.getHeight();
    const int top    = padTop;
    const int span   = jmax (1, bottom - top);

    for (auto* note : clip->getSequence().getNotes())
    {
        const int x = owner.beatToX (note->getStartBeat().inBeats());
        if (x < PianoRollLayout::gutterW || x > bounds.getWidth())
            continue;

        const float vel = jlimit (1.0f, 127.0f, (float) note->getVelocity()) / 127.0f;
        const int   barH = (int) std::round (vel * span);
        const int   barY = bottom - barH;

        const auto r = Rectangle<int> (x - barWidth / 2, barY, barWidth, barH).toFloat();
        const bool sel = owner.isSelected (*note);

        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (sel ? 1.0f : 0.55f + 0.4f * vel));
        g.fillRect (r);

        // A small cap pip so even very low velocities have a grabbable target.
        g.fillRect ((float) (x - barWidth / 2), (float) barY, (float) barWidth, 2.0f);
    }
}

void VelocityLane::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // Keep keyboard focus on the roll (matches the grid + note components) so Delete / Ctrl+C / Ctrl+V
    // still reach PianoRollView::keyPressed after a velocity edit.
    owner.grabKeyboardFocus();

    editVelocityAt (e);
}

void VelocityLane::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    editVelocityAt (e);
}
