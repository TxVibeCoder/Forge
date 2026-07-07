/*
    PeakMeter — Forge's shared thin vertical level bar, driven by a te::LevelMeasurer via a
    Client we register on the source measurer. Extracted (W04b) from MixerView.cpp so the mixer
    strips, the master, the aux returns AND the Arrange channel tray all draw the same meter from
    one definition rather than re-implementing it (the shared-utilities principle) — the class
    body below is the mixer's original, moved verbatim.

    pushLevelDb is called from the owner's timer; the bar holds a smoothed "current" value that
    decays toward the live reading so movement looks like a real meter rather than a strobe. If no
    measurer is attached the meter simply draws empty — it NEVER fabricates a level.

    Reads the louder of the (up to two) measured channels for a single mono-ish bar; this keeps
    the strip narrow while still showing clipping on either side.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/ForgeLookAndFeel.h"

namespace te = tracktion;

namespace forge::meter
{
    // Meter ballistics / scale. Header-scoped (anonymous-namespace-equivalent via inline) so each
    // translation unit that includes this shares one definition; named distinctly from any strip's
    // fader/pan constants so an including TU's own kMin/kMax can't collide.
    inline constexpr float kMeterMinDb        = -60.0f;   // bottom of the meter
    inline constexpr float kMeterMaxDb        =   6.0f;   // top of the meter (matches fader headroom)
    inline constexpr float kMeterDecayDbPerSec = 18.0f;   // visual fall-off when the signal drops
    inline constexpr float kMeterHoldDecayDbPerSec = 6.0f;   // W08: the peak-HOLD line falls SLOWER than the
                                                            // bar (18) so it lingers above the falling level

    /** Maps a dB level into a 0..1 fill fraction for the meter (clamped). */
    inline float dbToMeterFraction (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
    }

    //==========================================================================
    // W08 peak-hold + clip-latch — a Component-FREE pure state + transition, so the ballistics are
    // headlessly gate-able (the computeLcdState pattern) without a live meter/paint. The PeakMeter
    // owns one MeterHold and advances it each poll via advanceMeterHold; the gate drives the free
    // function directly.
    struct MeterHold
    {
        float holdDb      = kMeterMinDb;   // the lingering peak line (instant attack, slow decay)
        bool  clipLatched = false;         // sticky once the signal crossed 0 dBFS; cleared only on click
    };

    /** Pure transition: given the previous hold state and this tick's LIVE (pre-bar-decay) reading,
        return the next state. The hold jumps up instantly to a new peak and decays slowly toward the
        live level; the clip latch sets sticky the instant liveDb crosses 0 dBFS and is NEVER cleared
        here (only a user click resets it — that's what makes the latch assertable deterministically). */
    inline MeterHold advanceMeterHold (MeterHold prev, float liveDb, float secondsSinceLast,
                                       float holdDecayDbPerSec = kMeterHoldDecayDbPerSec)
    {
        MeterHold out = prev;

        if (liveDb >= out.holdDb)
            out.holdDb = liveDb;
        else
            out.holdDb = juce::jmax (liveDb, out.holdDb - holdDecayDbPerSec * secondsSinceLast);

        if (liveDb > 0.0f)
            out.clipLatched = true;

        return out;
    }

    /** Pure click-to-clear transition (the SAME guard PeakMeter::mouseDown applies): the sticky clip
        latch is reset ONLY when the clip indicator is enabled — a click on a plain meter never clears
        it. Both PeakMeter::mouseDown and --selftest-peakhold route through this, so the gate exercises
        the real predicate, not a bare field write. */
    inline MeterHold clearClipLatch (MeterHold prev, bool showClip)
    {
        MeterHold out = prev;
        if (showClip)
            out.clipLatched = false;
        return out;
    }
}

//==============================================================================
class PeakMeter : public juce::Component
{
public:
    PeakMeter() = default;

    ~PeakMeter() override
    {
        detach();
    }

