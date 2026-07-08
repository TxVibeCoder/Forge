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
    stopPerformanceCapture (false);   // abandon any in-flight capture across an edit swap (Wave 7)
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
    stopPerformanceCapture (false);   // abandon any in-flight capture across an edit swap (Wave 7)
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

te::WaveAudioClip::Ptr ProjectSession::importAudioFile (const juce::File& f, te::TimePosition start, int trackIndex)
{
    if (edit == nullptr)
        return {};

    auto clip = EngineHelpers::loadAudioFileAsClip (*edit, f, trackIndex, start);

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

te::Clip* ProjectSession::sendSlotToArrangement (int trackIndex, int sceneIndex, bool keepAsLoop)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0)
        return nullptr;

    // Source: the slot clip, resolved fresh via the const getClipSlot (never cached, R1/R2).
    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("Send to arrangement: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex)
                         + " could not be resolved");
        return nullptr;
    }

    auto* src = slot->getClip();

    if (src == nullptr)
    {
        FORGE_LOG_WARN ("Send to arrangement: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex)
                        + " is empty — nothing to send");
        return nullptr;
    }

    // Destination: the SAME track's LINEAR timeline (keeps the clip's instrument / mixer routing).
    auto tracks = te::getAudioTracks (*edit);

    if (trackIndex >= tracks.size())
    {
        FORGE_LOG_ERROR ("Send to arrangement: track " + juce::String (trackIndex) + " out of range");
        return nullptr;
    }

    auto* track = tracks[trackIndex];

    // Append point = end of THIS track's existing ARRANGE clips (0 for an empty lane). Slot clips live
    // in the ClipSlotList, not getClips(), so they never shift the append point. Preserve the source
    // clip's length + content offset so the copy is faithful; insertClipWithState stamps the position.
    const auto appendAt = track->getTotalRange().getEnd();
    const auto srcPos   = src->getPosition();
    const te::ClipPosition destPos { { appendAt, appendAt + srcPos.getLength() }, srcPos.getOffset() };

    // Faithful copy at the append point via the shared clone helper (Wave 7 factored it out of here so
    // performance capture can reuse the identical clone/normalize/strip path). keepAsLoop defaults false —
    // a sent clip is a plain linear one-shot (matches a direct Arrange import) — but the caller may ask to
    // keep the source's loop (the "Send to Arrangement (as loop)" fast-follow).
    auto* newClip = insertClipCopyOnTimeline (*track, *src, destPos, keepAsLoop);

    if (newClip == nullptr)
    {
        FORGE_LOG_ERROR ("Send to arrangement: failed to insert the copied clip onto track " + juce::String (trackIndex));
        return nullptr;
    }

    edit->markAsChanged();
    return newClip;
}

//==============================================================================
int ProjectSession::sendSceneToArrangement (int sceneIndex)
{
    if (edit == nullptr || sceneIndex < 0)
        return 0;

    auto tracks = te::getAudioTracks (*edit);

    // Pass 1: collect every (track, clip) with a FILLED slot in this scene, and the SHARED start = the
    // MAX current append point across only THOSE tracks (a track with nothing to send in this scene is
    // excluded from the max — nothing is being placed there, so its existing content can't dictate the
    // start). Clip pointers are resolved fresh right here and used immediately below in the SAME
    // function call — never cached across a tick/poll (R1 is about ACROSS-CALL caching; a same-call
    // local is the same discipline every other seam in this file already uses).
    struct Item { int trackIndex; te::Clip* src; };
    std::vector<Item> items;
    te::TimePosition sharedStart;   // default-constructed = 0

    for (int t = 0; t < tracks.size(); ++t)
    {
        auto* slot = getClipSlot (t, sceneIndex);
        auto* src  = (slot != nullptr) ? slot->getClip() : nullptr;

        if (src == nullptr)
            continue;

        items.push_back ({ t, src });

        const auto append = tracks[t]->getTotalRange().getEnd();
        if (append > sharedStart)
            sharedStart = append;
    }

    if (items.empty())
    {
        FORGE_LOG_WARN ("Send scene to arrangement: scene " + juce::String (sceneIndex) + " has no filled slots — nothing to send");
        return 0;
    }

    // Pass 2: insert every copy at the SAME sharedStart (preserving each clip's own length/offset), one
    // clone per filled track, all in ONE undo transaction (no beginNewTransaction between them — mirrors
    // performance capture's multi-clip commit; the caller brackets + seals the whole gesture).
    int sent = 0;

    for (const auto& item : items)
    {
        auto* track        = tracks[item.trackIndex];
        const auto srcPos   = item.src->getPosition();
        const te::ClipPosition destPos { { sharedStart, sharedStart + srcPos.getLength() }, srcPos.getOffset() };

        if (insertClipCopyOnTimeline (*track, *item.src, destPos, /*keepAsLoop*/ false) != nullptr)
            ++sent;
        else
            FORGE_LOG_ERROR ("Send scene to arrangement: failed to insert the copy for track " + juce::String (item.trackIndex));
    }

    if (sent > 0)
        edit->markAsChanged();

    return sent;
}

