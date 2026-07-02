/*
    ForgeMenuModel — the model behind the traditional top menu bar (W04a): the exhaustive,
    discoverable index of the shell's global commands, with keyboard shortcuts displayed
    beside the items.

    A dumb model: it owns no project logic and makes no engine calls. Every command forwards
    intent through a nullable std::function in Callbacks (ControlBar's convention), and every
    invocation is null-guarded, so a partially wired model — e.g. the headless --selftest-menu
    gate — never crashes. Tick marks are read live from the query functions each time a menu
    is built (MenuBarComponent re-calls getMenuForIndex on every open), so no invalidation
    machinery is needed; an unset query simply reads as unticked.

    Shortcut labels are DISPLAY-ONLY strings (PopupMenu::Item::shortcutKeyDescription — the
    menu never catches or processes the key): the shell's keyPressed stays the single owner
    of key handling. If a binding changes there, update the command table in
    ForgeMenuModel.cpp — the single source of truth for id, display name, shortcut label,
    and menu placement; the --selftest-menu gate asserts known name/shortcut pairs so drift
    fails loudly.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

class ForgeMenuModel : public juce::MenuBarModel
{
public:
    // Explicit default ctor: the non-copyable macro below declares a (deleted) copy ctor, which
    // suppresses the implicit default one — without this line the model can't be constructed.
    ForgeMenuModel() = default;

    //==============================================================================
    // Top-level menus, in bar order; getMenuBarNames() returns one name per entry.
    enum MenuIndex { menuFile = 0, menuEdit, menuView, menuTransport, menuHelp, numMenus };

    // Command ids (PopupMenu item ids — must be non-zero: 0 means "dismissed without
    // selection"). One id per row of the command table in ForgeMenuModel.cpp.
    enum CmdID
    {
        cmdNewProject = 1, cmdOpenProject, cmdSave, cmdSaveAs,
        cmdImportAudio, cmdExportMixdown, cmdExportStems,
        cmdAudioSettings, cmdPluginManager,

        cmdUndo, cmdRedo,   // W05
        cmdMidiLearn,

        cmdViewSession, cmdViewArrange, cmdViewMix,
        cmdToggleBrowser, cmdToggleDrawer,
        cmdPopOutMixer, cmdPopOutPianoRoll,   // W04b tear-off panels

        cmdTogglePlay, cmdToggleRecord, cmdToggleLoop,
        cmdToggleMetronome, cmdToggleMidiClock,
        cmdCountInOff, cmdCountIn1Bar, cmdCountIn2Bars,   // Transport > Count-In radio group

        cmdAbout,

        cmdExit
    };

    //==============================================================================
    /*  Intent callbacks + truth queries, wired by the shell. All nullable: an unset command
        is a silent no-op and an unset query reads as unticked (view mode: nothing ticked),
        so the model stays safe partially wired — the --selftest-menu gate builds it bare. */
    struct Callbacks
    {
        // File
        std::function<void()> onNewProject, onOpenProject, onSave, onSaveAs,
                              onImportAudio, onExportMixdown, onExportStems,
                              onAudioSettings, onPluginManager;
        std::function<void()> onExit;

        // Edit
        std::function<void()> onUndo, onRedo;                       // W05
        std::function<bool()> queryCanUndo, queryCanRedo;           // W05: item enablement (unset = enabled)
        std::function<void()> onMidiLearn;

        // View
        std::function<void (int)> onViewMode;      // 0 = Session, 1 = Arrange, 2 = Mixer (ControlBar's convention)
        std::function<void()> onToggleBrowser, onToggleDrawer;
        std::function<void()> onPopOutMixer, onPopOutPianoRoll;   // W04b tear-offs (re-invoke = front the window)

        // Transport
        std::function<void()> onTogglePlay, onToggleRecord, onToggleLoop,
                              onToggleMetronome, onToggleMidiClock;
        std::function<void (int)> onCountInBars;   // count-in length in bars; 0 = off

        // Help
        std::function<void()> onAbout;

        // Truth queries for tick marks, read live on every menu open.
        std::function<bool()> queryMetronomeEnabled, queryMidiClockEnabled,
                              queryBrowserVisible, queryDrawerVisible;
        std::function<bool()> queryMixerPoppedOut, queryPianoRollPoppedOut;   // W04b: tick = torn off
        std::function<int()>  queryViewMode;       // same 0/1/2 convention as onViewMode
        std::function<int()>  queryCountInBars;    // bars, 0 = off (radio-ticks the Count-In submenu)
    };

    Callbacks callbacks;

    //==============================================================================
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeMenuModel)
};
