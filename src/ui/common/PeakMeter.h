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

    /** Maps a dB level into a 0..1 fill fraction for the meter (clamped). */
    inline float dbToMeterFraction (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
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
        repaint();
    }

    bool hasSource() const noexcept { return measurer.get() != nullptr; }

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

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (ForgeLookAndFeel::raisedBg));
        g.fillRect (r);

        const float frac = forge::meter::dbToMeterFraction (currentDb);
        if (frac > 0.0f)
        {
            auto fill = r.removeFromBottom (r.getHeight() * frac);

            // Amber under 0 dBFS-ish, red once we're into the top headroom band (clipping warning).
            const bool hot = currentDb > 0.0f;
            g.setColour (hot ? juce::Colour (ForgeLookAndFeel::recordRed)
                             : juce::Colour (ForgeLookAndFeel::accent));
            g.fillRect (fill);
        }

        // 0 dB tick line so the user can read the headroom point.
        const float zeroFrac = forge::meter::dbToMeterFraction (0.0f);
        const float zeroY = (float) getHeight() * (1.0f - zeroFrac);
        g.setColour (juce::Colour (ForgeLookAndFeel::hairline));
        g.fillRect (0.0f, zeroY, (float) getWidth(), 1.0f);

        g.setColour (juce::Colour (ForgeLookAndFeel::hairline));
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }

private:
    juce::WeakReference<te::LevelMeasurer> measurer;   // nulls itself when the owner dies (see attach)
    te::LevelMeasurer::Client client;
    float currentDb = forge::meter::kMeterMinDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakMeter)
};
