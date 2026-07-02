#include "ui/arrange/ArrangeView.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

#include <array>
#include <cmath>

using namespace juce;

namespace
{
    // A small fixed palette offered by the "Set Colour..." context-menu items. Using a
    // discrete palette (rather than a live ColourSelector) keeps colour-picking fully
    // synchronous and avoids managing an async ChangeListener lifetime.
    const std::array<Colour, 8> kSwatchPalette
    {
        Colour (0xffe24b4a), Colour (0xffe0902f), Colour (0xffe0c93f), Colour (0xff6bbf59),
        Colour (0xff3fa7c9), Colour (0xff5566cc), Colour (0xff9b59b6), Colour (0xff8a9095)
    };

    // Human-readable label for the active snap division, used in the drag hint strip.
    String snapDivisionLabel (ArrangeView::SnapDivision d)
    {
        switch (d)
        {
            case ArrangeView::SnapDivision::off:       return "off";
            case ArrangeView::SnapDivision::bar:       return "bars";
            case ArrangeView::SnapDivision::half:      return "1/2 notes";
            case ArrangeView::SnapDivision::quarter:   return "1/4 notes";
            case ArrangeView::SnapDivision::eighth:    return "1/8 notes";
            case ArrangeView::SnapDivision::sixteenth: return "1/16 notes";
            default:                                   return "grid";
        }
    }
}

//==============================================================================
TimeRulerComponent::TimeRulerComponent (TimelineView& v)
    : view (v)
{
    setInterceptsMouseClicks (false, false);
}

void TimeRulerComponent::setEdit (te::Edit* e)
{
    edit = e;
    repaint();
}

void TimeRulerComponent::setSubBeatTicks (int ticksPerBeat)
{
    const int clamped = jmax (0, ticksPerBeat);
    if (subBeatTicks != clamped)
    {
        subBeatTicks = clamped;
        repaint();
    }
}

void TimeRulerComponent::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (bounds);
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

    if (edit == nullptr || getWidth() <= 0)
        return;

    const int w = getWidth();
    const int h = getHeight();

    // Convert the visible time window to a beat range, then iterate whole beats. For each
    // beat we ask the TempoSequence which bar/beat it lands on so bar starts can be drawn
    // emphasised with a bar number. This honours tempo/time-sig changes across the window.
    auto& seq = edit->tempoSequence;

    const auto startBeat = seq.toBeats (view.viewStart);
    const auto endBeat   = seq.toBeats (view.viewEnd);

    int firstBeat = (int) std::floor (startBeat.inBeats());
    int lastBeat  = (int) std::ceil  (endBeat.inBeats());

    // Guard against degenerate / huge ranges so we never spin drawing thousands of lines.
    if (lastBeat <= firstBeat || (lastBeat - firstBeat) > 4096)
        return;

    g.setFont (Font (FontOptions (11.0f)));

    // Detect bar starts by a change in the integer `bars` field, NOT by getWholeBeats()==0 on a
    // value round-tripped through toTime()/toBarsAndBeats(). toTime uses secondsPerBeat while
    // toBarsAndBeats uses the separately-stored beatsPerSecond reciprocal, so for non-exact tempos
    // a true bar boundary can come back as N-epsilon, making getWholeBeats() return numerator-1
    // and dropping the bar line/number. `bars` = floor(beatsSinceFirstBar / numerator) is robust
    // because the epsilon never crosses an integer boundary there. A sentinel seed means the very
    // first visible beat is classified on its own merits below (we never round-trip a negative
    // beat/time, which can trip engine asserts).
    bool havePrev = false;
    int  prevBars = 0;

    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const auto t  = seq.toTime (te::BeatPosition::fromBeats ((double) beat));
        const auto bb = seq.toBarsAndBeats (t);

        // Bar start = the integer bar index changed from the previous beat. For the first beat we
        // examine (no previous bar yet) fall back to the position-within-bar being at the start,
        // with a small tolerance to absorb the round-trip epsilon.
        const bool isBarStart = havePrev
                                  ? (bb.bars != prevBars)
                                  : (bb.getWholeBeats() == 0 || bb.getFractionalBeats().inBeats() > 0.999);
        prevBars = bb.bars;
        havePrev = true;

        const int x = view.timeToX (t, w);

        if (x < 0 || x > w)
            continue;

        if (isBarStart)
        {
            g.setColour (Colour (ForgeLookAndFeel::textSec).withAlpha (0.9f));
            g.fillRect (x, 0, 1, h);
            g.setColour (Colour (ForgeLookAndFeel::textPrim));
            g.drawText (String (bb.bars + 1),
                        Rectangle<int> (x + 3, 1, 40, h - 2),
                        Justification::centredLeft, false);
        }
        else
        {
            g.setColour (Colour (ForgeLookAndFeel::hairline));
            g.fillRect (x, h / 2, 1, h / 2);
        }

        // Sub-beat subdivision ticks (active for finer-than-beat snap divisions): draw short faint
        // marks at evenly-spaced fractions of THIS beat -> the next, so the ruler previews the snap
        // grid. Drawn only for the last quarter of the strip height to read as minor gridlines, and
        // skipped on the very last iteration (no following beat to subdivide into).
        if (subBeatTicks > 0 && beat < lastBeat)
        {
            const auto tNext = seq.toTime (te::BeatPosition::fromBeats ((double) (beat + 1)));
            const int  xNext = view.timeToX (tNext, w);

            g.setColour (Colour (ForgeLookAndFeel::hairline).withAlpha (0.6f));

            for (int s = 1; s <= subBeatTicks; ++s)
            {
                const int sx = x + (int) std::lround ((double) (xNext - x) * (double) s
                                                      / (double) (subBeatTicks + 1));
                if (sx > 0 && sx < w)
                    g.fillRect (sx, (h * 3) / 4, 1, h / 4);
            }
        }
    }
}

//==============================================================================
ClipComponent::ClipComponent (TimelineView& v, te::Clip& c)
    : view (v), clip (c)
{
}

void ClipComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

int ClipComponent::clipAreaWidth() const
{
    // The clip is a child of TrackLaneComponent; the clip area is the parent width minus the fixed
    // track-header strip on the left. (resized() positions clips with exactly this width.)
    if (auto* parent = getParentComponent())
        return jmax (0, parent->getWidth() - ArrangeLayout::headerW);

    return jmax (0, getWidth());
}

void ClipComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClicked != nullptr)
            onRightClicked (*this, e);

        return;
    }

    // Left button: select immediately, and arm a potential drag-to-move. dragging only becomes
    // true once the pointer crosses the move threshold in mouseDrag, so a plain click never nudges.
    // The drag anchor is captured in PARENT (lane) coordinates: because the live drag moves this
    // component, an anchor in this component's own space would shift under us each frame and the
    // delta would jitter. The parent doesn't move, so a parent-relative anchor stays stable.
    if (onClicked != nullptr)
        onClicked (*this);

    dragging        = false;
    dragAnchorX     = (getParentComponent() != nullptr) ? e.getEventRelativeTo (getParentComponent()).x : e.x;
    dragOriginStart = clip.getPosition().getStart();
}

