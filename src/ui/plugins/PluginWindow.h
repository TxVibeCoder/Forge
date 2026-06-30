/*
    PluginWindow — a floating editor window for a single Tracktion plugin.

    The engine's built-in effects (EQ, reverb, delay, compressor, ...) do NOT ship a native
    editor component (Plugin::createEditor() returns nullptr for them), so this window has two
    modes:
      1. If the plugin provides its own editor (external VST3/AU via ExternalPlugin), that
         editor is hosted directly.
      2. Otherwise a GENERIC editor is built from the plugin's automatable parameters: one
         labelled slider per parameter, two-way bound to the engine so moving a slider changes
         the sound and external changes move the slider.

    Windows are owned internally in a static registry keyed by the plugin. They auto-close when
    their plugin is deleted (tracked via SelectableListener), so callers never dangle.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace PluginWindow
{
    /** Opens — or, if one is already open, brings to front — a floating editor window for this
        plugin. Windows are owned internally and auto-close when their plugin is deleted. Safe
        to call repeatedly. */
    void show (te::Plugin&);

    /** Closes the window for one specific plugin if it is open (no-op otherwise). Called by
        PluginHost::removePlugin before deleting a plugin. */
    void closeFor (te::Plugin&);

    /** Closes every open plugin window. Call on project swap / shutdown. */
    void closeAll();
}