//==============================================================================
te::Clip* ProjectSession::insertClipCopyOnTimeline (te::AudioTrack& track, te::Clip& src,
                                                    te::ClipPosition destPos, bool keepAsLoop)
{
    // Faithful copy via the engine's own duplication idiom (mirrors te::split): clone the source state
    // (carries wave source / loop / gain OR the MIDI sequence), and the multi-arg insertClipWithState
    // re-IDs + repositions it. deleteExistingClips=false: append, don't replace. A createCopy() is
    // parentless, so there's no duplicate-ID / re-parent-live-tree assertion (engine gotcha).
    auto* newClip = track.insertClipWithState (src.state.createCopy(), src.getName(), src.type,
                                               destPos, /*deleteExistingClips*/ false,
                                               /*allowSpottingAdjustment*/ false);

    if (newClip == nullptr)
        return nullptr;

    // Normalize the copy to a plain linear one-shot (unless the caller wants a loop kept). A clip placed in
    // a ClipSlot is force-set by the engine to auto-tempo + a full-length loop range; state.createCopy()
    // carries that onto the timeline. Left as-is, the arrange clip would RE-TILE its content the instant the
    // user drags its right edge longer (a QC-confirmed latent bug), and an audio copy would time-warp on a
    // tempo change. disableLooping() clears the loop while PRESERVING the clip's visible region;
    // setAutoTempo(false) makes a sent audio one-shot behave like a direct import. (NB: setLoopRangeBeats({})
    // is deliberately NOT used — it re-asserts setAutoTempo(true), so it would undo the normalization.)
    if (! keepAsLoop)
    {
        if (auto* acb = dynamic_cast<te::AudioClipBase*> (newClip))
        {
            acb->disableLooping();
            acb->setAutoTempo (false);
        }
        else if (auto* mc = dynamic_cast<te::MidiClip*> (newClip))
        {
            mc->disableLooping();
        }
    }

    // Make the copy AUDIBLE in Arrange playback. A track latches playSlotClips=true when any of its slots
    // plays, and NOTHING in the engine's live path clears it — so the arranger output stays gated off
    // (playArranger = ! playSlotClips). Switch this track to arrange playback: the engine's defined
    // Session -> Arrange handoff (it stops any still-playing slot on THIS track, harmless when none is).
    track.playSlotClips = false;

    // Strip launcher-only metadata: an Arrange clip is never launched, so a copied follow action / launch
    // mode is dead data (the engine's node-builder reads them only for ClipSlotList clips, never the arrange
    // path). Removing it keeps the arrange clip clean and immunises a future move-not-copy from surfacing a
    // launcher-only concept on a clip that has no launcher (W1 QC — inert-but-drift-prone residue).
    newClip->state.removeChild (newClip->state.getChildWithName (te::IDs::FOLLOWACTIONS), &edit->getUndoManager());
    newClip->state.removeProperty (juce::Identifier ("forgeLaunchMode"), &edit->getUndoManager());

    return newClip;
}

//==============================================================================
// Performance capture (Wave 7). See the header for the sample-and-accumulate rationale.
void ProjectSession::startPerformanceCapture()
{
    if (edit == nullptr)
        return;

    capturedSpans.clear();
    openSpans.clear();
    capturing = true;
    startTimerHz (30);   // message-thread sampler; getPlayedRange is read-only + message-safe
    FORGE_LOG_INFO ("Performance capture armed");
}

void ProjectSession::timerCallback()
{
    performanceCaptureTick();
}

void ProjectSession::sealCaptureSpan (int track, int scene, const OpenSpan& open)
{
    // Guard a zero/negative-length span (a re-launch reseal caught on the same tick the span opened, or a
    // clip that queued but never advanced a block): getPlayedRange only returns a value once at least one
    // block has played, so a real span is > 0, but stay defensive.
    if (open.endBeat - open.startBeat <= 1.0e-4)
        return;

    capturedSpans.push_back ({ track, scene,
                               te::BeatRange (te::BeatPosition::fromBeats (open.startBeat),
                                              te::BeatPosition::fromBeats (open.endBeat)),
                               open.clipID });
}