void ClipComponent::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // Parent-relative pointer x (stable as this component moves during the drag).
    const int pointerX = (getParentComponent() != nullptr) ? e.getEventRelativeTo (getParentComponent()).x : e.x;

    if (! dragging)
    {
        // Require a small horizontal movement before treating this as a move (mirrors standard
        // DAW behaviour and avoids accidental nudges on a click).
        if (std::abs (pointerX - dragAnchorX) < 3)
            return;

        dragging = true;

        if (onDragStarted != nullptr)
            onDragStarted (*this);
    }

    const int areaW = clipAreaWidth();
    if (areaW <= 0)
        return;

    // Pixel delta -> time delta via the shared TimelineView, then derive a candidate start.
    const int dxPixels = pointerX - dragAnchorX;
    const auto delta   = view.xToTime (dxPixels, areaW) - view.xToTime (0, areaW);
    auto candidateStart = dragOriginStart + delta;

    // Snap the live position too (unless Ctrl/Cmd bypasses it) so the clip lands visibly on bars.
    if (! e.mods.isCommandDown() && snapStartTime != nullptr)
        candidateStart = snapStartTime (candidateStart);

    if (candidateStart < te::TimePosition())
        candidateStart = te::TimePosition();

    // Move the component live (horizontal only). Bounds are recomputed from the candidate start so
    // the live preview matches exactly where the commit will land.
    const int newLeft = ArrangeLayout::headerW + view.timeToX (candidateStart, areaW);
    setTopLeftPosition (newLeft, getY());
}

void ClipComponent::mouseUp (const MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;

    const int areaW = clipAreaWidth();
    if (areaW <= 0)
        return;

    const int pointerX = (getParentComponent() != nullptr) ? e.getEventRelativeTo (getParentComponent()).x : e.x;
    const int dxPixels = pointerX - dragAnchorX;
    const auto delta   = view.xToTime (dxPixels, areaW) - view.xToTime (0, areaW);
    auto newStart      = dragOriginStart + delta;

    if (! e.mods.isCommandDown() && snapStartTime != nullptr)
        newStart = snapStartTime (newStart);

    if (newStart < te::TimePosition())
        newStart = te::TimePosition();

    // Commit to the engine clip: keepLength=true moves the clip horizontally without resizing it;
    // preserveSync=false matches a plain timeline move (source material shifts with the clip).
    clip.setStart (newStart, false, true);

    if (onDragCommitted != nullptr)
        onDragCommitted (*this);
}

void ClipComponent::paint (Graphics& g)
{
    // Shared chrome: clip-colour fill + border, a name fallback, and the selection outline.
    // Subclasses draw their type-specific body (waveform / notes) in their own override.
    auto bounds = getLocalBounds();

    g.setColour (clip.getColour().withAlpha (0.55f));
    g.fillRect (bounds);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRect (bounds);

    g.setColour (Colour (ForgeLookAndFeel::textPrim));
    g.drawText (clip.getName(), bounds.reduced (4), Justification::topLeft);

    if (selected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRect (bounds, 2);
    }
}

//==============================================================================
AudioClipComponent::AudioClipComponent (TimelineView& v, te::Clip& c)
    : ClipComponent (v, c)
{
    if (auto* wac = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        const te::AudioFile audioFile = wac->getPlaybackFile();

        if (audioFile.isValid())
            thumbnail = std::make_unique<te::SmartThumbnail> (clip.edit.engine, audioFile, *this, &clip.edit);
        else
            FORGE_LOG_ERROR ("Failed to create waveform thumbnail for clip '" + clip.getName() + "' — playback file is invalid or missing");
    }
}

void AudioClipComponent::drawWaveformWindow (Graphics& g, Rectangle<int> area,
                                             double sourceStartSecs, double sourceLenSecs, double speed)
{
    if (thumbnail == nullptr || area.getWidth() <= 0 || sourceLenSecs <= 0.0)
        return;

    // SmartThumbnail wants SOURCE-file time, so scale endpoints by the speed ratio (matches
    // the engine's own AudioClipComponent::drawWaveform reference). speed == 1 for
    // imported/recorded non-stretched clips.
    const auto t1 = te::TimePosition::fromSeconds (sourceStartSecs * speed);
    const auto t2 = te::TimePosition::fromSeconds ((sourceStartSecs + sourceLenSecs) * speed);

    thumbnail->drawChannels (g, area, te::TimeRange (t1, t2), 1.0f);
}

void AudioClipComponent::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (clip.getColour().withAlpha (0.55f));
    g.fillRect (bounds);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRect (bounds);

    auto* wac = dynamic_cast<te::WaveAudioClip*> (&clip);

    if (thumbnail == nullptr || wac == nullptr)
    {
        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.drawText (clip.getName(), bounds.reduced (4), Justification::topLeft);
    }
    else if (thumbnail->isGeneratingProxy())
    {
        g.setColour (Colour (ForgeLookAndFeel::textPrim));
        g.drawText ("Creating proxy: " + String (roundToInt (thumbnail->getProxyProgress() * 100.0f)) + "%",
                    bounds, Justification::centred);
    }
    else
    {
        const auto pos     = wac->getPosition();
        const double speed = wac->getSpeedRatio();
        const double off   = pos.getOffset().inSeconds();
        const double len   = pos.getLength().inSeconds();

        g.setColour (Colours::white.withAlpha (0.85f));

        // getLoopLength() is the loop region length in SECONDS; for beat-based (auto-tempo)
        // looping it returns 0 and only getLoopLengthBeats() is meaningful — that case is
        // deferred (see devlog) and falls through to a single-window draw to avoid garbage.
        const bool secondsBasedLoop = wac->isLooping()
                                      && ! wac->beatBasedLooping()
                                      && wac->getLoopLength().inSeconds() > 0.0;

        if (secondsBasedLoop)
        {
            // Tile the loop region's source waveform across the clip's on-timeline length,
            // one repeat per loop period. Tiling starts at the loop boundary (x = 0); a
            // partial start phase from the clip offset is NOT modelled (deferred).
            const double loopStart = wac->getLoopStart().inSeconds();
            const double loopLen   = wac->getLoopLength().inSeconds();

            const int    fullW     = bounds.getWidth();
            const double pxPerLoop = (double) fullW * loopLen / jmax (1.0e-9, len);
            // Cap the tile count so a pathological loop/length ratio can't stall painting.
            const int    repeats   = jmin (2048, (int) std::ceil (len / jmax (1.0e-9, loopLen)) + 1);

            Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (bounds.reduced (1));

            double xStart = 0.0;
            for (int i = 0; i < repeats && (int) xStart < fullW; ++i)
            {
                const int x = roundToInt (xStart);
                const int wTile = jmax (1, roundToInt (pxPerLoop));
                drawWaveformWindow (g, Rectangle<int> (bounds.getX() + x, bounds.getY() + 1,
                                                       wTile, bounds.getHeight() - 2),
                                    loopStart, loopLen, speed);
                xStart += pxPerLoop;
            }
        }
        else
        {
            // Non-looped (or beat-based-loop fallback): one source window across the body.
            drawWaveformWindow (g, bounds.reduced (1), off, len, speed);
        }
    }

    if (selected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRect (bounds, 2);
    }
}

//==============================================================================
MidiClipComponent::MidiClipComponent (TimelineView& v, te::Clip& c)
    : ClipComponent (v, c)
{
}

