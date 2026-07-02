#include "services/files/ProjectSession.h"
#include "engine/EngineHelpers.h"
#include "engine/PluginHost.h"
#include "engine/ClipFades.h"
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
    {
        ClipFades::applyDefaultEdgeFades (*clip);   // anti-click edge fade (P6)
        edit->markAsChanged();
    }
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
    {
        ClipFades::applyDefaultEdgeFades (*clip);   // anti-click edge fade (P6)
        edit->markAsChanged();
    }
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

void ProjectSession::setGlobalLaunchQuantisation (te::LaunchQType t)
{
    if (edit == nullptr)
        return;

    edit->getLaunchQuantisation().type = t;
}

te::LaunchQType ProjectSession::getGlobalLaunchQuantisation() const
{
    return edit != nullptr ? edit->getLaunchQuantisation().type.get() : te::LaunchQType::bar;
}

//==============================================================================
// Session MIDI-record seam (W7). Message-thread only.
//
// ProjectSession orchestrates the born-audible + arm recipe on top of the injected recorder
// std::function seams (recorderArmSlot / recorderDisarmSlot / recorderIsSlotArmed), so it holds no
// hard RecordController dependency and never touches raw engine record APIs itself apart from the
// transport roll/stop. The single active record slot is tracked here (activeRecordTrack/Scene).

bool ProjectSession::recordArmSlot (int trackIndex, int sceneIndex)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0)
    {
        FORGE_LOG_ERROR ("recordArmSlot: no edit or out-of-range cell "
                         + juce::String (trackIndex) + "," + juce::String (sceneIndex));
        return false;
    }

    // Ensure the track and the grid row exist (mutating path — may grow tracks/scenes).
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("recordArmSlot: failed to create or access track at index " + juce::String (trackIndex));
        return false;
    }

    ensureScenes (sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots (sceneIndex + 1);   // pad a slot-deficient loaded track

    // Born-audible: the engine materialises the captured clip on stop; the track must already host an
    // instrument for that clip to sound (omit -> silent captured clip). Idempotent — never stacks synths.
    PluginHost::ensureDefaultInstrument (*track);

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("recordArmSlot: clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex)
                         + " could not be resolved after grid growth");
        return false;
    }

    // Delegate the actual engine arm to the injected recorder seam. Do NOT pre-insert a clip — the
    // engine creates the born-audible MidiClip in the slot at commit (addMidiAsTransaction on stop).
    if (! recorderArmSlot)
    {
        FORGE_LOG_ERROR ("recordArmSlot: recorder arm-slot seam not wired");
        return false;
    }

    if (! recorderArmSlot (*edit, *slot))
        return false;   // recorder logs / sets its own lastError; the shell surfaces getLastError()

    activeRecordTrack = trackIndex;
    activeRecordScene = sceneIndex;
    return true;
}

void ProjectSession::recordDisarmSlot (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return;

    // removeTarget fails while recording — stop the transport FIRST if this is the capturing slot.
    if (auto* transport = getTransport())
        if (transport->isRecording())
            transport->stop (false, false);

    if (auto* slot = getClipSlot (trackIndex, sceneIndex))
    {
        if (recorderDisarmSlot)
            recorderDisarmSlot (*edit, *slot);
    }
    else
    {
        FORGE_LOG_ERROR ("recordDisarmSlot: clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex)
                         + " could not be resolved");
    }

    if (trackIndex == activeRecordTrack && sceneIndex == activeRecordScene)
    {
        activeRecordTrack = -1;
        activeRecordScene = -1;
    }
}

bool ProjectSession::isSlotRecordArmed (int trackIndex, int sceneIndex) const
{
    // Pure read (25 Hz poll) — re-derived from engine targets via the injected seam, no cached flag,
    // never logs. getClipSlot is the const, non-mutating resolve.
    if (edit == nullptr || ! recorderIsSlotArmed)
        return false;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    return slot != nullptr && recorderIsSlotArmed (*edit, *slot);
}

void ProjectSession::beginSlotRecord (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return;

    // No-op unless this is the currently-armed active record slot.
    if (trackIndex != activeRecordTrack || sceneIndex != activeRecordScene)
        return;

    // Ensure the track is born-audible before rolling (idempotent). Resolve the track without
    // growing structure — the slot was already ensured/armed by recordArmSlot.
    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= 0 && trackIndex < tracks.size())
        if (auto* track = tracks[trackIndex])
            PluginHost::ensureDefaultInstrument (*track);

    // Roll: the armed slot's recordEnabled destination is what startRecording captures. NO launchSlot
    // (recording is transport-driven; there is no clip to launch yet — §0/§6).
    if (auto* transport = getTransport())
        transport->record (false);
}