void ProjectSession::performanceCaptureTick()
{
    if (! capturing || edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    const int numScenes = getNumScenes();

    for (int t = 0; t < tracks.size(); ++t)
        for (int s = 0; s < numScenes; ++s)
        {
            // R1: re-resolve the slot/clip/handle fresh every tick — never cache across ticks.
            auto* slot = getClipSlot (t, s);
            auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;
            auto  lh   = (clip != nullptr) ? clip->getLaunchHandle() : nullptr;

            const std::optional<te::BeatRange> r = lh ? lh->getPlayedRange() : std::nullopt;

            const auto key = std::make_pair (t, s);
            auto it = openSpans.find (key);

            if (r.has_value())
            {
                const double start = r->getStart().inBeats();
                const double end   = r->getEnd().inBeats();

                if (it == openSpans.end())
                {
                    // Fresh play: clip is guaranteed non-null here (r came from clip->getLaunchHandle()).
                    // Capture ITS identity now — never re-derived from "whatever's in the cell" later.
                    openSpans[key] = { start, end, clip->itemID };
                }
                else if (std::abs (it->second.startBeat - start) > 1.0e-4)
                {
                    // The launch startBeat jumped: a stop+relaunch happened between ticks (given Forge never
                    // calls LaunchHandle::nudge/setLooping/playSynced, a changed start always means a new
                    // play here — see the header note). Seal the OLD span with ITS OWN stored identity
                    // (it->second.clipID — NOT the freshly-resolved `clip`, which may already be a
                    // DIFFERENT clip if the slot's content changed inside this same tick gap), then open a
                    // fresh span for the new play under the newly-resolved clip's identity.
                    sealCaptureSpan (t, s, it->second);
                    it->second = { start, end, clip->itemID };
                }
                else
                {
                    it->second.endBeat = end;   // same play, still growing; identity unchanged
                }
            }
            else if (it != openSpans.end())
            {
                // Play -> stop transition: seal with the span's OWN stored identity (the clip may already
                // be nullptr this tick if it was deleted the instant it stopped) and close the span.
                sealCaptureSpan (t, s, it->second);
                openSpans.erase (it);
            }
        }
}

int ProjectSession::stopPerformanceCapture (bool commit)
{
    stopTimer();
    capturing = false;

    if (edit == nullptr)
    {
        openSpans.clear();
        capturedSpans.clear();
        return 0;
    }

    // Seal any span still open (a clip still playing when capture stopped).
    for (const auto& [key, open] : openSpans)
        sealCaptureSpan (key.first, key.second, open);
    openSpans.clear();

    if (! commit || capturedSpans.empty())
    {
        capturedSpans.clear();
        return 0;
    }

    // Stamp one one-shot clip per captured span at its ABSOLUTE captured Edit beat. Converts beats -> time
    // (ClipPosition is in seconds) via the tempo sequence at commit. NO beginNewTransaction here — the
    // caller brackets all inserts into ONE undo transaction (one Ctrl+Z removes the whole take).
    int stamped = 0;
    auto tracks = te::getAudioTracks (*edit);

    for (const auto& span : capturedSpans)
    {
        if (span.track < 0 || span.track >= tracks.size())
            continue;

        auto* track = tracks[span.track];

        // Resolve by IDENTITY, never by re-reading the cell: the cell may have been cleared and re-filled
        // with a DIFFERENT clip since this span was captured (a live jam commonly swaps a slot's clip
        // mid-performance) — resolving "whatever's in the cell now" would stamp the replacement clip's
        // content at THIS span's captured beat (QC-caught). findClipForID walks the whole edit, so it still
        // finds the original clip even if it has since moved to a different slot; only a genuine delete
        // fails to resolve.
        auto* src = te::findClipForID (*edit, span.clipID);

        if (track == nullptr || src == nullptr)
        {
            FORGE_LOG_WARN ("Performance capture: captured clip for cell (" + juce::String (span.track) + ","
                            + juce::String (span.scene) + ") no longer exists at commit — skipping its span");
            continue;
        }

        const auto startTime = edit->tempoSequence.toTime (span.range.getStart());
        const auto endTime   = edit->tempoSequence.toTime (span.range.getEnd());
        const te::ClipPosition destPos { { startTime, endTime }, src->getPosition().getOffset() };

        if (insertClipCopyOnTimeline (*track, *src, destPos, /*keepAsLoop*/ false) != nullptr)
            ++stamped;
    }

    capturedSpans.clear();

    if (stamped > 0)
        edit->markAsChanged();

    FORGE_LOG_INFO ("Performance capture committed " + juce::String (stamped) + " clip(s) to the arrangement");
    return stamped;
}

bool ProjectSession::isPerformanceCaptureArmed() const
{
    return capturing;
}

int ProjectSession::getCapturedSpanCount() const
{
    return (int) capturedSpans.size() + (int) openSpans.size();
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

    // Clone srcClip's state into targetSlot via the engine's own slot-paste idiom (W3). The ONLY engine
    // insert in the duplicate/move/copy path — the public seams compose it + getClipSlot + the grow calls.
    //  - createCopy() is PARENTLESS (satisfies te::insertClipWithState's !state.getParent() assert) — never
    //    pass the live src->state.
    //  - Re-ID BEFORE insert: the single-arg insertClipWithState does NOT mint a new itemID, and a raw copy
    //    carries the source itemID -> a duplicate ID in the same Edit. Stamp a fresh one (like te::split).
    //  - insertClipWithState(ClipOwner&, ValueTree) into a ClipSlot (a) auto-removes any existing target clip
    //    (baked replace-on-filled) and (b) RE-IMPOSES slot normalization. So NO manual normalization here
    //    (that is the Arrange-only sendSlotToArrangement concern) and NO launcher-metadata strip — the target
    //    IS a launcher slot, so a faithful duplicate keeps its FOLLOWACTIONS / forgeLaunchMode / launch-Q.
    te::Clip* cloneClipIntoSlot (te::Edit& e, te::Clip& srcClip, te::ClipSlot& targetSlot)
    {
        // Capture the source's one-shot state BEFORE the insert. The engine's ClipSlot normalization
        // (tracktion_ClipOwner.cpp:372-381) RE-IMPOSES a full-length loop on any freshly-inserted clip that
        // reads !isLooping() — so a duplicated ONE-SHOT would silently come back LOOPING (a W11 launcher-state
        // regression, QC-caught) unless we re-assert disableLooping() after the insert.
        const bool wasOneShot = ! srcClip.isLooping();

        auto newState = srcClip.state.createCopy();
        e.createNewItemID().writeID (newState, nullptr);   // fresh id BEFORE insert (single-arg does not re-ID)

        // Stop any live launch on a filled target so no LaunchHandle dangles across the engine's internal
        // removeFromParent (which, unlike clearSlot, does NOT stop the handle first).
        stopClipInSlot (&targetSlot, e);

        auto* clip = te::insertClipWithState (targetSlot, newState);   // ClipSlot IS-A ClipOwner

        // Re-assert the one-shot state the engine's slot-normalization just stripped. disableLooping() is the
        // correct inverse — NEVER setLoopRangeBeats({}), which re-asserts auto-tempo (the W5/W10 gotcha).
        if (clip != nullptr && wasOneShot)
            clip->disableLooping();

        return clip;
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

// ── Scene lifecycle (W15) ────────────────────────────────────────────────────────────────────
void ProjectSession::setSceneName (int index, const juce::String& newName)
{
    if (edit == nullptr)
    {
        FORGE_LOG_WARN ("setSceneName: no open edit");
        return;
    }

    auto scenes = edit->getSceneList().getScenes();

    if (index < 0 || index >= scenes.size() || scenes[index] == nullptr)
    {
        FORGE_LOG_WARN ("setSceneName: scene index " + juce::String (index) + " out of range");
        return;
    }

    // te::Scene::name is a CachedValue bound to the Edit UndoManager (tracktion_Scene.cpp:19-20),
    // so this write is undoable and dirties the tree. A blank name is intentional — the scene row
    // falls back to its 1-based number. No markAsChanged: the shell seals + saves via onEditMutated.
    scenes[index]->name = newName;
}

juce::String ProjectSession::getSceneName (int index) const
{
    if (edit == nullptr)
        return {};

    auto scenes = edit->getSceneList().getScenes();

    if (index < 0 || index >= scenes.size() || scenes[index] == nullptr)
        return {};

    return scenes[index]->name.get();
}

bool ProjectSession::deleteScene (int index)
{
    if (edit == nullptr)
    {
        FORGE_LOG_WARN ("deleteScene: no open edit");
        return false;
    }

    auto& sceneList = edit->getSceneList();
    auto scenes = sceneList.getScenes();

    if (index < 0 || index >= scenes.size() || scenes[index] == nullptr)
    {
        FORGE_LOG_WARN ("deleteScene: scene index " + juce::String (index) + " out of range");
        return false;
    }

    // Stop any live/queued launch in the deleted row FIRST (parity with clearSlot / the duplicate
    // path) so a clip in this row is cleanly silenced at the stop-quantise point rather than left
    // sounding until the async graph rebuild drops it (QC-2, W15). Not a UAF either way — the
    // LaunchHandle is shared_ptr-owned by the audio graph — but stop-first matches launch semantics.
    {
        auto tracks = te::getAudioTracks (*edit);
        for (int t = 0; t < tracks.size(); ++t)
            stopClipInSlot (getClipSlot (t, index), *edit);
    }

    // SceneList::deleteScene removes the SCENE row (with the UndoManager) AND every audio track's
    // slot at this index — clip->removeFromParent() + ClipSlotList::deleteSlot are BOTH UndoManager-
    // bound (tracktion_Scene.cpp:224-241 / Clip.cpp:397-401 / ClipSlot.cpp:179-183), so with the
    // shell's single per-gesture transaction one Ctrl-Z restores scene + slots + clips atomically.
    sceneList.deleteScene (*scenes[index]);
    return true;
}

bool ProjectSession::moveScene (int from, int to)
{
    if (edit == nullptr)
    {
        FORGE_LOG_WARN ("moveScene: no open edit");
        return false;
    }

    const int n = getNumScenes();

    if (from == to || from < 0 || from >= n || to < 0 || to >= n)
    {
        FORGE_LOG_WARN ("moveScene: bad indices " + juce::String (from) + " -> " + juce::String (to)
                        + " (count " + juce::String (n) + ")");
        return false;
    }

    // No engine moveScene seam exists. SceneList and ClipSlotList are both ValueTreeObjectLists whose
    // objectOrderChanged() is a no-op override (the base auto-resyncs its objects array on a raw child
    // move), and both expose a public `state` ValueTree. Move the SCENES tree AND every track's
    // CLIPSLOTS tree with the SAME from/to (the desync guard) on the Edit UndoManager, inside the
    // shell's transaction, so one Ctrl-Z reverts the whole reorder.
    auto* um = &edit->getUndoManager();

    // Lockstep REQUIRES every track's CLIPSLOTS tree to have the same child count as SCENES before the
    // move. A freshly appendAudioTrack'd track materialises slots only on demand (up to the touched row),
    // so it can hold a FILLED slot yet have fewer total slots than `to`. Skipping such a track (an earlier
    // guard did) would leave its clip behind while the scene moves — a silent scene<->clip desync that
    // PERSISTS to disk (QC-3, W15). Pad every track to the scene count FIRST (a no-op for already-full
    // tracks), in the SAME transaction so pad + move revert as one Ctrl-Z; then every slot tree moves in
    // lockstep with SCENES and no per-track skip is possible.
    for (auto* at : te::getAudioTracks (*edit))
        if (at != nullptr)
            at->getClipSlotList().ensureNumberOfSlots (n);

    edit->getSceneList().state.moveChild (from, to, um);

    for (auto* at : te::getAudioTracks (*edit))
        if (at != nullptr)
            at->getClipSlotList().state.moveChild (from, to, um);

    return true;
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

te::StepClip::Ptr ProjectSession::createStepClipInSlot (int trackIndex, int sceneIndex, const juce::String& name)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0)
        return {};

    // Ensure the track + grid row exist (mutating path — may grow tracks/scenes, R2). Mirrors
    // createMidiClipInSlot exactly up to the insert.
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex));
        return {};
    }

    ensureScenes (sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots (sceneIndex + 1);

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("Clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " could not be resolved after grid growth");
        return {};
    }

    // A fresh StepClip's default pattern is ONE BAR: numNotes = getBeatsPerBar()*4 steps x 0.25 beat =
    // getBeatsPerBar() beats — i.e. the time-sig numerator (16 steps / 4 beats in 4/4, 12 steps / 3 beats
    // in 3/4). Insert at EXACTLY that length, NOT the MIDI path's fixed 16 beats: the ClipOwner slot-insert
    // arm sets the launcher LOOP to the inserted clip length (tracktion_ClipOwner.cpp — the StepClip branch:
    // setLoopRangeBeats({0, getLengthInBeats()})), so a longer clip would loop with silent bars after the
    // pattern and a shorter one would truncate steps. Derive the length from the meter (getTimeSigAt
    // numerator) so it's correct in ANY time signature, not just 4/4 (QC-caught). Slot clips start at beat 0.
    const int  beatsPerBar = juce::jmax (1, edit->tempoSequence.getTimeSigAt (te::BeatPosition()).numerator.get());
    const auto startTime = edit->tempoSequence.toTime (te::BeatPosition());
    const auto endTime   = edit->tempoSequence.toTime (te::BeatPosition() + te::BeatDuration::fromBeats ((double) beatsPerBar));

    // No step-specific free inserter exists (unlike insertMIDIClip) — use the GENERIC free
    // insertNewClip(ClipOwner&, TrackItem::Type::step, name, range), which returns a raw Clip*. The
    // StepClip ctor auto-builds the 8-GM-drum-channel x 16-step grid; we build nothing. The slot
    // insert path (tracktion_ClipOwner.cpp) gives a StepClip a full-length loop = the desired launcher
    // drum behaviour (no one-shot dance, no auto-tempo trap — that's AudioClipBase-only).
    auto* rawClip = te::insertNewClip (*slot, te::TrackItem::Type::step,
                                       name, te::TimeRange (startTime, endTime));
    te::StepClip::Ptr clip = dynamic_cast<te::StepClip*> (rawClip);

    if (clip != nullptr)
    {
        PluginHost::ensureDrumKitInstrument (*track);   // born audible — self-rendered CC0 drum kit at chain head
        edit->markAsChanged();                          // user mutation -> persist (Sf)
    }
    else
    {
        FORGE_LOG_ERROR ("Failed to insert step clip into slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex));
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

//==============================================================================
te::MidiClip::Ptr ProjectSession::importMidiIntoSlot (int trackIndex, int sceneIndex, const juce::File& file)
{
    if (edit == nullptr || trackIndex < 0 || sceneIndex < 0 || ! file.existsAsFile())
        return {};

    // Mutating path: ensure the track + grid row exist (mirrors importAudioIntoSlot / createMidiClipInSlot).
    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex));
        return {};
    }

    ensureScenes (sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots (sceneIndex + 1);

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr)
    {
        FORGE_LOG_ERROR ("Clip slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " could not be resolved after grid growth");
        return {};
    }

    // ClipSlot IS a te::ClipOwner, so the engine's createClipFromFile drops straight into it. It reads the
    // .mid via the tempo-INDEPENDENT ticks->beats path (notes land on the right content-relative beats
    // regardless of the edit tempo), sizes the clip to the next whole bar, and the slot-insert normalization
    // gives it a full-length launcher loop. false = standard MIDI (no MPE / note-expression). Returns {} on
    // an unreadable / empty / note-less file (all covered by the null guard). Only the FIRST track/channel
    // is imported (documented v1 limit).
    te::MidiClip::Ptr clip = te::createClipFromFile (file, *slot, false);

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("Import MIDI into slot: no importable notes in " + file.getFullPathName()
                        + " (unreadable / empty / meta-only)");
        return {};
    }

    PluginHost::ensureDefaultInstrument (*track);   // createClipFromFile adds NO instrument — born audible needs one
    edit->markAsChanged();
    return clip;
}