void MidiClipComponent::paint (Graphics& g)
{
    auto bounds = getLocalBounds();

    // Distinct fill so a MIDI clip reads differently from a wave clip: shift the clip colour
    // towards a fixed hue rather than drawing the raw colour, then the usual border.
    g.setColour (clip.getColour().withRotatedHue (0.5f).withAlpha (0.55f));
    g.fillRect (bounds);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRect (bounds);

    g.setColour (Colour (ForgeLookAndFeel::textPrim));
    g.drawText (clip.getName(), bounds.reduced (4), Justification::topLeft);

    // Mini note-preview: plot each note as a tiny filled rect, x from its beat range (mapped onto
    // the clip's own beat span) and y from its pitch (mapped onto a fixed pitch window so the scale
    // stays cheap and robust). An empty clip just shows the chrome + name above.
    if (auto* mc = dynamic_cast<te::MidiClip*> (&getClip()))
    {
        const auto& notes = mc->getSequence().getNotes();

        if (! notes.isEmpty())
        {
            // MidiNote beats are CONTENT beats (the sequence's own coordinate system, including the
            // clip offset), so map each note's content-beat range to timeline time via the clip's
            // getTimeOfContentBeat, then onto the clip's own pixel width through its on-timeline
            // length. This stays correct under tempo/offset/speed without re-deriving any of it here.
            const auto pos       = mc->getPosition();
            const double clipT0  = pos.getStart().inSeconds();
            const double clipLen = jmax (1.0e-9, pos.getLength().inSeconds());

            auto plot = bounds.reduced (1).withTrimmedTop (bounds.getHeight() / 3);  // notes under the name
            const int plotW = plot.getWidth();
            const int plotH = plot.getHeight();
            const double pitchSpan = (double) (pitchHi - pitchLo);

            if (plotW > 0 && plotH > 0)
            {
                g.setColour (Colours::white.withAlpha (0.85f));

                for (auto* n : notes)
                {
                    if (n == nullptr)
                        continue;

                    // Content-beat -> timeline time -> fraction of the clip's on-timeline length,
                    // clamped into [0, 1] so notes outside the visible window collapse to the edges.
                    const double t0 = mc->getTimeOfContentBeat (n->getStartBeat()).inSeconds();
                    const double t1 = mc->getTimeOfContentBeat (n->getEndBeat()).inSeconds();

                    const double f0 = jlimit (0.0, 1.0, (t0 - clipT0) / clipLen);
                    const double f1 = jlimit (0.0, 1.0, (t1 - clipT0) / clipLen);

                    const int x1 = plot.getX() + roundToInt (f0 * (double) plotW);
                    const int x2 = plot.getX() + roundToInt (f1 * (double) plotW);

                    // Pitch -> y (inverted so higher pitches sit higher), within the fixed window.
                    const int pitch = jlimit (pitchLo, pitchHi, n->getNoteNumber());
                    const int y = plot.getY()
                                    + roundToInt ((double) (pitchHi - pitch) / jmax (1.0, pitchSpan)
                                                  * (double) (plotH - 2));

                    g.fillRect (x1, y, jmax (2, x2 - x1), 2);
                }
            }
        }
    }

    if (selected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRect (bounds, 2);
    }
}

//==============================================================================
TrackLaneComponent::TrackLaneComponent (TimelineView& v, te::AudioTrack& t)
    : view (v), track (t)
{
    auto configureToggle = [this] (TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (TextButton::buttonColourId,   Colour (ForgeLookAndFeel::raisedBg));
        b.setColour (TextButton::buttonOnColourId, Colour (ForgeLookAndFeel::accent));
        b.setColour (TextButton::textColourOffId,  Colour (ForgeLookAndFeel::textSec));
        b.setColour (TextButton::textColourOnId,   Colour (ForgeLookAndFeel::onAccent));
        addAndMakeVisible (b);
    };

    configureToggle (muteButton);
    configureToggle (soloButton);
    configureToggle (armButton);
    configureToggle (autoButton);

    muteButton.onClick = [this]
    {
        track.setMute (muteButton.getToggleState());
        if (onEditMutated != nullptr) onEditMutated();
    };

    soloButton.onClick = [this]
    {
        track.setSolo (soloButton.getToggleState());
        if (onEditMutated != nullptr) onEditMutated();
    };

    armButton.onClick = [this]
    {
        armed = armButton.getToggleState();
        if (onArmToggled != nullptr)
            onArmToggled (track, armed);
        repaint();
    };

    // Expands/collapses this track's automation sub-lane. Pure view state owned by ArrangeView
    // (no Edit mutation, so no onEditMutated here).
    autoButton.onClick = [this]
    {
        if (onAutomationToggled != nullptr)
            onAutomationToggled (track, autoButton.getToggleState());
    };

    refreshControlStates();
    rebuildClips();
}

void TrackLaneComponent::refreshControlStates()
{
    // Derive ALL three indicators from engine truth so they survive a rebuild. Arm is queried
    // from the engine (queryArmed) exactly like mute/solo are read from the track — without this,
    // arm was a transient local bool that reset to false on every rebuild() while the input stayed
    // armed, desyncing the UI from the engine.
    if (queryArmed != nullptr)
        armed = queryArmed (track);

    muteButton.setToggleState (track.isMuted (false), dontSendNotification);
    soloButton.setToggleState (track.isSolo (false),  dontSendNotification);
    armButton.setToggleState  (armed,                 dontSendNotification);

    // The A (automation lane) indicator is view state owned by ArrangeView, queried the same way
    // as arm so it too survives rebuild().
    if (queryAutomationShown != nullptr)
        autoButton.setToggleState (queryAutomationShown (track), dontSendNotification);
}

void TrackLaneComponent::setSelectedClip (te::Clip* clip)
{
    for (auto* cc : clipComps)
        cc->setSelected (&cc->getClip() == clip);
}

void TrackLaneComponent::setTrackSelected (bool shouldBeSelected)
{
    if (trackSelected != shouldBeSelected)
    {
        trackSelected = shouldBeSelected;
        repaint();
    }
}

void TrackLaneComponent::rebuildClips()
{
    clipComps.clear();

    for (auto* c : track.getClips())
    {
        // Pick the component by clip type so a MIDI clip draws as a note-block instead of the
        // wave path: MidiClipComponent for te::MidiClip, AudioClipComponent for everything else.
        ClipComponent* cc = dynamic_cast<te::MidiClip*> (c)
                                ? static_cast<ClipComponent*> (clipComps.add (new MidiClipComponent (view, *c)))
                                : static_cast<ClipComponent*> (clipComps.add (new AudioClipComponent (view, *c)));

        cc->onClicked = [this] (ClipComponent& clicked)
        {
            if (onClipClicked != nullptr)
                onClipClicked (clicked);
        };

        cc->onRightClicked = [this] (ClipComponent& clicked, const MouseEvent& e)
        {
            if (onClipRightClicked != nullptr)
                onClipRightClicked (clicked, e);
        };

        cc->onDragStarted = [this] (ClipComponent& dragged)
        {
            if (onClipDragStarted != nullptr)
                onClipDragStarted (dragged);
        };

        cc->onDragCommitted = [this] (ClipComponent& dragged)
        {
            if (onClipDragCommitted != nullptr)
                onClipDragCommitted (dragged);
        };

        // Forward to ArrangeView's snap helper (identity when snapping is off).
        cc->snapStartTime = [this] (te::TimePosition t)
        {
            return snapStartTime != nullptr ? snapStartTime (t) : t;
        };

        addAndMakeVisible (cc);
    }

    resized();
}

