#include "engine/PluginScanner.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
/*  How a scan surfaces plugins to Forge
    ------------------------------------
    The engine owns exactly one KnownPluginList and one AudioPluginFormatManager, both reachable
    via te::PluginManager:

        engine.getPluginManager().pluginFormatManager   // formats: VST3 (+ AU on Apple), etc.
        engine.getPluginManager().knownPluginList        // the persisted set of scanned plugins

    pluginFormatManager.addDefaultFormats() is called in PluginManager::initialise(), so VST3
    (and AudioUnit on Apple) are already registered before we run — we don't add formats here.

    PluginHost::getScannedExternals() iterates knownPluginList.getTypes(), so anything a scan
    adds to that list immediately becomes insertable. Persistence is the engine's job: it
    registers as a ChangeListener on knownPluginList and writes the list to property storage on
    every change (tracktion_PluginManager.cpp). KnownPluginList::scanAndAddFile() ->addType()
    broadcasts that change, so we never touch the XML ourselves.

    The dialog path reuses juce::PluginListComponent exactly as the engine's reference does
    (examples/DemoRunner/DemoRunner.h), which is the least-code, most-robust route.            */
//==============================================================================

namespace PluginScanner
{
    //==============================================================================
    void scanForPlugins (te::Engine& engine,
                         std::function<void (float, const String&)> onProgress,
                         std::function<void (int)> onDone)
    {
        auto& pm                = engine.getPluginManager();
        auto& formatManager     = pm.pluginFormatManager;
        auto& knownList         = pm.knownPluginList;

        // The dead-man's-pedal file lets a re-scan skip plugins that crashed last time. We use
        // the engine's temp area, matching the dialog path and the engine's own examples.
        const auto deadMansPedal = engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal");

        const int typesBefore = knownList.getNumTypes();

        FORGE_LOG_DEBUG ("Plugin scan starting: " + juce::String (typesBefore) + " types already known");

        // Walk every registered format that actually needs a directory scan (VST3 does; AU is
        // found by the OS but still enumerates via searchPathsForPlugins with an empty path).
        for (auto* format : formatManager.getFormats())
        {
            if (format == nullptr || ! format->canScanForPlugins())
                continue;

            // Gather candidate files/identifiers from the format's default install locations.
            const auto searchPaths = format->getDefaultLocationsToSearch();
            const auto filesOrIds  = format->searchPathsForPlugins (searchPaths, true, false);

            const int total = filesOrIds.size();

            FORGE_LOG_DEBUG ("Scanning format '" + format->getName() + "': " + juce::String (total) + " candidate(s)");

            for (int i = 0; i < total; ++i)
            {
                const auto& fileOrId = filesOrIds[i];

                if (onProgress != nullptr)
                {
                    const float progress = total > 0 ? (float) i / (float) total : 1.0f;
                    onProgress (progress, format->getNameOfPluginFromIdentifier (fileOrId));
                }

                // dontRescanIfAlreadyInList = true: skip files already known and unchanged, so
                // repeat scans are cheap. New/changed plugins are added (and persisted) here.
                OwnedArray<PluginDescription> found;
                knownList.scanAndAddFile (fileOrId, true, found, *format);
            }
        }

        // Let any custom scanner release resources (the engine installs an out-of-process one).
        knownList.scanFinished();

        FORGE_LOG_DEBUG ("Plugin scan finished: " + juce::String (jmax (0, knownList.getNumTypes() - typesBefore))
                         + " new type(s) added");

        if (onProgress != nullptr)
            onProgress (1.0f, {});

        if (onDone != nullptr)
            onDone (jmax (0, knownList.getNumTypes() - typesBefore));
    }

    //==============================================================================
    void showScanDialog (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();

        // juce::PluginListComponent provides the whole scan UI (scan button, search-path
        // options, results table, remove/rescan) bound to the engine's format manager and
        // known list. Passing the engine's PropertiesFile lets it remember search paths, and
        // the temp dead-man's-pedal file lets re-scans skip previously-crashing plugins.
        // allowAsync = true so AUv3 / asynchronously-instantiated plugins are included.
        auto* list = new PluginListComponent (pm.pluginFormatManager,
                                              pm.knownPluginList,
                                              engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                              std::addressof (engine.getPropertyStorage().getPropertiesFile()),
                                              true);
        list->setSize (820, 560);

        DialogWindow::LaunchOptions o;
        o.dialogTitle                  = TRANS ("Scan for Plugins");
        o.dialogBackgroundColour       = Colour (ForgeLookAndFeel::shellBg);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = true;
        o.resizable                    = true;
        o.useBottomRightCornerResizer  = true;
        o.content.setOwned (list);   // DialogWindow takes ownership of the component

        // Async, non-blocking: returns immediately, the window manages its own lifetime.
        o.launchAsync();
    }
}