te::MidiClip::Ptr ProjectSession::importMidiFile (const juce::File& file, te::TimePosition start, int trackIndex)
{
    if (edit == nullptr || trackIndex < 0 || ! file.existsAsFile())
        return {};

    auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackIndex);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("Failed to create or access track at index " + juce::String (trackIndex));
        return {};
    }

    // AudioTrack IS a te::ClipOwner: createClipFromFile lands a NON-looping one-shot at [0, len] (tempo-
    // independent beats); slide it to the drop position (notes are content-relative, so they move with the
    // clip). keepLength=true preserves the imported span.
    te::MidiClip::Ptr clip = te::createClipFromFile (file, *track, false);

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("Import MIDI file: no importable notes in " + file.getFullPathName()
                        + " (unreadable / empty / meta-only)");
        return {};
    }

    clip->setStart (start, /*preserveSync*/ false, /*keepLength*/ true);
    PluginHost::ensureDefaultInstrument (*track);
    edit->markAsChanged();
    return clip;
}

bool ProjectSession::clearSlot (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return false;

    auto* slot = getClipSlot (trackIndex, sceneIndex);

    if (slot == nullptr || slot->getClip() == nullptr)
        return false;

    // Stop a live/queued launch first so no LaunchHandle dangles onto the clip we are about to remove.
    stopClipInSlot (slot, *edit);

    // te::Clip::removeFromParent detaches via the Edit's UndoManager (the same one W05 global Undo
    // runs over), so the delete is undoable once the shell seals the transaction (onEditMutated). The
    // slot object itself survives — only its clip is removed, so getClip() reads null afterwards.
    if (auto c = slot->getClip())
        c->removeFromParent();

    edit->markAsChanged();
    return true;
}

