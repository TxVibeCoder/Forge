/*
    ClipFades — the automatic anti-click edge fade applied to audio clips on create/import/record.

    A tiny, pure, message-thread helper (a namespace, like Exporter). When a fresh audio clip is
    born — imported to the arrangement, dropped into a Session slot, or committed from a recording —
    its raw sample boundaries usually don't sit on a zero crossing, so playback starts/ends with an
    audible click. A short linear ramp at each edge removes it.

    Design (ported from a sibling clip-editing tool):
      - Default fade length = kDefaultEdgeFadeMs (5 ms) — short enough to be inaudible as a fade,
        long enough to kill the click. (This matches the length te::AudioClipBase::applyEdgeFades()
        uses internally; we implement it here so the length + curve are ours to control and the
        constant is one canonical source.)
      - Curve = linear (AudioFadeCurve::linear).
      - Clamped to <= half the clip length, so a symmetric in+out fade can never overlap on a very
        short clip.
      - Idempotent and non-destructive: re-applying never stacks, and it only ever GROWS a fade
        toward the target — a longer fade a user set by hand in the DetailView inspector is left
        alone.
      - Audio only. MIDI and every other clip type are a silent no-op.

    Message-thread only — this runs at clip-create time, never on the audio/RT thread and never in a
    per-tick poll or paint. Do NOT call it from a hot path.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace ClipFades
{
    /** Default anti-click edge-fade length, in milliseconds (README product call). */
    constexpr double kDefaultEdgeFadeMs = 5.0;

    /** Applies the default anti-click edge fade to a freshly-created clip.

        If `clip` is an audio clip (dynamic_cast to te::AudioClipBase succeeds), sets a linear
        fade-in and fade-out of kDefaultEdgeFadeMs, each clamped to <= half the clip length.
        Idempotent (only grows toward the target — never shrinks a longer existing fade).

        If `clip` is a MIDI clip (or any non-audio clip), this is a no-op.

        Call this once, on the message thread, after a clip is imported / inserted into a slot /
        committed from a recording. */
    void applyDefaultEdgeFades (te::Clip& clip);

    /** Applies a linear edge fade of a caller-chosen length to an audio clip.

        `fadeIn` / `fadeOut` are Tracktion TimeDurations (seconds) — the units the engine's
        setFadeIn/setFadeOut speak (see DetailView, which drives them from the inspector sliders).
        Each is clamped to >= 0 and <= half the clip length. As with applyDefaultEdgeFades, the fade
        is only ever grown toward the requested length, so the call is idempotent and won't stomp a
        longer manual fade. A zero/negative-length clip is skipped (logged at DEBUG). */
    void applyEdgeFades (te::AudioClipBase& clip, te::TimeDuration fadeIn, te::TimeDuration fadeOut);
}
