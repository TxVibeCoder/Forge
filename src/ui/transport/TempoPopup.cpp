#include "ui/transport/TempoPopup.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

TempoPopup::TempoPopup (std::function<double()> queryBpmIn,
                        std::function<void (double)> onBpmChangedIn)
    : queryBpm (std::move (queryBpmIn)),
      onBpmChanged (std::move (onBpmChangedIn))
{
    const double seed = jlimit (minBpm, maxBpm, queryBpm ? queryBpm() : 120.0);

    // The BPM readout: editable, in the timeTempo transport-clock accent to echo the LCD.
    bpmLabel.setColour (Label::backgroundColourId,             Colour (ForgeLookAndFeel::lcdBg));
    bpmLabel.setColour (Label::textColourId,                   Colour (ForgeLookAndFeel::timeTempo));
    bpmLabel.setColour (Label::outlineColourId,                Colour (ForgeLookAndFeel::lcdFrame));
    bpmLabel.setColour (Label::backgroundWhenEditingColourId,  Colour (ForgeLookAndFeel::raisedBg));
    bpmLabel.setColour (Label::textWhenEditingColourId,        Colour (ForgeLookAndFeel::textPrim));
    bpmLabel.setJustificationType (Justification::centred);
    bpmLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 24.0f, Font::plain)));
    bpmLabel.setEditable (true, true, false);   // single- and double-click to edit; return/focus-loss commits
    bpmLabel.onTextChange = [this]
    {
        // Parse whatever the user committed; clamp + echo back the canonical value. A junk
        // string parses to 0.0, which the clamp lifts to minBpm rather than corrupting the tempo.
        applyBpm (bpmLabel.getText().getDoubleValue());
    };
    addAndMakeVisible (bpmLabel);

    // +/- steppers: 0.1 BPM nudges, 1.0 BPM with Shift held.
    auto styleStepper = [] (TextButton& b)
    {
        b.setColour (TextButton::buttonColourId,   Colour (ForgeLookAndFeel::raisedBg));
        b.setColour (TextButton::textColourOffId,  Colour (ForgeLookAndFeel::textPrim));
    };

    styleStepper (upButton);
    styleStepper (downButton);

    upButton.onClick = [this]
    {
        const double step = ModifierKeys::getCurrentModifiers().isShiftDown() ? 1.0 : 0.1;
        applyBpm (bpmLabel.getText().getDoubleValue() + step);
    };
    downButton.onClick = [this]
    {
        const double step = ModifierKeys::getCurrentModifiers().isShiftDown() ? 1.0 : 0.1;
        applyBpm (bpmLabel.getText().getDoubleValue() - step);
    };

    addAndMakeVisible (upButton);
    addAndMakeVisible (downButton);

    // TAP: feed the hi-res clock into the pure estimator; apply once two taps give a BPM.
    tapButton.setColour (TextButton::buttonColourId,  Colour (ForgeLookAndFeel::raisedBg));
    tapButton.setColour (TextButton::textColourOffId, Colour (ForgeLookAndFeel::timeTempo));
    tapButton.onClick = [this]
    {
        tapTempo.tap (Time::getMillisecondCounterHiRes());

        if (const auto bpm = tapTempo.currentBpm())
            applyBpm (*bpm);
    };
    addAndMakeVisible (tapButton);

    refreshLabel (seed);
    setSize (180, 120);
}

void TempoPopup::resized()
{
    auto area = getLocalBounds().reduced (8);

    bpmLabel.setBounds (area.removeFromTop (44));
    area.removeFromTop (6);

    auto stepperRow = area.removeFromTop (28);
    downButton.setBounds (stepperRow.removeFromLeft (stepperRow.getWidth() / 2).reduced (2, 0));
    upButton.setBounds   (stepperRow.reduced (2, 0));

    area.removeFromTop (6);
    tapButton.setBounds (area.removeFromTop (28));
}

void TempoPopup::applyBpm (double bpm)
{
    const double clamped = jlimit (minBpm, maxBpm, bpm);

    if (onBpmChanged)
        onBpmChanged (clamped);

    refreshLabel (clamped);
}

void TempoPopup::refreshLabel (double bpm)
{
    // dontSendNotification: this is the canonical echo — it must not re-enter onTextChange.
    bpmLabel.setText (String (jlimit (minBpm, maxBpm, bpm), 1), dontSendNotification);
}
