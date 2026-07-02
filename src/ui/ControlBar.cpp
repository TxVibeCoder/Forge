#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

ControlBar::ControlBar()
{
    // The file-command buttons are gone (W04a): the menu bar owns those commands now; the bar
    // keeps only the performance surface — region toggles, transport, LCD, view switch. Their
    // std::function callbacks stay on this class for the menu model to copy.
    for (auto* b : { &browserBtn, &sessionBtn, &arrangeBtn, &mixBtn, &drawerBtn })
        addAndMakeVisible (b);

    // The view-switch buttons must NOT retain keyboard focus: otherwise Enter (SessionView's "launch the
    // focused slot" key) would re-fire the focused button's click instead of reaching the grid (QC fix).
    sessionBtn.setWantsKeyboardFocus (false);
    arrangeBtn.setWantsKeyboardFocus (false);
    mixBtn    .setWantsKeyboardFocus (false);

    addAndMakeVisible (transportBar);
    addAndMakeVisible (lcd);

    browserBtn.onClick = [this] { if (onToggleBrowser) onToggleBrowser(); };
    drawerBtn.onClick  = [this] { if (onToggleDrawer)  onToggleDrawer(); };

    sessionBtn.onClick = [this] { setViewMode (0); if (onViewMode) onViewMode (0); };
    arrangeBtn.onClick = [this] { setViewMode (1); if (onViewMode) onViewMode (1); };
    mixBtn.onClick     = [this] { setViewMode (2); if (onViewMode) onViewMode (2); };

    updateViewButtons();
}

void ControlBar::setEdit (te::Edit* e)
{
    transportBar.setEdit (e);
    lcd.setEdit (e);
}

void ControlBar::setViewMode (int mode)
{
    viewMode = mode;
    updateViewButtons();
}

void ControlBar::updateViewButtons()
{
    sessionBtn.setToggleState (viewMode == 0, dontSendNotification);
    arrangeBtn.setToggleState (viewMode == 1, dontSendNotification);
    mixBtn.setToggleState     (viewMode == 2, dontSendNotification);
}

void ControlBar::resized()
{
    auto r = getLocalBounds().reduced (6, 6);

    browserBtn.setBounds (r.removeFromLeft (66));
    r.removeFromLeft (8);

    // Right side, laid out from the right edge inward: Editor | Mix | Arrange | Session.
    drawerBtn.setBounds (r.removeFromRight (62));
    r.removeFromRight (10);
    mixBtn.setBounds (r.removeFromRight (48));
    arrangeBtn.setBounds (r.removeFromRight (62));
    sessionBtn.setBounds (r.removeFromRight (62));
    r.removeFromRight (12);

    // Centre priority (QC blocker fix): the transport's fixed controls get FIRST claim on the
    // span — every button must stay clickable at any window width the shell allows. The LCD
    // takes only the leftover, and hides entirely when that leftover is below its 150 px floor
    // rather than starving the transport (it re-appears as soon as the window grows; with the
    // file buttons gone it fits from the default width up).
    const int transportW = jmin (TransportBar::preferredWidth, r.getWidth());
    transportBar.setBounds (r.removeFromLeft (transportW));
    r.removeFromLeft (6);

    const bool lcdFits = r.getWidth() >= LcdDisplay::minimumWidth;
    lcd.setVisible (lcdFits);
    if (lcdFits)
        lcd.setBounds (r.withSizeKeepingCentre (jmin (LcdDisplay::preferredWidth, r.getWidth()),
                                                r.getHeight())
                           .reduced (0, 4));
}

void ControlBar::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::panelBg));
    g.setColour (Colour (ForgeLookAndFeel::hairline));
    g.fillRect (getLocalBounds().removeFromBottom (1));
}
