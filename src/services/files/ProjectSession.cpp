#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"
#include "core/Log.h"

ProjectSession::ProjectSession (te::Engine& e)
    : engine (e)
{
}

void ProjectSession::newProject (const juce::File& f)
{
    editFile = f;
    const auto parentDir = f.getParentDirectory();

    if (! parentDir.createDirectory().wasOk())
        FORGE_LOG_WARN ("Failed to create project parent directory: " + parentDir.getFullPathName() + " — the edit may not persist correctly");

    edit = te::createEmptyEdit (engine, f);

    if (edit == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create empty edit in memory");
        return;
    }

    // Write the (empty) edit to disk NOW, before any clip is inserted, so that clip
    // source files are serialized as paths relative to the edit file.
    if (! te::EditFileOperations (*edit).save (true, true, false))
        FORGE_LOG_ERROR ("Failed to save new project to disk at " + f.getFullPathName());

    edit->ensureNumberOfAudioTracks (1);
}

bool ProjectSession::openProject (const juce::File& f)
{
    auto loaded = te::loadEditFromFile (engine, f);

    if (loaded == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to load project from " + f.getFullPathName() + " — file may be corrupted or unsupported");
        return false;
    }

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

    const bool ok = te::EditFileOperations (*edit).save (true, true, false);

    if (! ok)
        FORGE_LOG_ERROR ("Failed to save project to " + editFile.getFullPathName());

    return ok;
}

