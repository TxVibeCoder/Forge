/*
    ExportProgress — a small transient panel shown while an async audio export runs.

    It carries a caption (what is being exported), a determinate progress bar (0..1), and a
    Cancel button. It holds NO engine / Exporter / te:: state: the shell drives it from the
    Exporter's message-thread onProgress callback via setProgress(), and the Cancel button
    invokes the onCancel std::function (which the shell wires to the in-flight render's cancel()).

    A negative progress value renders the bar as indeterminate ("finishing…") — used when the
    render reports no measurable progress.

    Message-thread only. Nothing is logged from paint()/repaint() — it is a hot path and the
    JUCE ProgressBar repaints itself on its own timer.
*/

#pragma once

#include <JuceHeader.h>

class ExportProgress : public juce::Component
{
public:
    ExportProgress();
    ~ExportProgress() override;

    /** Sets the caption line, e.g. "Exporting mix.wav" or "Exporting stems…". Ignored once the
        user has pressed Cancel (the caption then reads "Cancelling…"). */
    void setCaption (const juce::String& caption);

    /** Updates the bar. Clamped to [0, 1]; a negative value shows the indeterminate animation.
        Message-thread only. */
    void setProgress (float progress);

    /** Invoked (on the message thread) when the user clicks Cancel. Fires at most once. Wire
        this to the render handle's cancel(). Leaving it unset simply makes Cancel a no-op. */
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label       captionLabel;
    double            progressValue { 0.0 };            // bound into progressBar by reference
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton  cancelButton { "Cancel" };
    bool              cancelled { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportProgress)
};