void TrackLaneComponent::mouseDown (const MouseEvent& e)
{
    using namespace ArrangeLayout;

    const bool inHeader = e.x < headerW;

    if (e.mods.isPopupMenu())
    {
        // Right-click in the header opens the lane menu. (Right-click on a clip is handled by
        // ClipComponent.) Right-click in the empty clip area offers a "New MIDI Clip" affordance
        // at the clicked time via onClipAreaRightClicked.
        if (inHeader)
        {
            if (onHeaderRightClicked != nullptr)
                onHeaderRightClicked (*this, e);
        }
        else if (onClipAreaRightClicked != nullptr)
        {
            onClipAreaRightClicked (*this, e.x - headerW, jmax (0, getWidth() - headerW), e);
        }
    }
    else if (inHeader)
    {
        // Header click selects the track.
        if (onHeaderClicked != nullptr)
            onHeaderClicked (*this);
    }
    else
    {
        // Click in the empty clip area (a child clip would have consumed the event) clears
        // the current selection, then scrubs the transport to the clicked time (preserving the
        // old playhead-overlay scrub behaviour now that lanes sit above the playhead so clips
        // remain clickable/draggable).
        if (onLaneAreaClicked != nullptr)
            onLaneAreaClicked (*this);

        if (onLaneAreaScrub != nullptr)
            onLaneAreaScrub (e.x - headerW, jmax (0, getWidth() - headerW));
    }
}

//==============================================================================
// FileDragAndDropTarget — OS/external audio-file drops onto this lane.
//
// A drop imports the FIRST accepted audio file onto THIS lane's track (the lane owns its track, so
// the track index is implicit) at the drop time. isInterestedInFileDrag is called repeatedly during
// the drag and must be cheap: it only tests filename extensions (never opens a file). The x,y passed
// to these callbacks are LANE-relative; the header strip occupies the leftmost headerW px, so a drop
// in the header (x < headerW) is ignored and the clip-area x is (x - headerW). The actual import is
// deferred to ArrangeView via onFilesDropped (routes through the ProjectSession seam) — no raw te::
// import here. dragHover/dragHoverX drive a neutral insertion marker in paint(); both are cleared in
// fileDragExit AND filesDropped (fileDragExit may not fire after a drop) so the marker never sticks.

bool TrackLaneComponent::isInterestedInFileDrag (const StringArray& files)
{
    // Audio-only for v1 (the import seam is wave-only). Accept if ANY dragged file has an audio
    // extension; a mixed drop is filtered again in filesDropped. soundFileExtensions is the engine's
    // audio-only list (no MIDI). Cheap string test only — never touch the filesystem here.
    for (const auto& f : files)
        if (File (f).hasFileExtension (te::soundFileExtensions))
            return true;

    return false;
}

void TrackLaneComponent::fileDragEnter (const StringArray&, int x, int)
{
    using namespace ArrangeLayout;

    dragHover  = true;
    dragHoverX = (x >= headerW) ? (x - headerW) : -1;   // -1 => over the header (no insertion marker)
    repaint();
}

void TrackLaneComponent::fileDragMove (const StringArray&, int x, int)
{
    using namespace ArrangeLayout;

    // Track the pointer within the lane so the insertion marker previews where the clip will land.
    const int newX = (x >= headerW) ? (x - headerW) : -1;
    if (! dragHover || newX != dragHoverX)
    {
        dragHover  = true;
        dragHoverX = newX;
        repaint();
    }
}

void TrackLaneComponent::fileDragExit (const StringArray&)
{
    dragHover  = false;
    dragHoverX = -1;
    repaint();
}

void TrackLaneComponent::filesDropped (const StringArray& files, int x, int)
{
    using namespace ArrangeLayout;

    // Clear the hover marker FIRST (fileDragExit is not guaranteed after a drop).
    dragHover  = false;
    dragHoverX = -1;
    repaint();

    // Ignore drops on the header strip (no time under the pointer there).
    if (x < headerW)
        return;

    // Re-filter to the FIRST accepted audio file (a slot/lane import takes one file, not a loop).
    for (const auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension (te::soundFileExtensions))
        {
            if (onFilesDropped != nullptr)
                onFilesDropped (*this, file, x - headerW, jmax (0, getWidth() - headerW));

            return;
        }
    }
}

void TrackLaneComponent::paint (Graphics& g)
{
    using namespace ArrangeLayout;

    g.setColour (Colour (0xff262626));
    g.fillRect (getLocalBounds());

    auto header = getLocalBounds().removeFromLeft (headerW);
    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (header);

    // Colour swatch (left edge of the header).
    auto swatch = header.removeFromLeft (10);
    g.setColour (track.getColour());
    g.fillRect (swatch.reduced (0, 2));

    // Track name occupies the top portion of the header (controls sit below, laid out in resized()).
    g.setColour (Colour (ForgeLookAndFeel::textPrim));
    g.drawText (track.getName(),
                header.withTrimmedRight (4).removeFromTop (header.getHeight() / 2).withTrimmedLeft (6),
                Justification::centredLeft);

    // Armed lanes get a subtle record-red tint on the header edge.
    if (armed)
    {
        g.setColour (Colour (ForgeLookAndFeel::recordRed).withAlpha (0.85f));
        g.fillRect (swatch.getRight(), getLocalBounds().getY(), 2, getHeight());
    }

    g.setColour (Colour (0xff141414));
    g.drawRect (getLocalBounds());

    if (trackSelected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent));
        g.drawRect (getLocalBounds().removeFromLeft (headerW), 2);
    }

    // File-drop hover feedback: a NEUTRAL bright insertion marker, deliberately NOT any of the
    // semantic accents (amber = selection, recordRed = recording, timeTempo = playhead/clock,
    // playGreen = sound-happening) so "a file will land here" reads as its own thing. A faint white
    // wash tints the clip area to signal the lane is an active drop target; a 2 px bright neutral
    // vertical bar at the pointer previews the insertion point. Only drawn over the clip area (right
    // of the header); dragHoverX == -1 means the pointer is over the header, so no marker is shown.
    if (dragHover)
    {
        auto clipArea = getLocalBounds().withTrimmedLeft (headerW);

        g.setColour (Colour (ForgeLookAndFeel::textPrim).withAlpha (0.08f));
        g.fillRect (clipArea);

        if (dragHoverX >= 0)
        {
            g.setColour (Colour (ForgeLookAndFeel::textPrim).withAlpha (0.9f));
            g.fillRect (headerW + dragHoverX, 0, 2, getHeight());
        }
    }
}

