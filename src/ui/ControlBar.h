/*
    ControlBar — the single persistent top strip (Ableton-style): left = the Browser region
    toggle (a themed folder-icon toggle) alongside the Session|Arrange|Mix view switch + the
    Detail-drawer toggle; center/right = the embedded TransportBar plus the transport LCD
    (W04a — it supersedes the bar's old timecode readout).

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
    void setBrowserOpen (bool isOpen);    // shell calls this when browser visibility changes

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

    // Forge's first juce::Path vector icon: an icon-only folder toggle in place of the old
    // "Browser" text button. Outline in textSec; tinted with the amber accent when the browser
    // region is open. Draws itself in paintButton so the open/closed state repaints cleanly.
    class FolderIconButton : public juce::Button
    {
    public:
        FolderIconButton() : juce::Button ("Browser") {}

        void setOpen (bool shouldBeOpen)
        {
            if (open == shouldBeOpen)
                return;
            open = shouldBeOpen;
            repaint();
        }

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    private:
        bool open = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderIconButton)
    };

    FolderIconButton browserBtn;
    TransportBar transportBar;
    LcdDisplay lcd;
    juce::TextButton sessionBtn { "Session" }, arrangeBtn { "Arrange" }, mixBtn { "Mix" };
    juce::TextButton drawerBtn { "Editor" };

    int viewMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlBar)
};
