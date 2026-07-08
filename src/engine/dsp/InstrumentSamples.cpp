#include "engine/dsp/InstrumentSamples.h"

#include "core/Log.h"

#include <cmath>
#include <cstdint>

using namespace juce;

namespace InstrumentSamples
{
    namespace
    {
        //==============================================================================
        // Fixed synthesis constants — the one-shot's byte content is a pure function of these.
        constexpr double kSampleRate = 44100.0;
        constexpr double kSeconds    = 2.2;      // piano-ish decay tail
        constexpr int    kNumPartials = 8;       // additive harmonic stack

        // MIDI note -> frequency in Hz (equal temperament, A4 = 440 Hz at note 69).
        double midiNoteToHz (int note) noexcept
        {
            return 440.0 * std::pow (2.0, (double (note) - 69.0) / 12.0);
        }

        // A tiny SELF-OWNED xorshift32 PRNG. We roll our own (rather than juce::Random) so the strike
        // transient is byte-identical across platforms/STL versions — juce::Random's seeding/impl is a
        // portability risk for a deterministic asset. Seeded from a fixed constant.
        struct Xorshift32
        {
            std::uint32_t s;
            explicit Xorshift32 (std::uint32_t seed) noexcept : s (seed != 0 ? seed : 0x9E3779B9u) {}

            std::uint32_t nextU32() noexcept
            {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                return s;
            }

            // Uniform noise in [-1, 1).
            float nextBipolar() noexcept
            {
                return (float) (nextU32() / 4294967296.0) * 2.0f - 1.0f;
            }
        };

        //==============================================================================
        // Normalises a mono buffer so its peak magnitude is targetPeak (leaving headroom below full
        // scale so the Sampler + downstream chain never clips). No-op on near-silence. The piano uses
        // 0.89 (~ -1 dBFS); the drum voices reuse this exact step (quieter voices pass a lower target).
        void normalisePeak (AudioBuffer<float>& buffer, float targetPeak) noexcept
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);

            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = jmax (peak, std::abs (out[i]));

