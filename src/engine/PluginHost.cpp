#include "engine/PluginHost.h"
#include "ui/plugins/PluginWindow.h"

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
    // "Instrument" and go at the HEAD of a track's chain as its sound source. Currently just
    // the 4OSC synth, which is what a fresh MIDI track gets so its clips are audible.
    static Array<Creatable> getBuiltInInstruments()
    {
        Array<Creatable> list;
        list.add (makeBuiltIn<te::FourOscPlugin> (TRANS ("Instrument")));   // "4OSC"
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
            return {};

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
            return {};

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
        addInstrumentToTrack (track, te::FourOscPlugin::getPluginName());
        return true;
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
