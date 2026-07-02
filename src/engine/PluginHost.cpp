#include "engine/PluginHost.h"
#include "engine/dsp/InstrumentSamples.h"
#include "ui/plugins/PluginWindow.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
/*  How a plugin gets created in Tracktion
    --------------------------------------
    Every plugin — built-in or external — is created through the Edit's PluginCache:

        edit.getPluginCache().createNewPlugin (xmlType, pluginDescription)

    where `xmlType` is the plugin's ValueTree type name and `pluginDescription` carries the
    name / format / identifier. For the engine's BUILT-IN plugins:
      - xmlType is the plugin class's `xmlTypeName` (e.g. EqualiserPlugin::xmlTypeName),
      - the description's pluginFormatName must be PluginManager::builtInPluginFormatName,
      - fileOrIdentifier is the same xmlTypeName.
    PluginManager::createBuiltInPluginDescription<PluginClass>() fills all of that in for us.

    For EXTERNAL (scanned VST3/AU) plugins:
      - xmlType is ExternalPlugin::xmlTypeName,
      - the description is the one the engine already stored in knownPluginList.

    This mirrors examples/common/Components.cpp (createBuiltInItems / PluginTreeItem::create)
    and DemoRunner's PluginDemo, which is the reference implementation for this API.            */
//==============================================================================

namespace PluginHost
{
    //==============================================================================
    // One creatable plugin: the display name shown in the menu plus everything
    // createNewPlugin() needs to instantiate it.
    struct Creatable
    {
        String displayName;
        String xmlType;
        PluginDescription description;
    };

    // Builds a PluginDescription + xmlType for one built-in plugin class. Same shape as
    // PluginManager::createBuiltInPluginDescription<>(), kept explicit here so we don't depend
    // on template availability and so the displayName matches getName() at runtime.
    template <typename PluginClass>
    static Creatable makeBuiltIn (const juce::String& category = TRANS ("Effect"))
    {
        Creatable c;
        c.displayName = TRANS (PluginClass::getPluginName());
        c.xmlType     = PluginClass::xmlTypeName;

        c.description.name             = c.displayName;
        c.description.pluginFormatName = te::PluginManager::builtInPluginFormatName;
        c.description.category         = category;
        c.description.manufacturerName = "Tracktion Software Corporation";
        c.description.fileOrIdentifier = PluginClass::xmlTypeName;

        return c;
    }

    // The always-available built-in EFFECTS, in a sensible insert-menu order. We deliberately
    // omit routing/utility internals (patch bays, aux send/return, freeze point, text, MIDI
    // modifiers, volume/meter) and synths — this list is for track effect inserts. Each of
    // these is a real audio effect a user can add and hear immediately, no scanning required.
    static Array<Creatable> getBuiltInEffects()
    {
        Array<Creatable> list;
        list.add (makeBuiltIn<te::EqualiserPlugin>());     // "4-Band Equaliser"
        list.add (makeBuiltIn<te::CompressorPlugin>());    // "Compressor/Limiter"
        list.add (makeBuiltIn<te::ReverbPlugin>());        // "Reverb"
        list.add (makeBuiltIn<te::DelayPlugin>());         // "Delay"
        list.add (makeBuiltIn<te::ChorusPlugin>());        // "Chorus"
        list.add (makeBuiltIn<te::PhaserPlugin>());        // "Phaser"
        list.add (makeBuiltIn<te::PitchShiftPlugin>());    // "Pitch Shifter"
        list.add (makeBuiltIn<te::LowPassPlugin>());       // "Low/High-Pass Filter"
        return list;
    }

    // The always-available built-in INSTRUMENTS. Unlike effects these report category
    // "Instrument" and go at the HEAD of a track's chain as its sound source. Both are registered
    // engine built-ins inserted through the exact same createNewPlugin path.
    //   - 4OSC   : the default synth a fresh MIDI track gets so its clips are audible.
    //   - Sampler: plays a one-shot chromatically; used for the demo piano (a self-rendered CC0 sample).
    static Array<Creatable> getBuiltInInstruments()
    {
        Array<Creatable> list;
        list.add (makeBuiltIn<te::FourOscPlugin>  (TRANS ("Instrument")));   // "4OSC"
        list.add (makeBuiltIn<te::SamplerPlugin>  (TRANS ("Instrument")));   // "Sampler"
        return list;
    }