void TrackLaneComponent::resized()
{
    using namespace ArrangeLayout;

    // Lay out the M / S / R / A toggle buttons in a row across the bottom half of the header.
    auto header = getLocalBounds().removeFromLeft (headerW);
    header.removeFromLeft (10);                     // colour swatch column
    auto controls = header.removeFromBottom (header.getHeight() / 2).reduced (4, 4);

    const int bw = jmax (18, (controls.getWidth() - 12) / 4);
    muteButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    soloButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    armButton.setBounds  (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    autoButton.setBounds (controls.removeFromLeft (bw));

    const int clipAreaW = jmax (0, getWidth() - headerW);

    for (auto* cc : clipComps)
    {
        const auto pos = cc->getClip().getPosition();
        const int x1 = view.timeToX (pos.getStart(), clipAreaW);
        const int x2 = view.timeToX (pos.getEnd(),   clipAreaW);
        cc->setBounds (headerW + x1, 3, jmax (2, x2 - x1), getHeight() - 6);
    }
}

//==============================================================================
PlayheadComponent::PlayheadComponent (TimelineView& v, te::TransportControl& t)
    : view (v), transport (t)
{
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

void PlayheadComponent::paint (Graphics& g)
{
    // timeTempo, not accent (W04b semantic vocabulary): the playhead is a CLOCK element, so it
    // wears the transport-clock family the LCD uses; amber stays selection/focus-only.
    // A 1 px dark edge either side separates the 2 px line from lane content it crosses —
    // without it the playhead is near-indistinguishable from the automationCurve teal when it
    // sweeps an expanded automation lane (QC; the same outline trick the lane's point handles
    // use). The line itself is brightened so it reads as THE bright clock line of the family.
    const int x = view.timeToX (transport.getPosition(), getWidth());
    g.setColour (Colour (ForgeLookAndFeel::shellBg).withAlpha (0.8f));
    g.fillRect (x - 1, 0, 1, getHeight());
    g.fillRect (x + 2, 0, 1, getHeight());
    g.setColour (Colour (ForgeLookAndFeel::timeTempo).brighter (0.35f));
    g.fillRect (x, 0, 2, getHeight());
}

void PlayheadComponent::timerCallback()
{
    const int x = view.timeToX (transport.getPosition(), getWidth());

    if (x != lastX)
    {
        const int prev = (lastX < 0 ? x : lastX);
        const int lo = jmin (x, prev) - 2;
        const int hi = jmax (x, prev) + 3;
        lastX = x;
        repaint (lo, 0, hi - lo, getHeight());
    }
}

bool PlayheadComponent::hitTest (int x, int)
{
    // Grab the mouse only within a few px of the current playhead line. Everywhere else the overlay
    // is transparent to clicks so the clips (drag-to-move) and lanes (click-to-scrub) underneath
    // receive the event. ~5px each side gives a comfortable grab target without shadowing clips.
    const int playheadX = view.timeToX (transport.getPosition(), getWidth());
    return std::abs (x - playheadX) <= 5;
}

void PlayheadComponent::mouseDown (const MouseEvent& e)
{
    transport.setUserDragging (true);
    scrubTo (e);
}

void PlayheadComponent::mouseDrag (const MouseEvent& e)
{
    scrubTo (e);
}

void PlayheadComponent::mouseUp (const MouseEvent&)
{
    transport.setUserDragging (false);
}

void PlayheadComponent::scrubTo (const MouseEvent& e)
{
    transport.setPosition (view.xToTime (e.x, getWidth()));
}

//==============================================================================
ArrangeView::ArrangeView (TimelineView& v)
    : view (v)
{
    buildSnapSelector();
}

void ArrangeView::buildSnapSelector()
{
    // Discrete snap-division picker living in the headerW x rulerH corner box (top-left, above the
    // lane headers and left of the ruler). Item IDs are (int) SnapDivision + 1 so id 0 (no selection)
    // is never a valid division. Colours come from ForgeLookAndFeel via the ComboBox colour IDs that
    // the shared LookAndFeel already sets, so no per-instance colour overrides are needed here.
    snapSelector.addItem ("Off",  (int) SnapDivision::off       + 1);
    snapSelector.addItem ("Bar",  (int) SnapDivision::bar       + 1);
    snapSelector.addItem ("1/2",  (int) SnapDivision::half      + 1);
    snapSelector.addItem ("1/4",  (int) SnapDivision::quarter   + 1);
    snapSelector.addItem ("1/8",  (int) SnapDivision::eighth    + 1);
    snapSelector.addItem ("1/16", (int) SnapDivision::sixteenth + 1);

    snapSelector.setTooltip ("Snap clip moves to this grid (hold Ctrl while dragging to bypass)");
    snapSelector.setJustificationType (Justification::centredLeft);
    snapSelector.setSelectedId ((int) division + 1, dontSendNotification);

    snapSelector.onChange = [this]
    {
        const int id = snapSelector.getSelectedId();
        if (id > 0)
            setSnapDivision (static_cast<SnapDivision> (id - 1));
    };

    addAndMakeVisible (snapSelector);
}

void ArrangeView::setEdit (te::Edit* e)
{
    if (edit != e)
        autoLaneStates.clear();   // per-session view state; another Edit's itemIDs mean nothing here

    edit = e;
    rebuild();
}

void ArrangeView::rebuild()
{
    lanes.clear();
    autoLanes.clear();
    ruler.reset();
    playhead.reset();

    selectedClip = nullptr;
    selectedTrack = nullptr;

    if (edit != nullptr)
    {
        ruler = std::make_unique<TimeRulerComponent> (view);
        ruler->setEdit (edit);
        // Re-apply the subdivision-tick preview for the current division (the ruler is recreated on
        // every rebuild, so its tick state would otherwise reset to none).
        ruler->setSubBeatTicks (division == SnapDivision::sixteenth ? 3
                                : division == SnapDivision::eighth  ? 1
                                                                    : 0);
        addAndMakeVisible (*ruler);

        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* lane = lanes.add (new TrackLaneComponent (view, *track));

            lane->onClipClicked = [this] (ClipComponent& cc)
            {
                selectClip (&cc);
            };

            lane->onClipRightClicked = [this] (ClipComponent& cc, const MouseEvent& e)
            {
                selectClip (&cc);
                showClipContextMenu (cc, e);
            };

            lane->onClipDragStarted = [this] (ClipComponent& cc)
            {
                selectClip (&cc);
                setHint (isSnapEnabled()
                             ? "Moving clip - snapping to " + snapDivisionLabel (division)
                                 + " - hold Ctrl to bypass snap"
                             : "Moving clip - snap off");
            };

            lane->onClipDragCommitted = [this] (ClipComponent&)
            {
                // The clip's start was committed to the engine; persist, then re-lay-out every lane
                // DIRECTLY so each clip's bounds are re-derived from the authoritative engine
                // position. (ArrangeView::resized() would skip lanes whose own bounds are unchanged,
                // which is the case here, so call the lane resized() ourselves.)
                notifyEditMutated();
                for (auto* l : lanes)
                    l->resized();
                setHint ("Drag a clip to move it - hold Ctrl to bypass snap");
            };

            // Per-lane snap seam -> ArrangeView's grid-snap helper (honours the active division).
            lane->snapStartTime = [this] (te::TimePosition t) { return snapToGrid (t); };

            // Right-click on the empty clip area -> the "New MIDI Clip" affordance at that time.
            lane->onClipAreaRightClicked = [this] (TrackLaneComponent& l, int clipAreaX, int clipAreaW,
                                                   const MouseEvent& e)
            {
                showClipAreaContextMenu (l, clipAreaX, clipAreaW, e);
            };

            // An OS/Explorer audio file was dropped on this lane. Map the clip-area x -> edit time
            // (snapped to the active grid, exactly like a New-MIDI-Clip drop), resolve the lane's
            // track index, and forward to the shell's onFilesDropped (ProjectSession import seam +
            // save + rebuild). The header is already subtracted by the lane before it calls us.
            lane->onFilesDropped = [this] (TrackLaneComponent& l, const File& file,
                                           int clipAreaX, int clipAreaW)
            {
                if (edit == nullptr || onFilesDropped == nullptr)
                    return;

                const auto tracks     = te::getAudioTracks (*edit);
                const int  trackIndex = tracks.indexOf (&l.getTrack());
                if (trackIndex < 0)
                    return;

                const auto startTime = snapToGrid (view.xToTime (clipAreaX, clipAreaW));
                onFilesDropped (trackIndex, file, startTime);
            };

            lane->onHeaderClicked = [this] (TrackLaneComponent& l)
            {
                selectTrack (&l);
            };

            lane->onLaneAreaClicked = [this] (TrackLaneComponent&)
            {
                clearSelection();
            };

            // Empty-area click scrubs the transport (the lanes sit above the playhead overlay so
            // clips stay clickable/draggable; this keeps the old click-to-scrub behaviour).
            lane->onLaneAreaScrub = [this] (int clipAreaX, int clipAreaW)
            {
                if (edit != nullptr && clipAreaW > 0)
                    edit->getTransport().setPosition (view.xToTime (clipAreaX, clipAreaW));
            };

            lane->onHeaderRightClicked = [this] (TrackLaneComponent& l, const MouseEvent& e)
            {
                selectTrack (&l);
                showLaneContextMenu (l, e);
            };

            lane->onArmToggled = [this] (te::AudioTrack& t, bool shouldArm)
            {
                if (onArmToggled != nullptr)
                    onArmToggled (t, shouldArm);
            };

            lane->queryArmed = isTrackArmed;   // engine-truth arm state, applied below + on refresh

            lane->onEditMutated = [this] { notifyEditMutated(); };

            // ---- W03: per-track automation sub-lane (collapsed by default; A button toggles) ----
            // View state (expanded flag + param choice) is keyed by track itemID in autoLaneStates
            // so it survives rebuild(); the components themselves are recreated every pass, exactly
            // like the lanes, so neither array ever holds a stale engine object.
            const auto trackID = track->itemID;
            const auto laneState = autoLaneStates[trackID];   // default-constructs collapsed/Volume on first sight

            auto* autoLane = autoLanes.add (new AutomationLane (view, *track, laneState.param));

            autoLane->onEditMutated  = [this] { notifyEditMutated(); };
            autoLane->onParamChanged = [this, trackID] (AutomationLane::Param p)
            {
                autoLaneStates[trackID].param = p;
            };

            // Safe capture: lane and autoLane are siblings owned by this ArrangeView and are
            // destroyed together by the next rebuild(), so the pointer can never outlive its target.
            lane->onAutomationToggled = [this, trackID, autoLane] (te::AudioTrack&, bool shouldShow)
            {
                autoLaneStates[trackID].expanded = shouldShow;
                autoLane->setVisible (shouldShow);
                resized();   // re-flow the stack; the playhead overlay grows to span the expansion
            };

            lane->queryAutomationShown = [this] (te::AudioTrack& t)
            {
                const auto it = autoLaneStates.find (t.itemID);
                return it != autoLaneStates.end() && it->second.expanded;
            };

            addChildComponent (*autoLane);            // visibility mirrors the expanded view state
            autoLane->setVisible (laneState.expanded);

            addAndMakeVisible (lane);

            // The lane ctor ran refreshControlStates() before queryArmed/queryAutomationShown were
            // wired; re-derive now so a track that is still armed in the engine (or has an expanded
            // automation lane) shows the right indicators immediately after rebuild().
            lane->refreshControlStates();
        }

        playhead = std::make_unique<PlayheadComponent> (view, edit->getTransport());
        addAndMakeVisible (*playhead);
    }

    hintText = lanes.isEmpty() ? juce::String()
                               : juce::String ("Drag a clip to move it - hold Ctrl to bypass snap");

    resized();
}

void ArrangeView::resized()
{
    using namespace ArrangeLayout;

    // Snap-division selector occupies the corner box (above the lane headers, left of the ruler).
    // A small inset keeps it visually distinct from the ruler/header edges.
    snapSelector.setBounds (Rectangle<int> (0, 0, headerW, rulerH).reduced (2, 2));

    if (ruler != nullptr)
        ruler->setBounds (headerW, 0, jmax (0, getWidth() - headerW), rulerH);

    // Reserve a one-line hint strip across the very bottom; the playhead overlay stops above it.
    const int laneAreaBottom = jmax (rulerH, getHeight() - hintH);

    int y = rulerH;
    for (int i = 0; i < lanes.size(); ++i)
    {
        lanes.getUnchecked (i)->setBounds (0, y, getWidth(), laneH);
        y += laneH;

        // W03: an expanded automation sub-lane sits directly below its track lane; the stack
        // (and the playhead span computed from y below) grows to fit. Collapsed lanes cost
        // nothing, so the all-collapsed layout is byte-identical to the pre-W03 one.
        auto* autoLane = autoLanes[i];   // parallel array; operator[] is range-checked

        if (autoLane != nullptr && autoLane->isVisible())
        {
            autoLane->setBounds (0, y, getWidth(), autoLaneH);
            y += autoLaneH;
        }

        y += gap;
    }

    if (playhead != nullptr)
        playhead->setBounds (headerW, rulerH,
                             jmax (0, getWidth() - headerW),
                             jmax (y - rulerH, laneAreaBottom - rulerH));
}

void ArrangeView::paint (Graphics& g)
{
    using namespace ArrangeLayout;

    g.fillAll (Colour (ForgeLookAndFeel::shellBg));

    if (lanes.isEmpty())
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("No tracks — import audio to begin",
                    getLocalBounds().withTrimmedBottom (hintH), Justification::centred);
    }

    // Info/help strip across the very bottom: a themed one-line hint about clip interactions.
    auto hint = getLocalBounds().removeFromBottom (hintH);
    g.setColour (Colour (ForgeLookAndFeel::panelBg));
    g.fillRect (hint);
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (hint.getX(), hint.getY(), hint.getWidth(), 1);

    g.setColour (Colour (ForgeLookAndFeel::textSec));
    g.setFont (Font (FontOptions (12.0f)));
    g.drawText (hintText, hint.withTrimmedLeft (8), Justification::centredLeft, true);
}

