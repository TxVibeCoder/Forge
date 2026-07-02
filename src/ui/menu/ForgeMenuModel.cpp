#include "ui/menu/ForgeMenuModel.h"
#include "core/Log.h"

using namespace juce;

//==============================================================================
/*  The command table — the single source of truth for the menu bar. Each row carries a
    command's id, its top-level menu, the display name, the display-only shortcut label,
    its structure (separator above it / membership of the Transport > Count-In submenu),
    and how to dispatch and tick it: pointers-to-member into Callbacks, so the table stays
    a file-scope constant while the shell wires the actual functions at runtime.

    getMenuForIndex() builds each menu from this table in row order; menuItemSelected()
    dispatches from it. Adding a command = one CmdID + one row here (+ the shell wiring).
    The shortcut column must be kept in step with MainComponent::keyPressed, which remains
    the single owner of real key handling — labels here are never caught or processed.    */
namespace
{
    using M  = ForgeMenuModel;             // brevity in the table below
    using CB = ForgeMenuModel::Callbacks;

    using VoidFn = std::function<void()>;
    using IntFn  = std::function<void (int)>;
    using BoolFn = std::function<bool()>;
    using IntQFn = std::function<int()>;

    struct Command
    {
        int         id;                            // ForgeMenuModel::CmdID (never 0)
        int         menu;                          // ForgeMenuModel::MenuIndex
        const char* name;                          // display text
        const char* shortcut         = "";         // display-only right-edge label ("" = none)
        bool        separatorBefore  = false;      // insert a separator above this row
        bool        inCountInSubmenu = false;      // row lives in Transport > Count-In
        VoidFn CB::* action          = nullptr;    // simple command...
        IntFn  CB::* intAction       = nullptr;    // ...or parameterised command, passed intArg
        int         intArg           = 0;
        BoolFn CB::* tick            = nullptr;    // ticked when query() is true...
        IntQFn CB::* radioTick       = nullptr;    // ...or when query() == intArg (radio groups)
        BoolFn CB::* enabledWhen     = nullptr;    // greyed out when query() is false (unset = enabled)
    };

    const Command commandTable[] =
    {
        // File
        { .id = M::cmdNewProject,     .menu = M::menuFile,      .name = "New",              .shortcut = "Ctrl+N",       .action = &CB::onNewProject },
        { .id = M::cmdOpenProject,    .menu = M::menuFile,      .name = "Open...",          .shortcut = "Ctrl+O",       .action = &CB::onOpenProject },
        { .id = M::cmdSave,           .menu = M::menuFile,      .name = "Save",             .shortcut = "Ctrl+S",       .action = &CB::onSave },
        { .id = M::cmdSaveAs,         .menu = M::menuFile,      .name = "Save As...",       .shortcut = "Ctrl+Shift+S", .action = &CB::onSaveAs },
        { .id = M::cmdImportAudio,    .menu = M::menuFile,      .name = "Import Audio...",  .shortcut = "Ctrl+I",       .separatorBefore = true, .action = &CB::onImportAudio },
        { .id = M::cmdExportMixdown,  .menu = M::menuFile,      .name = "Export WAV...",                                .action = &CB::onExportMixdown },
        { .id = M::cmdExportStems,    .menu = M::menuFile,      .name = "Export Stems...",                              .action = &CB::onExportStems },
        { .id = M::cmdAudioSettings,  .menu = M::menuFile,      .name = "Audio Settings...",                            .separatorBefore = true, .action = &CB::onAudioSettings },
        { .id = M::cmdPluginManager,  .menu = M::menuFile,      .name = "Plugin Manager...",                            .action = &CB::onPluginManager },

        // Edit (W05: global Undo/Redo over the Edit's UndoManager; note-level copy/paste stays view-local)
        { .id = M::cmdUndo,           .menu = M::menuEdit,      .name = "Undo",             .shortcut = "Ctrl+Z",       .action = &CB::onUndo, .enabledWhen = &CB::queryCanUndo },
        { .id = M::cmdRedo,           .menu = M::menuEdit,      .name = "Redo",             .shortcut = "Ctrl+Shift+Z", .action = &CB::onRedo, .enabledWhen = &CB::queryCanRedo },
        { .id = M::cmdMidiLearn,      .menu = M::menuEdit,      .name = "MIDI Learn...",    .shortcut = "Ctrl+L",       .separatorBefore = true, .action = &CB::onMidiLearn },

        // View (the mode trio radio-ticks against queryViewMode; the region toggles tick their visibility)
        { .id = M::cmdViewSession,    .menu = M::menuView,      .name = "Session",          .shortcut = "F8",           .intAction = &CB::onViewMode, .intArg = 0, .radioTick = &CB::queryViewMode },
        { .id = M::cmdViewArrange,    .menu = M::menuView,      .name = "Arrange",          .shortcut = "F9",           .intAction = &CB::onViewMode, .intArg = 1, .radioTick = &CB::queryViewMode },
        { .id = M::cmdViewMix,        .menu = M::menuView,      .name = "Mix",              .shortcut = "F11",          .intAction = &CB::onViewMode, .intArg = 2, .radioTick = &CB::queryViewMode },
        { .id = M::cmdToggleBrowser,  .menu = M::menuView,      .name = "Browser Sidebar",  .shortcut = "B",            .separatorBefore = true, .action = &CB::onToggleBrowser, .tick = &CB::queryBrowserVisible },
        { .id = M::cmdToggleDrawer,   .menu = M::menuView,      .name = "Editor Drawer",    .shortcut = "E",            .action = &CB::onToggleDrawer,  .tick = &CB::queryDrawerVisible },
        { .id = M::cmdPopOutMixer,    .menu = M::menuView,      .name = "Pop Out Mixer",                                .separatorBefore = true, .action = &CB::onPopOutMixer,     .tick = &CB::queryMixerPoppedOut },
        { .id = M::cmdPopOutPianoRoll, .menu = M::menuView,     .name = "Pop Out Piano Roll",                           .action = &CB::onPopOutPianoRoll, .tick = &CB::queryPianoRollPoppedOut },

        // Transport (Count-In options mirror TransportBar's selector: 0/1/2 bars — the engine's
        // native count-in tops out at two bars, so never offer more than it honours)
        { .id = M::cmdTogglePlay,     .menu = M::menuTransport, .name = "Play/Stop",        .shortcut = "Space",        .action = &CB::onTogglePlay },
        { .id = M::cmdToggleRecord,   .menu = M::menuTransport, .name = "Record",           .shortcut = "R",            .action = &CB::onToggleRecord },
        { .id = M::cmdToggleLoop,     .menu = M::menuTransport, .name = "Loop",                                         .action = &CB::onToggleLoop },
        { .id = M::cmdToggleMetronome, .menu = M::menuTransport, .name = "Metronome Click",                             .separatorBefore = true, .action = &CB::onToggleMetronome, .tick = &CB::queryMetronomeEnabled },
        { .id = M::cmdCountInOff,     .menu = M::menuTransport, .name = "No count-in",                                  .inCountInSubmenu = true, .intAction = &CB::onCountInBars, .intArg = 0, .radioTick = &CB::queryCountInBars },
        { .id = M::cmdCountIn1Bar,    .menu = M::menuTransport, .name = "1 bar",                                        .inCountInSubmenu = true, .intAction = &CB::onCountInBars, .intArg = 1, .radioTick = &CB::queryCountInBars },
        { .id = M::cmdCountIn2Bars,   .menu = M::menuTransport, .name = "2 bars",                                       .inCountInSubmenu = true, .intAction = &CB::onCountInBars, .intArg = 2, .radioTick = &CB::queryCountInBars },
        { .id = M::cmdToggleMidiClock, .menu = M::menuTransport, .name = "MIDI Clock Out",                              .action = &CB::onToggleMidiClock, .tick = &CB::queryMidiClockEnabled },

        // Help
        { .id = M::cmdAbout,          .menu = M::menuHelp,      .name = "About Forge",                                  .action = &CB::onAbout },
    };

