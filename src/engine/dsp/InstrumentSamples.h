/*
    InstrumentSamples — self-rendered, CC0 instrument one-shots for Forge's built-in voices.

    WHY SELF-RENDERED. Forge is a public AGPLv3 repo; shipping third-party sample packs would drag
    in upstream licences and binary blobs. Instead the piano one-shot the engine Sampler plays is
    SYNTHESIZED FROM FORGE'S OWN MATH at runtime — CC0 / redistribution-clean by construction, with
    nothing to license-audit and no binary committed to the repo (the generator code IS the canonical
    source; the .wav is a derived, machine-local artifact under %APPDATA%\Forge\library).

    This extends the proven procedural-WAV pattern (src/main.cpp createSineWaveFile): synthesize into a
    juce::AudioBuffer<float>, then WavAudioFormat::createWriterFor -> writeFromAudioSampleBuffer.

    DETERMINISM. Output is a pure function of the fixed synthesis constants — no wall-clock, no device,
    no juce::Random (its seeding/impl is a cross-platform portability risk). Any stochastic content uses
    a tiny SELF-OWNED seeded PRNG so the bytes are reproducible across platforms and STL versions.

    Message-thread only (file I/O). Every fallible seam logs via core/Log.h on failure.
*/

#pragma once

#include <JuceHeader.h>

namespace InstrumentSamples
{
    // The MIDI note the piano one-shot is rendered at (middle C, ~261.63 Hz). The Sampler maps this
    // root note chromatically across the keyboard by resampling, so the one sample plays every pitch.
    inline constexpr int kRootNote = 60;

    /** Generates a self-rendered CC0 piano one-shot into %APPDATA%\Forge\library\piano.wav (mono,
        44100 Hz) if a valid one is not already present, and returns its File. Idempotent: an existing,
        non-empty file is returned without regenerating. Returns an empty File on failure (and logs).
        Deterministic (seeded self-owned PRNG for the strike transient, NOT juce::Random).
        Message-thread only. */
    juce::File ensurePianoOneShot();

    //==============================================================================
    // Drum kit — one self-rendered CC0 one-shot per GM drum note a StepClip channel triggers, so the
    // 8-lane drum grid sounds like a kit (loaded into a te::SamplerPlugin, one voice per note) instead
    // of 8 pitches of a synth. Same determinism/CC0 contract as the piano.

    /** One rendered drum voice: the on-disk one-shot, the GM MIDI note it is mapped to, and a display
        name. Returned in StepClip CHANNEL ORDER by ensureDrumKit(). */
    struct DrumHit
    {
        juce::File   file;
        int          midiNote = 0;
        juce::String name;
    };

    /** Generates (if missing) the 8 CC0 drum one-shots into %APPDATA%\Forge\library\drum_<note>.wav and
        returns them in StepClip channel order — GM notes 36 (kick), 38 (snare), 42 (closed hat),
        46 (open hat), 39 (clap), 45 (low tom), 50 (high tom), 51 (ride). Idempotent + crash-safe PER
        VOICE (looksValid reuse, temp-then-move, log on failure): a voice that fails to write is omitted,
        never returned half-written, so the result holds only the voices actually on disk (0..8, in
        order). Deterministic (a distinct seeded self-owned PRNG per voice, NOT juce::Random).
        Message-thread only (file I/O). */
    juce::Array<DrumHit> ensureDrumKit();
}
