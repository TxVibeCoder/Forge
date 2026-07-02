#include "ui/popout/PopoutWindow.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

PopoutWindow::PopoutWindow (const String& title,
                            Component& content,
                            std::function<void()> onClosedCallback)
    : DocumentWindow (title,
                      Colour (ForgeLookAndFeel::shellBg),
                      DocumentWindow::closeButton,
                      /*addToDesktop*/ true),
      onClosed (std::move (onClosedCallback))
{
    // Capture the docked size FIRST: setContentNonOwned resizes the content to fit the
    // still-default-sized window, so reading it afterwards would always give the floor.
    const int contentW = content.getWidth();
    const int contentH = content.getHeight();

    setUsingNativeTitleBar (true);   // PluginWindow convention

    // NON-owned is load-bearing: the content is a shell member. setContentOwned would make
    // ~ResizableWindow delete it — a double-delete when the shell destructs its members.
    setContentNonOwned (&content, false);

    setResizable (true, false);

    // Adopt the docked size (min 480x320 for a never-laid-out view, e.g. a piano-roll whose
    // drawer was never opened). NO setAlwaysOnTop — normal z-order is a deliberate divergence
    // from PluginWindow: a full-size mixer pinned above the shell would permanently occlude it.
    centreWithSize (jmax (480, contentW), jmax (320, contentH));
    setVisible (true);
}

PopoutWindow::~PopoutWindow()
{
    // Non-owned content: this only detaches the child and nulls the window's SafePointer — it
    // never deletes. If the shell already reparented the content home, removeChildComponent
    // resolves to index -1 and no-ops. (~ResizableWindow would do the same; being explicit
    // keeps the discipline visible if teardown is ever restructured.)
    clearContentComponent();
}

void PopoutWindow::closeButtonPressed()
{
    // Route to the shell and do NOTHING else: this runs on the window's own stack, so the
    // shell defers the window's destruction via MessageManager::callAsync (PluginWindow
    // precedent) — a synchronous reset here would delete the window mid-method.
    if (onClosed)
        onClosed();
}