    // Scanned external plugins (VST3/AU) the engine already knows about. Empty if nothing has
    // been scanned, which is fine — the built-ins above keep the feature demonstrable.
    static Array<Creatable> getScannedExternals (te::Engine& engine)
    {
        Array<Creatable> list;

        for (const auto& desc : engine.getPluginManager().knownPluginList.getTypes())
        {
            // Skip instruments — this menu is for effect inserts on existing audio tracks.
            if (desc.isInstrument)
                continue;

            Creatable c;
            c.displayName = desc.name;
            c.xmlType     = te::ExternalPlugin::xmlTypeName;
            c.description = desc;
            list.add (c);
        }

        return list;
    }

    // The full creatable set: built-ins first, then externals. Names are de-duplicated so a
    // display name maps unambiguously back to exactly one Creatable.
    static Array<Creatable> getAllCreatable (te::Engine& engine)
    {
        Array<Creatable> all;
        StringArray seen;

        auto addUnique = [&] (const Array<Creatable>& src)
        {
            for (const auto& c : src)
            {
                if (c.displayName.isEmpty() || seen.contains (c.displayName))
                    continue;

                seen.add (c.displayName);
                all.add (c);
            }
        };

        addUnique (getBuiltInEffects());
        addUnique (getScannedExternals (engine));

        return all;
    }

    //==============================================================================
    juce::StringArray getAvailablePluginNames (te::Engine& engine)
    {
        StringArray names;

        for (const auto& c : getAllCreatable (engine))
            names.add (c.displayName);

        return names;
    }

    //==============================================================================
    te::Plugin::Ptr addPluginToTrack (te::AudioTrack& track, const juce::String& displayName)
    {
        auto& edit = track.edit;

        // Find the Creatable whose display name matches.
        const Creatable* match = nullptr;

        const auto creatable = getAllCreatable (edit.engine);
        for (const auto& c : creatable)
        {
            if (c.displayName == displayName)
            {
                match = &c;
                break;
            }
        }

        if (match == nullptr)
            return {};

        // Create the plugin instance through the Edit's cache (the canonical path).
        auto plugin = edit.getPluginCache().createNewPlugin (match->xmlType, match->description);

        if (plugin == nullptr)
        {
            FORGE_LOG_ERROR ("Failed to create plugin '" + match->displayName + "' — may be corrupted or unsupported");
            return {};
        }

        // A track's chain ends with the always-present [volume&pan, level-meter] tail. We want
        // the new effect at the END of the user's insert section, i.e. just BEFORE that tail,
        // so it processes audio ahead of the fader/meter. Insert at the volume plugin's index;
        // if for some reason there's no volume plugin, fall back to appending (-1).
        int insertIndex = -1;

        if (auto* volume = track.getVolumePlugin())
        {
            const auto idx = track.pluginList.indexOf (volume);
            if (idx >= 0)
                insertIndex = idx;
        }

        track.pluginList.insertPlugin (plugin, insertIndex, nullptr);

        return plugin;
    }

    //==============================================================================
    te::Plugin::Ptr addInstrumentToTrack (te::AudioTrack& track, const juce::String& displayName)
    {
        auto& edit = track.edit;

        // Find the built-in instrument whose display name matches.
        const Creatable* match = nullptr;

        const auto instruments = getBuiltInInstruments();
        for (const auto& c : instruments)
        {
            if (c.displayName == displayName)
            {
                match = &c;
                break;
            }
        }

        if (match == nullptr)
            return {};

        // Create the plugin instance through the Edit's cache (the canonical path).
        auto plugin = edit.getPluginCache().createNewPlugin (match->xmlType, match->description);

        if (plugin == nullptr)
        {
            FORGE_LOG_ERROR ("Failed to create instrument '" + match->displayName + "' — may be corrupted");
            return {};
        }

        // The instrument is the track's sound source, so it goes at the HEAD of the chain
        // (index 0) — effects, then the volume/meter tail, all follow it.
        track.pluginList.insertPlugin (plugin, 0, nullptr);

        return plugin;
    }

    //==============================================================================
    bool ensureDefaultInstrument (te::AudioTrack& track)
    {
        // If the chain already hosts a synth / MIDI-input plugin, there's nothing to do — and
        // we must NOT add another, or repeated clip (re)creation would stack synths.
        for (auto* p : track.pluginList)
            if (p != nullptr && (p->isSynth() || p->takesMidiInput()))
                return false;

        // No instrument yet: insert a default 4OSC at the head of the chain.
        if (addInstrumentToTrack (track, te::FourOscPlugin::getPluginName()) == nullptr)
            FORGE_LOG_ERROR ("Failed to ensure default instrument on track — MIDI clips may be inaudible");

        return true;
    }