int ArrangeView::getNumClipComponentsOnTrack0() const
{
    return lanes.isEmpty() ? 0 : lanes.getFirst()->getNumClipComponents();
}

void ArrangeView::refreshArmStates()
{
    for (auto* lane : lanes)
        lane->refreshControlStates();
}

void ArrangeView::setAutomationLaneExpanded (int trackIndex, bool shouldShow)
{
    if (edit == nullptr)
        return;

    const auto tracks = te::getAudioTracks (*edit);
    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return;

    // Same per-session view state the header A button flips; rebuild() recreates the lane stack
    // with the new visibility (this is a harness/demo seam, not a per-tick path — rebuild is fine).
    autoLaneStates[tracks[trackIndex]->itemID].expanded = shouldShow;
    rebuild();
}

//==============================================================================
void ArrangeView::selectClip (ClipComponent* cc)
{
    selectedClip = (cc != nullptr ? &cc->getClip() : nullptr);

    // Clip selection FOLLOWS TO ITS TRACK (QC + the GarageBand pattern the channel tray cites):
    // clicking a clip means working on that track, so the lane highlight and the tray both track
    // the clip's owner instead of silently keeping a previously selected track bound.
    selectedTrack = (selectedClip != nullptr ? selectedClip->getTrack() : nullptr);

    for (auto* lane : lanes)
    {
        lane->setSelectedClip (selectedClip);
        lane->setTrackSelected (selectedTrack != nullptr && &lane->getTrack() == selectedTrack);
    }

    if (onClipSelected != nullptr)
        onClipSelected (selectedClip);

    if (onTrackSelected != nullptr)
        onTrackSelected (selectedTrack);
}

