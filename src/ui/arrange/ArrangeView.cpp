#include "ui/arrange/ArrangeView.h"
#include "ui/ForgeLookAndFeel.h"

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

    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const auto t = seq.toTime (te::BeatPosition::fromBeats ((double) beat));
        const int x = view.timeToX (t, w);

        if (x < 0 || x > w)
            continue;

        const auto bb = seq.toBarsAndBeats (t);
        const bool isBarStart = bb.getWholeBeats() == 0;

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
    }
}

//==============================================================================
AudioClipComponent::AudioClipComponent (TimelineView& v, te::Clip& c)
    : view (v), clip (c)
{
    if (auto* wac = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        const te::AudioFile audioFile = wac->getPlaybackFile();

        if (audioFile.isValid())
            thumbnail = std::make_unique<te::SmartThumbnail> (clip.edit.engine, audioFile, *this, &clip.edit);
    }
}

void AudioClipComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void AudioClipComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClicked != nullptr)
            onRightClicked (*this, e);
    }
    else
    {
        if (onClicked != nullptr)
            onClicked (*this);
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

    refreshControlStates();
    rebuildClips();
}

void TrackLaneComponent::refreshControlStates()
{
    muteButton.setToggleState (track.isMuted (false), dontSendNotification);
    soloButton.setToggleState (track.isSolo (false),  dontSendNotification);
    armButton.setToggleState  (armed,                 dontSendNotification);
}

void TrackLaneComponent::setArmed (bool shouldBeArmed)
{
    armed = shouldBeArmed;
    armButton.setToggleState (armed, dontSendNotification);
    repaint();
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
        auto* cc = clipComps.add (new AudioClipComponent (view, *c));

        cc->onClicked = [this] (AudioClipComponent& clicked)
        {
            if (onClipClicked != nullptr)
                onClipClicked (clicked);
        };

        cc->onRightClicked = [this] (AudioClipComponent& clicked, const MouseEvent& e)
        {
            if (onClipRightClicked != nullptr)
                onClipRightClicked (clicked, e);
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
        // Right-click in the header opens the lane menu. (Right-click on a clip is handled
        // by AudioClipComponent; right-click in the empty clip area is a no-op for now.)
        if (inHeader && onHeaderRightClicked != nullptr)
            onHeaderRightClicked (*this, e);
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
        // the current selection.
        if (onLaneAreaClicked != nullptr)
            onLaneAreaClicked (*this);
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
}

void TrackLaneComponent::resized()
{
    using namespace ArrangeLayout;

    // Lay out the M / S / R toggle buttons in a row across the bottom half of the header.
    auto header = getLocalBounds().removeFromLeft (headerW);
    header.removeFromLeft (10);                     // colour swatch column
    auto controls = header.removeFromBottom (header.getHeight() / 2).reduced (4, 4);

    const int bw = jmax (18, (controls.getWidth() - 8) / 3);
    muteButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    soloButton.setBounds (controls.removeFromLeft (bw));
    controls.removeFromLeft (4);
    armButton.setBounds  (controls.removeFromLeft (bw));

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
    const int x = view.timeToX (transport.getPosition(), getWidth());
    g.setColour (Colour (ForgeLookAndFeel::accent));
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
}

void ArrangeView::setEdit (te::Edit* e)
{
    edit = e;
    rebuild();
}

void ArrangeView::rebuild()
{
    lanes.clear();
    ruler.reset();
    playhead.reset();

    selectedClip = nullptr;
    selectedTrack = nullptr;

    if (edit != nullptr)
    {
        ruler = std::make_unique<TimeRulerComponent> (view);
        ruler->setEdit (edit);
        addAndMakeVisible (*ruler);

        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* lane = lanes.add (new TrackLaneComponent (view, *track));

            lane->onClipClicked = [this] (AudioClipComponent& cc)
            {
                selectClip (&cc);
            };

            lane->onClipRightClicked = [this] (AudioClipComponent& cc, const MouseEvent& e)
            {
                selectClip (&cc);
                showClipContextMenu (cc, e);
            };

            lane->onHeaderClicked = [this] (TrackLaneComponent& l)
            {
                selectTrack (&l);
            };

            lane->onLaneAreaClicked = [this] (TrackLaneComponent&)
            {
                clearSelection();
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

            lane->onEditMutated = [this] { notifyEditMutated(); };

            addAndMakeVisible (lane);
        }

        playhead = std::make_unique<PlayheadComponent> (view, edit->getTransport());
        addAndMakeVisible (*playhead);
    }

    resized();
}

void ArrangeView::resized()
{
    using namespace ArrangeLayout;

    if (ruler != nullptr)
        ruler->setBounds (headerW, 0, jmax (0, getWidth() - headerW), rulerH);

    int y = rulerH;
    for (auto* lane : lanes)
    {
        lane->setBounds (0, y, getWidth(), laneH);
        y += laneH + gap;
    }

    if (playhead != nullptr)
        playhead->setBounds (headerW, rulerH,
                             jmax (0, getWidth() - headerW),
                             jmax (y - rulerH, getHeight() - rulerH));
}

void ArrangeView::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::shellBg));

    if (lanes.isEmpty())
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("No tracks — import audio to begin", getLocalBounds(), Justification::centred);
    }
}

int ArrangeView::getNumClipComponentsOnTrack0() const
{
    return lanes.isEmpty() ? 0 : lanes.getFirst()->getNumClipComponents();
}

void ArrangeView::setTrackArmState (te::AudioTrack& track, bool armed)
{
    for (auto* lane : lanes)
        if (&lane->getTrack() == &track)
        {
            lane->setArmed (armed);
            return;
        }
}

//==============================================================================
void ArrangeView::selectClip (AudioClipComponent* cc)
{
    selectedClip = (cc != nullptr ? &cc->getClip() : nullptr);
    selectedTrack = nullptr;

    for (auto* lane : lanes)
    {
        lane->setSelectedClip (selectedClip);
        lane->setTrackSelected (false);
    }

    if (onClipSelected != nullptr)
        onClipSelected (selectedClip);
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
}

void ArrangeView::notifyEditMutated()
{
    if (onEditMutated != nullptr)
        onEditMutated();
}

//==============================================================================
void ArrangeView::showClipContextMenu (AudioClipComponent& cc, const MouseEvent&)
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