    //==============================================================================
    // ---- applyInstrumentPreset + helpers ------------------------------------------------------
    //
    // The demo builder assigns a distinct voice per track. Kick/Bass are pure 4OSC parameter sets
    // (deterministic, headlessly provable by rendering + non-silence check); Piano is the engine
    // Sampler loaded with Forge's self-rendered CC0 one-shot, pitched from one sample.

    namespace
    {
        // Removes every existing head synth / MIDI-input instrument from the track so a fresh preset
        // never stacks on top of a prior one (e.g. the default 4OSC ensureDefaultInstrument inserted).
        void removeExistingInstruments (te::AudioTrack& track)
        {
            // Collect first — deleting mutates the pluginList we'd otherwise be iterating.
            juce::Array<te::Plugin*> toRemove;
            for (auto* p : track.pluginList)
                if (p != nullptr && (p->isSynth() || p->takesMidiInput()))
                    toRemove.add (p);

            for (auto* p : toRemove)
                if (p != nullptr)
                    removePlugin (*p);
        }

        // Sets a 4OSC AutomatableParameter (level, tune, ADSR, filter freq/res) by its RAW value.
        // The param is attached to its CachedValue, so setParameter is the notification-safe path.
        void setParam (te::AutomatableParameter::Ptr param, float value)
        {
            if (param != nullptr)
                param->setParameter (value, juce::dontSendNotification);
        }

        // Programs a 4OSC as a punchy KICK: a single sine oscillator, no sustain, a short amp decay,
        // and a fast filter-envelope sweep for the classic downward "thump". Pure synthesis.
        void programKick (te::FourOscPlugin& synth)
        {
            // Osc 1 = sine at full level; osc 2-4 silent.
            if (synth.oscParams.size() >= 1)
            {
                auto* o = synth.oscParams[0];
                o->waveShapeValue = (int) te::Oscillator::sine;   // CachedValue<int> (not automatable)
                setParam (o->level, 0.0f);                        // 0 dB (range -100..0)
                setParam (o->tune,  0.0f);
            }
            for (int i = 1; i < synth.oscParams.size(); ++i)
                setParam (synth.oscParams[i]->level, -100.0f);    // silence osc 2-4

            // Amp ADSR (seconds; sustain in %): instant attack, fast decay, no sustain -> percussive.
            setParam (synth.ampAttack,  0.001f);
            setParam (synth.ampDecay,   0.28f);
            setParam (synth.ampSustain, 0.0f);
            setParam (synth.ampRelease, 0.12f);

            // Low-pass filter with a fast decaying envelope for the pitch-drop "thump" character.
            synth.filterTypeValue = 1;                            // 1 = low-pass
            setParam (synth.filterFreq,      66.0f);              // note-scale (~370 Hz baseline)
            setParam (synth.filterResonance, 12.0f);              // %
            setParam (synth.filterAmount,    0.7f);               // env drives cutoff up on attack
            setParam (synth.filterAttack,    0.001f);
            setParam (synth.filterDecay,     0.16f);
            setParam (synth.filterSustain,   0.0f);
            setParam (synth.filterRelease,   0.10f);

            setParam (synth.masterLevel, 0.0f);
        }

        // Programs a 4OSC as a round synth BASS: a saw oscillator through a low-pass filter with a
        // moderate decay and some sustain. 4OSC's home turf.
        void programBass (te::FourOscPlugin& synth)
        {
            if (synth.oscParams.size() >= 1)
            {
                auto* o = synth.oscParams[0];
                o->waveShapeValue = (int) te::Oscillator::saw;    // rich harmonics for a filter to shape
                setParam (o->level, 0.0f);
                setParam (o->tune,  0.0f);
            }
            // Osc 2 = square one octave down for weight; osc 3-4 silent.
            if (synth.oscParams.size() >= 2)
            {
                auto* o = synth.oscParams[1];
                o->waveShapeValue = (int) te::Oscillator::square;
                setParam (o->level, -8.0f);
                setParam (o->tune,  -12.0f);                       // -1 octave (range +-36 st)
            }
            for (int i = 2; i < synth.oscParams.size(); ++i)
                setParam (synth.oscParams[i]->level, -100.0f);

            // Amp ADSR: quick attack, medium decay, sustained body, short release.
            setParam (synth.ampAttack,  0.005f);
            setParam (synth.ampDecay,   0.30f);
            setParam (synth.ampSustain, 60.0f);                   // %
            setParam (synth.ampRelease, 0.15f);

            // Low-pass to keep it round and dark, with a little envelope movement.
            synth.filterTypeValue = 1;                            // 1 = low-pass
            setParam (synth.filterFreq,      58.0f);              // note-scale (~230 Hz) -> dark
            setParam (synth.filterResonance, 18.0f);              // %
            setParam (synth.filterAmount,    0.35f);
            setParam (synth.filterAttack,    0.01f);
            setParam (synth.filterDecay,     0.35f);
            setParam (synth.filterSustain,   40.0f);              // %
            setParam (synth.filterRelease,   0.15f);

            setParam (synth.masterLevel, 0.0f);
        }
    } // namespace