    /** Registers as a Client on `m`'s measurer so getAndClearAudioLevel() returns live peaks.

        LIFETIME: the source is held as a juce::WeakReference (LevelMeasurer is declared weak-
        referenceable in the engine), because a measurer's OWNER can die under us on the message
        thread — the master measurer lives on the playback context (freed by freePlaybackContext /
        device restarts), and a track measurer's LevelMeterPlugin can be reclaimed by the engine's
        plugin cull after a track delete. The weak ref nulls itself exactly when the owner dies, so
        detach() skips removeClient precisely when calling it would walk freed memory (the W03
        sync-gate teardown spun forever on exactly that). A dead measurer's client list died with
        it, so skipping the unregister is the correct teardown, not a leak. */
    void attach (te::LevelMeasurer* m)
    {
        // Note: a dead-and-recycled source at the same address compares as changed here, because
        // the weak ref reads back null once the old owner died — which forces the re-register the
        // fresh measurer needs.
        if (measurer.get() == m)
            return;

        detach();
        measurer = m;

        if (m != nullptr)
            m->addClient (client);
    }

    void detach()
    {
        if (auto* m = measurer.get())
            m->removeClient (client);   // owner still alive: unregister properly

        measurer = nullptr;
        client.reset();
        currentDb = forge::meter::kMeterMinDb;
        hold = {};   // W08: a fresh/absent source must not carry a stale hold line or latched clip
        repaint();
    }

    bool hasSource() const noexcept { return measurer.get() != nullptr; }

    /** Orient the level fill HORIZONTALLY (fills left->right) instead of the default VERTICAL (fills
        bottom->up). For a wide, short meter row (e.g. the W08 Session mixer strip) the horizontal fill
        reads as a real level bar; a vertical fill in a 9px-tall row is an unreadable sliver. Backward-
        compatible: default is vertical, so every existing caller (mixer strips / master / returns / tray)
        is unchanged. */
    void setHorizontal (bool h) { horizontal = h; repaint(); }

    /** W08: opt-in peak-HOLD line (a slow-decaying marker that lingers at the recent peak). Default
        OFF so every existing meter (mixer strips / returns / tray) renders byte-identically. */
    void setPeakHold (bool enabled) { peakHold = enabled; repaint(); }

    /** W08: opt-in sticky CLIP latch (a red indicator at the hot end once the signal crossed 0 dBFS,
        cleared by clicking the meter). Default OFF — existing callers are unchanged. */
    void setShowClip (bool enabled) { showClip = enabled; repaint(); }

    /** Selftest / owner read-backs for the hold ballistics (valid after poll). */
    float getHoldDb()     const noexcept { return hold.holdDb; }
    bool  isClipLatched() const noexcept { return hold.clipLatched; }

    /** Pull the latest peak off the measurer and apply decay. Called on the owner's timer.
        `secondsSinceLast` is the timer interval, used for the fall-off rate. */
    void poll (float secondsSinceLast)
    {
        if (measurer.get() == nullptr)
            return;

        // Peak across the active channels (mono bar shows the hotter side).
        float liveDb = forge::meter::kMeterMinDb;
        const int chans = juce::jmax (1, client.getNumChannelsUsed());

        for (int ch = 0; ch < juce::jmin (chans, 2); ++ch)
        {
            const auto pair = client.getAndClearAudioLevel (ch);
            liveDb = juce::jmax (liveDb, pair.dB);
        }

        // Instant attack, timed decay: a quiet/zero reading must not snap the bar to silence.
        if (liveDb >= currentDb)
            currentDb = liveDb;
        else
            currentDb = juce::jmax (liveDb, currentDb - forge::meter::kMeterDecayDbPerSec * secondsSinceLast);

        // W08: advance the (slower) hold line + sticky clip latch off the SAME live reading. Only when
        // a feature is enabled — a default meter never touches the hold state, so its paint is unchanged.
        if (peakHold || showClip)
            hold = forge::meter::advanceMeterHold (hold, liveDb, secondsSinceLast);

        repaint();
    }