void ArrangeView::selectTrack (TrackLaneComponent* lane)
{
    selectedClip = nullptr;
    selectedTrack = (lane != nullptr ? &lane->getTrack() : nullptr);

    for (auto* l : lanes)
    {
        l->setSelectedClip (nullptr);
        l->setTrackSelected (l == lane);
    }

    if (onTrackSelected != nullptr)
        onTrackSelected (selectedTrack);
}

void ArrangeView::clearSelection()
{
    selectedClip = nullptr;
    selectedTrack = nullptr;

    for (auto* l : lanes)
    {
        l->setSelectedClip (nullptr);
        l->setTrackSelected (false);
    }

    if (onClipSelected != nullptr)
        onClipSelected (nullptr);

    // Deselection must NOTIFY (QC: the tray stayed bound to a deselected track because this
    // path never fired) — the shell's null handling empties the tray.
    if (onTrackSelected != nullptr)
        onTrackSelected (nullptr);
}

void ArrangeView::notifyEditMutated()
{
    if (onEditMutated != nullptr)
        onEditMutated();
}

void ArrangeView::setSnapEnabled (bool shouldSnap)
{
    // Thin wrapper over the division model: enabling restores the last non-off division (so a
    // toolbar toggle round-trips back to whatever the user last picked), disabling sets off.
    if (shouldSnap)
        setSnapDivision (lastEnabledDivision == SnapDivision::off ? SnapDivision::bar
                                                                  : lastEnabledDivision);
    else
        setSnapDivision (SnapDivision::off);
}

void ArrangeView::setSnapDivision (SnapDivision newDivision)
{
    division = newDivision;

    if (division != SnapDivision::off)
        lastEnabledDivision = division;

    // Keep the in-surface selector in sync (no-op if the change originated there).
    snapSelector.setSelectedId ((int) division + 1, dontSendNotification);

    // Preview finer-than-beat grids on the ruler. The ruler already draws whole-beat lines, so only
    // sub-beat divisions add ticks: 1/8 -> 1 tick per beat (halves), 1/16 -> 3 ticks (quarters).
    if (ruler != nullptr)
        ruler->setSubBeatTicks (division == SnapDivision::sixteenth ? 3
                                : division == SnapDivision::eighth  ? 1
                                                                    : 0);

    if (onSnapDivisionChanged != nullptr)
        onSnapDivisionChanged (division);
}

void ArrangeView::setHint (const String& text)
{
    if (hintText != text)
    {
        hintText = text;
        repaint (getLocalBounds().removeFromBottom (ArrangeLayout::hintH));
    }
}

double ArrangeView::gridStepInBeats (SnapDivision d, int numerator, int denominator)
{
    // Grid step measured in ENGINE BEATS (the unit of BarsAndBeats::beats / BeatPosition). Under
    // Tracktion's default LengthOfOneBeat::dependsOnTimeSignature policy one engine beat is one
    // DENOMINATOR-note (e.g. an eighth-note in 6/8), NOT always a quarter-note. So the musical
    // 1/4, 1/8, 1/16 divisions are scaled by denominator/4 to land on true note values in any
    // time signature (denominator/4 == 1 in 4/4, the common case, so that path is unchanged).
    // 'half' means half a BAR, derived from the bar's beat count, so it needs no scaling.
    // Returns 0 for off/bar, which snapToGrid handles via the dedicated bar path.
    const double quarterNoteInBeats = jmax (1, denominator) / 4.0;

    switch (d)
    {
        case SnapDivision::half:      return numerator * 0.5;             // half of a bar
        case SnapDivision::quarter:   return 1.0  * quarterNoteInBeats;   // one quarter-note
        case SnapDivision::eighth:    return 0.5  * quarterNoteInBeats;   // an eighth-note
        case SnapDivision::sixteenth: return 0.25 * quarterNoteInBeats;   // a sixteenth-note
        case SnapDivision::off:
        case SnapDivision::bar:
        default:                      return 0.0;
    }
}

te::TimePosition ArrangeView::snapToBar (te::TimePosition t) const
{
    // Retained legacy entry point (existing call sites / shell seams): forward to the grid snapper.
    return snapToGrid (t);
}

te::TimePosition ArrangeView::snapToGrid (te::TimePosition t) const
{
    // Snap a candidate clip-start to the nearest grid line for the active division. We never
    // round-trip a negative time/beat (which can trip engine asserts) — negatives are clamped to 0.
    if (division == SnapDivision::off || edit == nullptr)
        return t;

    if (t <= te::TimePosition())
        return te::TimePosition();

    auto& seq = edit->tempoSequence;

    if (division == SnapDivision::bar)
    {
        // Snap to the nearest BAR. Mirrors the ruler's robust idiom: ask the TempoSequence which
        // bar/beat the time lands on, round to the nearest whole bar by the fraction of the bar
        // already elapsed, then convert that bar back to a time.
        const auto bb = seq.toBarsAndBeats (t);

        // bb.beats is the TOTAL beats elapsed into the current bar (whole + fractional combined), and
        // numerator is that bar's beats-per-bar. fractionOfBar in [0, 1) tells us whether to round
        // down to this bar or up to the next.
        const int    numerator     = jmax (1, bb.numerator);
        const double fractionOfBar = bb.beats.inBeats() / (double) numerator;

        int nearestBar = bb.bars + (int) std::lround (fractionOfBar);
        if (nearestBar < 0)
            nearestBar = 0;

        // BarsAndBeats with zero beats -> the exact start of `nearestBar` (toTime reads tempo
        // sections, so the numerator field is not consulted here; bars + zero beats is sufficient).
        const auto snapped = seq.toTime (te::tempo::BarsAndBeats { nearestBar, te::BeatDuration() });

        return snapped < te::TimePosition() ? te::TimePosition() : snapped;
    }

    // Sub-bar division: snap on the absolute beat grid. The step is derived from the local time
    // signature in force at the candidate — numerator gives beats-per-bar (for 'half'), the
    // denominator scales the musical note divisions onto the engine-beat grid (see gridStepInBeats).
    const auto bb          = seq.toBarsAndBeats (t);
    const int  numerator   = jmax (1, bb.numerator);
    const int  denominator = seq.getTimeSigAt (t).denominator;
    const double step      = gridStepInBeats (division, numerator, denominator);

    if (step <= 0.0)
        return t;

    const double absBeats     = seq.toBeats (t).inBeats();
    const double snappedBeats = std::lround (absBeats / step) * step;

    if (snappedBeats <= 0.0)
        return te::TimePosition();

    const auto snapped = seq.toTime (te::BeatPosition::fromBeats (snappedBeats));

    return snapped < te::TimePosition() ? te::TimePosition() : snapped;
}

