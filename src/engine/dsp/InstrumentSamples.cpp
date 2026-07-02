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
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = jmax (peak, std::abs (out[i]));

            if (peak > 1.0e-6f)
            {
                const float norm = 0.89f / peak;   // ~ -1 dBFS
                for (int i = 0; i < numSamples; ++i)
                    out[i] *= norm;
            }
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

        // Write via the same WavAudioFormat recipe main.cpp's createSineWaveFile uses. Write to a temp
        // sibling first, then move into place, so a crash mid-write never leaves a half-file that
        // looksValid() would later trust.
        const File tmp = out.getSiblingFile ("piano.wav.tmp");
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
                    FORGE_LOG_ERROR ("Failed to write piano one-shot samples to "
                                     + tmp.getFullPathName() + " — I/O error or disk full");
                    writerOwner.reset();
                    tmp.deleteFile();
                    return {};
                }
            }
            else
            {
                FORGE_LOG_ERROR ("Failed to create WAV writer for piano one-shot "
                                 + tmp.getFullPathName());
                return {};
            }
        }
        else
        {
            FORGE_LOG_ERROR ("Failed to create output stream for piano one-shot "
                             + tmp.getFullPathName() + " (createOutputStream returned null)");
            return {};
        }

        // Move the completed temp file into place (replacing any stale file).
        out.deleteFile();
        if (! tmp.moveFileTo (out))
        {
            FORGE_LOG_ERROR ("Failed to move rendered piano one-shot into place at "
                             + out.getFullPathName());
            tmp.deleteFile();
            return {};
        }

        FORGE_LOG_INFO ("Rendered CC0 piano one-shot to " + out.getFullPathName());
        return out;
    }
}
