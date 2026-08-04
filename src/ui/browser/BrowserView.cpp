#include "ui/browser/BrowserView.h"
#include "ui/ForgeLookAndFeel.h"
#include "core/Log.h"

using namespace juce;

namespace
{
    // Root the tree at the user's Music folder when it exists; otherwise the home folder.
    // (userMusicDirectory is where Forge's own exports default, so it's a sensible landing spot.)
    File defaultBrowserRoot()
    {
        auto music = File::getSpecialLocation (File::userMusicDirectory);
        if (music.isDirectory())
            return music;

        return File::getSpecialLocation (File::userHomeDirectory);
    }

    constexpr int kSectionHeaderH = 20;   // "INSTRUMENTS" / "FILES" band
    constexpr int kInstrumentRowH = 20;

    // Styles a small section-header label (the INSTRUMENTS / FILES bands).
    void styleSectionHeader (Label& l, const String& text)
    {
        l.setText (text, dontSendNotification);
        l.setJustificationType (Justification::centredLeft);
        l.setFont (FontOptions (11.0f, Font::bold));
        l.setColour (Label::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
        l.setColour (Label::textColourId,       Colour (ForgeLookAndFeel::textSec));
        l.setBorderSize ({ 0, 10, 0, 6 });
        l.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
BrowserView::BrowserView()
{
    // --- Header ("Browser") -----------------------------------------------------------------
    header.setText ("Browser", dontSendNotification);
    header.setJustificationType (Justification::centredLeft);
    header.setColour (Label::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
    header.setColour (Label::textColourId,       Colour (ForgeLookAndFeel::textSec));
    header.setBorderSize ({ 0, 10, 0, 6 });
    header.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (header);

    // --- Instruments (B6) -------------------------------------------------------------------
    // Forge's built-in CC0 voices, straight from PluginHost's catalogue. Double-click assigns to the
    // shell's current target track; the list itself knows nothing about tracks.
    styleSectionHeader (instrumentHeader, "INSTRUMENTS");
    addAndMakeVisible (instrumentHeader);

    instrumentList.setRowHeight (kInstrumentRowH);
    instrumentList.setColour (ListBox::backgroundColourId, Colour (ForgeLookAndFeel::panelBg));
    instrumentList.setColour (ListBox::outlineColourId,    Colour (ForgeLookAndFeel::hairline));
    instrumentList.setOutlineThickness (0);
    instrumentList.setTooltip ("Double-click to load onto the selected track");
    addAndMakeVisible (instrumentList);

    styleSectionHeader (filesHeader, "FILES");
    addAndMakeVisible (filesHeader);

    // --- File tree --------------------------------------------------------------------------
    tree.setColour (TreeView::backgroundColourId,             Colour (ForgeLookAndFeel::panelBg));
    tree.setColour (TreeView::linesColourId,                  Colour (ForgeLookAndFeel::hairline));
    tree.setColour (TreeView::selectedItemBackgroundColourId, Colour (ForgeLookAndFeel::accent).withAlpha (0.30f));
    tree.setColour (TreeView::dragAndDropIndicatorColourId,   Colour (ForgeLookAndFeel::accent));

    // The tree's text/highlight colours come from the DirectoryContentsDisplayComponent base.
    tree.setColour (DirectoryContentsDisplayComponent::textColourId,            Colour (ForgeLookAndFeel::textPrim));
    tree.setColour (DirectoryContentsDisplayComponent::highlightColourId,       Colour (ForgeLookAndFeel::accent).withAlpha (0.30f));
    tree.setColour (DirectoryContentsDisplayComponent::highlightedTextColourId, Colour (ForgeLookAndFeel::textPrim));

    tree.setItemHeight (22);
    tree.setIndentSize (14);
    tree.addListener (this);
    addAndMakeVisible (tree);

    // Start the background scan thread first, then point the list at the root so the initial
    // scan has a worker to run on (mirrors juce's ImagesDemo ordering).
    if (! scanThread.startThread (Thread::Priority::background))
        FORGE_LOG_ERROR ("Failed to start file-scanner thread — the browser tree will not populate");

    const auto browserRoot = defaultBrowserRoot();
    if (! browserRoot.isDirectory())
        FORGE_LOG_WARN ("Failed to open browser directory: " + browserRoot.getFullPathName());

    contents.setDirectory (browserRoot,
                           true,    // includeDirectories — let the user drill into sub-folders
                           true);   // includeFiles
}

BrowserView::~BrowserView()
{
    tree.removeListener (this);

    // Stop the list scanning before the thread is torn down, then stop the thread with a
    // generous timeout so it isn't forcibly killed mid-scan.
    contents.clear();
    scanThread.stopThread (2000);
}

//==============================================================================
void BrowserView::paint (Graphics& g)
{
    g.fillAll (Colour (ForgeLookAndFeel::panelBg));
}

void BrowserView::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (24));

    // INSTRUMENTS takes its natural height (one row per voice) but never more than a third of the
    // panel, so a short sidebar still leaves the file tree usable — the list scrolls when clipped.
    const int naturalH = instruments.size() * kInstrumentRowH;
    const int listH    = jlimit (kInstrumentRowH, jmax (kInstrumentRowH, r.getHeight() / 3), naturalH);

    instrumentHeader.setBounds (r.removeFromTop (kSectionHeaderH));
    instrumentList.setBounds   (r.removeFromTop (listH));
    filesHeader.setBounds      (r.removeFromTop (kSectionHeaderH));

    tree.setBounds (r);
}

//==============================================================================
void BrowserView::paintListBoxItem (int row, Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! isPositiveAndBelow (row, instruments.size()))
        return;

    if (rowIsSelected)
    {
        g.setColour (Colour (ForgeLookAndFeel::accent).withAlpha (0.30f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (Colour (ForgeLookAndFeel::textPrim));
    g.setFont (FontOptions (12.0f));
    g.drawText (instruments.getReference (row).name,
                10, 0, width - 14, height, Justification::centredLeft, true);
}

void BrowserView::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    activateInstrumentRow (row);
}

void BrowserView::returnKeyPressed (int lastRowSelected)
{
    // Keyboard parity with the double-click (the list is focusable, so Enter must do something).
    activateInstrumentRow (lastRowSelected);
}

bool BrowserView::activateInstrumentRow (int row)
{
    if (! isPositiveAndBelow (row, instruments.size()) || onInstrumentChosen == nullptr)
        return false;

    instrumentList.selectRow (row);
    onInstrumentChosen (instruments.getReference (row).preset);
    return true;
}

//==============================================================================
void BrowserView::fileDoubleClicked (const File& file)
{
    // Only fire for real, existing files (audio or MIDI — the shell's dispatch branches on the
    // extension). Directories double-click to expand/collapse in the tree itself; the
    // WildcardFileFilter already keeps non-importable files out of the list, but we still guard
    // existsAsFile() defensively.
    if (file.existsAsFile() && onImportFile != nullptr)
        onImportFile (file);
}
