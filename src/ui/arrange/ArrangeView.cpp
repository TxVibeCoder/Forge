#include "ui/arrange/ArrangeView.h"

using namespace juce;

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
        g.setColour (Colours::white);
        g.drawText (clip.getName(), bounds.reduced (4), Justification::topLeft);
        return;
    }

    if (thumbnail->isGeneratingProxy())
    {
        g.setColour (Colours::white);
        g.drawText ("Creating proxy: " + String (roundToInt (thumbnail->getProxyProgress() * 100.0f)) + "%",
                    bounds, Justification::centred);
        return;
    }

    // Map the full component width to the clip's SOURCE-file time window. SmartThumbnail
    // wants source time, so scale BOTH endpoints by the speed ratio (matches the engine's
    // own AudioClipComponent::drawWaveform reference). speed == 1 for imported/recorded clips.
    const auto pos     = wac->getPosition();
    const double speed = wac->getSpeedRatio();
    const double off   = pos.getOffset().inSeconds();
    const double len   = pos.getLength().inSeconds();

    const auto t1 = te::TimePosition::fromSeconds (off * speed);
    const auto t2 = te::TimePosition::fromSeconds ((off + len) * speed);

    g.setColour (Colours::white.withAlpha (0.85f));
    thumbnail->drawChannels (g, bounds.reduced (1), te::TimeRange (t1, t2), 1.0f);
}

//==============================================================================
TrackLaneComponent::TrackLaneComponent (TimelineView& v, te::AudioTrack& t)
    : view (v), track (t)
{
    rebuildClips();
}

void TrackLaneComponent::rebuildClips()
{
    clipComps.clear();

    for (auto* c : track.getClips())
    {
        auto* cc = clipComps.add (new AudioClipComponent (view, *c));
        addAndMakeVisible (cc);
    }

    resized();
}

void TrackLaneComponent::paint (Graphics& g)
{
    using namespace ArrangeLayout;

    g.setColour (Colour (0xff262626));
    g.fillRect (getLocalBounds());

    auto header = getLocalBounds().removeFromLeft (headerW);
    g.setColour (Colour (0xff353535));
    g.fillRect (header);
    g.setColour (Colours::white);
    g.drawText (track.getName(), header.reduced (8, 0), Justification::centredLeft);

    g.setColour (Colour (0xff141414));
    g.drawRect (getLocalBounds());
}

void TrackLaneComponent::resized()
{
    using namespace ArrangeLayout;

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
    g.setColour (Colours::yellow);
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
    playhead.reset();

    if (edit != nullptr)
    {
        for (auto* track : te::getAudioTracks (*edit))
        {
            auto* lane = lanes.add (new TrackLaneComponent (view, *track));
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

    int y = 0;
    for (auto* lane : lanes)
    {
        lane->setBounds (0, y, getWidth(), laneH);
        y += laneH + gap;
    }

    if (playhead != nullptr)
        playhead->setBounds (headerW, 0, jmax (0, getWidth() - headerW), jmax (y, getHeight()));
}

void ArrangeView::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1b1b1b));

    if (lanes.isEmpty())
    {
        g.setColour (Colours::grey);
        g.drawText ("No tracks — import audio to begin", getLocalBounds(), Justification::centred);
    }
}

int ArrangeView::getNumClipComponentsOnTrack0() const
{
    return lanes.isEmpty() ? 0 : lanes.getFirst()->getNumClipComponents();
}
