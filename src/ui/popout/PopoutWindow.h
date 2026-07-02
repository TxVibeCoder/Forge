/*
    PopoutWindow — a floating desktop window hosting a torn-off shell view (W04b).

    Ownership contract: the content is a live SHELL MEMBER (MainComponent's mixerView or
    pianoRoll), reparented INTO this window on tear-off and back into the shell on restore.
    The window NEVER owns or deletes it — construction uses setContentNonOwned exclusively,
    and the destructor's clearContentComponent() only detaches (JUCE's non-owned branch
    removes the child and nulls the window's SafePointer; it never deletes). If the content
    was already reparented home before the window dies, the detach is a clean no-op.

    Deferred-close discipline: closeButtonPressed() fires onClosed and does NOTHING else.
    The callback runs on the window's own stack, so the shell must defer the window's
    destruction via MessageManager::callAsync (the PluginWindow precedent) — a synchronous
    reset from inside the callback would destroy the window mid-method (use-after-free).

    Project-swap stance: a swap rebinds the views in place (the component instances are
    never destroyed), so a torn-off window deliberately SURVIVES the swap, showing the
    rebound — or designed-empty — state. No swap-path code is needed here or in the shell.

    Z-order is NORMAL — a deliberate divergence from PluginWindow's setAlwaysOnTop: a
    full-size mixer or piano-roll pinned above the shell would permanently occlude it.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

#include <functional>

class PopoutWindow : public juce::DocumentWindow
{
public:
    /** Tears `content` off into a native-title-bar desktop window (close button only).
        Captures the content's current (docked) size BEFORE setContentNonOwned — attaching
        resizes the content to fit the still-default-sized window, so reading it afterwards
        would always yield the floor — then sizes the window to it (min 480x320), centres,
        and shows. `onClosed` fires on the close button; the SHELL owns what happens next
        (reparent home + DEFERRED window destruction). */
    PopoutWindow (const juce::String& title,
                  juce::Component& content,
                  std::function<void()> onClosed);

    /** Detaches the non-owned content (no-op if it was already reparented home); never
        deletes it. */
    ~PopoutWindow() override;

    /** Fires onClosed and nothing else — restore and destruction are the shell's, deferred. */
    void closeButtonPressed() override;

    /** Shell hook for keys the torn-off content did not consume (W04b QC: with a popout
        focused, space/R/F-keys/B/E were dead). Keys bubble the popout's own hierarchy first,
        so the piano-roll's local Delete/Ctrl+C/V keep priority; only genuinely unconsumed
        keys reach the shell — which acts on the MAIN window's transport/layout, the desired
        second-monitor behaviour. */
    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    bool keyPressed (const juce::KeyPress& key) override
    {
        return onUnhandledKey && onUnhandledKey (key);
    }

private:
    std::function<void()> onClosed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PopoutWindow)
};
