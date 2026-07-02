/*
    SplashWindow — Forge's launch splash (TICKET 1.7, COSMETIC ONLY).

    Honest disclosure: this splash does NOT mask Forge's actual startup latency. te::Engine is a
    ForgeApplication DATA MEMBER, so its constructor — which enumerates audio devices synchronously —
    runs BEFORE ForgeApplication::initialise() is ever called. initialise() is the earliest point any
    JUCE window (this one included) can be shown. By the time this splash appears, the ~8 s of engine
    construction is already behind us; all this buys is a bit of visual cover for MainWindow/MainComponent
    construction and the LookAndFeel/menu-model wiring that follows. Don't extend this comment or the
    header into a promise it can't keep — it's decoration, not a progress indicator.

    Because of that "cosmetic only" scope, the caller (ForgeApplication::initialise() in main.cpp) MUST
    skip constructing this splash entirely whenever any --selftest-* or --screenshot command-line flag
    is present — mirroring the early-return discipline those modes already use elsewhere in main.cpp —
    so the headless selftest floor never spawns a visible window.

    Usage (from ForgeApplication::initialise(), guarded by "not a selftest/screenshot run"):
        splashWindow = std::make_unique<forge::SplashWindow>();
        splashWindow->centreAndShow();
        ... construct MainWindow ...
        splashWindow = nullptr;   // hide + destroy once MainWindow is visible

    Everything lives in this one header — no .cpp, no CMake change (header-only, per Forge's
    file-disjoint wave discipline for a cosmetic, low-risk ticket).
*/

#pragma once

#include <JuceHeader.h>
#include "ui/ForgeLookAndFeel.h"

namespace forge
{
    // ForgeSplash — the drawn content. A plain juce::Component (not a window itself) so it can be
    // hosted inside SplashWindow below, or embedded anywhere else that wants the same mark.
    class ForgeSplash : public juce::Component
    {
    public:
        ForgeSplash()
        {
            setSize (420, 240);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (ForgeLookAndFeel::shellBg));

            auto bounds = getLocalBounds().toFloat();

            // "FORGE" wordmark, large, in the warm amber accent colour.
            g.setColour (juce::Colour (ForgeLookAndFeel::accent));
            g.setFont (juce::Font (juce::FontOptions (56.0f, juce::Font::bold)));
            g.drawText ("FORGE", bounds.removeFromTop (bounds.getHeight() * 0.6f),
                        juce::Justification::centred, false);

            // Small "loading..." sub-line in the secondary text colour.
            g.setColour (juce::Colour (ForgeLookAndFeel::textSec));
            g.setFont (juce::Font (juce::FontOptions (16.0f)));
            g.drawText ("loading...", bounds, juce::Justification::centred, false);
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeSplash)
    };

    // SplashWindow — a tiny borderless, always-on-top top-level window that hosts a ForgeSplash and
    // centres itself on the primary display. Derives from juce::ResizableWindow (not the plainer
    // TopLevelWindow) purely because that's where setContentOwned() lives; resizing itself is never
    // enabled, so in practice it behaves like a fixed-size borderless splash. Construct it, call
    // centreAndShow(), and destroy it (reset the owning unique_ptr) once MainWindow is up — there is
    // nothing else to tear down.
    class SplashWindow : public juce::ResizableWindow
    {
    public:
        SplashWindow()
            : juce::ResizableWindow ("Forge", juce::Colour (ForgeLookAndFeel::shellBg), true)   // true = addToDesktop
        {
            setUsingNativeTitleBar (false);
            setDropShadowEnabled (true);
            setResizable (false, false);

            // setContentNonOwned, not setContentOwned: 'splash' below is a member with the SAME
            // lifetime as this window (not heap-allocated), so setContentOwned's "I'll delete this
            // for you" contract would try to delete a non-heap object on teardown.
            setContentNonOwned (&splash, true);
            setSize (splash.getWidth(), splash.getHeight());

            // Always-on-top so the splash stays visible in front of the (about-to-be-created)
            // MainWindow rather than getting buried behind it.
            setAlwaysOnTop (true);
        }

        // Explicit destructor: detach the content BEFORE 'splash' (a member of this same class)
        // is destroyed. Base-class teardown order is the trap here — ~ResizableWindow() also calls
        // clearContentComponent(), but derived members are destroyed BEFORE the base class dtor
        // runs, so by then 'splash' would already be gone and that later call would touch a
        // dangling pointer. Calling it here, first, makes the detach happen while 'splash' is
        // still alive; ~ResizableWindow()'s own call then simply no-ops (contentComponent is null).
        ~SplashWindow() override
        {
            clearContentComponent();
        }

        // Centres on the primary display's user area and shows the window. Kept as a separate call
        // (rather than doing this in the constructor) so the caller controls exactly when the splash
        // becomes visible.
        void centreAndShow()
        {
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            toFront (false);
        }

    private:
        ForgeSplash splash;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
    };
}