bool ProjectSession::copySlotClip (int srcTrack, int srcScene, int dstTrack, int dstScene)
{
    if (edit == nullptr)
        return false;

    if (srcTrack == dstTrack && srcScene == dstScene)
        return false;   // self-copy is a no-op

    // Source must be filled — check BEFORE the grow so an empty source can't grow the grid.
    {
        auto* s0 = getClipSlot (srcTrack, srcScene);
        if (s0 == nullptr || s0->getClip() == nullptr)
        {
            FORGE_LOG_WARN ("copySlotClip: source slot " + juce::String (srcTrack) + "," + juce::String (srcScene) + " is empty");
            return false;
        }
    }

    // Materialise the destination slot on demand. The grow stays OFF the user undo stack (ensureScenes
    // self-inhibits + clears history) and runs BEFORE the undoable insert, so one Ctrl+Z removes only the
    // clip. Mirrors createMidiClipInSlot's grow pair.
    auto* dstTrk = EngineHelpers::getOrInsertAudioTrackAt (*edit, dstTrack);
    if (dstTrk == nullptr)
    {
        FORGE_LOG_ERROR ("copySlotClip: destination track " + juce::String (dstTrack) + " unresolved");
        return false;
    }

    ensureScenes (dstScene + 1);
    dstTrk->getClipSlotList().ensureNumberOfSlots (dstScene + 1);

    // Re-resolve BOTH slots AFTER the grow (a grow can rebuild slot objects — never hold a clip across it, R1).
    auto* dstSlot = getClipSlot (dstTrack, dstScene);
    auto* srcSlot = getClipSlot (srcTrack, srcScene);
    auto* srcClip = (srcSlot != nullptr) ? srcSlot->getClip() : nullptr;

    if (dstSlot == nullptr || srcClip == nullptr)
    {
        FORGE_LOG_ERROR ("copySlotClip: slot unresolved after grid growth (src or dst)");
        return false;
    }

    if (cloneClipIntoSlot (*edit, *srcClip, *dstSlot) == nullptr)
    {
        FORGE_LOG_ERROR ("copySlotClip: insert into destination slot "
                         + juce::String (dstTrack) + "," + juce::String (dstScene) + " failed");
        return false;
    }

    edit->markAsChanged();
    return true;
}

