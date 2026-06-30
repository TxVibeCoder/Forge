#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

ControlBar::ControlBar()
{
    for (auto* b : { &browserBtn, &newBtn, &openBtn, &saveBtn, &saveAsBtn, &importBtn, &exportBtn,
                     &scanBtn, &audioBtn, &arrangeBtn, &mixBtn, &drawerBtn })
        addAndMakeVisible (b);

    addAndMakeVisible (transportBar);

    browserBtn.onClick = [this] { if (onToggleBrowser) onToggleBrowser(); };
    drawerBtn.onClick  = [this] { if (onToggleDrawer)  onToggleDrawer(); };

    newBtn.onClick     = [this] { if (onNew) onNew(); };
    openBtn.onClick    = [this] { if (onOpen) onOpen(); };
    saveBtn.onClick    = [this] { if (onSave) onSave(); };
    saveAsBtn.onClick  = [this] { if (onSaveAs) onSaveAs(); };
    importBtn.onClick  = [this] { if (onImport) onImport(); };
    exportBtn.onClick  = [this] { showExportMenu(); };
    scanBtn.onClick    = [this] { if (onScanPlugins) onScanPlugins(); };
    audioBtn.onClick   = [this] { if (onAudioSettings) onAudioSettings(); };

    arrangeBtn.onClick = [this] { setViewMode (0); if (onViewMode) onViewMode (0); };
    mixBtn.onClick     = [this] { setViewMode (1); if (onViewMode) onViewMode (1); };

    updateViewButtons();
}

void ControlBar::setEdit (te::Edit* e)
{
    transportBar.setEdit (e);
}

void ControlBar::setViewMode (int mode)
{
    viewMode = mode;
    updateViewButtons();
}

void ControlBar::updateViewButtons()
{
    arrangeBtn.setToggleState (viewMode == 0, dontSendNotification);
    mixBtn.setToggleState     (viewMode == 1, dontSendNotification);
}

void ControlBar::showExportMenu()
{
    PopupMenu m;
    m.addItem (1, "Export Mixdown (WAV)...");
    m.addItem (2, "Export Stems...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (exportBtn),
                     [this] (int r)
                     {
                         if      (r == 1) { if (onExport)      onExport(); }
                         else if (r == 2) { if (onExportStems) onExportStems(); }
                     });
}

void ControlBar::resized()
{
    auto r = getLocalBounds().reduced (6, 6);

    browserBtn.setBounds (r.removeFromLeft (66));
    r.removeFromLeft (8);
    for (auto* b : { &newBtn, &openBtn, &saveBtn, &saveAsBtn, &importBtn, &exportBtn, &scanBtn, &audioBtn })
    {
        b->setBounds (r.removeFromLeft (60));
        r.removeFromLeft (3);
    }

    // Right side, laid out from the right edge inward.
    drawerBtn.setBounds (r.removeFromRight (62));
    r.removeFromRight (10);
    mixBtn.setBounds (r.removeFromRight (52));
    arrangeBtn.setBounds (r.removeFromRight (66));
    r.removeFromRight (12);

    transportBar.setBounds (r);   // center fills the remainder
}

void ControlBar::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::panelBg));
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (getLocalBounds().removeFromBottom (1));
}