    // Builds the PopupMenu::Item for a table row. Ticks are resolved HERE, live, against the
    // query functions — getMenuForIndex is re-called on every menu open, so this is the whole
    // freshness story. An unset query reads as unticked (never a crash).
    PopupMenu::Item makeItem (const Command& row, const CB& cb)
    {
        PopupMenu::Item item (String (row.name));
        item.itemID = row.id;
        item.shortcutKeyDescription = row.shortcut;   // display-only; the key itself is keyPressed's job

        if (row.tick != nullptr)
        {
            if (const auto& query = cb.*(row.tick); query)
                item.isTicked = query();
        }
        else if (row.radioTick != nullptr)
        {
            if (const auto& query = cb.*(row.radioTick); query)
                item.isTicked = (query() == row.intArg);
        }

        // Enablement (W05): a wired query gates the item live on every menu open; an UNSET query
        // leaves the item enabled (the null-safe convention every other column follows).
        if (row.enabledWhen != nullptr)
            if (const auto& query = cb.*(row.enabledWhen); query)
                item.isEnabled = query();

        return item;
    }
}

//==============================================================================
StringArray ForgeMenuModel::getMenuBarNames()
{
    return { "File", "Edit", "View", "Transport", "Help" };
}

PopupMenu ForgeMenuModel::getMenuForIndex (int topLevelMenuIndex, const String&)
{
    PopupMenu menu;

    for (int i = 0; i < numElementsInArray (commandTable); ++i)
    {
        const auto& row = commandTable[i];

        if (row.menu != topLevelMenuIndex)
            continue;

        if (row.separatorBefore)
            menu.addSeparator();

        // Consecutive Count-In rows collapse into one submenu, anchored where the first of
        // them sits (table order == menu order, so the anchor lands between its neighbours).
        if (row.inCountInSubmenu)
        {
            PopupMenu sub;

            for (; i < numElementsInArray (commandTable)
                     && commandTable[i].menu == topLevelMenuIndex
                     && commandTable[i].inCountInSubmenu; ++i)
                sub.addItem (makeItem (commandTable[i], callbacks));

            --i;   // the outer loop's ++i would otherwise skip the row after the submenu
            menu.addSubMenu ("Count-In", std::move (sub));
            continue;
        }

        menu.addItem (makeItem (row, callbacks));
    }

    return menu;
}

void ForgeMenuModel::menuItemSelected (int menuItemID, int)
{
    for (const auto& row : commandTable)
    {
        if (row.id != menuItemID)
            continue;

        // Null-guarded dispatch (ControlBar's convention): a command the shell hasn't wired
        // yet is a silent no-op, so a partially wired model can never crash.
        if (row.action != nullptr)
        {
            if (const auto& fn = callbacks.*(row.action); fn)
                fn();
        }
        else if (row.intAction != nullptr)
        {
            if (const auto& fn = callbacks.*(row.intAction); fn)
                fn (row.intArg);
        }

        return;
    }

    // Reachable only if an id escapes the table (enum/row drift) — the bar itself can't emit an
    // unknown id, so leave a diagnostic. 0 means dismissed-without-selection: MenuBarComponent
    // never delivers it, but guard for direct callers (e.g. the selftest gate).
    if (menuItemID != 0)
        FORGE_LOG_WARN ("Menu command id not in the command table: " + String (menuItemID));
}
