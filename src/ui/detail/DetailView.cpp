#include "ui/detail/DetailView.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

using namespace juce;

namespace
{
    // Gain travel for a clip, in dB. Matches the spirit of the mixer fader (-60..+6) so the two
    // controls read consistently; clip gain is an absolute trim on the source material.
    constexpr double kMinGainDb = -60.0;
    constexpr double kMaxGainDb =   6.0;

    // Upper bound offered for a fade slider when the clip length is unknown / zero. The real cap is
    // the clip length (set per-clip in refreshFromClip) — setFadeIn/Out also clamp engine-side.
    constexpr double kFadeFallbackMax = 10.0;
}

//==============================================================================
DetailView::DetailView()
{
    // Section titles + the read-only timing line are plain secondary-text labels.
    auto styleTitle = [this] (Label& l, const String& text)
    {
        l.setText (text, dontSendNotification);
        l.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
        l.setFont (Font (FontOptions (12.0f)));
        addAndMakeVisible (l);
    };

    styleTitle (nameTitle,   "Name");
    styleTitle (gainTitle,   "Gain");
    styleTitle (fadeInTitle, "Fade In");
    styleTitle (fadeOutTitle,"Fade Out");

    timingLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
    timingLabel.setFont (Font (FontOptions (12.0f)));
    timingLabel.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (timingLabel);

    // --- Name (editable label) -------------------------------------------------------------------
    nameEditor.setColour (Label::backgroundColourId,        Colour (ForgeLookAndFeel::raisedBg));
    nameEditor.setColour (Label::textColourId,              Colour (ForgeLookAndFeel::textPrim));
    nameEditor.setColour (Label::outlineColourId,           Colour (ForgeLookAndFeel::hairline));
    nameEditor.setColour (Label::backgroundWhenEditingColourId, Colour (ForgeLookAndFeel::raisedBg));
    nameEditor.setColour (Label::textWhenEditingColourId,   Colour (ForgeLookAndFeel::textPrim));
    nameEditor.setEditable (true);   // double-click to edit; commits on return / focus-loss
    nameEditor.onTextChange = [this]
    {
        if (clip != nullptr)
        {
            const auto trimmed = nameEditor.getText().trim();
            if (trimmed.isNotEmpty() && trimmed != clip->getName())
            {
                clip->setName (trimmed);
                notifyMutated();
            }
            else
            {
                // Re-show the canonical name if the edit was empty/identical.
                nameEditor.setText (clip->getName(), dontSendNotification);
            }
        }
    };
    addAndMakeVisible (nameEditor);

    // --- Gain (dB slider) ------------------------------------------------------------------------
    gainSlider.setSliderStyle (Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 18);
    gainSlider.setRange (kMinGainDb, kMaxGainDb, 0.1);
    gainSlider.setNumDecimalPlacesToDisplay (1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);   // double-click -> unity (0 dB)
    gainSlider.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
    gainSlider.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
    gainSlider.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
    gainSlider.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
    gainSlider.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));
    gainSlider.onValueChange = [this]
    {
        if (auto* acb = getAudioClip())
        {
            acb->setGainDB ((float) gainSlider.getValue());
            notifyMutated();
        }
    };
    addAndMakeVisible (gainSlider);

    // --- Mute ------------------------------------------------------------------------------------
    muteToggle.setColour (ToggleButton::textColourId,    Colour (ForgeLookAndFeel::textPrim));
    muteToggle.setColour (ToggleButton::tickColourId,    Colour (ForgeLookAndFeel::accent));
    muteToggle.setColour (ToggleButton::tickDisabledColourId, Colour (ForgeLookAndFeel::hairline));
    muteToggle.onClick = [this]
    {
        if (clip != nullptr)
        {
            clip->setMuted (muteToggle.getToggleState());
            notifyMutated();
        }
    };
    addAndMakeVisible (muteToggle);

    // --- Fades (seconds sliders; AudioClipBase only) ---------------------------------------------
    auto configureFade = [this] (Slider& s)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 56, 18);
        s.setRange (0.0, kFadeFallbackMax, 0.01);
        s.setNumDecimalPlacesToDisplay (2);
        s.setTextValueSuffix (" s");
        s.setColour (Slider::thumbColourId,          Colour (ForgeLookAndFeel::accent));
        s.setColour (Slider::trackColourId,          Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        s.setColour (Slider::backgroundColourId,     Colour (ForgeLookAndFeel::raisedBg));
        s.setColour (Slider::textBoxTextColourId,    Colour (ForgeLookAndFeel::textSec));
        s.setColour (Slider::textBoxOutlineColourId, Colour (ForgeLookAndFeel::hairline));
        addAndMakeVisible (s);
    };

    configureFade (fadeInSlider);
    configureFade (fadeOutSlider);

    fadeInSlider.onValueChange = [this]
    {
        if (auto* acb = getAudioClip())
        {
            acb->setFadeIn (te::TimeDuration::fromSeconds (fadeInSlider.getValue()));
            notifyMutated();
        }
    };
    fadeOutSlider.onValueChange = [this]
    {
        if (auto* acb = getAudioClip())
        {
            acb->setFadeOut (te::TimeDuration::fromSeconds (fadeOutSlider.getValue()));
            notifyMutated();
        }
    };

    setClip (nullptr);   // start in the empty-hint state
}