te::MidiClip::Ptr ProjectSession::commitSlotRecord (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return {};

    // Stop first: the engine commits the captured notes into a new MidiClip in the slot via
    // addMidiAsTransaction on stop.
    if (auto* transport = getTransport())
        transport->stop (false, false);

    // Disarm the slot (removeTarget now succeeds — transport stopped). This also clears the active
    // record slot when it matches.
    recordDisarmSlot (trackIndex, sceneIndex);

    // Re-resolve the slot and return the captured MidiClip, if any.
    if (auto* slot = getClipSlot (trackIndex, sceneIndex))
        if (auto* clip = dynamic_cast<te::MidiClip*> (slot->getClip()))
            return te::MidiClip::Ptr (clip);

    return {};
}

bool ProjectSession::isSlotRecording (int trackIndex, int sceneIndex) const
{
    // Pure engine read (drives the recording pad in the 25 Hz poll) — never logs. A slot is capturing
    // iff it is the armed record target AND the transport is recording. NOT "LaunchHandle playing"
    // (unreachable for an empty capturing slot).
    if (edit == nullptr || ! recorderIsSlotArmed)
        return false;

    auto* transport = getTransport();

    if (transport == nullptr || ! transport->isRecording())
        return false;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    return slot != nullptr && recorderIsSlotArmed (*edit, *slot);
}

//==============================================================================
// Aux buses / sends seam (P3). Message-thread only.
//
// A bus is a plain te::AudioTrack hosting an AuxReturnPlugin; a per-track AuxSendPlugin with a
// matching busNumber feeds it. Return tracks are APPENDED at the END of the track list so absolute
// track indices (used by MixerView sends + the Session grid) never shift. See the header.

te::AudioTrack* ProjectSession::getAuxReturnTrack (int busIdx) const
{
    if (edit == nullptr)
        return nullptr;

    for (auto* track : te::getAudioTracks (*edit))
        if (track != nullptr)
            for (auto* p : track->pluginList)
                if (auto* ar = dynamic_cast<te::AuxReturnPlugin*> (p))
                    if (ar->busNumber == busIdx)
                        return track;

    return nullptr;
}

bool ProjectSession::isAuxReturnTrack (const te::AudioTrack& track) const
{
    // pluginList iteration is non-const; the const_cast reads the chain without mutating it.
    for (auto* p : const_cast<te::AudioTrack&> (track).pluginList)
        if (dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr)
            return true;

    return false;
}

juce::String ProjectSession::getAuxBusName (int busIdx) const
{
    return edit != nullptr ? edit->getAuxBusName (busIdx) : juce::String();
}

te::AudioTrack* ProjectSession::ensureAuxBus (int busIdx)
{
    if (edit == nullptr || busIdx < 0)
    {
        FORGE_LOG_ERROR ("ensureAuxBus: no edit or bad bus index " + juce::String (busIdx));
        return nullptr;
    }

    // Grow-only: reuse the existing return for this bus if one is already provisioned.
    if (auto* existing = getAuxReturnTrack (busIdx))
        return existing;

    // Append a NEW audio track at the END of the list (LOAD-BEARING — keeps every existing absolute
    // track index stable, so mixer sends + the Session grid keep addressing tracks correctly). Default
    // plugins give it the vol/pan + level-meter tail the return fader + meter read.
    auto returnTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);

    if (returnTrack == nullptr)
    {
        FORGE_LOG_ERROR ("ensureAuxBus: failed to insert aux-return track for bus " + juce::String (busIdx));
        return nullptr;
    }

    // Put an AuxReturnPlugin(busNumber=busIdx) at the HEAD of the return track's chain.
    auto returnPlugin = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});

    if (auto* ar = dynamic_cast<te::AuxReturnPlugin*> (returnPlugin.get()))
    {
        ar->busNumber = busIdx;
        returnTrack->pluginList.insertPlugin (returnPlugin, 0, nullptr);
    }
    else
    {
        FORGE_LOG_ERROR ("ensureAuxBus: failed to create AuxReturnPlugin for bus " + juce::String (busIdx));
    }

    // Friendly name (Return A / Return B / …) on both the aux-bus map (what the mixer reads) and the
    // track (what Arrange/Session show).
    const juce::String label = "Return " + juce::String::charToString ((juce::juce_wchar) ('A' + busIdx));
    edit->setAuxBusName (busIdx, label);
    returnTrack->setName (label);

    edit->markAsChanged();

    // A track was actually added → let the shell rebuild track-ref-caching views + persist.
    if (onTracksChanged)
        onTracksChanged();

    return returnTrack.get();
}

