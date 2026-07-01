#include "engine/ClipFades.h"
#include "core/Log.h"

namespace ClipFades
{

void applyEdgeFades (te::AudioClipBase& clip, te::TimeDuration fadeIn, te::TimeDuration fadeOut)
{
    // Length drives the clamp. A zero/negative-length clip can't carry a fade — bail (this is the
    // only "too short to fade" case: with the half-length clamp below, any positive-length clip
    // still gets a proportionally-shorter fade rather than nothing).
    const double lenSecs = clip.getPosition().getLength().inSeconds();

    if (lenSecs <= 0.0)
    {
        FORGE_LOG_DEBUG ("ClipFades: clip \"" + clip.getName()
                         + "\" has zero/negative length; skipping edge fade");
        return;
    }

    // Clamp each fade to [0, half the clip length] so a symmetric in + out fade can never overlap on
    // a very short clip. The engine's setFadeIn/Out also clamp + rescale, but doing it here keeps the
    // intent explicit and the two fades independent.
    const auto half = te::TimeDuration::fromSeconds (lenSecs * 0.5);
    fadeIn  = juce::jlimit (te::TimeDuration(), half, fadeIn);
    fadeOut = juce::jlimit (te::TimeDuration(), half, fadeOut);

    // Idempotent + non-destructive: only grow an edge toward the target, never shrink a longer fade
    // a user set by hand in the inspector (mirrors te::AudioClipBase::applyEdgeFades). Set the LINEAR
    // curve (the standard de-click) on the SAME edges we touch and no others — so an edge we leave
    // alone keeps the user's hand-picked curve too. NOTE: in this engine version setFadeIn/setFadeOut
    // return a MEANINGLESS bool — they always `return false`, even on success
    // (tracktion_AudioClipBase.cpp) — so we deliberately do NOT test the return value nor log it.
    if (clip.getFadeIn()  < fadeIn)  { clip.setFadeInType  (te::AudioFadeCurve::linear); clip.setFadeIn  (fadeIn); }
    if (clip.getFadeOut() < fadeOut) { clip.setFadeOutType (te::AudioFadeCurve::linear); clip.setFadeOut (fadeOut); }
}

void applyDefaultEdgeFades (te::Clip& clip)
{
    // Audio clips only — MIDI and every other clip type are a silent no-op (no fade concept).
    if (auto* acb = dynamic_cast<te::AudioClipBase*> (&clip))
    {
        const auto fade = te::TimeDuration::fromSeconds (kDefaultEdgeFadeMs / 1000.0);
        applyEdgeFades (*acb, fade, fade);
    }
}

} // namespace ClipFades
