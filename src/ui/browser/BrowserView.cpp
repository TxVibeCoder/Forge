#include "ui/browser/BrowserView.h"
#include "ui/ForgeLookAndFeel.h"

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
    scanThread.startThread (Thread::Priority::background);
    contents.setDirectory (defaultBrowserRoot(),
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
    tree.setBounds (r);
}

//==============================================================================
void BrowserView::fileDoubleClicked (const File& file)
{
    // Only fire for real, existing audio files. Directories double-click to expand/collapse in
    // the tree itself; the WildcardFileFilter already keeps non-audio files out of the list, but
    // we still guard existsAsFile() defensively.
    if (file.existsAsFile() && onImportFile != nullptr)
        onImportFile (file);
}