bool ProjectSession::saveAs (const juce::File& f)
{
    if (edit == nullptr)
        return false;

    // Only adopt the new file once the write actually succeeds. On failure leave editFile
    // (and thus getEditFile()) pointing at the previously-saved location.
    if (! te::EditFileOperations (*edit).saveAs (f, true))
    {
        FORGE_LOG_ERROR ("Failed to save project to " + f.getFullPathName());
        return false;
    }

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
    else
        FORGE_LOG_ERROR ("Failed to import audio file " + f.getFullPathName() + " — format may be unsupported");

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
    {
        FORGE_LOG_ERROR ("Failed to create MIDI clip on track " + juce::String (trackIndex));
        return {};
    }

    auto clip = track->insertMIDIClip (name, range, nullptr);

    if (clip != nullptr)
    {
        PluginHost::ensureDefaultInstrument (*track);   // born audible — default 4OSC at chain head
        edit->markAsChanged();
    }
    else
    {
        FORGE_LOG_ERROR ("Failed to create MIDI clip on track " + juce::String (trackIndex));
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

//==============================================================================
// SessionView clip-launch grid seam.
//
// The launch helpers below are ported (with minimal cosmetic changes) from the verified
// in-tree reference utils:: namespace in
//   libs/.../examples/DemoRunner/demos/ClipLauncherDemo.h  (~lines 100-363).
// They are the proven quantise + audibility logic: launchClip starts the transport (so a
// launched clip SOUNDS while playing) and queues to the next launch-quantise boundary via
// getLaunchPosition when the transport is already running. Kept in an anonymous namespace so
// they are private to this translation unit; the public ProjectSession methods are thin,
// message-thread, model-mutation seams over them. Never call LaunchHandle::advance() (R5).
namespace
{
    te::TimePosition getStartTimeOfBar (te::TransportControl& tc, te::TimePosition t)
    {
        auto& ts = tc.edit.tempoSequence;
        const auto barsBeats = ts.toBarsAndBeats (t);
        return ts.toTime (te::tempo::BarsAndBeats { .bars = barsBeats.bars });
    }

    void pauseForSyncPointChange (te::Edit& edit)
    {
        // Pause until the position change has gone through to the clips.
        if (auto epc = edit.getTransport().getCurrentPlaybackContext())
            epc->blockUntilSyncPointChange();
    }

    te::BeatDuration getLaunchOffset (const te::LaunchQuantisation& lq, const te::BeatPosition pos,
                                      std::optional<te::BeatRange> loopRange)
    {
        // Quantise position first, ensuring it is not negative.
        te::BeatPosition quantisedPos = pos;

        for (;;)
        {
            quantisedPos = lq.getNext (quantisedPos);

            if (quantisedPos >= te::BeatPosition())
                break;

            quantisedPos = quantisedPos + te::BeatDuration::fromBeats (0.001);
        }

        if (! loopRange || quantisedPos < loopRange->getEnd())
            return quantisedPos - pos;

        // If it's after the loop range, quantise the start loop position.
        quantisedPos = lq.getNext (loopRange->getStart());

        if (! loopRange->contains (quantisedPos))
            quantisedPos = loopRange->getStart();

        if (loopRange->contains (pos))
            return (loopRange->getEnd() - pos) + (quantisedPos - loopRange->getStart());

        return quantisedPos - pos;
    }

    te::LaunchQuantisation& getLaunchQuantisation (te::Clip& c)
    {
        if (! c.usesGlobalLaunchQuatisation())
            if (auto clipLQ = c.getLaunchQuantisation())
                return *clipLQ;

        return c.edit.getLaunchQuantisation();
    }

    te::MonotonicBeat getLaunchPosition (te::Edit& e, const te::LaunchQuantisation& lq, te::SyncPoint syncPoint)
    {
        auto& t = e.getTransport();
        auto& ts = e.tempoSequence;
        const auto currentBeat = syncPoint.beat;
        const auto offset = getLaunchOffset (lq,
                                             currentBeat,
                                             t.looping.get() ? std::optional (ts.toBeats (t.getLoopRange()))
                                                             : std::nullopt);

        return { syncPoint.monotonicBeat.v + offset };
    }

    te::MonotonicBeat getLaunchPosition (te::Edit& e, const te::LaunchQuantisation& lq)
    {
        auto epc = e.getTransport().getCurrentPlaybackContext();

        if (! epc)
            return {};

        auto syncPoint = epc->getSyncPoint();

        if (! syncPoint)
            return {};

        return getLaunchPosition (e, lq, *syncPoint);
    }

    te::MonotonicBeat getLaunchPosition (te::Clip& c)
    {
        return getLaunchPosition (c.edit, getLaunchQuantisation (c));
    }

    te::MonotonicBeat getStopPosition (te::Clip& c)
    {
        return getLaunchPosition (c);
    }

    enum class StartTransport { no, yes };

    void launchClip (te::Clip& c, bool stopOthers, StartTransport startTransport)
    {
        if (startTransport == StartTransport::yes
            && (! c.edit.getTransport().isPlaying()))
        {
            auto& tc = c.edit.getTransport();
            tc.setPosition (getStartTimeOfBar (tc, tc.startPosition));
            pauseForSyncPointChange (c.edit);
        }

        if (stopOthers)
        {
            if (auto at = dynamic_cast<te::AudioTrack*> (c.getTrack()))
            {
                for (auto slot : at->getClipSlotList().getClipSlots())
                {
                    if (slot != nullptr)
                    {
                        if (auto otherClip = slot->getClip())
                        {
                            if (&c == otherClip)
                                launchClip (c, false, StartTransport::no);
                            else if (auto lh = otherClip->getLaunchHandle())
                                lh->stop (c.edit.getTransport().isPlaying()
                                              ? std::make_optional (getStopPosition (*otherClip))
                                              : std::nullopt);
                        }
                    }
                }
            }
        }
        else
        {
            if (auto lh = c.getLaunchHandle())
                lh->play (getLaunchPosition (c));
        }

        if (startTransport == StartTransport::yes)
            if (! c.edit.getTransport().isPlaying())
                c.edit.getTransport().play (false);
    }

    void launchSceneClips (te::Edit& e, int sceneIndex)
    {
        for (auto at : te::getAudioTracks (e))
        {
            auto slots = at->getClipSlotList().getClipSlots();

            if (sceneIndex < 0 || sceneIndex >= slots.size())
                continue;

            const bool shouldStopClipsOnTrack = [cs = slots[sceneIndex]]
                                                { return cs == nullptr || cs->getClip() != nullptr; }();

            for (int i = 0; i < slots.size(); ++i)
            {
                if (auto slot = slots[i])
                {
                    if (auto c = slot->getClip())
                    {
                        if (i == sceneIndex)
                            launchClip (*c, false, StartTransport::yes);
                        else if (auto lh = c->getLaunchHandle(); lh && shouldStopClipsOnTrack)
                            lh->stop (at->edit.getTransport().isPlaying()
                                          ? std::make_optional (getStopPosition (*c))
                                          : std::nullopt);
                    }
                }
            }
        }
    }

    void stopClipInSlot (te::ClipSlot* slot, te::Edit& e)
    {
        if (slot == nullptr)
            return;

        if (auto c = slot->getClip())
            if (auto lh = c->getLaunchHandle())
                lh->stop (e.getTransport().isPlaying() ? std::make_optional (getStopPosition (*c))
                                                       : std::nullopt);
    }

    // Sheet-00 default scene names for the first 8 rows; rows 9-16 stay unnamed (the UI shows
    // the row number for an empty name). Used only to seed a newly-created, still-unnamed scene.
    const juce::StringArray& defaultSceneNames()
    {
        static const juce::StringArray names { "Intro", "Verse A", "Pre", "Chorus",
                                               "Verse B", "Bridge", "Drop", "Outro" };
        return names;
    }
}

void ProjectSession::ensureScenes (int n)
{
    if (edit == nullptr || n <= 0)
        return;

    auto& sceneList = edit->getSceneList();

    // R3: only grow. Preserve existing scenes/clips; legacy 0-scene edits are safe.
    const int oldCount = sceneList.getNumScenes();
    if (oldCount >= n)
        return;

    {
        // R3: keep the grow AND the default-name seed OFF the user undo stack. The inhibitor
        // prevents a new undo transaction being opened for the structural append + the name
        // seeds; clearing the undo history afterwards guarantees the user's first Ctrl-Z can't
        // wipe the freshly-grown grid. ensureScenes only runs at bind/load time (and only when
        // actually growing), where there is no meaningful user undo history to discard.
        const te::Edit::UndoTransactionInhibitor inhibitor (*edit);

        sceneList.ensureNumberOfScenes (n);

        // Seed default names ONLY for the scenes we just appended (indices [oldCount, n)). A
        // pre-existing scene the user deliberately left blank in a loaded edit is NOT re-seeded
        // (QC fix: the old loop scanned ALL scenes for an empty name and would overwrite a blank
        // loaded row on every bind).
        const auto& names = defaultSceneNames();

        for (auto* scene : sceneList.getScenes())
        {
            if (scene == nullptr)
                continue;

            const int idx = scene->getIndex();

            if (idx >= oldCount && idx < names.size() && scene->name.get().isEmpty())
                scene->name = names[idx];
        }
    }

    edit->getUndoManager().clearUndoHistory();

    // R3: a pure grow-to-default does NOT markAsChanged (no user mutation).
}

int ProjectSession::getNumScenes() const
{
    return edit != nullptr ? edit->getSceneList().getNumScenes() : 0;
}

te::ClipSlot* ProjectSession::getClipSlot (int trackIndex, int sceneIndex) const
{
    // R2: const, non-mutating. Walk existing tracks only — NEVER insert a track or a slot.
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0)
        return nullptr;

    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
        return nullptr;

    auto slots = tracks[trackIndex]->getClipSlotList().getClipSlots();

    if (sceneIndex >= slots.size())
        return nullptr;

    return slots[sceneIndex];
}

bool ProjectSession::isSlotFilled (int trackIndex, int sceneIndex) const
{
    auto* slot = getClipSlot (trackIndex, sceneIndex);
    return slot != nullptr && slot->getClip() != nullptr;
}

te::MidiClip::Ptr ProjectSession::createMidiClipInSlot (int trackIndex, int sceneIndex, const juce::String& name)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0)
        return {};

    // Ensure the track and the grid row exist (mutating path — may grow tracks/scenes, R2).
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex));
        return {};
    }

    ensureScenes (sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots (sceneIndex + 1);   // pad a slot-deficient loaded track (QC fix)

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("Clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " could not be resolved after grid growth");
        return {};
    }

    // Default ~4-bar / 16-beat clip length, built from the tempo sequence like the linear
    // MIDI-create caller in main.cpp. Slot clips start at beat 0 (the slot owns the position).
    const auto startTime = edit->tempoSequence.toTime (te::BeatPosition());
    const auto endTime   = edit->tempoSequence.toTime (te::BeatPosition() + te::BeatDuration::fromBeats (16.0));

    // Slot inserts use the FREE te::insertMIDIClip(ClipOwner&, name, TimeRange) — name BEFORE
    // range — via ClipSlot's implicit upcast to ClipOwner&. (Not the AudioTrack member overload,
    // which would target the track's linear timeline, not the slot.)
    auto clip = te::insertMIDIClip (*slot, name, te::TimeRange (startTime, endTime));

    if (clip != nullptr)
    {
        PluginHost::ensureDefaultInstrument (*track);   // born audible — default 4OSC at chain head
        edit->markAsChanged();                          // user mutation -> persist (Sf)
    }
    else
    {
        FORGE_LOG_ERROR ("Failed to insert MIDI clip into slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex));
    }

    return clip;
}

te::WaveAudioClip::Ptr ProjectSession::importAudioIntoSlot (int trackIndex, int sceneIndex, const juce::File& file)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0 || ! file.existsAsFile())
        return {};

    // Sf: the wave source serialises RELATIVE to the edit file only if the edit already exists on disk.
    // Guarantee that invariant before inserting (mirrors newProject's save-first rule) — and BAIL if the
    // save fails, so we never fall through to an absolute-path insert (QC fix; saveAs checks this too).
    if (! editFile.existsAsFile() && ! save())
    {
        FORGE_LOG_ERROR ("Failed to save project before importing audio (path must serialize relative)");
        return {};
    }

    // Mutating path: ensure track + grid row exist.
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex));
        return {};
    }

    ensureScenes (sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots (sceneIndex + 1);   // pad a slot-deficient loaded track (QC fix)

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("Clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " could not be resolved after grid growth");
        return {};
    }

    te::AudioFile audioFile (edit->engine, file);

    if (! audioFile.isValid())
    {
        FORGE_LOG_ERROR ("Failed to load audio file " + file.getFullPathName() + " — format may be unsupported");
        return {};
    }

    const auto length = te::TimeDuration::fromSeconds (audioFile.getLength());
    const te::ClipPosition pos { { te::TimePosition(), te::TimePosition() + length }, {} };

    // Free te::insertWaveClip(ClipOwner&, name, file, ClipPosition, DeleteExistingClips) via the
    // slot's upcast. DeleteExistingClips::yes -> one clip per slot.
    auto clip = te::insertWaveClip (*slot, file.getFileNameWithoutExtension(), file,
                                    pos, te::DeleteExistingClips::yes);

    if (clip != nullptr)
        edit->markAsChanged();
    else
        FORGE_LOG_ERROR ("Failed to insert audio clip into slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex));

    return clip;
}

void ProjectSession::launchSlot (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
        return;

    if (auto c = slot->getClip())
        launchClip (*c, /*stopOthers*/ true, StartTransport::yes);   // per-track exclusivity + audible
}

void ProjectSession::stopSlot (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return;

    stopClipInSlot (getClipSlot (trackIndex, sceneIndex), *edit);
}

void ProjectSession::stopTrackClips (int trackIndex)
{
    if (edit == nullptr || trackIndex < 0)
        return;

    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
        return;

    for (auto slot : tracks[trackIndex]->getClipSlotList().getClipSlots())
        stopClipInSlot (slot, *edit);
}

void ProjectSession::stopAllSlots()
{
    if (edit == nullptr)
        return;

    for (auto at : te::getAudioTracks (*edit))
        for (auto slot : at->getClipSlotList().getClipSlots())
            stopClipInSlot (slot, *edit);
}

void ProjectSession::launchScene (int sceneIndex)
{
    if (edit == nullptr)
        return;

    launchSceneClips (*edit, sceneIndex);
}
