#include "ui/export/ExportProgress.h"

using namespace juce;

ExportProgress::ExportProgress()
{
    captionLabel.setJustificationType (Justification::centredLeft);
    captionLabel.setInterceptsMouseClicks (false, false);
    captionLabel.setColour (Label::textColourId, Colours::white);
    addAndMakeVisible (captionLabel);

    // The JUCE ProgressBar runs its own repaint timer while visible and reads progressValue by
    // reference, so setProgress() only needs to update the value — no manual repaint here.
    progressBar.setPercentageDisplay (true);
    addAndMakeVisible (progressBar);

    cancelButton.onClick = [this]
    {
        if (cancelled)
            return;

        cancelled = true;
        cancelButton.setEnabled (false);
        captionLabel.setText ("Cancelling…", dontSendNotification);

        if (onCancel)
            onCancel();
    };
    addAndMakeVisible (cancelButton);

    setSize (360, 128);
}

ExportProgress::~ExportProgress() = default;

void ExportProgress::setCaption (const String& caption)
{
    if (! cancelled)
        captionLabel.setText (caption, dontSendNotification);
}

void ExportProgress::setProgress (float progress)
{
    // Negative → indeterminate animation (JUCE ProgressBar convention); otherwise clamp to [0, 1].
    progressValue = progress < 0.0f ? -1.0 : (double) jlimit (0.0f, 1.0f, progress);
}

void ExportProgress::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff2a2a2e));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colour (0xff4a4a52));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void ExportProgress::resized()
{
    auto b = getLocalBounds().reduced (18);
    captionLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (10);
    cancelButton.setBounds (b.removeFromBottom (26).removeFromRight (96));
    b.removeFromBottom (10);
    progressBar.setBounds (b.removeFromTop (22));
}