    te::Plugin::Ptr applyInstrumentPreset (te::AudioTrack& track, InstrumentPreset preset)
    {
        // Clear any existing head synth so presets never stack.
        removeExistingInstruments (track);

        if (preset == InstrumentPreset::Piano)
        {
            // Render (or reuse) the self-rendered CC0 piano one-shot.
            const juce::File sample = InstrumentSamples::ensurePianoOneShot();
            if (! sample.existsAsFile())
            {
                FORGE_LOG_ERROR ("Piano preset: no piano one-shot available — instrument not inserted");
                return {};
            }

            // Insert the engine Sampler at the head via the same built-in path 4OSC uses.
            auto plugin = addInstrumentToTrack (track, te::SamplerPlugin::getPluginName());
            if (plugin == nullptr)
            {
                FORGE_LOG_ERROR ("Piano preset: failed to insert Sampler instrument");
                return {};
            }

            if (auto* sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get()))
            {
                // addSound resolves an absolute path via the Edit's filePathResolver (returns it as-is).
                // startTime 0, length 0 -> whole file. gainDb 0. Returns "" on success, else an error.
                const juce::String err = sampler->addSound (sample.getFullPathName(), "Piano",
                                                            0.0, 0.0, 0.0f);
                if (err.isNotEmpty())
                {
                    FORGE_LOG_ERROR ("Piano preset: Sampler addSound failed — " + err);
                    return plugin;   // the plugin is inserted; it just has no sound
                }

                // Map the single sound at the sample's root note across the full keyboard so it
                // pitches chromatically (Sampler resamples per note-on: ratio = f(note)/f(keyNote)).
                sampler->setSoundParams (0, InstrumentSamples::kRootNote, 0, 127);

                // CAVEAT: the audio loads on an AsyncUpdater — a headless render must pump the message
                // loop after this before rendering, or getNumSamples()==0 and the note is skipped.
            }
            else
            {
                FORGE_LOG_ERROR ("Piano preset: inserted instrument was not a SamplerPlugin");
            }

            return plugin;
        }

        // Kick / Bass: insert a 4OSC then program it.
        auto plugin = addInstrumentToTrack (track, te::FourOscPlugin::getPluginName());
        if (plugin == nullptr)
        {
            FORGE_LOG_ERROR ("Instrument preset: failed to insert 4OSC instrument");
            return {};
        }

        if (auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get()))
        {
            if (preset == InstrumentPreset::Kick)
                programKick (*synth);
            else
                programBass (*synth);
        }
        else
        {
            FORGE_LOG_ERROR ("Instrument preset: inserted instrument was not a FourOscPlugin");
        }

        return plugin;
    }

    //==============================================================================
    juce::Array<te::Plugin*> getTrackInserts (te::AudioTrack& track)
    {
        Array<te::Plugin*> inserts;

        auto* volume = track.getVolumePlugin();
        auto* meter  = track.getLevelMeterPlugin();

        for (auto* p : track.pluginList)
            if (p != nullptr && p != volume && p != meter)
                inserts.add (p);

        return inserts;
    }

    //==============================================================================
    juce::Array<LearnableParam> getAutomatableParameters (te::Plugin& plugin)
    {
        Array<LearnableParam> params;

        // Plugin is an AutomatableEditItem; getAutomatableParameters() returns the plugin's flat
        // parameter list (tracktion_AutomatableEditItem.cpp:32). getParameterName() is the
        // plugin-local name (tracktion_AutomatableParameter.h:49) — right for a per-plugin picker
        // (getFullName() would prepend "track / plugin /").
        for (auto* p : plugin.getAutomatableParameters())
            if (p != nullptr)
                params.add ({ p->getParameterName(), p });

        return params;
    }

    //==============================================================================
    void removePlugin (te::Plugin& plugin)
    {
        // Close any editor window we opened for it first so the window doesn't outlive the
        // plugin (it would auto-close via its listener anyway, but this is deterministic).
        PluginWindow::closeFor (plugin);

        // deleteFromParent() removes it from the chain AND hides automation / engine windows;
        // it's the right call for a full delete (vs removeFromParent which only detaches).
        plugin.deleteFromParent();
    }
}