int ProjectSession::duplicateSlotClip (int trackIndex, int srcScene)
{
    if (edit == nullptr)
        return -1;

    auto* srcSlot = getClipSlot (trackIndex, srcScene);
    if (srcSlot == nullptr || srcSlot->getClip() == nullptr)
    {
        FORGE_LOG_WARN ("duplicateSlotClip: slot " + juce::String (trackIndex) + "," + juce::String (srcScene) + " has nothing to duplicate");
        return -1;
    }

    // Target = the first EMPTY slot below the source on the same track; else a freshly-grown new last row.
    int target = -1;
    for (int s = srcScene + 1; s < getNumScenes(); ++s)
        if (! isSlotFilled (trackIndex, s)) { target = s; break; }
    if (target < 0)
        target = getNumScenes();

    return copySlotClip (trackIndex, srcScene, trackIndex, target) ? target : -1;
}

bool ProjectSession::moveSlotClip (int srcTrack, int srcScene, int dstTrack, int dstScene)
{
    // Copy FIRST; clear the source only once the copy lands (a failed copy leaves the source intact). NO
    // beginNewTransaction between the halves, so one Ctrl+Z reverses the whole move — the shell seals the
    // transaction after the gesture via onEditMutated().
    if (! copySlotClip (srcTrack, srcScene, dstTrack, dstScene))
        return false;

    return clearSlot (srcTrack, srcScene);
}