void ProjectSession::setTrackSendLevel (int trackIndex, int busIdx, float gainDb)
{
    if (edit == nullptr || trackIndex < 0)
    {
        FORGE_LOG_ERROR ("setTrackSendLevel: no edit or bad track index " + juce::String (trackIndex));
        return;
    }

    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
    {
        FORGE_LOG_ERROR ("setTrackSendLevel: track index out of range " + juce::String (trackIndex));
        return;
    }

    // Make sure the destination bus exists (grow-only; may append a return track at the end). Resolve
    // the sending track AFTER, so `tracks` isn't stale if ensureAuxBus appended (it appends at the end,
    // so trackIndex stays valid, but re-fetch to be safe).
    if (ensureAuxBus (busIdx) == nullptr)
    {
        FORGE_LOG_ERROR ("setTrackSendLevel: could not ensure aux bus " + juce::String (busIdx));
        return;
    }

    tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
        return;

    auto* track = tracks[trackIndex];

    // Find an existing send for this bus, else create + insert one post-fader.
    te::AuxSendPlugin* send = nullptr;

    for (auto* p : track->pluginList)
        if (auto* as = dynamic_cast<te::AuxSendPlugin*> (p))
            if (as->busNumber == busIdx)
            {
                send = as;
                break;
            }

    if (send == nullptr)
    {
        auto sendPlugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::create());
        send = dynamic_cast<te::AuxSendPlugin*> (sendPlugin.get());

        if (send == nullptr)
        {
            FORGE_LOG_ERROR ("setTrackSendLevel: failed to create AuxSendPlugin for bus " + juce::String (busIdx));
            return;
        }

        send->busNumber = busIdx;

        // Post-fader: insert just AFTER the VolumeAndPanPlugin (before the meter tail). Fall back to
        // appending (-1) if there's no volume plugin.
        int insertIndex = -1;

        if (auto* volume = track->getVolumePlugin())
        {
            const auto idx = track->pluginList.indexOf (volume);
            if (idx >= 0)
                insertIndex = idx + 1;   // AFTER the volume/pan → post-fader send
        }

        track->pluginList.insertPlugin (sendPlugin, insertIndex, nullptr);
    }

    send->setGainDb (juce::jlimit (-100.0f, 6.0f, gainDb));
    edit->markAsChanged();
}

float ProjectSession::getTrackSendLevel (int trackIndex, int busIdx) const
{
    if (edit == nullptr || trackIndex < 0)
        return -100.0f;

    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
        return -100.0f;

    for (auto* p : tracks[trackIndex]->pluginList)
        if (auto* as = dynamic_cast<te::AuxSendPlugin*> (p))
            if (as->busNumber == busIdx)
                return as->getGainDb();

    return -100.0f;   // no send for this bus == silence
}

//==============================================================================
// Markers / cue points seam (P5). Message-thread only. Stable key = EditItemID (const, edit-wide
// unique), NOT the reassignable marker NUMBER. Pure reads never log.

te::MarkerClip* ProjectSession::findMarkerById (te::EditItemID id) const
{
    if (edit == nullptr)
        return nullptr;

    for (auto* clip : edit->getMarkerManager().getMarkers())   // temp array; the track owns the clips
        if (clip != nullptr && clip->itemID == id)
            return clip;

    return nullptr;
}

te::EditItemID ProjectSession::addMarker (te::TimePosition time, const juce::String& name)
{
    if (edit == nullptr)
        return {};

    edit->ensureMarkerTrack();   // idempotent; guards createMarker's null path

    auto clip = edit->getMarkerManager().createMarker (-1, time, {}, nullptr);   // -1 → auto unique number

    if (clip == nullptr)
    {
        FORGE_LOG_ERROR ("addMarker: createMarker failed (no marker track)");
        return {};
    }

    if (name.isNotEmpty())
        clip->setName (name);   // else keep the engine default "Marker N"

    edit->markAsChanged();
    return clip->itemID;
}

bool ProjectSession::removeMarker (te::EditItemID id)
{
    if (edit == nullptr)
        return false;

    auto* clip = findMarkerById (id);

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("removeMarker: id not found");
        return false;
    }

    clip->removeFromParent();   // MarkerManager has no delete method
    edit->markAsChanged();
    return true;
}

bool ProjectSession::moveMarker (te::EditItemID id, te::TimePosition newTime)
{
    if (edit == nullptr)
        return false;

    auto* clip = findMarkerById (id);

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("moveMarker: id not found");
        return false;
    }

    clip->setStart (newTime, /*preserveSync*/ false, /*keepLength*/ true);   // engine clamps to [0, maxEnd]
    edit->markAsChanged();
    return true;
}

bool ProjectSession::renameMarker (te::EditItemID id, const juce::String& newName)
{
    if (edit == nullptr)
        return false;

    auto* clip = findMarkerById (id);

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("renameMarker: id not found");
        return false;
    }

    clip->setName (newName);
    edit->markAsChanged();
    return true;
}

std::vector<ProjectSession::Marker> ProjectSession::getMarkers() const
{
    std::vector<Marker> out;

    if (edit == nullptr)
        return out;

    for (auto* clip : edit->getMarkerManager().getMarkers())
        if (clip != nullptr)
            out.push_back ({ clip->itemID, clip->getPosition().getStart(), clip->getName() });

    return out;   // already time-sorted by getMarkers()
}

void ProjectSession::jumpTransportTo (te::TimePosition time)
{
    if (auto* t = getTransport())
        t->setPosition (time);
}
