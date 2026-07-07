/*
    SessionMasterStrip — the compact MASTER strip that fills the fixed bottom-right CORNER of the
    SessionView grid (Wave 8). The corner is the SessionLayout::sceneColW × SessionLayout::mixerBandH
    region under the pinned scene column: the scene column above it is the scene-launch "master"
    (STOP ALL); this is the audio master directly below it, so the Session grid becomes a
    self-contained mixing surface (ride the master + trust the meters without leaving for the Mix view).

    Drives the Edit's MASTER VOLUME (edit.getMasterVolumePlugin(), a VolumeAndPanPlugin with
    getVolumeDb/setVolumeDb) and meters the engine's post-master-fader output
    (EditPlaybackContext::masterLevels) — the SAME idiom as MixerView::MasterStrip, deliberately NOT
    inserting a LevelMeterPlugin into the master list (which would dirty a clean Edit + meter
    pre-fader). The playback context comes and goes with the transport graph, so the meter re-binds
    its WeakReference source every poll rather than caching a LevelMeasurer* (R1 / the W03 UAF fix).
    Compact + horizontal to match the Session mixer band: a "MASTER" label, a horizontal PeakMeter
    (with peak-HOLD + sticky clip latch enabled — the W08 meter polish, the master being the meter
    most worth watching), and a horizontal dB fader. No pan (mirrors MixerView::MasterStrip).

    LIFETIME (R1): caches ONLY the Edit* (raw, non-owning). Re-resolves the master volume plugin
    fresh every refresh/poll and re-binds the meter's WeakReference source from the CURRENT playback
    context every poll — never caches the plugin or the measurer. A ~12 Hz visibility-gated poll
    live-syncs the fader from the engine (guarded: never mid-drag / mid-focus, always
    dontSendNotification) so an external master change shows without a rebuild. Message-thread only.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/common/PeakMeter.h"

namespace te = tracktion;

//==============================================================================
class SessionMasterStrip : public juce::Component,
                           private juce::Timer
{
public:
    SessionMasterStrip();
    ~SessionMasterStrip() override;

    /** Binds the strip to `edit`'s master (nullptr / no master plugin -> empty state). Caches ONLY
        the Edit*; re-resolves the master volume plugin live every refresh. Starts the poll when
        bound + visible; stops it otherwise. */
    void setEdit (te::Edit* edit);

    /** Synchronous engine->widget sync (the poll body AND the --selftest-sessionmaster seam):
        re-resolve the master plugin, seed the fader from getVolumeDb() (guarded, dontSendNotification),
        self-clear to empty if the master no longer resolves. */
    void refreshControls();

    // Selftest read-backs (valid after refreshControls):
    bool   isBound()    const;
    double getFaderDb() const;   // the fader's shown master dB

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    //==============================================================================
    /** ~12 Hz live-sync poll: re-resolve the master first (self-clear on miss), then the guarded
        fader sync, then re-bind + poll the meter off the CURRENT playback context. Never logs. */
    void timerCallback() override;

    /** Gates the poll on visibility (a hidden Session tab does no engine work). */
    void visibilityChanged() override;

    /** THE R1 gate: the Edit's master volume plugin for the cached Edit, or nullptr. Never cached. */
    te::VolumeAndPanPlugin* resolveMaster() const;

    /** Full rebind: resolve the master, toggle empty-state visibility, seed the fader. setEdit only. */
    void rebindFromEdit();

    //==============================================================================
    te::Edit* edit = nullptr;    // raw, non-owning (R1); the ONLY handle we keep
    bool bound         = false;  // true once the master plugin resolved on the last rebind/refresh
    bool faderDragging = false;  // the poll skips a held fader so the sync never fights a gesture

    juce::Label      nameLabel;
    juce::Slider     fader;      // horizontal dB fader (forge::strip::styleDbFader + LinearHorizontal)
    PeakMeter        meter;      // shared meter, peak-hold + clip enabled (WeakReference source, UAF-safe)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionMasterStrip)
};
