#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"

ProjectSession::ProjectSession (te::Engine& e)
    : engine (e)
{
}

void ProjectSession::newProject (const juce::File& f)
{
    editFile = f;
    f.getParentDirectory().createDirectory();

    edit = te::createEmptyEdit (engine, f);

    // Write the (empty) edit to disk NOW, before any clip is inserted, so that clip
    // source files are serialized as paths relative to the edit file.
    te::EditFileOperations (*edit).save (true, true, false);

    edit->ensureNumberOfAudioTracks (1);
}

bool ProjectSession::openProject (const juce::File& f)
{
    auto loaded = te::loadEditFromFile (engine, f);

    if (loaded == nullptr)
        return false;

    edit = std::move (loaded);
    editFile = f;
    edit->ensureNumberOfAudioTracks (1);
    return true;
}

bool ProjectSession::openOrCreate (const juce::File& f)
{
    if (f.existsAsFile() && openProject (f))
        return true;

    newProject (f);
    return false;
}

bool ProjectSession::save()
{
    if (edit == nullptr)
        return false;

    return te::EditFileOperations (*edit).save (true, true, false);
}

bool ProjectSession::saveAs (const juce::File& f)
{
    if (edit == nullptr)
        return false;

    // Only adopt the new file once the write actually succeeds. On failure leave editFile
    // (and thus getEditFile()) pointing at the previously-saved location.
    if (! te::EditFileOperations (*edit).saveAs (f, true))
        return false;

    editFile = f;
    return true;
}

te::WaveAudioClip::Ptr ProjectSession::importAudioFile (const juce::File& f, te::TimePosition start)
{
    if (edit == nullptr)
        return {};

    auto clip = EngineHelpers::loadAudioFileAsClip (*edit, f, 0, start);

    if (clip != nullptr)
        edit->markAsChanged();

    return clip;
}

te::MidiClip::Ptr ProjectSession::createMidiClip (int trackIndex, te::TimeRange range, const juce::String& name)
{
    if (edit == nullptr)
        return {};

    // Ensure the track exists, then insert the (empty) MIDI clip on it. We keep the track handle
    // so we can give it a default instrument here — the MIDI-clip insert deliberately stays out of
    // the plugin chain (EngineHelpers stays free of PluginHost; ensuring audibility lives here).
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
        return {};

    auto clip = track->insertMIDIClip (name, range, nullptr);

    if (clip != nullptr)
    {
        PluginHost::ensureDefaultInstrument (*track);   // born audible — default 4OSC at chain head
        edit->markAsChanged();
    }

    return clip;
}

te::TransportControl* ProjectSession::getTransport() const
{
    return edit != nullptr ? &edit->getTransport() : nullptr;
}

int ProjectSession::getNumAudioTracks() const
{
    return edit != nullptr ? te::getAudioTracks (*edit).size() : 0;
}

bool ProjectSession::isModified() const
{
    return edit != nullptr && edit->hasChangedSinceSaved();
}

int ProjectSession::getNumClipsOnTrack0() const
{
    if (edit == nullptr)
        return 0;

    auto tracks = te::getAudioTracks (*edit);

    if (tracks.isEmpty())
        return 0;

    return tracks.getFirst()->getClips().size();
}
