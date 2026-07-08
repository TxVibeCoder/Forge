/*
    PluginHost — engine-side effect hosting for an AudioTrack's plugin chain.

    Wraps the Tracktion plugin-creation recipe (PluginCache::createNewPlugin + a
    PluginDescription) so the rest of the app can insert effects by display name without
    touching ValueTrees or PluginDescriptions directly. Prioritises the engine's BUILT-IN
    plugins (4-band EQ, reverb, delay, chorus, phaser, compressor, etc.) which are always
    available with no scanning, then appends any externally-scanned plugins the engine
    already knows about.

    The track's volume&pan and level-meter plugins are always present and are NOT effects the
    user inserts, so getTrackInserts() filters them out — the UI only ever sees real inserts.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace PluginHost
{
    /** Display names of plugins that can be inserted, in menu order: the engine's built-in
        effects first, then any scanned external plugins. Suitable for a popup menu. The names
        returned here are exactly what addPluginToTrack() expects. */
    juce::StringArray getAvailablePluginNames (te::Engine&);

    /** Inserts a plugin (named from getAvailablePluginNames) at the END of the track's chain,
        i.e. after any existing inserts but before the always-present volume/meter tail.
        Returns the created plugin, or nullptr (chain unchanged) on failure. */
    te::Plugin::Ptr addPluginToTrack (te::AudioTrack& track, const juce::String& displayName);

    /** Creates the named built-in INSTRUMENT and inserts it at the HEAD (index 0) of the
        track's chain, so it is the sound source ahead of any effects and the volume/meter tail.
        The name is resolved against the built-in instruments (currently just "4OSC"). Returns
        the created plugin, or nullptr (chain unchanged) on failure. */
    te::Plugin::Ptr addInstrumentToTrack (te::AudioTrack& track, const juce::String& displayName);

    /** Ensures the track has an instrument at the head of its chain so a MIDI clip on it is
        audible. If the chain already hosts a synth / MIDI-input plugin this is a no-op and
        returns false; otherwise it inserts a default 4OSC at index 0 and returns true.
        Idempotent — safe to call every time clips are (re)created; never stacks synths. */
    bool ensureDefaultInstrument (te::AudioTrack& track);

    /** Ensures the track has a DRUM KIT at the head of its chain so a Step Clip on it sounds like a
        kit (8 GM drum voices) instead of 8 pitches of a synth. Same idempotent contract as
        ensureDefaultInstrument: if the chain already hosts a synth / MIDI-input plugin this is a
        no-op and returns false (never clobbers an existing melodic instrument, never stacks);
        otherwise it inserts a te::SamplerPlugin at index 0, loads it with the self-rendered CC0
        drum one-shots from InstrumentSamples::ensureDrumKit() — one sound per GM note (36 kick,
        38 snare, 42 closed hat, 46 open hat, 39 clap, 45 low tom, 50 high tom, 51 ride), each mapped
        to a single-note range so it plays at natural pitch — and returns true.

        NOTE (headless callers): the Sampler loads its audio asynchronously (on an AsyncUpdater), so a
        headless RENDER of a drum note must pump the message loop (e.g. dispatchPendingUpdates /
        MessageManager::runDispatchLoopUntil) AFTER this returns and before rendering, or the sound
        will have zero samples and be skipped on note-on. Structural inspection (getNumSounds /
        getKeyNote — read straight from the ValueTree) needs no pump. Message-thread only. */
    bool ensureDrumKitInstrument (te::AudioTrack& track);

    /** The demo's three self-contained instrument voices. Kick + Bass are programmed 4OSC presets
        (pure synthesis, deterministic, no asset). Piano is the engine Sampler loaded with Forge's
        self-rendered CC0 piano one-shot (see InstrumentSamples), pitched chromatically from one sample. */
    enum class InstrumentPreset { Kick, Bass, Piano };

    /** Replaces the track's HEAD instrument with the given preset's instrument and configures it:
          Kick / Bass -> a 4OSC with programmed parameters (osc waveShape/tune/level, amp+filter ADSR,
                         filter) giving a punchy kick / round synth bass.
          Piano       -> the engine Sampler (te::SamplerPlugin) loaded via InstrumentSamples::
                         ensurePianoOneShot(), mapped at InstrumentSamples::kRootNote so it plays
                         chromatically.
        Removes any existing head synth first so it never stacks. Returns the inserted instrument
        plugin, or nullptr on failure (logs). Message-thread only.

        NOTE (headless callers): the Sampler loads its audio asynchronously (on an AsyncUpdater), so a
        headless render of a piano note must pump the message loop (e.g. dispatchPendingUpdates /
        MessageManager::runDispatchLoopUntil) AFTER this returns and before rendering, or the sound will
        have zero samples and be skipped on note-on. Kick/Bass have no such requirement. */
    te::Plugin::Ptr applyInstrumentPreset (te::AudioTrack& track, InstrumentPreset preset);

    /** The user-insertable plugins on this track in chain order, EXCLUDING the always-present
        built-ins (volume&pan, level meter) so the UI only shows real inserts. */
    juce::Array<te::Plugin*> getTrackInserts (te::AudioTrack& track);

    /** One automatable parameter exposed for a MIDI-learn / mapping target picker: its display name
        (the plugin-local parameter name, e.g. "Frequency") plus the live parameter pointer the
        binding is made against. The pointer is owned by the plugin — valid only while the plugin
        lives; the caller must not outlive it (message-thread, transient use). */
    struct LearnableParam
    {
        juce::String name;
        te::AutomatableParameter* param = nullptr;
    };

    /** Lists a plugin's automatable parameters as (name, parameter*) pairs, in the plugin's own
        parameter order — the candidate targets a "Learn" UI offers for CC mapping. Read-only and
        additive: it does not touch the plugin or the chain. Excludes any null entries; includes
        both active and inactive parameters (the caller can filter on AutomatableParameter state if
        it wants to grey out inactive ones, mirroring Tracktion's own mappings menu). */
    juce::Array<LearnableParam> getAutomatableParameters (te::Plugin& plugin);

    /** Removes a plugin from its parent and fully deletes it from the Edit (closing any
        editor window first via PluginWindow). */
    void removePlugin (te::Plugin&);
}
