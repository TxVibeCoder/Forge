/*
    DetailView — the clip Inspector shown in the bottom Detail-drawer region of the shell,
    replacing the Phase-2 placeholder Label.

    When a clip is selected in the arrange view the shell calls setClip(c); this surfaces the
    clip's editable properties (name, gain, mute, and — for a WaveAudioClip — fade in/out) plus
    read-only timing (start / length / offset) and a large waveform thumbnail. Editing any
    property pushes the change straight to the engine clip and fires onEditMutated() so the
    shell can persist the Edit.

    While a clip is bound, a 10 Hz poll (juce::Timer; started/stopped in setClip) live-syncs the
    editors from the engine, so a change made on another surface (mixer, MIDI-learn hardware)
    appears here without re-selecting. Sync writes use dontSendNotification and skip any control
    the user is interacting with (slider drags, an in-progress name edit), so a gesture is never
    fought and no feedback loop can form.

    Lifetime: the selected clip is held as a te::Clip::Ptr (the engine's reference-counted
    handle) so the Inspector never dereferences a clip that has been removed from its track and
    deleted underneath it. The shell additionally re-calls setClip(nullptr) on structural
    changes (rebuild), so a stale clip is never shown across a rebuild.

    Message-thread only.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
class DetailView : public juce::Component,
                   private juce::Timer
{
public:
    DetailView();
    ~DetailView() override;

    /** Binds the Inspector to a clip (or nullptr to show the empty hint). Rebuilds the controls
        from the clip's current state, and starts (clip) / stops (nullptr) the 10 Hz live-sync
        poll. Safe to call repeatedly; passing nullptr clears the view. */
    void setClip (te::Clip*);

    /** The clip currently bound to the Inspector, or nullptr. Lets the shell detect (and clear) a clip
        DETACHED underneath the drawer (e.g. a Session slot delete): the Ptr keeps it alive but its state
        tree goes parentless, so further edits write to a dead tree. See MainComponent::reconcileDrawerClip. */
    te::Clip* getClip() const { return clip.get(); }

    /** Runs one live-sync poll tick synchronously — the deterministic, headless mirror of the
        10 Hz timer for selftests (same seam shape as MixerView::refreshControls). */
    void refreshNow();

    /** Gain-slider value (dB) currently shown — selftest seam. */
    double getGainSliderDb() const;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Fired after the user changes a property (name / gain / mute / fade), so the shell saves. */
    std::function<void()> onEditMutated;

private:
    //==============================================================================
    /** 10 Hz live-sync poll: engine→widget only, guarded per control, message thread. Runs only
        while a clip is bound. */
    void timerCallback() override;

    /** Rebuilds editor visibility/values from `clip`. Called by setClip(). */
    void refreshFromClip();

    /** Returns the held clip as an AudioClipBase* (gain/mute/fade live there), or nullptr. */
    te::AudioClipBase* getAudioClip() const;

    /** (Re)creates the SmartThumbnail for a WaveAudioClip's playback file, or clears it. */
    void rebuildThumbnail();

    /** Paints the large waveform (or a status string) into `area`. */
    void paintWaveform (juce::Graphics&, juce::Rectangle<int> area);

    /** Formats a TimePosition / TimeDuration as bars|beats-free seconds for the read-only row. */
    static juce::String formatSeconds (double seconds);

    /** Builds the read-only "Start / Length / Offset" line shown in the timing label. */
    static juce::String formatTiming (double startSecs, double lengthSecs, double offsetSecs);

    void notifyMutated();

    //==============================================================================
    // The selected clip, held by the engine's reference-counted handle so it can't dangle.
    te::Clip::Ptr clip;

    // Waveform thumbnail for a WaveAudioClip (null for other clip types / no clip).
    std::unique_ptr<te::SmartThumbnail> thumbnail;

    // --- Editors -----------------------------------------------------------------------------
    juce::Label  nameTitle, gainTitle, timingLabel;
    juce::Label  nameEditor;                       // editable (setEditable) -> clip.setName
    juce::Slider gainSlider;                        // dB -> AudioClipBase::setGainDB
    juce::ToggleButton muteToggle { "Mute" };       // -> AudioClipBase::setMuted

    juce::Label  fadeInTitle, fadeOutTitle;
    juce::Slider fadeInSlider, fadeOutSlider;       // seconds -> AudioClipBase::setFadeIn/Out

    bool hasClip = false;

    // Set for the duration of a mouse drag on the matching slider — the live-sync poll skips a
    // control the user is holding, so it never fights a gesture.
    bool gainDragging = false, fadeInDragging = false, fadeOutDragging = false;

    // Edge-compare state for the timing poll (raw seconds): the label string and the fade-slider
    // ranges are rebuilt ONLY when one of these changes, never per-tick. Reset in setClip().
    double lastStart = -1.0, lastLen = -1.0, lastOffset = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailView)
};