te::AudioTrack* ProjectSession::appendAudioTrack (const juce::String& name)
{
    if (edit == nullptr)
    {
        FORGE_LOG_ERROR ("appendAudioTrack: no open edit");
        return nullptr;
    }

    // Append a NEW audio track at the END of the list (mirrors ensureAuxBus) — keeps every existing
    // absolute track index stable. Default plugins give it the vol/pan + level-meter tail the mixer reads.
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);

    if (track == nullptr)
    {
        FORGE_LOG_ERROR ("appendAudioTrack: insertNewAudioTrack failed");
        return nullptr;
    }

    if (name.isNotEmpty())
        track->setName (name);

    edit->markAsChanged();

    // A track was actually added → let the shell rebuild track-ref-caching views + persist.
    if (onTracksChanged)
        onTracksChanged();

    return track.get();
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
// Launcher expressiveness seam (Wave 1). Message-thread only. Each op resolves the clip via the const
// getClipSlot(...)->getClip() (never cached, R1). Follow actions are consumed by the engine at graph-build
// (EditNodeBuilder passes createFollowAction(clip) into the SlotControlNode) — Forge does ZERO per-tick work.

void ProjectSession::setFollowAction (int trackIndex, int sceneIndex, te::FollowAction action)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("setFollowAction: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    auto* fa = clip->getFollowActions();   // lazily creates the FOLLOWACTIONS child (mutation intended here)

    if (fa == nullptr)
    {
        FORGE_LOG_WARN ("setFollowAction: clip type has no follow actions");
        return;
    }

    // Keep EXACTLY one Action, then set its type EXPLICITLY. The explicit set is what defeats the engine
    // footgun: writing a follow-action DURATION on an empty action list auto-plants a currentGroupRoundRobin
    // action (Clip::valueTreePropertyChanged -> FollowActions::addAction defaulting the type).
    if (fa->getActions().empty())
        fa->addAction();

    while (fa->getActions().size() > 1)             // trim extras -> exactly one (weighted lists are v2)
        fa->removeAction (*fa->getActions().back());

    if (auto actions = fa->getActions(); ! actions.empty())
        actions.front()->action = action;

    edit->markAsChanged();
}

te::FollowAction ProjectSession::getFollowAction (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return te::FollowAction::none;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
        return te::FollowAction::none;

    // Non-mutating read: guard on the FOLLOWACTIONS child EXISTING before calling getFollowActions() (which
    // lazily creates it and would dirty the tree). No child -> no follow action set.
    if (! clip->state.getChildWithName (te::IDs::FOLLOWACTIONS).isValid())
        return te::FollowAction::none;

    if (auto* fa = clip->getFollowActions())
        if (auto actions = fa->getActions(); ! actions.empty())
            return actions.front()->action;

    return te::FollowAction::none;
}

void ProjectSession::setFollowActionDuration (int trackIndex, int sceneIndex,
                                              te::Clip::FollowActionDurationType type, double amount)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("setFollowActionDuration: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    // Ensure an Action exists BEFORE writing the duration so the duration write doesn't trip the auto-plant.
    // We do NOT set the action type here — the UI always pairs this with setFollowAction, whose explicit
    // type-set follows and wins.
    if (auto* fa = clip->getFollowActions())
        if (fa->getActions().empty())
            fa->addAction();

    clip->followActionDurationType = type;

    if (type == te::Clip::FollowActionDurationType::loops)
        clip->followActionNumLoops = amount;
    else
        clip->followActionBeats = te::BeatDuration::fromBeats (amount);

    edit->markAsChanged();
}

