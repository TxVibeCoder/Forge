/*
    ChannelTray — the Arrange sidebar's per-track channel strip (W04a): the GarageBand/Logic
    inspector pattern. The shell binds it to the SELECTED track (setTrack), and it surfaces that
    one track's mix controls — colour band, name, pan, A/B aux sends, a compact insert list, the
    volume fader, and M/S — so per-track mixing happens without leaving the arrangement. The
    standalone Mixer view is unchanged; this is a compact re-implementation of its strip idioms,
    NOT an extraction (the mixer's InsertPanel/SendControls are file-local to MixerView.cpp; a
    shared extraction is a separate ticket).

    Engine access goes through the same seams MixerView uses: EngineHelpers vol/pan dB helpers,
    AudioTrack setMute/setSolo, the ProjectSession aux-send seam (getTrackSendLevel /
    setTrackSendLevel by ABSOLUTE track index), and the PluginHost insert contract
    (getTrackInserts / addPluginToTrack / PluginWindow::show). No raw engine bus calls.

    LIFETIME (the R1 rule — non-negotiable): the bound track is held ONLY as an identity to
    re-validate, never as a trusted reference. Every poll tick — and every user-gesture callback —
    first confirms the pointer is still present in te::getAudioTracks (a cheap contains scan)
    BEFORE any dereference; on a mismatch the tray self-clears to the empty state. A track deleted
    in Arrange while shown here therefore can never be dereferenced stale (the W03 ReturnStrip
    use-after-free class). The shell additionally clears eagerly via clearIfShowing / refreshNow
    from its mutation hooks; the per-tick scan is the backstop for paths that bypass them (undo).

    Live sync: a 10 Hz poll (juce::Timer, DetailView precedent; started/stopped in setTrack)
    pulls engine values into the widgets so a change made on another surface (mixer, MIDI-learn
    hardware, automation) appears here without re-selecting. Full W03 guard set: per-slider drag
    brackets + keyboard-focus guards, isMouseButtonDown for the buttons, every write uses
    dontSendNotification, the name/colour are edge-compared so a steady-state tick repaints
    nothing, and the tick never logs. The insert rows are rebuilt only on an edge-compared chain
    size change (a cheap int), never per tick — and each row holds the engine's reference-counted
    plugin handle, so a row click can never walk freed memory even inside the same tick.

    No meter in v1 (the measurer-lifetime surface is deliberately out of this wave).

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

class ProjectSession;   // aux-send seam + edit access — see the ctor note

//==============================================================================
class ChannelTray : public juce::Component,
                    private juce::Timer
{
public:
    /** The tray drives all bus/send structure through the ProjectSession seam and all insert ops
        through the PluginHost namespace (free functions — mirroring MixerView, which also binds a
        session pointer and calls PluginHost directly). The session outlives the tray. */
    explicit ChannelTray (ProjectSession&);
    ~ChannelTray() override;

    /** Width the shell should give the tray when hosting it in the sidebar. */
    static constexpr int preferredWidth = 200;

    /** Binds the tray to a track (or nullptr to show the "Select a track" hint) and starts
        (track) / stops (nullptr) the 10 Hz live-sync poll. The pointer is validated against the
        current edit's track list before anything is read from it; a stale bind degrades to the
        empty state. Safe to call repeatedly. */
    void setTrack (te::AudioTrack*);

    /** Shell mutation hook: clears to the empty state iff `t` is the currently-bound track.
        Pure pointer-identity compare — `t` may already be dangling at a delete-track hook, so it
        is never dereferenced here. Null / non-matching tracks are a no-op. */
    void clearIfShowing (te::Track* t);

    /** Runs one live-sync poll tick synchronously — the deterministic, headless mirror of the
        10 Hz timer for selftests (same seam shape as MixerView::refreshControls). Also the
        shell's cheap "re-validate now" hook: it self-clears if the bound track has died. */
    void refreshNow();

    /** Fader value (dB) currently shown — selftest seam. */
    double getFaderDb() const;

    /** Mute-button state currently shown — selftest seam. */
    bool getMuteShown() const;

    /** True while a track is bound (i.e. not in the empty state) — selftest seam. */
    bool isShowingTrack() const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    //==============================================================================
    // "A"/"B" aux buses — matches MixerLayout::auxBusCount (README product call: default 2).
    static constexpr int auxBusCount = 2;

    /** 10 Hz live-sync poll: identity re-validation first, then structure (insert-chain edge
        compare), then guarded engine-to-widget value sync. Message thread; never logs. */
    void timerCallback() override;

    /** Gates the poll on visibility (QC): a hidden tray (sidebar parked on the Files tab) does
        no engine work; re-showing resyncs immediately and restarts the poll, so a stale frame
        never renders. The headless gate drives refreshNow() directly and is unaffected. */
    void visibilityChanged() override;

    /** One "identity:name" entry per plugin in the track's chain — the structure edge-compare
        target (catches same-count replaces and renames, not just count changes — QC). */
    static juce::StringArray chainSignature (te::AudioTrack&);

    /** THE validation gate every dereference goes through: returns the bound track only if it is
        still present in te::getAudioTracks for the current edit (else nullptr), optionally
        reporting its absolute index — the same index the ProjectSession send seam keys on.
        Does not self-clear (callers decide); a null result in the tick triggers the clear. */
    te::AudioTrack* resolveLiveTrack (int* liveIndexOut = nullptr) const;

    /** Full rebind: validates the bound track, toggles empty-state visibility, seeds every
        control from the engine, and rebuilds the insert rows. Called by setTrack only. */
    void rebuildFromTrack();

    /** Rebuilds the compact insert-row buttons from the track's current insert chain. Called on
        bind and on an edge-compared chain-size change — never on a steady-state tick. */
    void rebuildInsertRows();

    /** Guarded engine-to-widget sync for one tick (values only; structure is handled before
        this is called). `liveIndex` is the validated absolute track index for the send seam. */
    void syncControls (te::AudioTrack&, int liveIndex);

    /** Pops the add-insert menu (PluginHost::getAvailablePluginNames -> addPluginToTrack). */
    void showAddInsertMenu();

    //==============================================================================
    ProjectSession& session;

    // The bound track — an IDENTITY, not a trusted reference (see the header lifetime note).
    // Every dereference path re-validates through resolveLiveTrack() first.
    te::AudioTrack* track = nullptr;

    // Paint cache: paint() must never touch the engine, so the band colour is cached here and
    // refreshed (edge-compared) by the tick.
    juce::Colour trackColour;

    // Edge-compare state for the insert-chain structure poll (see chainSignature — catches
    // count changes, same-count replaces, and renames). Reset on rebind.
    juce::StringArray lastChainSig;

    // Set for the duration of a mouse drag on the matching control — the live-sync poll skips a
    // control the user is holding, so it never fights a gesture.
    bool faderDragging = false, panDragging = false;
    bool sendDragging[auxBusCount] = {};

    // Aux sends are hidden when the bound track IS an aux return (a return sending to itself
    // would be a feedback structure; the mixer's return strips have no send row either).
    bool sendsShown = false;

    juce::Label  nameLabel;
    juce::Slider pan;
    juce::OwnedArray<juce::Slider> sendKnobs;    // auxBusCount rotaries, built once in the ctor
    juce::OwnedArray<juce::Label>  sendLabels;   // the "A"/"B" letters under the knobs
    juce::OwnedArray<juce::TextButton> insertRows;   // one per user insert; rebuilt on chain change
    juce::TextButton addInsertButton { "+" };
    juce::Slider fader;
    juce::TextButton muteButton { "M" }, soloButton { "S" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelTray)
};
