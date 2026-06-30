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

    /** The user-insertable plugins on this track in chain order, EXCLUDING the always-present
        built-ins (volume&pan, level meter) so the UI only shows real inserts. */
    juce::Array<te::Plugin*> getTrackInserts (te::AudioTrack& track);

    /** Removes a plugin from its parent and fully deletes it from the Edit (closing any
        editor window first via PluginWindow). */
    void removePlugin (te::Plugin&);
}
