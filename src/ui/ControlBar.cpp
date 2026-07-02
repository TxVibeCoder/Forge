#include "ui/ControlBar.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

ControlBar::ControlBar()
{
    // The file-command buttons are gone (W04a): the menu bar owns those commands now; the bar
    // keeps only the performance surface — region toggles, transport, LCD, view switch. Their
    // std::function callbacks stay on this class for the menu model to copy.
    // browserBtn is a FolderIconButton, the rest are TextButtons — a braced-init-list can't deduce a
    // common element type across the two pointer types, so add each by reference.
    addAndMakeVisible (browserBtn);
    addAndMakeVisible (sessionBtn);
    addAndMakeVisible (arrangeBtn);
    addAndMakeVisible (mixBtn);
    addAndMakeVisible (drawerBtn);

    // The view-switch buttons must NOT retain keyboard focus: otherwise Enter (SessionView's "launch the
    // focused slot" key) would re-fire the focused button's click instead of reaching the grid (QC fix).
    // The browser icon toggle joins them for the same reason.
    browserBtn.setWantsKeyboardFocus (false);
    sessionBtn.setWantsKeyboardFocus (false);
    arrangeBtn.setWantsKeyboardFocus (false);
    mixBtn    .setWantsKeyboardFocus (false);

    browserBtn.setTooltip ("Browser (B)");

    addAndMakeVisible (transportBar);
    addAndMakeVisible (lcd);

    browserBtn.onClick = [this] { if (onToggleBrowser) onToggleBrowser(); };
    drawerBtn.onClick  = [this] { if (onToggleDrawer)  onToggleDrawer(); };

    sessionBtn.onClick = [this] { setViewMode (0); if (onViewMode) onViewMode (0); };
    arrangeBtn.onClick = [this] { setViewMode (1); if (onViewMode) onViewMode (1); };
    mixBtn.onClick     = [this] { setViewMode (2); if (onViewMode) onViewMode (2); };

    updateViewButtons();
}

void ControlBar::FolderIconButton::paintButton (Graphics& g, bool shouldDrawButtonAsHighlighted,
                                                bool /*shouldDrawButtonAsDown*/)
{
    // Colour: amber accent when the browser is open; secondary-text outline otherwise. A slight
    // brighten on hover so the icon reads as interactive without a filled button background.
    Colour c = open ? Colour (ForgeLookAndFeel::accent) : Colour (ForgeLookAndFeel::textSec);
    if (shouldDrawButtonAsHighlighted)
        c = c.brighter (0.3f);

    // Build the folder glyph inside a centred square, insetting from the button bounds so the
    // stroke never clips at the edges. A tab across the upper-left, then the body rectangle.
    auto area = getLocalBounds().toFloat().reduced (7.0f);
    const float side  = jmin (area.getWidth(), area.getHeight());
    const float x     = area.getCentreX() - side * 0.5f;
    const float y     = area.getCentreY() - side * 0.5f;

    const float tabH   = side * 0.22f;    // height of the raised tab
    const float tabW   = side * 0.45f;    // width of the tab across the top-left
    const float corner = side * 0.08f;

    Path folder;
    // Tab: a rounded-corner shelf sitting on top of the body's left half.
    folder.startNewSubPath (x, y + tabH);
    folder.lineTo (x, y + corner);
    folder.quadraticTo (x, y, x + corner, y);
    folder.lineTo (x + tabW - corner, y);
    folder.quadraticTo (x + tabW, y, x + tabW, y + tabH);

    // Body: full-width rounded rectangle from the tab shelf down.
    folder.addRoundedRectangle (x, y + tabH, side, side - tabH, corner);

    g.setColour (c);
    g.strokePath (folder, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void ControlBar::setEdit (te::Edit* e)
{
    transportBar.setEdit (e);
    lcd.setEdit (e);
}

void ControlBar::setBrowserOpen (bool isOpen)
{
    browserBtn.setOpen (isOpen);
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

    // Top-LEFT cluster, laid out from the left edge outward:
    //   [folder icon] [Session] [Arrange] [Mix] [Editor].
    // The folder toggle is a compact square; "Editor" (the drawer toggle) travels with the group.
    browserBtn.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (10);
    sessionBtn.setBounds (r.removeFromLeft (62));
    arrangeBtn.setBounds (r.removeFromLeft (62));
    mixBtn.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (10);
    drawerBtn.setBounds (r.removeFromLeft (62));
    r.removeFromLeft (12);

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