//==============================================================================
void ArrangeView::showClipAreaContextMenu (TrackLaneComponent& lane, int clipAreaX, int clipAreaW,
                                           const MouseEvent&)
{
    if (edit == nullptr)
        return;

    auto* track = &lane.getTrack();
    // Snap the clicked time to the active grid so the new clip lands where a dragged clip would.
    const auto startTime = snapToGrid (view.xToTime (clipAreaX, clipAreaW));

    Component::SafePointer<ArrangeView> safeThis (this);

    PopupMenu menu;
    menu.addItem ("New MIDI Clip", [safeThis, track, startTime]
    {
        if (safeThis != nullptr && track != nullptr && safeThis->edit != nullptr
              && safeThis->onCreateMidiClipRequested != nullptr)
        {
            const auto tracks = te::getAudioTracks (*safeThis->edit);
            const int trackIndex = tracks.indexOf (track);
            if (trackIndex >= 0)
                safeThis->onCreateMidiClipRequested (trackIndex, startTime);
        }
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&lane));
}

void ArrangeView::showClipContextMenu (ClipComponent& cc, const MouseEvent&)
{
    auto* clip = &cc.getClip();
    Component::SafePointer<ArrangeView> safeThis (this);

    PopupMenu menu;
    menu.addItem ("Rename", [safeThis, clip]
    {
        if (safeThis != nullptr && clip != nullptr)
            safeThis->renameClip (*clip);
    });

    menu.addItem ("Delete", [safeThis, clip]
    {
        if (safeThis != nullptr && clip != nullptr)
            safeThis->deleteClip (*clip);
    });

    PopupMenu colours;
    for (size_t i = 0; i < kSwatchPalette.size(); ++i)
    {
        const auto col = kSwatchPalette[i];

        // The item text is drawn in the swatch colour so the menu reads as a palette.
        PopupMenu::Item item;
        item.text = String::fromUTF8 ("\xe2\x96\xa0  Colour ") + String ((int) i + 1);  // filled square + label
        item.colour = col;
        item.action = [safeThis, clip, col]
        {
            if (safeThis != nullptr && clip != nullptr)
            {
                clip->setColour (col);
                safeThis->notifyEditMutated();
                safeThis->repaint();
            }
        };
        colours.addItem (std::move (item));
    }
    menu.addSubMenu ("Set Colour...", colours);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&cc));
}

void ArrangeView::showLaneContextMenu (TrackLaneComponent& lane, const MouseEvent&)
{
    auto* track = &lane.getTrack();
    Component::SafePointer<ArrangeView> safeThis (this);

    PopupMenu menu;
    menu.addItem ("Add Track", [safeThis, track]
    {
        if (safeThis != nullptr)
            safeThis->addTrack (track);
    });

    menu.addItem ("Rename Track", [safeThis, track]
    {
        if (safeThis != nullptr && track != nullptr)
            safeThis->renameTrack (*track);
    });

    menu.addItem ("Delete Track", [safeThis, track]
    {
        if (safeThis != nullptr && track != nullptr)
            safeThis->deleteTrack (*track);
    });

    // Convenience: create a new MIDI clip at time 0 on this lane's track. Mirrors the clip-area
    // affordance but always at the bar-1 start (the header has no time-under-pointer to use).
    menu.addItem ("New MIDI Clip", [safeThis, track]
    {
        if (safeThis != nullptr && track != nullptr && safeThis->edit != nullptr
              && safeThis->onCreateMidiClipRequested != nullptr)
        {
            const auto tracks = te::getAudioTracks (*safeThis->edit);
            const int trackIndex = tracks.indexOf (track);
            if (trackIndex >= 0)
                safeThis->onCreateMidiClipRequested (trackIndex, te::TimePosition());
        }
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&lane));
}

//==============================================================================
void ArrangeView::renameClip (te::Clip& clip)
{
    Component::SafePointer<ArrangeView> safeThis (this);
    te::Clip* clipPtr = &clip;

    // The AlertWindow is owned by a shared_ptr captured in the modal callback so it stays
    // alive until the callback has read its text editor, then is destroyed when the lambda
    // (and its capture) is released. deleteWhenDismissed is left false to avoid the window
    // being deleted *before* the callback runs.
    auto aw = std::make_shared<AlertWindow> ("Rename Clip", "Enter a new clip name:",
                                             MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", clip.getName());
    aw->addButton ("OK",     1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    aw->enterModalState (true, ModalCallbackFunction::create ([safeThis, aw, clipPtr] (int result)
    {
        if (result == 1 && safeThis != nullptr && clipPtr != nullptr)
        {
            const auto newName = aw->getTextEditorContents ("name").trim();
            if (newName.isNotEmpty())
            {
                clipPtr->setName (newName);
                safeThis->notifyEditMutated();
                safeThis->rebuild();
            }
        }
    }), false);
}

void ArrangeView::renameTrack (te::AudioTrack& track)
{
    Component::SafePointer<ArrangeView> safeThis (this);
    te::AudioTrack* trackPtr = &track;

    auto aw = std::make_shared<AlertWindow> ("Rename Track", "Enter a new track name:",
                                             MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", track.getName());
    aw->addButton ("OK",     1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    aw->enterModalState (true, ModalCallbackFunction::create ([safeThis, aw, trackPtr] (int result)
    {
        if (result == 1 && safeThis != nullptr && trackPtr != nullptr)
        {
            const auto newName = aw->getTextEditorContents ("name").trim();
            if (newName.isNotEmpty())
            {
                trackPtr->setName (newName);
                safeThis->notifyEditMutated();
                safeThis->rebuild();
            }
        }
    }), false);
}

void ArrangeView::deleteClip (te::Clip& clip)
{
    if (edit == nullptr)
        return;

    selectedClip = nullptr;
    clip.removeFromParent();

    notifyEditMutated();
    rebuild();
}

void ArrangeView::addTrack (te::AudioTrack* after)
{
    if (edit == nullptr)
        return;

    const te::TrackInsertPoint insertPoint = (after != nullptr)
        ? te::TrackInsertPoint (*after, false)               // insert after the clicked track
        : te::TrackInsertPoint::getEndOfTracks (*edit);

    edit->insertNewAudioTrack (insertPoint, nullptr);

    notifyEditMutated();
    rebuild();
}

void ArrangeView::deleteTrack (te::AudioTrack& track)
{
    if (edit == nullptr)
        return;

    selectedTrack = nullptr;
    edit->deleteTrack (&track);

    notifyEditMutated();
    rebuild();
}