    /** Click-to-clear the sticky clip latch, via the SAME pure transition the gate tests
        (forge::meter::clearClipLatch — a no-op unless the clip indicator is enabled). */
    void mouseDown (const juce::MouseEvent&) override
    {
        const auto next = forge::meter::clearClipLatch (hold, showClip);
        if (next.clipLatched != hold.clipLatched)
        {
            hold = next;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (ForgeLookAndFeel::raisedBg));
        g.fillRect (r);

        const float frac = forge::meter::dbToMeterFraction (currentDb);
        // Amber under 0 dBFS-ish, red once we're into the top headroom band (clipping warning).
        const bool  hot         = currentDb > 0.0f;
        const auto  levelColour = hot ? juce::Colour (ForgeLookAndFeel::recordRed)
                                      : juce::Colour (ForgeLookAndFeel::accent);
        const float zeroFrac    = forge::meter::dbToMeterFraction (0.0f);

        // W08: the hold-line fraction + whether to draw the sticky clip cap (both guarded on the opt-in
        // flags, so a default meter takes neither branch and paints exactly as before).
        const float holdFrac = forge::meter::dbToMeterFraction (hold.holdDb);
        const bool  drawHold  = peakHold && hold.holdDb > forge::meter::kMeterMinDb;
        const bool  drawClip  = showClip && hold.clipLatched;

        if (horizontal)
        {
            if (frac > 0.0f)
            {
                g.setColour (levelColour);
                g.fillRect (r.removeFromLeft (r.getWidth() * frac));
            }

            // 0 dB tick as a vertical line so the user can read the headroom point.
            const float zeroX = (float) getWidth() * zeroFrac;
            g.setColour (juce::Colour (ForgeLookAndFeel::hairline));
            g.fillRect (zeroX, 0.0f, 1.0f, (float) getHeight());

            if (drawHold)   // a bright 2px vertical marker at the recent peak
            {
                g.setColour (juce::Colour (ForgeLookAndFeel::textPrim));
                g.fillRect ((float) getWidth() * holdFrac - 1.0f, 0.0f, 2.0f, (float) getHeight());
            }

            if (drawClip)   // sticky red cap at the hot (right) end
            {
                g.setColour (juce::Colour (ForgeLookAndFeel::recordRed));
                g.fillRect ((float) getWidth() - 3.0f, 0.0f, 3.0f, (float) getHeight());
            }
        }
        else
        {
            if (frac > 0.0f)
            {
                g.setColour (levelColour);
                g.fillRect (r.removeFromBottom (r.getHeight() * frac));
            }

            // 0 dB tick as a horizontal line.
            const float zeroY = (float) getHeight() * (1.0f - zeroFrac);
            g.setColour (juce::Colour (ForgeLookAndFeel::hairline));
            g.fillRect (0.0f, zeroY, (float) getWidth(), 1.0f);

            if (drawHold)   // a bright 2px horizontal marker at the recent peak
            {
                g.setColour (juce::Colour (ForgeLookAndFeel::textPrim));
                g.fillRect (0.0f, (float) getHeight() * (1.0f - holdFrac) - 1.0f, (float) getWidth(), 2.0f);
            }

            if (drawClip)   // sticky red cap at the hot (top) end
            {
                g.setColour (juce::Colour (ForgeLookAndFeel::recordRed));
                g.fillRect (0.0f, 0.0f, (float) getWidth(), 3.0f);
            }
        }

        g.setColour (juce::Colour (ForgeLookAndFeel::hairline));
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }

private:
    juce::WeakReference<te::LevelMeasurer> measurer;   // nulls itself when the owner dies (see attach)
    te::LevelMeasurer::Client client;
    float currentDb = forge::meter::kMeterMinDb;
    bool  horizontal = false;   // W08: left->right fill for a wide meter row (default: vertical, bottom->up)
    bool  peakHold = false;     // W08: draw the slow-decay peak-hold line (opt-in; default off)
    bool  showClip = false;     // W08: draw + latch the sticky clip cap (opt-in; default off)
    forge::meter::MeterHold hold;   // W08: hold-line + clip-latch state, advanced each poll when enabled

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakMeter)
};
