/*
    BrowserView — a real file browser for the collapsible LEFT shell region, replacing the
    Phase-2 placeholder Label. Lets the user navigate the filesystem and pick audio files to
    import: a small "Browser" header sits above a juce::FileTreeComponent, which is backed by a
    DirectoryContentsList scanned on a background TimeSliceThread and filtered to common audio
    extensions (*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3).

    Double-clicking an existing audio file fires onImportFile(file); the shell imports it via
    ProjectSession. A dumb view: it owns no project logic, only forwards intent via the callback.

    Message-thread only (scanning runs on the owned TimeSliceThread; callbacks arrive on the
    message thread).
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class BrowserView : public juce::Component,
                    private juce::FileBrowserListener
{
public:
    BrowserView();
    ~BrowserView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Fired when the user activates an audio file (double-click). The shell imports it.
    std::function<void (const juce::File&)> onImportFile;

private:
    //==============================================================================
    // FileBrowserListener — all four are pure-virtual, so all must be implemented.
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}

    //==============================================================================
    // Declaration order is load-bearing: the filter and thread must outlive the list, and the
    // list must outlive the tree that displays it (the tree holds a reference to the list).
    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3",
                                           "*",                       // allow all directories
                                           "Audio files" };
    juce::TimeSliceThread     scanThread  { "Forge Browser Scanner" };
    juce::DirectoryContentsList contents  { &audioFilter, scanThread };
    juce::FileTreeComponent   tree        { contents };

    juce::Label header;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserView)
};
