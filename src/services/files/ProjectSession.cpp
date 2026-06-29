#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"

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

    editFile = f;
    return te::EditFileOperations (*edit).saveAs (f, true);
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

te::TransportControl* ProjectSession::getTransport() const
{
    return edit != nullptr ? &edit->getTransport() : nullptr;
}

int ProjectSession::getNumAudioTracks() const
{
    return edit != nullptr ? te::getAudioTracks (*edit).size() : 0;
}
