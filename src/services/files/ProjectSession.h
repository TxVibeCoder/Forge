/*
    ProjectSession — owns the active project (a Tracktion Edit) and its file on disk.

    The single source of truth for "the open project". Wraps create / open / save with the
    real engine APIs and enforces the load-bearing ordering gotcha: a new Edit is written to
    disk *before* any clip is inserted, so clip source files serialize as relative paths.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class ProjectSession
{
public:
    explicit ProjectSession (te::Engine&);

    /** Opens editFile if it exists (else creates a new project there).
        Returns true if an existing project was loaded, false if a new one was created. */
    bool openOrCreate (const juce::File& editFile);

    /** Creates a fresh empty 1-track project at editFile and writes it to disk immediately. */
    void newProject (const juce::File& editFile);

    /** Loads an existing .tracktionedit. Returns false (and leaves the current edit) on failure. */
    bool openProject (const juce::File& editFile);

    /** Persists the current edit to its file. */
    bool save();

    /** Persists the current edit to a new file and adopts it as the current file. */
    bool saveAs (const juce::File&);

    /** Imports an audio file as a clip on track 0 starting at `start`. Returns the clip. */
    te::WaveAudioClip::Ptr importAudioFile (const juce::File&, te::TimePosition start);

    te::Edit*             getEdit() const      { return edit.get(); }
    te::TransportControl* getTransport() const;
    juce::File            getEditFile() const  { return editFile; }
    int                   getNumAudioTracks() const;

    /** True if the edit has unsaved changes. Backed by te::Edit::hasChangedSinceSaved();
        returns false when there is no open edit. Use this to enable a Save action and to
        decide whether to prompt before closing. */
    bool                  isModified() const;

    /** Number of clips on the first audio track (track 0). Returns 0 if there is no edit
        or no track 0. Safe, read-only convenience accessor. */
    int                   getNumClipsOnTrack0() const;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    juce::File editFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSession)
};