void ProjectSession::setSlotClipLooping (int trackIndex, int sceneIndex, bool shouldLoop)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("setSlotClipLooping: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    if (shouldLoop)
        // A REAL full-clip loop range (never setLoopRangeBeats({}) — an empty range re-asserts auto-tempo,
        // the W5/W10 gotcha). setLoopRangeBeats is a Clip virtual: MidiClip sets the loop; AudioClipBase
        // additionally sets auto-tempo(true), keeping an audio loop beat-locked.
        clip->setLoopRangeBeats ({ te::BeatPosition(), te::BeatPosition() + clip->getLengthInBeats() });
    else
        clip->disableLooping();

    edit->markAsChanged();
}

bool ProjectSession::isSlotClipLooping (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return false;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    return clip != nullptr && clip->isLooping();
}

void ProjectSession::setLaunchMode (int trackIndex, int sceneIndex, LaunchMode mode)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("setLaunchMode: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    // Forge-owned property on the clip's ValueTree — persists with the edit, undoable. Absence reads as
    // Trigger (Trigger == 0), so every pre-Wave-1 clip is Trigger and no existing gate changes.
    static const juce::Identifier forgeLaunchModeProp ("forgeLaunchMode");
    clip->state.setProperty (forgeLaunchModeProp, (int) mode, &edit->getUndoManager());
    edit->markAsChanged();
}

LaunchMode ProjectSession::getLaunchMode (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return LaunchMode::Trigger;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
        return LaunchMode::Trigger;

    static const juce::Identifier forgeLaunchModeProp ("forgeLaunchMode");
    return (LaunchMode) (int) clip->state.getProperty (forgeLaunchModeProp, (int) LaunchMode::Trigger);
}

bool ProjectSession::isSlotActive (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return false;

    if (auto* slot = getClipSlot (trackIndex, sceneIndex))
        if (auto* clip = slot->getClip())
            if (auto lh = clip->getLaunchHandle())
            {
                if (lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                    return true;

                if (auto queued = lh->getQueuedStatus())   // queued-to-play counts as active (Toggle pre-roll)
                    return *queued == te::LaunchHandle::QueueState::playQueued;
            }

    return false;
}

void ProjectSession::setClipLaunchQuantisation (int trackIndex, int sceneIndex, te::LaunchQType t)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("setClipLaunchQuantisation: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    // BOTH writes are load-bearing: setUsesGlobalLaunchQuatisation(false) makes the resolver prefer the
    // clip's OWN LaunchQuantisation (it gates on the flag first); the type write sets what that override
    // snaps to. The engine spells it "Quatisation" (a verbatim engine typo) and INVERTS it — false ENABLES
    // the override. getLaunchQuantisation() returns the clip's own object (non-null on Midi/Audio/Step
    // clips); the null-guard is belt-and-suspenders.
    clip->setUsesGlobalLaunchQuatisation (false);
    if (auto* lq = clip->getLaunchQuantisation())
        lq->type = t;
    edit->markAsChanged();
}

te::LaunchQType ProjectSession::getClipLaunchQuantisation (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return te::LaunchQType::bar;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    // Report the clip's own type only while it overrides; otherwise report the effective (global) type,
    // so a caller that just wants "what type" still gets a sensible value.
    if (clip == nullptr || clip->usesGlobalLaunchQuatisation())
        return getGlobalLaunchQuantisation();

    if (auto* lq = clip->getLaunchQuantisation())
        return lq->type.get();

    return getGlobalLaunchQuantisation();
}

void ProjectSession::clearClipLaunchQuantisation (int trackIndex, int sceneIndex)
{
    if (edit == nullptr)
        return;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
    {
        FORGE_LOG_WARN ("clearClipLaunchQuantisation: slot " + juce::String (trackIndex) + "," + juce::String (sceneIndex) + " has no clip");
        return;
    }

    clip->setUsesGlobalLaunchQuatisation (true);   // revert to global; the stored clip type is left intact
    edit->markAsChanged();
}

bool ProjectSession::clipInheritsGlobalLaunchQuantisation (int trackIndex, int sceneIndex) const
{
    if (edit == nullptr)
        return true;

    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
        return true;

    // Pure in-memory read of the useClipLaunchQuantisation CachedValue — no tree write, and deliberately
    // NOT getLaunchQuantisation() (which would needlessly build the clip's C++ LaunchQuantisation member).
    // Engine method "Quatisation" is a verbatim typo; the flag is inverted (true == inherits global).
    return clip->usesGlobalLaunchQuatisation();
}

te::LaunchQType ProjectSession::resolveEffectiveLaunchQType (int trackIndex, int sceneIndex) const
{
    // Delegates into the file-local getLaunchQuantisation(te::Clip&) resolver defined earlier in this TU —
    // the EXACT function launchSlot's launch path feeds — so --selftest-session proves override precedence
    // through the real code path, not a re-derived mirror.
    auto* slot = getClipSlot (trackIndex, sceneIndex);
    auto* clip = (slot != nullptr) ? slot->getClip() : nullptr;

    if (clip == nullptr)
        return getGlobalLaunchQuantisation();

    return getLaunchQuantisation (*clip).type.get();
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