            if (peak > 1.0e-6f)
            {
                const float norm = targetPeak / peak;
                for (int i = 0; i < numSamples; ++i)
                    out[i] *= norm;
            }
        }

        //==============================================================================
        // Renders the piano one-shot into a mono buffer. Deterministic.
        //
        // Recipe: an additive stack of slightly-INHARMONIC partials at kRootNote's fundamental (real
        // piano strings are stiff, so partial n sits a touch above n*f0), each with decreasing gain and
        // a faster decay for higher partials (higher harmonics die first, as on a real piano). A fast
        // attack + exponential body decay shapes the whole note, and a very short seeded-noise strike
        // transient at the onset gives the hammer "knock". The result is normalised to leave headroom.
        void renderPiano (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);

            const double f0 = midiNoteToHz (kRootNote);

            // Inharmonicity coefficient — partial n frequency = n * f0 * sqrt(1 + B * n^2).
            // Small B keeps it musical while adding the metallic shimmer that reads as "piano".
            const double B = 0.0008;

            Xorshift32 rng (0xF0F17E01u);   // fixed seed -> reproducible strike transient

            // Amp envelope: fast attack (a few ms), then exponential decay over the note.
            const double attackSeconds = 0.004;
            const int    attackSamples = jmax (1, (int) (attackSeconds * kSampleRate));
            const double bodyDecayRate = 4.2;   // larger = faster overall decay

            // Strike transient: a short filtered-noise burst at onset (hammer knock).
            const double strikeSeconds = 0.010;
            const int    strikeSamples = jmax (1, (int) (strikeSeconds * kSampleRate));
            float strikeLpState = 0.0f;         // one-pole low-pass state for the noise burst

            for (int i = 0; i < numSamples; ++i)
            {
                const double t = (double) i / kSampleRate;

                // --- additive partials ---
                double sample = 0.0;
                for (int p = 1; p <= kNumPartials; ++p)
                {
                    const double partialFreq = p * f0 * std::sqrt (1.0 + B * (double) (p * p));
                    if (partialFreq >= kSampleRate * 0.5)   // skip anything past Nyquist
                        break;

                    // Higher partials are quieter and decay faster.
                    const double partialGain  = 1.0 / (double) p;
                    const double partialDecay = bodyDecayRate * (1.0 + 0.35 * (double) (p - 1));
                    const double env          = std::exp (-partialDecay * t);

                    sample += partialGain * env
                                * std::sin (MathConstants<double>::twoPi * partialFreq * t);
                }

                // --- overall amp envelope (attack ramp * exponential body decay) ---
                double amp = std::exp (-bodyDecayRate * t);
                if (i < attackSamples)
                    amp *= (double) i / (double) attackSamples;

                double value = sample * amp;

                // --- seeded strike transient (hammer knock), low-passed and fast-decaying ---
                if (i < strikeSamples)
                {
                    const float noise = rng.nextBipolar();
                    strikeLpState += 0.35f * (noise - strikeLpState);   // one-pole LP
                    const double strikeEnv = std::exp (-40.0 * t);      // ~25 ms decay
                    value += 0.25 * strikeLpState * strikeEnv;
                }

                out[i] = (float) value;
            }

            // Normalise to -1 dBFS-ish headroom so the Sampler + downstream chain never clips.
            normalisePeak (buffer, 0.89f);
        }

        //==============================================================================
        // The persistent library file. Forge already owns %APPDATA%\Forge for logs, so a sibling
        // "library" dir is idiomatic and user-writable without elevation.
        File libraryDir()
        {
            return File::getSpecialLocation (File::userApplicationDataDirectory)
                       .getChildFile ("Forge")
                       .getChildFile ("library");
        }

        File pianoFile()
        {
            return libraryDir().getChildFile ("piano.wav");
        }

        // A file is "valid enough" to reuse if it exists and is non-trivially sized (a truncated or
        // zero-byte file from an earlier failed write must be regenerated, not trusted).
        bool looksValid (const File& f)
        {
            return f.existsAsFile() && f.getSize() > 1024;
        }

        //==============================================================================
        // Writes channel 0 of `buffer` as a mono 44.1 kHz 16-bit WAV to `target`, via a temp sibling
        // then a move — so a crash mid-write never leaves a half-file that looksValid() would later
        // trust. Returns true on success; logs + returns false on any fallible seam. Shared by the
        // piano one-shot and every drum voice (same WavAudioFormat recipe main.cpp's createSineWaveFile
        // uses). The written bytes are a pure function of `buffer` + the fixed 44100/1/16 format.
        bool writeMonoWav (const File& target, const AudioBuffer<float>& buffer)
        {
            const int  numSamples = buffer.getNumSamples();
            const File tmp = target.getSiblingFile (target.getFileName() + ".tmp");
            tmp.deleteFile();

            if (auto outStream = tmp.createOutputStream())
            {
                WavAudioFormat wavFormat;

                if (auto* writer = wavFormat.createWriterFor (outStream.get(), kSampleRate, 1, 16, {}, 0))
                {
                    outStream.release();
                    std::unique_ptr<AudioFormatWriter> writerOwner (writer);

                    if (! writerOwner->writeFromAudioSampleBuffer (buffer, 0, numSamples))
                    {
                        FORGE_LOG_ERROR ("Failed to write samples to " + tmp.getFullPathName()
                                         + " — I/O error or disk full");
                        writerOwner.reset();
                        tmp.deleteFile();
                        return false;
                    }
                }
                else
                {
                    FORGE_LOG_ERROR ("Failed to create WAV writer for " + tmp.getFullPathName());
                    return false;
                }
            }
            else
            {
                FORGE_LOG_ERROR ("Failed to create output stream for " + tmp.getFullPathName()
                                 + " (createOutputStream returned null)");
                return false;
            }

            target.deleteFile();
            if (! tmp.moveFileTo (target))
            {
                FORGE_LOG_ERROR ("Failed to move rendered WAV into place at " + target.getFullPathName());
                tmp.deleteFile();
                return false;
            }

            return true;
        }

        //==============================================================================
        // ---- Drum-kit voices ----------------------------------------------------------------------
        //
        // Eight self-rendered CC0 one-shots, one per GM drum note a StepClip channel triggers. Each is
        // a pure function of fixed constants + a per-voice-seeded Xorshift32 (same determinism contract
        // as the piano). Character over accuracy: they must READ as a kit, not model one sample-exactly.
        // A render fn writes RAW signal into channel 0; ensureDrumKit() normalises (per-voice target)
        // then writes it. No wall-clock, no device, no juce::Random.

        // Exponential decay envelope: 1.0 at t=0, ~exp(-t/tau); ~0.05 at t = 3*tau.
        inline double expDecay (double t, double tau) noexcept
        {
            return std::exp (-t / jmax (1.0e-6, tau));
        }

        // 36 — Kick: a sine gliding ~90 -> 45 Hz over ~40 ms, ~0.30 s exponential amp decay, plus a
        // short low-passed-noise click at the onset for the beater "knock".
        void renderKick (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0x4B1C0024u);

            const double startHz = 90.0, endHz = 45.0, sweepSeconds = 0.040;
            const int    clickSamples = jmax (1, (int) (0.005 * kSampleRate));
            float  clickLp = 0.0f;
            double phase = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                const double t    = (double) i / kSampleRate;
                const double k    = jmin (1.0, t / sweepSeconds);
                const double freq = startHz * std::pow (endHz / startHz, k);   // exp glide, then hold
                phase += MathConstants<double>::twoPi * freq / kSampleRate;

                double v = std::sin (phase) * expDecay (t, 0.10);              // body (~0.30 s)

                if (i < clickSamples)
                {
                    clickLp += 0.5f * (rng.nextBipolar() - clickLp);          // one-pole LP on noise
                    v += 0.6 * clickLp * expDecay (t, 0.002);
                }

                out[i] = (float) v;
            }
        }

        // 38 — Snare: a high-passed noise body (the rattle) plus two tonal partials (~180/330 Hz shell
        // + head modes), ~0.20 s decay.
        void renderSnare (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0x5EA12026u);

            float hpPrev = 0.0f, xPrev = 0.0f;
            const double f1 = 180.0, f2 = 330.0;

            for (int i = 0; i < numSamples; ++i)
            {
                const double t  = (double) i / kSampleRate;
                const float  x  = rng.nextBipolar();
                const float  hp = 0.85f * (hpPrev + x - xPrev);               // one-pole high-pass
                hpPrev = hp; xPrev = x;

                double v = 0.70 * hp * expDecay (t, 0.065);                    // rattle (~0.20 s)
                v += 0.35 * std::sin (MathConstants<double>::twoPi * f1 * t) * expDecay (t, 0.07);
                v += 0.25 * std::sin (MathConstants<double>::twoPi * f2 * t) * expDecay (t, 0.05);

                out[i] = (float) v;
            }
        }

        // 42 — Closed hat: first-difference (bright, +6 dB/oct) noise with a very short ~0.05 s decay.
        void renderClosedHat (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0xC105ED2Au);

            float xPrev = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const double t  = (double) i / kSampleRate;
                const float  x  = rng.nextBipolar();
                const float  hp = x - xPrev;                                  // bright high-pass
                xPrev = x;
                out[i] = (float) (hp * expDecay (t, 0.014));                  // ~0.05 s sharp decay
            }
        }

        // 46 — Open hat: the same bright noise, held far longer (~0.30 s decay).
        void renderOpenHat (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0x09E12E2Eu);

            float xPrev = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const double t  = (double) i / kSampleRate;
                const float  x  = rng.nextBipolar();
                const float  hp = x - xPrev;
                xPrev = x;
                out[i] = (float) (hp * expDecay (t, 0.10));                   // ~0.30 s decay
            }
        }

        // 39 — Clap: three short staggered noise bursts ~12 ms apart, then a softer diffuse ~0.12 s
        // noise tail (the classic clap "spread").
        void renderClap (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0xC1AF0027u);

            const double burstSpacing = 0.012;   // ~12 ms apart
            const int    numBursts    = 3;
            const double burstTau     = 0.006;
            const double tailStart    = burstSpacing * numBursts;
            const double tailTau      = 0.040;   // ~0.12 s tail (3*tau)

            float xPrev = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const double t  = (double) i / kSampleRate;
                const float  x  = rng.nextBipolar();
                const float  hp = 0.6f * (x - xPrev) + 0.4f * x;             // mildly bright noise
                xPrev = x;

                double env = 0.0;
                for (int b = 0; b < numBursts; ++b)
                {
                    const double bt = t - (double) b * burstSpacing;
                    if (bt >= 0.0)
                        env = jmax (env, expDecay (bt, burstTau));
                }
                if (t >= tailStart)
                    env = jmax (env, 0.5 * expDecay (t - tailStart, tailTau));

                out[i] = (float) (hp * env);
            }
        }

        // Shared tom body: a sine gliding startHz -> endHz over ~glide seconds (the pitch "drop"), a
        // light 2nd harmonic, and a very short noise attack click. decayTau shapes the tail.
        void renderTom (AudioBuffer<float>& buffer, std::uint32_t seed,
                        double startHz, double endHz, double glideSeconds, double decayTau)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (seed);

            const int clickSamples = jmax (1, (int) (0.003 * kSampleRate));
            float  clickLp = 0.0f;
            double phase = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                const double t    = (double) i / kSampleRate;
                const double k    = jmin (1.0, t / glideSeconds);
                const double freq = startHz + (endHz - startHz) * k;         // linear pitch drop
                phase += MathConstants<double>::twoPi * freq / kSampleRate;

                double v = std::sin (phase) * expDecay (t, decayTau);
                v += 0.20 * std::sin (2.0 * phase) * expDecay (t, decayTau * 0.65);

                if (i < clickSamples)
                {
                    clickLp += 0.5f * (rng.nextBipolar() - clickLp);
                    v += 0.30 * clickLp * expDecay (t, 0.0015);
                }

                out[i] = (float) v;
            }
        }

        // 45 — Low tom: ~120 Hz body with a slight downward pitch env, ~0.35 s.
        void renderLowTom (AudioBuffer<float>& buffer)
        {
            renderTom (buffer, 0x10772D45u, 130.0, 112.0, 0.10, 0.12);
        }

        // 50 — High tom: ~250 Hz body with a pitch env, ~0.28 s.
        void renderHighTom (AudioBuffer<float>& buffer)
        {
            renderTom (buffer, 0x41670032u, 270.0, 235.0, 0.08, 0.093);
        }

        // 51 — Ride: bright first-difference noise shimmer + a few inharmonic metallic partials,
        // long ~0.6 s decay. Sits lower in level than the rest of the kit (quieter target).
        void renderRide (AudioBuffer<float>& buffer)
        {
            const int numSamples = buffer.getNumSamples();
            auto* out = buffer.getWritePointer (0);
            Xorshift32 rng (0x71DE0033u);

            const double p1 = 2100.0, p2 = 3170.0, p3 = 4720.0;              // inharmonic partials
            float xPrev = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const double t  = (double) i / kSampleRate;
                const float  x  = rng.nextBipolar();
                const float  hp = x - xPrev;
                xPrev = x;

                double v = 0.50 * hp * expDecay (t, 0.22);                    // shimmer (~0.6 s)
                v += 0.25 * std::sin (MathConstants<double>::twoPi * p1 * t) * expDecay (t, 0.20);
                v += 0.18 * std::sin (MathConstants<double>::twoPi * p2 * t) * expDecay (t, 0.18);
                v += 0.14 * std::sin (MathConstants<double>::twoPi * p3 * t) * expDecay (t, 0.16);

                out[i] = (float) v;
            }
        }

        //==============================================================================
        // One drum voice: its GM note, display name, buffer length (decay tail + margin), normalise
        // target (~ -1 dBFS = 0.89; quieter voices sit back), and its render fn. In StepClip channel
        // order — the order ensureDrumKit() returns and the Sampler maps sounds in.
        struct DrumVoiceSpec
        {
            int         midiNote;
            const char* name;
            double      seconds;
            float       targetPeak;
            void      (*render) (AudioBuffer<float>&);
        };

        constexpr int kNumDrumVoices = 8;

        const DrumVoiceSpec kDrumVoices[kNumDrumVoices] =
        {
            { 36, "Kick",       0.45, 0.89f, &renderKick      },
            { 38, "Snare",      0.35, 0.89f, &renderSnare     },
            { 42, "Closed Hat", 0.12, 0.85f, &renderClosedHat },
            { 46, "Open Hat",   0.45, 0.85f, &renderOpenHat   },
            { 39, "Clap",       0.30, 0.85f, &renderClap      },
            { 45, "Low Tom",    0.50, 0.89f, &renderLowTom    },
            { 50, "High Tom",   0.42, 0.89f, &renderHighTom   },
            { 51, "Ride",       0.85, 0.55f, &renderRide      },
        };

        File drumFile (int midiNote)
        {
            return libraryDir().getChildFile ("drum_" + juce::String (midiNote) + ".wav");
        }
    } // namespace

    //==============================================================================
    File ensurePianoOneShot()
    {
        const File out = pianoFile();

        // Idempotent: reuse a previously-generated, non-empty file.
        if (looksValid (out))
            return out;

        const File dir = libraryDir();
        const auto dirResult = dir.createDirectory();
        if (dirResult.failed())
        {
            FORGE_LOG_ERROR ("Failed to create instrument library dir " + dir.getFullPathName()
                             + " — " + dirResult.getErrorMessage());
            return {};
        }

        // Synthesize into a mono buffer.
        const int numSamples = (int) (kSeconds * kSampleRate);
        AudioBuffer<float> buffer (1, numSamples);
        buffer.clear();
        renderPiano (buffer);

        // Write the rendered buffer to disk (mono 44.1 kHz 16-bit, temp-then-move) via the shared
        // writer the drum voices also use — same WavAudioFormat recipe main.cpp's createSineWaveFile
        // uses, so the output bytes are unchanged.
        if (! writeMonoWav (out, buffer))
            return {};   // writeMonoWav logged the specific failure

        FORGE_LOG_INFO ("Rendered CC0 piano one-shot to " + out.getFullPathName());
        return out;
    }

    //==============================================================================
    juce::Array<DrumHit> ensureDrumKit()
    {
        juce::Array<DrumHit> hits;

        const File dir = libraryDir();
        const auto dirResult = dir.createDirectory();
        if (dirResult.failed())
        {
            FORGE_LOG_ERROR ("Failed to create instrument library dir " + dir.getFullPathName()
                             + " — " + dirResult.getErrorMessage());
            return hits;   // empty — caller degrades (Sampler inserted but silent)
        }

        for (int i = 0; i < kNumDrumVoices; ++i)
        {
            const auto& spec = kDrumVoices[i];
            const File  out  = drumFile (spec.midiNote);

            // Idempotent: reuse a previously-generated, non-empty voice.
            if (looksValid (out))
            {
                hits.add ({ out, spec.midiNote, juce::String (spec.name) });
                continue;
            }

            // Synthesize into a mono buffer, normalise to this voice's target, then write it.
            const int numSamples = jmax (1, (int) (spec.seconds * kSampleRate));
            AudioBuffer<float> buffer (1, numSamples);
            buffer.clear();
            spec.render (buffer);
            normalisePeak (buffer, spec.targetPeak);

            // Crash-safe PER VOICE: writeMonoWav temp-then-moves, so a failure omits this voice
            // (never a half-written file) — the returned kit holds only voices actually on disk.
            if (! writeMonoWav (out, buffer))
                continue;   // writeMonoWav logged the specific failure

            hits.add ({ out, spec.midiNote, juce::String (spec.name) });
        }

        if (hits.size() == kNumDrumVoices)
            FORGE_LOG_INFO ("Rendered CC0 drum kit (" + juce::String (hits.size()) + " voices) under "
                            + dir.getFullPathName());
        else
            FORGE_LOG_WARN ("Drum kit incomplete: only " + juce::String (hits.size()) + " of "
                            + juce::String (kNumDrumVoices) + " voices available");

        return hits;
    }
}
