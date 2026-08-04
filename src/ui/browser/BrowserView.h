/*
    BrowserView — the collapsible LEFT shell region's two-part picker: Forge's built-in INSTRUMENTS
    on top, the filesystem below.

    INSTRUMENTS (B6) is a fixed list of the built-in CC0 voices, rendered from PluginHost's single
    catalogue (getInstrumentChoices) so adding a voice to that table lights it up here with no change
    to this file. Double-clicking a row fires onInstrumentChosen(preset); the shell applies it to the
    current target track via ProjectSession::setTrackInstrument. It is a TRACK-level assignment —
    the engine has no per-clip instrument (see the W21 "first-instrument-wins" gotcha).

    FILES is a juce::FileTreeComponent backed by a DirectoryContentsList scanned on a background
    TimeSliceThread and filtered to common audio extensions (*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3)
    plus MIDI files (*.mid;*.midi). Double-clicking an existing file fires onImportFile(file); the
    shell imports it via ProjectSession (its dispatch branches audio-vs-MIDI on the extension, the
    same branch the Arrange file-drop uses).

    A dumb view either way: it owns no project logic and knows nothing about tracks — it only
    forwards intent via the two callbacks.

    Message-thread only (scanning runs on the owned TimeSliceThread; callbacks arrive on the
    message thread).
*/

#pragma once

#include <JuceHeader.h>

#include "engine/PluginHost.h"   // PluginHost::InstrumentChoice — the INSTRUMENTS list's contents

//==============================================================================
class BrowserView : public juce::Component,
                    private juce::FileBrowserListener,
                    private juce::ListBoxModel
{
public:
    BrowserView();
    ~BrowserView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Fired when the user activates an importable file (double-click) — audio or MIDI. The shell
    // imports it, branching on the extension (the same audio-vs-MIDI dispatch as the Arrange drop).
    std::function<void (const juce::File&)> onImportFile;

    /** Fired when the user activates an INSTRUMENTS row (double-click). The shell applies the voice
        to its current target track through ProjectSession::setTrackInstrument. Default null => the
        list still renders but activating a row is a no-op. */
    std::function<void (PluginHost::InstrumentPreset)> onInstrumentChosen;

    /** Number of rows in the INSTRUMENTS list (= PluginHost::getInstrumentChoices().size()). */
    int getNumInstrumentRows() const                 { return instruments.size(); }

    /** Fires onInstrumentChosen for `row` exactly as a double-click would, and selects the row.
        Returns false for an out-of-range row or an unwired callback. PUBLIC so a headless gate can
        drive the browser->instrument path without an OS mouse event (the same discipline as
        PianoRollView::handleWheel). Message-thread only. */
    bool activateInstrumentRow (int row);

private:
    //==============================================================================
    // FileBrowserListener — all four are pure-virtual, so all must be implemented.
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}

    //==============================================================================
    // ListBoxModel — the INSTRUMENTS list.
    int  getNumRows() override                       { return instruments.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int lastRowSelected) override;

    //==============================================================================
    // Declaration order is load-bearing: the filter and thread must outlive the list, and the
    // list must outlive the tree that displays it (the tree holds a reference to the list).
    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi",
                                           "*",                       // allow all directories
                                           "Audio & MIDI files" };
    juce::TimeSliceThread     scanThread  { "Forge Browser Scanner" };
    juce::DirectoryContentsList contents  { &audioFilter, scanThread };
    juce::FileTreeComponent   tree        { contents };

    // Snapshotted once in the ctor: the catalogue is a static table, so re-reading it per paint
    // would allocate on the paint path for nothing.
    const juce::Array<PluginHost::InstrumentChoice> instruments { PluginHost::getInstrumentChoices() };
    juce::ListBox instrumentList { "Instruments", this };

    juce::Label header, instrumentHeader, filesHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserView)
};
