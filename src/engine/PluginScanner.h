/*
    PluginScanner — external VST3 / AU plugin scanning for Forge.

    Surfaces externally-installed plugins to the rest of the app by populating the engine's
    KnownPluginList. PluginHost::getScannedExternals() already reads that list
    (engine.getPluginManager().knownPluginList.getTypes()), so once a scan adds a plugin here
    it automatically appears in PluginHost::getAvailablePluginNames() and the insert menu — no
    other wiring is needed on the host side.

    Two entry points:
      - showScanDialog(): hosts JUCE's stock juce::PluginListComponent in a DialogWindow. That
        component already provides a scan button, search-path options, a results table and the
        remove/rescan actions, all bound to the engine's pluginFormatManager + knownPluginList.
        This mirrors the engine's own reference recipe (examples/DemoRunner/DemoRunner.h:137).
      - scanForPlugins(): a headless programmatic scan over each format's default search paths,
        with progress/done callbacks, for callers that want to drive a scan without the dialog.

    Persistence is automatic: te::PluginManager registers itself as a ChangeListener on its
    knownPluginList and writes the list to engine.getPropertyStorage() on every change (see
    tracktion_PluginManager.cpp changeListenerCallback). Adding a type broadcasts a change, so
    scans survive restart with no explicit XML write from us.

    Message-thread only. In-process scanning is used for this pass — see the deferred
    out-of-process-validation risk in the feature notes.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace PluginScanner
{
    /** Runs a synchronous, in-process scan over every scannable format's default search paths
        (VST3 everywhere, AudioUnit on Apple), adding any plugins found to the engine's
        known-plugin list. The list is persisted by the engine automatically.

        @param onProgress  Optional. Called on the message thread as scanning advances with a
                           0..1 progress value and the display name of the file being scanned.
        @param onDone      Optional. Called once when the scan completes with the number of NEW
                           plugin types added to the known list.

        Message-thread only. Blocks the message thread for the duration of the scan, so prefer
        showScanDialog() for interactive use. */
    void scanForPlugins (te::Engine& engine,
                         std::function<void (float progress, const juce::String& current)> onProgress,
                         std::function<void (int numFound)> onDone);

    /** Opens a self-contained, non-blocking DialogWindow that hosts a juce::PluginListComponent
        bound to the engine's plugin format manager and known-plugin list. The user can scan,
        view, rescan and remove plugins; results are persisted by the engine automatically.

        Message-thread only. */
    void showScanDialog (te::Engine& engine);
}