DetailView::~DetailView() = default;

//==============================================================================
te::AudioClipBase* DetailView::getAudioClip() const
{
    return dynamic_cast<te::AudioClipBase*> (clip.get());
}

void DetailView::setClip (te::Clip* c)
{
    clip = c;                 // Clip::Ptr (ref-counted) — keeps the clip alive while inspected
    hasClip = (c != nullptr);

    rebuildThumbnail();
    refreshFromClip();
    resized();
    repaint();
}

void DetailView::rebuildThumbnail()
{
    thumbnail.reset();

    // Only WaveAudioClips have a playback file to thumbnail (mirrors AudioClipComponent in the
    // arrange view). The thumbnail repaints THIS component as it loads.
    if (auto* wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
    {
        const te::AudioFile audioFile = wac->getPlaybackFile();

        if (audioFile.isValid())
            thumbnail = std::make_unique<te::SmartThumbnail> (wac->edit.engine, audioFile, *this, &wac->edit);
        else
            FORGE_LOG_ERROR ("Failed to create waveform thumbnail for clip '" + wac->getName() + "' — playback file is invalid or missing");
    }
}

void DetailView::refreshFromClip()
{
    const bool isAudio = (getAudioClip() != nullptr);

    // Name — always available on a Clip.
    if (clip != nullptr)
        nameEditor.setText (clip->getName(), dontSendNotification);
    else
        nameEditor.setText ({}, dontSendNotification);

    nameEditor.setVisible (hasClip);
    nameTitle.setVisible (hasClip);

    // Gain / mute — AudioClipBase only.
    if (auto* acb = getAudioClip())
    {
        gainSlider.setValue (acb->getGainDB(), dontSendNotification);
        muteToggle.setToggleState (acb->isMuted(), dontSendNotification);
    }

    gainSlider.setVisible (isAudio);
    gainTitle.setVisible (isAudio);
    muteToggle.setVisible (isAudio);

    // Read-only timing — start / length / offset from the clip position.
    if (clip != nullptr)
    {
        const auto pos = clip->getPosition();
        timingLabel.setText ("Start " + formatSeconds (pos.getStart().inSeconds())
                               + "    Length " + formatSeconds (pos.getLength().inSeconds())
                               + "    Offset " + formatSeconds (pos.getOffset().inSeconds()),
                             dontSendNotification);
    }
    else
    {
        timingLabel.setText ({}, dontSendNotification);
    }
    timingLabel.setVisible (hasClip);

    // Fades — AudioClipBase only. Cap the slider travel at the clip length so a fade can't be set
    // longer than the clip (the engine clamps too, but this keeps the UI honest).
    if (auto* acb = getAudioClip())
    {
        const double lenSecs = jmax (0.0, acb->getPosition().getLength().inSeconds());
        const double fadeMax = (lenSecs > 0.0) ? lenSecs : kFadeFallbackMax;

        fadeInSlider.setRange  (0.0, fadeMax, 0.01);
        fadeOutSlider.setRange (0.0, fadeMax, 0.01);
        fadeInSlider.setValue  (acb->getFadeIn().inSeconds(),  dontSendNotification);
        fadeOutSlider.setValue (acb->getFadeOut().inSeconds(), dontSendNotification);
    }

    fadeInSlider.setVisible (isAudio);
    fadeOutSlider.setVisible (isAudio);
    fadeInTitle.setVisible (isAudio);
    fadeOutTitle.setVisible (isAudio);
}

//==============================================================================
void DetailView::notifyMutated()
{
    // Timing/name may have changed; refresh the read-only line without re-triggering setters.
    if (clip != nullptr)
    {
        const auto pos = clip->getPosition();
        timingLabel.setText ("Start " + formatSeconds (pos.getStart().inSeconds())
                               + "    Length " + formatSeconds (pos.getLength().inSeconds())
                               + "    Offset " + formatSeconds (pos.getOffset().inSeconds()),
                             dontSendNotification);
    }

    if (onEditMutated != nullptr)
        onEditMutated();
}

String DetailView::formatSeconds (double seconds)
{
    return String (seconds, 2) + "s";
}

//==============================================================================
void DetailView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    if (! hasClip)
        return;   // empty state is painted; no children are laid out

    // Left column: the editor stack. Right column: the large waveform thumbnail.
    const int waveformW = jmax (0, r.getWidth() / 2 - 6);
    auto waveformArea = r.removeFromRight (waveformW);   // captured for paint(); reserved here
    juce::ignoreUnused (waveformArea);
    r.removeFromRight (12);   // gap between the editor column and the waveform

    const int rowH    = 24;
    const int titleW  = 64;
    const int rowGap  = 6;

    auto layoutRow = [&r, rowH, titleW, rowGap] (Label& title, Component& editor)
    {
        auto row = r.removeFromTop (rowH);
        title.setBounds (row.removeFromLeft (titleW));
        editor.setBounds (row);
        r.removeFromTop (rowGap);
    };

    layoutRow (nameTitle, nameEditor);
    layoutRow (gainTitle, gainSlider);

    // Mute sits on its own short row (no title column — the toggle carries its own label).
    {
        auto row = r.removeFromTop (rowH);
        row.removeFromLeft (titleW);
        muteToggle.setBounds (row.removeFromLeft (140));
        r.removeFromTop (rowGap);
    }

    layoutRow (fadeInTitle,  fadeInSlider);
    layoutRow (fadeOutTitle, fadeOutSlider);

    // Read-only timing line across the bottom of the editor column.
    timingLabel.setBounds (r.removeFromBottom (rowH));
}

