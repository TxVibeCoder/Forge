#include "ui/transport/TimeSigPopup.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

TimeSigPopup::TimeSigPopup (std::function<juce::String()> querySigIn,
                            std::function<void (int, int)> onSigChangedIn)
    : querySig (std::move (querySigIn)),
      onSigChanged (std::move (onSigChangedIn))
{
    // Seed from the live "n/d" string. A junk / absent query falls back to 4/4; the numerator is
    // clamped into range, the denominator matched against the offered powers of two below.
    const String seed    = querySig ? querySig() : "4/4";
    const int    seedNum = jlimit (minNumerator, maxNumerator,
                                   seed.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue());
    const int    seedDen = seed.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

    // NUMERATOR: editable, in the timeTempo transport-clock accent to echo the LCD (mirrors
    // TempoPopup's bpmLabel colour usage).
    numeratorLabel.setColour (Label::backgroundColourId,            Colour (ForgeLookAndFeel::lcdBg));
    numeratorLabel.setColour (Label::textColourId,                  Colour (ForgeLookAndFeel::timeTempo));
    numeratorLabel.setColour (Label::outlineColourId,               Colour (ForgeLookAndFeel::lcdFrame));
    numeratorLabel.setColour (Label::backgroundWhenEditingColourId, Colour (ForgeLookAndFeel::raisedBg));
    numeratorLabel.setColour (Label::textWhenEditingColourId,       Colour (ForgeLookAndFeel::textPrim));
    numeratorLabel.setJustificationType (Justification::centred);
    numeratorLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 24.0f, Font::plain)));
    numeratorLabel.setEditable (true, true, false);   // single- and double-click to edit; return/focus-loss commits
    numeratorLabel.onTextChange = [this] { applySig(); };
    // dontSendNotification: seeding must NOT fire onTextChange (that would write to the edit on open).
    numeratorLabel.setText (String (seedNum), dontSendNotification);
    addAndMakeVisible (numeratorLabel);

    // The "/" divider — static, secondary colour so the two editable fields read as the controls.
    slashLabel.setText ("/", dontSendNotification);
    slashLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
    slashLabel.setJustificationType (Justification::centred);
    slashLabel.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 24.0f, Font::plain)));
    addAndMakeVisible (slashLabel);

    // DENOMINATOR: the valid power-of-two meter denominators. The item ID IS the denominator value
    // (all non-zero, so getSelectedId() returns the denominator directly — no index bookkeeping).
    for (int d : { 1, 2, 4, 8, 16 })
        denominatorBox.addItem (String (d), d);

    denominatorBox.setColour (ComboBox::backgroundColourId, Colour (ForgeLookAndFeel::lcdBg));
    denominatorBox.setColour (ComboBox::textColourId,       Colour (ForgeLookAndFeel::timeTempo));
    denominatorBox.setColour (ComboBox::outlineColourId,    Colour (ForgeLookAndFeel::lcdFrame));
    denominatorBox.setJustificationType (Justification::centred);

    // Seed the selection to the live denominator when it is one of the offered powers of two, else a
    // 4 default (a stored 32, or any non-offered value, simply shows 4 until the user picks). Again
    // dontSendNotification so opening the popup is side-effect-free.
    const bool denOffered = (seedDen == 1 || seedDen == 2 || seedDen == 4 || seedDen == 8 || seedDen == 16);
    denominatorBox.setSelectedId (denOffered ? seedDen : 4, dontSendNotification);
    denominatorBox.onChange = [this] { applySig(); };
    addAndMakeVisible (denominatorBox);

    setSize (180, 72);
}

void TimeSigPopup::resized()
{
    auto area = getLocalBounds().reduced (10, 12);

    // numerator | "/" | denominator, one row. The slash takes a fixed sliver; the two fields split
    // the rest. The combo is inset a touch vertically so it doesn't tower over the big numerals.
    const int slashW = 16;
    numeratorLabel.setBounds (area.removeFromLeft ((area.getWidth() - slashW) / 2));
    slashLabel.setBounds     (area.removeFromLeft (slashW));
    denominatorBox.setBounds (area.reduced (0, 6));
}

void TimeSigPopup::applySig()
{
    const int num = jlimit (minNumerator, maxNumerator, numeratorLabel.getText().getIntValue());
    const int den = denominatorBox.getSelectedId();   // item IDs ARE the denominator values (1..16)

    if (onSigChanged && den > 0)
        onSigChanged (num, den);

    // Echo the canonical numerator back (a junk entry -> clamped); the combo already shows den.
    // dontSendNotification: this canonical echo must not re-enter onTextChange.
    numeratorLabel.setText (String (num), dontSendNotification);
}
