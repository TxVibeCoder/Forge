/*
    SessionMixerStrip — the compact per-track mix band that sits UNDER a SessionView clip-launch
    column (Wave 3 / W08). One strip per audio-track column, laid out in a fixed bottom band
    (SessionView owns the band + horizontal-scroll sync); this component owns ONE track's compact
    mix surface: a horizontal peak meter, a horizontal dB fader, a pan knob, and M / S toggles.
    NO sends here — those stay in the ChannelTray (Arrange) and the Mix view; the Session band is
    deliberately the clean, glanceable subset that fits a 179px column in a ~96px band.

    Template: this is a compact re-implementation of the ChannelTray idioms (the same shared
    forge::strip styling + the shared PeakMeter), NOT an extraction — matching the "compact
    re-implementation, not extraction" stance ChannelTray itself took. The engine seams are the
    same ones the tray uses: EngineHelpers vol/pan dB helpers, AudioTrack setMute/setSolo, and the
    track's LevelMeterPlugin measurer via ui/common/PeakMeter.h. No raw engine bus calls.

    LIFETIME (the R1 rule — non-negotiable): the strip caches ONLY (edit, trackIndex). It NEVER
    caches a te::AudioTrack* / te::LevelMeasurer* across a poll or rebuild. Every poll tick — and
    every user-gesture callback — re-resolves the live AudioTrack fresh via te::getAudioTracks
    (indexOf-by-index) BEFORE any dereference; an out-of-range index degrades to the empty state.
    The PeakMeter holds its source as a juce::WeakReference (the W03 UAF fix), so a mid-poll track
    / plugin cull can never walk freed memory; the strip re-attaches on rebind and re-resolves the
    track before every meter poll — it adds NO second raw measurer cache alongside the WeakReference.

    Live sync: a ~12 Hz poll (juce::Timer) pulls engine values into the widgets so a change made on
    another surface (mixer, tray, MIDI-learn) appears here without re-selecting. Full guard set:
    per-slider drag brackets + keyboard-focus guards, isMouseButtonDown on the buttons, every
    engine->widget write uses dontSendNotification, and the tick never logs. The poll is
    visibility-gated so a hidden Session tab does no engine work.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/common/PeakMeter.h"

namespace te = tracktion;

//==============================================================================
class SessionMixerStrip : public juce::Component,
                          private juce::Timer
{
public:
    SessionMixerStrip();
    ~SessionMixerStrip() override;

    /** Binds the strip to ABSOLUTE track `trackIndex` in `edit` (nullptr / out-of-range -> empty
        state). Caches ONLY (edit, index) and re-resolves the AudioTrack LIVE every refresh (R1 —
        never caches the AudioTrack*). Attaches the PeakMeter's WeakReference measurer from the
        resolved track. Starts the poll when bound + visible; stops it otherwise. */
    void setTrack (te::Edit* edit, int trackIndex);

    /** Renders this column's track as an aux RETURN (a subtle desaturated tint) rather than a plain
        track. Cosmetic only — a return is a normal AudioTrack with the same vol/pan/M-S/meter, so
        every control still works. SessionView calls this from rebuild() using
        ProjectSession::isAuxReturnTrack; the strip itself makes no ProjectSession calls. */
    void setIsReturn (bool isReturn);

    /** Synchronous engine->widget sync (the poll body AND the --selftest-sessionmixer seam).
        Re-resolves the track and updates fader / pan / M / S / meter from the engine with
        juce::dontSendNotification; self-clears to empty if the index no longer resolves. */
    void refreshControls();

    // Selftest read-back accessors (valid after refreshControls):
    bool   isBound()     const;
    double getFaderDb()  const;   // the fader's shown dB
    double getPanValue() const;   // -1..+1
    bool   isMuteOn()    const;
    bool   isSoloOn()    const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    //==============================================================================
    /** ~12 Hz live-sync poll: re-resolve the track first (self-clear on miss), then the guarded
        engine->widget value sync, then meter.poll. Message thread; never logs. */
    void timerCallback() override;

    /** Gates the poll on visibility (a hidden Session tab does no engine work); re-showing resyncs
        immediately and restarts the poll so a stale frame never renders. */
    void visibilityChanged() override;

    /** THE R1 validation gate: returns the live AudioTrack at the cached absolute index for the
        cached edit, or nullptr (no edit / out-of-range). Never caches the result. */
    te::AudioTrack* resolveLiveTrack() const;

    /** Full rebind: resolve the track, toggle empty-state visibility, (re)attach the meter, and
        seed every control from the engine. Called by setTrack only. */
    void rebindFromTrack();

    //==============================================================================
    // R1 identity: the strip caches ONLY these two. Every dereference re-resolves live.
    te::Edit* edit  = nullptr;   // raw, non-owning (R1); re-validated indirectly via resolveLiveTrack
    int trackIndex  = -1;        // ABSOLUTE audio-track index; the only track handle we keep

    bool bound      = false;     // true once a track resolved on the last rebind/refresh
    bool isReturn   = false;     // render as an aux-return (subtle desaturated tint)

    // Drag/focus guards: the poll skips a control the user is actively holding so it never fights
    // a gesture (every engine->widget write is dontSendNotification besides).
    bool faderDragging = false, panDragging = false;

    juce::Slider   fader;                     // horizontal dB fader (forge::strip::styleDbFader + LinearBar)
    juce::Slider   pan;                       // pan knob (forge::strip::stylePanKnob)
    PeakMeter      meter;                     // shared level meter (WeakReference source — W03 UAF-safe)
    juce::TextButton muteButton { "M" }, soloButton { "S" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionMixerStrip)
};
