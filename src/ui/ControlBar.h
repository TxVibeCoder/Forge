/*
    ControlBar — the single persistent top strip (Ableton-style): left = the Browser region
    toggle; center = the embedded TransportBar plus the transport LCD (W04a — it supersedes
    the bar's old timecode readout); right = the Session|Arrange|Mix view switch + the
    Detail-drawer toggle.

    The file-command buttons (New/Open/Save/… /Plugins/Audio) moved to the W04a menu bar —
    the charter's division of labor: the menu is the discoverable command index, the bar
    keeps only the PERFORMANCE controls. Their std::function callbacks stay here (the shell
    wires them once; ForgeMenuModel copies them), so the commands themselves are unchanged.
    Removing ~500 px of buttons is also what lets the transport strip and the LCD both fit
    at the default window width (QC blocker: the LCD's reserved floor was starving the
    transport buttons).

    A dumb view: it owns no project logic, only forwards intent via std::function callbacks.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/transport/LcdDisplay.h"
#include "ui/transport/TransportBar.h"

namespace te = tracktion;

class ControlBar : public juce::Component
{
public:
    ControlBar();

    void setEdit (te::Edit*);
    void setViewMode (int mode);          // 0 = Session, 1 = Arrange, 2 = Mixer

    void resized() override;
    void paint (juce::Graphics&) override;

    // Intent callbacks, wired by the shell.
    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onExport, onExportStems,
                          onScanPlugins, onAudioSettings;
    std::function<void()> onToggleBrowser, onToggleDrawer;
    std::function<void (int)> onViewMode;

    TransportBar&       getTransportBar()       { return transportBar; }
    const TransportBar& getTransportBar() const { return transportBar; }

    LcdDisplay&       getLcdDisplay()       { return lcd; }
    const LcdDisplay& getLcdDisplay() const { return lcd; }

private:
    void updateViewButtons();

    juce::TextButton browserBtn { "Browser" };
    TransportBar transportBar;
    LcdDisplay lcd;
    juce::TextButton sessionBtn { "Session" }, arrangeBtn { "Arrange" }, mixBtn { "Mix" };
    juce::TextButton drawerBtn { "Editor" };

    int viewMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlBar)
};