void DetailView::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::panelBg));

    // Top hairline separates the drawer from the view above it.
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (0, 0, getWidth(), 1);

    if (! hasClip)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (14.0f)));
        g.drawText ("Select a clip to inspect it", getLocalBounds(), Justification::centred);
        return;
    }

    // Waveform panel on the right half (mirrors the right column reserved in resized()).
    auto r = getLocalBounds().reduced (10, 8);
    const int waveformW = jmax (0, r.getWidth() / 2 - 6);
    auto waveformArea = r.removeFromRight (waveformW);

    paintWaveform (g, waveformArea);
}

void DetailView::paintWaveform (Graphics& g, Rectangle<int> area)
{
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    // Panel background + frame.
    g.setColour (Colour (ForgeLookAndFeel::shellBg));
    g.fillRect (area);
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.drawRect (area);

    auto* wac = dynamic_cast<te::WaveAudioClip*> (clip.get());

    if (thumbnail == nullptr || wac == nullptr)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (12.0f)));
        g.drawText (wac == nullptr ? "No waveform for this clip type" : "No audio",
                    area, Justification::centred);
        return;
    }

    if (thumbnail->isGeneratingProxy())
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.drawText ("Creating proxy: " + String (roundToInt (thumbnail->getProxyProgress() * 100.0f)) + "%",
                    area, Justification::centred);
        return;
    }

    // Draw the clip's visible source window across the whole panel. SmartThumbnail wants SOURCE-file
    // time, so scale the clip's on-timeline offset/length by the speed ratio — matches the engine's
    // own AudioClipComponent::drawWaveform and the arrange view's drawWaveformWindow.
    const auto pos     = wac->getPosition();
    const double speed = wac->getSpeedRatio();
    const double off   = pos.getOffset().inSeconds();
    const double len   = pos.getLength().inSeconds();

    if (len <= 0.0)
        return;

    const auto t1 = te::TimePosition::fromSeconds (off * speed);
    const auto t2 = te::TimePosition::fromSeconds ((off + len) * speed);

    g.setColour (Colours::white.withAlpha (0.85f));
    thumbnail->drawChannels (g, area.reduced (1), te::TimeRange (t1, t2), 1.0f);
}
